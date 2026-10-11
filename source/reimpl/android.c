/*
 * Android NDK / bionic-dynamic-linker pieces needed by liblooking_glass.so.
 *
 * New file (not in soloader-boilerplate). Part of spike-c/loader, derived from
 * soloader-boilerplate rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT licence.
 *
 *   - ANativeWindow_fromSurface / _release / _setBuffersGeometry: miniquad turns
 *     the Surface jobject received in surfaceOnSurfaceChanged into a window and
 *     hands it to eglCreateWindowSurface. One fake window is enough.
 *   - dl_iterate_phdr: used by the embedded libunwind (C++ exceptions in oboe)
 *     and by Rust backtraces after a panic. Served from the module so_util
 *     loaded (dl_unwind_find_exidx lives in dynlib.c).
 *   - slCreateEngine for oboe (looked up with dlsym in "libOpenSLES.so"):
 *     reports SL_RESULT_FEATURE_UNSUPPORTED unless LOADER_AUDIO_ENABLED.
 */

#include "reimpl/android.h"

#include <string.h>
#include <stdint.h>
#include <psp2/kernel/threadmgr.h>

#include <so_util/so_util.h>
#include <SLES/OpenSLES.h>

#include "utils/logger.h"
#include "config.h"

extern so_module so_mod;

/* ------------------------------------------------------------ ANativeWindow */

typedef struct {
    uint32_t magic;
    volatile int refs;
    int32_t width, height, format;
} alice_native_window;

static alice_native_window the_window = {
    .magic = 0x414E5701, .refs = 0,
    .width = LOADER_SCREEN_W, .height = LOADER_SCREEN_H, .format = 1,
};

void *ANativeWindow_fromSurface_soloader(void *env, void *surface) {
    int refs = __sync_add_and_fetch(&the_window.refs, 1);
    l_notice("ANativeWindow_fromSurface(surface=%p) -> %p (refs %d)", surface, &the_window, refs);
    return &the_window;
}

void ANativeWindow_acquire_soloader(void *window) {
    __sync_add_and_fetch(&the_window.refs, 1);
}

void ANativeWindow_release_soloader(void *window) {
    if (window != &the_window) {
        l_error("ANativeWindow_release(%p): unknown window", window);
        return;
    }
    int refs = __sync_sub_and_fetch(&the_window.refs, 1);
    l_debug("ANativeWindow_release -> refs %d", refs);
}

int32_t ANativeWindow_setBuffersGeometry_soloader(void *window, int32_t width, int32_t height, int32_t format) {
    // The Vita framebuffer is fixed (vitaGL, 960x544); remember what was asked.
    l_notice("ANativeWindow_setBuffersGeometry(%p, %d, %d, format %d)%s", window, width, height, format,
             (width && (width != LOADER_SCREEN_W || height != LOADER_SCREEN_H)) ? " (differs from 960x544, ignored)" : "");
    if (width) the_window.width = width;
    if (height) the_window.height = height;
    if (format) the_window.format = format;
    return 0;
}

int32_t ANativeWindow_getWidth_soloader(void *window) { return LOADER_SCREEN_W; }
int32_t ANativeWindow_getHeight_soloader(void *window) { return LOADER_SCREEN_H; }

/* --------------------------------------------------- dynamic linker helpers */

#define PT_LOAD_ 1

typedef struct {
    uint32_t dlpi_addr;
    const char *dlpi_name;
    const Elf32_Phdr *dlpi_phdr;
    uint16_t dlpi_phnum;
    unsigned long long dlpi_adds;
    unsigned long long dlpi_subs;
    size_t dlpi_tls_modid;
    void *dlpi_tls_data;
} dl_phdr_info_bionic;

static Elf32_Phdr phdr_copy[16];
static int phdr_count = -1;

/*
 * so_util frees the file image after loading, so mod->phdr is dangling. The
 * first PT_LOAD of liblooking_glass.so maps file offset 0 at vaddr 0, so the
 * ELF header and program headers are also present at load_addr: copy them
 * and undo the load_addr bias so_util may have applied to p_vaddr.
 */
static int build_phdr_copy(void) {
    if (phdr_count >= 0)
        return phdr_count;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)so_mod.load_addr;
    if (!so_mod.load_addr || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
        eh->e_phnum == 0 || eh->e_phnum > 16) {
        l_error("dl_iterate_phdr: ELF header not mapped at load address %p", (void *)so_mod.load_addr);
        phdr_count = 0;
        return 0;
    }
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(so_mod.load_addr + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        phdr_copy[i] = ph[i];
        if (phdr_copy[i].p_vaddr >= so_mod.load_addr)
            phdr_copy[i].p_vaddr -= so_mod.load_addr;
    }
    phdr_count = eh->e_phnum;
    return phdr_count;
}

int dl_iterate_phdr_soloader(int (*callback)(void *info, size_t size, void *data), void *data) {
    int n = build_phdr_copy();
    if (n <= 0) {
        l_stub("dl_iterate_phdr: no module information available, 0 modules");
        return 0;
    }
    dl_phdr_info_bionic info;
    memset(&info, 0, sizeof(info));
    info.dlpi_addr = so_mod.load_addr;
    info.dlpi_name = SO_PATH;
    info.dlpi_phdr = phdr_copy;
    info.dlpi_phnum = (uint16_t)n;
    info.dlpi_adds = 1;
    l_debug("dl_iterate_phdr(%p): 1 module at %p", callback, (void *)so_mod.load_addr);
    return callback(&info, sizeof(info), data);
}

/* ------------------------------------------------------------- OpenSL ES */

#if !LOADER_AUDIO_ENABLED
SLresult slCreateEngine_soloader(SLObjectItf *pEngine, SLuint32 numOptions,
                                 const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
                                 const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired) {
    l_stub("slCreateEngine: audio disabled for the first boot (LOADER_AUDIO_ENABLED=0), "
           "SL_RESULT_FEATURE_UNSUPPORTED");
    if (pEngine) *pEngine = NULL;
    return SL_RESULT_FEATURE_UNSUPPORTED;
}
#else
SLresult slCreateEngine_soloader(SLObjectItf *pEngine, SLuint32 numOptions,
                                 const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
                                 const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired) {
    SLresult r = slCreateEngine(pEngine, numOptions, pEngineOptions, numInterfaces, pInterfaceIds, pInterfaceRequired);
    l_notice("slCreateEngine (VitaSDK libOpenSLES) = 0x%x", (unsigned)r);
    return r;
}
#endif
