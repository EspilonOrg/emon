#include "ui/scrollback.h"
#include "utils/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>

/* ── ANSI escape helpers ─────────────────────────────────────────────── */
#define ESC_ALT_ON    "\033[?1049h"   /* enter alternate screen          */
#define ESC_ALT_OFF   "\033[?1049l"   /* leave alternate screen          */
#define ESC_CLEAR     "\033[H\033[2J" /* clear + home                    */
#define ESC_CLREOL    "\033[K"        /* clear to end of line            */
#define ESC_HOME      "\033[H"
#define ESC_BOLD      "\033[1m"
#define ESC_REV       "\033[7m"       /* reverse video - for status bar  */
#define ESC_RESET     "\033[0m"
#define ESC_DIM       "\033[2m"
#define ESC_YELLOW    "\033[33m"

/* Position cursor at row r (1-based), column 1 */
#define ESC_GOTO(r)  printf("\033[%d;1H", (r))

/* ── Ring buffer ─────────────────────────────────────────────────────── */

static struct {
    char lines[SCROLLBACK_LINES][SCROLLBACK_WIDTH];
    int  head;    /* next write slot */
    int  count;   /* filled slots, up to SCROLLBACK_LINES */
} s_buf;

static pthread_mutex_t s_mutex    = PTHREAD_MUTEX_INITIALIZER;
static atomic_int      s_active   = 0;

void scrollback_init(void)
{
    memset(&s_buf, 0, sizeof(s_buf));
}

void scrollback_push(const char *line)
{
    if (!line) return;
    pthread_mutex_lock(&s_mutex);
    int slot = s_buf.head % SCROLLBACK_LINES;
    strncpy(s_buf.lines[slot], line, SCROLLBACK_WIDTH - 1);
    s_buf.lines[slot][SCROLLBACK_WIDTH - 1] = '\0';
    s_buf.head = (s_buf.head + 1) % SCROLLBACK_LINES;
    if (s_buf.count < SCROLLBACK_LINES) s_buf.count++;
    pthread_mutex_unlock(&s_mutex);
}

int scrollback_is_active(void)
{
    return atomic_load(&s_active);
}

/* ── Terminal helpers ────────────────────────────────────────────────── */

static void term_size(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
}

/* ── Search ──────────────────────────────────────────────────────────── */

static char s_query[128] = {0};

/* Returns 1 if `query` is found in `line` (case-insensitive, ANSI stripped) */
static int line_matches(const char *line, const char *query)
{
    if (!query[0]) return 0;
    char plain[SCROLLBACK_WIDTH];
    strip_ansi(line, plain, (int)sizeof(plain));
    /* Case-insensitive search via strstr-like loop */
    size_t qlen = strlen(query);
    for (size_t i = 0; plain[i]; i++) {
        size_t j;
        for (j = 0; j < qlen; j++) {
            char pc = plain[i+j];
            char qc = query[j];
            if (pc >= 'A' && pc <= 'Z') pc += 32;
            if (qc >= 'A' && qc <= 'Z') qc += 32;
            if (pc != qc) break;
        }
        if (j == qlen) return 1;
    }
    return 0;
}

/* Find next match from `from_offset` forward (or backward if dir=-1).
 * Returns new offset or -1 if not found. */
static int search_next(int from, int dir, int total)
{
    if (!s_query[0] || total == 0) return -1;
    for (int i = 1; i <= total; i++) {
        int off = ((from + dir * i) % total + total) % total;
        int slot = (s_buf.head - total + off + SCROLLBACK_LINES * 2)
                   % SCROLLBACK_LINES;
        if (line_matches(s_buf.lines[slot], s_query))
            return off;
    }
    return -1;
}

/* ── Renderer ────────────────────────────────────────────────────────── */

static void render(int offset, int rows, int cols __attribute__((unused)), const char *status_extra)
{
    pthread_mutex_lock(&s_mutex);
    int total  = s_buf.count;
    int nlines = rows - 1;    /* one line reserved for status bar */

    /* Clamp offset so we never show past the end */
    if (offset + nlines > total) offset = total > nlines ? total - nlines : 0;
    if (offset < 0) offset = 0;

    printf(ESC_HOME);

    for (int r = 0; r < nlines; r++) {
        int line_idx = offset + r;
        if (line_idx < total) {
            int slot = (s_buf.head - total + line_idx + SCROLLBACK_LINES * 2)
                       % SCROLLBACK_LINES;
            const char *line = s_buf.lines[slot];
            /* Highlight search match */
            if (s_query[0] && line_matches(line, s_query))
                printf(ESC_YELLOW);
            printf("%s" ESC_RESET ESC_CLREOL "\r\n", line);
        } else {
            printf("~" ESC_CLREOL "\r\n");
        }
    }

    /* Status bar */
    int at  = total > 0 ? offset + nlines : 0;
    int pct = total > 0 ? (int)((long)at * 100 / total) : 100;
    if (at > total) at = total;

    printf(ESC_REV);
    if (s_query[0])
        printf(" SCROLLBACK  %d/%d (%d%%)  search: \"%s\"  "
               "n=next N=prev  q=quit" ESC_CLREOL,
               at, total, pct, s_query);
    else
        printf(" SCROLLBACK  %d/%d (%d%%)  "
               "↑↓ scroll  PgUp/PgDn  g/G top/bottom  / search  q quit"
               "%s" ESC_CLREOL,
               at, total, pct, status_extra ? status_extra : "");
    printf(ESC_RESET);
    fflush(stdout);

    pthread_mutex_unlock(&s_mutex);
}

