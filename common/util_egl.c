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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include "assertegl.h"
#include "winsys.h"
#include "util_egl.h"

static EGLDisplay s_dpy;
static EGLSurface s_sfc;
static EGLContext s_ctx;

static EGLConfig find_egl_config(int r, int g, int b, int a, int d, int s,
				 int ms, int sfc_type, int ver)
{
	EGLint num_conf, i;
	EGLBoolean ret;
	EGLConfig conf = 0, *conf_array = NULL;

	EGLint config_attribs[] = { EGL_RED_SIZE,
				    8, /*  0 */
				    EGL_GREEN_SIZE,
				    8, /*  2 */
				    EGL_BLUE_SIZE,
				    8, /*  4 */
				    EGL_ALPHA_SIZE,
				    8, /*  6 */
				    EGL_DEPTH_SIZE,
				    EGL_DONT_CARE, /*  8 */
				    EGL_STENCIL_SIZE,
				    EGL_DONT_CARE, /* 10 */
				    EGL_SAMPLES,
				    EGL_DONT_CARE, /* 12 */
				    EGL_SURFACE_TYPE,
				    EGL_WINDOW_BIT, /* 14 */
				    EGL_RENDERABLE_TYPE,
				    EGL_OPENGL_ES2_BIT,
				    EGL_NONE };

	config_attribs[1] = r;
	config_attribs[3] = g;
	config_attribs[5] = b;
	config_attribs[7] = a;
	config_attribs[9] = d;
	config_attribs[11] = s;
	config_attribs[13] = ms;
	config_attribs[15] = sfc_type; /* EGL_WINDOW_BIT/EGL_STREAM_BIT_KHR */

	switch (ver) {
	case 1:
	case 2:
		config_attribs[17] = EGL_OPENGL_ES2_BIT;
		break;
#if defined EGL_OPENGL_ES3_BIT
	case 3:
		config_attribs[17] = EGL_OPENGL_ES3_BIT;
		break;
#endif
	default:
		ELOG("%s\n", __FUNCTION__);
		goto exit;
	}

	ret = eglChooseConfig(s_dpy, config_attribs, NULL, 0, &num_conf);
	if (ret != EGL_TRUE || num_conf == 0) {
		ELOG("%s\n", __FUNCTION__);
		goto exit;
	}

	conf_array = (EGLConfig *)calloc(num_conf, sizeof(EGLConfig));
	if (conf_array == NULL) {
		ELOG("%s\n", __FUNCTION__);
		goto exit;
	}

	ret = eglChooseConfig(s_dpy, config_attribs, conf_array, num_conf,
			      &num_conf);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		goto exit;
	}

	for (i = 0; i < num_conf; i++) {
		EGLint id, rsize, gsize, bsize, asize;

		eglGetConfigAttrib(s_dpy, conf_array[i], EGL_CONFIG_ID, &id);
		eglGetConfigAttrib(s_dpy, conf_array[i], EGL_RED_SIZE, &rsize);
		eglGetConfigAttrib(s_dpy, conf_array[i], EGL_GREEN_SIZE,
				   &gsize);
		eglGetConfigAttrib(s_dpy, conf_array[i], EGL_BLUE_SIZE, &bsize);
		eglGetConfigAttrib(s_dpy, conf_array[i], EGL_ALPHA_SIZE,
				   &asize);

		DLOG("[%d] (%d, %d, %d, %d)\n", id, rsize, gsize, bsize, asize);

		if (rsize == r && gsize == g && bsize == b && asize == a) {
			conf = conf_array[i];
			break;
		}
	}

	if (i == num_conf) {
		ELOG("%s\n", __FUNCTION__);
		goto exit;
	}

exit:
	if (conf_array)
		free(conf_array);

	return conf;
}

