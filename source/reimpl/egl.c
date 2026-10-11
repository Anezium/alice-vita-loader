/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * Rewritten for the Alice Vita loader (spike-c/loader, derived from
 * soloader-boilerplate rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT).
 * The boilerplate's egl.c was not compiled (vitaGL's own EGL was used); this
 * file is the EGL layer served to miniquad through dlopen("libEGL.so")/dlsym:
 *   - all 28 EGL entry points miniquad resolves exist (declare_module! unwraps
 *     every dlsym result, a NULL would panic before the first frame);
 *   - GLES3 is refused (config with EGL_OPENGL_ES3_BIT, context version >= 3),
 *     miniquad's packager patch then retries with a GLES2 context;
 *   - EGL_SAMPLES / EGL_SAMPLE_BUFFERS are accepted and ignored (MSAA is a
 *     vitaGL init parameter, LOADER_VGL_MSAA);
 *   - eglGetProcAddress serves GL through the loader symbol table (shader hooks
 *     first, then vitaGL), see soloader_lookup_symbol() in dynlib.c.
 * Values returned for configs/surfaces follow the boilerplate's stubs.
 */

#include "reimpl/egl.h"

#include "utils/glutil.h"
#include "utils/logger.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>

#define EGL_BAD_MATCH_            0x3009
#define EGL_BAD_CONFIG_           0x3005
#define EGL_OPENGL_ES3_BIT_KHR_   0x0040
#define EGL_SAMPLE_BUFFERS_       0x3032
#define EGL_DRAW_                 0x3059
#define EGL_READ_                 0x305A
#define EGL_CONTEXT_MAJOR_VERSION_ 0x3098 /* same value as EGL_CONTEXT_CLIENT_VERSION */
#define WINDOW_FORMAT_RGBA_8888   1

// Fake handles (any non-NULL value; never dereferenced by miniquad).
#define ALICE_EGL_DISPLAY ((EGLDisplay)0xA11CE001)
#define ALICE_EGL_CONFIG  ((EGLConfig)0xA11CE002)
#define ALICE_EGL_CONTEXT ((EGLContext)0xA11CE003)
#define ALICE_EGL_SURFACE ((EGLSurface)0xA11CE004)

static volatile EGLint egl_last_error = EGL_SUCCESS;
static volatile int egl_gl_ready = 0;
// EGL types are macros in vitaGL.h (EGLSurface = void*): one declaration per line.
static EGLSurface cur_draw = EGL_NO_SURFACE;
static EGLSurface cur_read = EGL_NO_SURFACE;
static EGLContext cur_ctx = EGL_NO_CONTEXT;
static EGLDisplay cur_dpy = EGL_NO_DISPLAY;

#define EGL_FAIL(err, ret) do { egl_last_error = (err); return (ret); } while (0)
#define EGL_OK(ret)        do { egl_last_error = EGL_SUCCESS; return (ret); } while (0)

EGLDisplay alice_eglGetDisplay(void *native_display) {
    l_notice("eglGetDisplay(%p) -> %p", native_display, ALICE_EGL_DISPLAY);
    EGL_OK(ALICE_EGL_DISPLAY);
}

