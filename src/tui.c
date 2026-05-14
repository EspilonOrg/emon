#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/ioctl.h>

tui_t g_tui;

/* ── Time helper ─────────────────────────────────────────────────── */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

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

/* ── ANSI strip ──────────────────────────────────────────────────── */

static int strip_ansi(const char *src, char *dst, int maxlen)
{
    int di = 0;
    for (int si = 0; src[si] && di < maxlen - 1; si++) {
        if ((unsigned char)src[si] == 0x1b && src[si+1] == '[') {
            si += 2;
            while (src[si] && !((src[si] >= 'A' && src[si] <= 'Z') ||
                                 (src[si] >= 'a' && src[si] <= 'z')))
                si++;
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
    return di;
}

/* ── Layout engine ───────────────────────────────────────────────── */

static void tui_compute_layout(void)
{
    int rows = g_tui.term_rows;
    int cols = g_tui.term_cols;
    int n    = g_tui.npanes;

    if (n == 0) return;

    int nrows  = (n + 1) / 2;
    int row_h  = (rows - 1) / nrows;   /* -1 for status bar */
    if (row_h < 3) row_h = 3;
    int col_w  = cols / 2;

    for (int i = 0; i < n; i++) {
        tui_pane_t *p  = &g_tui.panes[i];
        int grid_row   = i / 2;
        int grid_col   = i % 2;
        bool last_odd  = (n % 2 == 1) && (i == n - 1);

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

    /* Border color */
    const char *bcol;
    if      (critical) bcol = "\033[31m";
    else if (focused)  bcol = "\033[1;37m";
    else               bcol = "\033[2;37m";

    /* ── Top border ─────────────────────────────────────────── */
    rb_appendf("\033[%d;%dH", p->y, p->x);
    rb_append(bcol);
    rb_append("+");

    /* Visual title for width calculation */
    char vis_title[48];
    if (frozen)
        snprintf(vis_title, sizeof(vis_title), "[SCROLL] %s", p->title);
    else
        snprintf(vis_title, sizeof(vis_title), " %s ", p->title);

    int title_vis = (int)strlen(vis_title);
    int dashes    = content_w - 1 - title_vis;
    rb_append("-");

    if (frozen) {
        rb_appendf("\033[33m[SCROLL] %s\033[0m%s", p->title, bcol);
    } else {
        rb_appendf(" %s ", p->title);
    }

    if (dashes > 0) rb_appendn('-', dashes);
    rb_append("+");
    rb_append("\033[0m");

    /* ── Content lines ──────────────────────────────────────── */
    pthread_mutex_lock(&p->lock);
    int total = p->count;

    /* Which line in the logical sequence to start from:
     * scroll_offset=0 → tail: show lines [total-content_h, total)
     * scroll_offset=N → show lines [total-content_h-N, total-N) */
    int from = total - content_h - p->scroll_offset;
    if (from < 0) from = 0;

    for (int row = 0; row < content_h; row++) {
        int line_idx = from + row;
        int abs_row  = p->y + 1 + row;

        rb_appendf("\033[%d;%dH", abs_row, p->x);
        rb_append(bcol);
        rb_append("|");
        rb_append("\033[0m");

        /* Jump to content start */
        rb_appendf("\033[%d;%dH", abs_row, p->x + 1);

        if (line_idx >= 0 && line_idx < total) {
            int slot = (p->head - total + line_idx + TUI_PANE_LINES * 4) % TUI_PANE_LINES;
            const char *raw = p->lines[slot];

            char stripped[TUI_PANE_WIDTH];
            int vis = strip_ansi(raw, stripped, sizeof(stripped));

            if (vis <= content_w) {
                rb_append(raw);
                rb_append("\033[0m");
                rb_appendn(' ', content_w - vis);
            } else {
                /* Truncate stripped version */
                int tlen = content_w - 3;
                if (tlen < 0) tlen = 0;
                stripped[tlen] = '\0';
                rb_append(stripped);
                rb_append("\033[2m...\033[0m");
            }
        } else {
            rb_appendn(' ', content_w);
        }

        /* Right border — jump to exact column to avoid misalignment */
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

    rb_appendf("\033[%d;1H\033[7m", g_tui.term_rows);

    rb_append(" espilon-monitor");

    for (int i = 0; i < g_tui.npanes; i++) {
        tui_pane_t *p = &g_tui.panes[i];
        rb_appendf("  %s:%d", p->title, p->event_count);
        if (p->has_critical) rb_append("!");
    }

    rb_appendf("  %02llu:%02llu:%02llu",
               (unsigned long long)h,
               (unsigned long long)m,
               (unsigned long long)s);

    rb_append("  Tab:next  Ctrl+A[:scroll  Ctrl+AX:quit");

    rb_append("\033[K\033[0m");
}

/* ── Public: full redraw ─────────────────────────────────────────── */

void tui_draw_all(void)
{
    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    rb_append("\033[H\033[2J");
    for (int i = 0; i < g_tui.npanes; i++) draw_pane(i);
    draw_statusbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

/* ── Init / destroy ──────────────────────────────────────────────── */

void tui_init(int n, const char **names, const char **colors)
{
    memset(&g_tui, 0, sizeof(g_tui));
    pthread_mutex_init(&g_tui.render_lock, NULL);
    atomic_store(&g_tui.needs_redraw, 0);

    g_tui.start_ms = now_ms();
    g_tui.npanes   = n > TUI_MAX_PANES ? TUI_MAX_PANES : n;
    g_tui.focused  = 0;

    for (int i = 0; i < g_tui.npanes; i++) {
        tui_pane_t *p = &g_tui.panes[i];
        p->dev_idx    = i;
        p->color      = colors[i];
        strncpy(p->title, names[i], sizeof(p->title) - 1);
        pthread_mutex_init(&p->lock, NULL);
    }

    term_size(&g_tui.term_rows, &g_tui.term_cols);
    tui_compute_layout();

    /* Enter alternate screen + hide cursor */
    write(STDOUT_FILENO, "\033[?1049h", 8);
    write(STDOUT_FILENO, "\033[?25l",   6);

    g_tui.active = true;
    tui_draw_all();
}

void tui_destroy(void)
{
    if (!g_tui.active) return;
    g_tui.active = false;

    write(STDOUT_FILENO, "\033[?25h",   6);   /* show cursor */
    write(STDOUT_FILENO, "\033[?1049l", 8);   /* leave alt screen */

    for (int i = 0; i < g_tui.npanes; i++)
        pthread_mutex_destroy(&g_tui.panes[i].lock);
    pthread_mutex_destroy(&g_tui.render_lock);
}

bool tui_is_active(void)
{
    return g_tui.active;
}

/* ── Push ────────────────────────────────────────────────────────── */

void tui_push_line(int dev_idx, const char *line)
{
    if (!g_tui.active || dev_idx < 0 || dev_idx >= g_tui.npanes) return;

    tui_pane_t *p = &g_tui.panes[dev_idx];

    /* 1. Insert into ring buffer */
    pthread_mutex_lock(&p->lock);
    strncpy(p->lines[p->head], line, TUI_PANE_WIDTH - 1);
    p->lines[p->head][TUI_PANE_WIDTH - 1] = '\0';
    p->head = (p->head + 1) % TUI_PANE_LINES;
    if (p->count < TUI_PANE_LINES) p->count++;
    pthread_mutex_unlock(&p->lock);

    /* 2. Incremental or full redraw */
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

/* ── Key handler ─────────────────────────────────────────────────── */

/* ESC sequence state machine (static — single stdin thread) */
static int s_esc = 0;   /* 0=normal 1=saw ESC 2=saw ESC[ 3=ESC[5 4=ESC[6 */

static void scroll_pane(int delta)
{
    tui_pane_t *p = &g_tui.panes[g_tui.focused];
    if (!p->frozen) return;

    p->scroll_offset += delta;
    if (p->scroll_offset < 0) p->scroll_offset = 0;

    /* If scrolled past tail, unfreeze */
    int content_h = p->h - 2;
    int max_off   = p->count > content_h ? p->count - content_h : 0;
    if (p->scroll_offset > max_off) p->scroll_offset = max_off;
}

static void redraw_focused(void)
{
    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    draw_pane(g_tui.focused);
    draw_statusbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

static void redraw_all_borders(void)
{
    pthread_mutex_lock(&g_tui.render_lock);
    rb_reset();
    for (int i = 0; i < g_tui.npanes; i++) draw_pane(i);
    draw_statusbar();
    rb_flush();
    pthread_mutex_unlock(&g_tui.render_lock);
}

bool tui_handle_key(uint8_t c, bool ctrl_a_pending)
{
    if (!g_tui.active) return false;

    /* ── Handle Ctrl+A combos ───────────────────────────────── */
    if (ctrl_a_pending) {
        if (c == '[') {
            tui_pane_t *p = &g_tui.panes[g_tui.focused];
            p->frozen = !p->frozen;
            if (!p->frozen) p->scroll_offset = 0;
            redraw_focused();
            return true;
        }
        /* Other Ctrl+A combos (X, H, A...): let interactive.c handle */
        return false;
    }

    /* ── ESC sequence state machine ─────────────────────────── */
    if (s_esc == 1) {
        s_esc = (c == '[') ? 2 : 0;
        return true;
    }
    if (s_esc == 2) {
        s_esc = 0;
        tui_pane_t *p = &g_tui.panes[g_tui.focused];
        int pg = (p->h > 4) ? p->h - 3 : 1;
        if      (c == 'A' && p->frozen) { scroll_pane(-1); redraw_focused(); }
        else if (c == 'B' && p->frozen) { scroll_pane(+1); redraw_focused(); }
        else if (c == '5')              { s_esc = 3; return true; }
        else if (c == '6')              { s_esc = 4; return true; }
        (void)pg;
        return true;
    }
    if (s_esc == 3) {   /* ESC [ 5 ~ → PgUp */
        s_esc = 0;
        if (c == '~') {
            tui_pane_t *p = &g_tui.panes[g_tui.focused];
            int pg = (p->h > 4) ? p->h - 3 : 1;
            if (p->frozen) { scroll_pane(-pg); redraw_focused(); }
        }
        return true;
    }
    if (s_esc == 4) {   /* ESC [ 6 ~ → PgDn */
        s_esc = 0;
        if (c == '~') {
            tui_pane_t *p = &g_tui.panes[g_tui.focused];
            int pg = (p->h > 4) ? p->h - 3 : 1;
            if (p->frozen) { scroll_pane(+pg); redraw_focused(); }
        }
        return true;
    }

    /* ── Raw ESC → start sequence ───────────────────────────── */
    if (c == 0x1b) {
        s_esc = 1;
        return true;
    }

    /* ── Tab: cycle focused pane ────────────────────────────── */
    if (c == '\t') {
        g_tui.focused = (g_tui.focused + 1) % g_tui.npanes;
        redraw_all_borders();
        return true;
    }

    /* ── Vim-style scroll in frozen pane ────────────────────── */
    tui_pane_t *fp = &g_tui.panes[g_tui.focused];
    if (fp->frozen) {
        if (c == 'k') { scroll_pane(-1); redraw_focused(); return true; }
        if (c == 'j') { scroll_pane(+1); redraw_focused(); return true; }
        if (c == 'g') {
            /* Jump to top of buffer */
            int content_h = fp->h - 2;
            int max_off = fp->count > content_h ? fp->count - content_h : 0;
            fp->scroll_offset = max_off;
            redraw_focused();
            return true;
        }
        if (c == 'G') {
            /* Jump to tail, unfreeze */
            fp->scroll_offset = 0;
            fp->frozen = false;
            redraw_focused();
            return true;
        }
    }

    return false;
}
