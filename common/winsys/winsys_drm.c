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
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <libudev.h>
#include "util_log.h"
#include "winsys_drm.h"
#include "util_env.h"
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <systemd/sd-event.h>
#include <pthread.h>
#include <stdbool.h>

#define UNUSED(x) (void)(x)

static int s_drm_fd;
struct gbm_device *s_gbm;
struct gbm_surface *s_gbm_sfc;
modeset_dev_t *modeset_list = NULL;
modeset_dev_t *s_modeset_dev = NULL;
bool async_flip = false;

static struct libinput *s_libinput = NULL;
static pthread_t s_input_thread;

static double s_cursor_pos[2] = { 0, 0 };

static void (*s_motion_func)(int x, int y) = NULL;
static void (*s_button_func)(int button, int state, int x, int y) = NULL;
static void (*s_key_func)(int key, int state) = NULL;
static void (*s_touch_down_func)(int32_t id, int32_t x, int32_t y) = NULL;
static void (*s_touch_up_func)(int32_t id) = NULL;
static void (*s_touch_motion_func)(int32_t id, int32_t x, int32_t y) = NULL;

static void on_device_added(struct libinput_event *event)
{
	struct libinput_device *dev = libinput_event_get_device(event);
	ILOG("device_name: \"%s\"\n", libinput_device_get_name(dev));
	if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
		libinput_device_config_accel_set_profile(
			dev, LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
		libinput_device_config_accel_set_speed(dev, 0.0);
	}
}

static void on_keyboard_key(struct libinput_event *event)
{
	struct libinput_event_keyboard *ev =
		libinput_event_get_keyboard_event(event);

	uint32_t key = libinput_event_keyboard_get_key(ev);
	int state = libinput_event_keyboard_get_key_state(ev);
	int seat_key_count = libinput_event_keyboard_get_seat_key_count(ev);

	/* Ignore key events that are not seat wide state changes. */
	if ((state == LIBINPUT_KEY_STATE_PRESSED && seat_key_count != 1) ||
	    (state == LIBINPUT_KEY_STATE_RELEASED && seat_key_count != 0))
		return;

	if (s_key_func) {
		s_key_func(key, state);
	}
}

static void on_pointer_motion(struct libinput_event *event)
{
	struct libinput_event_pointer *ev =
		libinput_event_get_pointer_event(event);
	double dx = libinput_event_pointer_get_dx_unaccelerated(ev);
	double dy = libinput_event_pointer_get_dy_unaccelerated(ev);

	double px = s_cursor_pos[0] + dx;
	double py = s_cursor_pos[1] + dy;
	if (px < 0)
		px = 0;
	if (py < 0)
		py = 0;

	struct modeset_dev *dev = s_modeset_dev;
	if (dev) {
		double dw = (double)dev->width; /* display width  */
		double dh = (double)dev->height; /* display height */
		if (px > dw - 1)
			px = dw - 1;
		if (py > dh - 1)
			py = dh - 1;
	}

	s_cursor_pos[0] = px;
	s_cursor_pos[1] = py;

	if (s_motion_func) {
		s_motion_func((int)px, (int)py);
	}
}

static void on_pointer_motion_absolute(struct libinput_event *event)
{
	struct modeset_dev *dev = s_modeset_dev;
	if (dev == NULL)
		return;

	struct libinput_event_pointer *ev =
		libinput_event_get_pointer_event(event);
	double px = libinput_event_pointer_get_absolute_x(ev);
	double py = libinput_event_pointer_get_absolute_y(ev);

	s_cursor_pos[0] = px;
	s_cursor_pos[1] = py;
	if (s_motion_func) {
		s_motion_func((int)px, (int)py);
	}
}

static void on_pointer_button(struct libinput_event *event)
{
	struct libinput_event_pointer *ev =
		libinput_event_get_pointer_event(event);
	uint32_t button = libinput_event_pointer_get_button(ev);
	enum libinput_button_state state =
		libinput_event_pointer_get_button_state(ev);

	DLOG("pointer_button (%d, %d)\n", button, state);

	if (button == BTN_LEFT) {
		int pressed = (state == LIBINPUT_BUTTON_STATE_PRESSED) ? 1 : 0;
		if (s_button_func) {
			int px = (int)s_cursor_pos[0];
			int py = (int)s_cursor_pos[1];
			s_button_func(button, pressed, px, py);
		}
	}
}

