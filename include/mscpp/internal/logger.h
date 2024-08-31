#pragma once

#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"
// # TODO

typedef enum log_level_e
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

typedef struct log_state_s
{
    char        file[PATH_MAX];
    log_level_t level;
    bool        console;
    bool        console_color;
    bool        rotate;
    bool        shutdown;
    bool        synchronous;

    FILE* fd;
} log_state_t;

static list_t          log_queue;
static log_level_t     light_log_level;
static log_state_t     target_log_state;
static log_state_t     current_log_state;
static pthread_once_t  log_init_flag = PTHREAD_ONCE_INIT;
static pthread_mutex_t log_lock;
static pthread_cond_t  log_cond;

void log_init()
{
    pthread_mutexattr_t lockAttr;

    pthread_mutexattr_init(&lockAttr);
    pthread_mutexattr_settype(&lockAttr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_init(&log_lock, &lockAttr);
    pthread_cond_init(&log_cond, NULL);

    list_init(&log_queue);

    memset(&target_log_state, 0, sizeof target_log_state);
    memset(&current_log_state, 0, sizeof current_log_state);

    light_log_level          = LOG_INFO;
    target_log_state.level   = LOG_INFO;
    target_log_state.console = true;
    /* smartly color based on whether STDERR is attached to a tty */
    target_log_state.console_color = isatty(STDERR_FILENO) == 1;
}

void mslogSetLevel(log_level_t level)
{
    pthread_once(&log_init_flag, log_init);
    // TODO
}
