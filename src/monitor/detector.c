#include "monitor/detector.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* ── ANSI colors ───────────────────────────────────────────────────────── */
#define COLOR_RESET    "\033[0m"
#define COLOR_WARN     "\033[33m"
#define COLOR_HIGH     "\033[91m"
#define COLOR_CRITICAL "\033[1;91m"

/* ── Helpers ───────────────────────────────────────────────────────────── */

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
    snprintf(r->name,    sizeof(r->name),    "%s", name);
    snprintf(r->pattern, sizeof(r->pattern), "%s", pattern);
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

int detector_load_file(detector_t *d, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return -1;
    }

    char line[512];
    int  loaded = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';

        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') continue;

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
            uint32_t h = det_hash(r->name, line);

            if (dedup_seen(d, h))
                return false;

            dedup_add(d, h);

            event->rule      = r;
            event->severity  = r->severity;
            event->hash      = h;
            event->timestamp = now_ms();
            snprintf(event->line,   sizeof(event->line),   "%s", line);
            snprintf(event->device, sizeof(event->device), "%s", device);

            return true;
        }
    }
    return false;
}
