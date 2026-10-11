/*
 * Linux futex(2) emulation for .so files built for Android.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * Why: Rust std for target_os = "android" implements Mutex, RwLock, Once,
 * Condvar and thread::park with raw syscall(SYS_futex, ...) calls. Only
 * FUTEX_WAIT / FUTEX_WAIT_BITSET / FUTEX_WAKE / FUTEX_WAKE_BITSET are used by
 * std; the bitset is always FUTEX_BITSET_MATCH_ANY.
 *
 * Design:
 *   - FUTEX_BUCKETS hash buckets, each = one SceKernelLwMutex + one
 *     SceKernelLwCond bound to it, plus an intrusive list of waiters.
 *   - WAIT: lock bucket; if *uaddr != val -> EAGAIN; else enqueue a waiter node
 *     (on the waiter's stack) and sceKernelWaitLwCond (atomically releases the
 *     bucket mutex) until the node is marked woken or the timeout expires.
 *   - WAKE(n): lock bucket; mark up to n waiters on uaddr as woken and unlink
 *     them; SignalLwCondAll if any. Waiters on other addresses of the same
 *     bucket wake up spuriously, see their node unmarked, and wait again.
 *   The value check and the enqueue happen under the same lock as WAKE, which is
 *   the only property futex users rely on (no lost wake-up).
 *
 * Trade-off: a WAKE also wakes the waiters of other addresses that hash to the
 * same bucket; they recheck their node and sleep again, which is cheap with a
 * dozen threads. Rejected: event flags (one kernel object per futex, cannot be
 * indexed by address without a dynamic table) and one semaphore per wait
 * (kernel object creation on every contended lock).
 */

#include "reimpl/futex.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/error.h>
#include <psp2/rtc.h>

#include "utils/logger.h"

#define FUTEX_BUCKETS 16

#define FUTEX_WAIT            0
#define FUTEX_WAKE            1
#define FUTEX_REQUEUE         3
#define FUTEX_CMP_REQUEUE     4
#define FUTEX_WAKE_OP         5
#define FUTEX_WAIT_BITSET     9
#define FUTEX_WAKE_BITSET    10
#define FUTEX_PRIVATE_FLAG  128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK      (~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME))

typedef struct futex_waiter {
    struct futex_waiter *next;
    volatile uint32_t *addr;
    volatile int woken;
} futex_waiter;

typedef struct {
    SceKernelLwMutexWork mutex;
    SceKernelLwCondWork cond;
    futex_waiter *head;
} futex_bucket;

static futex_bucket buckets[FUTEX_BUCKETS] __attribute__((aligned(8)));
static volatile int futex_ready = 0;

int futex_init(void) {
    char name[32];
    for (int i = 0; i < FUTEX_BUCKETS; i++) {
        snprintf(name, sizeof(name), "futex_m%d", i);
        int ret = sceKernelCreateLwMutex(&buckets[i].mutex, name, 0, 0, NULL);
        if (ret < 0) {
            l_fatal("futex_init: sceKernelCreateLwMutex(%d) failed: 0x%08x", i, ret);
            return ret;
        }
        snprintf(name, sizeof(name), "futex_c%d", i);
        ret = sceKernelCreateLwCond(&buckets[i].cond, name, 0, &buckets[i].mutex, NULL);
        if (ret < 0) {
            l_fatal("futex_init: sceKernelCreateLwCond(%d) failed: 0x%08x", i, ret);
            return ret;
        }
        buckets[i].head = NULL;
    }
    futex_ready = 1;
    return 0;
}

static inline futex_bucket *bucket_for(volatile uint32_t *addr) {
    uintptr_t a = (uintptr_t)addr;
    return &buckets[((a >> 2) ^ (a >> 7) ^ (a >> 13)) % FUTEX_BUCKETS];
}

// Monotonic clock in microseconds: same source as clock_gettime(CLOCK_MONOTONIC)
// in reimpl/sys.c, which is what Rust uses to compute absolute deadlines.
static inline int64_t now_monotonic_us(void) {
    return (int64_t)sceKernelGetProcessTimeWide();
}

// Same source and epoch shift as clock_gettime(CLOCK_REALTIME) in reimpl/sys.c.
static inline int64_t now_realtime_us(void) {
    SceRtcTick tick;
    sceRtcGetCurrentTick(&tick);
    return (int64_t)(tick.tick - 62135587294000000ULL);
}

