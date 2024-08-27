// SPDX-License-Identifier: Apache-2.0
/**
 * Copyright (c) 2024  Panasonic Automotive Systems, Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _UTIL_EXTENSION_H_
#define _UTIL_EXTENSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#if defined(GL_RG16F_EXT) && !defined(GL_RG16F)
#define GL_RG16F GL_RG16F_EXT
#endif

#if defined(GL_RGBA16F_EXT) && !defined(GL_RGBA16F)
#define GL_RGBA16F GL_RGBA16F_EXT
#endif

#if defined(GL_RG_EXT) && !defined(GL_RG)
#define GL_RG GL_RG_EXT
#endif

#if defined(GL_HALF_FLOAT_OES) && !defined(GL_HALF_FLOAT)
#define GL_HALF_FLOAT GL_HALF_FLOAT_OES
#endif

#if defined(GL_TEXTURE_MAX_LEVEL_APPLE) && !defined(GL_TEXTURE_MAX_LEVEL)
#define GL_TEXTURE_MAX_LEVEL GL_TEXTURE_MAX_LEVEL_APPLE
#endif

/* ------------------------------------------------- *
 * EGL_WL_bind_wayland_display
 * ------------------------------------------------- */
#ifndef EGL_WL_bind_wayland_display
#define EGL_WL_bind_wayland_display 1

#define EGL_WAYLAND_BUFFER_WL 0x31D5 /* eglCreateImageKHR target */
#define EGL_WAYLAND_PLANE_WL 0x31D6 /* eglCreateImageKHR target */

#define EGL_WAYLAND_Y_INVERTED_WL 0x31DB /* eglQueryWaylandBufferWL attribute */

#define EGL_TEXTURE_Y_U_V_WL 0x31D7
#define EGL_TEXTURE_Y_UV_WL 0x31D8
#define EGL_TEXTURE_Y_XUXV_WL 0x31D9
#define EGL_TEXTURE_EXTERNAL_WL 0x31DA

struct wl_display;
struct wl_resource;
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY
eglBindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *display);
EGLAPI EGLBoolean EGLAPIENTRY
eglUnbindWaylandDisplayWL(EGLDisplay dpy, struct wl_display *display);
EGLAPI EGLBoolean EGLAPIENTRY
eglQueryWaylandBufferWL(EGLDisplay dpy, struct wl_resource *buffer,
			EGLint attribute, EGLint *value);
#endif
typedef EGLBoolean(EGLAPIENTRYP PFNEGLBINDWAYLANDDISPLAYWL)(
	EGLDisplay dpy, struct wl_display *display);
typedef EGLBoolean(EGLAPIENTRYP PFNEGLUNBINDWAYLANDDISPLAYWL)(
	EGLDisplay dpy, struct wl_display *display);
typedef EGLBoolean(EGLAPIENTRYP PFNEGLQUERYWAYLANDBUFFERWL)(
	EGLDisplay dpy, struct wl_resource *buffer, EGLint attribute,
	EGLint *value);

#endif

/* ------------------------------------------------- *
 * EGL_WL_bind_wayland_display
 * ------------------------------------------------- */
#ifndef EGL_WL_create_wayland_buffer_from_image
#define EGL_WL_create_wayland_buffer_from_image 1

struct wl_buffer;
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI struct wl_buffer *EGLAPIENTRY
eglCreateWaylandBufferFromImageWL(EGLDisplay dpy, EGLImageKHR image);
#endif
typedef struct wl_buffer *(EGLAPIENTRYP PFNEGLCREATEWAYLANDBUFFERFROMIMAGEWL)(
	EGLDisplay dpy, EGLImageKHR image);

#endif

#ifdef __cplusplus
}
#endif
#endif /* _UTIL_EXTENSION_H_ */
