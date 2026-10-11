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
#include "config.h"

#include <stdarg.h>
#include <stdio.h>

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

// Android system properties actually read by the .so (oboe getSdkVersion(),
// isAtLeastPreReleaseCodename(); Rust std reads none). Unknown names behave
// like bionic: empty value, length 0, and are logged.
static const struct { const char *name; const char *value; } system_properties[] = {
    { "ro.build.version.sdk",          LOADER_STR(LOADER_ANDROID_SDK) },
    { "ro.build.version.release",      "8.0.0" },
    { "ro.build.version.codename",     "REL" },
    { "ro.product.manufacturer",       "Sony" },
    { "ro.product.brand",              "Sony" },
    { "ro.product.model",              "PS Vita" },
    { "ro.product.device",             "psvita" },
    { "ro.hardware",                   "psvita" },
    { "ro.product.cpu.abi",            "armeabi-v7a" },
    { "ro.arch",                       "arm" },
};

int __system_property_get_soloader(const char *name, char *value) {
    if (!name || !value)
        return 0;
    for (size_t i = 0; i < sizeof(system_properties) / sizeof(system_properties[0]); i++) {
        if (strcmp(name, system_properties[i].name) == 0) {
            strncpy(value, system_properties[i].value, 92 /* PROP_VALUE_MAX */);
            value[91] = 0;
            l_notice("__system_property_get(\"%s\") = \"%s\"", name, value);
            return (int)strlen(value);
        }
    }
    l_stub("__system_property_get(\"%s\"): unknown property, returning \"\"", name);
    value[0] = 0;
    return 0;
}

