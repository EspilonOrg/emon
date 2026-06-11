#include "ui/tui.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

tui_t g_tui;

/* ── Terminal size ───────────────────────────────────────────────── */

static void term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/* ── Render buffer ───────────────────────────────────────────────── */

static char s_rbuf[256 * 1024];
static int  s_rlen;

static void rb_reset(void) { s_rlen = 0; }

static void rb_append(const char *s)
{
    if (!s) return;
    int rem = (int)sizeof(s_rbuf) - s_rlen - 1;
    if (rem <= 0) return;
    int len = (int)strlen(s);
    if (len > rem) len = rem;
    memcpy(s_rbuf + s_rlen, s, len);
    s_rlen += len;
}

static void rb_appendf(const char *fmt, ...)
{
    int rem = (int)sizeof(s_rbuf) - s_rlen - 1;
    if (rem <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_rbuf + s_rlen, (size_t)rem, fmt, ap);
    va_end(ap);
    if (n > 0) s_rlen += (n < rem) ? n : rem - 1;
}

static void rb_appendn(char c, int n)
{
    while (n-- > 0 && s_rlen < (int)sizeof(s_rbuf) - 1)
        s_rbuf[s_rlen++] = c;
}

static void rb_flush(void)
{
    if (s_rlen > 0) {
        write(STDOUT_FILENO, s_rbuf, s_rlen);
        s_rlen = 0;
    }
}

/* ── Layout engine ───────────────────────────────────────────────── */

static void tui_compute_layout(void)
{
    int rows = g_tui.term_rows;
    int cols = g_tui.term_cols;
    int n    = g_tui.npanes;

    if (n == 0) return;

    /* Reserve 2 rows at bottom: 1 status bar + 1 input bar */
    int avail  = rows - 2;
    int nrows  = (n + 1) / 2;
    int row_h  = avail / nrows;
    if (row_h < 3) row_h = 3;
    int col_w  = cols / 2;

    for (int i = 0; i < n; i++) {
        tui_pane_t *p = &g_tui.panes[i];
        int grid_row  = i / 2;
        int grid_col  = i % 2;
        bool last_odd = (n % 2 == 1) && (i == n - 1);

        p->y = grid_row * row_h + 1;
        p->h = row_h;

        if (last_odd || n == 1) {
            p->x = 1;
            p->w = cols;
        } else {
            p->x = grid_col * col_w + 1;
            p->w = col_w;
        }
    }
}

/* ── Draw a single pane ──────────────────────────────────────────── */

