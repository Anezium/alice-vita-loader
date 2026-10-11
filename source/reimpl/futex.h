/*
 * Linux futex(2) emulation for .so files built for Android.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_FUTEX_H
#define SOLOADER_FUTEX_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create the bucket locks. Must run before the .so can take a std lock. */
int futex_init(void);

/**
 * Emulated futex syscall. `time64` selects the timespec layout (0: 32-bit
 * bionic timespec of SYS_futex, 1: 64-bit timespec of SYS_futex_time64).
 * Returns >= 0 on success, -1 with (newlib) errno set on failure.
 */
long futex_syscall(int time64, uint32_t *uaddr, int op, uint32_t val,
                   const void *timeout, uint32_t *uaddr2, uint32_t val3);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_FUTEX_H
