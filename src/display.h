#ifndef ESPILON_DISPLAY_H
#define ESPILON_DISPLAY_H

#include "detector.h"
#include <stdbool.h>

typedef struct {
    bool    verbose;      /* print INFO lines too */
    bool    timestamps;   /* prepend HH:MM:SS.mmm */
    bool    color;        /* ANSI colors */
    bool    show_stats;   /* live stats line */
} display_cfg_t;

void display_init(const display_cfg_t *cfg);

/* Print a raw line (no event matched) */
void display_line(const char *device, const char *color,
                  const char *line);

/* Print a matched event with severity badge */
void display_event(const det_event_t *ev, const char *device_color);

/* Print stats summary */
void display_stats(int ndevices, const char **names,
                   const int *crash_counts, uint64_t uptime_ms);

/* Print startup banner */
void display_banner(int nports);

/* Assign a color to a device by index */
const char *display_device_color(int idx);

#endif
