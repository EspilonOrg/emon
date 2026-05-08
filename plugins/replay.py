#!/usr/bin/env python3
"""
espilon-monitor session replayer

Reads a captured .log file (produced by espilon-monitor's recorder) and
replays it to stdout or to a serial port, optionally at original speed.

Use-cases:
  - Replay a crash session to a new firmware to verify reproducibility
  - Feed captured traffic back through the detector offline
  - Slow-motion review of a long session

Usage:
  # Replay to stdout at 10× speed
  python3 plugins/replay.py /tmp/logs/VIC.log

  # Write back to a serial port at original speed
  python3 plugins/replay.py /tmp/logs/VIC.log --port /dev/ttyUSB1 --speed 1.0

  # Feed through detector rules to re-score an old session
  python3 plugins/replay.py /tmp/logs/VIC.log --detect --patterns ../patterns/esp32.pat
"""

import argparse
import re
import sys
import time
from pathlib import Path

# ── Log line format ───────────────────────────────────────────────────────────
# Recorder writes: [HH:MM:SS.mmm] <content>
_RE_TS = re.compile(r"^\[(\d{2}):(\d{2}):(\d{2})\.(\d{3})\] (.*)$")

def parse_log(path: Path):
    """Yield (ts_ms, text) tuples from a recorder .log file."""
    with open(path, "r", errors="replace") as f:
        for raw in f:
            raw = raw.rstrip("\n")
            m = _RE_TS.match(raw)
            if not m:
                continue
            h, mi, s, ms = (int(m.group(i)) for i in range(1, 5))
            ts = ((h * 3600 + mi * 60 + s) * 1000) + ms
            yield ts, m.group(5)


def replay_stdout(path: Path, speed: float):
    prev_ts = None
    for ts, text in parse_log(path):
        if prev_ts is not None and speed > 0:
            delta = (ts - prev_ts) / 1000.0 / speed
            if 0 < delta < 5.0:
                time.sleep(delta)
        print(text)
        sys.stdout.flush()
        prev_ts = ts


def replay_serial(path: Path, port_path: str, baud: int, speed: float):
    try:
        import serial
    except ImportError:
        print("error: pyserial not installed — pip install pyserial", file=sys.stderr)
        sys.exit(1)

    ser = serial.Serial(port_path, baud, timeout=1)
    print(f"[replay] opened {port_path} @ {baud}")

    prev_ts = None
    count = 0
    for ts, text in parse_log(path):
        if prev_ts is not None and speed > 0:
            delta = (ts - prev_ts) / 1000.0 / speed
            if 0 < delta < 5.0:
                time.sleep(delta)
        line = (text + "\n").encode(errors="replace")
        ser.write(line)
        count += 1
        prev_ts = ts

    ser.close()
    print(f"[replay] sent {count} lines to {port_path}")


def replay_detect(path: Path, pattern_files: list[str]):
    """Re-run detector patterns over the log and print matches."""
    rules = []
    for pf in pattern_files:
        try:
            with open(pf) as f:
                for raw in f:
                    raw = raw.strip()
                    if not raw or raw.startswith("#"):
                        continue
                    parts = raw.split(None, 2)
                    if len(parts) < 3:
                        continue
                    sev, name, pat = parts[0], parts[1], parts[2]
                    try:
                        import re as re_mod
                        compiled = re_mod.compile(pat, re_mod.IGNORECASE)
                        rules.append((sev, name, compiled))
                    except re_mod.error:
                        pass
        except FileNotFoundError:
            print(f"[detect] pattern file not found: {pf}", file=sys.stderr)

    if not rules:
        print("[detect] no rules loaded", file=sys.stderr)
        return

    SEV_COLOR = {
        "CRITICAL": "\033[1;31m",
        "HIGH":     "\033[33m",
        "WARN":     "\033[93m",
        "INFO":     "\033[36m",
    }
    RESET = "\033[0m"

    hits = 0
    for ts, text in parse_log(path):
        for sev, name, rx in rules:
            if rx.search(text):
                color = SEV_COLOR.get(sev, "")
                print(f"{color}[{sev:8s}] {name}: {text}{RESET}")
                hits += 1
                break

    print(f"\n[detect] {hits} match(es) found")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Replay espilon-monitor session logs")
    ap.add_argument("logfile", help="path to .log file produced by espilon-monitor")
    ap.add_argument("--port",     default=None,
                    help="serial port to write to (e.g. /dev/ttyUSB1)")
    ap.add_argument("--baud",     type=int, default=115200,
                    help="baud rate when writing to serial (default: 115200)")
    ap.add_argument("--speed",    type=float, default=0.0,
                    help="playback speed multiplier (0=max, 1=real-time, 2=2×, etc.)")
    ap.add_argument("--detect",   action="store_true",
                    help="re-run detector patterns over the log instead of replaying")
    ap.add_argument("--patterns", nargs="+", default=[],
                    help="one or more .pat files for --detect mode")
    args = ap.parse_args()

    path = Path(args.logfile)
    if not path.is_file():
        print(f"error: {path} not found", file=sys.stderr)
        sys.exit(1)

    if args.detect:
        replay_detect(path, args.patterns)
    elif args.port:
        replay_serial(path, args.port, args.baud, args.speed)
    else:
        replay_stdout(path, args.speed)


if __name__ == "__main__":
    main()
