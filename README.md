# emon

<img src="img/emon-banner.png" width="100%" alt="emon banner"/>

**Universal serial monitor for embedded devices.**

Monitor any number of serial devices simultaneously: production boards, development kits, test rigs, from a single terminal.

---

## The Problem

You have boards. They output things on serial. You need to know what's happening.

With one board and one terminal, `screen` works fine. With five boards running 24/7, it breaks down: missed events, no logging, no alerting, no context when something goes wrong.

`emon` gives you a unified view of all your devices, with the intelligence to tell you what matters.

---

## What it does

```
emon  4 ports
-----------------------------------------

[bot-alpha ] online @ 115200 baud  [family: espilon]
[bot-beta  ] online @ 115200 baud  [family: espilon]
[dev-board ] online @ 115200 baud  [family: esp32]
[target-ble] online @ 115200 baud  [family: esp32]

12:04:31.042 [sensor-01 ] Temperature: 23.4C  humidity: 61%
12:04:32.198 [sensor-02 ] Motion detected on GPIO4
12:04:33.571 [esp32-dev ] [CRITICAL] Guru Meditation Error: Core 0 panic'd
12:04:33.571 [esp32-dev ]   !! GURU_MEDITATION - esp32-dev
12:04:34.802 [stm32-board] [HIGH] HardFault_Handler called
```

- **N ports, one view**: read from any number of serial devices at the same time
- **Pattern detection**: define what "interesting" looks like per device or globally
- **Severity levels**: CRITICAL, HIGH, WARN, INFO, filter the noise
- **Full logging**: every byte, timestamped, per device
- **Event context**: when something triggers an alert, capture what happened around it
- **Auto-reset**: send a reset signal (RTS/DTR) to a device on a specific event
- **Input injection**: send commands or payloads to any device from the monitor
- **Extensible**: add custom behavior via Python plugins (webhooks, triggers, parsers)

---

## Use cases

**Operations / fleet management**
Monitor a fleet of ESP32 devices running autonomous tasks. Get alerted when one goes silent, crashes, or outputs an unexpected state. Log everything for post-mortem analysis.

**Development**
Replace your 5 terminal windows with a single view. See all your boards at once during a test session. Spot regressions instantly.

**Security research**
Monitor embedded targets during security assessments. Classify faults in real time, auto-reset and continue. Detect anomalies (stack dumps, fault handlers, unexpected resets).

**Test automation**
Feed inputs to devices, watch for expected outputs, flag anything outside spec.

---

## Supported devices

Anything with a UART serial output. Built-in event patterns for:

| Family | Examples |
|--------|---------|
| ESP-IDF | ESP32, ESP32-C6, ESP32-S3, ESP32-H2 |
| STM32 | Any STM32 with standard fault output |
| Arduino | AVR, ARM, ESP8266 |
| Zephyr RTOS | Kernel panics, assertions |
| FreeRTOS | Stack overflow, heap corruption |
| Custom | Define your own patterns |

---

## Quick start

```bash
# Requirements: gcc, make (Linux / macOS)
make

# Monitor a device
emon /dev/ttyUSB0

# Monitor 3 devices at once
emon /dev/ttyACM0 /dev/ttyUSB0 /dev/ttyUSB1

# Name your devices
emon --name /dev/ttyACM0=alpha --name /dev/ttyUSB0=beta /dev/ttyACM0 /dev/ttyUSB0

# Auto-detect family, log to disk
emon --logdir ./logs /dev/ttyUSB0

# Background daemon
emon --bg --logdir /opt/logs /dev/ttyUSB1

# Interactive: send input to device (Ctrl+A X to quit, Ctrl+A [ for scrollback)
emon -i /dev/ttyUSB0

# CI/test runner: wait for pattern, exit 0 on success / 124 on timeout
emon --wait-for BOOT_COMPLETE --timeout 30 /dev/ttyUSB0

# Machine-readable JSON event stream
emon --json-events --exit-on GURU_MEDITATION /dev/ttyUSB0

# Stop a running background daemon
emon stop
```

---

## Architecture

```
emon/
├── src/
│   ├── main.c           # Entry point, CLI parsing
│   ├── serial.c/.h      # Multi-port I/O (libserialport)
│   ├── detector.c/.h    # Pattern matching (POSIX regex)
│   ├── monitor.c/.h     # Main loop (pthreads, one thread per port)
│   ├── recorder.c/.h    # Logging + event context capture
│   ├── reset.c/.h       # Hardware reset via RTS/DTR
│   ├── display.c/.h     # Terminal output
│   ├── config.c/.h      # Config file parsing
│   ├── daemon.c/.h      # Background daemon mode
│   ├── interactive.c/.h # Input injection mode
│   ├── scrollback.c/.h  # Scrollback buffer
│   └── tui.c/.h         # Split-pane TUI
│
├── patterns/            # Event patterns per device family
│   ├── esp32.pat
│   ├── stm32.pat
│   ├── arduino.pat
│   ├── freertos.pat
│   └── zephyr.pat
│
└── plugins/             # Python: custom handlers, integrations
    ├── webhooks.py      # Forward events to ntfy/Slack/Discord
    └── replay.py        # Replay recorded sessions
```

**C core**: one thread per port, minimal overhead, runs forever without surprises.

**Pattern files**: plain text rules, no recompile needed:
```
# patterns/esp32.pat
CRITICAL  GURU_MEDITATION  Guru Meditation Error
CRITICAL  ABORT            abort\(\) was called
HIGH      STACK_OVERFLOW   stack overflow
WARN      RESET            rst:0x
INFO      BOOT             I \([0-9]+\) boot:
```

**Python plugins**: bolt on behavior without touching the core:
```python
# plugins/webhooks.py
# Forward events to ntfy.sh, Slack, Discord, or any webhook
python3 plugins/webhooks.py --logdir /tmp/logs --url https://ntfy.sh/my-topic
```

---

## Dependencies

| | Purpose | Required |
|--|---------|---------|
| `libserialport` | Cross-platform serial I/O | Vendored (no install needed) |
| `pthreads` | Per-port threads | Built-in |
| `regex.h` | Pattern matching | Built-in |
| Python 3.8+ | Plugin system | Optional |

---

## License

Apache 2.0, see [LICENSE](LICENSE).

---

## Contributors

| Role | Profile |
|------|---------|
| Author & Maintainer | [@Eun0us](https://github.com/Eun0us) |

Part of the [Espilon Association](https://espilon.net) open source ecosystem.
