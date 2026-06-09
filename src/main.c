#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "app/config.h"
#include "monitor/monitor.h"
#include "serial/serial.h"
#include "ui/interactive.h"
#include "ui/display.h"
#include "app/daemon.h"

#ifndef VERSION_STR
#define VERSION_STR "0.1.0"
#endif

static monitor_t g_monitor;

/* Signal handler: only sets a flag — monitor_stop() is a single bool store,
 * which is safe to call from a signal handler on all POSIX architectures. */
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
        "  --baud <N>              Baud rate (default: 115200)\n"
        "  --auto-patterns <dir>   Auto-load <dir>/<family>.pat on device detect\n"
        "  --patterns, -p <file>   Load extra .pat pattern file\n"
        "  --interactive, -i       Bidirectional mode: stdin → device, device → stdout\n"
        "                          Optional: -i <port>  to select target port when multi-port\n"
        "                          Escape key Ctrl+A:  X=quit  A=send Ctrl+A  H=help\n"
        "  --exit-on <RULE>[=N]    Exit with code N when named pattern fires (default N=0)\n"
        "  --wait-for <RULE>       Exit 0 when pattern fires, 124 on timeout\n"
        "  --timeout <secs>        Exit 124 after N seconds (use with --exit-on / --wait-for)\n"
        "  --json-events           Emit NDJSON to stdout for each detected event\n"
        "  --logdir <dir>          Log directory (logging disabled if not set)\n"
        "  --name <port>=<name>    Friendly name for a port\n"
        "  --auto-reset            Reset device on CRITICAL event\n"
        "  --context <N>           Pre-event context lines in events.log (default: 10, 0=off)\n"
        "  --tui                   Split-pane TUI: one pane per device (Tab=next  Ctrl+A[=scroll)\n"
        "  --bg                    Run as background daemon (PID file in logdir/)\n"
        "  -v, --verbose           Print all lines, not just events\n"
        "  -q, --quiet             Print only detected events (overrides --verbose)\n"
        "  --no-color              Disable ANSI colors\n"
        "  --no-timestamps         Disable timestamps\n"
        "  --config <file>         Load config file\n"
        "  --list                  List available serial ports\n"
        "  --version               Show version\n"
        "\n"
        "Sub-commands:\n"
        "  stop                    Stop a running background daemon\n"
        "  status                  Show daemon status\n"
        "\n"
        "Examples:\n"
        "  espilon-monitor ttyACM0 ttyUSB0 ttyUSB1\n"
        "  espilon-monitor --auto-patterns patterns/ --bg --logdir /opt/emon/logs /dev/ttyUSB1\n"
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
            printf("espilon-monitor %s\n", VERSION_STR);
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

    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            if (config_load_file(&cfg, argv[i+1]) != 0)
                fprintf(stderr, "Warning: could not load %s\n", argv[i+1]);
            break;
        }
    }

    config_parse_args(&cfg, argc, argv);

    if (cfg.nports == 0) {
        fprintf(stderr, "Error: no ports specified.\n");
        usage(argv[0]);
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = on_signal;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (cfg.background) {
        int r = daemonize(cfg.logdir);
        if (r == 0) {
            printf("espilon-monitor: started in background (logs → %s/)\n",
                   cfg.logdir);
            return 0;
        }
    }

    if (monitor_init(&g_monitor, &cfg) != 0) {
        fprintf(stderr, "monitor_init failed\n");
        return 1;
    }

    if ((cfg.interactive || cfg.tui) && interactive_init() != 0) {
        cfg.interactive = false;
        cfg.tui         = false;
    }

    monitor_run(&g_monitor);

    monitor_print_summary(&g_monitor);
    int exit_code = monitor_get_exit_code();
    monitor_free(&g_monitor);
    return exit_code;
}
