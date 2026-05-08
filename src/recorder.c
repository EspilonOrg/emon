#include "recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static void write_ts(FILE *f)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);
    fprintf(f, "[%02d:%02d:%02d.%03ld] ",
            t->tm_hour, t->tm_min, t->tm_sec,
            ts.tv_nsec / 1000000);
}

static void rotate_if_needed(recorder_t *r)
{
    if (r->bytes_written < r->cfg->max_bytes) return;
    if (r->logfile) {
        fclose(r->logfile);
        r->logfile = fopen(r->logpath, "w"); /* truncate */
        r->bytes_written = 0;
    }
}

int recorder_init(recorder_t *r, const char *device, recorder_cfg_t *cfg)
{
    if (!r || !device || !cfg) return -1;
    memset(r, 0, sizeof(*r));
    strncpy(r->device, device, sizeof(r->device) - 1);
    r->cfg = cfg;

    /* Ensure logdir exists */
    mkdir(cfg->logdir, 0755);

    snprintf(r->logpath,   512,
             "%s/%s.log",   cfg->logdir, device);
    snprintf(r->crashpath, 512,
             "%s/%s_events.log", cfg->logdir, device);

    r->logfile   = fopen(r->logpath,   "a");
    r->crashfile = fopen(r->crashpath, "a");

    if (!r->logfile || !r->crashfile) return -1;

    /* Session header */
    struct tm *t;
    time_t now = time(NULL);
    t = localtime(&now);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", t);
    fprintf(r->logfile, "\n=== session %s [%s] ===\n", tbuf, device);
    return 0;
}

void recorder_free(recorder_t *r)
{
    if (!r) return;
    if (r->logfile)   { fclose(r->logfile);   r->logfile   = NULL; }
    if (r->crashfile) { fclose(r->crashfile); r->crashfile = NULL; }
}

void recorder_write(recorder_t *r, const char *line)
{
    if (!r || !line) return;

    /* Store in ring buffer for pre-event context */
    int slot = r->ctx_head % RECORDER_CONTEXT_LINES;
    strncpy(r->ctx[slot], line, RECORDER_MAX_LINE - 1);
    r->ctx_head++;
    if (r->ctx_count < RECORDER_CONTEXT_LINES) r->ctx_count++;

    /* Write to log file */
    if (r->logfile) {
        write_ts(r->logfile);
        fprintf(r->logfile, "%s\n", line);
        fflush(r->logfile);
        r->bytes_written += (long)(strlen(line) + 20);
        rotate_if_needed(r);
    }
}

void recorder_save_event(recorder_t *r, const det_event_t *ev)
{
    if (!r || !ev || !r->crashfile) return;

    fprintf(r->crashfile,
            "\n╔══════════════════════════════════════╗\n"
            "║ EVENT: %-30s║\n"
            "║ SEV:   %-30s║\n"
            "╚══════════════════════════════════════╝\n",
            ev->rule ? ev->rule->name : "UNKNOWN",
            severity_str(ev->severity));

    /* Write pre-event context */
    fprintf(r->crashfile, "── context (last %d lines) ──\n",
            r->ctx_count);
    int start = r->ctx_head - r->ctx_count;
    for (int i = 0; i < r->ctx_count; i++) {
        int slot = (start + i) % RECORDER_CONTEXT_LINES;
        fprintf(r->crashfile, "  %s\n", r->ctx[slot]);
    }

    /* Write the triggering line */
    fprintf(r->crashfile, ">> %s\n\n", ev->line);
    fflush(r->crashfile);
}
