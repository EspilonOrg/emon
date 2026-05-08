#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "serial.h"

static volatile int running = 1;

static void on_signal(int sig) { (void)sig; running = 0; }

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <port> [port2 ...]\n"
        "\nOptions:\n"
        "  --baud <N>    Baud rate (default: 115200)\n"
        "  --list        List available serial ports\n"
        "  --version     Show version\n"
        "\nExamples:\n"
        "  %s ttyACM0 ttyUSB0 ttyUSB1\n"
        "  %s --baud 9600 ttyACM0\n",
        prog, prog, prog);
}

int main(int argc, char *argv[])
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (argc < 2) { usage(argv[0]); return 1; }

    if (strcmp(argv[1], "--list") == 0) {
        char ports[SERIAL_MAX_PORTS][64];
        int n = serial_list(ports, SERIAL_MAX_PORTS);
        printf("Available serial ports (%d):\n", n);
        for (int i = 0; i < n; i++) printf("  %s\n", ports[i]);
        return 0;
    }

    if (strcmp(argv[1], "--version") == 0) {
        printf("espilon-monitor 0.1.0\n"); return 0;
    }

    int baud = 115200;
    serial_port_t ports[SERIAL_MAX_PORTS];
    int nports = 0;

    for (int i = 1; i < argc && nports < SERIAL_MAX_PORTS; i++) {
        if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]); continue;
        }
        if (argv[i][0] == '-') continue;

        serial_port_t *p = &ports[nports++];
        memset(p, 0, sizeof(*p));
        if (strncmp(argv[i], "/dev/", 5) == 0)
            strncpy(p->path, argv[i], sizeof(p->path) - 1);
        else
            snprintf(p->path, sizeof(p->path), "/dev/%s", argv[i]);
        snprintf(p->name, sizeof(p->name), "%s", strrchr(p->path, '/') + 1);
        p->baud           = baud;
        p->auto_reconnect = true;
    }

    if (nports == 0) {
        fprintf(stderr, "Error: no ports specified\n");
        usage(argv[0]); return 1;
    }

    printf("espilon-monitor — opening %d port(s)\n\n", nports);
    for (int i = 0; i < nports; i++) {
        if (serial_open(&ports[i]) == 0)
            printf("  [%s] opened @ %d baud\n", ports[i].name, ports[i].baud);
        else
            fprintf(stderr, "  [%s] failed to open\n", ports[i].name);
    }
    printf("\n");

    /* Minimal read loop — replaced by monitor.c threading later */
    uint8_t buf[SERIAL_BUF_SIZE];
    while (running) {
        for (int i = 0; i < nports; i++) {
            if (!serial_is_open(&ports[i])) {
                if (ports[i].auto_reconnect) serial_reopen(&ports[i]);
                continue;
            }
            int n = serial_read(&ports[i], buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                char *line = (char *)buf;
                char *end;
                while ((end = strchr(line, '\n')) != NULL) {
                    *end = '\0';
                    if (line[0]) printf("[%s] %s\n", ports[i].name, line);
                    line = end + 1;
                }
                if (line[0]) printf("[%s] %s", ports[i].name, line);
            }
        }
    }

    printf("\nShutting down...\n");
    for (int i = 0; i < nports; i++) serial_close(&ports[i]);
    return 0;
}
