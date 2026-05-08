#include "display.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"

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
};

static const char *sev_badge(severity_t s)
{
    switch (s) {
    case SEV_CRITICAL: return "\033[1;91m[CRITICAL]\033[0m";
    case SEV_HIGH:     return "\033[91m[HIGH    ]\033[0m";
    case SEV_WARN:     return "\033[33m[WARN    ]\033[0m";
    default:           return "\033[2m[INFO    ]\033[0m";
    }
}

static void print_ts(void)
{
    if (!g_cfg.timestamps) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm *t = localtime(&ts.tv_sec);
    printf("%s%02d:%02d:%02d.%03ld%s ",
           DIM,
           t->tm_hour, t->tm_min, t->tm_sec,
           ts.tv_nsec / 1000000,
           RESET);
}

void display_init(const display_cfg_t *cfg)
{
    if (cfg) g_cfg = *cfg;
}

void display_line(const char *device, const char *color, const char *line)
{
    if (!g_cfg.verbose) return;
    print_ts();
    printf("%s[%-10s]%s %s\n",
           g_cfg.color ? color  : "",
           device,
           g_cfg.color ? RESET  : "",
           line);
}

void display_event(const det_event_t *ev, const char *device_color)
{
    print_ts();
    printf("%s[%-10s]%s %s %s\n",
           g_cfg.color ? device_color : "",
           ev->device,
           g_cfg.color ? RESET        : "",
           g_cfg.color ? sev_badge(ev->severity) : severity_str(ev->severity),
           ev->line);

    if (ev->severity >= SEV_CRITICAL)
        printf("%s  ⚠  %s — %s%s\n",
               g_cfg.color ? "\033[1;91m" : "",
               ev->rule ? ev->rule->name : "?",
               ev->device,
               g_cfg.color ? RESET : "");
}

void display_stats(int ndevices, const char **names,
                   const int *crash_counts, uint64_t uptime_ms)
{
    uint64_t s  = uptime_ms / 1000;
    uint64_t h  = s / 3600; s %= 3600;
    uint64_t m  = s / 60;   s %= 60;
    int total   = 0;
    for (int i = 0; i < ndevices; i++) total += crash_counts[i];

    printf("\n%s── Stats ", DIM);
    printf("uptime %02lu:%02lu:%02lu  ", h, m, s);
    for (int i = 0; i < ndevices; i++)
        printf("%s:%d  ", names[i], crash_counts[i]);
    printf("total crashes:%d%s\n\n", total, RESET);
}

void display_banner(int nports)
{
    printf("%sespilon-monitor%s — %d port(s)\n",
           BOLD, RESET, nports);
    printf("%s%s%s\n\n",
           DIM,
           "─────────────────────────────────────────",
           RESET);
}

const char *display_device_color(int idx)
{
    return DEVICE_COLORS[idx % N_DEVICE_COLORS];
}
