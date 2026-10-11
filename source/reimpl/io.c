/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"
#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <psp2/kernel/clib.h>

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

/*
 * Looking Glass falls back to hard-coded Android locations when the
 * LOOKING_GLASS_* environment variables are not usable (fork src/android.rs).
 * Map the app-private ones onto the Vita data directory.
 */
static const char *android_prefixes[] = {
    "/storage/emulated/0/Android/data/com.lookingglass.alice/files",
    "/sdcard/Android/data/com.lookingglass.alice/files",
    "/data/user/0/com.lookingglass.alice/files",
    "/data/data/com.lookingglass.alice/files",
};

const char *path_android_to_vita(const char *path, char *buf, size_t buflen) {
    if (!path || path[0] != '/')
        return path;
    for (size_t i = 0; i < sizeof(android_prefixes) / sizeof(android_prefixes[0]); i++) {
        size_t n = strlen(android_prefixes[i]);
        if (strncmp(path, android_prefixes[i], n) == 0 && (path[n] == 0 || path[n] == '/')) {
            snprintf(buf, buflen, "%s%s", LOADER_STORAGE_DIR, path + n);
            l_notice("path redirect: %s -> %s", path, buf);
            return buf;
        }
    }
    return path;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    char redirected[PATH_MAX];
    filename = path_android_to_vita(filename, redirected, sizeof(redirected));

    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    char redirected[PATH_MAX];
    path = path_android_to_vita(path, redirected, sizeof(redirected));
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    } else if (strcmp(path, "/dev/urandom") == 0) {
        return open_soloader("app0:/urandom", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    char redirected[PATH_MAX];
    path = path_android_to_vita(path, redirected, sizeof(redirected));
    if (strcmp(path, "/system/lib/libOpenSLES.so") == 0) {
        l_debug("stat(%s): returning 0 in case this is a check for OpenSLES support", path);
        return 0;
    }

    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    char redirected[PATH_MAX];
    _pathname = (char *)path_android_to_vita(_pathname, redirected, sizeof(redirected));
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_stub_once("fcntl(%i, %i, ...): not implemented, returning 0 (logged once)", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_stub("ioctl(%i, 0x%x, ...): not implemented, returning 0", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}

int mkdir_soloader(const char *path, mode_t mode) {
    char redirected[PATH_MAX];
    path = path_android_to_vita(path, redirected, sizeof(redirected));
    int ret = mkdir(path, mode);
    l_debug("mkdir(%s): %i", path, ret);
    return ret;
}

/*
 * lseek64 used to be ret0, which breaks every seek done by Rust std (File::seek
 * is lseek64 on Android): zip readers seek to the end to find the central
 * directory. newlib's off_t is 32-bit on the Vita.
 */
int64_t lseek64_soloader(int fd, int64_t offset, int whence) {
    if (offset > INT32_MAX || offset < INT32_MIN) {
        l_error("lseek64(%i, %lld, %i): offset does not fit newlib's 32-bit off_t", fd, offset, whence);
        errno = EOVERFLOW;
        return -1;
    }
    off_t ret = lseek(fd, (off_t)offset, whence); // SEEK_SET/CUR/END are 0/1/2 on both sides
    if (ret < 0)
        l_debug("lseek64(%i, %lld, %i): %li", fd, offset, whence, (long)ret);
    return (int64_t)ret;
}

/* stdout/stderr of the .so (Rust eprintln!/println!, libc++abi) go to the loader log. */
static void log_std_stream(int fd, const void *buf, size_t count) {
    const char *p = (const char *)buf;
    while (count > 0) {
        int chunk = count > 1024 ? 1024 : (int)count;
        sceClibPrintf("[%s] %.*s", fd == 2 ? "stderr" : "stdout", chunk, p);
        if (p[chunk - 1] != '\n' && count <= 1024)
            sceClibPrintf("\n");
        p += chunk;
        count -= chunk;
    }
}

ssize_t write_soloader(int fd, const void *buf, size_t count) {
    if (fd == 1 || fd == 2) {
        if (count) log_std_stream(fd, buf, count);
        return (ssize_t)count;
    }
    return write(fd, buf, count);
}

typedef struct { void *iov_base; size_t iov_len; } iovec_bionic;

ssize_t writev_soloader(int fd, const void *iov_, int iovcnt) {
    const iovec_bionic *iov = (const iovec_bionic *)iov_;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_len) continue;
        ssize_t r = write_soloader(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

ssize_t readv_soloader(int fd, const void *iov_, int iovcnt) {
    const iovec_bionic *iov = (const iovec_bionic *)iov_;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_len) continue;
        ssize_t r = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : r;
        total += r;
        if ((size_t)r < iov[i].iov_len) break; // short read: stop like the kernel would
    }
    return total;
}

ssize_t readlink_soloader(const char *path, char *buf, size_t bufsiz) {
    // std::env::current_exe() reads /proc/self/exe: answer with the .so path.
    if (path && (strcmp(path, "/proc/self/exe") == 0)) {
        size_t n = strlen(SO_PATH);
        if (n > bufsiz) n = bufsiz;
        memcpy(buf, SO_PATH, n); // readlink does not NUL-terminate
        l_notice("readlink(/proc/self/exe) -> %s", SO_PATH);
        return (ssize_t)n;
    }
    l_stub("readlink(\"%s\"): no symlinks on the Vita, EINVAL", path ? path : "(null)");
    errno = EINVAL;
    return -1;
}

int dirfd_soloader(DIR *dir) {
    int fd = dirfd(dir);
    if (fd < 0)
        l_stub("dirfd(%p): newlib returned %d (no fd behind Vita DIR handles)", dir, fd);
    return fd;
}

int fchmod_soloader(int fd, mode_t mode) {
    // No POSIX permissions on Vita file systems: report success.
    l_stub_once("fchmod(%d, 0%o): no permissions on Vita, returning 0 (logged once)", fd, (unsigned)mode);
    return 0;
}

/*
 * bionic's stdin/stdout/stderr are &__sF[0..2], with sizeof(FILE) == 84 on
 * 32-bit bionic. Exporting an array of newlib FILEs meant that &__sF[2]
 * computed by the .so did not point at the third element (and with
 * USE_SCELIBC_IO a newlib FILE* was handed to SceLibc). Export a bionic-sized
 * array and recognise its three slots in the stdio wrappers.
 */
#define BIONIC_FILE_SIZE 84
uint8_t sF_bionic[3][BIONIC_FILE_SIZE] __attribute__((aligned(8)));

static int bionic_std_stream(const void *f) {
    for (int i = 0; i < 3; i++)
        if (f == sF_bionic[i]) return i;
    return -1;
}

int vfprintf_soloader(FILE *f, const char *fmt, va_list ap) {
    int s = bionic_std_stream(f);
    if (s >= 0) {
        char text[1024];
        int n = vsnprintf(text, sizeof(text), fmt, ap);
        if (s == 0) return -1;
        log_std_stream(s, text, n < (int)sizeof(text) ? (size_t)(n > 0 ? n : 0) : sizeof(text) - 1);
        return n;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_vfprintf(f, fmt, ap);
#else
    return vfprintf(f, fmt, ap);
#endif
}

int fprintf_soloader(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf_soloader(f, fmt, ap);
    va_end(ap);
    return r;
}

int fputc_soloader(int c, FILE *f) {
    int s = bionic_std_stream(f);
    if (s >= 0) {
        char ch = (char)c;
        if (s) log_std_stream(s, &ch, 1);
        return (unsigned char)c;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fputc(c, f);
#else
    return fputc(c, f);
#endif
}

size_t fwrite_soloader(const void *ptr, size_t size, size_t nmemb, FILE *f) {
    int s = bionic_std_stream(f);
    if (s >= 0) {
        if (s) log_std_stream(s, ptr, size * nmemb);
        return nmemb;
    }
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fwrite(ptr, size, nmemb, f);
#else
    return fwrite(ptr, size, nmemb, f);
#endif
}

int fflush_soloader(FILE *f) {
    if (!f || bionic_std_stream(f) >= 0)
        return 0;
#ifdef USE_SCELIBC_IO
    return sceLibcBridge_fflush(f);
#else
    return fflush(f);
#endif
}
