# Changelog

All notable changes to **emon** (espilon-monitor) are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project aims to follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- `exploit.pat` pattern family: exploitation signatures for vuln research and PoC
  triage (control-flow hijack in PC/RA, NULL function-pointer calls, fault-address
  context, Xtensa exception causes, sanitizer OOB primitives, PoC harness markers).
- `esp-idf.pat` pattern family.
- Hardware test harness under `tests/` (`run_hw_tests.sh`, `build_fw.sh`, `firmwares/`).

### Changed
- Source tree reorganized into `src/{app,monitor,serial,ui,utils}/` (in progress).

## [0.1.0] - first release (preparing)

First public release. Universal multi-port serial monitor for embedded devices.

### Added
- Multi-port serial I/O: monitor any number of devices at once, one thread per port
  (vendored libserialport, no system install required).
- Pattern detection engine: severity levels (CRITICAL/HIGH/WARN/INFO), per-device or
  global rules, event deduplication. Built-in families: esp32, stm32, arduino,
  freertos, zephyr, espilon.
- Per-device timestamped logging with surrounding event-context capture.
- Auto-detect chip family (USB VID/PID, instant) and per-device family in the banner.
- Hardware reset via RTS/DTR on a configured event.
- Interactive mode (`-i`): bidirectional stdin to device.
- In-process scrollback viewer (Ctrl+A `[`).
- Background daemon mode (`--bg`) and `emon stop`.
- Test-runner support: `--wait-for`, `--exit-on`, `--timeout`, `--json-events`.
- Split-pane TUI (`--tui`), native (no ncurses).
- Python plugin system: webhook notifier (ntfy/Slack/Discord) and session replayer.
- Apache 2.0 license.

[Unreleased]: https://github.com/Eun0us/espilon-monitor/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Eun0us/espilon-monitor/releases/tag/v0.1.0