int egl_init_with_platform_window_surface(int gles_version, int depth_size,
					  int stencil_size, int sample_num,
					  int *win_w, int *win_h, bool windowed)
{
	void *native_dpy, *native_win;
	EGLint major, minor;
	EGLConfig config;
	EGLBoolean ret;
	EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLint sfc_attr[] = { EGL_NONE };

	native_dpy = winsys_init_native_display();
	if ((native_dpy != EGL_DEFAULT_DISPLAY) && (native_dpy == NULL)) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	s_dpy = eglGetDisplay(native_dpy);
	if (s_dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	ret = eglInitialize(s_dpy, &major, &minor);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	config = find_egl_config(8, 8, 8, 8, depth_size, stencil_size,
				 sample_num, EGL_WINDOW_BIT, gles_version);
	if (config == NULL) {
		if (gles_version == 3) {
			WLOG("failed to find GLES3 configs. retry with GLES2.\n");
			gles_version = 2;
			config = find_egl_config(8, 8, 8, 8, depth_size,
						 stencil_size, sample_num,
						 EGL_WINDOW_BIT, gles_version);
		}
		if (config == NULL) {
			ELOG("%s\n", __FUNCTION__);
			return -1;
		}
	}

	native_win = winsys_init_native_window(s_dpy, win_w, win_h, windowed);
	if (native_win == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	s_sfc = eglCreateWindowSurface(s_dpy, config,
				       (NativeWindowType)native_win, sfc_attr);
	if (s_sfc == EGL_NO_SURFACE) {
		ELOG("%s\n", __FUNCTION__);
		return (-1);
	}

	switch (gles_version) {
	case 1:
		context_attribs[1] = 1;
		break;
	case 2:
		context_attribs[1] = 2;
		break;
	case 3:
		context_attribs[1] = 3;
		break;
	default:
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	s_ctx = eglCreateContext(s_dpy, config, EGL_NO_CONTEXT,
				 context_attribs);
	if (s_ctx == EGL_NO_CONTEXT) {
		ELOG("%s\n", __FUNCTION__);
		return (-1);
	}
	EGLASSERT();

	ret = eglMakeCurrent(s_dpy, s_sfc, s_sfc, s_ctx);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}
	EGLASSERT();

	return 0;
}

int egl_terminate()
{
	EGLBoolean ret;

	ret = eglMakeCurrent(s_dpy, NULL, NULL, NULL);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}
	EGLASSERT();

	ret = eglDestroySurface(s_dpy, s_sfc);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}
	EGLASSERT();

	ret = eglDestroyContext(s_dpy, s_ctx);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}
	EGLASSERT();

	ret = eglTerminate(s_dpy);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	return 0;
}

int egl_swap(bool vsync)
{
	EGLBoolean ret;

	ret = eglSwapBuffers(s_dpy, s_sfc);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	winsys_swap(vsync);
	return 0;
}

int egl_set_swap_interval(bool vsync)
{
	EGLBoolean ret;
	int interval = 0;
	if (vsync) {
		interval = 1;
	}
	ret = eglSwapInterval(s_dpy, interval);
	if (ret != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	return 0;
}

EGLDisplay egl_get_display()
{
	EGLDisplay dpy;

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
	}

	return dpy;
}

EGLContext egl_get_context()
{
	EGLDisplay dpy;
	EGLContext ctx;

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_CONTEXT;
	}

	ctx = eglGetCurrentContext();
	if (ctx == EGL_NO_CONTEXT) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_CONTEXT;
	}

	return ctx;
}

EGLSurface egl_get_surface()
{
	EGLDisplay dpy;
	EGLSurface sfc;

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_SURFACE;
	}

	sfc = eglGetCurrentSurface(EGL_DRAW);
	if (sfc == EGL_NO_SURFACE) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_SURFACE;
	}

	return sfc;
}

EGLConfig egl_get_config()
{
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig cfg;
	int cfg_id, ival;
	EGLint cfg_attribs[] = { EGL_CONFIG_ID, 0, EGL_NONE };

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_CONTEXT;
	}

	ctx = eglGetCurrentContext();
	if (ctx == EGL_NO_CONTEXT) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_CONTEXT;
	}

	eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &cfg_id);
	cfg_attribs[1] = cfg_id;
	if (eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &ival) != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return EGL_NO_CONTEXT;
	}

	return cfg;
}

