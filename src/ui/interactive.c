#define _DEFAULT_SOURCE
#include "ui/interactive.h"
#include "serial/serial.h"
#include "ui/scrollback.h"
#include "ui/tui.h"
#include <stdatomic.h>

#define RESET  "\033[0m"
#define BOLD   "\033[1m"
#define DIM    "\033[2m"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#define ESCAPE_KEY  0x01   /* Ctrl+A */

/* ── Terminal state ──────────────────────────────────────────── */

static struct termios s_orig;
static int            s_is_tty   = 0;
static int            s_restored = 0;

void interactive_restore(void)
{
    if (!s_is_tty || s_restored) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig);
    s_restored = 1;
}

void interactive_print_help(void)
{
    dprintf(STDERR_FILENO,
        "\r\n"
        BOLD "[espilon-monitor interactive]" RESET "\r\n"
        "  Ctrl+A  [   - enter scrollback (↑↓ PgUp/PgDn / search  q=exit)\r\n"
        "  Ctrl+A  X   - quit monitor\r\n"
        "  Ctrl+A  A   - send literal Ctrl+A to device\r\n"
        "  Ctrl+A  H   - this help\r\n"
        "\r\n");
}

int interactive_init(void)
{
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "[interactive] stdin is not a tty - interactive mode disabled\n");
        return -1;
    }

    if (tcgetattr(STDIN_FILENO, &s_orig) != 0) {
        perror("[interactive] tcgetattr");
        return -1;
    }

    struct termios raw = s_orig;
    cfmakeraw(&raw);
    /* Keep OPOST so \n → \r\n on output works correctly */
    raw.c_oflag |= OPOST;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        perror("[interactive] tcsetattr");
        return -1;
    }

    s_is_tty   = 1;
    s_restored = 0;
    atexit(interactive_restore);
    return 0;
}

/* ── Stdin thread ────────────────────────────────────────────── */

typedef struct {
    serial_port_t *port;
    _Atomic int    running;
    bool           forward;   /* true = forward non-TUI keys to device */
} stdin_arg_t;

static pthread_t  s_thread;
static stdin_arg_t s_arg;

extern void monitor_stop_all(void);   /* forward decl - implemented in monitor.c */

static void *stdin_thread(void *arg)
{
    stdin_arg_t *a = (stdin_arg_t *)arg;
    uint8_t      buf[64];
    int          escape_pending = 0;

    /* Print a small banner so user knows they're in interactive mode */
    dprintf(STDERR_FILENO,
        "\r" DIM "-- interactive mode (Ctrl+A H for help) --" RESET "\r\n");

    while (a->running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        struct timeval tv = {0, 100000};   /* 100 ms select timeout */
        int r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (r <= 0) continue;

        int n = (int)read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) break;

        for (int i = 0; i < n && a->running; i++) {
            uint8_t c = buf[i];

            /* ── TUI key delegation (Ctrl+A combos + ESC sequences) ── */
            if (tui_is_active()) {
                if (escape_pending) {
                    /* Pass Ctrl+A combo to TUI first */
                    if (tui_handle_key(c, true)) {
                        escape_pending = 0;
                        continue;
                    }
                } else {
                    if (tui_handle_key(c, false))
                        continue;
                }
            }

            if (escape_pending) {
                escape_pending = 0;
                switch (c) {
                case 'x': case 'X':
                    dprintf(STDERR_FILENO, "\r\n" DIM "[quit]" RESET "\r\n");
                    interactive_restore();
                    monitor_stop_all();
                    return NULL;

                case ESCAPE_KEY:
                    /* Ctrl+A Ctrl+A → send literal 0x01 to device */
                    if (a->forward && a->port)
                        serial_write(a->port, &c, 1);
                    break;

                case '[':
                    /* Ctrl+A [ → scrollback (non-TUI mode only) */
                    if (!tui_is_active())
                        scrollback_enter();
                    break;

                case 'h': case 'H': case '?':
                    interactive_print_help();
                    break;

                default:
                    break;
                }
                continue;
            }

            if (c == ESCAPE_KEY) {
                escape_pending = 1;
                continue;
            }

            /* Forward raw byte to device */
            if (a->forward && a->port)
                serial_write(a->port, &c, 1);
        }
    }

    return NULL;
}

int interactive_start(serial_port_t *port, bool forward)
{
    if (!s_is_tty) return -1;

    s_arg.port    = port;
    s_arg.running = 1;
    s_arg.forward = forward;

    if (pthread_create(&s_thread, NULL, stdin_thread, &s_arg) != 0) {
        perror("[interactive] pthread_create");
        return -1;
    }
    return 0;
}

void interactive_stop(void)
{
    if (!s_is_tty) return;
    s_arg.running = 0;
    pthread_join(s_thread, NULL);
    interactive_restore();
}
