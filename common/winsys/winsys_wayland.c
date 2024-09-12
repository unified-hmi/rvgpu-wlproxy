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
#include <sys/mman.h>
#include <signal.h>
#include "wayland-client.h"
#include "wayland-egl.h"
#include "util_log.h"
#include "winsys_wayland.h"
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#define UNUSED(x) (void)(x)

struct Display s_display;
struct Window s_window;

static void (*s_motion_func)(int x, int y) = NULL;
static void (*s_button_func)(int button, int state, int x, int y) = NULL;
static void (*s_key_func)(int key, int state) = NULL;
static void (*s_touch_down_func)(int32_t id, int32_t x, int32_t y) = NULL;
static void (*s_touch_up_func)(int32_t id) = NULL;
static void (*s_touch_motion_func)(int32_t id, int32_t x, int32_t y) = NULL;

static pthread_t s_event_thread;

static void handle_ping(void *data, struct wl_shell_surface *wlShellSurface,
			uint32_t serial)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(data);

	wl_shell_surface_pong(wlShellSurface, serial);
}

static void handle_configure(void *data, struct wl_shell_surface *shell_surface,
			     uint32_t edges, int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
	struct Window *window = data;
	UNUSED(data);
	UNUSED(shell_surface);
	UNUSED(edges);

	if (window->wlEGLNativeWindow) {
		wl_egl_window_resize(window->wlEGLNativeWindow, width, height,
				     0, 0);
	}

	window->geometry.width = width;
	window->geometry.height = height;

	if (!window->fullscreen) {
		window->window_size = window->geometry;
	}
}

static void popup_done(void *data, struct wl_shell_surface *shell_surface)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_ping, handle_configure, popup_done
};

static void configure_callback(void *data, struct wl_callback *callback,
			       uint32_t time)
{
	DLOG("%s\n", __FUNCTION__);
	struct Window *window = data;
	UNUSED(time);

	wl_callback_destroy(callback);

	window->configured = 1;
}

static struct wl_callback_listener configure_callback_listener = {
	configure_callback,
};

static void toggle_fullscreen(struct Window *window, int fullscreen)
{
	DLOG("%s\n", __FUNCTION__);
	struct wl_callback *callback;

	window->fullscreen = fullscreen;
	window->configured = 0;

	if (fullscreen) {
		wl_shell_surface_set_fullscreen(
			window->wlShellSurface,
			WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 0, NULL);
	} else {
		wl_shell_surface_set_toplevel(window->wlShellSurface);
		handle_configure(window, window->wlShellSurface, 0,
				 window->window_size.width,
				 window->window_size.height);
	}

	callback = wl_display_sync(window->display->wlDisplay);
	wl_callback_add_listener(callback, &configure_callback_listener,
				 window);
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer,
				 uint32_t serial, struct wl_surface *surface,
				 wl_fixed_t sx, wl_fixed_t sy)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(pointer);
	UNUSED(serial);
	UNUSED(surface);
	struct Display *dpy = (struct Display *)data;
	dpy->cur_pointer_x = (sx >> 8);
	dpy->cur_pointer_y = (sx >> 8);

	if (s_motion_func) {
		s_motion_func(dpy->cur_pointer_x, dpy->cur_pointer_y);
	}
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer,
				 uint32_t serial, struct wl_surface *surface)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(data);
	UNUSED(pointer);
	UNUSED(serial);
	UNUSED(surface);

	if (s_motion_func) {
		s_motion_func(-1, -1);
	}
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer,
				  uint32_t time, wl_fixed_t sx_w,
				  wl_fixed_t sy_w)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(pointer);
	UNUSED(time);
	struct Display *dpy = (struct Display *)data;
	dpy->cur_pointer_x = (sx_w >> 8);
	dpy->cur_pointer_y = (sy_w >> 8);
	if (s_motion_func) {
		s_motion_func(dpy->cur_pointer_x, dpy->cur_pointer_y);
	}
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
				  uint32_t serial, uint32_t time,
				  uint32_t button, uint32_t state)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(wl_pointer);
	UNUSED(serial);
	UNUSED(time);
	struct Display *dpy = (struct Display *)data;
	if (s_button_func) {
		s_button_func(button, state, dpy->cur_pointer_x,
			      dpy->cur_pointer_y);
	}
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
				uint32_t time, uint32_t axis, wl_fixed_t value)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(data);
	UNUSED(wl_pointer);
	UNUSED(time);
	UNUSED(axis);
	UNUSED(value);
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,  pointer_handle_leave, pointer_handle_motion,
	pointer_handle_button, pointer_handle_axis,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
				   uint32_t format, int fd, uint32_t size)
{
	DLOG("%s\n", __FUNCTION__);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard,
				  uint32_t serial, struct wl_surface *surface,
				  struct wl_array *keys)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(data);
	UNUSED(keyboard);
	UNUSED(serial);
	UNUSED(surface);
	UNUSED(keys);
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard,
				  uint32_t serial, struct wl_surface *surface)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(data);
	UNUSED(keyboard);
	UNUSED(serial);
	UNUSED(surface);
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
				uint32_t serial, uint32_t time, uint32_t key,
				uint32_t state)
{
	DLOG("%s\n", __FUNCTION__);
	if (s_key_func) {
		s_key_func(key, state);
	}
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard,
				      uint32_t serial, uint32_t mods_depressed,
				      uint32_t mods_latched,
				      uint32_t mods_locked, uint32_t group)
{
	DLOG("%s\n", __FUNCTION__);
}