void assert2(const char* f, int l, const char* func, const char* msg) {
    l_fatal("[%s:%i][%s] Assertion failed: %s", f, l, func, msg);
    // bionic __assert2 is noreturn; returning into the .so would continue on
    // a broken invariant.
    abort();
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

#define BIONIC_PR_SET_NAME 15
#define BIONIC_PR_GET_NAME 16

int prctl_soloader(int option, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    switch (option) {
        case BIONIC_PR_SET_NAME:
            l_notice("prctl(PR_SET_NAME, \"%.16s\") on thread 0x%x", (const char *)a2, sceKernelGetThreadId());
            return 0;
        case BIONIC_PR_GET_NAME:
            if (a2) strcpy((char *)a2, "alice");
            return 0;
        default:
            l_stub("prctl(%d, 0x%lx, ...) from %p: not supported, EINVAL",
                   option, a2, __builtin_return_address(0));
            errno = EINVAL;
            return -1;
    }
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

#define BIONIC_TIMER_ABSTIME 1

int clock_nanosleep_soloader(clockid_t clock_id, int flags,
                             const struct timespec *req, struct timespec *rem) {
    if (!req || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L)
        return 22; /* EINVAL (bionic), returned not set in errno */
    int64_t us = (int64_t)req->tv_sec * 1000000LL + req->tv_nsec / 1000;
    if (flags & BIONIC_TIMER_ABSTIME) {
        struct timespec now;
        clock_gettime_soloader(clock_id, &now);
        us -= (int64_t)now.tv_sec * 1000000LL + now.tv_nsec / 1000;
    }
    if (us > 0) {
        while (us > 0x7fffffff) {
            sceKernelDelayThread(0x7fffffff);
            us -= 0x7fffffff;
        }
        sceKernelDelayThread((SceUInt)us);
    }
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0; // clock_nanosleep returns an error number, never sets errno
}

int __register_atfork_soloader(void (*prepare)(void), void (*parent)(void),
                               void (*child)(void), void *dso) {
    // fork() does not exist on the Vita, so the handlers can never run.
    l_stub_once("__register_atfork(%p, %p, %p): accepted, fork() never happens on Vita",
                prepare, parent, child);
    return 0;
}

void android_set_abort_message_soloader(const char *msg) {
    l_fatal("android_set_abort_message: %s", msg ? msg : "(null)");
}

void openlog_soloader(const char *ident, int option, int facility) {
    l_notice("openlog(\"%s\", 0x%x, 0x%x): syslog goes to the loader log", ident ? ident : "", option, facility);
}

void closelog_soloader(void) {
    l_notice("closelog()");
}

void syslog_soloader(int priority, const char *fmt, ...) {
    char text[1024];
    va_list list;
    va_start(list, fmt);
    vsnprintf(text, sizeof(text), fmt, list);
    va_end(list);
    l_error("[syslog prio %d] %s", priority, text);
}

ssize_t sendfile_soloader(int out_fd, int in_fd, void *offset, size_t count) {
    // Strong import (std::io::copy fast path); ENOSYS makes std fall back to read/write.
    l_stub_once("sendfile(%d, %d, %u): ENOSYS, std falls back to read/write", out_fd, in_fd, (unsigned)count);
    errno = ENOSYS;
    return -1;
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

/*
 * Environment. The boilerplate getenv/setenv were stubs. Looking Glass reads
 * LOOKING_GLASS_SETTINGS_DIR / LOOKING_GLASS_DATA to find its writable root and
 * PK3 directory (fork src/android.rs), and Rust std reads RUST_MIN_STACK and
 * TMPDIR. A small private table, guarded by a lightweight mutex.
 */
#define ENV_MAX 64
static struct { char *name; char *value; } env_table[ENV_MAX];
static SceKernelLwMutexWork env_mutex;
static int env_ready = 0;

static int env_find(const char *name) {
    for (int i = 0; i < ENV_MAX; i++)
        if (env_table[i].name && strcmp(env_table[i].name, name) == 0)
            return i;
    return -1;
}

static void env_set_locked(const char *name, const char *value) {
    int i = env_find(name);
    if (i < 0) {
        for (i = 0; i < ENV_MAX && env_table[i].name; i++);
        if (i == ENV_MAX) {
            l_error("setenv(\"%s\"): environment table full (%d entries)", name, ENV_MAX);
            return;
        }
        env_table[i].name = strdup(name);
    }
    // Old values are leaked on purpose: getenv() results must stay valid.
    env_table[i].value = strdup(value);
}

void env_init(void) {
    sceKernelCreateLwMutex(&env_mutex, "env_lock", 0, 0, NULL);
    env_ready = 1;
    env_set_locked("LOOKING_GLASS_SETTINGS_DIR", LOADER_STORAGE_DIR);
    env_set_locked("LOOKING_GLASS_ANDROID_STORAGE", LOADER_STORAGE_DIR);
    env_set_locked("LOOKING_GLASS_DATA", LOADER_PK3_DIR);
    env_set_locked("TMPDIR", LOADER_STORAGE_DIR "/tmp");
    env_set_locked("HOME", LOADER_STORAGE_DIR);
    env_set_locked("RUST_MIN_STACK", LOADER_RUST_MIN_STACK);
    for (int i = 0; i < ENV_MAX && env_table[i].name; i++)
        l_notice("env: %s=%s", env_table[i].name, env_table[i].value);
}

char * getenv_soloader(const char * var) {
    if (!var || !env_ready) return NULL;
    sceKernelLockLwMutex(&env_mutex, 1, NULL);
    int i = env_find(var);
    char *ret = i >= 0 ? env_table[i].value : NULL;
    sceKernelUnlockLwMutex(&env_mutex, 1);
    l_debug("getenv(\"%s\") = %s", var, ret ? ret : "(null)");
    return ret;
}

int setenv_soloader(const char * name, const char * value, int overwrite) {
    if (!name || !*name || strchr(name, '=') || !value) {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(name, "RUST_MIN_STACK") == 0) {
        // The fork asks 16 MiB for every std thread; keep the loader value.
        l_notice("setenv(\"RUST_MIN_STACK\", \"%s\") ignored, keeping %s (config.h)",
                 value, LOADER_RUST_MIN_STACK);
        return 0;
    }
    sceKernelLockLwMutex(&env_mutex, 1, NULL);
    if (overwrite || env_find(name) < 0)
        env_set_locked(name, value);
    sceKernelUnlockLwMutex(&env_mutex, 1);
    l_notice("setenv(\"%s\", \"%s\", %d)", name, value, overwrite);
    return 0;
}

int unsetenv_soloader(const char * name) {
    if (!name) { errno = EINVAL; return -1; }
    sceKernelLockLwMutex(&env_mutex, 1, NULL);
    int i = env_find(name);
    if (i >= 0) { env_table[i].name = NULL; env_table[i].value = NULL; }
    sceKernelUnlockLwMutex(&env_mutex, 1);
    return 0;
}

int getpagesize(void) {
    return PAGE_SIZE;
}
