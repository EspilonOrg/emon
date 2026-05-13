#include "serial.h"

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <termios.h>
#include <sys/ioctl.h>
#endif

/* ── Internal helpers ──────────────────────────────────────────────────── */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int configure_port(struct sp_port *sp, int baud)
{
    struct sp_port_config *cfg;

    if (sp_new_config(&cfg) != SP_OK)
        return -1;

    sp_set_config_baudrate(cfg, baud);
    sp_set_config_bits(cfg, 8);
    sp_set_config_parity(cfg, SP_PARITY_NONE);
    sp_set_config_stopbits(cfg, 1);
    sp_set_config_flowcontrol(cfg, SP_FLOWCONTROL_NONE);

    int ret = (sp_set_config(sp, cfg) == SP_OK) ? 0 : -1;
    sp_free_config(cfg);
    return ret;
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

int serial_open(serial_port_t *p)
{
    if (!p || !p->path[0])
        return -1;

    /* Get port handle */
    if (sp_get_port_by_name(p->path, &p->sp) != SP_OK) {
        p->state = PORT_STATE_ERROR;
        return -1;
    }

    /* Open read/write */
    if (sp_open(p->sp, SP_MODE_READ_WRITE) != SP_OK) {
        sp_free_port(p->sp);
        p->sp    = NULL;
        p->state = PORT_STATE_ERROR;
        return -1;
    }

#ifndef _WIN32
    /*
     * Prevent USB-UART bridge from resetting the device (ESP32, Arduino, etc.)
     *   1. Clear HUPCL so close() doesn't drop DTR (next open() won't pulse it).
     *   2. Hold DTR and RTS de-asserted explicitly — keeps EN/GPIO0 in run state
     *      on standard auto-reset circuits (CP2102, CH340, FT232).
     * The very first open() may still emit a brief pulse, but subsequent
     * reopens via auto-reconnect won't.
     */
    int fd = -1;
    if (sp_get_port_handle(p->sp, &fd) == SP_OK && fd >= 0) {
        struct termios t;
        if (tcgetattr(fd, &t) == 0) {
            t.c_cflag &= ~HUPCL;
            tcsetattr(fd, TCSANOW, &t);
        }
    }
    sp_set_dtr(p->sp, SP_DTR_OFF);
    sp_set_rts(p->sp, SP_RTS_OFF);
#endif

    if (configure_port(p->sp, p->baud) != 0) {
        sp_close(p->sp);
        sp_free_port(p->sp);
        p->sp    = NULL;
        p->state = PORT_STATE_ERROR;
        return -1;
    }

    p->state     = PORT_STATE_OPEN;
    p->open_time = now_ms();
    p->bytes_rx  = 0;
    p->bytes_tx  = 0;
    return 0;
}

void serial_close(serial_port_t *p)
{
    if (!p || !p->sp)
        return;
    sp_close(p->sp);
    sp_free_port(p->sp);
    p->sp    = NULL;
    p->state = PORT_STATE_CLOSED;
}

int serial_reopen(serial_port_t *p)
{
    serial_close(p);
    {struct timespec _ts = {0, 200000000L}; nanosleep(&_ts, NULL);} /* 200ms pause before retry */
    return serial_open(p);
}

/* ── I/O ───────────────────────────────────────────────────────────────── */

int serial_read(serial_port_t *p, uint8_t *buf, size_t len)
{
    if (!p || !p->sp || p->state != PORT_STATE_OPEN)
        return -1;

    int n = sp_blocking_read(p->sp, buf, len, SERIAL_READ_TIMEOUT);

    if (n < 0) {
        p->state = PORT_STATE_ERROR;
        return -1;
    }
    p->bytes_rx += (uint64_t)n;
    return n;
}

int serial_write(serial_port_t *p, const uint8_t *buf, size_t len)
{
    if (!p || !p->sp || p->state != PORT_STATE_OPEN)
        return -1;

    int n = sp_blocking_write(p->sp, buf, len, 1000);
    if (n < 0) {
        p->state = PORT_STATE_ERROR;
        return -1;
    }
    p->bytes_tx += (uint64_t)n;
    return n;
}

int serial_write_str(serial_port_t *p, const char *s)
{
    return serial_write(p, (const uint8_t *)s, strlen(s));
}

/* ── Hardware control ──────────────────────────────────────────────────── */

int serial_set_rts(serial_port_t *p, bool level)
{
    if (!p || !p->sp)
        return -1;
    return (sp_set_rts(p->sp, level ? SP_RTS_ON : SP_RTS_OFF) == SP_OK) ? 0 : -1;
}

int serial_set_dtr(serial_port_t *p, bool level)
{
    if (!p || !p->sp)
        return -1;
    return (sp_set_dtr(p->sp, level ? SP_DTR_ON : SP_DTR_OFF) == SP_OK) ? 0 : -1;
}

int serial_reset(serial_port_t *p)
{
    if (!p || !p->sp)
        return -1;

    /*
     * Standard ESP32 / Arduino reset sequence:
     *   DTR low + RTS high  → enter bootloader / reset
     *   DTR high + RTS low  → normal boot
     * A 100ms pulse is enough to trigger the reset.
     */
    serial_set_dtr(p, false);
    serial_set_rts(p, true);
    {struct timespec _ts = {0, 100000000L}; nanosleep(&_ts, NULL);}
    serial_set_dtr(p, true);
    serial_set_rts(p, false);
    {struct timespec _ts = {0, 50000000L}; nanosleep(&_ts, NULL);}
    return 0;
}

/* ── Discovery ─────────────────────────────────────────────────────────── */

static int match_family(const char *buf, char *family_out, size_t family_len);  /* fwd */

int serial_list(char out[][64], int max)
{
    struct sp_port **ports;
    int count = 0;

    if (sp_list_ports(&ports) != SP_OK)
        return 0;

    for (int i = 0; ports[i] && count < max; i++) {
        const char *name = sp_get_port_name(ports[i]);
        if (name) {
            strncpy(out[count], name, 63);
            out[count][63] = '\0';
            count++;
        }
    }
    sp_free_port_list(ports);
    return count;
}

int serial_detect_device(serial_port_t *p, char *family_out, size_t family_len)
{
    uint8_t buf[256];
    char    boot[512] = {0};
    int     total     = 0;
    uint64_t deadline = now_ms() + 2000; /* 2s window */

    /* Flush input then read boot messages */
    sp_flush(p->sp, SP_BUF_INPUT);
    serial_reset(p);

    while (now_ms() < deadline && total < (int)sizeof(boot) - 1) {
        int n = serial_read(p, buf, sizeof(buf) - 1);
        if (n > 0) {
            memcpy(boot + total, buf, n);
            total += n;
        }
    }
    boot[total] = '\0';
    return match_family(boot, family_out, family_len);
}

/* Match boot strings against known families. Returns 0 on match, -1 if unknown. */
static int match_family(const char *buf, char *family_out, size_t family_len)
{
    if (strstr(buf, "ESPILON") || strstr(buf, "espm_sys") ||
        strstr(buf, "espilon")) {
        strncpy(family_out, "espilon", family_len - 1);
    } else if (strstr(buf, "ESP-IDF") || strstr(buf, "esp32") ||
               strstr(buf, "ESP32")   || strstr(buf, "esp-idf")) {
        strncpy(family_out, "esp32", family_len - 1);
    } else if (strstr(buf, "STM32") || strstr(buf, "CubeMX")) {
        strncpy(family_out, "stm32", family_len - 1);
    } else if (strstr(buf, "Arduino")) {
        strncpy(family_out, "arduino", family_len - 1);
    } else if (strstr(buf, "Zephyr")) {
        strncpy(family_out, "zephyr", family_len - 1);
    } else {
        strncpy(family_out, "unknown", family_len - 1);
        family_out[family_len - 1] = '\0';
        return -1;
    }
    family_out[family_len - 1] = '\0';
    return 0;
}

int serial_detect_passive(serial_port_t *p, char *family_out, size_t family_len)
{
    uint8_t  buf[256];
    char     boot[1024] = {0};
    int      total      = 0;
    uint64_t deadline   = now_ms() + 2000;   /* 2s passive window */

    sp_flush(p->sp, SP_BUF_INPUT);            /* discard stale bytes */

    while (now_ms() < deadline && total < (int)sizeof(boot) - 1) {
        int n = serial_read(p, buf, sizeof(buf) - 1);
        if (n > 0) {
            memcpy(boot + total, buf, (size_t)n);
            total += n;
        }
    }
    boot[total] = '\0';

    return match_family(boot, family_out, family_len);
}

int serial_detect_from_usb(const char *path, char *family_out, size_t family_len)
{
    struct sp_port *port = NULL;
    if (sp_get_port_by_name(path, &port) != SP_OK) return -1;

    int matched = 0;

    /* ── Step 1: USB VID/PID ─────────────────────────────────────────── */
    int vid = 0, pid = 0;
    if (sp_get_port_usb_vid_pid(port, &vid, &pid) == SP_OK) {
        switch (vid) {
        /* Espressif internal USB-JTAG (C3/C6/H2/S3/P4) */
        case 0x303A:
            strncpy(family_out, "esp32", family_len - 1);
            matched = 1; break;

        /* Silicon Labs CP2101/CP2102/CP2104 — ubiquitous on ESP32 devkits */
        case 0x10C4:
        /* QinHeng CH340/CH341 — very common cheap ESP32 boards */
        case 0x1A86:
        /* FTDI FT232R/FT231X — older devkits */
        case 0x0403:
            strncpy(family_out, "esp32", family_len - 1);
            matched = 1; break;

        /* STMicroelectronics ST-Link/V2/V3 */
        case 0x0483:
            strncpy(family_out, "stm32", family_len - 1);
            matched = 1; break;

        /* Arduino LLC */
        case 0x2341:
        /* Arduino.org */
        case 0x2A03:
        /* Adafruit (CircuitPython/Arduino-compatible) */
        case 0x239A:
            strncpy(family_out, "arduino", family_len - 1);
            matched = 1; break;

        default:
            break;
        }
    }

    /* ── Step 2: description string (fallback for unknown VIDs) ─────── */
    if (!matched) {
        const char *desc = sp_get_port_description(port);
        if (desc) {
            if (strstr(desc, "CP210") || strstr(desc, "CH340") ||
                strstr(desc, "CH341") || strstr(desc, "Espressif") ||
                strstr(desc, "ESP32") || strstr(desc, "ESP8266")) {
                strncpy(family_out, "esp32", family_len - 1);
                matched = 1;
            } else if (strstr(desc, "STMicro") || strstr(desc, "ST-Link") ||
                       strstr(desc, "STLink")) {
                strncpy(family_out, "stm32", family_len - 1);
                matched = 1;
            } else if (strstr(desc, "Arduino")) {
                strncpy(family_out, "arduino", family_len - 1);
                matched = 1;
            }
        }
    }

    if (matched) family_out[family_len - 1] = '\0';
    sp_free_port(port);
    return matched ? 0 : -1;
}

/* ── Helpers ───────────────────────────────────────────────────────────── */

const char *serial_state_str(port_state_t state)
{
    switch (state) {
    case PORT_STATE_CLOSED:       return "closed";
    case PORT_STATE_OPEN:         return "open";
    case PORT_STATE_ERROR:        return "error";
    case PORT_STATE_RECONNECTING: return "reconnecting";
    default:                      return "unknown";
    }
}

bool serial_is_open(const serial_port_t *p)
{
    return p && p->sp && p->state == PORT_STATE_OPEN;
}
