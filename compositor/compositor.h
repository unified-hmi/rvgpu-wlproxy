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

#ifndef COMPOSITOR_H_
#define COMPOSITOR_H_

#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>
#include <GLES2/gl2.h>
#include <pthread.h>

#define TEX_PLANE_NUM 2

struct xkb_info {
	struct xkb_keymap *keymap;
	int fd;
	size_t size;
	int32_t ref_count;
	xkb_mod_index_t shift_mod;
	xkb_mod_index_t caps_mod;
	xkb_mod_index_t ctrl_mod;
	xkb_mod_index_t alt_mod;
	xkb_mod_index_t mod2_mod;
	xkb_mod_index_t mod3_mod;
	xkb_mod_index_t super_mod;
	xkb_mod_index_t mod5_mod;
	xkb_led_index_t num_led;
	xkb_led_index_t caps_led;
	xkb_led_index_t scroll_led;
};

typedef struct {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *pointer_resource;
	struct wl_resource *touch_resource;
	struct wl_resource *keyboard_resource;
	struct wl_listener destroy_listener;
} client_data;

typedef struct compositor {
	struct wl_resource *resource;
	struct wl_display *wl_display;
	struct wl_global *wl_shell;
	struct wl_list client_list;
	struct wl_list surface_list;
	pthread_mutex_t event_mutex;
	int width; /* compositor width  */
	int height; /* compositor height */
	bool sfc_fullscreen;
	struct xkb_state *xkb_state;
	struct {
		uint32_t mods_depressed;
		uint32_t mods_latched;
		uint32_t mods_locked;
		uint32_t group;
	} modifiers;
	struct xkb_rule_names xkb_names;
	struct xkb_context *xkb_context;
	struct xkb_info *xkb_info;
} compositor;

/* wl_compositor_create_surface() */
typedef struct compositor_surface {
	struct wl_resource *resource;
	struct wl_list link;
	struct wl_client *client;
	compositor *compositor;
	struct shell_surface *shell_surface;
	struct wl_list pending_frame_callback_list;
	struct wl_list frame_callback_list;

	struct wl_resource *wl_buffer;
	struct wl_resource *wl_used_buffer;
	EGLImageKHR eglImg;
	GLuint texid[2];
	int status[2];
	GLsync glsyncobj_tex;
	int current_tex_index;
	int updated_tex_index;
	int img_w; /* shm_buffer width      */
	int img_h; /* shm_buffer height     */
	bool pointer_focused;
	bool keyboard_focused;
} compositor_surface;

typedef struct compositor_region {
	struct wl_resource *resource;
} compositor_region;

typedef struct shell_client {
	struct wl_resource *resource;
	compositor *compositor;
	struct wl_client *client;
	struct wl_list surface_list;
} shell_client;

struct toplevel_state {
	bool maximized;
	bool fullscreen;
	bool resizing;
	bool activated;
};

struct compositor_size {
	int32_t width, height;
};

typedef struct xdg_toplevel {
	struct wl_resource *resource;
	struct {
		struct toplevel_state state;
		struct compositor_size size;
	} pending;
} xdg_toplevel;

typedef struct shell_surface {
	struct wl_resource *resource;
	compositor_surface *csfc;
	xdg_toplevel *toplevel;
	bool added;
} shell_surface;

typedef struct compositor_frame_callback {
	struct wl_resource *resource;
	struct wl_list link;
} compositor_frame_callback;

void compositor_seat_init(compositor *compositor);

#define UNUSED(x) (void)(x)

#endif /* COMPOSITOR_H_ */
