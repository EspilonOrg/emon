#ifndef ESPILON_DETECTOR_H
#define ESPILON_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include <regex.h>

#define DETECTOR_MAX_PATTERNS   256
#define DETECTOR_MAX_RULE_LEN   256
#define DETECTOR_CONTEXT_LINES  10   /* lines captured before/after event */

/* ── Severity levels ───────────────────────────────────────────────────── */
typedef enum {
    SEV_INFO     = 0,
    SEV_WARN     = 1,
    SEV_HIGH     = 2,
    SEV_CRITICAL = 3,
} severity_t;

/* ── A single detection rule ───────────────────────────────────────────── */
typedef struct {
    char        name[64];       /* e.g. "GURU_MEDITATION" */
    char        pattern[DETECTOR_MAX_RULE_LEN];
    severity_t  severity;
    regex_t     compiled;
    bool        auto_reset;     /* trigger hardware reset on match */
    bool        active;
} det_rule_t;

/* ── Result of a detection ─────────────────────────────────────────────── */
typedef struct {
    const det_rule_t   *rule;       /* matched rule, NULL if no match */
    severity_t          severity;
    char                line[1024]; /* matched line */
    uint64_t            timestamp;  /* ms since epoch */
    char                device[32]; /* device name */
    uint32_t            hash;       /* simple dedup hash */
} det_event_t;

/* ── Detector context ──────────────────────────────────────────────────── */
typedef struct {
    det_rule_t  rules[DETECTOR_MAX_PATTERNS];
    int         nrules;

    /* Deduplication: ring buffer of recent hashes */
    uint32_t    seen[512];
    int         seen_head;
} detector_t;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */
int  detector_init(detector_t *d);
void detector_free(detector_t *d);

/* ── Rules ─────────────────────────────────────────────────────────────── */

/* Add a rule programmatically */
int  detector_add_rule(detector_t *d, const char *name,
                       const char *pattern, severity_t sev,
                       bool auto_reset);

/* Load rules from a .pat file */
int  detector_load_file(detector_t *d, const char *path);

/* ── Detection ─────────────────────────────────────────────────────────── */

/*
 * Run `line` against all rules.
 * Returns true and fills `event` if a rule matched.
 * Returns false if no match (INFO level, not an event).
 */
bool detector_check(detector_t *d, const char *line,
                    const char *device, det_event_t *event);

/* ── Helpers ───────────────────────────────────────────────────────────── */
const char *severity_str(severity_t s);
const char *severity_color(severity_t s);  /* ANSI escape */
uint32_t    det_hash(const char *rule_name, const char *line);

#endif /* ESPILON_DETECTOR_H */