EGLBoolean alice_eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor) {
    // First EGL call of miniquad's render thread: bring vitaGL up here so that
    // the GXM context lives on the thread that will issue every GL call.
    if (!egl_gl_ready) {
        gl_init();
        egl_gl_ready = 1;
    }
    if (major) *major = 1;
    if (minor) *minor = 4;
    l_notice("eglInitialize(%p): EGL 1.4 over vitaGL", dpy);
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglTerminate(EGLDisplay dpy) {
    l_notice("eglTerminate(%p)", dpy);
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglBindAPI(EGLenum api) {
    if (api != 0x30A0 /* EGL_OPENGL_ES_API */) {
        l_stub("eglBindAPI(0x%x): only EGL_OPENGL_ES_API is supported", (unsigned)api);
        EGL_FAIL(EGL_BAD_PARAMETER, EGL_FALSE);
    }
    EGL_OK(EGL_TRUE);
}

static int config_wants_es3(const EGLint *attrib_list) {
    if (!attrib_list) return 0;
    for (const EGLint *a = attrib_list; a[0] != EGL_NONE; a += 2) {
        switch (a[0]) {
            case EGL_RENDERABLE_TYPE:
            case EGL_CONFORMANT:
                if (a[1] & EGL_OPENGL_ES3_BIT_KHR_) return 1;
                break;
            case EGL_SAMPLES:
            case EGL_SAMPLE_BUFFERS_:
                if (a[1] > 0)
                    l_notice("eglChooseConfig: attribute 0x%x=%d ignored (MSAA is LOADER_VGL_MSAA=%d)",
                             a[0], a[1], LOADER_VGL_MSAA);
                break;
            default:
                break;
        }
    }
    return 0;
}

EGLBoolean alice_eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs,
                                 EGLint config_size, EGLint *num_config) {
    if (!num_config)
        EGL_FAIL(EGL_BAD_PARAMETER, EGL_FALSE);
    if (config_wants_es3(attrib_list)) {
        l_notice("eglChooseConfig: EGL_OPENGL_ES3_BIT requested, no matching config (vitaGL is GLES2)");
        *num_config = 0;
        EGL_OK(EGL_TRUE);
    }
    *num_config = 1;
    if (configs && config_size > 0)
        configs[0] = ALICE_EGL_CONFIG;
    l_notice("eglChooseConfig: 1 config (RGBA8888, depth 24, stencil 8, GLES2)");
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config) {
    if (!num_config)
        EGL_FAIL(EGL_BAD_PARAMETER, EGL_FALSE);
    *num_config = 1;
    if (configs && config_size > 0)
        configs[0] = ALICE_EGL_CONFIG;
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglGetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute, EGLint *value) {
    if (!value)
        EGL_FAIL(EGL_BAD_PARAMETER, EGL_FALSE);
    switch (attribute) {
        case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE:
            *value = 8; break;
        case EGL_BUFFER_SIZE:          *value = 32; break;
        case EGL_DEPTH_SIZE:           *value = 24; break;
        case EGL_STENCIL_SIZE:         *value = 8; break;
        case EGL_ALPHA_MASK_SIZE:      *value = 0; break;
        case EGL_LUMINANCE_SIZE:       *value = 0; break;
        case EGL_BIND_TO_TEXTURE_RGB:
        case EGL_BIND_TO_TEXTURE_RGBA: *value = EGL_FALSE; break;
        case EGL_COLOR_BUFFER_TYPE:    *value = EGL_RGB_BUFFER; break;
        case EGL_CONFIG_CAVEAT:        *value = EGL_NONE; break;
        case EGL_CONFIG_ID:            *value = 1; break;
        case EGL_CONFORMANT:
        case EGL_RENDERABLE_TYPE:      *value = EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT; break; // no ES3
        case EGL_LEVEL:                *value = 0; break;
        case EGL_MAX_PBUFFER_WIDTH:
        case EGL_MAX_PBUFFER_HEIGHT:
        case EGL_MAX_PBUFFER_PIXELS:   *value = 0; break;
        case EGL_MAX_SWAP_INTERVAL:    *value = 1; break;
        case EGL_MIN_SWAP_INTERVAL:    *value = 0; break;
        case EGL_NATIVE_RENDERABLE:    *value = EGL_FALSE; break;
        case EGL_NATIVE_VISUAL_ID:     *value = WINDOW_FORMAT_RGBA_8888; break;
        case EGL_NATIVE_VISUAL_TYPE:   *value = 0; break;
        case EGL_SAMPLE_BUFFERS:       *value = LOADER_VGL_MSAA ? 1 : 0; break;
        case EGL_SAMPLES:              *value = LOADER_VGL_MSAA ? (2 << (LOADER_VGL_MSAA - 1)) : 0; break;
        case EGL_SURFACE_TYPE:         *value = EGL_WINDOW_BIT; break;
        case EGL_TRANSPARENT_TYPE:     *value = EGL_NONE; break;
        case EGL_TRANSPARENT_RED_VALUE:
        case EGL_TRANSPARENT_GREEN_VALUE:
        case EGL_TRANSPARENT_BLUE_VALUE: *value = 0; break;
        default:
            l_stub("eglGetConfigAttrib(0x%x): unknown attribute, EGL_BAD_ATTRIBUTE", attribute);
            EGL_FAIL(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    }
    EGL_OK(EGL_TRUE);
}

EGLContext alice_eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context,
                                  const EGLint *attrib_list) {
    int version = 1;
    if (attrib_list) {
        for (const EGLint *a = attrib_list; a[0] != EGL_NONE; a += 2) {
            if (a[0] == EGL_CONTEXT_CLIENT_VERSION) // == EGL_CONTEXT_MAJOR_VERSION
                version = a[1];
        }
    }
    if (version >= 3) {
        l_notice("eglCreateContext: GLES %d context refused (EGL_BAD_MATCH), caller should retry with GLES2", version);
        EGL_FAIL(EGL_BAD_MATCH_, EGL_NO_CONTEXT);
    }
    l_notice("eglCreateContext: GLES %d context -> %p", version, ALICE_EGL_CONTEXT);
    EGL_OK(ALICE_EGL_CONTEXT);
}

EGLBoolean alice_eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    if (ctx == cur_ctx) cur_ctx = EGL_NO_CONTEXT;
    EGL_OK(EGL_TRUE);
}

