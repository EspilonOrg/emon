#ifndef ESPILON_TUI_H
#define ESPILON_TUI_H

#include "detector.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

#define TUI_MAX_PANES   8
#define TUI_PANE_LINES  500
#define TUI_PANE_WIDTH  512

typedef struct {
    /* Layout (1-based terminal coords) */
    int  x, y, w, h;

    int  dev_idx;
    char title[32];
    const char *color;   /* ANSI device color */

    /* Per-pane ring buffer */
    char lines[TUI_PANE_LINES][TUI_PANE_WIDTH];
    int  head;           /* next write slot */
    int  count;          /* filled slots */
    pthread_mutex_t lock;

    /* Per-pane scrollback state */
    int  scroll_offset;  /* lines scrolled up from tail (0 = live) */
    bool frozen;         /* true = in-pane scrollback active */

    /* Stats */
    int  event_count;
    bool has_critical;
} tui_pane_t;

typedef struct {
    tui_pane_t      panes[TUI_MAX_PANES];
    int             npanes;
    int             focused;
    int             term_rows, term_cols;
    bool            active;
    atomic_int      needs_redraw;   /* set by SIGWINCH handler */
    pthread_mutex_t render_lock;
    uint64_t        start_ms;
} tui_t;

extern tui_t g_tui;

/* Lifecycle */
void tui_init(int n, const char **names, const char **colors);
void tui_destroy(void);
bool tui_is_active(void);

/* Push lines into a pane (called from display.c under mon->print_lock) */
void tui_push_line(int dev_idx, const char *line);
void tui_push_event(int dev_idx, severity_t sev, const char *line);

/* Full redraw (call after SIGWINCH or on startup) */
void tui_draw_all(void);

/* Key handler: returns true if key was consumed by TUI */
bool tui_handle_key(uint8_t c, bool ctrl_a_pending);

/* Called from SIGWINCH handler */
void tui_signal_resize(void);

#endif
