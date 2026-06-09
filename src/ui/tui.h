#ifndef ESPILON_TUI_H
#define ESPILON_TUI_H

#include "monitor/detector.h"
#include "serial/serial.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>

#define TUI_MAX_PANES    8
#define TUI_PANE_LINES   500
#define TUI_PANE_WIDTH   512
#define TUI_HIST_SIZE    20
#define TUI_INPUT_MAX    256

typedef struct {
    /* Layout (1-based terminal coords) */
    int  x, y, w, h;

    int  dev_idx;
    char title[32];
    const char *color;

    /* Per-pane ring buffer */
    char lines[TUI_PANE_LINES][TUI_PANE_WIDTH];
    int  head;
    int  count;
    pthread_mutex_t lock;

    /* Scrollback state */
    int  scroll_offset;  /* lines scrolled up from tail (0 = live) */
    bool frozen;

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
    atomic_int      needs_redraw;
    pthread_mutex_t render_lock;
    uint64_t        start_ms;

    /* Ports - for UART send from input mode */
    serial_port_t  *ports[TUI_MAX_PANES];
    int             nports;

    /* Input mode */
    bool            input_mode;
    char            input_buf[TUI_INPUT_MAX];
    int             input_len;
    int             input_esc;      /* ESC sequence state in input mode */

    /* Command history */
    char            history[TUI_HIST_SIZE][TUI_INPUT_MAX];
    int             hist_head;      /* next write slot */
    int             hist_count;     /* filled slots */
    int             hist_sel;       /* -1 = not browsing */
} tui_t;

extern tui_t g_tui;

/* Lifecycle */
void tui_init(int n, const char **names, const char **colors);
void tui_destroy(void);
bool tui_is_active(void);

/* Register serial ports for UART send (call before monitor_run) */
void tui_set_ports(serial_port_t **ports, int n);

/* Push lines into a pane */
void tui_push_line(int dev_idx, const char *line);
void tui_push_event(int dev_idx, severity_t sev, const char *line);

/* Full redraw */
void tui_draw_all(void);

/* Key handler: returns true if key was consumed */
bool tui_handle_key(uint8_t c, bool ctrl_a_pending);

/* Called from SIGWINCH handler */
void tui_signal_resize(void);

#endif
