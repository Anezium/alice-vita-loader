/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/sys.h"
#include "reimpl/futex.h"

#include <psp2/kernel/rng.h>

#include <sys/errno.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <string.h>
#include <stdint.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/rtc.h>
#include <stdlib.h>

#include "utils/utils.h"
#include "utils/logger.h"

#define BIONIC_CLOCK_REALTIME           0
#define BIONIC_CLOCK_MONOTONIC          1
#define BIONIC_CLOCK_PROCESS_CPUTIME_ID 2
#define BIONIC_CLOCK_THREAD_CPUTIME_ID  3
#define BIONIC_CLOCK_MONOTONIC_RAW      4
#define BIONIC_CLOCK_REALTIME_COARSE    5
#define BIONIC_CLOCK_MONOTONIC_COARSE   6
#define BIONIC_CLOCK_BOOTTIME           7
#define BIONIC_CLOCK_REALTIME_ALARM     8
#define BIONIC_CLOCK_BOOTTIME_ALARM     9
#define BIONIC_CLOCK_SGI_CYCLE         10
#define BIONIC_CLOCK_TAI               11

// 1969 years in microseconds, used to adjust SCE tick to UNIX timestamp
#define __epoch 62135587294000000

int clock_gettime_soloader(clockid_t clock_id, struct timespec * tp) {
    switch (clock_id) {
        case BIONIC_CLOCK_MONOTONIC:
        case BIONIC_CLOCK_MONOTONIC_RAW:
        case BIONIC_CLOCK_MONOTONIC_COARSE:
        case BIONIC_CLOCK_BOOTTIME:
        case BIONIC_CLOCK_BOOTTIME_ALARM:
        case BIONIC_CLOCK_SGI_CYCLE:
        case BIONIC_CLOCK_PROCESS_CPUTIME_ID:
        case BIONIC_CLOCK_THREAD_CPUTIME_ID: {
            uint64_t proctime = sceKernelGetProcessTimeWide();

            tp->tv_sec = (proctime / 1000000);
            tp->tv_nsec = ((proctime - (tp->tv_sec * 1000000)) * 1000);
            break;
        }
        case BIONIC_CLOCK_REALTIME:
        case BIONIC_CLOCK_REALTIME_COARSE:
        case BIONIC_CLOCK_REALTIME_ALARM:
        case BIONIC_CLOCK_TAI: {
            SceRtcTick tick;
            sceRtcGetCurrentTick(&tick);
            tick.tick -= __epoch;

            tp->tv_sec = (tick.tick / 1000000);
            tp->tv_nsec = ((tick.tick - (tp->tv_sec * 1000000)) * 1000);
            break;
        }
        default:
            l_error("clock_gettime / unexpected clock id %i", clock_id);
    }

    return 0;
}

int clock_getres_soloader(clockid_t clock_id, struct timespec * res) {
    res->tv_sec = 0;
    res->tv_nsec = 1000;
    return 0;
}

clock_t clock_soloader(void) {
    return sceKernelGetProcessTimeLow();
}

int sigaction(int signum, const struct sigaction * act, struct sigaction * oldact) {
    l_warn("sigaction(%i, ...): not implemented", signum);
    return 0;
}

int __system_property_get_soloader(const char *name, char *value) {
    l_warn("__system_property_get(%s, %p): not implemented", name, value);
    strncpy(value, "psvita", 7);
    return 7;
}

void assert2(const char* f, int l, const char* func, const char* msg) {
    l_fatal("[%s:%i][%s] Assertion failed: %s", f, l, func, msg);
}

// ARM EABI syscall numbers that Rust std issues through syscall().
#define SYS_GETTID_ARM          224
#define SYS_FUTEX_ARM           240
#define SYS_SPLICE_ARM          340
#define SYS_GETRANDOM_ARM       384
#define SYS_COPY_FILE_RANGE_ARM 391
#define SYS_FUTEX_TIME64_ARM    422

// syscall(long number, ...): ARM AAPCS passes variadic integer arguments
// exactly like fixed ones (r0-r3, then stack), so a fixed 7-argument
// definition reads them correctly; extra slots are just unused stack words.
long syscall_soloader(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    switch (n) {
        case SYS_FUTEX_ARM:
            return futex_syscall(0, (uint32_t *)a1, (int)a2, (uint32_t)a3,
                                 (const void *)a4, (uint32_t *)a5, (uint32_t)a6);
        case SYS_FUTEX_TIME64_ARM:
            return futex_syscall(1, (uint32_t *)a1, (int)a2, (uint32_t)a3,
                                 (const void *)a4, (uint32_t *)a5, (uint32_t)a6);
        case SYS_GETRANDOM_ARM:
            return getrandom_soloader((void *)a1, (size_t)a2, (unsigned int)a3);
        case SYS_GETTID_ARM:
            return gettid_soloader();
        case SYS_COPY_FILE_RANGE_ARM:
        case SYS_SPLICE_ARM:
            // std::io::copy probes these and falls back to read/write on ENOSYS.
            l_stub_once("syscall(%ld) (copy_file_range/splice): ENOSYS, std falls back to read/write", n);
            errno = ENOSYS;
            return -1;
        default:
            l_stub("syscall(%ld, 0x%lx, 0x%lx, 0x%lx, ...) from %p: not emulated, ENOSYS",
                   n, a1, a2, a3, __builtin_return_address(0));
            errno = ENOSYS;
            return -1;
    }
}

