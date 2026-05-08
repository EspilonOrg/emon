#ifndef ESPILON_RESET_H
#define ESPILON_RESET_H

#include "serial.h"
#include "detector.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    serial_port_t  *port;
    severity_t      threshold;
    int             cooldown_ms;
    uint64_t        last_reset_ms;
    int             reset_count;
} resetter_t;

void resetter_init(resetter_t *r, serial_port_t *port,
                   severity_t threshold, int cooldown_ms);

/*
 * Trigger a reset if event severity >= threshold AND cooldown elapsed.
 * Returns true if reset was performed.
 */
bool resetter_maybe_reset(resetter_t *r, const det_event_t *ev);

#endif
