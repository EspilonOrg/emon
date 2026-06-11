#ifndef ESPILON_INTERACTIVE_H
#define ESPILON_INTERACTIVE_H

#include "serial/serial.h"
#include <stdbool.h>

/*
 * Interactive mode - bidirectional serial terminal.
 *
 * stdin → (raw, escape-filtered) → target serial port
 * serial port → (unchanged)       → stdout + log  (handled by monitor thread)
 *
 * Escape key: Ctrl+A (0x01)
 *   Ctrl+A X  - quit monitor
 *   Ctrl+A A  - send literal Ctrl+A to device
 *   Ctrl+A H  - print key bindings
 */

/* Call once before monitor_run(). Saves terminal state and sets raw mode.
 * Returns 0 on success, -1 if stdin is not a tty. */
int  interactive_init(void);

/* Start the stdin-forwarding thread targeting `port`.
 * forward=true: typing is forwarded to device (requires -i).
 * forward=false: TUI navigation only, no serial forwarding.
 * interactive_init() must have been called first. */
int  interactive_start(serial_port_t *port, bool forward);

/* Signal the stdin thread to stop (non-blocking). */
void interactive_stop(void);

/* Restore the original terminal state. Safe to call multiple times.
 * Registered automatically via atexit() by interactive_init(). */
void interactive_restore(void);

/* Print the escape-key help line to stderr (does not disturb raw mode). */
void interactive_print_help(void);

/* Called by monitor/display threads (with print_lock held) to avoid
 * interleaving device output with the user's in-progress input line.
 * pre_print erases the current input line; post_print redraws it. */
void interactive_pre_print(void);
void interactive_post_print(void);

#endif /* ESPILON_INTERACTIVE_H */
