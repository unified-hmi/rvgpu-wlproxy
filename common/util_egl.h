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

#ifndef _UTIL_EGL_H_
#define _UTIL_EGL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "util_log.h"
#include "util_extension.h"
#include <stdbool.h>

int egl_init_with_platform_window_surface(int gles_version, int depth_size,
					  int stencil_size, int sample_num,
					  int win_w, int win_h);
int egl_terminate();
int egl_swap(bool vsync);
int egl_set_swap_interval(bool vsync);

int egl_get_current_surface_dimension(int *width, int *height);

int egl_show_current_context_attrib();
int egl_show_current_config_attrib();
int egl_show_current_surface_attrib();
int egl_show_gl_info();

void egl_set_motion_func(void (*func)(int x, int y));
void egl_set_button_func(void (*func)(int button, int state, int x, int y));
void egl_set_key_func(void (*func)(int key, int state));
void egl_set_touch_down_func(void (*func)(int id, int x, int y));
void egl_set_touch_up_func(void (*func)(int id));
void egl_set_touch_motion_func(void (*func)(int id, int x, int y));

EGLDisplay egl_get_display();
EGLContext egl_get_context();
EGLSurface egl_get_surface();
EGLConfig egl_get_config();

#define EGL_GET_PROC_ADDR(name)                                                \
	do {                                                                   \
		name = (void *)eglGetProcAddress(#name);                       \
		if (!name) {                                                   \
			ELOG("%s\n", __FUNCTION__);                            \
		}                                                              \
	} while (0)

#define EGLU_KEY_RESERVED 0
#define EGLU_KEY_ESC 1
#define EGLU_KEY_1 2
#define EGLU_KEY_2 3
#define EGLU_KEY_3 4
#define EGLU_KEY_4 5
#define EGLU_KEY_5 6
#define EGLU_KEY_6 7
#define EGLU_KEY_7 8
#define EGLU_KEY_8 9
#define EGLU_KEY_9 10
#define EGLU_KEY_0 11
#define EGLU_KEY_MINUS 12
#define EGLU_KEY_EQUAL 13
#define EGLU_KEY_BACKSPACE 14
#define EGLU_KEY_TAB 15
#define EGLU_KEY_Q 16
#define EGLU_KEY_W 17
#define EGLU_KEY_E 18
#define EGLU_KEY_R 19
#define EGLU_KEY_T 20
#define EGLU_KEY_Y 21
#define EGLU_KEY_U 22
#define EGLU_KEY_I 23
#define EGLU_KEY_O 24
#define EGLU_KEY_P 25
#define EGLU_KEY_LEFTBRACE 26
#define EGLU_KEY_RIGHTBRACE 27
#define EGLU_KEY_ENTER 28
#define EGLU_KEY_LEFTCTRL 29
#define EGLU_KEY_A 30
#define EGLU_KEY_S 31
#define EGLU_KEY_D 32
#define EGLU_KEY_F 33
#define EGLU_KEY_G 34
#define EGLU_KEY_H 35
#define EGLU_KEY_J 36
#define EGLU_KEY_K 37
#define EGLU_KEY_L 38
#define EGLU_KEY_SEMICOLON 39
#define EGLU_KEY_APOSTROPHE 40
#define EGLU_KEY_GRAVE 41
#define EGLU_KEY_LEFTSHIFT 42
#define EGLU_KEY_BACKSLASH 43
#define EGLU_KEY_Z 44
#define EGLU_KEY_X 45
#define EGLU_KEY_C 46
#define EGLU_KEY_V 47
#define EGLU_KEY_B 48
#define EGLU_KEY_N 49
#define EGLU_KEY_M 50
#define EGLU_KEY_COMMA 51
#define EGLU_KEY_DOT 52
#define EGLU_KEY_SLASH 53
#define EGLU_KEY_RIGHTSHIFT 54
#define EGLU_KEY_KPASTERISK 55
#define EGLU_KEY_LEFTALT 56
#define EGLU_KEY_SPACE 57
#define EGLU_KEY_CAPSLOCK 58
#define EGLU_KEY_F1 59
#define EGLU_KEY_F2 60
#define EGLU_KEY_F3 61
#define EGLU_KEY_F4 62
#define EGLU_KEY_F5 63
#define EGLU_KEY_F6 64
#define EGLU_KEY_F7 65
#define EGLU_KEY_F8 66
#define EGLU_KEY_F9 67
#define EGLU_KEY_F10 68
#define EGLU_KEY_NUMLOCK 69
#define EGLU_KEY_SCROLLLOCK 70
#define EGLU_KEY_KP7 71
#define EGLU_KEY_KP8 72
#define EGLU_KEY_KP9 73
#define EGLU_KEY_KPMINUS 74
#define EGLU_KEY_KP4 75
#define EGLU_KEY_KP5 76
#define EGLU_KEY_KP6 77
#define EGLU_KEY_KPPLUS 78
#define EGLU_KEY_KP1 79
#define EGLU_KEY_KP2 80
#define EGLU_KEY_KP3 81
#define EGLU_KEY_KP0 82
#define EGLU_KEY_KPDOT 83

#define EGLU_KEY_ZENKAKUHANKAKU 85
#define EGLU_KEY_102ND 86
#define EGLU_KEY_F11 87
#define EGLU_KEY_F12 88
#define EGLU_KEY_RO 89
#define EGLU_KEY_KATAKANA 90
#define EGLU_KEY_HIRAGANA 91
#define EGLU_KEY_HENKAN 92
#define EGLU_KEY_KATAKANAHIRAGANA 93
#define EGLU_KEY_MUHENKAN 94
#define EGLU_KEY_KPJPCOMMA 95
#define EGLU_KEY_KPENTER 96
#define EGLU_KEY_RIGHTCTRL 97
#define EGLU_KEY_KPSLASH 98
#define EGLU_KEY_SYSRQ 99
#define EGLU_KEY_RIGHTALT 100
#define EGLU_KEY_LINEFEED 101
#define EGLU_KEY_HOME 102
#define EGLU_KEY_UP 103
#define EGLU_KEY_PAGEUP 104
#define EGLU_KEY_LEFT 105
#define EGLU_KEY_RIGHT 106
#define EGLU_KEY_END 107
#define EGLU_KEY_DOWN 108
#define EGLU_KEY_PAGEDOWN 109
#define EGLU_KEY_INSERT 110
#define EGLU_KEY_DELETE 111
#define EGLU_KEY_MACRO 112
#define EGLU_KEY_MUTE 113
#define EGLU_KEY_VOLUMEDOWN 114
#define EGLU_KEY_VOLUMEUP 115
#define EGLU_KEY_POWER 116
#define EGLU_KEY_KPEQUAL 117
#define EGLU_KEY_KPPLUSMINUS 118
#define EGLU_KEY_PAUSE 119
#define EGLU_KEY_SCALE 120

#ifdef __cplusplus
}
#endif
#endif
