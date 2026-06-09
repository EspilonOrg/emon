#include "serial/reset.h"
#include "utils/utils.h"
#include <stdio.h>

void resetter_init(resetter_t *r, serial_port_t *port,
                   severity_t threshold, int cooldown_ms)
{
    r->port           = port;
    r->threshold      = threshold;
    r->cooldown_ms    = cooldown_ms;
    r->last_reset_ms  = 0;
    r->reset_count    = 0;
}

bool resetter_maybe_reset(resetter_t *r, const det_event_t *ev)
{
    if (!r || !ev) return false;
    if (ev->severity < r->threshold) return false;

    uint64_t now = now_ms();
    if (now - r->last_reset_ms < (uint64_t)r->cooldown_ms)
        return false;

    r->last_reset_ms = now;
    r->reset_count++;

    fprintf(stderr, "[reset] %s - #%d after %s\n",
            r->port->name, r->reset_count,
            ev->rule ? ev->rule->name : "event");

    serial_reset(r->port);
    return true;
}
