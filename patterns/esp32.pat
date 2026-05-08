# ESP-IDF / ESP32 patterns
# Format: SEVERITY  NAME  REGEX

CRITICAL  GURU_MEDITATION   Guru Meditation Error
CRITICAL  CORE_PANIC        Core [0-9]+ panic
CRITICAL  INSTR_FAULT       Instruction access fault
CRITICAL  LOAD_FAULT        Load access fault
CRITICAL  STORE_FAULT       Store access fault
CRITICAL  M_MODE_EXEC       Origin: M-mode
CRITICAL  ABORT             abort\(\) was called
CRITICAL  STACK_OVERFLOW    stack overflow
CRITICAL  HEAP_CORRUPT      CORRUPT HEAP
CRITICAL  ASAN_WRITE        WRITE of size
CRITICAL  HEAP_OOB          heap-buffer-overflow
CRITICAL  STACK_OOB         stack-buffer-overflow
HIGH      ASAN_READ         READ of size
HIGH      BACKTRACE         Backtrace:
HIGH      MEPC_DUMP         MEPC[ \t]*:
HIGH      PANIC_RESET       rst:0x[0-9a-f]+.*PANIC
HIGH      WATCHDOG          Task watchdog got triggered
WARN      BROWNOUT          rst:0x[0-9a-f]+.*BROWNOUT
WARN      ESP_ERROR         ^E \([0-9]+\)
