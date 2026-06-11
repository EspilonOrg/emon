# pico_test.pat — expected events for the Pi Pico hardware test sequence
# Format: SEVERITY  NAME  REGEX

INFO      BOOT_OK         pico_test phase
HIGH      WATCHDOG_RESET  Watchdog Reset
HIGH      ASSERT          AssertionError
HIGH      PANIC           PANIC:
HIGH      MEM_ERROR       MemoryError
INFO      ALL_DONE        ALL_DONE