static void draw_pane(int idx)
{
    tui_pane_t *p    = &g_tui.panes[idx];
    bool focused     = (idx == g_tui.focused);
    bool critical    = p->has_critical;
    bool frozen      = p->frozen;

    int content_w = p->w - 2;
    int content_h = p->h - 2;
    if (content_w < 1) content_w = 1;
    if (content_h < 1) content_h = 1;

    const char *bcol;
    if      (critical) bcol = "\033[31m";
    else if (focused)  bcol = "\033[1;37m";
    else               bcol = "\033[2;37m";

    /* ── Top border ─────────────────────────────────────────── */
    rb_appendf("\033[%d;%dH", p->y, p->x);
    rb_append(bcol);
    rb_append("+");

    char vis_title[64];
    if (frozen)
        snprintf(vis_title, sizeof(vis_title), "[SCROLL] %s", p->title);
    else
        snprintf(vis_title, sizeof(vis_title), " %s ", p->title);

    int title_vis = (int)strlen(vis_title);
    int dashes    = content_w - 1 - title_vis;
    rb_append("-");

    if (frozen)
        rb_appendf("\033[33m[SCROLL] %s\033[0m%s", p->title, bcol);
    else
        rb_appendf(" %s ", p->title);

    if (dashes > 0) rb_appendn('-', dashes);
    rb_append("+");
    rb_append("\033[0m");

    /* ── Content lines ──────────────────────────────────────── */
    pthread_mutex_lock(&p->lock);
    int total = p->count;

    int from = total - content_h - p->scroll_offset;
    if (from < 0) from = 0;

    for (int row = 0; row < content_h; row++) {
        int line_idx = from + row;
        int abs_row  = p->y + 1 + row;

        rb_appendf("\033[%d;%dH", abs_row, p->x);
        rb_append(bcol);
        rb_append("|");
        rb_append("\033[0m");

        rb_appendf("\033[%d;%dH", abs_row, p->x + 1);

        if (line_idx >= 0 && line_idx < total) {
            int slot = (p->head - total + line_idx + TUI_PANE_LINES * 4) % TUI_PANE_LINES;
            const char *raw = p->lines[slot];

            char stripped[TUI_PANE_WIDTH];
            int vis = strip_ansi(raw, stripped, sizeof(stripped));

            /* Highlight CRITICAL lines with dim red background */
            bool is_critical = (strstr(raw, "[CRITICAL]") != NULL);
            if (is_critical) rb_append("\033[41m");

            if (vis <= content_w) {
                rb_append(raw);
                rb_append("\033[0m");
                rb_appendn(' ', content_w - vis);
            } else {
                int tlen = content_w - 3;
                if (tlen < 0) tlen = 0;
                stripped[tlen] = '\0';
                rb_append(stripped);
                rb_append("\033[2m...\033[0m");
            }
        } else {
            rb_appendn(' ', content_w);
        }

        rb_appendf("\033[%d;%dH", abs_row, p->x + p->w - 1);
        rb_append(bcol);
        rb_append("|");
        rb_append("\033[0m");
    }
    pthread_mutex_unlock(&p->lock);

    /* ── Bottom border ──────────────────────────────────────── */
    rb_appendf("\033[%d;%dH", p->y + p->h - 1, p->x);
    rb_append(bcol);
    rb_append("+");
    rb_appendn('-', p->w - 2);
    rb_append("+");
    rb_append("\033[0m");
}

/* ── Status bar ──────────────────────────────────────────────────── */

static void draw_statusbar(void)
{
    uint64_t uptime = now_ms() - g_tui.start_ms;
    uint64_t s = uptime / 1000;
    uint64_t h = s / 3600; s %= 3600;
    uint64_t m = s / 60;   s %= 60;

    /* Status bar: second-to-last row */
    rb_appendf("\033[%d;1H\033[7m", g_tui.term_rows - 1);
    rb_append(" emon");

    for (int i = 0; i < g_tui.npanes; i++) {
        tui_pane_t *p = &g_tui.panes[i];
        if (i == g_tui.focused)
            rb_appendf("  \033[1m>%s:%d\033[0m\033[7m", p->title, p->event_count);
        else
            rb_appendf("  %s:%d", p->title, p->event_count);
        if (p->has_critical) rb_append("!");
    }

    rb_appendf("  %02llu:%02llu:%02llu",
               (unsigned long long)h,
               (unsigned long long)m,
               (unsigned long long)s);

    rb_append("  \033[2m\033[7m↑:scroll  G:live  Enter:send  Tab:next  Ctrl+Q:quit\033[0m\033[7m");
    rb_append("\033[K\033[0m");
}

/* ── Input bar ───────────────────────────────────────────────────── */

static void draw_inputbar(void)
{
    rb_appendf("\033[%d;1H\033[K", g_tui.term_rows);

    if (g_tui.input_mode) {
        tui_pane_t *p = &g_tui.panes[g_tui.focused];
        rb_appendf("\033[1m[%s]>\033[0m %s", p->title, g_tui.input_buf);
    }
}

/* ── Public: full redraw ─────────────────────────────────────────── */