static void keyboard_handle_repeat_info(void *data,
					struct wl_keyboard *keyboard,
					int32_t rate, int32_t delay)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,	   keyboard_handle_enter,
	keyboard_handle_leave,	   keyboard_handle_key,
	keyboard_handle_modifiers, keyboard_handle_repeat_info
};

static void touch_handle_down(void *data, struct wl_touch *wl_touch,
			      uint32_t serial, uint32_t time,
			      struct wl_surface *surface, int32_t id,
			      wl_fixed_t sx_w, wl_fixed_t sy_w)
{
	DLOG("%s\n", __FUNCTION__);
	int32_t x = (sx_w >> 8);
	int32_t y = (sy_w >> 8);
	if (s_touch_down_func) {
		s_touch_down_func(id, x, y);
	}
}

static void touch_handle_up(void *data, struct wl_touch *wl_touch,
			    uint32_t serial, uint32_t time, int32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	if (s_touch_up_func) {
		s_touch_up_func(id);
	}
}

static void touch_handle_motion(void *data, struct wl_touch *wl_touch,
				uint32_t time, int32_t id, wl_fixed_t sx_w,
				wl_fixed_t sy_w)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(time);

	int32_t x = (sx_w >> 8);
	int32_t y = (sy_w >> 8);
	if (s_touch_motion_func) {
		s_touch_motion_func(id, x, y);
	}
}

static void touch_handle_frame(void *data, struct wl_touch *wl_touch)
{
	DLOG("%s\n", __FUNCTION__);
}

static void touch_handle_cancel(void *data, struct wl_touch *wl_touch)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_touch_listener touch_listener = {
	touch_handle_down,  touch_handle_up,	 touch_handle_motion,
	touch_handle_frame, touch_handle_cancel,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat,
				     enum wl_seat_capability caps)
{
	DLOG("%s\n", __FUNCTION__);
	struct Display *d = data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !d->pointer) {
		d->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(d->pointer, &pointer_listener, d);
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !d->keyboard) {
		d->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(d->keyboard, &keyboard_listener, d);
	}

	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !d->touch) {
		d->touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(d->touch, &touch_listener, d);
	}
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_seat_listener seat_listener = { seat_handle_capabilities,
						       seat_handle_name };

