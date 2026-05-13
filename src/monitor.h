#ifndef ESPILON_MONITOR_H
#define ESPILON_MONITOR_H

#include "serial.h"
#include "detector.h"
#include "recorder.h"
#include "reset.h"
#include "display.h"
#include "config.h"
#include <pthread.h>
#include <stdbool.h>

#define MONITOR_MAX_DEVICES 32

typedef struct {
    serial_port_t   port;
    detector_t      detector;
    recorder_t      recorder;
    resetter_t      resetter;
    const char     *color;
    int             crash_count;
    bool            running;
    pthread_t       thread;
} monitor_device_t;

typedef struct {
    monitor_device_t devices[MONITOR_MAX_DEVICES];
    int              ndevices;
    config_t        *cfg;
    bool             running;
    pthread_mutex_t  print_lock;  /* serialize output */
    uint64_t         start_ms;
} monitor_t;

int  monitor_init(monitor_t *m, config_t *cfg);
void monitor_free(monitor_t *m);

int  monitor_run(monitor_t *m);   /* blocks until stop */
void monitor_stop(monitor_t *m);
void monitor_stop_all(void);
int  monitor_get_exit_code(void);

void monitor_print_summary(const monitor_t *m);

#endif
