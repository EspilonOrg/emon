#!/usr/bin/env python3
"""
espilon-monitor webhook notifier

Tails *_events.log files produced by espilon-monitor and forwards each
detected event to an HTTP endpoint.

Supported backends:
  ntfy.sh   (default)  --url https://ntfy.sh/my-topic
  Slack                --url https://hooks.slack.com/services/...
  Discord              --url https://discord.com/api/webhooks/...
  Custom               Any HTTP endpoint that accepts POST

Usage:
  python3 plugins/webhooks.py --logdir /tmp/monitor-logs --url https://ntfy.sh/my-topic
  python3 plugins/webhooks.py --logdir /tmp/monitor-logs --url https://hooks.slack.com/...
  python3 plugins/webhooks.py --help
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.request
import urllib.error
from datetime import datetime
from pathlib import Path

# ── Backends ──────────────────────────────────────────────────────────────────

SEV_EMOJI = {
    "CRITICAL": "🔴",
    "HIGH":     "🟠",
    "WARN":     "🟡",
    "INFO":     "🟢",
}

def _post_json(url: str, headers: dict, body: dict) -> bool:
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            return resp.status < 300
    except urllib.error.URLError as e:
        print(f"[webhook] POST failed: {e}", file=sys.stderr)
        return False


def send_ntfy(url: str, token: str | None, device: str, rule: str,
              severity: str, line: str) -> bool:
    emoji = SEV_EMOJI.get(severity, "⚪")
    headers = {
        "Content-Type": "application/json",
        "Title":        f"{emoji} [{device}] {rule}",
        "Priority":     "5" if severity == "CRITICAL" else "3",
        "Tags":         f"warning,{severity.lower()}",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    body = {"message": line[:1000]}
    return _post_json(url, headers, body)


def send_slack(url: str, device: str, rule: str, severity: str, line: str) -> bool:
    emoji = SEV_EMOJI.get(severity, "⚪")
    body = {
        "text": f"{emoji} *[{device}] {rule}* ({severity})\n```{line[:500]}```"
    }
    return _post_json(url, {"Content-Type": "application/json"}, body)


def send_discord(url: str, device: str, rule: str, severity: str, line: str) -> bool:
    emoji = SEV_EMOJI.get(severity, "⚪")
    body = {
        "content": f"{emoji} **[{device}] {rule}** ({severity})\n```{line[:500]}```"
    }
    return _post_json(url, {"Content-Type": "application/json"}, body)


def send_generic(url: str, device: str, rule: str, severity: str, line: str) -> bool:
    body = {
        "device":    device,
        "rule":      rule,
        "severity":  severity,
        "line":      line,
        "timestamp": datetime.utcnow().isoformat() + "Z",
    }
    return _post_json(url, {"Content-Type": "application/json"}, body)


def dispatch(url: str, token: str | None, device: str, rule: str,
             severity: str, line: str) -> bool:
    if "ntfy.sh" in url or ("ntfy" in url and "hooks" not in url):
        return send_ntfy(url, token, device, rule, severity, line)
    elif "slack.com" in url:
        return send_slack(url, device, rule, severity, line)
    elif "discord.com" in url:
        return send_discord(url, device, rule, severity, line)
    else:
        return send_generic(url, device, rule, severity, line)


# ── Event log parser ──────────────────────────────────────────────────────────

# Lines from recorder_save_event look like:
#   ║ EVENT: GURU_MEDITATION                ║
#   ║ SEV:   CRITICAL                       ║
# followed by context lines then:
#   >> <actual crashing line>

_RE_EVENT = re.compile(r"║ EVENT: (.+?)[ │║]+$")
_RE_SEV   = re.compile(r"║ SEV:\s+(\w+)")
_RE_LINE  = re.compile(r"^>> (.+)")


class EventLogTailer:
    def __init__(self, path: Path):
        self.path = path
        self.device = path.stem.replace("_events", "")
        self._fh = None
        self._pending_event: str | None = None
        self._pending_sev:   str | None = None

    def open(self):
        self._fh = open(self.path, "r", errors="replace")
        self._fh.seek(0, 2)  # start at end

    def poll(self):
        """Yield (device, rule, severity, line) tuples for new events."""
        if not self._fh:
            return
        while True:
            raw = self._fh.readline()
            if not raw:
                break
            raw = raw.rstrip()
            m = _RE_EVENT.search(raw)
            if m:
                self._pending_event = m.group(1).strip()
                self._pending_sev   = None
                continue
            m = _RE_SEV.search(raw)
            if m and self._pending_event:
                self._pending_sev = m.group(1).strip()
                continue
            m = _RE_LINE.match(raw)
            if m and self._pending_event and self._pending_sev:
                yield (self.device, self._pending_event,
                       self._pending_sev, m.group(1))
                self._pending_event = None
                self._pending_sev   = None

    def close(self):
        if self._fh:
            self._fh.close()
            self._fh = None


# ── Main loop ─────────────────────────────────────────────────────────────────

def run(logdir: str, url: str, token: str | None,
        poll_interval: float, min_severity: str):
    sev_rank = {"INFO": 0, "WARN": 1, "HIGH": 2, "CRITICAL": 3}
    min_rank = sev_rank.get(min_severity.upper(), 1)

    logpath = Path(logdir)
    tailers: dict[Path, EventLogTailer] = {}

    print(f"[webhooks] watching {logdir}  →  {url}")
    if min_severity != "INFO":
        print(f"[webhooks] filtering: only {min_severity}+")

    while True:
        # Discover new *_events.log files
        for p in logpath.glob("*_events.log"):
            if p not in tailers:
                t = EventLogTailer(p)
                t.open()
                tailers[p] = t
                print(f"[webhooks] tracking {p.name}")

        for tailer in list(tailers.values()):
            for device, rule, severity, line in tailer.poll():
                rank = sev_rank.get(severity, 0)
                if rank < min_rank:
                    continue
                ts = datetime.now().strftime("%H:%M:%S")
                print(f"[{ts}] {SEV_EMOJI.get(severity,'?')} [{device}] {rule}  {line[:80]}")
                ok = dispatch(url, token, device, rule, severity, line)
                if not ok:
                    print(f"  ↳ delivery failed", file=sys.stderr)

        time.sleep(poll_interval)


def main():
    ap = argparse.ArgumentParser(
        description="Forward espilon-monitor events to a webhook")
    ap.add_argument("--logdir",       default="/tmp/espilon-logs",
                    help="directory containing *_events.log files (default: /tmp/espilon-logs)")
    ap.add_argument("--url",          required=True,
                    help="webhook URL (ntfy.sh/topic, Slack, Discord, or custom)")
    ap.add_argument("--token",        default=None,
                    help="auth token (Bearer, used for ntfy.sh)")
    ap.add_argument("--min-severity", default="WARN",
                    choices=["INFO", "WARN", "HIGH", "CRITICAL"],
                    help="minimum severity to forward (default: WARN)")
    ap.add_argument("--interval",     type=float, default=1.0,
                    help="poll interval in seconds (default: 1.0)")
    args = ap.parse_args()

    try:
        run(args.logdir, args.url, args.token, args.interval, args.min_severity)
    except KeyboardInterrupt:
        print("\n[webhooks] stopped")


if __name__ == "__main__":
    main()
