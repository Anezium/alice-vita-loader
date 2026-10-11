/*
 * FalsoJNI implementation file (JNI methods and fields served to the .so).
 *
 * Rewritten for the Alice Vita loader (spike-c/loader, derived from
 * soloader-boilerplate rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT;
 * the boilerplate file was an empty template).
 *
 * Who calls what (read from miniquad 0.4.8 as patched by the fork's packager,
 * the fork's src/android.rs at 6a9b8cd, cpal 0.15.3 and oboe 0.6.1):
 *
 *   miniquad   setFullScreen(Z)V            run() when conf.fullscreen, asserts the method exists
 *              showKeyboard(Z)V             ShowKeyboard request, asserts the method exists
 *              getClipboardText()Ljava/lang/String;   clipboard get
 *              setClipboardText(Ljava/lang/String;)V  clipboard set
 *              getAssets()Landroid/content/res/AssetManager;  load_asset (not linked in)
 *   Looking    reportNativeCrash(Ljava/lang/String;)V  panic hook
 *   Glass      finish()V                    clean Quit
 *   launcher   openFolderPicker, openFilePicker, openTelegramLink, openGithubLink,
 *              showCreditsDialog, enterGameRenderMode, checkForUpdates  (all ()V)
 *   cpal       android/media/AudioTrack.getMinBufferSize(III)I   static, result unwrapped
 *              (a missing method would panic inside default_output_config)
 *   oboe-rs    android/os/Build$VERSION.SDK_INT   static int field
 *              Context.getSystemService(...)       deliberately absent: the JNI error
 *                                                  makes oboe-rs report "no device list"
 *
 * FalsoJNI looks methods up by name only (any class), see lib/falso_jni/README.md.
 */

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>

#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>

#include "utils/logger.h"
#include "config.h"

static void log_jstring(const char *what, jstring s) {
    if (!s) {
        l_notice("[java] %s(null)", what);
        return;
    }
    const char *c = jni->GetStringUTFChars(&jni, s, NULL);
    l_notice("[java] %s(\"%s\")", what, c ? c : "(null)");
    if (c) jni->ReleaseStringUTFChars(&jni, s, (char *)c);
}

/* ---- MainActivity (void) ------------------------------------------------ */

void MainActivity_showKeyboard(jmethodID id, va_list args) {
    jboolean show = (jboolean)va_arg(args, int);
    // TODO(jalon 4+): sceImeDialog for text entry (savegame names).
    l_stub("[java] showKeyboard(%d): no on-screen keyboard yet", show);
}

void MainActivity_setFullScreen(jmethodID id, va_list args) {
    jboolean fs = (jboolean)va_arg(args, int);
    l_notice("[java] setFullScreen(%d): always full screen on Vita", fs);
}

void MainActivity_reportNativeCrash(jmethodID id, va_list args) {
    jstring msg = va_arg(args, jstring);
    // The fork's panic hook also writes crash.log in LOOKING_GLASS_SETTINGS_DIR
    // and parks the panicking thread.
    l_fatal("[java] reportNativeCrash:");
    log_jstring("reportNativeCrash", msg);
}

void MainActivity_finish(jmethodID id, va_list args) {
    l_notice("[java] finish(): game asked to quit, exiting");
    sceKernelExitProcess(0);
}

void MainActivity_setClipboardText(jmethodID id, va_list args) {
    jstring text = va_arg(args, jstring);
    log_jstring("setClipboardText (ignored, no clipboard)", text);
}

void MainActivity_launcherAction(jmethodID id, va_list args) {
    const char *name = "?";
    switch ((int)id) {
        case 6:  name = "openFolderPicker"; break;
        case 7:  name = "openFilePicker"; break;
        case 8:  name = "openTelegramLink"; break;
        case 9:  name = "openGithubLink"; break;
        case 10: name = "showCreditsDialog"; break;
        case 11: name = "enterGameRenderMode"; break;
        case 12: name = "checkForUpdates"; break;
    }
    l_stub("[java] %s(): Android launcher UI, nothing to do on Vita", name);
}

/* ---- MainActivity (object) ---------------------------------------------- */

jobject MainActivity_getClipboardText(jmethodID id, va_list args) {
    l_notice("[java] getClipboardText(): no clipboard, null");
    return NULL;
}

jobject MainActivity_getAssets(jmethodID id, va_list args) {
    // Only reachable through miniquad's load_asset (APK assets). The game data
    // are PK3 files read with std::fs, so this should never be called.
    l_stub("[java] getAssets(): APK assets do not exist on Vita, returning a dummy object");
    return (jobject)0x41535354;
}