static void on_touch_down(struct libinput_event *event)
{
	struct modeset_dev *dev = s_modeset_dev;
	if (dev == NULL)
		return;

	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	int32_t id = libinput_event_touch_get_slot(tevent);

	double dw = (double)dev->width; /* display width  */
	double dh = (double)dev->height; /* display height */

	struct libinput_event_touch *ev = libinput_event_get_touch_event(event);
	double px = libinput_event_touch_get_x_transformed(ev, dw);
	double py = libinput_event_touch_get_y_transformed(ev, dh);

	if (s_touch_down_func) {
		s_touch_down_func(id, (int)px, (int)py);
	}
}

static void on_touch_motion(struct libinput_event *event)
{
	struct modeset_dev *dev = s_modeset_dev;
	if (dev == NULL)
		return;

	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	int32_t id = libinput_event_touch_get_slot(tevent);

	double dw = (double)dev->width; /* display width  */
	double dh = (double)dev->height; /* display height */

	struct libinput_event_touch *ev = libinput_event_get_touch_event(event);
	double px = libinput_event_touch_get_x_transformed(ev, dw);
	double py = libinput_event_touch_get_y_transformed(ev, dh);

	if (s_touch_motion_func) {
		s_touch_motion_func(id, (int)px, (int)py);
	}
}

static void on_touch_up(struct libinput_event *event)
{
	struct libinput_event_touch *tevent =
		libinput_event_get_touch_event(event);
	int32_t id = libinput_event_touch_get_slot(tevent);

	if (s_touch_up_func) {
		s_touch_up_func(id);
	}
}

static void process_input_events(struct libinput *libinput)
{
	struct libinput_event *event;
	while ((event = libinput_get_event(libinput))) {
		switch (libinput_event_get_type(event)) {
		case LIBINPUT_EVENT_DEVICE_ADDED:
			DLOG("[libinput] LIBINPUT_EVENT_DEVICE_ADDED\n");
			on_device_added(event);
			break;
		case LIBINPUT_EVENT_DEVICE_REMOVED:
			DLOG("[libinput] LIBINPUT_EVENT_DEVICE_REMOVED\n");
			break;

		case LIBINPUT_EVENT_KEYBOARD_KEY:
			DLOG("[libinput] LIBINPUT_EVENT_KEYBOARD_KEY\n");
			on_keyboard_key(event);
			break;

		case LIBINPUT_EVENT_POINTER_MOTION:
			DLOG("[libinput] LIBINPUT_EVENT_POINTER_MOTION\n");
			on_pointer_motion(event);
			break;
		case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
			DLOG("[libinput] LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE\n");
			on_pointer_motion_absolute(event);
			break;
		case LIBINPUT_EVENT_POINTER_BUTTON:
			DLOG("[libinput] LIBINPUT_EVENT_POINTER_BUTTON\n");
			on_pointer_button(event);
			break;
		case LIBINPUT_EVENT_POINTER_AXIS:
			DLOG("[libinput] LIBINPUT_EVENT_POINTER_AXIS\n");
			break;

		case LIBINPUT_EVENT_TOUCH_FRAME:
			DLOG("[libinput] LIBINPUT_EVENT_TOUCH_FRAME\n");
			break;
		case LIBINPUT_EVENT_TOUCH_CANCEL:
			DLOG("[libinput] LIBINPUT_EVENT_TOUCH_CANCEL\n");
			break;
		case LIBINPUT_EVENT_TOUCH_DOWN:
			DLOG("[libinput] LIBINPUT_EVENT_TOUCH_DOWN\n");
			on_touch_down(event);
			break;
		case LIBINPUT_EVENT_TOUCH_MOTION:
			DLOG("[libinput] LIBINPUT_EVENT_TOUCH_MOTION\n");
			on_touch_motion(event);
			break;
		case LIBINPUT_EVENT_TOUCH_UP:
			DLOG("[libinput] LIBINPUT_EVENT_TOUCH_UP\n");
			on_touch_up(event);
			break;
		default:
			DLOG("[libinput] LIBINPUT_EVENT_XXXXX\n");
		}

		libinput_event_destroy(event);
	}
}

