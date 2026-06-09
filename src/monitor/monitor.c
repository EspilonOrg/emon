#include "monitor/monitor.h"
#include "ui/interactive.h"
#include "ui/display.h"
#include "ui/scrollback.h"
#include "ui/tui.h"
#include "utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/wait.h>

/* Thread argument - carries both device and parent monitor */
typedef struct {
    monitor_t        *mon;
    monitor_device_t *dev;
} thread_arg_t;

/* Global monitor pointer - used by interactive_stop_all() */
static monitor_t   *s_global_monitor = NULL;

static void *timeout_thread(void *arg);   /* forward decl */

/* Shared exit code - set by the first matching exit rule or timeout */
static atomic_int   s_exit_code = 0;

/* ── Plugin hooks ────────────────────────────────────────────────────────
 * For each --on-event script, fork a grandchild (double-fork so no zombie),
 * pipe the JSON event to its stdin, and exec python3.
 * The calling thread is not blocked; each fork+_exit is ~microseconds.
 * ──────────────────────────────────────────────────────────────────────── */
static void plugin_fire(const config_t *cfg, const det_event_t *ev)
{
    if (!cfg->non_event_scripts) return;

    /* Build JSON — escape backslash, double-quote, and newline in line field */
    char esc[768];
    int  ej = 0;
    for (const char *p = ev->line; *p && ej < (int)sizeof(esc) - 3; p++) {
        if (*p == '\\' || *p == '"') esc[ej++] = '\\';
        else if (*p == '\n')         { esc[ej++] = '\\'; esc[ej++] = 'n'; continue; }
        esc[ej++] = *p;
    }
    esc[ej] = '\0';

    char json[1024];
    snprintf(json, sizeof(json),
             "{\"rule\":\"%s\",\"severity\":\"%s\","
             "\"device\":\"%s\",\"line\":\"%s\",\"ts\":%llu}\n",
             ev->rule ? ev->rule->name : "UNKNOWN",
             severity_str(ev->severity),
             ev->device, esc,
             (unsigned long long)ev->timestamp);

    for (int i = 0; i < cfg->non_event_scripts; i++) {
        const char *script = cfg->on_event_scripts[i];

        pid_t child = fork();
        if (child < 0) continue;

        if (child == 0) {
            /* Child: fork again so grandchild is reparented to init */
            pid_t grand = fork();
            if (grand == 0) {
                /* Grandchild: pipe JSON to stdin and exec the script */
                int fds[2];
                if (pipe(fds) == 0) {
                    if (write(fds[1], json, strlen(json)) > 0) { /* written */ }
                    close(fds[1]);
                    if (dup2(fds[0], STDIN_FILENO) < 0) _exit(1);
                    close(fds[0]);
                }
                execlp("python3", "python3", script, NULL);
                _exit(1);
            }
            _exit(0);
        }
        /* Parent: reap child immediately — grandchild inherited by init */
        waitpid(child, NULL, 0);
    }
}

