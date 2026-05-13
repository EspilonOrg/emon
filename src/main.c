#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "config.h"
#include "monitor.h"
#include "serial.h"
#include "interactive.h"

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
        "  --patterns <file>     Load extra .pat pattern file\n"
        "  --logdir <dir>        Log directory (default: logs/)\n"
        "  --name <port>=<name>  Friendly name for a port\n"
        "  --auto-reset          Reset device on CRITICAL event\n"
        "  -v, --verbose         Print all lines, not just events\n"
        "  --no-color            Disable ANSI colors\n"
        "  --no-timestamps       Disable timestamps\n"
        "  --config <file>       Load config file\n"
        "  --list                List available serial ports\n"
        "  --version             Show version\n"
        "\n"
        "Examples:\n"
        "  %s ttyACM0 ttyUSB0 ttyUSB1\n"
        "  %s --family esp32 --auto-reset ttyACM0\n"
        "  %s --baud 9600 --name ttyUSB0=STM32 ttyUSB0\n",
        prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    /* Quick flags before full parse */
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

    if (monitor_init(&g_monitor, &cfg) != 0) {
        fprintf(stderr, "monitor_init failed\n");
        return 1;
    }

    if (cfg.interactive && interactive_init() != 0)
        cfg.interactive = false;   /* fallback: observe-only if tty unavailable */

    monitor_run(&g_monitor);   /* blocks */

    monitor_print_summary(&g_monitor);
    monitor_free(&g_monitor);
    return 0;
}