static int open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);
	if (fd < 0) {
		return -errno;
	}
	DLOG("[libinput] open(%d)\n", fd);
	return fd;
}

static void close_restricted(int fd, void *user_data)
{
	close(fd);
	DLOG("[libinput] close(%d)\n", fd);
}

const static struct libinput_interface li_interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static void libinput_log_func(struct libinput *libinput,
			      enum libinput_log_priority priority,
			      const char *format, va_list args)
{
	char *buf = (char *)malloc(64 + strlen(format) + 1);
	sprintf(buf, "[libinput] %s", format);
	vprintf(buf, args);
	free(buf);
}

static int on_libinput_event(sd_event_source *source, int fd, uint32_t revents,
			     void *data)
{
	libinput_dispatch(s_libinput);
	process_input_events(s_libinput);

	return 0;
}

void add_string_to_list(int *length, char **list, const char *string)
{
	for (int i = 0; i < *length; i++) {
		if (strcmp(list[i], string) == 0) {
			return;
		}
	}

	list[*length] = strdup(string);
	(*length)++;
}

static void use_input_device_init_from_udev(int *num_input_devices,
					    char **input_devices)
{
	char *mouse = getenv_str("EGLWINSYS_DRM_MOUSE_DEV", NULL);
	char *mouse_abs = getenv_str("EGLWINSYS_DRM_MOUSEABS_DEV", NULL);
	char *keyboard = getenv_str("EGLWINSYS_DRM_KEYBOARD_DEV", NULL);
	char *touch = getenv_str("EGLWINSYS_DRM_TOUCH_DEV", NULL);
	char *device_seat = getenv_str("EGLWINSYS_DRM_SEAT", "seat_virtual");

	char *specified_mouse = NULL;
	char *specified_mouse_abs = NULL;
	char *specified_keyboard = NULL;
	char *specified_touch = NULL;
	char *mouse_devices[MAX_DEVICES] = { 0 };
	char *touch_devices[MAX_DEVICES] = { 0 };
	char *keyboard_devices[MAX_DEVICES] = { 0 };

	int num_mouse_devices = 0;
	int num_touch_devices = 0;
	int num_keyboard_devices = 0;

	struct udev *udev = udev_new();
	if (udev == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return;
	}

	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "input");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry *devices =
		udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry *entry = NULL;

	udev_list_entry_foreach(entry, devices)
	{
		const char *path = udev_list_entry_get_name(entry);
		struct udev_device *dev =
			udev_device_new_from_syspath(udev, path);
		const char *seat =
			udev_device_get_property_value(dev, "ID_SEAT");
		const char *sysname = udev_device_get_sysname(dev);

		if (seat && strcmp(seat, device_seat) == 0 &&
		    strncmp(sysname, "event", 5) == 0) {
			const char *devnode = udev_device_get_devnode(dev);
			if (devnode == NULL) {
				WLOG("cannot find devnode from %s\n", path);
				continue;
			}

			const char *devlinks =
				udev_device_get_property_value(dev, "DEVLINKS");
			const char *is_keyboard =
				udev_device_get_property_value(
					dev, "ID_INPUT_KEYBOARD");
			if (is_keyboard && strcmp(is_keyboard, "1") == 0) {
				if (keyboard &&
				    (strcmp(keyboard, devnode) == 0 ||
				     (devlinks &&
				      strcmp(keyboard, devlinks) == 0))) {
					ILOG("Specified Device: %s is a keyboard (%s -> %s)\n",
					     path, devnode, devlinks);
					specified_keyboard = strdup(devnode);
				} else {
					ILOG("Default Device: %s is a keyboard (%s -> %s)\n",
					     path, devnode, devlinks);
					add_string_to_list(
						&num_keyboard_devices,
						keyboard_devices, devnode);
				}
			}

			const char *is_mouse = udev_device_get_property_value(
				dev, "ID_INPUT_MOUSE");
			if (is_mouse && strcmp(is_mouse, "1") == 0) {
				if (!mouse && !mouse_abs) {
					ILOG("Default Device: %s is a mouse (%s -> %s)\n",
					     path, devnode, devlinks);
					add_string_to_list(&num_mouse_devices,
							   mouse_devices,
							   devnode);
				} else {
					int find_mouse = 0;
					if (mouse &&
					    (strcmp(mouse, devnode) == 0 ||
					     (devlinks &&
					      strcmp(mouse, devlinks) == 0))) {
						ILOG("Specified Device: %s is a mouse (%s -> %s)\n",
						     path, devnode, devlinks);
						specified_mouse =
							strdup(devnode);
						find_mouse = 1;
					}

					if (mouse_abs &&
					    (strcmp(mouse_abs, devnode) == 0 ||
					     (devlinks &&
					      strcmp(mouse_abs, devlinks) ==
						      0))) {
						ILOG("Specified Device: %s is a mouse_abs (%s -> %s)\n",
						     path, devnode, devlinks);
						specified_mouse_abs =
							strdup(devnode);
						find_mouse = 1;
					}

					if (find_mouse == 0) {
						ILOG("Your specified Device is not found, So Default Device: %s is a mouse (%s -> %s)\n",
						     path, devnode, devlinks);
						add_string_to_list(
							&num_mouse_devices,
							mouse_devices, devnode);
					}
				}
			}

			const char *is_touchpad =
				udev_device_get_property_value(
					dev, "ID_INPUT_TOUCHPAD");
			const char *is_touchscreen =
				udev_device_get_property_value(
					dev, "ID_INPUT_TOUCHSCREEN");
			if ((is_touchpad && strcmp(is_touchpad, "1") == 0) ||
			    (is_touchscreen &&
			     strcmp(is_touchscreen, "1") == 0)) {
				if (touch && (strcmp(touch, devnode) == 0 ||
					      (devlinks &&
					       strcmp(touch, devlinks) == 0))) {
					ILOG("Specified Device: %s is a touch (%s -> %s)\n",
					     path, devnode, devlinks);
					specified_touch = strdup(devnode);
				} else {
					ILOG("Default Device: %s is a touch device (%s -> %s)\n",
					     path, devnode, devlinks);
					add_string_to_list(&num_touch_devices,
							   touch_devices,
							   devnode);
				}
			}
		}
		udev_device_unref(dev);
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	if (specified_keyboard) {
		add_string_to_list(num_input_devices, input_devices,
				   specified_keyboard);
	} else {
		for (int i = 0; i < num_keyboard_devices; i++) {
			add_string_to_list(num_input_devices, input_devices,
					   keyboard_devices[i]);
		}
	}

	if (specified_touch) {
		add_string_to_list(num_input_devices, input_devices,
				   specified_touch);
	} else {
		for (int i = 0; i < num_touch_devices; i++) {
			add_string_to_list(num_input_devices, input_devices,
					   touch_devices[i]);
		}
	}

	if (specified_mouse == NULL && specified_mouse_abs == NULL) {
		for (int i = 0; i < num_mouse_devices; i++) {
			add_string_to_list(num_input_devices, input_devices,
					   mouse_devices[i]);
		}
	} else {
		if (specified_mouse) {
			add_string_to_list(num_input_devices, input_devices,
					   specified_mouse);
		}

		if (specified_mouse_abs) {
			add_string_to_list(num_input_devices, input_devices,
					   specified_mouse_abs);
		}
	}
}

