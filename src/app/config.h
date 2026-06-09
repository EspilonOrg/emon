#ifndef ESPILON_CONFIG_H
#define ESPILON_CONFIG_H

#include <stdbool.h>
#include "monitor/detector.h"

#define CONFIG_MAX_PORTS      32
#define CONFIG_MAX_PATTERNS   16
#define CONFIG_MAX_EXIT_RULES 16

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

    /* Patterns - loaded via -p / --patterns */
    char    pattern_files[CONFIG_MAX_PATTERNS][256];
    int     npattern_files;

    /* Auto-patterns: detect device → load <dir>/<family>.pat */
    char    auto_patterns_dir[256];

    /* Logging */
    char    logdir[256];
    bool    timestamps;
    bool    verbose;
    bool    color;
    int     context_lines;

    /* Behavior */
    bool       auto_reset;
    severity_t reset_threshold;
    int        reset_cooldown_ms;

    /* Daemon mode */
    bool       background;

    /* Interactive mode */
    bool       interactive;
    char       input_port[64];

    /* Exit on pattern */
    exit_rule_t exit_rules[CONFIG_MAX_EXIT_RULES];
    int         nexit_rules;
    int         timeout_sec;

    /* Machine-readable output */
    bool        json_events;

    /* TUI split-pane mode */
    bool        tui;
} config_t;

void config_defaults(config_t *cfg);
int  config_load_file(config_t *cfg, const char *path);
int  config_parse_args(config_t *cfg, int argc, char *argv[]);
void config_dump(const config_t *cfg);

#endif