static void unlink_waiter(futex_bucket *b, futex_waiter *w) {
    futex_waiter **pp = &b->head;
    while (*pp) {
        if (*pp == w) {
            *pp = w->next;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* timeout_us < 0: infinite. Returns 0 or -errno (newlib values). */
static int futex_wait_us(volatile uint32_t *uaddr, uint32_t val, int64_t timeout_us) {
    futex_bucket *b = bucket_for(uaddr);
    futex_waiter self = { .next = NULL, .addr = uaddr, .woken = 0 };
    int64_t deadline = timeout_us >= 0 ? now_monotonic_us() + timeout_us : -1;

    sceKernelLockLwMutex(&b->mutex, 1, NULL);
    if (__atomic_load_n(uaddr, __ATOMIC_SEQ_CST) != val) {
        sceKernelUnlockLwMutex(&b->mutex, 1);
        return -EAGAIN;
    }
    self.next = b->head;
    b->head = &self;

    int result = 0;
    while (!self.woken) {
        int ret;
        if (deadline < 0) {
            ret = sceKernelWaitLwCond(&b->cond, NULL);
        } else {
            int64_t left = deadline - now_monotonic_us();
            if (left <= 0) {
                result = -ETIMEDOUT;
                break;
            }
            unsigned int t = left > 0x7fffffff ? 0x7fffffff : (unsigned int)left;
            ret = sceKernelWaitLwCond(&b->cond, &t);
        }
        if (ret < 0 && ret != (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT) {
            l_error("futex_wait(%p): sceKernelWaitLwCond failed 0x%08x", uaddr, ret);
            result = -EINTR; // caller (Rust std) re-checks the value and retries
            break;
        }
        // On timeout or spurious wake-up, loop: woken flag / deadline decide.
    }
    if (!self.woken) {
        unlink_waiter(b, &self);
    } else {
        result = 0; // woken wins over a simultaneous timeout
    }
    sceKernelUnlockLwMutex(&b->mutex, 1);
    return result;
}

static int futex_wake_n(volatile uint32_t *uaddr, int n) {
    if (n <= 0)
        return 0;
    futex_bucket *b = bucket_for(uaddr);
    int woken = 0;

    sceKernelLockLwMutex(&b->mutex, 1, NULL);
    futex_waiter **pp = &b->head;
    while (*pp && woken < n) {
        futex_waiter *w = *pp;
        if (w->addr == uaddr) {
            *pp = w->next;   // unlink before signalling: the node lives on the waiter's stack
            w->next = NULL;
            w->woken = 1;
            woken++;
        } else {
            pp = &w->next;
        }
    }
    if (woken)
        sceKernelSignalLwCondAll(&b->cond);
    sceKernelUnlockLwMutex(&b->mutex, 1);
    return woken;
}

typedef struct { int32_t tv_sec; int32_t tv_nsec; } timespec32_bionic;
typedef struct { int64_t tv_sec; int64_t tv_nsec; } timespec64_linux;

long futex_syscall(int time64, uint32_t *uaddr, int op, uint32_t val,
                   const void *timeout, uint32_t *uaddr2, uint32_t val3) {
    if (!futex_ready) {
        l_fatal("futex(%p, op=%d) called before futex_init()", uaddr, op);
        errno = ENOSYS;
        return -1;
    }

    int cmd = op & FUTEX_CMD_MASK;
    int ret;

    switch (cmd) {
        case FUTEX_WAIT:
        case FUTEX_WAIT_BITSET: {
            if (cmd == FUTEX_WAIT_BITSET && val3 == 0) {
                errno = EINVAL;
                return -1;
            }
            int64_t timeout_us = -1;
            if (timeout) {
                int64_t sec, nsec;
                if (time64) {
                    sec = ((const timespec64_linux *)timeout)->tv_sec;
                    nsec = ((const timespec64_linux *)timeout)->tv_nsec;
                } else {
                    sec = ((const timespec32_bionic *)timeout)->tv_sec;
                    nsec = ((const timespec32_bionic *)timeout)->tv_nsec;
                }
                int64_t t_us = sec * 1000000LL + nsec / 1000;
                if (cmd == FUTEX_WAIT) {
                    timeout_us = t_us; // relative
                } else if (op & FUTEX_CLOCK_REALTIME) {
                    timeout_us = t_us - now_realtime_us(); // absolute, CLOCK_REALTIME
                } else {
                    timeout_us = t_us - now_monotonic_us(); // absolute, CLOCK_MONOTONIC
                }
                if (timeout_us < 0)
                    timeout_us = 0;
            }
            ret = futex_wait_us((volatile uint32_t *)uaddr, val, timeout_us);
            break;
        }
        case FUTEX_WAKE:
        case FUTEX_WAKE_BITSET:
            // Bitsets other than MATCH_ANY are not used by Rust std; treat as "any".
            ret = futex_wake_n((volatile uint32_t *)uaddr, (int)val);
            break;
        default:
            l_error("futex(%p, op=0x%x): operation %d not emulated, returning ENOSYS",
                    uaddr, op, cmd);
            ret = -ENOSYS;
            break;
    }

    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}