int egl_show_current_context_attrib()
{
	EGLDisplay dpy;
	EGLContext ctx;
	EGLint ival;

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	ctx = eglGetCurrentContext();
	if (dpy == EGL_NO_CONTEXT) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	LOG("-------------------------------------------------\n");
	LOG("  CONTEXT ATTRIBUTE                              \n");
	LOG("-------------------------------------------------\n");

	LOG(" %-32s: ", "EGL_CONFIG_ID");
	ival = -1;
	eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_CONTEXT_CLIENT_TYPE");
	ival = -1;
	eglQueryContext(dpy, ctx, EGL_CONTEXT_CLIENT_TYPE, &ival);
	switch (ival) {
	case EGL_OPENGL_API:
		LOG("EGL_OPENGL_API\n");
		break;
	case EGL_OPENGL_ES_API:
		LOG("EGL_OPENGL_ES_API\n");
		break;
	case EGL_OPENVG_API:
		LOG("EGL_OPENVG_API\n");
		break;
	default:
		LOG("UNKNOWN\n");
		break;
	}

	LOG(" %-32s: ", "EGL_CONTEXT_CLIENT_VERSION");
	ival = -1;
	eglQueryContext(dpy, ctx, EGL_CONTEXT_CLIENT_VERSION, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_RENDER_BUFFER");
	ival = -1;
	eglQueryContext(dpy, ctx, EGL_RENDER_BUFFER, &ival);
	switch (ival) {
	case EGL_SINGLE_BUFFER:
		LOG("EGL_SINGLE_BUFFER\n");
		break;
	case EGL_BACK_BUFFER:
		LOG("EGL_BACK_BUFFER\n");
		break;
	default:
		LOG("UNKNOWN\n");
		break;
	}
	LOG("-------------------------------------------------\n");
	return 0;
}

int egl_show_current_config_attrib()
{
	EGLDisplay dpy;
	EGLContext ctx;
	EGLConfig cfg;
	EGLint ival;
	int cfg_id;
	EGLint cfg_attribs[] = { EGL_CONFIG_ID, 0, EGL_NONE };

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	ctx = eglGetCurrentContext();
	if (dpy == EGL_NO_CONTEXT) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	eglQueryContext(dpy, ctx, EGL_CONFIG_ID, &cfg_id);

	cfg_attribs[1] = cfg_id;
	if (eglChooseConfig(dpy, cfg_attribs, &cfg, 1, &ival) != EGL_TRUE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	LOG("-------------------------------------------------\n");
	LOG("  CONFIG ATTRIBUTE                               \n");
	LOG("-------------------------------------------------\n");

	LOG(" %-32s: ", "EGL_CONFIG_ID");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_ID, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_BUFFER_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_BUFFER_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_RED_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_RED_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_GREEN_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_GREEN_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_BLUE_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_BLUE_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_LUMINANCE_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_LUMINANCE_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_ALPHA_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_ALPHA_MASK_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_ALPHA_MASK_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_BIND_TO_TEXTURE_RGB");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_BIND_TO_TEXTURE_RGB, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_BIND_TO_TEXTURE_RGBA");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_BIND_TO_TEXTURE_RGBA, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_COLOR_BUFFER_TYPE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_COLOR_BUFFER_TYPE, &ival);
	switch (ival) {
	case EGL_RGB_BUFFER:
		LOG("EGL_RGB_BUFFER\n");
		break;
	case EGL_LUMINANCE_BUFFER:
		LOG("EGL_LUMINANCE_BUFFER\n");
		break;
	default:
		ELOG("unknown EGL_COLOR_BUFFER_TYPE\n");
	}

	LOG(" %-32s: ", "EGL_CONFIG_CAVEAT");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_CAVEAT, &ival);
	switch (ival) {
	case EGL_NONE:
		LOG("EGL_NONE\n");
		break;
	case EGL_SLOW_CONFIG:
		LOG("EGL_SLOW_CONFIG\n");
		break;
	case EGL_NON_CONFORMANT_CONFIG:
		LOG("EGL_NON_CONFORMANT_CONFIG\n");
		break;
	default:
		ELOG("unknown EGL_CONFIG_CAVEAT\n");
	}

	LOG(" %-32s: ", "EGL_CONFORMANT");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_CONFORMANT, &ival);
	LOG("%d\n", ival);
	if (ival & EGL_OPENGL_ES_BIT)
		LOG(" %-32s  + EGL_OPENGL_ES_BIT\n", "");
	if (ival & EGL_OPENGL_ES2_BIT)
		LOG(" %-32s  + EGL_OPENGL_ES2_BIT\n", "");

	LOG(" %-32s: ", "EGL_DEPTH_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_DEPTH_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_LEVEL");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_LEVEL, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_MATCH_NATIVE_PIXMAP");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_MATCH_NATIVE_PIXMAP, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_MAX_SWAP_INTERVAL");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_MAX_SWAP_INTERVAL, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_MIN_SWAP_INTERVAL");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_MIN_SWAP_INTERVAL, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_NATIVE_RENDERABLE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_RENDERABLE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_NATIVE_VISUAL_TYPE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_TYPE, &ival);
	switch (ival) {
	case EGL_NONE:
		LOG("EGL_NONE\n");
		break;
	default:
		ELOG("UNKNOWN(%d)\n", ival);
	}

	LOG(" %-32s: ", "EGL_RENDERABLE_TYPE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_RENDERABLE_TYPE, &ival);
	LOG("%d\n", ival);
	if (ival & EGL_OPENGL_ES_BIT)
		LOG(" %-32s  + EGL_OPENGL_ES_BIT\n", "");
	if (ival & EGL_OPENGL_ES2_BIT)
		LOG(" %-32s  + EGL_OPENGL_ES2_BIT\n", "");

	LOG(" %-32s: ", "EGL_SAMPLE_BUFFERS");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_SAMPLE_BUFFERS, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_SAMPLES");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_SAMPLES, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_STENCIL_SIZE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_STENCIL_SIZE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_SURFACE_TYPE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_SURFACE_TYPE, &ival);
	LOG("%d\n", ival);
	if (ival & EGL_PBUFFER_BIT)
		LOG(" %-32s  + EGL_PBUFFER_BIT\n", "");
	if (ival & EGL_PIXMAP_BIT)
		LOG(" %-32s  + EGL_PIXMAP_BIT\n", "");
	if (ival & EGL_WINDOW_BIT)
		LOG(" %-32s  + EGL_WINDOW_BIT\n", "");
	if (ival & EGL_MULTISAMPLE_RESOLVE_BOX_BIT)
		LOG(" %-32s  + EGL_MULTISAMPLE_RESOLVE_BOX_BIT\n", "");
	if (ival & EGL_SWAP_BEHAVIOR_PRESERVED_BIT)
		LOG(" %-32s  + EGL_SWAP_BEHAVIOR_PRESERVED_BIT\n", "");

	LOG(" %-32s: ", "EGL_TRANSPARENT_TYPE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_TRANSPARENT_TYPE, &ival);
	switch (ival) {
	case EGL_NONE:
		LOG("EGL_NONE\n");
		break;
	case EGL_TRANSPARENT_RGB:
		LOG("EGL_TRANSPARENT_RGB\n");
		break;
	default:
		ELOG("unknown EGL_TRANSPARENT_TYPE\n");
	}

	LOG(" %-32s: ", "EGL_TRANSPARENT_RED_VALUE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_TRANSPARENT_RED_VALUE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_TRANSPARENT_GREEN_VALUE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_TRANSPARENT_GREEN_VALUE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_TRANSPARENT_BLUE_VALUE");
	ival = -1;
	eglGetConfigAttrib(dpy, cfg, EGL_TRANSPARENT_BLUE_VALUE, &ival);
	LOG("%d\n", ival);
	LOG("-------------------------------------------------\n");

	return 0;
}

int egl_show_current_surface_attrib()
{
	EGLDisplay dpy;
	EGLSurface sfc;
	EGLint ival;

	dpy = eglGetCurrentDisplay();
	if (dpy == EGL_NO_DISPLAY) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	sfc = eglGetCurrentSurface(EGL_DRAW);
	if (sfc == EGL_NO_SURFACE) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	LOG("-------------------------------------------------\n");
	LOG("  SURFACE ATTRIBUTE                              \n");
	LOG("-------------------------------------------------\n");

	LOG(" %-32s: ", "EGL_CONFIG_ID");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_CONFIG_ID, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_WIDTH");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_WIDTH, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_HEIGHT");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_HEIGHT, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_LARGEST_PBUFFER");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_LARGEST_PBUFFER, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_MIPMAP_TEXTURE");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_MIPMAP_TEXTURE, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_MIPMAP_LEVEL");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_MIPMAP_LEVEL, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_TEXTURE_FORMAT");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_TEXTURE_FORMAT, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_TEXTURE_TARGET");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_TEXTURE_TARGET, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_MULTISMAPLE_RESOLVE");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_MULTISAMPLE_RESOLVE, &ival);
	switch (ival) {
	case EGL_MULTISAMPLE_RESOLVE_DEFAULT:
		LOG("EGL_MULTISAMPLE_RESOLVE_DEFAULT\n");
		break;
	case EGL_MULTISAMPLE_RESOLVE_BOX:
		LOG("EGL_MULTISAMPLE_RESOLVE_BOX\n");
		break;
	default:
		ELOG("unknown EGL_MULTISAMPLE_RESOLVE\n");
	}

	LOG(" %-32s: ", "EGL_RENDER_BUFFER");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_RENDER_BUFFER, &ival);
	switch (ival) {
	case EGL_BACK_BUFFER:
		LOG("EGL_BACK_BUFFER\n");
		break;
	case EGL_SINGLE_BUFFER:
		LOG("EGL_SINGLE_BUFFER\n");
		break;
	default:
		ELOG("unknown EGL_RENDER_BUFFER\n");
	}

	LOG(" %-32s: ", "EGL_SWAP_BEHAVIOR");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_SWAP_BEHAVIOR, &ival);
	switch (ival) {
	case EGL_BUFFER_PRESERVED:
		LOG("EGL_BUFFER_PRESERVED\n");
		break;
	case EGL_BUFFER_DESTROYED:
		LOG("EGL_BUFFER_DESTROYED\n");
		break;
	default:
		ELOG("unknown EGL_SWAP_BEHAVIOR\n");
	}

	LOG(" %-32s: ", "EGL_HORIZONTAL_RESOLUTION");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_HORIZONTAL_RESOLUTION, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_VERTICAL_RESOLUTION");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_VERTICAL_RESOLUTION, &ival);
	LOG("%d\n", ival);

	LOG(" %-32s: ", "EGL_PIXEL_ASPECT_RATIO");
	ival = -1;
	eglQuerySurface(dpy, sfc, EGL_PIXEL_ASPECT_RATIO, &ival);
	LOG("%d\n", ival);

	LOG("-------------------------------------------------\n");

	return 0;
}

int egl_show_gl_info()
{
	const unsigned char *str;

	LOG("============================================\n");
	LOG("    GL INFO\n");
	LOG("============================================\n");

	LOG("GL_VERSION      : %s\n", glGetString(GL_VERSION));
	LOG("GL_SL_VERSION   : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
	LOG("GL_VENDOR       : %s\n", glGetString(GL_VENDOR));
	LOG("GL_RENDERER     : %s\n", glGetString(GL_RENDERER));

	str = glGetString(GL_EXTENSIONS);
	LOG("GL_EXTENSIONS   :\n");
	{
		char *p = strtok((char *)str, " ");
		while (p) {
			LOG("                  %s\n", p);
			p = strtok(NULL, " ");
		}
	}
	return 0;
}
