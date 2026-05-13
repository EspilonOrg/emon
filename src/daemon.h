#ifndef ESPILON_DAEMON_H
#define ESPILON_DAEMON_H

/*
 * Background daemon management.
 *
 * daemonize()     — double-fork, detach from terminal, write PID file.
 *                   Returns in the PARENT (pid > 0) so caller can exit.
 *                   The daemon process never returns from this call.
 *
 * daemon_stop()   — read PID file, send SIGTERM, wait up to 5s, SIGKILL.
 * daemon_status() — check if daemon is running, print PID + uptime.
 */

/* Returns 0 in the parent (caller should exit), -1 on error.
 * logdir is used to locate the PID file. */
int  daemonize(const char *logdir);

/* Returns 0 on success (daemon stopped), 1 if not running, -1 on error. */
int  daemon_stop(const char *logdir);

/* Prints status to stdout. Returns 0 if running, 1 if not. */
int  daemon_status(const char *logdir);

#endif /* ESPILON_DAEMON_H */
