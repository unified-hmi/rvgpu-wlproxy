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


/*
 * EGL window system dependent module for X11.
 * At first, you need to set up environment as below.
 *  > sudo apt install libgles2-mesa-dev libegl1-mesa-dev xorg-dev
 */
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/Xlib-xcb.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "util_log.h"
#include <pthread.h>
#include <poll.h>
#include <linux/input-event-codes.h>
#include "util_egl.h"
#define UNUSED(x) (void)(x)

static xcb_connection_t *s_xcb_conn;
static void (*s_motion_func)(int x, int y) = NULL;
static void (*s_button_func)(int button, int state, int x, int y) = NULL;
static void (*s_key_func)(int key, int state) = NULL;

static pthread_t s_event_thread;

uint32_t xcb_window_attrib_mask = XCB_CW_EVENT_MASK;
uint32_t xcb_window_attrib_list[] = { XCB_EVENT_MASK_BUTTON_PRESS |
				      XCB_EVENT_MASK_BUTTON_RELEASE |
				      XCB_EVENT_MASK_POINTER_MOTION |
				      XCB_EVENT_MASK_KEY_PRESS |
				      XCB_EVENT_MASK_KEY_RELEASE };

static int xkeysym2keycode(KeySym keysym)
{
	switch (keysym) {
	case XK_space:
		return EGLU_KEY_SPACE;
	case XK_apostrophe:
		return EGLU_KEY_APOSTROPHE;
	case XK_asterisk:
		return EGLU_KEY_KPASTERISK;
	case XK_plus:
		return EGLU_KEY_KPPLUS;
	case XK_comma:
		return EGLU_KEY_COMMA;
	case XK_minus:
		return EGLU_KEY_MINUS;
	case XK_period:
		return EGLU_KEY_DOT;
	case XK_slash:
		return EGLU_KEY_SLASH;

	case XK_0:
		return EGLU_KEY_0;
	case XK_1:
		return EGLU_KEY_1;
	case XK_2:
		return EGLU_KEY_2;
	case XK_3:
		return EGLU_KEY_3;
	case XK_4:
		return EGLU_KEY_4;
	case XK_5:
		return EGLU_KEY_5;
	case XK_6:
		return EGLU_KEY_6;
	case XK_7:
		return EGLU_KEY_7;
	case XK_8:
		return EGLU_KEY_8;
	case XK_9:
		return EGLU_KEY_9;

	case XK_semicolon:
		return EGLU_KEY_SEMICOLON;
	case XK_equal:
		return EGLU_KEY_EQUAL;
	case XK_backslash:
		return EGLU_KEY_BACKSLASH;
	case XK_bracketleft:
		return EGLU_KEY_LEFTBRACE;
	case XK_bracketright:
		return EGLU_KEY_RIGHTBRACE;
	case XK_grave:
		return EGLU_KEY_GRAVE;

	case XK_a:
		return EGLU_KEY_A;
	case XK_b:
		return EGLU_KEY_B;
	case XK_c:
		return EGLU_KEY_C;
	case XK_d:
		return EGLU_KEY_D;
	case XK_e:
		return EGLU_KEY_E;
	case XK_f:
		return EGLU_KEY_F;
	case XK_g:
		return EGLU_KEY_G;
	case XK_h:
		return EGLU_KEY_H;
	case XK_i:
		return EGLU_KEY_I;
	case XK_j:
		return EGLU_KEY_J;
	case XK_k:
		return EGLU_KEY_K;
	case XK_l:
		return EGLU_KEY_L;
	case XK_m:
		return EGLU_KEY_M;
	case XK_n:
		return EGLU_KEY_N;
	case XK_o:
		return EGLU_KEY_O;
	case XK_p:
		return EGLU_KEY_P;
	case XK_q:
		return EGLU_KEY_Q;
	case XK_r:
		return EGLU_KEY_R;
	case XK_s:
		return EGLU_KEY_S;
	case XK_t:
		return EGLU_KEY_T;
	case XK_u:
		return EGLU_KEY_U;
	case XK_v:
		return EGLU_KEY_V;
	case XK_w:
		return EGLU_KEY_W;
	case XK_x:
		return EGLU_KEY_X;
	case XK_y:
		return EGLU_KEY_Y;
	case XK_z:
		return EGLU_KEY_Z;

	case XK_braceleft:
		return EGLU_KEY_LEFTBRACE;
	case XK_braceright:
		return EGLU_KEY_RIGHTBRACE;

	case XK_BackSpace:
		return EGLU_KEY_BACKSPACE;
	case XK_Tab:
		return EGLU_KEY_TAB;
	case XK_Return:
		return EGLU_KEY_ENTER;
	case XK_Pause:
		return EGLU_KEY_PAUSE;
	case XK_Scroll_Lock:
		return EGLU_KEY_SCROLLLOCK;
	case XK_Sys_Req:
		return EGLU_KEY_SYSRQ;
	case XK_Escape:
		return EGLU_KEY_ESC;
	case XK_Delete:
		return EGLU_KEY_DELETE;

	case XK_Muhenkan:
		return EGLU_KEY_MUHENKAN;
	case XK_Henkan:
		return EGLU_KEY_HENKAN;
	case XK_Hiragana:
		return EGLU_KEY_HIRAGANA;
	case XK_Katakana:
		return EGLU_KEY_KATAKANA;
	case XK_Hiragana_Katakana:
		return EGLU_KEY_KATAKANAHIRAGANA;
	case XK_Zenkaku_Hankaku:
		return EGLU_KEY_ZENKAKUHANKAKU;

	case XK_Home:
		return EGLU_KEY_HOME;
	case XK_Left:
		return EGLU_KEY_LEFT;
	case XK_Up:
		return EGLU_KEY_UP;
	case XK_Right:
		return EGLU_KEY_RIGHT;
	case XK_Down:
		return EGLU_KEY_DOWN;
	case XK_Page_Up:
		return EGLU_KEY_PAGEUP;
	case XK_Page_Down:
		return EGLU_KEY_PAGEDOWN;
	case XK_End:
		return EGLU_KEY_END;

	case XK_Num_Lock:
		return EGLU_KEY_NUMLOCK;
	case XK_Shift_L:
		return EGLU_KEY_LEFTSHIFT;
	case XK_Shift_R:
		return EGLU_KEY_RIGHTSHIFT;
	case XK_Control_L:
		return EGLU_KEY_LEFTCTRL;
	case XK_Control_R:
		return EGLU_KEY_RIGHTCTRL;
	case XK_Caps_Lock:
		return EGLU_KEY_CAPSLOCK;
	case XK_Alt_L:
		return EGLU_KEY_LEFTALT;
	case XK_Alt_R:
		return EGLU_KEY_RIGHTALT;
	}
	return 0;
}

