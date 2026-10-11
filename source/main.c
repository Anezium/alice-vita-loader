/*
 * Alice (Looking Glass) Vita loader -- entry point.
 *
 * Rewritten for the Alice Vita loader (spike-c/loader, derived from
 * soloader-boilerplate rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT;
 * the boilerplate main.c was a template with an empty render loop).
 *
 * liblooking_glass.so is a miniquad "Java activity" program, not a
 * NativeActivity (no ANativeActivity_onCreate / android_main). We replay what
 * the fork's MainActivity.java does, on one thread (the "UI thread"), because
 * miniquad keeps its message channel in a thread_local of the thread that ran
 * activityOnCreate: every QuadNative callback must come from that thread.
 *
 *   1. JNI_OnLoad(vm, NULL)                        -> miniquad stores the JavaVM
 *   2. QuadNative.activityOnCreate(env, cls, act)  -> NewGlobalRef(activity),
 *        quad_main() -> init_runtime() (ndk_context, panic hook) -> Looking
 *        Glass main() -> miniquad::start(): spawns the render thread (16 MiB
 *        asked, capped) and returns. The render thread spins until it gets
 *        SurfaceChanged, then dlopen("libEGL.so"), EGL init, GL loading.
 *   3. QuadNative.surfaceOnSurfaceChanged(env, cls, surface, 960, 544)
 *        (MainActivity only ever sends surfaceChanged, never surfaceCreated)
 *   4. QuadNative.activityOnResume(env, cls)
 *   5. loop: sceCtrl/sceTouch -> MainActivity.nativeOnGamepad (XInput layout,
 *        atomics, any thread) and QuadNative.surfaceOnTouch (UI thread).
 */

#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include "config.h"

#include <pthread.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include "reimpl/controls.h"

int _newlib_heap_size_user = LOADER_NEWLIB_HEAP_SIZE;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = LOADER_SCELIBC_HEAP_SIZE;
#endif

so_module so_mod;

/* Fake Java objects: FalsoJNI never dereferences them (GetObjectClass returns
 * a constant, methods are looked up by name). Strings make logs readable. */
static jclass quad_native_class = (jclass)"quad_native/QuadNative";
static jclass main_activity_class = (jclass)"com/lookingglass/alice/MainActivity";
static jobject main_activity = (jobject)"com.lookingglass.alice.MainActivity@vita";
static jobject main_surface = (jobject)"android.view.Surface@vita";

typedef jint (*JNI_OnLoad_t)(JavaVM *vm, void *reserved);
typedef void (*jni_activity_create_t)(JNIEnv *env, jclass cls, jobject activity);
typedef void (*jni_activity_t)(JNIEnv *env, jclass cls);
typedef void (*jni_surface_changed_t)(JNIEnv *env, jclass cls, jobject surface, jint w, jint h);
typedef void (*jni_touch_t)(JNIEnv *env, jclass cls, jint id, jint phase, jfloat x, jfloat y);
typedef void (*jni_gamepad_t)(JNIEnv *env, jclass cls, jint connected, jint buttons,
                              jint lt, jint rt, jint lx, jint ly, jint rx, jint ry);

static jni_touch_t surfaceOnTouch;
static jni_gamepad_t nativeOnGamepad;

static void *require_symbol(const char *name) {
    void *p = (void *)so_symbol(&so_mod, name);
    if (!p) {
        l_fatal("required export %s not found in %s", name, SO_PATH);
        fatal_error("Error: %s does not export %s.\nIs it the armeabi-v7a build of Looking Glass?",
                    SO_PATH, name);
    }
    l_notice("export %s = %p", name, p);
    return p;
}

/* ------------------------------------------------------------- controls -- */

// XInput button bits, as mapped by the fork's MainActivity.mapGamepadButtonBit().
#define XI_DPAD_UP    0x0001
#define XI_DPAD_DOWN  0x0002
#define XI_DPAD_LEFT  0x0004
#define XI_DPAD_RIGHT 0x0008
#define XI_START      0x0010
#define XI_BACK       0x0020
#define XI_LTHUMB     0x0040
#define XI_RTHUMB     0x0080
#define XI_LB         0x0100
#define XI_RB         0x0200
#define XI_A          0x1000
#define XI_B          0x2000
#define XI_X          0x4000
#define XI_Y          0x8000

static volatile uint32_t pad_buttons = 0;
static volatile int pad_lt = 0, pad_rt = 0;
static volatile int pad_lx = 0, pad_ly = 0, pad_rx = 0, pad_ry = 0;

static uint32_t keycode_to_xinput(int32_t keycode) {
    switch (keycode) {
        case AKEYCODE_DPAD_UP:       return XI_DPAD_UP;
        case AKEYCODE_DPAD_DOWN:     return XI_DPAD_DOWN;
        case AKEYCODE_DPAD_LEFT:     return XI_DPAD_LEFT;
        case AKEYCODE_DPAD_RIGHT:    return XI_DPAD_RIGHT;
        case AKEYCODE_BUTTON_A:      return XI_A;      // Cross
        case AKEYCODE_BUTTON_B:      return XI_B;      // Circle
        case AKEYCODE_BUTTON_X:      return XI_X;      // Square
        case AKEYCODE_BUTTON_Y:      return XI_Y;      // Triangle
        case AKEYCODE_BUTTON_L1:     return XI_LB;
        case AKEYCODE_BUTTON_R1:     return XI_RB;
        case AKEYCODE_BUTTON_START:  return XI_START;
        case AKEYCODE_BUTTON_SELECT: return XI_BACK;
        case AKEYCODE_BUTTON_THUMBL: return XI_LTHUMB;
        case AKEYCODE_BUTTON_THUMBR: return XI_RTHUMB;
        default:                     return 0;
    }
}

