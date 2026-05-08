#ifndef ESPILON_CONFIG_H
#define ESPILON_CONFIG_H

#include <stdbool.h>
#include "detector.h"

#define CONFIG_MAX_PORTS    32
#define CONFIG_MAX_PATTERNS 16

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

    /* Logging */
    char    logdir[256];
    bool    timestamps;
    bool    verbose;
    bool    color;

    /* Behavior */
    bool       auto_reset;
    severity_t reset_threshold;
    int        reset_cooldown_ms;
} config_t;

void config_defaults(config_t *cfg);

/* Load from INI file */
int  config_load_file(config_t *cfg, const char *path);

/* Override from CLI argv */
int  config_parse_args(config_t *cfg, int argc, char *argv[]);

void config_dump(const config_t *cfg);

#endif