int xbtn_to_btn(unsigned int xbutton)
{
	uint32_t button = 0;
	switch (xbutton) {
	case 1:
		button = BTN_LEFT;
		break;
	case 2:
		button = BTN_MIDDLE;
		break;
	case 3:
		button = BTN_RIGHT;
		break;
	default:
		/* Unknown event type, ignore it */
		break;
	}
	return button;
}

static void *event_loop()
{
	if (s_xcb_conn == NULL)
		return 0;
	int xcb_fd = xcb_get_file_descriptor(s_xcb_conn);
	struct pollfd fds[1];
	fds[0].fd = xcb_fd;
	fds[0].events = POLLIN;

	xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(s_xcb_conn);
	if (!keysyms) {
		ELOG("Failed to alloc key symbols\n");
		return 0;
	}

	xcb_generic_event_t *event;

	while (1) {
		if (poll(fds, 1, -1) == -1) {
			ELOG("xcb fd poll failed\n");
			continue;
		}

		while (fds[0].revents & POLLIN &&
		       (event = xcb_poll_for_event(s_xcb_conn))) {
			switch (event->response_type & ~0x80) {
			case XCB_KEY_PRESS: {
				xcb_key_press_event_t *kp =
					(xcb_key_press_event_t *)event;
				DLOG("XCB_KEY_PRESS (%d)\n", kp->detail);
				xcb_keysym_t keysym =
					xcb_key_symbols_get_keysym(
						keysyms, kp->detail, 0);
				int keycode = xkeysym2keycode(keysym);
				if (s_key_func) {
					s_key_func(keycode, 1);
				}
				break;
			}
			case XCB_KEY_RELEASE: {
				xcb_key_release_event_t *kr =
					(xcb_key_release_event_t *)event;
				DLOG("XCB_KEY_RELEASE (%d)\n", kr->detail);
				xcb_keysym_t keysym =
					xcb_key_symbols_get_keysym(
						keysyms, kr->detail, 0);
				int keycode = xkeysym2keycode(keysym);
				if (s_key_func) {
					s_key_func(keycode, 0);
				}
				break;
			}
			case XCB_BUTTON_PRESS: {
				xcb_button_press_event_t *bp =
					(xcb_button_press_event_t *)event;
				DLOG("XCB_BUTTON_PRESS (%d,%d)\n", bp->event_x,
				     bp->event_y);

				if (s_button_func) {
					s_button_func(xbtn_to_btn(bp->detail),
						      1, bp->event_x,
						      bp->event_y);
				}
				break;
			}
			case XCB_BUTTON_RELEASE: {
				xcb_button_release_event_t *br =
					(xcb_button_release_event_t *)event;
				DLOG("XCB_BUTTON_RELEASE (%d,%d)\n",
				     br->event_x, br->event_y);

				if (s_button_func) {
					s_button_func(xbtn_to_btn(br->detail),
						      0, br->event_x,
						      br->event_y);
				}
				break;
			}
			case XCB_MOTION_NOTIFY: {
				xcb_motion_notify_event_t *mn =
					(xcb_motion_notify_event_t *)event;
				DLOG("XCB_POINTER_MOTION: detail(%d) (%d, %d) stat(%d)\n",
				     mn->detail, mn->event_x, mn->event_y,
				     mn->state);

				if (s_motion_func) {
					s_motion_func(mn->event_x, mn->event_y);
				}
				break;
			}
			default:
				/* Unknown event type, ignore it */
				break;
			}

			free(event);
		}
	}
	xcb_key_symbols_free(keysyms);

	return 0;
}

