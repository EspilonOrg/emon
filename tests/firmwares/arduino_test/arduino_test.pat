# arduino_test.pat — expected events for the arduino hardware test sequence
# Format: SEVERITY  NAME  REGEX

INFO      BOOT_OK         arduino_test phase
HIGH      WATCHDOG_RESET  Watchdog Reset
HIGH      ASSERT          Assertion .* failed
HIGH      PANIC           panic:
WARN      LOW_MEMORY      low memory
INFO      ALL_DONE        ALL_DONE
