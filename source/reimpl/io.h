/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  io.h
 * @brief Wrappers and implementations for some of the IO functions.
 */

#ifndef SOLOADER_IO_H
#define SOLOADER_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <sys/dirent.h>
#include <sys/syslimits.h>
#include <sys/fcntl.h>
#include <stdint.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#endif

/*
 * bionic's 32-bit ARM `struct stat` (== `struct stat64`, libc/include/sys/stat.h)
 * is NOT packed: under ARM EABI its long long fields are 8-byte aligned, so
 * st_size is at offset 48 and sizeof is 104. Declaring it packed gives the i386
 * layout (st_size at 44), so the .so read garbage sizes. newlib's nlink_t and
 * uid_t are 16-bit, bionic's are 32-bit. Same for struct dirent (d_ino is
 * 64-bit, d_reclen 16-bit). The offsets are checked at compile time below.
 */
typedef struct stat64_bionic {
    unsigned long long st_dev;
    unsigned char __pad0[4];
    unsigned long __st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned long long st_rdev;
    unsigned char __pad3[4];
    long long st_size;
    unsigned long st_blksize;
    unsigned long long st_blocks;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    unsigned long long st_ino;
} stat64_bionic;

typedef struct dirent64_bionic {
    uint64_t d_ino;
    int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[256];
} dirent64_bionic;

_Static_assert(sizeof(struct timespec) == 8, "newlib timespec must match bionic arm32");
_Static_assert(__builtin_offsetof(stat64_bionic, st_mode) == 16, "bionic stat layout");
_Static_assert(__builtin_offsetof(stat64_bionic, st_rdev) == 32, "bionic stat layout");
_Static_assert(__builtin_offsetof(stat64_bionic, st_size) == 48, "bionic stat layout");
_Static_assert(__builtin_offsetof(stat64_bionic, st_blksize) == 56, "bionic stat layout");
_Static_assert(__builtin_offsetof(stat64_bionic, st_blocks) == 64, "bionic stat layout");
_Static_assert(__builtin_offsetof(stat64_bionic, st_atim) == 72, "bionic stat layout");
_Static_assert(__builtin_offsetof(stat64_bionic, st_ino) == 96, "bionic stat layout");
_Static_assert(sizeof(stat64_bionic) == 104, "bionic stat layout");
_Static_assert(__builtin_offsetof(dirent64_bionic, d_reclen) == 16, "bionic dirent layout");
_Static_assert(__builtin_offsetof(dirent64_bionic, d_type) == 18, "bionic dirent layout");
_Static_assert(__builtin_offsetof(dirent64_bionic, d_name) == 19, "bionic dirent layout");
_Static_assert(sizeof(dirent64_bionic) == 280, "bionic dirent layout");

int open_soloader(const char * path, int oflag, ...);

FILE * fopen_soloader(const char * filename, const char * mode);

DIR *opendir_soloader(char *name);

int stat_soloader(const char * path, stat64_bionic * buf);

int fstat_soloader(int fd, stat64_bionic * buf);

struct dirent64_bionic * readdir_soloader(DIR *dir);

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result);

int close_soloader(int fd);

int fclose_soloader(FILE *f);

int closedir_soloader(DIR *dir);

int fcntl_soloader(int fd, int cmd, ...);

int ioctl_soloader(int fd, int request, ... /* arg */);

int fsync_soloader(int fd);

int64_t lseek64_soloader(int fd, int64_t offset, int whence);

// bionic stdin/stdout/stderr (__sF) and the stdio calls that must recognise them
#include <stdarg.h>
extern uint8_t sF_bionic[3][84];
int vfprintf_soloader(FILE *f, const char *fmt, va_list ap);
int fprintf_soloader(FILE *f, const char *fmt, ...);
int fputc_soloader(int c, FILE *f);
size_t fwrite_soloader(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fflush_soloader(FILE *f);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_IO_H