/* ---- android.media.AudioTrack (static int) ------------------------------ */

#define AUDIOTRACK_ERROR_BAD_VALUE (-2)

#define AUDIOFORMAT_ENCODING_PCM_8BIT  3
#define AUDIOFORMAT_ENCODING_PCM_FLOAT 4

jint AudioTrack_getMinBufferSize(jmethodID id, va_list args) {
    // getMinBufferSize(int sampleRateInHz, int channelConfig, int audioFormat).
    // cpal calls it through Rust's `jni` crate, i.e. CallStaticIntMethodA;
    // the FalsoJNI fork passes jvalue arguments through, so they read like
    // those of any other method.
    jint rate = va_arg(args, jint);
    jint channel_mask = va_arg(args, jint);
    jint encoding = va_arg(args, jint);
#if LOADER_AUDIO_ENABLED
    // About 20 ms of audio, which is what Android returns, never below 4096 bytes.
    int channels = __builtin_popcount((unsigned)channel_mask);
    int bytes = encoding == AUDIOFORMAT_ENCODING_PCM_FLOAT ? 4 :
                encoding == AUDIOFORMAT_ENCODING_PCM_8BIT ? 1 : 2;
    jint size = (rate / 50) * (channels ? channels : 1) * bytes;
    if (size < 4096)
        size = 4096;
    l_notice("[java] AudioTrack.getMinBufferSize(%d Hz, mask 0x%x, encoding %d) = %d",
             rate, channel_mask, encoding, size);
    return size;
#else
    // cpal unwraps this call (a missing method would panic) but treats a
    // negative size as "configuration unsupported": with every combination
    // rejected, rodio's OutputStream::try_default() returns an error and the
    // game runs without sound ("No audio output available").
    l_stub_once("[java] AudioTrack.getMinBufferSize(%d Hz, mask 0x%x, encoding %d): ERROR_BAD_VALUE, "
                "audio disabled (LOADER_AUDIO_ENABLED=0; logged once)", rate, channel_mask, encoding);
    return AUDIOTRACK_ERROR_BAD_VALUE;
#endif
}

/*
 * JNI Methods
 */

NameToMethodID nameToMethodId[] = {
    { 1,  "showKeyboard",        METHOD_TYPE_VOID },
    { 2,  "setFullScreen",       METHOD_TYPE_VOID },
    { 3,  "reportNativeCrash",   METHOD_TYPE_VOID },
    { 4,  "finish",              METHOD_TYPE_VOID },
    { 5,  "setClipboardText",    METHOD_TYPE_VOID },
    { 6,  "openFolderPicker",    METHOD_TYPE_VOID },
    { 7,  "openFilePicker",      METHOD_TYPE_VOID },
    { 8,  "openTelegramLink",    METHOD_TYPE_VOID },
    { 9,  "openGithubLink",      METHOD_TYPE_VOID },
    { 10, "showCreditsDialog",   METHOD_TYPE_VOID },
    { 11, "enterGameRenderMode", METHOD_TYPE_VOID },
    { 12, "checkForUpdates",     METHOD_TYPE_VOID },
    { 20, "getClipboardText",    METHOD_TYPE_OBJECT },
    { 21, "getAssets",           METHOD_TYPE_OBJECT },
    { 30, "getMinBufferSize",    METHOD_TYPE_INT },
};

MethodsBoolean methodsBoolean[] = {};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { 30, AudioTrack_getMinBufferSize },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { 20, MainActivity_getClipboardText },
    { 21, MainActivity_getAssets },
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { 1,  MainActivity_showKeyboard },
    { 2,  MainActivity_setFullScreen },
    { 3,  MainActivity_reportNativeCrash },
    { 4,  MainActivity_finish },
    { 5,  MainActivity_setClipboardText },
    { 6,  MainActivity_launcherAction },
    { 7,  MainActivity_launcherAction },
    { 8,  MainActivity_launcherAction },
    { 9,  MainActivity_launcherAction },
    { 10, MainActivity_launcherAction },
    { 11, MainActivity_launcherAction },
    { 12, MainActivity_launcherAction },
};

/*
 * JNI Fields
 */

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

// android.os.Build.VERSION.SDK_INT, read by oboe-rs before enumerating devices.
// 26 (Android 8.0) keeps oboe on OpenSL ES, see config.h.
const int SDK_INT = LOADER_ANDROID_SDK;

NameToFieldID nameToFieldId[] = {
    { 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT },
    { 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
    { 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
    { 0, WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES
