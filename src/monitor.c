#include "monitor.h"
#include "interactive.h"
#include "display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>

/* Thread argument — carries both device and parent monitor */
typedef struct {
    monitor_t        *mon;
    monitor_device_t *dev;
} thread_arg_t;

/* Global monitor pointer — used by interactive_stop_all() */
static monitor_t   *s_global_monitor = NULL;

static void *timeout_thread(void *arg);   /* forward decl */

/* Shared exit code — set by the first matching exit rule or timeout */
static atomic_int   s_exit_code = 0;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void *device_thread(void *arg)
{
    thread_arg_t     *ta  = (thread_arg_t *)arg;
    monitor_t        *mon = ta->mon;
    monitor_device_t *dev = ta->dev;
    free(ta);

    uint8_t raw[4096];
    char    linebuf[1024];
    int     linelen = 0;

    while (dev->running && mon->running) {

        if (!serial_is_open(&dev->port)) {
            if (dev->port.auto_reconnect) {
                serial_reopen(&dev->port);
                struct timespec ts = {0, 500000000L};
                nanosleep(&ts, NULL);
            } else {
                break;
            }
            continue;
        }

        int n = serial_read(&dev->port, raw, sizeof(raw) - 1);
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {
            char c = (char)raw[i];

            if (c == '\n' || linelen >= (int)sizeof(linebuf) - 2) {
                linebuf[linelen] = '\0';

                if (linelen > 0) {
                    recorder_write(&dev->recorder, linebuf);

                    det_event_t ev = {0};
                    bool hit = detector_check(&dev->detector,
                                              linebuf,
                                              dev->port.name, &ev);

                    pthread_mutex_lock(&mon->print_lock);
                    if (hit) {
                        dev->crash_count++;
                        display_event(&ev, dev->color);
                        if (mon->cfg->json_events)
                            display_event_json(&ev);
                        recorder_save_event(&dev->recorder, &ev);
                        if (mon->cfg->auto_reset)
                            resetter_maybe_reset(&dev->resetter, &ev);
                        /* Check exit rules — first match wins */
                        for (int r = 0; r < mon->cfg->nexit_rules; r++) {
                            if (strcmp(ev.rule->name,
                                       mon->cfg->exit_rules[r].rule_name) == 0) {
                                atomic_store(&s_exit_code,
                                             mon->cfg->exit_rules[r].exit_code);
                                pthread_mutex_unlock(&mon->print_lock);
                                monitor_stop_all();
                                goto thread_done;
                            }
                        }
                    } else {
                        display_line(dev->port.name, dev->color, linebuf);
                    }
                    pthread_mutex_unlock(&mon->print_lock);
                }
                linelen = 0;
            } else if (c != '\r') {
                linebuf[linelen++] = c;
            }
        }
    }
thread_done:
    return NULL;
}

int monitor_init(monitor_t *m, config_t *cfg)
{
    if (!m || !cfg) return -1;
    memset(m, 0, sizeof(*m));
    m->cfg = cfg;
    pthread_mutex_init(&m->print_lock, NULL);
    m->start_ms = now_ms();

    recorder_cfg_t rcfg = {
        .context_lines = cfg->context_lines > 0 ? cfg->context_lines
                                                 : RECORDER_CONTEXT_LINES,
        .max_bytes     = RECORDER_MAX_LOGSIZE,
    };
    strncpy(rcfg.logdir, cfg->logdir, sizeof(rcfg.logdir) - 1);

    /* Compute name column width = longest device name (min 1, max 24) */
    int name_w = 0;
    for (int i = 0; i < cfg->nports; i++) {
        int l = (int)strlen(cfg->names[i]);
        if (l > name_w) name_w = l;
    }
    if (name_w > 24) name_w = 24;

    display_cfg_t dcfg = {
        .verbose    = cfg->verbose,
        .timestamps = cfg->timestamps,
        .color      = cfg->color,
        .name_width = name_w,
    };
    display_init(&dcfg);

    for (int i = 0; i < cfg->nports && i < MONITOR_MAX_DEVICES; i++) {
        monitor_device_t *dev = &m->devices[m->ndevices];

        memset(dev, 0, sizeof(*dev));
        strncpy(dev->port.path, cfg->ports[i], sizeof(dev->port.path) - 1);
        strncpy(dev->port.name, cfg->names[i], sizeof(dev->port.name) - 1);
        dev->port.baud           = cfg->baud;
        dev->port.auto_reconnect = true;

        recorder_init(&dev->recorder, dev->port.name, &rcfg);
        resetter_init(&dev->resetter, &dev->port,
                      cfg->reset_threshold, cfg->reset_cooldown_ms);

        dev->color   = display_device_color(i);
        dev->running = true;

        /* ── Auto-detect family (priority chain) ──────────────────────
         * 1. --family explicit → skip all detection
         * 2. USB VID/PID + description (instant, port not yet open)
         * 3. Passive serial read 2s (can refine esp32 → espilon)
         * 4. Fallback: use cfg->builtin_family (default "esp32")
         * ────────────────────────────────────────────────────────── */
        char detected[32];
        const char *family = cfg->builtin_family;

        if (!cfg->family_explicit) {
            /* Step 2: USB metadata — query before opening the port */
            if (serial_detect_from_usb(dev->port.path, detected,
                                       sizeof(detected)) == 0) {
                family = detected;
                fprintf(stderr, "[%s] auto-detected (USB): %s\n",
                        dev->port.name, family);
            }
        }

        if (serial_open(&dev->port) != 0) {
            fprintf(stderr, "[monitor] failed to open %s\n", dev->port.path);
            m->ndevices++;
            continue;
        }

        if (!cfg->family_explicit) {
            /* Step 3: passive read — may refine family (e.g. esp32 → espilon) */
            char refined[32];
            if (serial_detect_passive(&dev->port, refined, sizeof(refined)) == 0
                && strcmp(refined, "unknown") != 0) {
                if (strcmp(refined, family) != 0) {
                    fprintf(stderr, "[%s] auto-detected (live): %s\n",
                            dev->port.name, refined);
                }
                family = refined;
            }
        }

        detector_init(&dev->detector);
        detector_load_builtin(&dev->detector, family);
        for (int j = 0; j < cfg->npattern_files; j++)
            detector_load_file(&dev->detector, cfg->pattern_files[j]);

        m->ndevices++;
    }
    return 0;
}

