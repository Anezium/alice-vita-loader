/*
 * Alice (Looking Glass) Vita loader -- build-time configuration.
 *
 * New file (not in soloader-boilerplate). Part of the loader derived from
 * soloader-boilerplate rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT licence
 * (see LICENSE). Every tunable used by the Alice-specific changes lives here;
 * each value can be overridden with -D on the CMake command line
 * (e.g. cmake -DLOADER_EXTRA_DEFS="-DLOADER_THREAD_STACK_CAP=4194304").
 * The reasoning behind each default is in CHANGES.md.
 */

#ifndef ALICE_LOADER_CONFIG_H
#define ALICE_LOADER_CONFIG_H

/* ---------------------------------------------------------------- memory -- */

/* newlib heap (malloc/posix_memalign of the .so: Rust allocator, game data).
 * The rest of user memory goes to vitaGL (textures, buffers) and thread stacks.
 * With LOADER_EXTENDED_MEM (ATTRIBUTE2=12 in param.sfo, set from CMake) the
 * application gets ~109 MiB more, so the heap can be larger. */
#ifndef LOADER_NEWLIB_HEAP_SIZE
#  ifdef LOADER_EXTENDED_MEM
#    define LOADER_NEWLIB_HEAP_SIZE (256 * 1024 * 1024)
#  else
#    define LOADER_NEWLIB_HEAP_SIZE (192 * 1024 * 1024)
#  endif
#endif

/* SceLibc heap, only used by SceLibcBridge stdio (USE_SCELIBC_IO). */
#ifndef LOADER_SCELIBC_HEAP_SIZE
#  define LOADER_SCELIBC_HEAP_SIZE (4 * 1024 * 1024)
#endif

/* vglInitExtended(legacy_pool, w, h, ram_threshold, msaa):
 * ram_threshold = memory vitaGL leaves to other sceKernelAllocMemBlock users
 * after taking the rest of main RAM for its pool. vitaGL starts on the render
 * thread; every Rust thread spawned later takes its stack (2 MiB, or more if
 * asked) from this reserve, hence 16 MiB instead of the boilerplate's 6. */
#ifndef LOADER_VGL_RAM_THRESHOLD
#  define LOADER_VGL_RAM_THRESHOLD (16 * 1024 * 1024)
#endif
#ifndef LOADER_VGL_LEGACY_POOL
#  define LOADER_VGL_LEGACY_POOL 0
#endif
/* 0 = SCE_GXM_MULTISAMPLE_NONE, 1 = 2X, 2 = 4X. miniquad asks for no MSAA
 * (the packager patch replaced EGL_SAMPLES by EGL_RENDERABLE_TYPE), so NONE. */
#ifndef LOADER_VGL_MSAA
#  define LOADER_VGL_MSAA 0
#endif

/* --------------------------------------------------------------- threads -- */

/* Stack of the loader main thread (= Android "UI thread": runs JNI_OnLoad,
 * activityOnCreate -> quad_main -> Looking Glass main() until it spawns the
 * render thread, then pumps input). */
#ifndef LOADER_MAIN_STACK_SIZE
#  define LOADER_MAIN_STACK_SIZE (8 * 1024 * 1024)
#endif

/* Priority of the UI thread (absolute Vita user priority, 64 = highest,
 * 0x10000100 = default of new threads). miniquad's render thread busy-spins
 * until it receives SurfaceChanged from the UI thread; a higher priority makes
 * sure the UI thread is scheduled even if both land on the same core. The UI
 * thread then sleeps 16 ms per input poll, so it does not starve rendering. */
#ifndef LOADER_UI_THREAD_PRIORITY
#  define LOADER_UI_THREAD_PRIORITY 96
#endif

/* Upper bound for any stack requested through pthread_attr_setstacksize.
 * miniquad asks 16 MiB for its render thread; capped (and logged) to this. */
#ifndef LOADER_THREAD_STACK_CAP
#  define LOADER_THREAD_STACK_CAP (8 * 1024 * 1024)
#endif

/* Stack for threads created without an explicit size (C/C++ code). */
#ifndef LOADER_THREAD_STACK_DEFAULT
#  define LOADER_THREAD_STACK_DEFAULT (1 * 1024 * 1024)
#endif

/* Value served for getenv("RUST_MIN_STACK"): the fork sets 16 MiB, which would
 * give every std::thread 16 MiB. Rust's own Android default is 2 MiB. */
#ifndef LOADER_RUST_MIN_STACK
#  define LOADER_RUST_MIN_STACK "2097152"
#endif

/* ---------------------------------------------------------- system info -- */

/* sysconf(_SC_NPROCESSORS_ONLN/CONF). The Vita has 4 Cortex-A9 cores but only
 * 3 are available to applications (core 3 belongs to the system). */
#ifndef LOADER_NPROCESSORS
#  define LOADER_NPROCESSORS 3
#endif

/* Android API level reported to the .so (ro.build.version.sdk, SDK_INT).
 * 26 keeps oboe on OpenSL ES (AAudio is only "recommended" from 27). */
#ifndef LOADER_ANDROID_SDK
#  define LOADER_ANDROID_SDK 26
#endif
#define LOADER_STR2(x) #x
#define LOADER_STR(x) LOADER_STR2(x)

/* ----------------------------------------------------------------- audio -- */

/* 0: first boot without sound. slCreateEngine (served to oboe via dlsym)
 *    returns SL_RESULT_FEATURE_UNSUPPORTED and AudioTrack.getMinBufferSize
 *    returns ERROR_BAD_VALUE, so cpal/rodio report "no audio output" cleanly.
 * 1: serve VitaSDK's libOpenSLES slCreateEngine (untested). */
#ifndef LOADER_AUDIO_ENABLED
#  define LOADER_AUDIO_ENABLED 0
#endif

/* ------------------------------------------------------------- display -- */

#define LOADER_SCREEN_W 960
#define LOADER_SCREEN_H 544

/* --------------------------------------------------------------- paths -- */

/* DATA_PATH ("ux0:data/alice/") and SO_PATH come from CMake. Writable root
 * for Looking Glass (settings, saves, crash.log) and PK3 directory, given to
 * the game through LOOKING_GLASS_SETTINGS_DIR / LOOKING_GLASS_DATA. */
#ifndef LOADER_STORAGE_DIR
#  define LOADER_STORAGE_DIR "ux0:data/alice"
#endif
#ifndef LOADER_PK3_DIR
#  define LOADER_PK3_DIR "ux0:data/alice"
#endif

/* ----------------------------------------------------------------- misc -- */

/* Scan .text for 0xFFFF0FC0/0xFFFF0FA0 (Linux kuser helpers) and patch them.
 * Off: Rust/LLVM armv7 code never calls kuser helpers, and a blind scan of
 * 9 MiB of .text can rewrite a literal that merely looks like the address. */
#ifndef LOADER_KUSER_PATCH
#  define LOADER_KUSER_PATCH 0
#endif

/* Print __android_log_* messages of every priority, also in Release builds. */
#ifndef LOADER_ALOG_ALWAYS
#  define LOADER_ALOG_ALWAYS 1
#endif

#endif // ALICE_LOADER_CONFIG_H