static void *device_thread(void *arg)
{
    thread_arg_t     *ta  = (thread_arg_t *)arg;
    monitor_t        *mon = ta->mon;
    monitor_device_t *dev = ta->dev;
    free(ta);

    uint8_t raw[4096];
    char    linebuf[1024];
    char    clean[1024];
    int     linelen = 0;

    /* State machines to respond to terminal queries from ESP-IDF linenoise:
     *   ESC[5n → ESC[0n  (capability probe  → "supported")
     *   ESC[6n → ESC[24;80R (cursor pos query → fake 80x24 terminal)
     * Without these, linenoise blocks waiting for responses and never prints
     * the prompt. */
    int probe_len   = 0;
    int cur_len     = 0;
    static const uint8_t PROBE_SEQ[4] = {0x1b, '[', '5', 'n'};
    static const uint8_t CURPOS_SEQ[4] = {0x1b, '[', '6', 'n'};

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

        /* Flush partial line on timeout (handles prompts like "striker:>" with no \n) */
        if (n <= 0) {
            if (linelen > 0) {
                linebuf[linelen] = '\0';
                strip_ansi(linebuf, clean, (int)sizeof(clean));
                recorder_write(&dev->recorder, clean);
                pthread_mutex_lock(&mon->print_lock);
                display_line(dev->dev_idx, dev->port.name, dev->color, linebuf);
                pthread_mutex_unlock(&mon->print_lock);
                linelen = 0;
            }
            continue;
        }

        for (int i = 0; i < n; i++) {
            char c = (char)raw[i];

            /* ESC[5n → ESC[0n : capability probe */
            if ((uint8_t)c == PROBE_SEQ[probe_len]) {
                if (++probe_len == 4) {
                    serial_write_str(&dev->port, "\033[0n");
                    probe_len = 0;
                }
            } else {
                probe_len = ((uint8_t)c == PROBE_SEQ[0]) ? 1 : 0;
            }

            /* ESC[6n → ESC[24;80R : cursor position query (for terminal width) */
            if ((uint8_t)c == CURPOS_SEQ[cur_len]) {
                if (++cur_len == 4) {
                    serial_write_str(&dev->port, "\033[24;80R");
                    cur_len = 0;
                }
            } else {
                cur_len = ((uint8_t)c == CURPOS_SEQ[0]) ? 1 : 0;
            }

            if (c == '\n' || linelen >= (int)sizeof(linebuf) - 2) {
                linebuf[linelen] = '\0';

                if (linelen > 0) {
                    strip_ansi(linebuf, clean, (int)sizeof(clean));
                    recorder_write(&dev->recorder, clean);

                    det_event_t ev = {0};
                    bool hit = detector_check(&dev->detector,
                                              clean,
                                              dev->port.name, &ev);

                    pthread_mutex_lock(&mon->print_lock);
                    if (hit) {
                        dev->crash_count++;
                        display_event(dev->dev_idx, &ev, dev->color);
                        if (mon->cfg->json_events)
                            display_event_json(&ev);
                        recorder_save_event(&dev->recorder, &ev);
                        plugin_fire(mon->cfg, &ev);
                        if (mon->cfg->auto_reset)
                            resetter_maybe_reset(&dev->resetter, &ev);
                        /* Check exit rules - first match wins */
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
                        display_line(dev->dev_idx, dev->port.name, dev->color, linebuf);
                    }
                    pthread_mutex_unlock(&mon->print_lock);
                }
                linelen = 0;
            } else if (c != '\r' && c != '\0') {
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

    /* Warn about flags that do nothing without patterns */
    bool has_patterns = cfg->npattern_files > 0 || cfg->auto_patterns_dir[0];
    if (!has_patterns) {
        if (cfg->nexit_rules > 0)
            fprintf(stderr, "warning: --wait-for / --exit-on set but no patterns loaded"
                            " - rules will never fire\n");
        if (cfg->auto_reset)
            fprintf(stderr, "warning: --auto-reset set but no patterns loaded"
                            " - no CRITICAL events will be generated\n");
    }

    recorder_cfg_t rcfg = {
        .context_lines = cfg->context_lines > 0 ? cfg->context_lines
                                                 : RECORDER_CONTEXT_LINES,
        .max_bytes     = RECORDER_MAX_LOGSIZE,
    };
    if (cfg->logdir[0])
        snprintf(rcfg.logdir, sizeof(rcfg.logdir), "%s", cfg->logdir);

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
        snprintf(dev->port.path, sizeof(dev->port.path), "%s", cfg->ports[i]);
        snprintf(dev->port.name, sizeof(dev->port.name), "%s", cfg->names[i]);
        dev->port.baud           = cfg->baud;
        dev->port.auto_reconnect = true;

        /* Flow control */
        if (strcmp(cfg->flow_control, "rtscts") == 0)
            dev->port.flow = SP_FLOWCONTROL_RTSCTS;
        else if (strcmp(cfg->flow_control, "xonxoff") == 0)
            dev->port.flow = SP_FLOWCONTROL_XONXOFF;
        else
            dev->port.flow = SP_FLOWCONTROL_NONE;

        if (cfg->logdir[0])
            recorder_init(&dev->recorder, dev->port.name, &rcfg);
        resetter_init(&dev->resetter, &dev->port,
                      cfg->reset_threshold, cfg->reset_cooldown_ms);

        dev->color   = display_device_color(i);
        dev->running = true;
        dev->dev_idx = m->ndevices;

        detector_init(&dev->detector);

        /* ── Auto-patterns: detect device → load <dir>/<family>.pat ──
         * 1. USB VID/PID (instant, port not yet open)
         * 2. Passive serial read 2s (fallback if USB gives nothing)
         * Skipped entirely if --auto-patterns not set.
         * ─────────────────────────────────────────────────────────── */
        if (cfg->auto_patterns_dir[0]) {
            char family[32] = {0};
            bool detected   = false;

            if (serial_detect_from_usb(dev->port.path, family,
                                       sizeof(family)) == 0) {
                detected = true;
            }

            if (serial_open(&dev->port) != 0) {
                fprintf(stderr, "[monitor] failed to open %s\n", dev->port.path);
                m->ndevices++;
                continue;
            }

            if (!detected) {
                serial_detect_passive(&dev->port, family, sizeof(family));
                if (strcmp(family, "unknown") == 0) family[0] = '\0';
            }

            if (family[0]) {
                char pat_path[512];
                snprintf(pat_path, sizeof(pat_path), "%s/%s.pat",
                         cfg->auto_patterns_dir, family);
                int n = detector_load_file(&dev->detector, pat_path);
                if (n > 0)
                    fprintf(stderr, "[%s] auto-patterns: %s → %s (%d rules)\n",
                            dev->port.name, family, pat_path, n);
                else
                    fprintf(stderr, "[%s] auto-patterns: %s detected but %s not found\n",
                            dev->port.name, family, pat_path);
                snprintf(dev->family, sizeof(dev->family), "%s", family);
            }
        } else {
            if (serial_open(&dev->port) != 0) {
                fprintf(stderr, "[monitor] failed to open %s\n", dev->port.path);
                m->ndevices++;
                continue;
            }
        }

        /* Load explicit -p / --patterns files */
        for (int j = 0; j < cfg->npattern_files; j++)
            detector_load_file(&dev->detector, cfg->pattern_files[j]);

        /* Warn about exit rules that don't match any loaded pattern */
        for (int r = 0; r < cfg->nexit_rules; r++) {
            bool found = false;
            for (int k = 0; k < dev->detector.nrules && !found; k++)
                if (strcmp(dev->detector.rules[k].name,
                           cfg->exit_rules[r].rule_name) == 0)
                    found = true;
            if (!found)
                fprintf(stderr,
                        "warning: [%s] --exit-on/--wait-for rule '%s' "
                        "not found in loaded patterns — it will never fire\n",
                        dev->port.name, cfg->exit_rules[r].rule_name);
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

static void sigwinch_handler(int sig)
{
    (void)sig;
    tui_signal_resize();
}

int monitor_run(monitor_t *m)
{
    if (!m) return -1;
    m->running     = true;
    s_global_monitor = m;

    scrollback_init();

    if (m->cfg->tui) {
        const char *names[MONITOR_MAX_DEVICES];
        const char *colors[MONITOR_MAX_DEVICES];
        serial_port_t *ports[MONITOR_MAX_DEVICES];
        for (int i = 0; i < m->ndevices; i++) {
            names[i]  = m->devices[i].port.name;
            colors[i] = m->devices[i].color;
            ports[i]  = &m->devices[i].port;
        }
        tui_init(m->ndevices, names, colors);
        tui_set_ports(ports, m->ndevices);
        signal(SIGWINCH, sigwinch_handler);
    } else {
        display_banner(m->ndevices);
    }

    for (int i = 0; i < m->ndevices; i++) {
        monitor_device_t *dev = &m->devices[i];

        /* Allocate thread arg on heap - freed by thread */
        thread_arg_t *ta = malloc(sizeof(*ta));
        if (!ta) continue;
        ta->mon = m;
        ta->dev = dev;

        pthread_create(&dev->thread, NULL, device_thread, ta);
        if (!m->cfg->tui) {
            if (dev->family[0])
                printf("%s[%s]%s online @ %d baud  %s[%s]%s\n",
                       dev->color, dev->port.name, "\033[0m", dev->port.baud,
                       "\033[2m", dev->family, "\033[0m");
            else
                printf("%s[%s]%s online @ %d baud\n",
                       dev->color, dev->port.name, "\033[0m", dev->port.baud);
        }
    }
    if (!m->cfg->tui) printf("\n");

    /* Timeout thread */
    pthread_t timeout_tid = 0;
    if (m->cfg->timeout_sec > 0) {
        pthread_create(&timeout_tid, NULL, timeout_thread,
                       &m->cfg->timeout_sec);
    }

    /* Interactive / TUI: start stdin thread.
     * TUI always needs it for keyboard navigation.
     * forward_to_device = true only when -i is also set. */
    if (m->cfg->interactive || m->cfg->tui) {
        monitor_device_t *target = NULL;
        if (m->cfg->input_port[0]) {
            for (int i = 0; i < m->ndevices; i++) {
                const char *p = m->devices[i].port.path;
                const char *n = m->devices[i].port.name;
                if (strcmp(p, m->cfg->input_port) == 0 ||
                    strcmp(n, m->cfg->input_port) == 0 ||
                    (strrchr(p, '/') &&
                     strcmp(strrchr(p, '/') + 1, m->cfg->input_port) == 0)) {
                    target = &m->devices[i];
                    break;
                }
            }
        }
        if (!target && m->ndevices > 0)
            target = &m->devices[0];

        if (target)
            interactive_start(&target->port, m->cfg->interactive);
    }

    for (int i = 0; i < m->ndevices; i++)
        pthread_join(m->devices[i].thread, NULL);

    if (m->cfg->interactive || m->cfg->tui)
        interactive_stop();

    if (m->cfg->tui)
        tui_destroy();

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

/* Timeout thread - fires after cfg->timeout_sec, exits with code 124 */
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
