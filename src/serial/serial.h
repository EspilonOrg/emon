#ifndef ESPILON_SERIAL_H
#define ESPILON_SERIAL_H

#include <stdint.h>
#include <stdbool.h>
#include <libserialport.h>

#define SERIAL_BUF_SIZE     4096
#define SERIAL_READ_TIMEOUT 100   /* ms */
#define SERIAL_MAX_PORTS    32

typedef enum {
    PORT_STATE_CLOSED = 0,
    PORT_STATE_OPEN,
    PORT_STATE_ERROR,
    PORT_STATE_RECONNECTING,
} port_state_t;

/* One serial port descriptor */
typedef struct {
    char                    path[64];     /* e.g. /dev/ttyACM0 */
    char                    name[32];     /* friendly name, e.g. "C6-TEE" */
    int                     baud;
    enum sp_flowcontrol     flow;         /* SP_FLOWCONTROL_NONE / RTSCTS / XONXOFF */
    struct sp_port         *sp;
    port_state_t            state;
    uint64_t                bytes_rx;
    uint64_t                bytes_tx;
    uint64_t                open_time;    /* unix timestamp */
    bool                    auto_reconnect;
} serial_port_t;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */
int  serial_open(serial_port_t *p);
void serial_close(serial_port_t *p);
int  serial_reopen(serial_port_t *p);

/* ── I/O ───────────────────────────────────────────────────────────────── */

/*
 * Read up to `len` bytes into `buf`.
 * Returns number of bytes read, 0 on timeout, -1 on error.
 */
int  serial_read(serial_port_t *p, uint8_t *buf, size_t len);

/*
 * Write `len` bytes from `buf`.
 * Returns bytes written, -1 on error.
 */
int  serial_write(serial_port_t *p, const uint8_t *buf, size_t len);

/* Convenience: write a null-terminated string */
int  serial_write_str(serial_port_t *p, const char *s);

/* ── Hardware control ──────────────────────────────────────────────────── */

/* Pulse RTS/DTR to trigger a hardware reset (typical ESP32 / Arduino) */
int  serial_reset(serial_port_t *p);

/* Set RTS pin high/low */
int  serial_set_rts(serial_port_t *p, bool level);

/* Set DTR pin high/low */
int  serial_set_dtr(serial_port_t *p, bool level);

/* ── Discovery ─────────────────────────────────────────────────────────── */

/* List available serial ports. Returns count, fills `out` up to `max`. */
int  serial_list(char out[][64], int max);

/* Detect device family from USB VID/PID and description string.
 * Does not open the port. Instant.
 * Returns 0 and fills family_out on success, -1 if unrecognised. */
int  serial_detect_from_usb(const char *path, char *family_out, size_t family_len);

/* Detect device family by passively reading 2s of serial output.
 * Port must already be open. Safe on running firmware (no reset sent).
 * Returns 0 and fills family_out on success, -1 if unrecognised. */
int  serial_detect_passive(serial_port_t *p, char *family_out, size_t family_len);

/* ── Helpers ───────────────────────────────────────────────────────────── */
const char *serial_state_str(port_state_t state);
bool        serial_is_open(const serial_port_t *p);

#endif /* ESPILON_SERIAL_H */
