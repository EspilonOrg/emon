# emon

![emon banner](img/emon-banner.png)

[![CI](https://github.com/EspilonOrg/espilon-monitor/actions/workflows/ci.yml/badge.svg)](https://github.com/EspilonOrg/espilon-monitor/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

**Universal serial monitor for embedded devices.**

Monitor any number of serial ports simultaneously, detect events by pattern, log everything, and react automatically. One tool for development, CI, fleet monitoring, and security research.

---

## Why emon

`screen` and `minicom` are great for one board. With five boards, they fall apart: you miss events, lose context, and have no way to act on what you see.

emon watches all your devices at once, tells you what matters, and gets out of the way.

---

## Features

| | |
|---|---|
| **Multi-port** | One thread per device, any number of ports |
| **Pattern detection** | POSIX regex rules, four severity levels, O(1) dedup |
| **Structured logging** | Per-device timestamped logs with rolling rotation |
| **Event context** | Captures lines before and after each trigger |
| **Hex dump** | Raw byte inspection with ASCII column (`--hex`) |
| **Auto-reset** | Assert RTS/DTR on CRITICAL events automatically |
| **Flow control** | none / rtscts / xonxoff via libserialport |
| **Interactive mode** | Send commands to any device from the monitor |
| **Split-pane TUI** | One pane per device, native ANSI, no ncurses |
| **CI integration** | `--wait-for RULE --timeout N` exits with the right code |
| **Event hooks** | Call a Python script on every match (`--on-event`) |
| **Config file** | All flags available in an INI config |

---

## Quick start

```bash
make
PREFIX=~/.local make install   # no sudo needed
```

```bash
# Basic monitoring
emon /dev/ttyUSB0
emon /dev/ttyACM0 /dev/ttyUSB0 /dev/ttyUSB1

# Auto-detect chip family, log to disk
emon --auto-patterns patterns/ --logdir ./logs /dev/ttyUSB0

# Split-pane TUI
emon --tui /dev/ttyUSB0 /dev/ttyUSB1

# Hex dump (binary protocols, raw inspection)
emon --hex /dev/ttyUSB0

# CI: wait for boot, exit 0 on success, 124 on timeout
emon --wait-for BOOT_OK --timeout 30 /dev/ttyUSB0

# Background daemon
emon --bg --logdir /opt/logs /dev/ttyUSB1
emon stop

# Event hook: call a script on every match
emon --on-event hooks/alert.py --patterns patterns/esp32.pat /dev/ttyUSB0
```

---

## Pattern files

Plain text, no recompile needed:

```
# patterns/esp32.pat
CRITICAL  GURU_MEDITATION   Guru Meditation Error
CRITICAL  ABORT             abort\(\) was called
HIGH      STACK_OVERFLOW    stack overflow
WARN      RESET             rst:0x
INFO      BOOT              I \([0-9]+\) boot:
```

Format: `SEVERITY  NAME  REGEX`

Built-in families: `esp32`, `stm32`, `arduino`, `freertos`, `zephyr`, `esp-idf`, `exploit`.

Load with `--patterns file.pat` or use `--auto-patterns patterns/` to auto-detect from USB VID/PID.

See [CONTRIBUTING.md](CONTRIBUTING.md) to add a new family (no C required).

---

## Config file

```ini
# .emon.conf  --  load with: emon --config .emon.conf /dev/ttyUSB0
baud         = 115200
flow_control = none        # none | rtscts | xonxoff
logdir       = ./logs
context      = 10
hex          = false
tui          = false

pattern      = patterns/esp32.pat   # additive, repeat as needed
on_event     = hooks/alert.py       # additive, up to 8 hooks
```

Full reference with every key documented: [`.emon.conf.example`](.emon.conf.example)

---

## Event hooks

When a rule fires, emon calls your script with a JSON payload on stdin:

```json
{ "rule": "GURU_MEDITATION", "severity": "CRITICAL",
  "device": "ttyUSB0", "line": "...", "ts": 1718000000000 }
```

```python
# hooks/alert.py
import json, sys
ev = json.load(sys.stdin)
if ev["severity"] == "CRITICAL":
    print(f"[ALERT] {ev['device']}: {ev['rule']}")
```

Fire-and-forget (double-fork) - the monitor is never blocked. Up to 8 hooks per session.

See [docs/hooks.md](docs/hooks.md) for the payload schema and examples (ntfy, Slack, Discord, SQLite).

---

## Architecture

```
src/
  main.c              entry point, CLI, signal handling
  app/                config parsing, daemon mode
  monitor/            main loop, pattern detector, logger
  serial/             libserialport I/O, USB auto-detect, reset
  ui/                 display, TUI, hex dump, scrollback, interactive
patterns/             .pat rule files per device family
vendor/               vendored libserialport (no system install needed)
docs/                 man page, hook reference
tests/                unit tests, hardware test harness
```

One pthread per port. libserialport vendored as a static build. Pattern detection uses POSIX regex with an open-addressing hash set for O(1) deduplication.

---

## Dependencies

| Dependency | Purpose | Status |
|---|---|---|
| gcc + make | Build | Required |
| libserialport | Serial I/O | Vendored |
| pthreads, regex.h | Threading, patterns | POSIX built-in |
| python3 | `--on-event` hooks | Optional |

---

## License

Apache 2.0 - see [LICENSE](LICENSE).

Part of the [Espilon Association](https://espilon.net) open source ecosystem.
