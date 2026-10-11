/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <reimpl/controls.h>
#include "reimpl/futex.h"
#include "reimpl/sys.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base address for the Android .so to be loaded at
#define LOAD_ADDRESS 0x98000000

extern so_module so_mod;

void soloader_init_all() {
    _log_init();
    l_notice("Alice Vita loader (so-loader for liblooking_glass.so), built " __DATE__ " " __TIME__);
    l_notice("config: SO_PATH=%s heap=%u MiB stack cap=%u KiB default=%u KiB ui=%u KiB "
             "vgl_threshold=%u MiB msaa=%d sdk=%d audio=%d cpus=%d",
             SO_PATH, (unsigned)(LOADER_NEWLIB_HEAP_SIZE >> 20),
             (unsigned)(LOADER_THREAD_STACK_CAP >> 10), (unsigned)(LOADER_THREAD_STACK_DEFAULT >> 10),
             (unsigned)(LOADER_MAIN_STACK_SIZE >> 10), (unsigned)(LOADER_VGL_RAM_THRESHOLD >> 20),
             LOADER_VGL_MSAA, LOADER_ANDROID_SDK, LOADER_AUDIO_ENABLED, LOADER_NPROCESSORS);

    // Rust std takes futex-based locks as soon as it runs; the environment
    // must exist before Looking Glass reads LOOKING_GLASS_* variables.
    if (futex_init() < 0)
        fatal_error("Error: could not create the futex emulation objects.");
    env_init();

	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_notice("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    // so_util calls kuKernelMemProtect (NID 0x566d2af1). Calling the unresolved
    // import crashes, so this only reads the installed skprx. The check is the
    // SHA1 gate from v-atamanenko/mc3-vita source/utils/init.c, limited to the
    // release whose export table does not contain that NID: TheOfficialFloW
    // v0.1, SHA1 E033D76A90C9B8F2D496735C2692AFD8C3ED32FE. bythos14 v0.2, v0.3
    // and v0.3.1 hotfix do export it (module version stays 0x0100 on all four),
    // so their hashes are not rejected here.
    {
        const char *kubridge_path = NULL;
        if (file_exists("ux0:/tai/kubridge.skprx"))
            kubridge_path = "ux0:/tai/kubridge.skprx";
        else if (file_exists("ur0:/tai/kubridge.skprx"))
            kubridge_path = "ur0:/tai/kubridge.skprx";

        char *kubridge_hash = kubridge_path ? file_sha1sum(kubridge_path) : NULL;
        if (!kubridge_hash) {
            l_fatal("kubridge.skprx not found in ux0:/tai or ur0:/tai.");
            fatal_error("Error: kubridge.skprx is not in ux0:/tai or ur0:/tai.");
        }
        l_notice("kubridge hash: %s (%s)", kubridge_hash, kubridge_path);
        if (strcmp(kubridge_hash, "E033D76A90C9B8F2D496735C2692AFD8C3ED32FE") == 0) {
            free(kubridge_hash);
            l_fatal("kubridge too old: kuKernelMemProtect missing, install bythos14 kubridge v0.3.1 hotfix");
            fatal_error("kubridge too old: kuKernelMemProtect missing, install bythos14 kubridge v0.3.1 hotfix");
        }
        free(kubridge_hash);
    }
    l_notice("kubridge check passed.");

    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }

    if (so_file_load(&so_mod, SO_PATH, LOAD_ADDRESS) < 0) {
        l_fatal("SO could not be loaded.");
        fatal_error("Error: could not load %s.", SO_PATH);
    }

    settings_load();
    l_notice("Settings loaded.");

    so_relocate(&so_mod);
    l_notice("SO relocated.");

    resolve_imports(&so_mod);
    l_notice("SO imports resolved.");

    so_patch();
    l_notice("SO patched.");

    so_flush_caches(&so_mod);
    l_notice("SO caches flushed.");

    so_initialize(&so_mod);
    l_notice("SO initialized.");

    gl_preload();
    l_notice("OpenGL preloaded (vitaGL itself starts in eglInitialize, on the render thread).");

    jni_init();
    l_notice("FalsoJNI initialized.");

#ifndef NDK_PORT
    controls_init();
    l_notice("Controls initialized.");
#endif
}