void controls_handler_key(int32_t keycode, ControlsAction action) {
    // L2/R2 exist on PS TV / DualShock only; they become the analog triggers.
    if (keycode == AKEYCODE_BUTTON_L2) { pad_lt = action == CONTROLS_ACTION_DOWN ? 255 : 0; return; }
    if (keycode == AKEYCODE_BUTTON_R2) { pad_rt = action == CONTROLS_ACTION_DOWN ? 255 : 0; return; }
    uint32_t bit = keycode_to_xinput(keycode);
    if (!bit) return;
    if (action == CONTROLS_ACTION_DOWN) pad_buttons |= bit;
    else if (action == CONTROLS_ACTION_UP) pad_buttons &= ~bit;
}

static int axis16(float v) {
    int r = (int)(v * 32767.0f);
    return r > 32767 ? 32767 : (r < -32767 ? -32767 : r);
}

void controls_handler_analog(ControlsStickId which, float x, float y, ControlsAction action) {
    // Vita sticks: y grows downwards. XInput (and the fork's Java side): up is positive.
    if (which == CONTROLS_STICK_LEFT) { pad_lx = axis16(x); pad_ly = axis16(-y); }
    else                              { pad_rx = axis16(x); pad_ry = axis16(-y); }
}

void controls_handler_touch(int32_t id, float x, float y, ControlsAction action) {
    // miniquad phases: 0 Moved, 1 Ended, 2 Started, 3 Cancelled.
    jint phase = action == CONTROLS_ACTION_DOWN ? 2 : (action == CONTROLS_ACTION_UP ? 1 : 0);
    if (surfaceOnTouch)
        surfaceOnTouch(&jni, quad_native_class, id, phase, x, y);
}

/* ---------------------------------------------------------- UI thread -- */

static void *android_ui_thread(void *arg) {
    int pr = sceKernelChangeThreadPriority(0, LOADER_UI_THREAD_PRIORITY);
    l_notice("UI thread 0x%x: priority %d (ret 0x%x)", sceKernelGetThreadId(), LOADER_UI_THREAD_PRIORITY, pr);

    JNI_OnLoad_t JNI_OnLoad = require_symbol("JNI_OnLoad");
    jni_activity_create_t activityOnCreate = require_symbol("Java_quad_1native_QuadNative_activityOnCreate");
    jni_activity_t activityOnResume = require_symbol("Java_quad_1native_QuadNative_activityOnResume");
    jni_surface_changed_t surfaceOnSurfaceChanged = require_symbol("Java_quad_1native_QuadNative_surfaceOnSurfaceChanged");
    surfaceOnTouch = require_symbol("Java_quad_1native_QuadNative_surfaceOnTouch");
    nativeOnGamepad = require_symbol("Java_com_lookingglass_alice_MainActivity_nativeOnGamepad");

    jint version = JNI_OnLoad(&jvm, NULL);
    l_notice("JNI_OnLoad -> 0x%x", version);

    l_notice("activityOnCreate: entering quad_main (Looking Glass main)");
    activityOnCreate(&jni, quad_native_class, main_activity);
    l_notice("activityOnCreate returned (render thread spawned)");

    surfaceOnSurfaceChanged(&jni, quad_native_class, main_surface, LOADER_SCREEN_W, LOADER_SCREEN_H);
    l_notice("surfaceOnSurfaceChanged(%dx%d) sent", LOADER_SCREEN_W, LOADER_SCREEN_H);

    activityOnResume(&jni, quad_native_class);
    l_notice("activityOnResume sent; entering input loop");

    // TODO(jalon 4+): activityOnPause/Resume on PS button / suspend
    // (sceAppMgr events), surfaceOnKeyDown(AKEYCODE_BACK) mapping if the
    // game's menus need the Android Back key.
    for (;;) {
        controls_poll();
        nativeOnGamepad(&jni, main_activity_class, 1, (jint)pad_buttons, pad_lt, pad_rt,
                        pad_lx, pad_ly, pad_rx, pad_ry);
        sceKernelDelayThread(16666);
    }
    return NULL;
}

int main() {
    soloader_init_all();

    // Run the Android "UI thread" with a large stack of our choosing rather
    // than relying on the process main thread's stack (LOADER_MAIN_STACK_SIZE).
    pthread_attr_t attr;
    pthread_t ui;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, LOADER_MAIN_STACK_SIZE);
    int ret = pthread_create(&ui, &attr, android_ui_thread, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        l_fatal("could not create the UI thread (stack %u KiB): %d",
                (unsigned)(LOADER_MAIN_STACK_SIZE / 1024), ret);
        fatal_error("Error: could not create the main game thread (%d).", ret);
    }
    l_notice("UI thread started (stack %u KiB)", (unsigned)(LOADER_MAIN_STACK_SIZE / 1024));

    sceKernelExitDeleteThread(0);
    return 0;
}