void *winsys_init_native_display(void)
{
	Display *xdpy = XOpenDisplay(NULL);
	if (xdpy == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	xcb_connection_t *connection = XGetXCBConnection(xdpy);
	if (!connection) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	/* Use the XCB event-handling functions. (not the Xlib) */
	XSetEventQueueOwner(xdpy, XCBOwnsEventQueue);

	if (xcb_connection_has_error(connection)) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	s_xcb_conn = connection;

	pthread_create(&s_event_thread, NULL, event_loop, NULL);
	return (void *)xdpy;
}

void *winsys_init_native_window(void *dpy, int win_w, int win_h)
{
	UNUSED(dpy);

	const xcb_setup_t *setup = xcb_get_setup(s_xcb_conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);

	xcb_screen_t *screen = iter.data;
	if (screen == 0) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	xcb_window_t window = xcb_generate_id(s_xcb_conn);
	if (window <= 0) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	xcb_void_cookie_t create_cookie = xcb_create_window_checked(
		s_xcb_conn,
		XCB_COPY_FROM_PARENT, // depth
		window,
		screen->root, // parent window
		0, 0, win_w, win_h, // x, y, w, h
		0, // border width
		XCB_WINDOW_CLASS_INPUT_OUTPUT, // class
		screen->root_visual, // visual
		xcb_window_attrib_mask, xcb_window_attrib_list);

	xcb_void_cookie_t map_cookie =
		xcb_map_window_checked(s_xcb_conn, window);

	/* Check errors. */
	if (xcb_request_check(s_xcb_conn, create_cookie)) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	if (xcb_request_check(s_xcb_conn, map_cookie)) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	xcb_flush(s_xcb_conn);

	return (void *)(uintptr_t)window;
}

int winsys_swap(bool vsync)
{
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
