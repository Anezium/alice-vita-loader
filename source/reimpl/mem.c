/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/mem.h"
#include "utils/logger.h"

#include <string.h>
#include <malloc.h>
#include <psp2/kernel/clib.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

void *sceClibMemclr(void *dst, size_t len) {
    return sceClibMemset(dst, 0, len);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offs) {
    // Anonymous mappings only (Rust backtrace/gimli after a panic). A file
    // mapping cannot be emulated by zeroed memory: refuse it loudly.
    if (length <= 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    if (fd >= 0) {
        l_stub("mmap(%p, %u, prot=%i, flags=0x%x, fd=%i, off=%li) from %p: file mappings "
               "unsupported, MAP_FAILED/ENODEV", addr, (unsigned)length, prot, flags, fd, offs,
               __builtin_return_address(0));
        errno = ENODEV;
        return MAP_FAILED;
    }
    void *ret = memalign(4096, length);
    if (!ret) {
        l_error("mmap(anonymous, %u): out of memory", (unsigned)length);
        errno = ENOMEM;
        return MAP_FAILED;
    }
    memset(ret, 0, length);
    l_notice("mmap(anonymous, %u) = %p (heap-backed)", (unsigned)length, ret);
    return ret;
}

int munmap(void *addr, size_t length) {
    if (addr) free(addr);
    return 0;
}

/*
 * Rust's System allocator (__rdl_alloc / __rdl_realloc / __rdl_alloc_zeroed)
 * calls posix_memalign for every alignment above 8. newlib has memalign(),
 * whose blocks are released by the regular free().
 * posix_memalign returns an error number (bionic values), it does not set errno.
 */
#define BIONIC_EINVAL 22
#define BIONIC_ENOMEM 12

static inline int is_pow2(size_t x) { return x && !(x & (x - 1)); }

int posix_memalign_soloader(void **memptr, size_t alignment, size_t size) {
    if (!memptr || !is_pow2(alignment) || (alignment % sizeof(void *)) != 0)
        return BIONIC_EINVAL;
    void *p = memalign(alignment, size ? size : 1);
    if (!p) {
        l_error("posix_memalign(align=%u, size=%u): out of memory", (unsigned)alignment, (unsigned)size);
        return BIONIC_ENOMEM;
    }
    *memptr = p;
    return 0;
}

void *aligned_alloc_soloader(size_t alignment, size_t size) {
    if (!is_pow2(alignment)) {
        errno = EINVAL;
        return NULL;
    }
    if (alignment < sizeof(void *))
        alignment = sizeof(void *);
    void *p = memalign(alignment, size ? size : 1);
    if (!p)
        errno = ENOMEM;
    return p;
}

void *memalign_soloader(size_t alignment, size_t size) {
    if (!is_pow2(alignment))
        alignment = sizeof(void *); // bionic rounds bad alignments, newlib would misbehave
    return memalign(alignment, size);
}