static void *input_thread_main()
{
	int ret;

	/* initialize libinput */
	{
		int num_input_devices = 0;
		char *input_devices[MAX_DEVICES] = { 0 };
		use_input_device_init_from_udev(&num_input_devices,
						input_devices);

		s_libinput = libinput_path_create_context(&li_interface, NULL);

		libinput_log_set_handler(s_libinput, &libinput_log_func);
		libinput_log_set_priority(
			s_libinput,
			LIBINPUT_LOG_PRIORITY_DEBUG); // INFO, ERROR;

		for (int i = 0; i < num_input_devices; i++) {
			struct libinput_device *device =
				libinput_path_add_device(s_libinput,
							 input_devices[i]);
			if (device == NULL) {
				ELOG("Failed to open device: %s\n",
				     input_devices[i]);
				continue;
			}
		}
	}

	/* initialize sd_event and enter the event loop */
	{
		struct sd_event *event_loop = NULL;

		ret = sd_event_new(&event_loop);
		if (ret != 0) {
			ELOG("%s\n", __FUNCTION__);
			return 0;
		}

		ret = sd_event_add_io(event_loop, NULL,
				      libinput_get_fd(s_libinput),
				      EPOLLIN | EPOLLRDHUP | EPOLLPRI,
				      on_libinput_event, NULL);
		if (ret != 0) {
			ELOG("%s\n", __FUNCTION__);
			return 0;
		}

		sd_event_loop(event_loop);
	}

	return 0;
}

