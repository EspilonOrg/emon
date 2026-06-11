#!/usr/bin/env bash
# Build all test firmwares (one-time, slow)
# Usage: ./tests/build_fw.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IDF_EXPORT="$HOME/esp-idf/export.sh"

FW_NAMES=(crash_guru crash_abort crash_stackoverflow crash_watchdog normal_boot test_suite)

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

if [ ! -f "$IDF_EXPORT" ]; then
    echo "Error: ESP-IDF not found at $HOME/esp-idf"
    exit 1
fi

# shellcheck disable=SC1090
source "$IDF_EXPORT" >/dev/null 2>&1 || true

nok=0
nfail=0

for fw in "${FW_NAMES[@]}"; do
    fw_dir="$SCRIPT_DIR/firmwares/$fw"
    logfile="/tmp/emon_build_${fw}.log"

    echo -n "  $fw ... "
    (cd "$fw_dir" && idf.py build) >"$logfile" 2>&1
    rc=$?

    if [ $rc -eq 0 ]; then
        echo -e "${GREEN}OK${NC}"
        nok=$((nok+1))
    else
        echo -e "${RED}FAILED${NC} (see $logfile)"
        tail -10 "$logfile"
        nfail=$((nfail+1))
    fi
done

echo ""
if [ $nfail -eq 0 ]; then
    echo -e "${GREEN}All ${nok} firmwares built.${NC} Run 'make test-hw' to flash + test."
    exit 0
else
    echo -e "${RED}${nfail} build(s) failed.${NC}"
    exit 1
fi
