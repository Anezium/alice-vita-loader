/*
 * Host stress test of the futex emulation (source/reimpl/futex.c) with the
 * locking protocol Rust std uses on Android (library/std/src/sys/sync/mutex/futex.rs):
 * state 0 = unlocked, 1 = locked, 2 = locked with waiters; FUTEX_WAIT_BITSET with
 * an absolute CLOCK_MONOTONIC deadline, FUTEX_WAKE of one waiter on unlock.
 * Also checks EAGAIN on value mismatch, timeouts and wake counts.
 * Build/run: see tests/host/run.sh. Part of spike-c/loader, MIT licence.
 */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "reimpl/futex.h"

#define FUTEX_WAIT_BITSET_PRIVATE (9 | 128)
#define FUTEX_WAKE_PRIVATE (1 | 128)

/* bionic arm32 timespec (32-bit time_t), what Rust passes on armv7-linux-androideabi */
typedef struct { int32_t tv_sec; int32_t tv_nsec; } ts32;

static long fwait(_Atomic uint32_t *f, uint32_t v, const struct timespec *abs) {
    ts32 t;
    if (abs) { t.tv_sec = (int32_t)abs->tv_sec; t.tv_nsec = (int32_t)abs->tv_nsec; }
    return futex_syscall(0, (uint32_t *)f, FUTEX_WAIT_BITSET_PRIVATE, v, abs ? &t : NULL, NULL, 0xffffffffu);
}
static long fwake(_Atomic uint32_t *f, int n) {
    return futex_syscall(0, (uint32_t *)f, FUTEX_WAKE_PRIVATE, (uint32_t)n, NULL, NULL, 0);
}

/* Rust std futex Mutex */
static void lock(_Atomic uint32_t *s) {
    uint32_t z = 0;
    if (atomic_compare_exchange_strong(s, &z, 1)) return;
    while (atomic_exchange(s, 2) != 0) fwait(s, 2, NULL);
}
static void unlock(_Atomic uint32_t *s) {
    if (atomic_exchange(s, 0) == 2) fwake(s, 1);
}

#define THREADS 8
#define ITERS 200000
static _Atomic uint32_t mtx = 0;
static long counter = 0;

static void *worker(void *arg) {
    for (int i = 0; i < ITERS; i++) { lock(&mtx); counter++; unlock(&mtx); }
    return NULL;
}

static _Atomic uint32_t parked = 0;
static void *parker(void *arg) {
    while (atomic_load(&parked) == 0) fwait(&parked, 0, NULL);
    return NULL;
}

int main(void) {
    assert(futex_init() == 0);

    _Atomic uint32_t f = 5;
    errno = 0;
    assert(fwait(&f, 4, NULL) == -1 && errno == EAGAIN);           // value mismatch
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    struct timespec dl = now; dl.tv_nsec += 50 * 1000000; if (dl.tv_nsec >= 1000000000) { dl.tv_sec++; dl.tv_nsec -= 1000000000; }
    errno = 0;
    assert(fwait(&f, 5, &dl) == -1 && errno == ETIMEDOUT);         // absolute deadline
    struct timespec after; clock_gettime(CLOCK_MONOTONIC, &after);
    long waited_ms = (after.tv_sec - now.tv_sec) * 1000 + (after.tv_nsec - now.tv_nsec) / 1000000;
    printf("timed wait: %ld ms (deadline 50 ms)\n", waited_ms);
    assert(waited_ms >= 45 && waited_ms < 500);
    assert(fwake(&f, 10) == 0);                                     // nobody waiting

    // wake one parked thread
    pthread_t p; pthread_create(&p, NULL, parker, NULL);
    struct timespec ms = {0, 20 * 1000000}; nanosleep(&ms, NULL);
    atomic_store(&parked, 1);
    long woke = fwake(&parked, 1);
    pthread_join(p, NULL);
    printf("wake after park: %ld waiter(s) woken\n", woke);

    // contended mutex
    pthread_t t[THREADS];
    for (int i = 0; i < THREADS; i++) pthread_create(&t[i], NULL, worker, NULL);
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
    printf("counter = %ld (expected %d)\n", counter, THREADS * ITERS);
    assert(counter == (long)THREADS * ITERS);

    errno = 0;
    assert(futex_syscall(0, (uint32_t *)&f, 3 /* REQUEUE */, 0, NULL, NULL, 0) == -1 && errno == ENOSYS);
    printf("futex host test: OK\n");
    return 0;
}
