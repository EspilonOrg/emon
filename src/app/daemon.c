#include "app/daemon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PID_FILENAME  ".espilon-monitor.pid"

static void pid_path(const char *logdir, char *out, size_t len)
{
    snprintf(out, len, "%s/%s", logdir, PID_FILENAME);
}

static pid_t read_pid(const char *logdir)
{
    char path[512];
    pid_path(logdir, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    pid_t pid = -1;
    fscanf(f, "%d", (int *)&pid);
    fclose(f);
    return pid;
}

static int write_pid(const char *logdir, pid_t pid)
{
    char path[512];
    pid_path(logdir, path, sizeof(path));

    mkdir(logdir, 0755);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%d\n", (int)pid);
    fclose(f);
    return 0;
}

static void remove_pid(const char *logdir)
{
    char path[512];
    pid_path(logdir, path, sizeof(path));
    unlink(path);
}

static int process_alive(pid_t pid)
{
    return (kill(pid, 0) == 0 || errno == EPERM);
}

/* ── daemonize ──────────────────────────────────────────────── */

int daemonize(const char *logdir)
{
    /* First fork */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid > 0) {
        /* Parent: wait for first child then return 0 so caller can exit */
        waitpid(pid, NULL, 0);
        return 0;
    }

    /* First child: new session leader */
    setsid();

    /* Second fork: detach from session, can never re-acquire terminal */
    pid = fork();
    if (pid < 0) { perror("fork2"); _exit(1); }
    if (pid > 0) { _exit(0); }                /* first child exits */

    /* Daemon process: redirect stdio to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) close(devnull);
    }

    /* Write PID file */
    write_pid(logdir, getpid());

    /* Daemon never returns - caller continues with monitor_run() */
    return -2;   /* sentinel: daemon path, do NOT exit */
}

/* ── daemon_stop ────────────────────────────────────────────── */

int daemon_stop(const char *logdir)
{
    pid_t pid = read_pid(logdir);
    if (pid <= 0) {
        fprintf(stderr, "espilon-monitor: not running (no PID file)\n");
        return 1;
    }

    if (!process_alive(pid)) {
        fprintf(stderr, "espilon-monitor: PID %d not found, cleaning up\n", pid);
        remove_pid(logdir);
        return 1;
    }

    printf("espilon-monitor: stopping PID %d...\n", (int)pid);
    kill(pid, SIGTERM);

    for (int i = 0; i < 50; i++) {
        struct timespec ts = {0, 100000000L};   /* 100ms */
        nanosleep(&ts, NULL);
        if (!process_alive(pid)) {
            remove_pid(logdir);
            printf("espilon-monitor: stopped\n");
            return 0;
        }
    }

    /* Still alive after 5s - SIGKILL */
    fprintf(stderr, "espilon-monitor: PID %d did not exit, sending SIGKILL\n",
            (int)pid);
    kill(pid, SIGKILL);
    remove_pid(logdir);
    return 0;
}

/* ── daemon_status ──────────────────────────────────────────── */

int daemon_status(const char *logdir)
{
    pid_t pid = read_pid(logdir);
    if (pid <= 0 || !process_alive(pid)) {
        printf("espilon-monitor: not running\n");
        if (pid > 0) remove_pid(logdir);
        return 1;
    }
    printf("espilon-monitor: running (PID %d)\n", (int)pid);
    return 0;
}
