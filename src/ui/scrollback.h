#ifndef ESPILON_SCROLLBACK_H
#define ESPILON_SCROLLBACK_H

/*
 * In-process scrollback buffer.
 *
 * All displayed lines are pushed into a ring buffer (with ANSI colors).
 * Ctrl+A [ in interactive mode enters a full-screen browsing mode:
 *   ↑ / ↓         scroll one line
 *   PgUp / PgDn   scroll one page
 *   g / G         go to top / bottom (live)
 *   / <query>     incremental search, Enter confirms
 *   n             next search hit
 *   N             previous search hit
 *   q / Ctrl+A [  exit scrollback, resume live view
 *
 * While in scrollback mode, live output is suppressed but still
 * accumulated in the buffer - nothing is lost.
 */

#define SCROLLBACK_LINES  5000
#define SCROLLBACK_WIDTH   512

void scrollback_init(void);

/* Push a formatted (ANSI) line.  Called by display_line / display_event. */
void scrollback_push(const char *line);

/* Enter full-screen scrollback mode.  Blocks until user exits.
 * Stdin must already be in raw mode (i.e. called from -i thread). */
void scrollback_enter(void);

/* Returns 1 while in scrollback mode - display functions suppress stdout. */
int  scrollback_is_active(void);

#endif /* ESPILON_SCROLLBACK_H */
