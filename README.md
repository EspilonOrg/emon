# espilon-monitor

**Multi-device embedded serial crash monitor for security research and fuzzing campaigns.**

---

## The Problem

When you're running a fuzzing campaign against embedded devices — 5 boards, 72 hours, thousands of test cases — you need answers fast:

- Which board crashed?
- What triggered it?
- Is it a new crash or the same one again?
- Can I reproduce it?

Existing tools (`minicom`, `screen`, `picocom`) are single-port, dumb terminals. They don't classify crashes, don't deduplicate, don't reset boards, and don't integrate with fuzz tooling. You end up with 5 terminal windows, a notepad, and a lot of missed crashes.

`espilon-monitor` fills that gap.

---

## What it does

```
┌─────────────────────────────────────────────────────────┐
│  espilon-monitor                                        │
│                                                         │
│  [C6-TEE  ] Guru Meditation Error ← CRITICAL           │
│  [ESP32-A ] Backtrace: 0x40...    ← HIGH                │
│  [STM32-B ] HardFault_Handler     ← CRITICAL           │
│  [Arduino ] assert failed         ← HIGH                │
│                                                         │
│  Crashes: 3 unique  |  Uptime: 04:23:11  |  Resets: 2  │
└─────────────────────────────────────────────────────────┘
```

- **Monitors N serial ports simultaneously** — ESP32, STM32, Arduino, RP2040, anything with UART
- **Classifies crashes in real time** — Guru Meditation, HardFault, stack overflow, ASAN errors, watchdog resets, and more
- **Deduplicates** — one alert per unique crash signature, not one per line
- **Records session context** — saves N lines before and after each crash for reproduction
- **Auto-resets boards** — triggers hardware reset via RTS/DTR pin after crash, keeps fuzzing
- **Replays crash inputs** — feeds AFL++ / libFuzzer crash files back to the target
- **Extensible via Python plugins** — add custom patterns, webhooks, integrations in a few lines

---

## Use cases

- **Fuzzing campaigns** — monitor targets 24/7, collect and classify every crash without manual intervention
- **Security research** — spot privilege escalation signals (e.g. `Origin: M-mode` on RISC-V TEE targets), memory corruption, unexpected resets
- **Regression testing** — run a test suite, detect any new crashes against known-good baselines
- **Red team tooling** — monitor victim devices during exploitation, catch crash-based side-channels
- **Development** — replace your 5 terminal windows with a single, intelligent view

---

## Supported devices

Any device with a UART serial output. Built-in pattern libraries for:

| Family | Examples |
|--------|---------|
| ESP-IDF (ESP32) | ESP32, ESP32-C6, ESP32-S3, ESP32-H2 |
| STM32 (CubeIDE) | Any STM32 with HardFault handler output |
| Arduino | Crash/assert output, watchdog resets |
| Zephyr RTOS | Kernel panic, assertion failures |
| FreeRTOS | Stack overflow, heap corruption |
| Bare metal | Configurable — bring your own patterns |

---

## Architecture

```
espilon-monitor/
├── src/
│   ├── serial.c/.h      # Multi-port serial I/O (libserialport)
│   ├── detector.c/.h    # Pattern matching engine (POSIX regex)
│   ├── monitor.c/.h     # Main monitoring loop (pthreads)
│   ├── recorder.c/.h    # Session recording + crash context
│   ├── reset.c/.h       # Hardware reset via RTS/DTR
│   ├── display.c/.h     # Terminal output (color, layout)
│   └── config.c/.h      # Config file parsing
│
├── patterns/            # Pattern definitions per device family
│   ├── esp32.pat
│   ├── stm32.pat
│   ├── arduino.pat
│   └── freertos.pat
│
└── plugins/             # Python — custom rules, webhooks, integrations
    ├── webhooks.py
    └── replay.py
```

**C core** — reliability matters on long fuzzing runs. No GC pauses, no interpreter overhead, direct `libserialport` calls, `pthreads` per port.

**Python plugins** — researchers know Python. Adding a Discord webhook or a custom crash pattern should take 5 lines, not a recompile.

**Pattern files** — plain text, one rule per line, editable without touching code:
```
# patterns/esp32.pat
CRITICAL  Guru Meditation Error
CRITICAL  Origin: M-mode
CRITICAL  Core \d+ panic'ed
HIGH      abort\(\) was called at PC
HIGH      stack overflow detected
WARN      W \([0-9]+\)
```

---

## Quick start

```bash
# Build
make

# Monitor 3 devices
./espilon-monitor --ports ttyACM0 ttyUSB0 ttyUSB1

# With custom patterns and auto-reset on CRITICAL
./espilon-monitor --ports ttyACM0 --patterns patterns/esp32.pat --auto-reset

# Replay AFL++ crash files to a target
./espilon-monitor --ports ttyUSB0 --replay /path/to/findings/crashes/
```

---

## Dependencies

| Dependency | Purpose | Required |
|-----------|---------|---------|
| `libserialport` | Cross-platform serial I/O | Yes |
| `pthreads` | Per-port threads | Built-in |
| `regex.h` | POSIX pattern matching | Built-in |
| `ncurses` | TUI dashboard | Optional |
| Python 3.8+ | Plugin system | Optional |

---

## Status

Work in progress. Core serial engine and crash detector are the first milestones.

---

## Author

[Eun0us](https://github.com/Eun0us) — [Espilon](https://github.com/espilon)
