#include "monitor.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ── Per-device thread ─────────────────────────────────────────────────── */

static void *device_thread(void *arg)
{
    monitor_device_t *dev = (monitor_device_t *)arg;
    monitor_t        *m   = (monitor_t *)((char *)arg -
                              offsetof(monitor_t, devices[0]));

    /*
     * Find our own monitor_t pointer: devices[i] is embedded in monitor_t,
     * so we walk back from the device pointer to the monitor struct.
     */
    monitor_t *mon = NULL;
    for (int i = 0; i < MONITOR_MAX_DEVICES; i++) {
        if (&m->devices[i] == dev) { mon = m; break; }
    }
    if (!mon) return NULL;

    uint8_t raw[4096];
    char    linebuf[1024];
    int     linelen = 0;

    while (dev->running && mon->running) {

        if (!serial_is_open(&dev->port)) {
            if (dev->port.auto_reconnect) {
                serial_reopen(&dev->port);
                struct timespec ts = {0, 500000000L}; nanosleep(&ts, NULL);
            } else {
                break;
            }
            continue;
        }

        int n = serial_read(&dev->port, raw, sizeof(raw) - 1);
        if (n <= 0) continue;

        /* Split into lines and process each */
        for (int i = 0; i < n; i++) {
            char c = (char)raw[i];

            if (c == '\n' || linelen >= (int)sizeof(linebuf) - 2) {
                linebuf[linelen] = '\0';

                if (linelen > 0) {
                    /* Record every line */
                    recorder_write(&dev->recorder, linebuf);

                    /* Run detector */
                    det_event_t ev = {0};
                    bool hit = detector_check(&dev->detector,
                                              linebuf,
                                              dev->port.name, &ev);

                    pthread_mutex_lock(&mon->print_lock);
                    if (hit) {
                        dev->crash_count++;
                        display_event(&ev, dev->color);
                        recorder_save_event(&dev->recorder, &ev);
                        if (mon->cfg->auto_reset)
                            resetter_maybe_reset(&dev->resetter, &ev);
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
    return NULL;
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int monitor_init(monitor_t *m, config_t *cfg)
{
    if (!m || !cfg) return -1;
    memset(m, 0, sizeof(*m));
    m->cfg = cfg;
    pthread_mutex_init(&m->print_lock, NULL);
    m->start_ms = now_ms();

    recorder_cfg_t rcfg = {
        .context_lines = RECORDER_CONTEXT_LINES,
        .max_bytes     = RECORDER_MAX_LOGSIZE,
    };
    strncpy(rcfg.logdir, cfg->logdir, sizeof(rcfg.logdir) - 1);

    display_cfg_t dcfg = {
        .verbose    = cfg->verbose,
        .timestamps = cfg->timestamps,
        .color      = cfg->color,
    };
    display_init(&dcfg);

    for (int i = 0; i < cfg->nports && i < MONITOR_MAX_DEVICES; i++) {
        monitor_device_t *dev = &m->devices[m->ndevices];

        /* Serial port */
        memset(&dev->port, 0, sizeof(dev->port));
        strncpy(dev->port.path, cfg->ports[i], sizeof(dev->port.path) - 1);
        strncpy(dev->port.name, cfg->names[i], sizeof(dev->port.name) - 1);
        dev->port.baud           = cfg->baud;
        dev->port.auto_reconnect = true;

        /* Detector: load built-in then extra files */
        detector_init(&dev->detector);
        detector_load_builtin(&dev->detector, cfg->builtin_family);
        for (int j = 0; j < cfg->npattern_files; j++)
            detector_load_file(&dev->detector, cfg->pattern_files[j]);

        /* Recorder */
        recorder_init(&dev->recorder, dev->port.name, &rcfg);

        /* Resetter */
        resetter_init(&dev->resetter, &dev->port,
                      cfg->reset_threshold, cfg->reset_cooldown_ms);

        dev->color   = display_device_color(i);
        dev->running = true;

        /* Open port */
        if (serial_open(&dev->port) != 0) {
            fprintf(stderr, "[monitor] failed to open %s\n", dev->port.path);
        }

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
    m->running = true;

    display_banner(m->ndevices);

    /* Start one thread per device */
    for (int i = 0; i < m->ndevices; i++) {
        monitor_device_t *dev = &m->devices[i];
        pthread_create(&dev->thread, NULL, device_thread, dev);
        printf("%s[%s]%s online @ %d baud\n",
               dev->color, dev->port.name, "\033[0m", dev->port.baud);
    }
    printf("\n");

    /* Main thread waits — actual I/O is in device threads */
    for (int i = 0; i < m->ndevices; i++)
        pthread_join(m->devices[i].thread, NULL);

    return 0;
}

void monitor_stop(monitor_t *m)
{
    if (!m) return;
    m->running = false;
    for (int i = 0; i < m->ndevices; i++)
        m->devices[i].running = false;
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
