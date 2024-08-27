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

#ifndef UTIL_WAYLAND_H_
#define UTIL_WAYLAND_H_

struct Window;
struct Display {
	struct wl_display *wlDisplay;
	struct wl_registry *wlRegistry;
	struct wl_compositor *wlCompositor;
	struct wl_shell *wlShell;
	struct wl_seat *wlSeat;
	struct wl_pointer *pointer;
	struct wl_touch *touch;
	struct wl_keyboard *keyboard;
	uint32_t modifiers;
	uint32_t serial;
	struct sigaction sigint;
	struct Window *window;

	uint32_t cur_pointer_x;
	uint32_t cur_pointer_y;
};

struct Geometry {
	int width, height;
};

struct Window {
	struct Display *display;
	struct wl_egl_window *wlEGLNativeWindow;
	struct wl_surface *wlSurface;
	struct wl_shell_surface *wlShellSurface;
	struct wl_callback *callback;
	int fullscreen, configured, opaque;
	struct Geometry geometry, window_size;
};

#endif