static int drm_open()
{
	char *card = getenv_str("EGLWINSYS_DRM_DEV_NAME", "/dev/dri/card0");
	int fd;

	fd = open(card, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ELOG("cannot open '%s': %m\n", card);
		return -1;
	}

	return fd;
}

void *winsys_init_native_display(void)
{
	int drm_fd;
	struct gbm_device *gbm;

	drm_fd = drm_open();
	if (drm_fd < 0) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	gbm = gbm_create_device(drm_fd);
	if (gbm == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return NULL;
	}

	s_drm_fd = drm_fd;
	s_gbm = gbm;

	/* only MASTER can call drmModeSetCrtc() */
	drmSetMaster(s_drm_fd);

	uint64_t cap;
	if (drmGetCap(s_drm_fd, DRM_CAP_ASYNC_PAGE_FLIP, &cap) == 0) {
		if (cap == 1) {
			ILOG("Async page flip is supported by this driver.\n");
			async_flip = true;
		} else {
			ILOG("Async page flip is not supported by this driver.\n");
		}
	}

	pthread_create(&s_input_thread, NULL, input_thread_main, NULL);

	return (void *)gbm;
}

static int modeset_find_crtc(int fd, drmModeRes *res, drmModeConnector *conn,
			     modeset_dev_t *dev)
{
	int32_t crtc;
	drmModeEncoder *enc = NULL;
	if (conn->encoder_id) /* already connected [Encoder+CRTC] */
		enc = drmModeGetEncoder(fd, conn->encoder_id);

	if (enc) {
		if (enc->crtc_id) {
			crtc = enc->crtc_id;

			/* already used for another connector -> SKIP */
			for (modeset_dev_t *iter = modeset_list; iter;
			     iter = iter->next) {
				if (iter->conn == conn->connector_id)
					continue;

				if (iter->crtc == crtc) {
					crtc = -1;
					break;
				}
			}

			/* reuse CRTC */
			if (crtc >= 0) {
				DLOG("REUSE CRTC[%d]\n", crtc);
				drmModeFreeEncoder(enc);
				dev->encd = conn->encoder_id;
				dev->crtc = crtc;
				return 0;
			}
		}
		drmModeFreeEncoder(enc);
	}

	/* global search for Encoder */
	for (unsigned int i = 0; i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(fd, conn->encoders[i]);
		if (!enc) {
			ELOG("cannot retrieve encoder %d (%d)\n", i,
			     conn->encoders[i]);
			continue;
		}

		for (unsigned int j = 0; j < res->count_crtcs; ++j) {
			/* this CRTC is suitable for Encoder ? */
			if (!(enc->possible_crtcs & (1 << j)))
				continue;

			/* this CRTC is already used for another connector. -> SKIP */
			crtc = res->crtcs[j];
			for (modeset_dev_t *iter = modeset_list; iter;
			     iter = iter->next) {
				if (iter->crtc == crtc) {
					crtc = -1;
					break;
				}
			}

			/* found CRTC */
			if (crtc >= 0) {
				drmModeFreeEncoder(enc);
				dev->encd = conn->encoders[i];
				dev->crtc = crtc;
				return 0;
			}
		}
		drmModeFreeEncoder(enc);
	}
	ELOG("cannot find suitable CRTC for connector %u\n",
	     conn->connector_id);
	return -1;
}