// Registry handling static function
static void registry_handle_global(void *data, struct wl_registry *registry,
				   uint32_t name, const char *interface,
				   uint32_t version)
{
	struct Display *d = data;
	UNUSED(version);

	DLOG("interface=%s\n", interface);
	if (strcmp(interface, "wl_compositor") == 0) {
		d->wlCompositor = wl_registry_bind(registry, name,
						   &wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_shell") == 0) {
		d->wlShell = wl_registry_bind(registry, name,
					      &wl_shell_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		d->wlSeat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
		wl_seat_add_listener(d->wlSeat, &seat_listener, d);
	}
}

static void registry_handle_global_remove(void *data,
					  struct wl_registry *registry,
					  uint32_t name)
{
	DLOG("%s\n", __FUNCTION__);
	UNUSED(data);
	UNUSED(registry);
	UNUSED(name);
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global, registry_handle_global_remove
};

static void *event_loop()
{
	int wl_fd = wl_display_get_fd(s_display.wlDisplay);
	int ret;

	while (1) {
		ret = wl_display_prepare_read(s_display.wlDisplay);
		if (ret == -1) {
			wl_display_dispatch_pending(s_display.wlDisplay);
			wl_display_flush(s_display.wlDisplay);
			continue;
		}

		struct pollfd pfd[1];
		pfd[0].fd = wl_fd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		ret = poll(pfd, 1, -1);

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("Poll error");
			wl_display_cancel_read(s_display.wlDisplay);
			break;
		}
		if (pfd[0].revents & POLLIN) {
			ret = wl_display_read_events(s_display.wlDisplay);
			if (ret == -1) {
				perror("wl_display_read_events error");
				break;
			}
			wl_display_dispatch_pending(s_display.wlDisplay);
		} else {
			wl_display_cancel_read(s_display.wlDisplay);
		}
	}
	return 0;
}

void *winsys_init_native_display(void)
{
	memset(&s_display, 0, sizeof(s_display));

	s_display.wlDisplay = wl_display_connect(NULL);
	if (s_display.wlDisplay == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return 0;
	}

	s_display.wlRegistry = wl_display_get_registry(s_display.wlDisplay);
	if (s_display.wlRegistry == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return 0;
	}

	wl_registry_add_listener(s_display.wlRegistry, &registry_listener,
				 &s_display);
	pthread_create(&s_event_thread, NULL, event_loop, NULL);

	wl_display_dispatch(s_display.wlDisplay);

	return s_display.wlDisplay;
	;
}

void *winsys_init_native_window(void *dpy, int *win_w, int *win_h, bool windowed)
{
	UNUSED(dpy);
	memset(&s_window, 0, sizeof(s_window));

	if (!s_display.wlCompositor || !s_display.wlShell) {
		ELOG("%s\n", __FUNCTION__);
		return 0;
	}

	s_window.wlSurface =
		wl_compositor_create_surface(s_display.wlCompositor);
	if (s_window.wlSurface == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return 0;
	}

	s_window.wlShellSurface = wl_shell_get_shell_surface(
		s_display.wlShell, s_window.wlSurface);
	if (s_window.wlShellSurface == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return 0;
	}

	wl_shell_surface_add_listener(s_window.wlShellSurface,
				      &shell_surface_listener, &s_window);

	s_window.window_size.width = *win_w;
	s_window.window_size.height = *win_h;
	s_window.display = &s_display;
	toggle_fullscreen(&s_window, 0);

	s_window.wlEGLNativeWindow =
		wl_egl_window_create(s_window.wlSurface, *win_w, *win_h);
	if (s_window.wlEGLNativeWindow == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return 0;
	}

	return s_window.wlEGLNativeWindow;
}

int winsys_swap(bool vsync)
{
	if (vsync) {
		wl_display_dispatch(s_display.wlDisplay);
	} else {
		wl_display_dispatch_pending(s_display.wlDisplay);
	}
	return 0;
}

void *winsys_create_native_pixmap(int width, int height)
{
	return NULL;
}

void egl_set_motion_func(void (*func)(int x, int y))
{
	s_motion_func = func;
}

void egl_set_button_func(void (*func)(int button, int state, int x, int y))
{
	s_button_func = func;
}

void egl_set_key_func(void (*func)(int key, int state))
{
	s_key_func = func;
}

void egl_set_touch_down_func(void (*func)(int id, int x, int y))
{
	s_touch_down_func = func;
}

void egl_set_touch_up_func(void (*func)(int id))
{
	s_touch_up_func = func;
}

void egl_set_touch_motion_func(void (*func)(int id, int x, int y))
{
	s_touch_motion_func = func;
}
