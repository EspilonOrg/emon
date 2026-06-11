# test_suite patterns - all states in one session
# Format: SEVERITY  NAME  REGEX

INFO      BOOT_OK         TEST: BOOT_OK
CRITICAL  ABORT           abort\(\) was called
CRITICAL  STACK_OVERFLOW  stack overflow in task
HIGH      WATCHDOG        Task watchdog got triggered
CRITICAL  GURU_MEDITATION Guru Meditation Error
INFO      ALL_DONE        TEST: ALL_DONE
