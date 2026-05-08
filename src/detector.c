#include "detector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>

/* ── ANSI colors ───────────────────────────────────────────────────────── */
#define COLOR_RESET    "\033[0m"
#define COLOR_WARN     "\033[33m"
#define COLOR_HIGH     "\033[91m"
#define COLOR_CRITICAL "\033[1;91m"

/* ── Built-in pattern tables ───────────────────────────────────────────── */

typedef struct { const char *name; const char *pattern; severity_t sev; bool reset; } builtin_t;

static const builtin_t builtin_esp32[] = {
    { "GURU_MEDITATION",  "Guru Meditation Error",             SEV_CRITICAL, true  },
    { "CORE_PANIC",       "Core [0-9]+ panic",                 SEV_CRITICAL, true  },
    { "INSTR_FAULT",      "Instruction access fault",          SEV_CRITICAL, true  },
    { "LOAD_FAULT",       "Load access fault",                 SEV_CRITICAL, true  },
    { "STORE_FAULT",      "Store access fault",                SEV_CRITICAL, true  },
    { "M_MODE_EXEC",      "Origin: M-mode",                    SEV_CRITICAL, false },
    { "ABORT",            "abort\\(\\) was called",            SEV_CRITICAL, true  },
    { "STACK_OVERFLOW",   "stack overflow",                    SEV_CRITICAL, true  },
    { "HEAP_CORRUPT",     "CORRUPT HEAP",                      SEV_CRITICAL, true  },
    { "ASAN_WRITE",       "WRITE of size",                     SEV_CRITICAL, false },
    { "ASAN_READ",        "READ of size",                      SEV_HIGH,     false },
    { "HEAP_OOB",         "heap-buffer-overflow",              SEV_CRITICAL, false },
    { "STACK_OOB",        "stack-buffer-overflow",             SEV_CRITICAL, false },
    { "BACKTRACE",        "Backtrace:",                        SEV_HIGH,     false },
    { "MEPC_DUMP",        "MEPC[ \\t]*:",                      SEV_HIGH,     false },
    { "PANIC_RESET",      "rst:0x[0-9a-f]+.*PANIC",           SEV_HIGH,     false },
    { "WATCHDOG",         "Task watchdog got triggered",       SEV_HIGH,     true  },
    { "ESP_ERROR",        "^E \\([0-9]+\\)",                   SEV_WARN,     false },
    { NULL, NULL, SEV_INFO, false }
};

static const builtin_t builtin_stm32[] = {
    { "HARD_FAULT",       "HardFault_Handler",                 SEV_CRITICAL, true  },
    { "MEM_FAULT",        "MemManage_Handler",                 SEV_CRITICAL, true  },
    { "BUS_FAULT",        "BusFault_Handler",                  SEV_CRITICAL, true  },
    { "USAGE_FAULT",      "UsageFault_Handler",                SEV_CRITICAL, true  },
    { "ASSERT_FAILED",    "assert_failed",                     SEV_HIGH,     false },
    { "STACK_OVERFLOW",   "stack overflow",                    SEV_CRITICAL, true  },
    { NULL, NULL, SEV_INFO, false }
};

static const builtin_t builtin_arduino[] = {
    { "WATCHDOG_RESET",   "Watchdog Reset",                    SEV_HIGH,     false },
    { "ASSERT",           "Assertion .* failed",               SEV_HIGH,     false },
    { "CRASH",            "Sketch uses",                       SEV_INFO,     false },
    { NULL, NULL, SEV_INFO, false }
};

static const builtin_t builtin_freertos[] = {
    { "STACK_OVERFLOW",   "Stack overflow in task",            SEV_CRITICAL, true  },
    { "HEAP_FAILED",      "pvPortMalloc.*NULL",                SEV_HIGH,     false },
    { "ASSERT",           "FreeRTOS.*assert",                  SEV_HIGH,     false },
    { NULL, NULL, SEV_INFO, false }
};

static const builtin_t builtin_zephyr[] = {
    { "FATAL",            "FATAL FAULT",                       SEV_CRITICAL, true  },
    { "KERNEL_PANIC",     "KERNEL PANIC",                      SEV_CRITICAL, true  },
    { "ASSERT",           "__ASSERT",                          SEV_HIGH,     false },
    { NULL, NULL, SEV_INFO, false }
};

/* ── Helpers ───────────────────────────────────────────────────────────── */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