/* ── Key reader ──────────────────────────────────────────────────────── */

/* Read one key sequence from stdin (already in raw mode).
 * Returns a simple token. */
#define KEY_UP     1
#define KEY_DOWN   2
#define KEY_PGUP   3
#define KEY_PGDN   4
#define KEY_HOME_K 5
#define KEY_END_K  6
#define KEY_QUIT   7
#define KEY_SEARCH 8   /* '/' */
#define KEY_NEXT   9   /* 'n' */
#define KEY_PREV   10  /* 'N' */
#define KEY_CTRL_A 11
#define KEY_ENTER  12
#define KEY_OTHER  99

static int read_key(void)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {0, 100000};
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0)
        return KEY_OTHER;

    uint8_t buf[8] = {0};
    int n = (int)read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) return KEY_QUIT;

    if (buf[0] == 'q' || buf[0] == 'Q') return KEY_QUIT;
    if (buf[0] == 'g') return KEY_HOME_K;
    if (buf[0] == 'G') return KEY_END_K;
    if (buf[0] == '/') return KEY_SEARCH;
    if (buf[0] == 'n') return KEY_NEXT;
    if (buf[0] == 'N') return KEY_PREV;
    if (buf[0] == 0x01) return KEY_CTRL_A;  /* Ctrl+A */
    if (buf[0] == '\r' || buf[0] == '\n') return KEY_ENTER;

    if (n >= 3 && buf[0] == '\033' && buf[1] == '[') {
        switch (buf[2]) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case '5': return KEY_PGUP;   /* ESC[5~ */
        case '6': return KEY_PGDN;   /* ESC[6~ */
        }
    }
    return KEY_OTHER;
}

/* ── Search input ────────────────────────────────────────────────────── */

static void read_search_query(int rows)
{
    /* Show prompt at bottom, collect input until Enter or Esc */
    ESC_GOTO(rows);
    printf(ESC_CLREOL ESC_BOLD "/ " ESC_RESET);
    fflush(stdout);

    memset(s_query, 0, sizeof(s_query));
    int qi = 0;

    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv = {5, 0};
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) break;

        uint8_t c;
        if (read(STDIN_FILENO, &c, 1) != 1) break;

        if (c == '\r' || c == '\n') break;
        if (c == '\033') { memset(s_query, 0, sizeof(s_query)); break; }
        if ((c == 0x7f || c == '\b') && qi > 0) {
            s_query[--qi] = '\0';
            printf("\b \b");
            fflush(stdout);
            continue;
        }
        if (c >= 0x20 && qi < (int)sizeof(s_query) - 1) {
            s_query[qi++] = (char)c;
            s_query[qi]   = '\0';
            printf("%c", c);
            fflush(stdout);
        }
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */

void scrollback_enter(void)
{
    if (atomic_exchange(&s_active, 1)) return;  /* already active */

    int rows, cols;
    term_size(&rows, &cols);

    /* Enter alternate screen */
    printf(ESC_ALT_ON ESC_CLEAR);
    fflush(stdout);

    pthread_mutex_lock(&s_mutex);
    int total = s_buf.count;
    pthread_mutex_unlock(&s_mutex);

    /* Start at the bottom (most recent lines) */
    int offset = total > rows - 1 ? total - (rows - 1) : 0;

    render(offset, rows, cols, NULL);

    while (1) {
        int key = read_key();

        /* Refresh terminal size on each key (handles SIGWINCH) */
        term_size(&rows, &cols);
        pthread_mutex_lock(&s_mutex);
        total = s_buf.count;
        pthread_mutex_unlock(&s_mutex);

        switch (key) {
        case KEY_QUIT:
        case KEY_CTRL_A:  /* Ctrl+A [ pressed again */
            goto done;

        case KEY_UP:
            if (offset > 0) offset--;
            break;

        case KEY_DOWN:
            offset++;
            break;

        case KEY_PGUP:
            offset -= (rows - 2);
            if (offset < 0) offset = 0;
            break;

        case KEY_PGDN:
            offset += (rows - 2);
            break;

        case KEY_HOME_K:
            offset = 0;
            break;

        case KEY_END_K:
            offset = total > rows - 1 ? total - (rows - 1) : 0;
            break;

        case KEY_SEARCH:
            read_search_query(rows);
            /* Jump to first match from current position */
            {
                int hit = search_next(offset, 1, total);
                if (hit >= 0) offset = hit;
            }
            break;

        case KEY_NEXT:
            {
                int hit = search_next(offset + 1, 1, total);
                if (hit >= 0) offset = hit;
            }
            break;

        case KEY_PREV:
            {
                int hit = search_next(offset - 1, -1, total);
                if (hit >= 0) offset = hit;
            }
            break;

        default:
            continue;
        }

        render(offset, rows, cols, NULL);
    }

done:
    printf(ESC_ALT_OFF);
    fflush(stdout);
    atomic_store(&s_active, 0);
}
