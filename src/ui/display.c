#include "ui/display.h"
#include "ui/scrollback.h"
#include "ui/tui.h"
#include "utils/utils.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"

#define DISPLAY_BUF 4096

static const char *DEVICE_COLORS[] = {
    "\033[96m",  /* cyan    */
    "\033[92m",  /* green   */
    "\033[95m",  /* magenta */
    "\033[93m",  /* yellow  */
    "\033[94m",  /* blue    */
    "\033[91m",  /* red     */
};
#define N_DEVICE_COLORS 6

static display_cfg_t g_cfg = {
    .verbose    = false,
    .timestamps = true,
    .color      = true,
    .show_stats = false,
    .name_width = 0,
};

static const char *sev_badge(severity_t s)
{
    switch (s) {
    case SEV_CRITICAL: return "\033[1;91m[CRITICAL]\033[0m";
    case SEV_HIGH:     return "\033[91m[HIGH]\033[0m";
    case SEV_WARN:     return "\033[33m[WARN]\033[0m";
    default:           return "\033[2m[INFO]\033[0m";
    }
}
static void print_ts_to(FILE *f)
{
    if (!g_cfg.timestamps) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);
    fprintf(f, "%s%02d:%02d:%02d.%03ld%s ",
            DIM,
            t->tm_hour, t->tm_min, t->tm_sec,
            ts.tv_nsec / 1000000,
            RESET);
}
static void print_ts(void)      { print_ts_to(stdout); }

/* Resolve the line to display: strip device ANSI if --no-color */
static const char *resolve_line(const char *line, char *tmp, int tmplen)
{
    if (g_cfg.color) return line;
    strip_ansi(line, tmp, tmplen);
    return tmp;
}

void display_init(const display_cfg_t *cfg)
{
    if (cfg) g_cfg = *cfg;
}

void display_hex(int dev_idx, const char *device, const char *color,
                 uint64_t offset, const uint8_t *data, int len)
{
    /* Build hex column: "XX XX XX XX XX XX XX XX  XX XX XX XX XX XX XX XX" (47 chars) */
    char hex[64] = {0};
    char asc[17] = {0};
    int  hi = 0;

    for (int i = 0; i < 16; i++) {
        if (i == 8) hex[hi++] = ' ';   /* extra gap between two groups of 8 */
        if (i < len) {
            hi += snprintf(hex + hi, sizeof(hex) - hi, "%02X ", data[i]);
            asc[i] = isprint((unsigned char)data[i]) ? (char)data[i] : '.';
        } else {
            memcpy(hex + hi, "   ", 3); hi += 3;
            asc[i] = ' ';
        }
    }
    if (hi > 0 && hex[hi - 1] == ' ') hex[--hi] = '\0';

    char buf[DISPLAY_BUF];

    if (tui_is_active()) {
        snprintf(buf, sizeof(buf), "%04llX  %s  |%s|",
                 (unsigned long long)offset, hex, asc);
        tui_push_line(dev_idx, buf);
        return;
    }

    int w = g_cfg.name_width > 0 ? g_cfg.name_width : (int)strlen(device);
    snprintf(buf, sizeof(buf), "%s[%-*s]%s %04llX  %s  |%s|",
             g_cfg.color ? color : "", w, device,
             g_cfg.color ? RESET : "",
             (unsigned long long)offset, hex, asc);

    if (!scrollback_is_active()) {
        print_ts();
        printf("%s\n", buf);
    }
    scrollback_push(buf);
}