EGLSurface alice_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, void *win, const EGLint *attrib_list) {
    l_notice("eglCreateWindowSurface(window=%p) -> %p (%dx%d)", win, ALICE_EGL_SURFACE,
             LOADER_SCREEN_W, LOADER_SCREEN_H);
    EGL_OK(ALICE_EGL_SURFACE);
}

EGLSurface alice_eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list) {
    l_stub("eglCreatePbufferSurface: pbuffers not supported, EGL_NO_SURFACE (use FBOs)");
    EGL_FAIL(EGL_BAD_MATCH_, EGL_NO_SURFACE);
}

EGLSurface alice_eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, void *pixmap, const EGLint *attrib_list) {
    l_stub("eglCreatePixmapSurface: pixmaps not supported, EGL_NO_SURFACE");
    EGL_FAIL(EGL_BAD_MATCH_, EGL_NO_SURFACE);
}

EGLBoolean alice_eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    if (surface == cur_draw) cur_draw = EGL_NO_SURFACE;
    if (surface == cur_read) cur_read = EGL_NO_SURFACE;
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    cur_dpy = dpy;
    cur_draw = draw;
    cur_read = read;
    cur_ctx = ctx;
    EGL_OK(EGL_TRUE);
}

EGLContext alice_eglGetCurrentContext(void) { EGL_OK(cur_ctx); }
EGLDisplay alice_eglGetCurrentDisplay(void) { EGL_OK(cur_dpy); }

EGLSurface alice_eglGetCurrentSurface(EGLint readdraw) {
    EGL_OK(readdraw == EGL_READ_ ? cur_read : cur_draw);
}

EGLBoolean alice_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    vglSwapBuffers(GL_FALSE);
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    static EGLint last = -1;
    if (interval != last) {
        l_notice("eglSwapInterval(%d): vblank wait %s", interval, interval > 0 ? "on" : "off");
        last = interval;
    }
    vglWaitVblankStart(interval > 0 ? GL_TRUE : GL_FALSE);
    EGL_OK(EGL_TRUE);
}

EGLint alice_eglGetError(void) {
    EGLint e = egl_last_error;
    egl_last_error = EGL_SUCCESS;
    return e;
}

const char *alice_eglQueryString(EGLDisplay display, EGLint name) {
    switch (name) {
        case EGL_CLIENT_APIS: EGL_OK("OpenGL_ES");
        case EGL_VENDOR:      EGL_OK("Alice Vita loader (vitaGL)");
        case EGL_VERSION:     EGL_OK("1.4 vitaGL");
        case EGL_EXTENSIONS:  EGL_OK("");
        default:
            l_stub("eglQueryString(0x%x): unknown name", name);
            EGL_FAIL(EGL_BAD_PARAMETER, NULL);
    }
}

