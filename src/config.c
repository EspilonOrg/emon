#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

void config_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->baud             = 115200;
    cfg->timestamps       = true;
    cfg->color            = true;
    cfg->verbose          = false;
    cfg->auto_reset       = false;
    cfg->reset_threshold  = SEV_CRITICAL;
    cfg->reset_cooldown_ms= 2000;
    cfg->interactive      = false;
    cfg->input_port[0]    = '\0';
    strncpy(cfg->logdir, "logs", sizeof(cfg->logdir) - 1);
    strncpy(cfg->builtin_family, "esp32", sizeof(cfg->builtin_family) - 1);
}

int config_load_file(config_t *cfg, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[512], key[64], val[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '[' || *p == '\0') continue;

        if (sscanf(p, "%63[^=]=%255[^\n]", key, val) != 2) continue;

        /* Trim key/val */
        char *k = key, *v = val;
        while (isspace((unsigned char)*k)) k++;
        char *ke = k + strlen(k) - 1;
        while (ke > k && isspace((unsigned char)*ke)) *ke-- = '\0';
        while (isspace((unsigned char)*v)) v++;

        if      (strcmp(k, "baud")       == 0) cfg->baud = atoi(v);
        else if (strcmp(k, "logdir")     == 0) strncpy(cfg->logdir, v, sizeof(cfg->logdir)-1);
        else if (strcmp(k, "family")     == 0) strncpy(cfg->builtin_family, v, sizeof(cfg->builtin_family)-1);
        else if (strcmp(k, "verbose")    == 0) cfg->verbose    = (strcmp(v,"true")==0||strcmp(v,"1")==0);
        else if (strcmp(k, "color")      == 0) cfg->color      = !(strcmp(v,"false")==0||strcmp(v,"0")==0);
        else if (strcmp(k, "timestamps") == 0) cfg->timestamps = !(strcmp(v,"false")==0||strcmp(v,"0")==0);
        else if (strcmp(k, "auto_reset") == 0) cfg->auto_reset = (strcmp(v,"true")==0||strcmp(v,"1")==0);
    }
    fclose(f);
    return 0;
}

int config_parse_args(config_t *cfg, int argc, char *argv[])
{
    /*
     * Two passes: --name overrides must apply after all positional port
     * args are registered, since they can appear in any order on the CLI.
     */

    /* Pass 1 — everything except --name (its arg is skipped here) */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--baud") == 0 && i+1 < argc)
            cfg->baud = atoi(argv[++i]);
        else if (strcmp(argv[i], "--logdir") == 0 && i+1 < argc)
            strncpy(cfg->logdir, argv[++i], sizeof(cfg->logdir)-1);
        else if (strcmp(argv[i], "--family") == 0 && i+1 < argc)
            strncpy(cfg->builtin_family, argv[++i], sizeof(cfg->builtin_family)-1);
        else if (strcmp(argv[i], "--patterns") == 0 && i+1 < argc) {
            if (cfg->npattern_files < CONFIG_MAX_PATTERNS)
                strncpy(cfg->pattern_files[cfg->npattern_files++],
                        argv[++i], 255);
        }
        else if (strcmp(argv[i], "--auto-reset") == 0) cfg->auto_reset = true;
        else if (strcmp(argv[i], "--interactive") == 0 ||
                 strcmp(argv[i], "-i") == 0) {
            cfg->interactive = true;
            /* Optional next arg: port name for input target (not a flag) */
            if (i+1 < argc && argv[i+1][0] != '-') {
                /* Check if it looks like a port (ttyUSB0, /dev/...) not a file */
                const char *next = argv[i+1];
                if (strncmp(next, "tty", 3) == 0 ||
                    strncmp(next, "/dev/", 5) == 0) {
                    strncpy(cfg->input_port, next, sizeof(cfg->input_port) - 1);
                    i++;
                }
            }
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            cfg->verbose = true;
        else if (strcmp(argv[i], "--no-color") == 0) cfg->color = false;
        else if (strcmp(argv[i], "--no-timestamps") == 0) cfg->timestamps = false;
        else if (strcmp(argv[i], "--name") == 0 && i+1 < argc) {
            i++;  /* defer to pass 2 */
        }
        else if (argv[i][0] != '-' && cfg->nports < CONFIG_MAX_PORTS) {
            /* Port path */
            int j = cfg->nports++;
            if (strncmp(argv[i], "/dev/", 5) == 0)
                strncpy(cfg->ports[j], argv[i], 63);
            else
                snprintf(cfg->ports[j], 64, "/dev/%s", argv[i]);
            /* Default name = basename */
            const char *base = strrchr(cfg->ports[j], '/');
            strncpy(cfg->names[j], base ? base+1 : cfg->ports[j], 31);
        }
    }

    /* Pass 2 — apply --name overrides now that ports are all registered.
     * Accepts both bare basename ("ttyUSB0=foo") and full path
     * ("/dev/ttyUSB0=foo"). */
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--name") != 0) continue;

        char *arg = argv[i+1];
        char *eq  = strchr(arg, '=');
        if (!eq) continue;
        int  keylen = (int)(eq - arg);
        char port_full[64];
        snprintf(port_full, sizeof(port_full),
                 (strncmp(arg, "/dev/", 5) == 0) ? "%.*s" : "/dev/%.*s",
                 keylen, arg);

        for (int j = 0; j < cfg->nports; j++) {
            if (strcmp(cfg->ports[j], port_full) == 0) {
                strncpy(cfg->names[j], eq+1, 31);
                cfg->names[j][31] = '\0';
                break;
            }
        }
    }
    return 0;
}

void config_dump(const config_t *cfg)
{
    printf("Config:\n");
    printf("  baud=%d  logdir=%s  family=%s\n",
           cfg->baud, cfg->logdir, cfg->builtin_family);
    printf("  verbose=%d  color=%d  auto_reset=%d\n",
           cfg->verbose, cfg->color, cfg->auto_reset);
    for (int i = 0; i < cfg->nports; i++)
        printf("  port[%d]: %s (%s)\n", i, cfg->ports[i], cfg->names[i]);
}
