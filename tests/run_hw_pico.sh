#!/usr/bin/env bash
# Hardware test for emon — Raspberry Pi Pico (MicroPython)
#
# Prerequisites:
#   MicroPython firmware flashed on the Pico (https://micropython.org/download/rp2-pico/)
#   mpremote installed: pip install mpremote
#
# Usage:
#   ./tests/run_hw_pico.sh [/dev/ttyACM0] [--manual]
#
# What it does:
#   1. Deploys pico_test.py as main.py via mpremote
#   2. Clears any leftover phase state on the device filesystem
#   3. Starts emon monitoring the port
#   4. The script cycles through 6 phases: WDT reset, AssertionError,
#      PANIC print, MemoryError, then ALL_DONE
#   5. Verifies all expected events appear in the emon event log
#
# Note: The Pico USB CDC port briefly disappears on machine.reset() —
# emon's auto-reconnect handles this automatically.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PORT="/dev/ttyACM0"
MANUAL=0

for arg in "$@"; do
    case "$arg" in
        /dev/*)   PORT="$arg" ;;
        tty*)     PORT="/dev/$arg" ;;
        --manual) MANUAL=1 ;;
    esac
done

EMON="$REPO_DIR/emon"
FW_DIR="$SCRIPT_DIR/firmwares/pico_test"
PAT="$FW_DIR/pico_test.pat"
LOGDIR="/tmp/emon_hw_pico"

GREEN='\033[0;32m'
RED='\033[0;31m'
DIM='\033[2m'
NC='\033[0m'

EXPECTED="BOOT_OK WATCHDOG_RESET ASSERT PANIC MEM_ERROR ALL_DONE"

# ── Preflight ────────────────────────────────────────────────────────────────

if [ ! -c "$PORT" ]; then
    echo "Error: $PORT not found. Is the Pico plugged in with MicroPython?"
    echo "  Flash MicroPython: https://micropython.org/download/rp2-pico/"
    exit 1
fi

if ! command -v mpremote &>/dev/null; then
    echo "Error: mpremote not found."
    echo "  Install: pip install mpremote"
    exit 1
fi

if [ $MANUAL -eq 0 ] && [ ! -x "$EMON" ]; then
    echo "Error: emon not built. Run 'make' first."
    exit 1
fi

# ── Deploy ───────────────────────────────────────────────────────────────────

echo "=== emon pico hardware test ==="
echo "port : $PORT"
echo "mode : $([ $MANUAL -eq 1 ] && echo 'manual' || echo 'auto')"
echo ""

echo -e "${DIM}Deploying pico_test.py to Pico...${NC}"

# Clear stale phase state, then copy the test script
mpremote connect "$PORT" fs rm :phase.txt 2>/dev/null || true
mpremote connect "$PORT" fs cp "$FW_DIR/pico_test.py" :main.py

echo -e "${DIM}Rebooting Pico into test mode...${NC}"
mpremote connect "$PORT" reset

# Give the Pico time to reboot and enumerate USB CDC before emon opens the port
sleep 2

echo -e "${DIM}Deploy OK${NC}"
echo ""

if [ $MANUAL -eq 1 ]; then
    echo "Manual mode - run in another terminal:"
    echo "  ./emon --wait-for ALL_DONE --timeout 90 --quiet -p $PAT --logdir $LOGDIR $PORT"
    exit 0
fi

# ── Monitor ──────────────────────────────────────────────────────────────────

echo "Sequence: BOOT_OK → WDT reset → AssertionError → PANIC → MemoryError → ALL_DONE"
echo "(Pico reboots between phases — emon reconnects automatically)"
echo ""

rm -rf "$LOGDIR" && mkdir -p "$LOGDIR"

"$EMON" --wait-for ALL_DONE --timeout 90 --quiet \
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
        echo -e "${RED}[FAIL]${NC} timeout - sequence did not complete in 90s"
        exit 1
        ;;
    *)
        echo -e "${RED}[FAIL]${NC} emon exited with code $rc"
        exit 1
        ;;
esac
