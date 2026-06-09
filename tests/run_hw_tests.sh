#!/usr/bin/env bash
# Hardware test for espilon-monitor - single firmware, all crash states
#
# Usage:
#   ./tests/run_hw_tests.sh [/dev/ttyUSB0] [--manual]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PORT="/dev/ttyUSB0"
MANUAL=0

for arg in "$@"; do
    case "$arg" in
        /dev/*)   PORT="$arg" ;;
        tty*)     PORT="/dev/$arg" ;;
        --manual) MANUAL=1 ;;
    esac
done

EMON="$REPO_DIR/emon"
FW_DIR="$SCRIPT_DIR/firmwares/test_suite"
PAT="$FW_DIR/test_suite.pat"
BUILD_DIR="$FW_DIR/build"
LOGDIR="/tmp/emon_hw_test"

GREEN='\033[0;32m'
RED='\033[0;31m'
DIM='\033[2m'
NC='\033[0m'

# NVS partition offset - default ESP-IDF layout (see partition table in build)
NVS_OFFSET=0x9000
NVS_SIZE=0x6000

# Expected events - all must appear in events log for PASS
EXPECTED="BOOT_OK ABORT STACK_OVERFLOW WATCHDOG GURU_MEDITATION ALL_DONE"

# ── Checks ─────────────────────────────────────────────────────────────────

if [ ! -c "$PORT" ]; then
    echo "Error: $PORT not found. Is the ESP32 plugged in?"
    exit 1
fi
if [ $MANUAL -eq 0 ] && [ ! -x "$EMON" ]; then
    echo "Error: $EMON not built. Run 'make' first."
    exit 1
fi
if [ ! -f "$BUILD_DIR/flash_args" ]; then
    echo "Error: firmware not built. Run 'make build-fw' first."
    exit 1
fi

# ── Flash + NVS erase ──────────────────────────────────────────────────────

echo "=== emon hardware test ==="
echo "port : $PORT"
echo "mode : $([ $MANUAL -eq 1 ] && echo 'manual' || echo 'auto')"
echo ""

flashlog="/tmp/emon_hw_flash.log"

flash_ok=0
for attempt in 1 2 3; do
    echo -e "${DIM}Flashing firmware + erasing NVS (attempt $attempt)...${NC}"

    # Erase NVS first so phase resets to 0 regardless of previous state
    (cd "$BUILD_DIR" && esptool -p "$PORT" -b 460800 \
        --before default_reset \
        erase_region $NVS_OFFSET $NVS_SIZE \
        && esptool -p "$PORT" -b 460800 \
        --before default_reset --after hard_reset \
        write_flash @flash_args) >"$flashlog" 2>&1

    if [ $? -eq 0 ]; then
        flash_ok=1
        break
    fi
    if grep -q "Errno 5\|Input/output error" "$flashlog" 2>/dev/null; then
        echo -e "  ${RED}port I/O error${NC} - unplug/replug the USB cable, then retry"
        sleep 3
    else
        break
    fi
done

if [ $flash_ok -eq 0 ]; then
    echo -e "${RED}Flash failed${NC} - see $flashlog"
    tail -5 "$flashlog"
    exit 1
fi
echo -e "${DIM}Flashed OK - NVS cleared, starting from phase 0${NC}"
echo ""

# ── Run ────────────────────────────────────────────────────────────────────

if [ $MANUAL -eq 1 ]; then
    echo "Manual mode - run in another terminal:"
    echo "  ./emon --wait-for ALL_DONE --timeout 90 --quiet -p $PAT --logdir $LOGDIR $PORT"
    exit 0
fi

echo "Sequence: BOOT_OK → abort → stack overflow → watchdog → guru → ALL_DONE"
echo ""

rm -rf "$LOGDIR" && mkdir -p "$LOGDIR"

"$EMON" --wait-for ALL_DONE --timeout 90 --quiet \
    -p "$PAT" --logdir "$LOGDIR" "$PORT"
rc=$?

echo ""

# ── Verify events log ──────────────────────────────────────────────────────

evlog="$LOGDIR/ttyUSB0_events.log"
if [ ! -f "$evlog" ]; then
    # Try with the port basename
    evlog=$(ls "$LOGDIR"/*_events.log 2>/dev/null | head -1)
fi

nmissing=0
if [ $rc -eq 0 ] && [ -f "$evlog" ]; then
    echo "Events detected:"
    for rule in $EXPECTED; do
        if grep -q "$rule" "$evlog"; then
            echo -e "  ${GREEN}✓${NC} $rule"
        else
            echo -e "  ${RED}✗${NC} $rule - NOT FOUND in events log"
            nmissing=$((nmissing+1))
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