uint32_t det_hash(const char *rule_name, const char *line)
{
    /* FNV-1a 32-bit over rule_name + first 64 chars of line */
    uint32_t h = 2166136261u;
    for (const char *p = rule_name; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    int n = 0;
    for (const char *p = line; *p && n < 64; p++, n++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static bool dedup_seen(detector_t *d, uint32_t hash)
{
    int sz = (int)(sizeof(d->seen) / sizeof(d->seen[0]));
    for (int i = 0; i < sz; i++)
        if (d->seen[i] == hash) return true;
    return false;
}

static void dedup_add(detector_t *d, uint32_t hash)
{
    int sz = (int)(sizeof(d->seen) / sizeof(d->seen[0]));
    d->seen[d->seen_head % sz] = hash;
    d->seen_head++;
}

const char *severity_str(severity_t s)
{
    switch (s) {
    case SEV_INFO:     return "INFO";
    case SEV_WARN:     return "WARN";
    case SEV_HIGH:     return "HIGH";
    case SEV_CRITICAL: return "CRITICAL";
    default:           return "?";
    }
}

const char *severity_color(severity_t s)
{
    switch (s) {
    case SEV_WARN:     return COLOR_WARN;
    case SEV_HIGH:     return COLOR_HIGH;
    case SEV_CRITICAL: return COLOR_CRITICAL;
    default:           return "";
    }
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int detector_init(detector_t *d)
{
    if (!d) return -1;
    memset(d, 0, sizeof(*d));
    return 0;
}

void detector_free(detector_t *d)
{
    if (!d) return;
    for (int i = 0; i < d->nrules; i++)
        if (d->rules[i].active)
            regfree(&d->rules[i].compiled);
    d->nrules = 0;
}

/* ── Rules ─────────────────────────────────────────────────────────────── */

int detector_add_rule(detector_t *d, const char *name,
                      const char *pattern, severity_t sev,
                      bool auto_reset)
{
    if (!d || !name || !pattern) return -1;
    if (d->nrules >= DETECTOR_MAX_PATTERNS) return -1;

    det_rule_t *r = &d->rules[d->nrules];
    strncpy(r->name,    name,    sizeof(r->name)    - 1);
    strncpy(r->pattern, pattern, sizeof(r->pattern) - 1);
    r->severity   = sev;
    r->auto_reset = auto_reset;

    int flags = REG_EXTENDED | REG_NOSUB | REG_ICASE;
    if (regcomp(&r->compiled, pattern, flags) != 0) {
        fprintf(stderr, "detector: invalid regex: %s\n", pattern);
        return -1;
    }
    r->active = true;
    d->nrules++;
    return 0;
}

int detector_load_builtin(detector_t *d, const char *family)
{
    const builtin_t *tbl = NULL;

    if      (strcmp(family, "esp32")   == 0) tbl = builtin_esp32;
    else if (strcmp(family, "stm32")   == 0) tbl = builtin_stm32;
    else if (strcmp(family, "arduino") == 0) tbl = builtin_arduino;
    else if (strcmp(family, "freertos")== 0) tbl = builtin_freertos;
    else if (strcmp(family, "zephyr")  == 0) tbl = builtin_zephyr;
    else {
        fprintf(stderr, "detector: unknown family '%s'\n", family);
        return -1;
    }

    int loaded = 0;
    for (int i = 0; tbl[i].name; i++) {
        if (detector_add_rule(d, tbl[i].name, tbl[i].pattern,
                              tbl[i].sev, tbl[i].reset) == 0)
            loaded++;
    }
    return loaded;
}

int detector_load_file(detector_t *d, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }

    /*
     * File format (.pat):
     *   # comment
     *   SEVERITY  name  regex
     *
     *   SEVERITY = CRITICAL | HIGH | WARN | INFO
     *   name     = identifier (no spaces)
     *   regex    = POSIX extended regex (rest of line)
     *
     * Example:
     *   CRITICAL  GURU_MEDITATION  Guru Meditation Error
     */
    char line[512];
    int loaded = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* Skip comments and blank lines */
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;

        /* Parse: SEVERITY  NAME  REGEX */
        char sev_str[16], name[64], pattern[DETECTOR_MAX_RULE_LEN];
        if (sscanf(p, "%15s %63s %255[^\n]", sev_str, name, pattern) != 3)
            continue;

        severity_t sev = SEV_WARN;
        if      (strcasecmp(sev_str, "CRITICAL") == 0) sev = SEV_CRITICAL;
        else if (strcasecmp(sev_str, "HIGH")     == 0) sev = SEV_HIGH;
        else if (strcasecmp(sev_str, "WARN")     == 0) sev = SEV_WARN;
        else if (strcasecmp(sev_str, "INFO")     == 0) sev = SEV_INFO;

        if (detector_add_rule(d, name, pattern, sev, false) == 0)
            loaded++;
    }
    fclose(f);
    return loaded;
}

/* ── Detection ─────────────────────────────────────────────────────────── */

bool detector_check(detector_t *d, const char *line,
                    const char *device, det_event_t *event)
{
    if (!d || !line || !event) return false;

    for (int i = 0; i < d->nrules; i++) {
        det_rule_t *r = &d->rules[i];
        if (!r->active) continue;

        if (regexec(&r->compiled, line, 0, NULL, 0) == 0) {
            /* Match found */
            uint32_t h = det_hash(r->name, line);

            if (dedup_seen(d, h))
                return false;   /* duplicate — suppress */

            dedup_add(d, h);

            event->rule      = r;
            event->severity  = r->severity;
            event->hash      = h;
            event->timestamp = now_ms();
            strncpy(event->line,   line,   sizeof(event->line)   - 1);
            strncpy(event->device, device, sizeof(event->device) - 1);
            event->line[sizeof(event->line)   - 1] = '\0';
            event->device[sizeof(event->device) - 1] = '\0';

            return true;
        }
    }
    return false;
}
