#!/usr/bin/env bash
# Hardware test for emon — Arduino Uno/Nano (ATmega328P)
#
# Prerequisites:
#   arduino-cli installed and configured (https://arduino.github.io/arduino-cli)
#   Arduino core: arduino:avr  (arduino-cli core install arduino:avr)
#
# Usage:
#   ./tests/run_hw_arduino.sh [/dev/ttyUSB0] [--board arduino:avr:uno] [--manual]
#
# What it does:
#   1. Compiles and flashes arduino_test.ino to the board
#   2. Starts emon monitoring the port
#   3. The firmware cycles through 5 crash phases across reboots
#   4. Verifies all 5 events appear in the emon event log
#
# Expected event sequence (each surviving a reboot):
#   BOOT_OK → WATCHDOG_RESET → ASSERT → PANIC → LOW_MEMORY → ALL_DONE

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PORT="/dev/ttyUSB0"
BOARD="arduino:avr:uno"
MANUAL=0

for arg in "$@"; do
    case "$arg" in
        /dev/*)          PORT="$arg" ;;
        tty*)            PORT="/dev/$arg" ;;
        --board)         ;;   # handled below with shift-like logic
        --board=*)       BOARD="${arg#--board=}" ;;
        --manual)        MANUAL=1 ;;
    esac
done

# Handle --board VALUE (two-arg form)
args=("$@")
for i in "${!args[@]}"; do
    if [[ "${args[$i]}" == "--board" && -n "${args[$i+1]:-}" ]]; then
        BOARD="${args[$i+1]}"
    fi
done

EMON="$REPO_DIR/emon"
FW_DIR="$SCRIPT_DIR/firmwares/arduino_test"
PAT="$FW_DIR/arduino_test.pat"
LOGDIR="/tmp/emon_hw_arduino"

GREEN='\033[0;32m'
RED='\033[0;31m'
DIM='\033[2m'
NC='\033[0m'

EXPECTED="BOOT_OK WATCHDOG_RESET ASSERT PANIC LOW_MEMORY ALL_DONE"

# ── Preflight ────────────────────────────────────────────────────────────────

if [ ! -c "$PORT" ]; then
    echo "Error: $PORT not found. Is the Arduino plugged in?"
    exit 1
fi

if ! command -v arduino-cli &>/dev/null; then
    echo "Error: arduino-cli not found."
    echo "  Install: https://arduino.github.io/arduino-cli/latest/installation/"
    exit 1
fi

if [ $MANUAL -eq 0 ] && [ ! -x "$EMON" ]; then
    echo "Error: emon not built. Run 'make' first."
    exit 1
fi

# ── Flash ────────────────────────────────────────────────────────────────────

echo "=== emon arduino hardware test ==="
echo "port  : $PORT"
echo "board : $BOARD"
echo "mode  : $([ $MANUAL -eq 1 ] && echo 'manual' || echo 'auto')"
echo ""

flashlog="/tmp/emon_arduino_flash.log"
flash_ok=0

for attempt in 1 2 3; do
    echo -e "${DIM}Compiling and flashing (attempt $attempt)...${NC}"
    arduino-cli compile --fqbn "$BOARD" "$FW_DIR" >"$flashlog" 2>&1 && \
    arduino-cli upload  --fqbn "$BOARD" --port "$PORT" "$FW_DIR" >>"$flashlog" 2>&1 && \
        flash_ok=1 && break
    echo -e "  ${RED}flash failed${NC} - check $flashlog"
    sleep 2
done

if [ $flash_ok -eq 0 ]; then
    echo -e "${RED}Flash failed${NC} - see $flashlog"
    tail -5 "$flashlog"
    exit 1
fi
echo -e "${DIM}Flashed OK${NC}"
echo ""

if [ $MANUAL -eq 1 ]; then
    echo "Manual mode - run in another terminal:"
    echo "  ./emon --wait-for ALL_DONE --timeout 60 --quiet -p $PAT --logdir $LOGDIR $PORT"
    exit 0
fi

# ── Monitor ──────────────────────────────────────────────────────────────────

echo "Sequence: BOOT_OK → WDT reset → assert → panic → low memory → ALL_DONE"
echo "(firmware reboots between phases — emon reconnects automatically)"
echo ""

rm -rf "$LOGDIR" && mkdir -p "$LOGDIR"

"$EMON" --wait-for ALL_DONE --timeout 60 --quiet \
    -p "$PAT" --logdir "$LOGDIR" "$PORT"
rc=$?

echo ""

# ── Verify ───────────────────────────────────────────────────────────────────

evlog=$(ls "$LOGDIR"/*_events.log 2>/dev/null | head -1)
nmissing=0

if [ $rc -eq 0 ] && [ -n "$evlog" ] && [ -f "$evlog" ]; then
    echo "Events detected:"
    for rule in $EXPECTED; do
        if grep -q "$rule" "$evlog"; then
            echo -e "  ${GREEN}✓${NC} $rule"
        else
            echo -e "  ${RED}✗${NC} $rule - NOT FOUND"
            nmissing=$((nmissing + 1))
        fi
    done
    echo ""
fi

case $rc in
    0)
        if [ $nmissing -eq 0 ]; then
            echo -e "${GREEN}[PASS]${NC} all 6 events detected"
            exit 0
        else
            echo -e "${RED}[FAIL]${NC} ALL_DONE received but $nmissing event(s) missing"
            exit 1
        fi
        ;;
    124)
        echo -e "${RED}[FAIL]${NC} timeout - sequence did not complete in 60s"
        exit 1
        ;;
    *)
        echo -e "${RED}[FAIL]${NC} emon exited with code $rc"
        exit 1
        ;;
esac
