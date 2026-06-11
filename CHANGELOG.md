# Changelog

All notable changes to **emon** (espilon-monitor) are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project aims to follow [Semantic Versioning](https://semver.org/).

## [0.1.1] - 2026-06-11

### Fixed

- `make install` now installs pattern files to `share/emon/patterns/` and
  `.emon.conf.example` to `share/doc/emon/` — previously only the binary and
  man page were installed.

## [0.1.0] - 2026-06-10

First public release. Universal multi-port serial monitor for embedded devices.

### Added

- Multi-port serial I/O: monitor any number of devices at once, one thread per port
  (vendored libserialport, no system install required).
- Pattern detection engine: POSIX regex, severity levels (CRITICAL/HIGH/WARN/INFO),
  O(1) event deduplication (open-addressing hash set, FNV-1a, 2048 slots).
- Per-device timestamped logging with rolling rotation
  (`.log → .log.1 → .log.2 → .log.3`).
- Auto-detect chip family (USB VID/PID) with per-device family banner.
- Hardware reset via RTS/DTR on a configured event.
- **Interactive mode** (`-i`): bidirectional stdin↔device, local echo in raw mode,
  per-device prompt, input/output sync via `pre_print`/`post_print` to prevent
  interleaving. Shortcuts: `Ctrl+A X` quit, `Ctrl+A C` send raw 0x03.
- **`--json-events` flag**: pure NDJSON on stdout, human output on stderr.
  Combine with `--quiet` to feed pipelines (`| jq .`).
- **Flow control** (`--flow-control none|rtscts|xonxoff`).
- **Background daemon** (`--bg`) with `emon status` and `emon stop`.
- **CI integration**: `--wait-for RULE --timeout N` (exit 0 on match, 124 on
  timeout), `--exit-on "RULE=CODE"`.
- **Python event hooks** (`--on-event <script>`): fires on each match with a JSON
  payload on stdin; double-fork so the monitor never blocks. Up to 8 hooks.
- Split-pane TUI (`--tui`), hex dump (`--hex`), in-process scrollback (`Ctrl+A [`).
- **Config file** (`--config .emon.conf`): all flags available as persistent INI.
- Exit-rule validation: warns at startup if `--exit-on`/`--wait-for` rule name is
  not found in any loaded pattern file.
- Built-in pattern families: `esp32`, `stm32`, `arduino`, `freertos`, `zephyr`,
  `esp-idf`, `exploit`.
- Python plugin system: webhook notifier (ntfy/Slack/Discord) and session replayer.
- Hardware test harness under `tests/` with bash lint in CI.
- Demo GIFs (5): basic monitoring, interactive, JSON stream, multi-port, daemon.
- `esp32_hello` and `arduino_interactive` firmware sources for demo reproduction.
- Apache 2.0 license.

[0.1.1]: https://github.com/EspilonOrg/emon/releases/tag/v0.1.1
[0.1.0]: https://github.com/EspilonOrg/emon/releases/tag/v0.1.0
