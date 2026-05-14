#ifndef ESPILON_CONFIG_H
#define ESPILON_CONFIG_H

#include <stdbool.h>
#include "detector.h"

#define CONFIG_MAX_PORTS    32
#define CONFIG_MAX_PATTERNS 16
#define CONFIG_MAX_EXIT_RULES 16

/* Exit rule: quit when named pattern fires, return exit_code */
typedef struct {
    char rule_name[64];
    int  exit_code;
} exit_rule_t;

typedef struct {
    /* Ports */
    char    ports[CONFIG_MAX_PORTS][64];
    char    names[CONFIG_MAX_PORTS][32];
    int     nports;
    int     baud;

    /* Patterns */
    char    pattern_files[CONFIG_MAX_PATTERNS][256];
    int     npattern_files;
    char    builtin_family[32];   /* "esp32", "stm32", etc. */
    bool    family_explicit;      /* true if --family was passed on CLI */

    /* Logging */
    char    logdir[256];
    bool    timestamps;
    bool    verbose;
    bool    color;
    int     context_lines;   /* pre-event lines saved in events.log (0=off) */

    /* Behavior */
    bool       auto_reset;
    severity_t reset_threshold;
    int        reset_cooldown_ms;

    /* Daemon mode */
    bool       background;   /* --bg: double-fork and run in background */

    /* Interactive mode */
    bool       interactive;
    char       input_port[64];   /* port that receives stdin; empty = first port */

    /* Exit on pattern */
    exit_rule_t exit_rules[CONFIG_MAX_EXIT_RULES];
    int         nexit_rules;
    int         timeout_sec;     /* 0 = no timeout; exit 124 on expiry */

    /* Machine-readable output */
    bool        json_events;     /* emit NDJSON to stdout for each event */

    /* TUI split-pane mode */
    bool        tui;             /* --tui: one pane per device */
} config_t;

void config_defaults(config_t *cfg);

/* Load from INI file */
int  config_load_file(config_t *cfg, const char *path);

/* Override from CLI argv */
int  config_parse_args(config_t *cfg, int argc, char *argv[]);

void config_dump(const config_t *cfg);

#endif