static int modeset_setup_dev(int fd, drmModeRes *res, drmModeConnector *conn,
			     modeset_dev_t *dev, int *win_w, int *win_h, bool windowed)
{
	int i, ret;
	int max_h, max_v, max_refresh;
	int found = 0;
	/* select MODE. */
	max_h = max_v = max_refresh = 0;
	if (windowed) {
		for (i = 0; i < conn->count_modes; i++) {
			int h = conn->modes[i].hdisplay;
			int v = conn->modes[i].vdisplay;
			int refresh = conn->modes[i].vrefresh;
			ILOG("mode [%2d/%2d] %4ux%4u@%d", i, conn->count_modes, h, v, refresh);
			if ((h == *win_w) && (v == *win_h)) {
				if (refresh > max_refresh) {
					memcpy(&dev->mode, &conn->modes[i], sizeof(dev->mode));
					max_refresh = refresh;
					dev->width = max_h = h;
					dev->height = max_v = v;
					LOG("*");
				}
				found = 1;
			}
			LOG("\n");
		}
	}

	if (found == 0) {
		memcpy(&dev->mode, &conn->modes[0], sizeof(dev->mode));
		dev->width = conn->modes[0].hdisplay;
		dev->height = conn->modes[0].vdisplay;
		*win_w = dev->width;
		*win_h = dev->height;
		ILOG("mode [%2d/%2d] %4ux%4u@%d*\n", 0, conn->count_modes, dev->width, dev->height, conn->modes[0].vrefresh);
	}

	/* find CRTC for this connector */
	ret = modeset_find_crtc(fd, res, conn, dev);
	if (ret) {
		ELOG("no valid crtc for connector %u\n", conn->connector_id);
		return ret;
	}

	ILOG("====> conn(%d), encd(%d), crtc(%d)\n", dev->conn, dev->encd,
	     dev->crtc);

	dev->drm_fd = fd;
	return 0;
}

static int create_drm_device(int *win_w, int *win_h, bool windowed)
{
	int drm_fd = s_drm_fd;
	modeset_dev_t *dev;
	EGLBoolean ret;
	int curConnIndx = getenv_int("EGLWINSYS_DRM_CONNECTOR_IDX", 0);

	drmModeRes *res = drmModeGetResources(drm_fd);
	if (res == 0) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	/* Parse all connector info */
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn =
			drmModeGetConnector(drm_fd, res->connectors[i]);
		if ((conn == NULL) ||
		    (conn->connection != DRM_MODE_CONNECTED) ||
		    (conn->count_modes <= 0)) {
			continue; /* skip if not connector or has no modes available */
		}

		dev = calloc(sizeof(*dev), 1);
		dev->conn = conn->connector_id;
		dev->conn_idx = i;

		if (conn->encoder_id) {
			drmModeEncoder *enc =
				drmModeGetEncoder(drm_fd, conn->encoder_id);
			dev->crtc = enc->crtc_id;
			drmModeFreeEncoder(enc);
		}
		drmModeFreeConnector(conn);

		dev->next = modeset_list;
		modeset_list = dev;
	}

	for (dev = modeset_list; dev; dev = dev->next) {
		ILOG("connector[%d], crtc(%d) conn(%d)", dev->conn_idx,
		     dev->crtc, dev->conn);
		if (dev->conn_idx != curConnIndx) {
			LOG("\n");
			continue;
		}
		LOG("*\n");

		drmModeConnector *conn = drmModeGetConnector(
			drm_fd, res->connectors[curConnIndx]);
		ret = modeset_setup_dev(drm_fd, res, conn, dev, win_w, win_h, windowed);
		if (ret) {
			drmModeFreeConnector(conn);
			continue;
		}
		drmModeFreeConnector(conn);

		s_modeset_dev = dev;
	}

	if (s_modeset_dev == NULL) {
		ELOG("can't find connector.\n");
		ret = -1;
	}

	drmModeFreeResources(res);
	return ret;
}

void *winsys_init_native_window(void *dpy, int *win_w, int *win_h, bool compositor_fullscreen)
{
	struct gbm_device *gbm = s_gbm;
	struct gbm_surface *gbm_sfc;

	if (create_drm_device(win_w, win_h, compositor_fullscreen) < 0) {
		ELOG("%s\n", __FUNCTION__);
		exit(0);
	}

	gbm_sfc = gbm_surface_create(gbm, *win_w, *win_h, GBM_FORMAT_ARGB8888,
				     GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (gbm_sfc == NULL) {
		ELOG("%s\n", __FUNCTION__);
		exit(0);
	}

	s_gbm_sfc = gbm_sfc;

	return (void *)gbm_sfc;
}

static void drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = data;
	struct gbm_device *gbm = gbm_bo_get_device(bo);

	ILOG("drm_fb_destroy_callback(): fb_id=%d\n", fb->fb_id);

	if (fb->fb_id) {
		drmModeRmFB(gbm_device_get_fd(gbm), fb->fb_id);
	}

	free(data);
}

