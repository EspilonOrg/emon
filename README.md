# espilon-monitor

**Universal serial monitor for embedded devices.**

Monitor any number of serial devices simultaneously: production bots, development boards, fuzzing targets, test rigs, from a single terminal.

---

## The Problem

You have boards. They output things on serial. You need to know what's happening.

With one board and one terminal, `screen` works fine. With five boards running 24/7, it breaks down: missed events, no logging, no alerting, no context when something goes wrong.

`espilon-monitor` gives you a unified view of all your devices, with the intelligence to tell you what matters.

---

## What it does

```
espilon-monitor  4 ports
-----------------------------------------

[bot-alpha ] online @ 115200 baud  [family: espilon]
[bot-beta  ] online @ 115200 baud  [family: espilon]
[dev-board ] online @ 115200 baud  [family: esp32]
[target-ble] online @ 115200 baud  [family: esp32]

12:04:31.042 [bot-alpha ] Task completed: scan_192.168.1.0/24
12:04:31.198 [bot-beta  ] Waiting for command...
12:04:33.571 [dev-board ] [CRITICAL] Guru Meditation Error: Core 0 panic'd
12:04:33.571 [dev-board ]   !! guru_meditation -- dev-board
12:04:34.802 [target-ble] Connecting to aa:bb:cc:dd:ee:ff...
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

**Operations / bots**
Monitor a fleet of ESP32 bots running autonomous tasks. Get alerted when one goes silent, crashes, or outputs an unexpected state. Log everything for post-mortem analysis.

**Development**
Replace your 5 terminal windows with a single view. See all your boards at once during a test session. Spot regressions instantly.

**Security research**
Run fuzzing campaigns against embedded targets. Classify crashes in real time, deduplicate, auto-reset and continue. Detect privilege escalation signals (stack dumps, fault handlers, unexpected resets).

**Test automation**
Feed inputs to devices, watch for expected outputs, flag anything outside spec. Works with AFL++, libFuzzer, or your own test harness.

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

## Architecture

```
espilon-monitor/
├── src/
│   ├── serial.c/.h      # Multi-port I/O (libserialport)
│   ├── detector.c/.h    # Pattern matching (POSIX regex)
│   ├── monitor.c/.h     # Main loop (pthreads, one thread per port)
│   ├── recorder.c/.h    # Logging + event context capture
│   ├── reset.c/.h       # Hardware reset via RTS/DTR
│   ├── display.c/.h     # Terminal output
│   └── config.c/.h      # Config file parsing
│
├── patterns/            # Event patterns per device family
│   ├── esp32.pat
│   ├── stm32.pat
│   ├── arduino.pat
│   └── freertos.pat
│
└── plugins/             # Python: custom handlers, integrations
    ├── webhooks.py
    └── replay.py
```

**C core**: one thread per port, minimal overhead, runs forever without surprises.

**Pattern files**: plain text rules, no recompile needed:
```
# patterns/esp32.pat
CRITICAL  Guru Meditation Error
CRITICAL  abort\(\) was called
HIGH      stack overflow
WARN      rst:0x
INFO      I \([0-9]+\)
```

**Python plugins**: bolt on behavior without touching the core:
```python
# plugins/webhooks.py
def on_event(device, severity, line):
    if severity == "CRITICAL":
        post_to_discord(device, line)
```

---

## Quick start

```bash
# Build (libserialport vendored, no system deps needed)
make

# Monitor 3 devices at once
./espilon-monitor /dev/ttyACM0 /dev/ttyUSB0 /dev/ttyUSB1

# Auto-detect family, log to disk
./espilon-monitor --logdir ./logs /dev/ttyUSB0

# Explicit family, background daemon
./espilon-monitor --family espilon --bg --logdir /opt/logs /dev/ttyUSB1

# Interactive: send input to device (Ctrl+A X to quit, Ctrl+A [ for scrollback)
./espilon-monitor -i /dev/ttyUSB0

# CI/test runner: wait for pattern, exit 0 on success / 124 on timeout
./espilon-monitor --wait-for HANDSHAKE_OK --timeout 30 /dev/ttyUSB0

# Machine-readable JSON event stream
./espilon-monitor --json-events --exit-on CRASH_DETECTED /dev/ttyUSB0

# Stop a running background daemon
./espilon-monitor stop
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

## Author

[Eun0us](https://github.com/Eun0us) / [Espilon](https://github.com/espilon)
