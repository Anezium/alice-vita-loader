/*
 * Host (Linux) shim of the few Vita kernel calls used by source/reimpl/futex.c,
 * so the futex emulation can be stress-tested without a console.
 * Part of spike-c/loader (Alice Vita loader), MIT licence.
 */
#ifndef VITA_SHIM_H
#define VITA_SHIM_H
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

typedef struct { pthread_mutex_t m; } SceKernelLwMutexWork;
typedef struct { pthread_cond_t c; SceKernelLwMutexWork *mutex; } SceKernelLwCondWork;
typedef struct { uint64_t tick; } SceRtcTick;
typedef unsigned int SceUInt;

#define SCE_KERNEL_ERROR_WAIT_TIMEOUT 0x80028005

static inline int sceKernelCreateLwMutex(SceKernelLwMutexWork *w, const char *n, unsigned a, int c, void *o) {
    return pthread_mutex_init(&w->m, NULL) ? -1 : 0;
}
static inline int sceKernelLockLwMutex(SceKernelLwMutexWork *w, int c, unsigned *t) { return pthread_mutex_lock(&w->m); }
static inline int sceKernelUnlockLwMutex(SceKernelLwMutexWork *w, int c) { return pthread_mutex_unlock(&w->m); }
static inline int sceKernelCreateLwCond(SceKernelLwCondWork *w, const char *n, unsigned a, SceKernelLwMutexWork *m, void *o) {
    w->mutex = m;
    pthread_condattr_t at; pthread_condattr_init(&at); pthread_condattr_setclock(&at, CLOCK_MONOTONIC);
    return pthread_cond_init(&w->c, &at) ? -1 : 0;
}
static inline uint64_t sceKernelGetProcessTimeWide(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
static inline int sceKernelWaitLwCond(SceKernelLwCondWork *w, unsigned *timeout_us) {
    if (!timeout_us) return pthread_cond_wait(&w->c, &w->mutex->m);
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_nsec + (uint64_t)*timeout_us * 1000ULL;
    ts.tv_sec += ns / 1000000000ULL; ts.tv_nsec = ns % 1000000000ULL;
    int r = pthread_cond_timedwait(&w->c, &w->mutex->m, &ts);
    return r == ETIMEDOUT ? (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT : (r ? -1 : 0);
}
static inline int sceKernelSignalLwCondAll(SceKernelLwCondWork *w) { return pthread_cond_broadcast(&w->c); }
static inline int sceRtcGetCurrentTick(SceRtcTick *t) {
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    t->tick = (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000 + 62135587294000000ULL;
    return 0;
}
#endif
