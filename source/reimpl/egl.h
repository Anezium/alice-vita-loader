/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * Modified for the Alice Vita loader (spike-c/loader, derived from
 * soloader-boilerplate rev 0a382f0ebce348c8cdc68f32fb5fa3186baddc38, MIT).
 */

/**
 * @file  egl.h
 * @brief Implementations for EGL functions. Most of these are just stubs that
 *        don't actually do anything, but we try to conform to the standard
 *        as closely as possible in terms of return values, etc.
 */

#ifndef SOLOADER_EGL_H
#define SOLOADER_EGL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Alice loader: EGL layer served to the .so (names prefixed so they never
// clash with vitaGL's own egl.c, which stays in libvitaGL.a).
EGLDisplay alice_eglGetDisplay(void *native_display);
EGLBoolean alice_eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor);
EGLBoolean alice_eglTerminate(EGLDisplay dpy);
EGLBoolean alice_eglBindAPI(EGLenum api);
EGLBoolean alice_eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean alice_eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);
EGLBoolean alice_eglGetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute, EGLint *value);
EGLContext alice_eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);
EGLBoolean alice_eglDestroyContext(EGLDisplay dpy, EGLContext ctx);
EGLSurface alice_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, void *win, const EGLint *attrib_list);
EGLSurface alice_eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list);
EGLSurface alice_eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, void *pixmap, const EGLint *attrib_list);
EGLBoolean alice_eglDestroySurface(EGLDisplay dpy, EGLSurface surface);
EGLBoolean alice_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
EGLContext alice_eglGetCurrentContext(void);
EGLDisplay alice_eglGetCurrentDisplay(void);
EGLSurface alice_eglGetCurrentSurface(EGLint readdraw);
EGLBoolean alice_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
EGLBoolean alice_eglSwapInterval(EGLDisplay dpy, EGLint interval);
EGLint alice_eglGetError(void);
const char *alice_eglQueryString(EGLDisplay display, EGLint name);
EGLBoolean alice_eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value);
EGLBoolean alice_eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);
EGLBoolean alice_eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value);
EGLBoolean alice_eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean alice_eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer);
EGLBoolean alice_eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, void *target);
EGLBoolean alice_eglWaitGL(void);
EGLBoolean alice_eglWaitNative(EGLint engine);
void *alice_eglGetProcAddress(const char *procname);

void alice_glBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void alice_glDrawBuffers(GLsizei n, const GLenum *bufs);
void alice_glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat,
                                            GLsizei width, GLsizei height);
void alice_glVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride,
                                  const void *pointer);
void alice_glDetachShader(GLuint program, GLuint shader);
void alice_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params);

// dynlib.c: loader symbol table lookup (table first, then vitaGL).
void *soloader_lookup_symbol(const char *name, int log_missing);

#define EGL_CONFIG_ID                     0x3028
#define EGL_HEIGHT                        0x3056
#define EGL_WIDTH                         0x3057
#define EGL_TEXTURE_FORMAT                0x3080
#define EGL_TEXTURE_TARGET                0x3081
#define EGL_SWAP_BEHAVIOR                 0x3093
#define EGL_LARGEST_PBUFFER               0x3058
#define EGL_MIPMAP_TEXTURE                0x3082
#define EGL_MIPMAP_LEVEL                  0x3083
#define EGL_MULTISAMPLE_RESOLVE           0x3099
#define EGL_HORIZONTAL_RESOLUTION         0x3090
#define EGL_VERTICAL_RESOLUTION           0x3091
#define EGL_PIXEL_ASPECT_RATIO            0x3092
#define EGL_RENDER_BUFFER                 0x3086
#define EGL_VG_COLORSPACE                 0x3087
#define EGL_VG_COLORSPACE_sRGB            0x3089
#define EGL_VG_ALPHA_FORMAT               0x3088
#define EGL_VG_ALPHA_FORMAT_NONPRE        0x308B
#define EGL_TIMESTAMPS_ANDROID            0x3430
#define EGL_DISPLAY_SCALING               10000
#define EGL_BUFFER_PRESERVED              0x3094
#define EGL_MULTISAMPLE_RESOLVE_DEFAULT   0x309A
#define EGL_BACK_BUFFER                   0x3084
#define EGL_ALPHA_SIZE                    0x3021
#define EGL_ALPHA_MASK_SIZE               0x303E
#define EGL_BIND_TO_TEXTURE_RGB           0x3039
#define EGL_BIND_TO_TEXTURE_RGBA          0x303A
#define EGL_BLUE_SIZE                     0x3022
#define EGL_BUFFER_SIZE                   0x3020
#define EGL_COLOR_BUFFER_TYPE             0x303F
#define EGL_CONFIG_CAVEAT                 0x3027
#define EGL_CONFIG_ID                     0x3028
#define EGL_CONFORMANT                    0x3042
#define EGL_DEPTH_SIZE                    0x3025
#define EGL_GREEN_SIZE                    0x3023
#define EGL_LEVEL                         0x3029
#define EGL_LUMINANCE_SIZE                0x303D
#define EGL_MAX_PBUFFER_WIDTH             0x302C
#define EGL_MAX_PBUFFER_HEIGHT            0x302A
#define EGL_MAX_PBUFFER_PIXELS            0x302B
#define EGL_MAX_SWAP_INTERVAL             0x303C
#define EGL_MIN_SWAP_INTERVAL             0x303B
#define EGL_NATIVE_RENDERABLE             0x302D
#define EGL_NATIVE_VISUAL_ID              0x302E
#define EGL_NATIVE_VISUAL_TYPE            0x302F
#define EGL_RED_SIZE                      0x3024
#define EGL_RENDERABLE_TYPE               0x3040
#define EGL_SAMPLE_BUFFERS                0x3032
#define EGL_SAMPLES                       0x3031
#define EGL_STENCIL_SIZE                  0x3026
#define EGL_SURFACE_TYPE                  0x3033
#define EGL_TRANSPARENT_TYPE              0x3034
#define EGL_TRANSPARENT_RED_VALUE         0x3037
#define EGL_TRANSPARENT_GREEN_VALUE       0x3036
#define EGL_TRANSPARENT_BLUE_VALUE        0x3035
#define EGL_RGB_BUFFER                    0x308E
#define EGL_NONE                          0x3038
#define EGL_TEXTURE_RGBA                  0x305E
#define EGL_TEXTURE_2D                    0x305F
#define EGL_PBUFFER_BIT                   0x0001
#define EGL_PIXMAP_BIT                    0x0002
#define EGL_WINDOW_BIT                    0x0004
#define EGL_OPENGL_ES_BIT                 0x0001
#define EGL_OPENVG_BIT                    0x0002
#define EGL_OPENGL_ES2_BIT                0x0004
#define EGL_OPENGL_BIT                    0x0008
#define EGL_CONTEXT_CLIENT_TYPE           0x3097
#define EGL_CONTEXT_CLIENT_VERSION        0x3098
#define EGL_VENDOR                        0x3053
#define EGL_VERSION                       0x3054
#define EGL_EXTENSIONS                    0x3055
#define EGL_CLIENT_APIS                   0x308D

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_EGL_H