int gettid_soloader(void) {
    return sceKernelGetThreadId();
}

// getrandom(2) (bionic API 28, weak import used by Rust std's HashMap seed).
ssize_t getrandom_soloader(void *buf, size_t buflen, unsigned int flags) {
    uint8_t *p = (uint8_t *)buf;
    size_t left = buflen;
    (void)flags; // GRND_NONBLOCK / GRND_RANDOM: the Vita RNG never blocks
    while (left > 0) {
        // sceKernelGetRandomNumber accepts at most 64 bytes per call.
        size_t chunk = left > 64 ? 64 : left;
        int ret = sceKernelGetRandomNumber(p, chunk);
        if (ret < 0) {
            l_error("getrandom: sceKernelGetRandomNumber(%u) failed: 0x%08x", (unsigned)chunk, ret);
            if (left == buflen) {
                errno = EIO;
                return -1;
            }
            break;
        }
        p += chunk;
        left -= chunk;
    }
    return (ssize_t)(buflen - left);
}

// sysconf(_SC_NPROCESSORS_ONLN/CONF). The Vita has 4 Cortex-A9 cores but only
// 3 are available to applications (core 3 belongs to the system).
#ifndef LOADER_NPROCESSORS
#  define LOADER_NPROCESSORS 3
#endif

// Bionic sysconf() names (libc/include/bits/sysconf.h).
#define BIONIC_SC_ARG_MAX            0x0000
#define BIONIC_SC_CLK_TCK            0x0006
#define BIONIC_SC_OPEN_MAX           0x000b
#define BIONIC_SC_IOV_MAX            0x0026
#define BIONIC_SC_PAGESIZE           0x0027
#define BIONIC_SC_PAGE_SIZE          0x0028
#define BIONIC_SC_GETGR_R_SIZE_MAX   0x0047
#define BIONIC_SC_GETPW_R_SIZE_MAX   0x0048
#define BIONIC_SC_THREAD_STACK_MIN   0x004c
#define BIONIC_SC_NPROCESSORS_CONF   0x0060
#define BIONIC_SC_NPROCESSORS_ONLN   0x0061
#define BIONIC_SC_PHYS_PAGES         0x0062
#define BIONIC_SC_AVPHYS_PAGES       0x0063

long sysconf_soloader(int name) {
    switch (name) {
        case BIONIC_SC_PAGESIZE:
        case BIONIC_SC_PAGE_SIZE:
            return PAGE_SIZE;
        case BIONIC_SC_NPROCESSORS_CONF:
        case BIONIC_SC_NPROCESSORS_ONLN:
            return LOADER_NPROCESSORS;
        case BIONIC_SC_PHYS_PAGES:
            // 512 MiB of LPDDR2, of which ~365 MiB are usable by applications.
            return (512L * 1024 * 1024) / PAGE_SIZE;
        case BIONIC_SC_AVPHYS_PAGES:
            return (256L * 1024 * 1024) / PAGE_SIZE;
        case BIONIC_SC_CLK_TCK:
            return 100;
        case BIONIC_SC_OPEN_MAX:
            return 1024;
        case BIONIC_SC_IOV_MAX:
            return 1024;
        case BIONIC_SC_ARG_MAX:
            return 128 * 1024;
        case BIONIC_SC_THREAD_STACK_MIN:
            return 16 * 1024;
        case BIONIC_SC_GETGR_R_SIZE_MAX:
        case BIONIC_SC_GETPW_R_SIZE_MAX:
            return 1024;
        default:
            l_stub("sysconf(0x%x) from %p: unknown name, returning -1/EINVAL",
                   name, __builtin_return_address(0));
            errno = EINVAL;
            return -1;
    }
}

void __stack_chk_fail_soloader() {
    l_fatal("Stack collapsed at address %p", __builtin_return_address(0));
}

void abort_soloader() {
    l_fatal("Abort called from address %p", __builtin_return_address(0));
    abort();
}

void exit_soloader(int status) {
    l_fatal("Exit(%i) called from %p", status, __builtin_return_address(0));
    exit(status);
}

int __atomic_dec(volatile int *ptr) {
    return __sync_fetch_and_sub(ptr, 1);
}

int __atomic_inc(volatile int *ptr) {
    return __sync_fetch_and_add(ptr, 1);
}

int __atomic_swap(int new_value, volatile int *ptr) {
    int old_value;
    do {
        old_value = *ptr;
    } while (__sync_val_compare_and_swap(ptr, old_value, new_value) != old_value);
    return old_value;
}

int __atomic_cmpxchg(int old_value, int new_value, volatile int* ptr) {
    /* We must return 0 on success */
    return __sync_val_compare_and_swap(ptr, old_value, new_value) != old_value;
}

char * getenv_soloader(const char * var) {
    l_warn("getenv(\"%s\"): not implemented.", var);
    return NULL;
}

int setenv_soloader(const char * name, const char * value, int overwrite) {
    l_warn("setenv(\"%s\", \"%s\"): not implemented.", name, value);
    return 0;
}

int getpagesize(void) {
    return PAGE_SIZE;
}
