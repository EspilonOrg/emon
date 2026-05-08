#include "../src/detector.h"
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

static void test_builtin_esp32(void)
{
    detector_t d;
    det_event_t ev;

    detector_init(&d);
    detector_load_builtin(&d, "esp32");

    /* Should match Guru Meditation */
    bool hit = detector_check(&d,
        "Guru Meditation Error: Core 0 panic'ed (LoadProhibited)",
        "TEST", &ev);
    CHECK(hit, "esp32: Guru Meditation matches");
    CHECK(hit && ev.severity == SEV_CRITICAL, "esp32: Guru Meditation is CRITICAL");

    /* Should match abort */
    hit = detector_check(&d, "abort() was called at PC 0x4008001c", "TEST", &ev);
    CHECK(hit, "esp32: abort() matches");

    /* Normal log line — should NOT match */
    hit = detector_check(&d, "I (1234) app: starting up", "TEST", &ev);
    CHECK(!hit, "esp32: info line does not match");

    /* Stack overflow */
    hit = detector_check(&d, "***ERROR*** A stack overflow in task wifi was detected", "TEST", &ev);
    CHECK(hit, "esp32: stack overflow matches");

    detector_free(&d);
}

static void test_dedup(void)
{
    detector_t d;
    det_event_t ev;

    detector_init(&d);
    detector_add_rule(&d, "TEST_RULE", "crash", SEV_HIGH, false);

    bool first  = detector_check(&d, "crash detected", "DEV", &ev);
    bool second = detector_check(&d, "crash detected", "DEV", &ev);

    CHECK(first,   "dedup: first occurrence fires");
    CHECK(!second, "dedup: duplicate is suppressed");

    /* Different line — should fire again */
    bool third = detector_check(&d, "crash in another location", "DEV", &ev);
    CHECK(third,   "dedup: different line fires");

    detector_free(&d);
}

static void test_severity_str(void)
{
    CHECK(strcmp(severity_str(SEV_CRITICAL), "CRITICAL") == 0, "severity_str CRITICAL");
    CHECK(strcmp(severity_str(SEV_HIGH),     "HIGH")     == 0, "severity_str HIGH");
    CHECK(strcmp(severity_str(SEV_WARN),     "WARN")     == 0, "severity_str WARN");
    CHECK(strcmp(severity_str(SEV_INFO),     "INFO")     == 0, "severity_str INFO");
}

static void test_add_rule(void)
{
    detector_t d;
    det_event_t ev;

    detector_init(&d);
    int rc = detector_add_rule(&d, "MY_RULE", "FAULT:.*addr=0x", SEV_HIGH, false);
    CHECK(rc == 0, "add_rule returns 0");

    bool hit = detector_check(&d, "FAULT: invalid addr=0x12345678", "DEV", &ev);
    CHECK(hit, "custom rule: regex matches");
    CHECK(hit && strcmp(ev.rule->name, "MY_RULE") == 0, "custom rule: name preserved");
    CHECK(hit && ev.severity == SEV_HIGH, "custom rule: severity preserved");

    /* Non-matching line */
    hit = detector_check(&d, "all is fine", "DEV", &ev);
    CHECK(!hit, "custom rule: non-matching line");

    detector_free(&d);
}

int main(void)
{
    printf("=== espilon-monitor detector tests ===\n\n");

    test_builtin_esp32();
    test_dedup();
    test_severity_str();
    test_add_rule();

    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
