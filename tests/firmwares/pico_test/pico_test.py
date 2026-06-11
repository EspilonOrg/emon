"""
pico_test.py — emon hardware test for Raspberry Pi Pico (MicroPython)

Deploy as main.py on the Pico via mpremote.
Phase state is persisted in phase.txt on the flash filesystem across reboots.
Each phase generates serial output that pico_test.pat should detect.

  Phase 0: arm WDT (500ms) → hang → hardware reset
  Phase 1: detect WDT reset cause → print "Watchdog Reset" → reset
  Phase 2: raise uncaught AssertionError → print traceback → reset
  Phase 3: print "PANIC:" → reset
  Phase 4: force MemoryError → reset
  Phase 5: print "ALL_DONE" → clear phase → done

Run with: emon --wait-for ALL_DONE --timeout 90 --quiet -p pico_test.pat /dev/ttyACM0
"""

import machine
import time
import sys
import os

PHASE_FILE = "phase.txt"


def read_phase():
    try:
        with open(PHASE_FILE) as f:
            v = int(f.read().strip())
            return v if 0 <= v <= 5 else 0
    except Exception:
        return 0


def write_phase(p):
    with open(PHASE_FILE, "w") as f:
        f.write(str(p))


reset_cause = machine.reset_cause()
phase = read_phase()

time.sleep_ms(400)
print("=== pico_test phase {}/{} ===".format(phase, 5))
print("reset_cause: {}".format(reset_cause))
sys.stdout.flush()
time.sleep_ms(200)

if phase == 0:
    write_phase(1)
    print("[phase 0] arming watchdog - board will reset")
    sys.stdout.flush()
    wdt = machine.WDT(timeout=500)
    while True:
        time.sleep_ms(100)   # starve WDT

elif phase == 1:
    if reset_cause == machine.WDT_RESET:
        print("Watchdog Reset: wdt timeout expired")
    else:
        print("Watchdog Reset (cause={})".format(reset_cause))
    sys.stdout.flush()
    write_phase(2)
    time.sleep_ms(800)
    machine.reset()

elif phase == 2:
    write_phase(3)
    sys.stdout.flush()
    time.sleep_ms(200)
    # Raise a real exception — MicroPython prints traceback to REPL/serial
    raise AssertionError("hardware self-test assertion failed at phase 2")

elif phase == 3:
    print("PANIC: stack guard check failed in emon pico test")
    sys.stdout.flush()
    write_phase(4)
    time.sleep_ms(800)
    machine.reset()

elif phase == 4:
    # Force a real MemoryError — MicroPython prints it to serial
    write_phase(5)
    sys.stdout.flush()
    try:
        _sink = bytearray(1_000_000)   # way too large for the Pico's 264KB RAM
    except MemoryError:
        # MicroPython already printed the traceback; add a clean summary line
        print("MemoryError: allocation failed in pico_test phase 4")
    sys.stdout.flush()
    time.sleep_ms(800)
    machine.reset()

elif phase == 5:
    print("ALL_DONE")
    sys.stdout.flush()
    write_phase(0)   # reset for next run
