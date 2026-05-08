#ifndef ESPILON_RECORDER_H
#define ESPILON_RECORDER_H

#include "detector.h"
#include <stdio.h>

#define RECORDER_CONTEXT_LINES  15
#define RECORDER_MAX_LINE       1024
#define RECORDER_MAX_LOGSIZE    (50 * 1024 * 1024)  /* 50MB rotation */

typedef struct {
    char    logdir[256];
    int     context_lines;  /* lines before event to save */
    long    max_bytes;      /* log rotation threshold */
} recorder_cfg_t;

typedef struct {
    char            device[32];
    char            logpath[256];
    char            crashpath[256];
    FILE           *logfile;
    FILE           *crashfile;
    /* Ring buffer: last N lines for pre-event context */
    char            ctx[RECORDER_CONTEXT_LINES][RECORDER_MAX_LINE];
    int             ctx_head;
    int             ctx_count;
    long            bytes_written;
    recorder_cfg_t *cfg;
} recorder_t;

int  recorder_init(recorder_t *r, const char *device,
                   recorder_cfg_t *cfg);
void recorder_free(recorder_t *r);

/* Call for every line received */
void recorder_write(recorder_t *r, const char *line);

/* Call when an event (crash) is detected */
void recorder_save_event(recorder_t *r, const det_event_t *ev);

#endif
