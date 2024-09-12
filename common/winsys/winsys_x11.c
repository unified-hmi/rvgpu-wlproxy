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
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
//#include "winsys_x11.h"
#include "util_egl.h"
#include "util_log.h"
#include <pthread.h>
#include <poll.h>
#include <linux/input-event-codes.h>
#define UNUSED(x) (void)(x)

#include <X11/Xatom.h> // Make sure this header is included for XA_ATOM
static Display *s_xdpy = NULL;
static Window s_xwin;

static void (*s_motion_func)(int x, int y) = NULL;
static void (*s_button_func)(int button, int state, int x, int y) = NULL;
static void (*s_key_func)(int key, int state) = NULL;

static pthread_t s_event_thread;

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
	if (s_xdpy == NULL)
		return 0;

	struct pollfd fds[1];
	int x11_fd = ConnectionNumber(s_xdpy);
	fds[0].fd = x11_fd;
	fds[0].events = POLLIN;

	XEvent event;
	while (1) {
		int ret = poll(fds, 1, -1);
		if (fds[0].revents & POLLIN) {
			ret = XPending(s_xdpy);
			if (ret < 0) {
				continue;
			}
		}
		XNextEvent(s_xdpy, &event);
		switch (event.type) {
		case ButtonPress:
			if (s_button_func) {
				s_button_func(xbtn_to_btn(event.xbutton.button),
					      1, event.xbutton.x,
					      event.xbutton.y);
			}
			break;
		case ButtonRelease:
			if (s_button_func) {
				s_button_func(xbtn_to_btn(event.xbutton.button),
					      0, event.xbutton.x,
					      event.xbutton.y);
			}
			break;
		case MotionNotify:
			if (s_motion_func) {
				s_motion_func(event.xmotion.x, event.xmotion.y);
			}
			break;
		case KeyPress:
			if (s_key_func) {
				KeySym keysym = XKeycodeToKeysym(
					s_xdpy, event.xkey.keycode, 0);
				int keycode = xkeysym2keycode(keysym);
				s_key_func(keycode, 1);
			}
			break;
		case KeyRelease:
			if (s_key_func) {
				KeySym keysym = XKeycodeToKeysym(
					s_xdpy, event.xkey.keycode, 0);
				int keycode = xkeysym2keycode(keysym);
				s_key_func(keycode, 0);
			}
			break;
		default:
			/* Unknown event type, ignore it */
			break;
		}
	}
	return 0;
}

void *winsys_init_native_display(void)
{
	Display *xdpy = XOpenDisplay(NULL);
	if (xdpy == NULL) {
		ELOG("%s Can't open XDisplay\n", __FUNCTION__);
	}

	s_xdpy = xdpy;

	pthread_create(&s_event_thread, NULL, event_loop, NULL);
	return (void *)xdpy;
}

void *winsys_init_native_window(void *dpy, int *win_w, int *win_h, bool windowed)
{
	UNUSED(dpy); /* We use XDisplay instead of EGLDisplay. */
	Display *xdpy = s_xdpy;

	int screen = DefaultScreen(xdpy);
	unsigned long black = BlackPixel(xdpy, DefaultScreen(xdpy));
	unsigned long white = WhitePixel(xdpy, DefaultScreen(xdpy));

	Window root = RootWindow(xdpy, screen);

	Window xwin =
		XCreateSimpleWindow(xdpy, root,
				    0, 0, *win_w, *win_h, 1, black, white);

	XMapWindow(xdpy, xwin);
	XSelectInput(xdpy, xwin,
		     ButtonPressMask | ButtonReleaseMask | Button1MotionMask |
			     PointerMotionMask | KeyPressMask | KeyReleaseMask);
	XFlush(xdpy);

	s_xwin = xwin;

	return (void *)xwin;
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
