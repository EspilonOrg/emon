# Changelog

All notable changes to **emon** (espilon-monitor) are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project aims to follow [Semantic Versioning](https://semver.org/).

## [0.2.0] - 2026-06-09

### Added
- **O(1) event deduplication**: open-addressing hash set (2048 slots, FNV-1a) replaces
  the O(n) 32-entry circular scan. No duplicate alert flooding under burst traffic.
- **Flow control** (`--flow-control none|rtscts|xonxoff`): applied per-session via
  libserialport `sp_set_config_flowcontrol()`.
- **Exit-rule validation**: warns at startup if a `--exit-on`/`--wait-for` rule name
  is not found in any loaded pattern file, so misconfigured CI jobs fail loud.
- **Rolling log rotation**: when a log reaches `max_bytes`, the chain
  `.log → .log.1 → .log.2 → .log.3` is applied instead of truncating.
- **Python event hook** (`--on-event <script>`): fires `python3 <script>` on each
  detected event with a JSON payload on stdin (`rule`, `severity`, `device`, `line`, `ts`).
  Uses double-fork so the monitor never blocks. Up to 8 hooks per session.
- `exploit.pat` pattern family: exploitation signatures for vuln research and PoC
  triage (control-flow hijack in PC/RA, NULL function-pointer calls, fault-address
  context, Xtensa exception causes, sanitizer OOB primitives, PoC harness markers).
- `esp-idf.pat` pattern family.
- Hardware test harness under `tests/` (`run_hw_tests.sh`, `build_fw.sh`).
- CI job `hw-test-lint`: bash -n syntax check on hardware test scripts.

### Changed
- Source tree fully migrated to `src/{app,monitor,serial,ui,utils}/`.
- `configure_port()` in serial.c now takes the full `serial_port_t *` to carry flow
  control setting (was `(struct sp_port *, int baud)`).

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
