# Contributing to emon

Thanks for your interest. emon is a small, dependency-light C tool; contributions
that keep it that way are very welcome.

## Build & test

```bash
make            # build ./espilon-monitor
make test       # build + run the detector test suite
make check      # verify libserialport is available
make DEBUG=1    # debug build (-g -O0 -DDEBUG)
```

CI runs `make`, `make test` and `make check` on Linux for every push and PR
(see `.github/workflows/ci.yml`). Please make sure all three pass locally first.

## Adding a device family (no C, no recompile)

The easiest contribution: a new pattern file in `patterns/`. Format is one rule per line:

```
# patterns/<family>.pat
SEVERITY  NAME  REGEX
```

- `SEVERITY` — `CRITICAL`, `HIGH`, `WARN` or `INFO`.
- `NAME` — a short uppercase event id, no spaces.
- `REGEX` — POSIX extended regex; everything after the name is the pattern.

Load with `emon --patterns <family>.pat`. See `patterns/esp32.pat` and
`patterns/exploit.pat` for examples. If you can, include a sample log line your
pattern matches in the PR description.

## C changes

- C11, `-Wall -Wextra` clean (the Makefile enables both; don't add new warnings).
- No new external dependencies without discussion — the only runtime dep is the
  vendored libserialport.
- One concern per module; keep the per-port thread model intact.
- Match the surrounding style (4-space indent, snake_case).

## Plugins

Python 3.8+ plugins live in `plugins/` and consume the JSON event stream /
logdir; they never link against the core. Keep them self-contained.

## Pull requests

- Branch from `main`, keep the PR focused.
- Add a `CHANGELOG.md` entry under `## [Unreleased]`.
- Describe what you tested (which board / which pattern / `make test`).

## Reporting bugs

Open an issue with: your OS, the `emon` command line, the device family, and the
relevant log excerpt. For security-sensitive reports, contact the maintainer
directly rather than filing a public issue.
