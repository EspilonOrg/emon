#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "config.h"
#include "monitor.h"
#include "serial.h"
#include "interactive.h"
#include "display.h"
#include "daemon.h"

#define VERSION "0.1.0"

static monitor_t g_monitor;

static void on_signal(int sig)
{
    (void)sig;
    monitor_stop(&g_monitor);
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] <port> [port2 ...]\n"
        "\n"
        "Options:\n"
        "  --baud <N>            Baud rate (default: 115200)\n"
        "  --family <name>       Built-in pattern family: espilon esp32 stm32 arduino freertos zephyr\n"
        "  --interactive, -i    Bidirectional mode: stdin → device, device → stdout\n"
        "                       Optional: -i <port>  to select target port when multi-port\n"
        "                       Escape key Ctrl+A:  X=quit  A=send Ctrl+A  H=help\n"
        "  --exit-on <RULE>[=N] Exit with code N when named pattern fires (default N=0)\n"
        "  --wait-for <RULE>    Exit 0 when pattern fires, 124 on timeout\n"
        "  --timeout <secs>     Exit 124 after N seconds (use with --exit-on / --wait-for)\n"
        "  --json-events        Emit NDJSON to stdout for each detected event\n"
        "  --patterns <file>     Load extra .pat pattern file\n"
        "  --logdir <dir>        Log directory (default: logs/)\n"
        "  --name <port>=<name>  Friendly name for a port\n"
        "  --auto-reset          Reset device on CRITICAL event\n"
        "  --context <N>         Pre-event context lines in events.log (default: 10, 0=off)\n"
        "  --bg                  Run as background daemon (PID file in logdir/)\n"
        "  -v, --verbose         Print all lines, not just events\n"
        "  --no-color            Disable ANSI colors\n"
        "  --no-timestamps       Disable timestamps\n"
        "  --config <file>       Load config file\n"
        "  --list                List available serial ports\n"
        "  --version             Show version\n"
        "\n"
        "\n"
        "Sub-commands:\n"
        "  stop                  Stop a running background daemon\n"
        "  status                Show daemon status\n"
        "\n"
        "Examples:\n"
        "  espilon-monitor ttyACM0 ttyUSB0 ttyUSB1\n"
        "  espilon-monitor --family espilon --bg --logdir /opt/espilon-monitor/logs /dev/ttyUSB1\n"
        "  espilon-monitor --wait-for HANDSHAKE_OK --timeout 30 --json-events /dev/ttyUSB0\n"
        "  espilon-monitor stop\n",
        prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    /* Quick flags + sub-commands before full parse */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("espilon-monitor %s\n", VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--list") == 0) {
            char ports[SERIAL_MAX_PORTS][64];
            int n = serial_list(ports, SERIAL_MAX_PORTS);
            printf("Available serial ports (%d):\n", n);
            for (int j = 0; j < n; j++)
                printf("  %s\n", ports[j]);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]); return 0;
        }
        /* Sub-commands: stop / status */
        if (strcmp(argv[i], "stop") == 0) {
            config_t cfg; config_defaults(&cfg);
            config_parse_args(&cfg, argc, argv);
            return daemon_stop(cfg.logdir);
        }
        if (strcmp(argv[i], "status") == 0) {
            config_t cfg; config_defaults(&cfg);
            config_parse_args(&cfg, argc, argv);
            return daemon_status(cfg.logdir);
        }
    }

    config_t cfg;
    config_defaults(&cfg);

    /* Load config file if --config is present */
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (config_load_file(&cfg, argv[i+1]) != 0)
                fprintf(stderr, "Warning: could not load %s\n", argv[i+1]);
            break;
        }
    }

    /* CLI overrides */
    config_parse_args(&cfg, argc, argv);

    if (cfg.nports == 0) {
        fprintf(stderr, "Error: no ports specified.\n");
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* Daemon mode: double-fork. Parent returns 0 and exits. */
    if (cfg.background) {
        int r = daemonize(cfg.logdir);
        if (r == 0) {
            /* Parent process: print status and exit */
            printf("espilon-monitor: started in background (logs → %s/)\n",
                   cfg.logdir);
            return 0;
        }
        /* r == -2: daemon process, fall through to monitor_run() */
        /* r == -1: error — fall through anyway, run in foreground */
    }

    if (monitor_init(&g_monitor, &cfg) != 0) {
        fprintf(stderr, "monitor_init failed\n");
        return 1;
    }

    if (cfg.interactive && interactive_init() != 0)
        cfg.interactive = false;   /* fallback: observe-only if tty unavailable */

    monitor_run(&g_monitor);   /* blocks */

    monitor_print_summary(&g_monitor);
    int exit_code = monitor_get_exit_code();
    monitor_free(&g_monitor);
    return exit_code;
}