void monitor_free(monitor_t *m)
{
    if (!m) return;
    for (int i = 0; i < m->ndevices; i++) {
        monitor_device_t *dev = &m->devices[i];
        dev->running = false;
        pthread_join(dev->thread, NULL);
        serial_close(&dev->port);
        recorder_free(&dev->recorder);
        detector_free(&dev->detector);
    }
    pthread_mutex_destroy(&m->print_lock);
}

int monitor_run(monitor_t *m)
{
    if (!m) return -1;
    m->running     = true;
    s_global_monitor = m;

    display_banner(m->ndevices);

    for (int i = 0; i < m->ndevices; i++) {
        monitor_device_t *dev = &m->devices[i];

        /* Allocate thread arg on heap — freed by thread */
        thread_arg_t *ta = malloc(sizeof(*ta));
        if (!ta) continue;
        ta->mon = m;
        ta->dev = dev;

        pthread_create(&dev->thread, NULL, device_thread, ta);
        printf("%s[%s]%s online @ %d baud\n",
               dev->color, dev->port.name, "\033[0m", dev->port.baud);
    }
    printf("\n");

    /* Timeout thread */
    pthread_t timeout_tid = 0;
    if (m->cfg->timeout_sec > 0) {
        pthread_create(&timeout_tid, NULL, timeout_thread,
                       &m->cfg->timeout_sec);
    }

    /* Interactive mode — start stdin thread targeting the right port */
    if (m->cfg->interactive) {
        monitor_device_t *target = NULL;
        if (m->cfg->input_port[0]) {
            /* Find the named/pathed port */
            for (int i = 0; i < m->ndevices; i++) {
                const char *p = m->devices[i].port.path;
                const char *n = m->devices[i].port.name;
                if (strcmp(p, m->cfg->input_port) == 0 ||
                    strcmp(n, m->cfg->input_port) == 0 ||
                    /* bare name like "ttyUSB0" */
                    (strrchr(p, '/') &&
                     strcmp(strrchr(p, '/') + 1, m->cfg->input_port) == 0)) {
                    target = &m->devices[i];
                    break;
                }
            }
        }
        if (!target && m->ndevices > 0)
            target = &m->devices[0];   /* default: first port */

        if (target)
            interactive_start(&target->port);
    }

    for (int i = 0; i < m->ndevices; i++)
        pthread_join(m->devices[i].thread, NULL);

    if (m->cfg->interactive)
        interactive_stop();

    if (timeout_tid) {
        /* Cancel the timeout thread if we exited before it fired */
        pthread_cancel(timeout_tid);
        pthread_join(timeout_tid, NULL);
    }

    return 0;
}

void monitor_stop(monitor_t *m)
{
    if (!m) return;
    m->running = false;
    for (int i = 0; i < m->ndevices; i++)
        m->devices[i].running = false;
}

void monitor_stop_all(void)
{
    if (s_global_monitor)
        monitor_stop(s_global_monitor);
}

int monitor_get_exit_code(void)
{
    return atomic_load(&s_exit_code);
}

/* Timeout thread — fires after cfg->timeout_sec, exits with code 124 */
static void *timeout_thread(void *arg)
{
    int secs = *(int *)arg;
    struct timespec ts = { (time_t)secs, 0 };
    nanosleep(&ts, NULL);
    if (s_global_monitor && s_global_monitor->running) {
        atomic_store(&s_exit_code, 124);
        monitor_stop_all();
    }
    return NULL;
}

void monitor_print_summary(const monitor_t *m)
{
    if (!m) return;
    uint64_t uptime = now_ms() - m->start_ms;
    const char *names[MONITOR_MAX_DEVICES];
    int crashes[MONITOR_MAX_DEVICES];
    for (int i = 0; i < m->ndevices; i++) {
        names[i]   = m->devices[i].port.name;
        crashes[i] = m->devices[i].crash_count;
    }
    display_stats(m->ndevices, names, crashes, uptime);
}