void display_line(int dev_idx, const char *device, const char *color, const char *line)
{
    /* In hex mode the raw bytes are already rendered by display_hex() — skip text */
    if (!g_cfg.verbose || g_cfg.hex_mode) return;

    /* Line content is bounded by linebuf[1024] in monitor.c - 1024 is sufficient */
    char plain[1024];
    const char *out = resolve_line(line, plain, (int)sizeof(plain));

    if (tui_is_active()) {
        tui_push_line(dev_idx, out);
        return;
    }

    int w = g_cfg.name_width > 0 ? g_cfg.name_width : (int)strlen(device);
    char buf[DISPLAY_BUF];
    snprintf(buf, sizeof(buf), "%s[%-*s]%s %s",
             g_cfg.color ? color : "", w, device,
             g_cfg.color ? RESET : "", out);

    if (!scrollback_is_active()) {
        print_ts();
        printf("%s\n", buf);
    }
    scrollback_push(buf);
}

void display_event(int dev_idx, const det_event_t *ev, const char *device_color)
{
    char buf[DISPLAY_BUF];

    if (tui_is_active()) {
        snprintf(buf, sizeof(buf), "%s %s",
                 g_cfg.color ? sev_badge(ev->severity) : severity_str(ev->severity),
                 ev->line);
        tui_push_event(dev_idx, ev->severity, buf);
        return;
    }

    int w = g_cfg.name_width > 0 ? g_cfg.name_width : (int)strlen(ev->device);
    snprintf(buf, sizeof(buf), "%s[%-*s]%s %s %s",
             g_cfg.color ? device_color : "",
             w, ev->device,
             g_cfg.color ? RESET        : "",
             g_cfg.color ? sev_badge(ev->severity) : severity_str(ev->severity),
             ev->line);
    if (!scrollback_is_active()) {
        FILE *out = g_cfg.json_events ? stderr : stdout;
        print_ts_to(out);
        fprintf(out, "%s\n", buf);
        if (ev->severity >= SEV_CRITICAL)
            fprintf(out, "%s  !!  %s -- %s%s\n",
                    g_cfg.color ? "\033[1;91m" : "",
                    ev->rule ? ev->rule->name : "?",
                    ev->device,
                    g_cfg.color ? RESET : "");
    }
    scrollback_push(buf);
}

void display_stats(int ndevices, const char **names,
                   const int *crash_counts, uint64_t uptime_ms)
{
    uint64_t s  = uptime_ms / 1000;
    uint64_t h  = s / 3600; s %= 3600;
    uint64_t m  = s / 60;   s %= 60;
    int total   = 0;
    for (int i = 0; i < ndevices; i++) total += crash_counts[i];

    FILE *sf = g_cfg.json_events ? stderr : stdout;
    fprintf(sf, "\n%s── Stats ", DIM);
    fprintf(sf, "uptime %02lu:%02lu:%02lu  ", h, m, s);
    for (int i = 0; i < ndevices; i++)
        fprintf(sf, "%s:%d  ", names[i], crash_counts[i]);
    fprintf(sf, "total crashes:%d%s\n\n", total, RESET);
}

void display_banner(int nports)
{
    fprintf(stderr, "%sespilon-monitor%s - %d port(s)\n",
            BOLD, RESET, nports);
    fprintf(stderr, "%s%s%s\n\n",
            DIM,
            "─────────────────────────────────────────",
            RESET);
}

const char *display_device_color(int idx)
{
    return DEVICE_COLORS[idx % N_DEVICE_COLORS];
}

void display_event_json(const det_event_t *ev)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);

    char escaped[sizeof(ev->line) * 2];
    int  ei = 0;
    for (int i = 0; ev->line[i] && ei < (int)sizeof(escaped) - 2; i++) {
        if (ev->line[i] == '"' || ev->line[i] == '\\')
            escaped[ei++] = '\\';
        escaped[ei++] = ev->line[i];
    }
    escaped[ei] = '\0';

    printf("{\"ts\":%llu,\"device\":\"%s\",\"rule\":\"%s\","
           "\"severity\":\"%s\",\"line\":\"%s\"}\n",
           (unsigned long long)ms,
           ev->device,
           ev->rule ? ev->rule->name : "?",
           severity_str(ev->severity),
           escaped);
    fflush(stdout);
}
