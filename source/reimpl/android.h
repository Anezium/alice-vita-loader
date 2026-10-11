/*
 * Android NDK / dynamic-linker helpers for the Alice Vita loader.
 * New file, part of spike-c/loader (derived from soloader-boilerplate
 * rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT licence).
 */

#ifndef ALICE_LOADER_ANDROID_H
#define ALICE_LOADER_ANDROID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *ANativeWindow_fromSurface_soloader(void *env, void *surface);
void ANativeWindow_acquire_soloader(void *window);
void ANativeWindow_release_soloader(void *window);
int32_t ANativeWindow_setBuffersGeometry_soloader(void *window, int32_t width, int32_t height, int32_t format);
int32_t ANativeWindow_getWidth_soloader(void *window);
int32_t ANativeWindow_getHeight_soloader(void *window);

int dl_iterate_phdr_soloader(int (*callback)(void *info, size_t size, void *data), void *data);

#ifdef __cplusplus
};
#endif

#endif // ALICE_LOADER_ANDROID_H