void tui_draw_all(void)
{
    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    rb_append("\033[H\033[2J");
    for (int i = 0; i < g_tui.npanes; i++) draw_pane(i);
    draw_statusbar();
    draw_inputbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

/* ── Init / destroy ──────────────────────────────────────────────── */

void tui_init(int n, const char **names, const char **colors)
{
    memset(&g_tui, 0, sizeof(g_tui));
    pthread_mutex_init(&g_tui.render_lock, NULL);
    atomic_store(&g_tui.needs_redraw, 0);

    g_tui.start_ms  = now_ms();
    g_tui.npanes    = n > TUI_MAX_PANES ? TUI_MAX_PANES : n;
    g_tui.focused   = 0;
    g_tui.hist_sel  = -1;

    for (int i = 0; i < g_tui.npanes; i++) {
        tui_pane_t *p = &g_tui.panes[i];
        p->dev_idx    = i;
        p->color      = colors[i];
        strncpy(p->title, names[i], sizeof(p->title) - 1);
        pthread_mutex_init(&p->lock, NULL);
    }

    term_size(&g_tui.term_rows, &g_tui.term_cols);
    tui_compute_layout();

    write(STDOUT_FILENO, "\033[?1049h", 8);
    write(STDOUT_FILENO, "\033[?25l",   6);

    g_tui.active = true;
    tui_draw_all();
}

void tui_destroy(void)
{
    if (!g_tui.active) return;
    g_tui.active = false;

    write(STDOUT_FILENO, "\033[?25h",   6);
    write(STDOUT_FILENO, "\033[?1049l", 8);

    for (int i = 0; i < g_tui.npanes; i++)
        pthread_mutex_destroy(&g_tui.panes[i].lock);
    pthread_mutex_destroy(&g_tui.render_lock);
}

bool tui_is_active(void)
{
    return g_tui.active;
}

void tui_set_ports(serial_port_t **ports, int n)
{
    g_tui.nports = n > TUI_MAX_PANES ? TUI_MAX_PANES : n;
    for (int i = 0; i < g_tui.nports; i++)
        g_tui.ports[i] = ports[i];
}

/* ── Push ────────────────────────────────────────────────────────── */

void tui_push_line(int dev_idx, const char *line)
{
    if (!g_tui.active || dev_idx < 0 || dev_idx >= g_tui.npanes) return;

    tui_pane_t *p = &g_tui.panes[dev_idx];

    pthread_mutex_lock(&p->lock);
    strncpy(p->lines[p->head], line, TUI_PANE_WIDTH - 1);
    p->lines[p->head][TUI_PANE_WIDTH - 1] = '\0';
    p->head = (p->head + 1) % TUI_PANE_LINES;
    if (p->count < TUI_PANE_LINES) p->count++;
    pthread_mutex_unlock(&p->lock);

    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    if (atomic_exchange(&g_tui.needs_redraw, 0)) {
        term_size(&g_tui.term_rows, &g_tui.term_cols);
        tui_compute_layout();
        rb_append("\033[H\033[2J");
        for (int i = 0; i < g_tui.npanes; i++) draw_pane(i);
    } else if (!p->frozen) {
        draw_pane(dev_idx);
    }
    draw_statusbar();
    draw_inputbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

void tui_push_event(int dev_idx, severity_t sev, const char *line)
{
    if (!g_tui.active || dev_idx < 0 || dev_idx >= g_tui.npanes) return;
    tui_pane_t *p = &g_tui.panes[dev_idx];
    p->event_count++;
    if (sev >= SEV_CRITICAL) p->has_critical = true;
    tui_push_line(dev_idx, line);
}

/* ── SIGWINCH ────────────────────────────────────────────────────── */

void tui_signal_resize(void)
{
    atomic_store(&g_tui.needs_redraw, 1);
}

/* ── Scroll helpers ──────────────────────────────────────────────── */

static void pane_freeze(tui_pane_t *p)
{
    p->frozen = true;
}

static void pane_unfreeze(tui_pane_t *p)
{
    p->frozen        = false;
    p->scroll_offset = 0;
}

static void scroll_pane(int delta)
{
    tui_pane_t *p = &g_tui.panes[g_tui.focused];
    p->scroll_offset += delta;
    if (p->scroll_offset < 0) p->scroll_offset = 0;
    int content_h = p->h - 2;
    int max_off   = p->count > content_h ? p->count - content_h : 0;
    if (p->scroll_offset > max_off) p->scroll_offset = max_off;

    /* Auto-unfreeze when scrolled back to tail */
    if (p->scroll_offset == 0) pane_unfreeze(p);
}

static void redraw_focused(void)
{
    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    draw_pane(g_tui.focused);
    draw_statusbar();
    draw_inputbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

static void redraw_all(void)
{
    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    for (int i = 0; i < g_tui.npanes; i++) draw_pane(i);
    draw_statusbar();
    draw_inputbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

/* ── History helpers ─────────────────────────────────────────────── */

static void hist_push(const char *cmd)
{
    if (!cmd[0]) return;
    snprintf(g_tui.history[g_tui.hist_head], TUI_INPUT_MAX, "%s", cmd);
    g_tui.hist_head = (g_tui.hist_head + 1) % TUI_HIST_SIZE;
    if (g_tui.hist_count < TUI_HIST_SIZE) g_tui.hist_count++;
}

static void hist_navigate(int dir)
{
    if (g_tui.hist_count == 0) return;
    if (g_tui.hist_sel == -1 && dir > 0) return;

    if (dir < 0) {
        /* Up → older */
        int next = (g_tui.hist_sel == -1)
                   ? g_tui.hist_count - 1
                   : g_tui.hist_sel - 1;
        if (next < 0) next = 0;
        g_tui.hist_sel = next;
    } else {
        /* Down → newer */
        int next = g_tui.hist_sel + 1;
        if (next >= g_tui.hist_count) {
            g_tui.hist_sel = -1;
            g_tui.input_buf[0] = '\0';
            g_tui.input_len = 0;
            return;
        }
        g_tui.hist_sel = next;
    }

    int slot = (g_tui.hist_head - g_tui.hist_count + g_tui.hist_sel
                + TUI_HIST_SIZE * 4) % TUI_HIST_SIZE;
    strncpy(g_tui.input_buf, g_tui.history[slot], TUI_INPUT_MAX - 1);
    g_tui.input_len = (int)strlen(g_tui.input_buf);
}

/* ── Input mode send ─────────────────────────────────────────────── */

static void input_send(void)
{
    if (!g_tui.input_len) return;

    int idx = g_tui.focused;
    if (idx < g_tui.nports && g_tui.ports[idx]) {
        /* Send char by char with small delay - linenoise smart mode echoes each byte */
        struct timespec delay = {0, 5000000L};  /* 5ms between chars */
        for (int i = 0; i < g_tui.input_len; i++) {
            serial_write(g_tui.ports[idx], (uint8_t *)&g_tui.input_buf[i], 1);
            nanosleep(&delay, NULL);
        }
        serial_write_str(g_tui.ports[idx], "\r");

        /* Visual echo in the pane */
        char echo[TUI_INPUT_MAX + 16];
        snprintf(echo, sizeof(echo), "\033[2m>> %s\033[0m", g_tui.input_buf);
        tui_push_line(idx, echo);
    }
    hist_push(g_tui.input_buf);
}

/* ── Key handler ─────────────────────────────────────────────────── */

bool tui_handle_key(uint8_t c, bool ctrl_a_pending)
{
    if (!g_tui.active) return false;

    /* ── Ctrl+A combos (always take priority) ───────────────── */
    if (ctrl_a_pending) {
        /* X handled by interactive.c - don't consume */
        return false;
    }

    /* ── Input mode ─────────────────────────────────────────── */
    if (g_tui.input_mode) {
        /* ESC sequence handling inside input mode */
        if (g_tui.input_esc == 1) {
            g_tui.input_esc = (c == '[') ? 2 : 0;
            return true;
        }
        if (g_tui.input_esc == 2) {
            g_tui.input_esc = 0;
            if (c == 'A') { hist_navigate(-1); redraw_focused(); }  /* ↑ */
            else if (c == 'B') { hist_navigate(+1); redraw_focused(); }  /* ↓ */
            return true;
        }

        if (c == 0x1b) { g_tui.input_esc = 1; return true; }

        if (c == '\r') {
            /* Send */
            input_send();
            g_tui.input_mode = false;
            g_tui.input_buf[0] = '\0';
            g_tui.input_len = 0;
            g_tui.hist_sel = -1;
            redraw_focused();
            return true;
        }

        if (c == 0x03) {
            /* Ctrl+C → cancel input mode */
            g_tui.input_mode = false;
            g_tui.input_buf[0] = '\0';
            g_tui.input_len = 0;
            g_tui.hist_sel = -1;
            redraw_focused();
            return true;
        }

        if ((c == 0x7f || c == '\b') && g_tui.input_len > 0) {
            g_tui.input_buf[--g_tui.input_len] = '\0';
            redraw_focused();
            return true;
        }

        if (c >= 0x20 && g_tui.input_len < TUI_INPUT_MAX - 1) {
            g_tui.input_buf[g_tui.input_len++] = (char)c;
            g_tui.input_buf[g_tui.input_len]   = '\0';
            redraw_focused();
            return true;
        }

        return true;
    }

    /* ── ESC sequence state machine (navigation mode) ───────── */
    static int s_esc = 0;

    if (s_esc == 1) {
        s_esc = (c == '[') ? 2 : 0;
        return true;
    }
    if (s_esc == 2) {
        s_esc = 0;
        tui_pane_t *p = &g_tui.panes[g_tui.focused];
        int pg = (p->h > 4) ? p->h - 3 : 1;

        if (c == 'A') {
            /* ↑ - freeze + scroll up */
            pane_freeze(p);
            scroll_pane(+1);
            redraw_focused();
        } else if (c == 'B') {
            /* ↓ - scroll down (auto-unfreeze at tail) */
            if (p->frozen) {
                scroll_pane(-1);
                redraw_focused();
            }
        } else if (c == '5') {
            s_esc = 3; return true;
        } else if (c == '6') {
            s_esc = 4; return true;
        } else if (c == 'H') {
            /* Home key - jump to top */
            if (p->frozen) {
                int content_h = p->h - 2;
                int max_off = p->count > content_h ? p->count - content_h : 0;
                p->scroll_offset = max_off;
                redraw_focused();
            }
        } else if (c == 'F') {
            /* End key - live tail */
            pane_unfreeze(p);
            redraw_focused();
        }
        (void)pg;
        return true;
    }
    if (s_esc == 3) {
        s_esc = 0;
        if (c == '~') {
            tui_pane_t *p = &g_tui.panes[g_tui.focused];
            int pg = (p->h > 4) ? p->h - 3 : 1;
            pane_freeze(p);
            scroll_pane(+pg);
            redraw_focused();
        }
        return true;
    }
    if (s_esc == 4) {
        s_esc = 0;
        if (c == '~') {
            tui_pane_t *p = &g_tui.panes[g_tui.focused];
            int pg = (p->h > 4) ? p->h - 3 : 1;
            if (p->frozen) { scroll_pane(-pg); redraw_focused(); }
        }
        return true;
    }
    if (c == 0x1b) {
        s_esc = 1;
        return true;
    }

    /* ── Ctrl+Q: quit ───────────────────────────────────────── */
    if (c == 0x11) {
        extern void monitor_stop_all(void);
        monitor_stop_all();
        return true;
    }

    /* ── Tab: cycle focused pane ────────────────────────────── */
    if (c == '\t') {
        g_tui.focused = (g_tui.focused + 1) % g_tui.npanes;
        redraw_all();
        return true;
    }

    /* ── Enter: open input mode (only \r, ignore \n that follows) ── */
    if (c == '\r') {
        g_tui.input_mode = true;
        g_tui.input_buf[0] = '\0';
        g_tui.input_len = 0;
        g_tui.hist_sel = -1;
        g_tui.input_esc = 0;
        redraw_focused();
        return true;
    }

    /* ── G: live tail ───────────────────────────────────────── */
    if (c == 'G') {
        tui_pane_t *p = &g_tui.panes[g_tui.focused];
        pane_unfreeze(p);
        redraw_focused();
        return true;
    }

    return false;
}