EGLBoolean alice_eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value) {
    if (!value) EGL_FAIL(EGL_BAD_PARAMETER, EGL_FALSE);
    switch (attribute) {
        case EGL_CONFIG_ID:               *value = 1; break;
        case EGL_CONTEXT_CLIENT_TYPE:     *value = 0x30A0; break; // EGL_OPENGL_ES_API
        case EGL_CONTEXT_CLIENT_VERSION:  *value = 2; break;
        case EGL_RENDER_BUFFER:           *value = EGL_BACK_BUFFER; break;
        default:
            l_stub("eglQueryContext(0x%x): unknown attribute", attribute);
            EGL_FAIL(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    }
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value) {
    if (!value) EGL_FAIL(EGL_BAD_PARAMETER, EGL_FALSE);
    switch (attribute) {
        case EGL_CONFIG_ID:        *value = 1; break;
        case EGL_WIDTH:            *value = LOADER_SCREEN_W; break;
        case EGL_HEIGHT:           *value = LOADER_SCREEN_H; break;
        case EGL_TEXTURE_FORMAT:   *value = EGL_NONE; break;
        case EGL_TEXTURE_TARGET:   *value = EGL_NONE; break;
        case EGL_SWAP_BEHAVIOR:    *value = 0x3095; break; // EGL_BUFFER_DESTROYED
        case EGL_LARGEST_PBUFFER:
        case EGL_MIPMAP_TEXTURE:   *value = EGL_FALSE; break;
        case EGL_MIPMAP_LEVEL:     *value = 0; break;
        case EGL_MULTISAMPLE_RESOLVE: *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT; break;
        case EGL_HORIZONTAL_RESOLUTION:
        case EGL_VERTICAL_RESOLUTION: *value = 220 * EGL_DISPLAY_SCALING; break; // ~220 dpi
        case EGL_PIXEL_ASPECT_RATIO: *value = EGL_DISPLAY_SCALING; break;        // square pixels
        case EGL_RENDER_BUFFER:    *value = EGL_BACK_BUFFER; break;
        case EGL_VG_COLORSPACE:    *value = EGL_VG_COLORSPACE_sRGB; break;
        case EGL_VG_ALPHA_FORMAT:  *value = EGL_VG_ALPHA_FORMAT_NONPRE; break;
        case EGL_TIMESTAMPS_ANDROID: *value = EGL_FALSE; break;
        default:
            l_stub("eglQuerySurface(0x%x): unknown attribute", attribute);
            EGL_FAIL(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    }
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) {
    l_stub("eglSurfaceAttrib(0x%x, %d): ignored", attribute, value);
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    l_stub("eglBindTexImage: not supported (no pbuffers), EGL_BAD_SURFACE");
    EGL_FAIL(EGL_BAD_SURFACE, EGL_FALSE);
}

EGLBoolean alice_eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    l_stub("eglReleaseTexImage: not supported (no pbuffers), EGL_BAD_SURFACE");
    EGL_FAIL(EGL_BAD_SURFACE, EGL_FALSE);
}

EGLBoolean alice_eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, void *target) {
    l_stub("eglCopyBuffers: not supported (no native pixmaps), EGL_BAD_SURFACE");
    EGL_FAIL(EGL_BAD_SURFACE, EGL_FALSE);
}

EGLBoolean alice_eglWaitGL(void) {
    glFinish();
    EGL_OK(EGL_TRUE);
}

EGLBoolean alice_eglWaitNative(EGLint engine) {
    l_stub_once("eglWaitNative(0x%x): nothing to wait for (logged once)", engine);
    EGL_OK(EGL_TRUE);
}

void *alice_eglGetProcAddress(const char *procname) {
    void *p = soloader_lookup_symbol(procname, 1);
    l_debug("eglGetProcAddress(\"%s\") = %p", procname, p);
    return p;
}

/* ------------------------------------------------------------------------ */
/* GLES2 entry points miniquad looks up that vitaGL does not provide.       */

void alice_glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    l_stub_once("glBlendColor(%f, %f, %f, %f): no constant blend colour in vitaGL, ignored (logged once)",
                r, g, b, a);
}

void alice_glDetachShader(GLuint program, GLuint shader) {
    // vitaGL keeps the compiled shader with the program; nothing to undo.
    l_stub_once("glDetachShader(%u, %u): no-op on vitaGL (logged once)", program, shader);
}

void alice_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) {
    if (pname == 0x1004 /* GL_TEXTURE_BORDER_COLOR */) {
        l_stub_once("glTexParameterfv(GL_TEXTURE_BORDER_COLOR): unsupported, ignored (logged once)");
        return;
    }
    glTexParameterf(target, pname, params ? params[0] : 0.0f);
}