static struct drm_fb *drm_fb_get_from_bo(int drm_fd, struct gbm_bo *bo,
					 unsigned int format)
{
	int ret;
	struct drm_fb *fb = gbm_bo_get_user_data(bo);
	if (fb)
		return fb;

	fb = calloc(1, sizeof(*fb));
	if (fb == NULL)
		return NULL;

	fb->bo = bo;
	fb->width = gbm_bo_get_width(bo);
	fb->height = gbm_bo_get_height(bo);
	fb->stride = gbm_bo_get_stride(bo);
	fb->handle = gbm_bo_get_handle(bo).u32;
	fb->prime_fd = gbm_bo_get_fd(bo);

	{
		unsigned int handles[4], pitches[4], offsets[4];
		handles[0] = fb->handle;
		pitches[0] = fb->stride;
		offsets[0] = 0;
		ret = drmModeAddFB2(drm_fd, fb->width, fb->height, format,
				    handles, pitches, offsets, &fb->fb_id, 0);
		if (ret) {
			ELOG("%s\n", __FUNCTION__);
		}
		ILOG("WH(%d, %d) drmModeAddFB2: fb_id=%d\n", fb->width,
		     fb->height, fb->fb_id);
	}
	gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);
	return fb;
}

void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
		       unsigned int usec, void *data)
{
	int *waiting_for_flip = data;
	*waiting_for_flip = 0;
}

int winsys_swap(bool vsync)
{
	int ret;
	static drm_fb_t *s_fb_del = NULL;
	struct modeset_dev *dev = s_modeset_dev;

	struct gbm_bo *bo_next = gbm_surface_lock_front_buffer(s_gbm_sfc);
	if (!bo_next) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}

	struct drm_fb *fb_next =
		drm_fb_get_from_bo(s_drm_fd, bo_next, GBM_FORMAT_XRGB8888);
	if (fb_next == NULL) {
		ELOG("%s\n", __FUNCTION__);
		return -1;
	}
	int waiting_for_flip = 0;
	drmEventContext evctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};
	if (s_fb_del == NULL) {
		ret = drmModeSetCrtc(s_drm_fd, dev->crtc, fb_next->fb_id, 0, 0,
				     &dev->conn, 1, &dev->mode);
		if (ret) {
			ELOG("cannot set CRTC. fb(%d), crtc(%d) conn(%d) ret: %d\n",
			     fb_next->fb_id, dev->crtc, dev->conn, ret);
			gbm_surface_release_buffer(s_gbm_sfc, bo_next);
			return -1;
		}
	} else {
		unsigned int flip_mode = 0;
		if (!vsync && async_flip) {
			flip_mode |= DRM_MODE_PAGE_FLIP_ASYNC;
		}

		if (vsync) {
			flip_mode |= DRM_MODE_PAGE_FLIP_EVENT;
		}

		ret = drmModePageFlip(s_drm_fd, dev->crtc, fb_next->fb_id,
				      flip_mode, &waiting_for_flip);
		if (ret < 0) {
			gbm_surface_release_buffer(s_gbm_sfc, fb_next->bo);
			if (errno != EBUSY) {
				ELOG("ERR:%s(%d):%s\n", __FILE__, __LINE__,
				     strerror(errno));
				return -1;
			}
		}

		if (vsync) {
			waiting_for_flip = 1;
			while (waiting_for_flip) {
				ret = drmHandleEvent(s_drm_fd, &evctx);
				if (ret < 0) {
					ELOG("Failed to handle DRM event: %s\n",
					     strerror(errno));
					gbm_surface_release_buffer(s_gbm_sfc,
								   fb_next->bo);
					return -1;
				}
			}
		}
	}

	s_fb_del = fb_next;
	if (s_fb_del) {
		gbm_surface_release_buffer(s_gbm_sfc, s_fb_del->bo);
	}
	s_fb_del = fb_next;
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
