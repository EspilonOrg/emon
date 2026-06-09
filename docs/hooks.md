# Event hooks (`--on-event`)

emon can call a Python 3 script every time a pattern rule fires. The script receives a JSON payload on stdin and can do anything: send an alert, log to a database, trigger a CI step, reboot a device via SSH.

## Usage

```bash
emon --on-event hooks/alert.py --patterns patterns/esp32.pat /dev/ttyUSB0

# Multiple hooks
emon --on-event hooks/alert.py --on-event hooks/slack.py /dev/ttyUSB0
```

Or in `.emon.conf`:

```ini
on_event = hooks/alert.py
on_event = hooks/slack.py
```

Up to 8 hooks per session.

## JSON payload

Each hook receives one JSON object, followed by a newline, on stdin:

```json
{
  "rule":     "GURU_MEDITATION",
  "severity": "CRITICAL",
  "device":   "ttyUSB0",
  "line":     "Guru Meditation Error: Core 0 panic'd (LoadProhibited). Exception was unhandled.",
  "ts":       1718000000123
}
```

| Field | Type | Description |
|---|---|---|
| `rule` | string | Pattern name (uppercase, e.g. `STACK_OVERFLOW`) |
| `severity` | string | `CRITICAL` / `HIGH` / `WARN` / `INFO` |
| `device` | string | Port name or friendly name if `--name` was used |
| `line` | string | The raw serial line that matched |
| `ts` | integer | Unix timestamp in milliseconds |

## Execution model

- The hook is called via `python3 <script>` - the script must be executable or importable by Python 3.
- The monitor **never blocks**: each hook runs as a fire-and-forget grandchild process (double-fork). If the script takes 10 seconds, emon keeps monitoring.
- If the script exits with a non-zero code, emon does not notice - hook failures are silent by design. Log inside the script if you need to debug.
- Hooks are called in registration order, each independently.

## Examples

### Minimal - print to stderr

```python
#!/usr/bin/env python3
import json, sys

ev = json.load(sys.stdin)
print(f"[{ev['severity']}] {ev['device']}: {ev['rule']} - {ev['line']}", file=sys.stderr)
```

### Alert on CRITICAL only

```python
#!/usr/bin/env python3
import json, sys

ev = json.load(sys.stdin)
if ev["severity"] != "CRITICAL":
    sys.exit(0)

# your alert logic here
print(f"CRITICAL on {ev['device']}: {ev['rule']}")
```

### ntfy.sh push notification

```python
#!/usr/bin/env python3
import json, sys, urllib.request

NTFY_TOPIC = "https://ntfy.sh/my-emon-alerts"

ev = json.load(sys.stdin)
body = f"{ev['device']}: {ev['line'][:200]}".encode()
req = urllib.request.Request(
    NTFY_TOPIC,
    data=body,
    headers={
        "Title": f"[{ev['severity']}] {ev['rule']}",
        "Priority": "urgent" if ev["severity"] == "CRITICAL" else "default",
    },
)
urllib.request.urlopen(req, timeout=5)
```

### Slack webhook

```python
#!/usr/bin/env python3
import json, sys, urllib.request

SLACK_URL = "https://hooks.slack.com/services/YOUR/WEBHOOK/URL"

ev = json.load(sys.stdin)
color = {"CRITICAL": "#FF0000", "HIGH": "#FF8800", "WARN": "#FFCC00"}.get(ev["severity"], "#999999")
payload = json.dumps({
    "attachments": [{
        "color": color,
        "title": f"[{ev['severity']}] {ev['rule']} on {ev['device']}",
        "text": ev["line"],
        "footer": f"emon | ts {ev['ts']}",
    }]
}).encode()
req = urllib.request.Request(SLACK_URL, data=payload,
                              headers={"Content-Type": "application/json"})
urllib.request.urlopen(req, timeout=5)
```

### Write to a database (SQLite)

```python
#!/usr/bin/env python3
import json, sys, sqlite3, pathlib

DB = pathlib.Path("/var/log/emon/events.db")
DB.parent.mkdir(parents=True, exist_ok=True)

ev = json.load(sys.stdin)
con = sqlite3.connect(DB)
con.execute("""
    CREATE TABLE IF NOT EXISTS events
    (ts INTEGER, device TEXT, rule TEXT, severity TEXT, line TEXT)
""")
con.execute("INSERT INTO events VALUES (?,?,?,?,?)",
            (ev["ts"], ev["device"], ev["rule"], ev["severity"], ev["line"]))
con.commit()
con.close()
```

## Tips

- Use `sys.exit(0)` to return early when severity doesn't match - the process cost is negligible.
- Don't `import time; time.sleep(...)` inside a hook - it only blocks that subprocess, not the monitor, but it wastes resources.
- For high-frequency events (e.g. WARN every second), add dedup logic in your script (file-based lock or SQLite) to avoid flooding your notification channel.
