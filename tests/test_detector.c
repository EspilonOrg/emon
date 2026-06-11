#include "../src/monitor/detector.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define PASS "\033[32mPASS\033[0m"
#define FAIL "\033[31mFAIL\033[0m"

static int passed = 0, failed = 0;

#define CHECK(cond, name) \
    do { \
        if (cond) { printf("[" PASS "] %s\n", name); passed++; } \
        else       { printf("[" FAIL "] %s\n", name); failed++; } \
    } while (0)

/* ── ESP32 ───────────────────────────────────────────────────────────────── */

static void test_builtin_esp32(void)
{
    printf("\n-- esp32 --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);
    detector_load_file(&d, "patterns/esp32.pat");

    bool hit = detector_check(&d,
        "Guru Meditation Error: Core 0 panic'ed (LoadProhibited)", "T", &ev);
    CHECK(hit,                                "esp32: Guru Meditation matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "esp32: Guru Meditation is CRITICAL");

    hit = detector_check(&d, "abort() was called at PC 0x4008001c", "T", &ev);
    CHECK(hit,                                "esp32: abort() matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "esp32: abort() is CRITICAL");

    hit = detector_check(&d,
        "***ERROR*** A stack overflow in task wifi was detected", "T", &ev);
    CHECK(hit,                                "esp32: stack overflow matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "esp32: stack overflow is CRITICAL");

    hit = detector_check(&d,
        "Task watchdog got triggered. The following tasks did not reset", "T", &ev);
    CHECK(hit,                                "esp32: task watchdog matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "esp32: task watchdog is HIGH");

    hit = detector_check(&d, "I (1234) app: starting up", "T", &ev);
    CHECK(!hit,                               "esp32: info line does not match");

    detector_free(&d);
}

/* ── STM32 ───────────────────────────────────────────────────────────────── */

static void test_builtin_stm32(void)
{
    printf("\n-- stm32 --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);
    detector_load_file(&d, "patterns/stm32.pat");

    bool hit = detector_check(&d, "HardFault_Handler called", "T", &ev);
    CHECK(hit,                                "stm32: HardFault matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "stm32: HardFault is CRITICAL");

    hit = detector_check(&d, "MemManage_Handler: invalid memory access", "T", &ev);
    CHECK(hit,                                "stm32: MemManage matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "stm32: MemManage is CRITICAL");

    hit = detector_check(&d, "BusFault_Handler triggered", "T", &ev);
    CHECK(hit,                                "stm32: BusFault matches");

    hit = detector_check(&d, "UsageFault_Handler: undefined instruction", "T", &ev);
    CHECK(hit,                                "stm32: UsageFault matches");

    hit = detector_check(&d, "assert_failed: file main.c, line 42", "T", &ev);
    CHECK(hit,                                "stm32: assert_failed matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "stm32: assert_failed is HIGH");

    hit = detector_check(&d, "stack overflow detected", "T", &ev);
    CHECK(hit,                                "stm32: stack overflow matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "stm32: stack overflow is CRITICAL");

    hit = detector_check(&d, "SysTick_Handler: tick 1234", "T", &ev);
    CHECK(!hit,                               "stm32: normal tick does not match");

    detector_free(&d);
}

/* ── Arduino ─────────────────────────────────────────────────────────────── */

static void test_builtin_arduino(void)
{
    printf("\n-- arduino --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);
    detector_load_file(&d, "patterns/arduino.pat");

    bool hit = detector_check(&d, "Watchdog Reset detected, restarting", "T", &ev);
    CHECK(hit,                                "arduino: Watchdog Reset matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "arduino: Watchdog Reset is HIGH");

    hit = detector_check(&d, "Assertion myVar == 0 failed at line 99", "T", &ev);
    CHECK(hit,                                "arduino: Assertion failed matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "arduino: Assertion is HIGH");

    hit = detector_check(&d, "setup() complete", "T", &ev);
    CHECK(!hit,                               "arduino: normal line does not match");

    detector_free(&d);
}

/* ── FreeRTOS ────────────────────────────────────────────────────────────── */

static void test_builtin_freertos(void)
{
    printf("\n-- freertos --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);
    detector_load_file(&d, "patterns/freertos.pat");

    /* Pattern: "Stack overflow in task" (capital S) */
    bool hit = detector_check(&d, "Stack overflow in task idle detected!", "T", &ev);
    CHECK(hit,                                "freertos: Stack overflow matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "freertos: Stack overflow is CRITICAL");

    /* Pattern: "pvPortMalloc.*NULL" */
    hit = detector_check(&d, "pvPortMalloc returned NULL (size=256)", "T", &ev);
    CHECK(hit,                                "freertos: pvPortMalloc NULL matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "freertos: pvPortMalloc NULL is HIGH");

    /* Pattern: "FreeRTOS.*assert" */
    hit = detector_check(&d, "FreeRTOS assert failed at tasks.c line 3456", "T", &ev);
    CHECK(hit,                                "freertos: FreeRTOS assert matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "freertos: FreeRTOS assert is HIGH");

    hit = detector_check(&d, "vTaskDelay( 100 / portTICK_PERIOD_MS )", "T", &ev);
    CHECK(!hit,                               "freertos: normal delay does not match");

    detector_free(&d);
}

/* ── Zephyr ──────────────────────────────────────────────────────────────── */

static void test_builtin_zephyr(void)
{
    printf("\n-- zephyr --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);
    detector_load_file(&d, "patterns/zephyr.pat");

    bool hit = detector_check(&d, "FATAL FAULT in thread main", "T", &ev);
    CHECK(hit,                                "zephyr: FATAL FAULT matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "zephyr: FATAL FAULT is CRITICAL");

    hit = detector_check(&d, "KERNEL PANIC: division by zero", "T", &ev);
    CHECK(hit,                                "zephyr: KERNEL PANIC matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "zephyr: KERNEL PANIC is CRITICAL");

    hit = detector_check(&d, "__ASSERT failed: condition is false", "T", &ev);
    CHECK(hit,                                "zephyr: __ASSERT matches");
    CHECK(hit && ev.severity == SEV_HIGH,     "zephyr: __ASSERT is HIGH");

    hit = detector_check(&d, "CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=64000000", "T", &ev);
    CHECK(!hit,                               "zephyr: config line does not match");

    detector_free(&d);
}

/* ── Deduplication ───────────────────────────────────────────────────────── */

static void test_dedup(void)
{
    printf("\n-- dedup --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);
    detector_add_rule(&d, "TEST_RULE", "crash", SEV_HIGH, false);

    bool first  = detector_check(&d, "crash detected", "DEV", &ev);
    bool second = detector_check(&d, "crash detected", "DEV", &ev);
    CHECK(first,   "dedup: first occurrence fires");
    CHECK(!second, "dedup: duplicate is suppressed");

    bool third = detector_check(&d, "crash in another location", "DEV", &ev);
    CHECK(third,   "dedup: different line fires");

    detector_free(&d);
}

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static void test_severity_str(void)
{
    printf("\n-- severity_str --\n");
    CHECK(strcmp(severity_str(SEV_CRITICAL), "CRITICAL") == 0, "severity_str CRITICAL");
    CHECK(strcmp(severity_str(SEV_HIGH),     "HIGH")     == 0, "severity_str HIGH");
    CHECK(strcmp(severity_str(SEV_WARN),     "WARN")     == 0, "severity_str WARN");
    CHECK(strcmp(severity_str(SEV_INFO),     "INFO")     == 0, "severity_str INFO");
}

static void test_add_rule(void)
{
    printf("\n-- add_rule --\n");
    detector_t d;
    det_event_t ev;
    detector_init(&d);

    int rc = detector_add_rule(&d, "MY_RULE", "FAULT:.*addr=0x", SEV_HIGH, false);
    CHECK(rc == 0, "add_rule returns 0");

    bool hit = detector_check(&d, "FAULT: invalid addr=0x12345678", "DEV", &ev);
    CHECK(hit,                                         "custom rule: regex matches");
    CHECK(hit && strcmp(ev.rule->name, "MY_RULE") == 0,"custom rule: name preserved");
    CHECK(hit && ev.severity == SEV_HIGH,              "custom rule: severity preserved");

    hit = detector_check(&d, "all is fine", "DEV", &ev);
    CHECK(!hit, "custom rule: non-matching line");

    detector_free(&d);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== emon detector tests ===\n");

    test_builtin_esp32();
    test_builtin_stm32();
    test_builtin_arduino();
    test_builtin_freertos();
    test_builtin_zephyr();
    test_dedup();
    test_severity_str();
    test_add_rule();

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
