#include "app/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

void config_defaults(config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->baud              = 115200;
    cfg->timestamps        = true;
    cfg->color             = true;
    cfg->verbose           = true;
    cfg->auto_reset        = false;
    cfg->reset_threshold   = SEV_CRITICAL;
    cfg->reset_cooldown_ms = 2000;
    cfg->interactive       = false;
    cfg->context_lines     = 10;
    cfg->logdir[0]         = '\0';   /* logging disabled by default - use --logdir to enable */
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

        char *k = key, *v = val;
        while (isspace((unsigned char)*k)) k++;
        char *ke = k + strlen(k) - 1;
        while (ke > k && isspace((unsigned char)*ke)) *ke-- = '\0';
        while (isspace((unsigned char)*v)) v++;

        if      (strcmp(k, "baud")          == 0) cfg->baud = atoi(v);
        else if (strcmp(k, "logdir")        == 0) snprintf(cfg->logdir, sizeof(cfg->logdir), "%s", v);
        else if (strcmp(k, "auto_patterns") == 0) snprintf(cfg->auto_patterns_dir, sizeof(cfg->auto_patterns_dir), "%s", v);
        else if (strcmp(k, "verbose")       == 0) cfg->verbose    = (strcmp(v,"true")==0||strcmp(v,"1")==0);
        else if (strcmp(k, "color")         == 0) cfg->color      = !(strcmp(v,"false")==0||strcmp(v,"0")==0);
        else if (strcmp(k, "timestamps")    == 0) cfg->timestamps = !(strcmp(v,"false")==0||strcmp(v,"0")==0);
        else if (strcmp(k, "auto_reset")    == 0) cfg->auto_reset = (strcmp(v,"true")==0||strcmp(v,"1")==0);
    }
    fclose(f);
    return 0;
}

int config_parse_args(config_t *cfg, int argc, char *argv[])
{
    /* Pass 1 - everything except --name */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--baud") == 0 && i+1 < argc)
            cfg->baud = atoi(argv[++i]);
        else if (strcmp(argv[i], "--logdir") == 0 && i+1 < argc)
            snprintf(cfg->logdir, sizeof(cfg->logdir), "%s", argv[++i]);
        else if ((strcmp(argv[i], "--patterns") == 0 ||
                  strcmp(argv[i], "-p") == 0) && i+1 < argc) {
            if (cfg->npattern_files < CONFIG_MAX_PATTERNS)
                snprintf(cfg->pattern_files[cfg->npattern_files++],
                         256, "%s", argv[++i]);
        }
        else if (strcmp(argv[i], "--auto-patterns") == 0 && i+1 < argc)
            snprintf(cfg->auto_patterns_dir, sizeof(cfg->auto_patterns_dir),
                     "%s", argv[++i]);
        else if (strcmp(argv[i], "--auto-reset") == 0) cfg->auto_reset = true;
        else if (strcmp(argv[i], "--bg") == 0) cfg->background = true;
        else if (strcmp(argv[i], "--context") == 0 && i+1 < argc)
            cfg->context_lines = atoi(argv[++i]);
        else if (strcmp(argv[i], "--timeout") == 0 && i+1 < argc)
            cfg->timeout_sec = atoi(argv[++i]);
        else if (strcmp(argv[i], "--json-events") == 0)
            cfg->json_events = true;
        else if (strcmp(argv[i], "--tui") == 0)
            cfg->tui = true;
        else if ((strcmp(argv[i], "--exit-on") == 0 ||
                  strcmp(argv[i], "--wait-for") == 0) && i+1 < argc) {
            int is_wait = (strcmp(argv[i], "--wait-for") == 0);
            char *arg = argv[++i];
            if (cfg->nexit_rules < CONFIG_MAX_EXIT_RULES) {
                exit_rule_t *r = &cfg->exit_rules[cfg->nexit_rules++];
                char *eq = strchr(arg, '=');
                if (eq && !is_wait) {
                    int namelen = (int)(eq - arg);
                    if (namelen >= (int)sizeof(r->rule_name))
                        namelen = (int)sizeof(r->rule_name) - 1;
                    snprintf(r->rule_name, sizeof(r->rule_name), "%.*s", namelen, arg);
                    r->exit_code = atoi(eq + 1);
                } else {
                    snprintf(r->rule_name, sizeof(r->rule_name), "%s", arg);
                    r->exit_code = 0;
                }
            }
        }
        else if (strcmp(argv[i], "--interactive") == 0 ||
                 strcmp(argv[i], "-i") == 0) {
            cfg->interactive = true;
            if (i+1 < argc && argv[i+1][0] != '-') {
                const char *next = argv[i+1];
                if (strncmp(next, "tty", 3) == 0 ||
                    strncmp(next, "/dev/", 5) == 0) {
                    snprintf(cfg->input_port, sizeof(cfg->input_port), "%s", next);
                    i++;
                }
            }
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            cfg->verbose = true;
        else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0)
            cfg->verbose = false;
        else if (strcmp(argv[i], "--no-color") == 0) cfg->color = false;
        else if (strcmp(argv[i], "--no-timestamps") == 0) cfg->timestamps = false;
        else if (strcmp(argv[i], "--name") == 0 && i+1 < argc) {
            i++;
        }
        else if (argv[i][0] != '-' && cfg->nports < CONFIG_MAX_PORTS) {
            int j = cfg->nports++;
            if (strncmp(argv[i], "/dev/", 5) == 0)
                snprintf(cfg->ports[j], sizeof(cfg->ports[j]), "%s", argv[i]);
            else
                snprintf(cfg->ports[j], sizeof(cfg->ports[j]), "/dev/%s", argv[i]);
            const char *base = strrchr(cfg->ports[j], '/');
            snprintf(cfg->names[j], sizeof(cfg->names[j]), "%s",
                     base ? base + 1 : cfg->ports[j]);
        }
    }

    /* Pass 2 - --name overrides */
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
                snprintf(cfg->names[j], sizeof(cfg->names[j]), "%s", eq + 1);
                break;
            }
        }
    }
    return 0;
}

void config_dump(const config_t *cfg)
{
    printf("Config:\n");
    printf("  baud=%d  logdir=%s\n", cfg->baud, cfg->logdir);
    printf("  verbose=%d  color=%d  auto_reset=%d\n",
           cfg->verbose, cfg->color, cfg->auto_reset);
    if (cfg->auto_patterns_dir[0])
        printf("  auto-patterns: %s\n", cfg->auto_patterns_dir);
    for (int i = 0; i < cfg->npattern_files; i++)
        printf("  pattern[%d]: %s\n", i, cfg->pattern_files[i]);
    for (int i = 0; i < cfg->nports; i++)
        printf("  port[%d]: %s (%s)\n", i, cfg->ports[i], cfg->names[i]);
}
