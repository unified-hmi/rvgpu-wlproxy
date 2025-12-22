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
#include <wayland-server.h>
#include "util_egl.h"
#include "util_log.h"
#include "compositor.h"
#include <sys/time.h>

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

extern compositor_surface *focused_csfc;

static bool check_pointer_enter_surface(struct wl_resource *surface_resource,
					int x, int y)
{
	compositor_surface *csfc = wl_resource_get_user_data(surface_resource);

	if ((x >= 0 && x < csfc->img_w) && (y >= 0 && y < csfc->img_h)) {
		return true;
	}
	return false;
}

uint32_t getCurrentTimeMs(void)
{
	struct timeval tv;
	uint32_t nt;

	gettimeofday(&tv, 0);
	nt = (uint32_t)(tv.tv_sec * 1000 + (tv.tv_usec / 1000));

	return nt;
}

/*--------------------------------------------------------------------------- *
 *  mouse pointer event
 *--------------------------------------------------------------------------- */
static void pointer_set_cursor(struct wl_client *client,
			       struct wl_resource *resource, uint32_t serial,
			       struct wl_resource *surface_resource, int32_t x,
			       int32_t y)
{
	DLOG("%s\n", __FUNCTION__);
}

static void pointer_release(struct wl_client *client,
			    struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static const struct wl_pointer_interface pointer_interface = {
	pointer_set_cursor, pointer_release
};

static void unbind_pointer_client_resource(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void seat_get_pointer(struct wl_client *client,
			     struct wl_resource *resource, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	struct wl_resource *cr;
	cr = wl_resource_create(client, &wl_pointer_interface,
				wl_resource_get_version(resource), id);
	if (cr == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_list_init(wl_resource_get_link(cr));
	wl_resource_set_implementation(cr, &pointer_interface, NULL,
				       unbind_pointer_client_resource);

	compositor *compositor = wl_resource_get_user_data(resource);
	client_data *client_data;
	wl_list_for_each(client_data, &compositor->client_list, link)
	{
		if (client == client_data->client) {
			client_data->pointer_resource = cr;
			break;
		}
	}
}

/*--------------------------------------------------------------------------- *
 *  keyboard event
 *--------------------------------------------------------------------------- */

WL_EXPORT int compositor_set_xkb_rule_names(compositor *ec,
					    struct xkb_rule_names *names)
{
	if (ec->xkb_context == NULL) {
		ec->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (ec->xkb_context == NULL) {
			ELOG("%s failed to create XKB context\n", __FUNCTION__);
			return -1;
		}
	}

	if (names)
		ec->xkb_names = *names;
	if (!ec->xkb_names.rules)
		ec->xkb_names.rules = strdup("evdev");
	if (!ec->xkb_names.model)
		ec->xkb_names.model = strdup("pc105");
	if (!ec->xkb_names.layout)
		ec->xkb_names.layout = strdup("us");

	ILOG("rules: %s, model: %s, layout: %s\n", ec->xkb_names.rules,
	     ec->xkb_names.model, ec->xkb_names.layout);
	return 0;
}

static int create_anonymous_file(off_t size)
{
	static const char template[] = "/tmp/wayland-shm-XXXXXX";
	char path[64];
	int fd;
	ssize_t ret;

	strncpy(path, template, sizeof(path));
	fd = mkstemp(path);
	if (fd < 0) {
		return -1;
	}

	ret = unlink(path);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	ret = ftruncate(fd, size);
	if (ret < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static struct xkb_info *xkb_info_create(struct xkb_keymap *keymap)
{
	char *keymap_string;
	struct xkb_info *xkb_info = calloc(1, sizeof *xkb_info);
	if (xkb_info == NULL)
		return NULL;

	xkb_info->keymap = xkb_keymap_ref(keymap);
	xkb_info->ref_count = 1;

	xkb_info->shift_mod =
		xkb_keymap_mod_get_index(xkb_info->keymap, XKB_MOD_NAME_SHIFT);
	xkb_info->caps_mod =
		xkb_keymap_mod_get_index(xkb_info->keymap, XKB_MOD_NAME_CAPS);
	xkb_info->ctrl_mod =
		xkb_keymap_mod_get_index(xkb_info->keymap, XKB_MOD_NAME_CTRL);
	xkb_info->alt_mod =
		xkb_keymap_mod_get_index(xkb_info->keymap, XKB_MOD_NAME_ALT);
	xkb_info->mod2_mod = xkb_keymap_mod_get_index(xkb_info->keymap, "Mod2");
	xkb_info->mod3_mod = xkb_keymap_mod_get_index(xkb_info->keymap, "Mod3");
	xkb_info->super_mod =
		xkb_keymap_mod_get_index(xkb_info->keymap, XKB_MOD_NAME_LOGO);
	xkb_info->mod5_mod = xkb_keymap_mod_get_index(xkb_info->keymap, "Mod5");

	xkb_info->num_led =
		xkb_keymap_led_get_index(xkb_info->keymap, XKB_LED_NAME_NUM);
	xkb_info->caps_led =
		xkb_keymap_led_get_index(xkb_info->keymap, XKB_LED_NAME_CAPS);
	xkb_info->scroll_led =
		xkb_keymap_led_get_index(xkb_info->keymap, XKB_LED_NAME_SCROLL);

	keymap_string = xkb_keymap_get_as_string(xkb_info->keymap,
						 XKB_KEYMAP_FORMAT_TEXT_V1);
	if (keymap_string == NULL) {
		ELOG("%s failed to get string version of keymap\n",
		     __FUNCTION__);
		goto err_keymap;
	}

	xkb_info->size = strlen(keymap_string) + 1;

	xkb_info->fd = create_anonymous_file(xkb_info->size);

	if (!xkb_info->fd) {
		ELOG("%s failed to create anonymous file for keymap\n",
		     __FUNCTION__);
		goto err_keymap;
	}

	char *map = mmap(NULL, xkb_info->size, PROT_READ | PROT_WRITE,
			 MAP_SHARED, xkb_info->fd, 0);
	strcpy(map, keymap_string);
	munmap(map, xkb_info->size);

	free(keymap_string);

	return xkb_info;

err_keymap:
	xkb_keymap_unref(xkb_info->keymap);
	free(xkb_info);
	return NULL;
}

static int compositor_build_global_keymap(compositor *ec)
{
	struct xkb_keymap *keymap;

	if (ec->xkb_info != NULL)
		return 0;

	keymap = xkb_keymap_new_from_names(ec->xkb_context, &ec->xkb_names, 0);
	if (keymap == NULL) {
		ELOG("%s failed to compile global XKB keymap\n", __FUNCTION__);
		ELOG("%s tried rules %s, model %s, layout %s, variant %s, options %s\n",
		     __FUNCTION__, ec->xkb_names.rules, ec->xkb_names.model,
		     ec->xkb_names.layout, ec->xkb_names.variant,
		     ec->xkb_names.options);
		return -1;
	}
	ec->xkb_info = xkb_info_create(keymap);
	ec->xkb_state = xkb_state_new(keymap);
	ec->modifiers.mods_depressed = 0;
	ec->modifiers.mods_latched = 0;
	ec->modifiers.mods_locked = 0;
	ec->modifiers.group = 0;
	xkb_keymap_unref(keymap);
	if (ec->xkb_info == NULL)
		return -1;

	return 0;
}

static void keyboard_release(struct wl_client *client,
			     struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface keyboard_interface = {
	keyboard_release
};

static void unbind_keyboard_resource(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void seat_get_keyboard(struct wl_client *client,
			      struct wl_resource *resource, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);

	struct wl_resource *cr;
	cr = wl_resource_create(client, &wl_keyboard_interface,
				wl_resource_get_version(resource), id);
	if (cr == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_list_init(wl_resource_get_link(cr));
	wl_resource_set_implementation(cr, &keyboard_interface, NULL,
				       unbind_keyboard_resource);

	compositor *compositor = wl_resource_get_user_data(resource);
	client_data *client_data;
	wl_list_for_each(client_data, &compositor->client_list, link)
	{
		if (client == client_data->client) {
			client_data->keyboard_resource = cr;
			break;
		}
	}
}

/*--------------------------------------------------------------------------- *
 *  touch event
 *--------------------------------------------------------------------------- */

static void touch_release(struct wl_client *client,
			  struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static const struct wl_touch_interface touch_interface = { touch_release };

static void unbind_touch_resource(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void seat_get_touch(struct wl_client *client,
			   struct wl_resource *resource, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);

	struct wl_resource *cr;
	cr = wl_resource_create(client, &wl_touch_interface,
				wl_resource_get_version(resource), id);
	if (cr == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_list_init(wl_resource_get_link(cr));
	wl_resource_set_implementation(cr, &touch_interface, NULL,
				       unbind_touch_resource);

	compositor *compositor = wl_resource_get_user_data(resource);
	client_data *client_data;
	wl_list_for_each(client_data, &compositor->client_list, link)
	{
		if (client == client_data->client) {
			client_data->touch_resource = cr;
			break;
		}
	}
}

static void seat_release(struct wl_client *client, struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static const struct wl_seat_interface seat_interface = {
	seat_get_pointer,
	seat_get_keyboard,
	seat_get_touch,
	seat_release,
};

static void unbind_resource(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void bind_seat(struct wl_client *client, void *data, uint32_t version,
		      uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	struct wl_resource *resource;
	enum wl_seat_capability caps = 0;

	resource = wl_resource_create(client, &wl_seat_interface, version, id);
	wl_resource_set_implementation(resource, &seat_interface, data,
				       unbind_resource);

	caps |= WL_SEAT_CAPABILITY_POINTER;
	caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	caps |= WL_SEAT_CAPABILITY_TOUCH;

	wl_seat_send_capabilities(resource, caps);

	//  if (version >= WL_SEAT_NAME_SINCE_VERSION)
	//      wl_seat_send_name (resource, "seat_virtual");
}

/*--------------------------------------------------------------------------- *
 *  Event handler
 *--------------------------------------------------------------------------- */
static void sendq_push(struct compositor *c, struct send_evt *ev)
{
	pthread_mutex_lock(&c->sendq_mutex);
	wl_list_insert(c->sendq.prev, &ev->link);
	pthread_mutex_unlock(&c->sendq_mutex);

	uint64_t one = 1;
	(void)write(c->send_efd, &one, sizeof one);
}

void compositor_post_pointer_enter(struct compositor *c,
			  struct wl_resource *ptr_res,
			  struct wl_display *display,
			  struct wl_resource *surface_res,
			  wl_fixed_t x, wl_fixed_t y)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_PTR_ENTER;
	ev->res = ptr_res;
	ev->display = display;
	ev->surface_res = surface_res;
	ev->x = x;
	ev->y = y;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_pointer_leave(struct compositor *c,
			  struct wl_resource *ptr_res,
			  struct wl_display *display,
			  struct wl_resource *surface_res)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_PTR_LEAVE;
	ev->res = ptr_res;
	ev->display = display;
	ev->surface_res = surface_res;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_pointer_motion(struct compositor *c,
			  struct wl_resource *ptr_res,
			  uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_PTR_MOTION;
	ev->res = ptr_res;
	ev->time = time;
	ev->x = x;
	ev->y = y;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_pointer_frame(struct compositor *c,
			  struct wl_resource *ptr_res)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_PTR_FRAME;
	ev->res = ptr_res;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

static void mousemove_cb(int x, int y)
{
	if (focused_csfc == NULL)
		return;

	struct wl_resource *resource;
	client_data *client_data;
	wl_list_for_each(client_data, &focused_csfc->compositor->client_list,
			 link)
	{
		if (focused_csfc->client == client_data->client) {
			resource = client_data->pointer_resource;
			break;
		}
	}

	if (resource == NULL) {
		return;
	}

	uint32_t serial =
		wl_display_next_serial(focused_csfc->compositor->wl_display);
	uint32_t msecs = getCurrentTimeMs();
	DLOG("mousemove_cb %d, %d, %d\n", msecs, x, y);
	wl_fixed_t fix_x = wl_fixed_from_int(x);
	wl_fixed_t fix_y = wl_fixed_from_int(y);

	pthread_mutex_lock(&focused_csfc->compositor->event_mutex);
	bool pointer_in_surface =
		check_pointer_enter_surface(focused_csfc->resource, x, y);

	if (!focused_csfc->pointer_focused && pointer_in_surface) {
		compositor_post_pointer_enter(focused_csfc->compositor, resource,
				focused_csfc->compositor->wl_display,
				focused_csfc->resource, fix_x, fix_y);
		focused_csfc->pointer_focused = true;
	}

	if (focused_csfc->pointer_focused && (x == -1 || y == -1)) {
		compositor_post_pointer_leave(focused_csfc->compositor, resource,
				focused_csfc->compositor->wl_display,
				focused_csfc->resource);
		focused_csfc->pointer_focused = false;
	}

	if (focused_csfc->pointer_focused && pointer_in_surface) {
		compositor_post_pointer_motion(focused_csfc->compositor, resource,
				msecs, fix_x, fix_y);
		if (wl_resource_get_version(resource) >=
		    WL_POINTER_FRAME_SINCE_VERSION) {
			compositor_post_pointer_frame(focused_csfc->compositor, resource);
		}
	}

	pthread_mutex_unlock(&focused_csfc->compositor->event_mutex);
}


void compositor_post_pointer_button(struct compositor *c,
			  struct wl_resource *ptr_res,
			  struct wl_display *display,
			  uint32_t time,
			  uint32_t button, uint32_t state)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_PTR_BUTTON;
	ev->res = ptr_res;
	ev->display = display;
	ev->time = time; ev->button = button;
	ev->state = state;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

static void button_cb(int button, int state, int x, int y)
{
	if (focused_csfc == NULL)
		return;

	struct wl_resource *resource;
	client_data *client_data;
	wl_list_for_each(client_data, &focused_csfc->compositor->client_list,
			 link)
	{
		if (focused_csfc->client == client_data->client) {
			resource = client_data->pointer_resource;
			break;
		}
	}

	if (resource == NULL) {
		return;
	}
	uint32_t serial =
		wl_display_next_serial(focused_csfc->compositor->wl_display);
	uint32_t msecs = getCurrentTimeMs();
	DLOG("button_cb %d, %d, %d, %d, %d, %d\n", serial, msecs, button, state,
	     x, y);
	pthread_mutex_lock(&focused_csfc->compositor->event_mutex);

	compositor_post_pointer_button(focused_csfc->compositor, resource,
			focused_csfc->compositor->wl_display,  msecs, button, state);
	pthread_mutex_unlock(&focused_csfc->compositor->event_mutex);
}


void compositor_post_keyboard_keymap(struct compositor *c,
			  struct wl_resource *kbd_res,
			  uint32_t format, int fd, uint32_t size)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	int dfd = dup(fd);
	if (dfd < 0) { free(ev); return; }
	ev->type = SEND_KBD_KEYMAP;
	ev->res = kbd_res;
	ev->keymap_format = format;
	ev->keymap_fd = dfd;
	ev->keymap_size = size;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_keyboard_enter(struct compositor *c,
			  struct wl_resource *kbd_res,
			  struct wl_display *display,
			  struct wl_resource *surface_res,
			  const struct wl_array *keys) // nullable
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_KBD_ENTER;
	ev->res = kbd_res;
	ev->display = display;
	ev->surface_res = surface_res;
	wl_array_init(&ev->keys);
	if (keys && keys->size) {
		void *data = malloc(keys->size);
		if (data) {
			memcpy(data, keys->data, keys->size);
			ev->keys.data = data;
			ev->keys.size = keys->size;
		}
	}

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_keyboard_modifiers(struct compositor *c,
			  struct wl_resource *kbd_res,
			  struct wl_display *display,
			  uint32_t depressed, uint32_t latched,
			  uint32_t locked, uint32_t group)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_KBD_MODS;
	ev->res = kbd_res;
	ev->display = display;
	ev->mods_depressed = depressed;
	ev->mods_latched   = latched;
	ev->mods_locked    = locked;
	ev->group          = group;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_keyboard_key(struct compositor *c,
			  struct wl_resource *kbd_res,
			  struct wl_display *display,
			  uint32_t time, uint32_t key, uint32_t state)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_KBD_KEY;
	ev->res = kbd_res;
	ev->display = display;
	ev->time = time;
	ev->key = key;
	ev->state = state;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}


void keyboard_cb(int key, int state)
{
	if (focused_csfc == NULL)
		return;

	struct wl_resource *resource;
	client_data *client_data;
	wl_list_for_each(client_data, &focused_csfc->compositor->client_list,
			 link)
	{
		if (focused_csfc->client == client_data->client) {
			resource = client_data->keyboard_resource;
			break;
		}
	}

	if (resource == NULL) {
		return;
	}

	if (!focused_csfc->keyboard_focused) {
		pthread_mutex_lock(&focused_csfc->compositor->event_mutex);
		compositor_post_keyboard_keymap(focused_csfc->compositor, resource,
			WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
			focused_csfc->compositor->xkb_info->fd,
			focused_csfc->compositor->xkb_info->size);

		struct wl_array keys;
		wl_array_init(&keys);
		compositor_post_keyboard_enter(focused_csfc->compositor, resource,
				focused_csfc->compositor->wl_display,
				focused_csfc->resource, &keys);
		wl_array_release(&keys);
		focused_csfc->keyboard_focused = true;
		pthread_mutex_unlock(&focused_csfc->compositor->event_mutex);
	}

	uint32_t msecs = getCurrentTimeMs();

	pthread_mutex_lock(&focused_csfc->compositor->event_mutex);
	xkb_state_update_key(focused_csfc->compositor->xkb_state, key + 8,
			     (state == WL_KEYBOARD_KEY_STATE_PRESSED) ?
				     XKB_KEY_DOWN :
				     XKB_KEY_UP);

	xkb_mod_mask_t mods_depressed = xkb_state_serialize_mods(
		focused_csfc->compositor->xkb_state, XKB_STATE_MODS_DEPRESSED);
	xkb_mod_mask_t mods_latched = xkb_state_serialize_mods(
		focused_csfc->compositor->xkb_state, XKB_STATE_MODS_LATCHED);
	xkb_mod_mask_t mods_locked = xkb_state_serialize_mods(
		focused_csfc->compositor->xkb_state, XKB_STATE_MODS_LOCKED);
	xkb_layout_index_t group =
		xkb_state_serialize_layout(focused_csfc->compositor->xkb_state,
					   XKB_STATE_LAYOUT_EFFECTIVE);

	if (mods_depressed !=
		    focused_csfc->compositor->modifiers.mods_depressed ||
	    mods_latched != focused_csfc->compositor->modifiers.mods_latched ||
	    mods_locked != focused_csfc->compositor->modifiers.mods_locked ||
	    group != focused_csfc->compositor->modifiers.group) {

		compositor_post_keyboard_modifiers(focused_csfc->compositor, resource,
				focused_csfc->compositor->wl_display,
				mods_depressed, mods_latched, mods_locked, group);

		focused_csfc->compositor->modifiers.mods_depressed =
			mods_depressed;
		focused_csfc->compositor->modifiers.mods_latched = mods_latched;
		focused_csfc->compositor->modifiers.mods_locked = mods_locked;
		focused_csfc->compositor->modifiers.group = group;
	}

	DLOG("keyboard_cb key: %d, state: %d\n", key, state);

	compositor_post_keyboard_key(focused_csfc->compositor, resource,
			focused_csfc->compositor->wl_display, msecs, key, state);

	pthread_mutex_unlock(&focused_csfc->compositor->event_mutex);
}

#if defined(USE_TOUCH)
void compositor_post_touch_frame(struct compositor *c,
			  struct wl_resource *touch_res)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_TOUCH_FRAME;
	ev->res = touch_res;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}

void compositor_post_touch_down(struct compositor *c,
			  struct wl_resource *tch_res,
			  struct wl_display *display,
			  uint32_t time,
			  struct wl_resource *surface_res,
			  int32_t id,
			  wl_fixed_t x, wl_fixed_t y)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_TOUCH_DOWN;
	ev->res = tch_res;
	ev->display = display;
	ev->time = time;
	ev->id = id;
	ev->surface_res = surface_res;
	ev->x = x;
	ev->y = y;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}


static void touch_down_cb(int32_t id, int32_t x, int32_t y)
{
	if (focused_csfc == NULL)
		return;

	struct wl_resource *resource;
	client_data *client_data;
	wl_list_for_each(client_data, &focused_csfc->compositor->client_list,
			 link)
	{
		if (focused_csfc->client == client_data->client) {
			resource = client_data->touch_resource;
			break;
		}
	}

	if (resource == NULL) {
		return;
	}

	wl_fixed_t fix_x = wl_fixed_from_int(x);
	wl_fixed_t fix_y = wl_fixed_from_int(y);

	compositor_post_touch_down(focused_csfc->compositor, resource,
			focused_csfc->compositor->wl_display, 0,
			focused_csfc->resource, id, fix_x, fix_y);
	compositor_post_touch_frame(focused_csfc->compositor, resource);
}


void compositor_post_touch_up(struct compositor *c,
			  struct wl_resource *tch_res,
			  struct wl_display *display,
			  uint32_t time, int32_t id)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_TOUCH_UP;
	ev->res = tch_res;
	ev->display = display;
	ev->time = time;
	ev->id = id;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}



static void touch_up_cb(int32_t id)
{
	if (focused_csfc == NULL)
		return;

	struct wl_resource *resource;
	client_data *client_data;
	wl_list_for_each(client_data, &focused_csfc->compositor->client_list,
			 link)
	{
		if (focused_csfc->client == client_data->client) {
			resource = client_data->touch_resource;
			break;
		}
	}

	if (resource == NULL) {
		return;
	}
	uint32_t time = getCurrentTimeMs();

	compositor_post_touch_up(focused_csfc->compositor, resource,
			focused_csfc->compositor->wl_display, time, id);
}

void compositor_post_touch_motion(struct compositor *c,
			  struct wl_resource *touch_res,
			  uint32_t time, int32_t id,
			  wl_fixed_t x, wl_fixed_t y)
{
	struct send_evt *ev = calloc(1, sizeof *ev);
	if (!ev) return;
	ev->type = SEND_TOUCH_MOTION;
	ev->time = time;
	ev->id = id;
	ev->x = x;
	ev->y = y;
	ev->res = touch_res;

	wl_list_init(&ev->link);
	sendq_push(c, ev);
}


static void touch_motion_cb(int32_t id, int32_t x, int32_t y)
{
	if (focused_csfc == NULL)
		return;

	struct wl_resource *resource;
	client_data *client_data;
	wl_list_for_each(client_data, &focused_csfc->compositor->client_list,
			 link)
	{
		if (focused_csfc->client == client_data->client) {
			resource = client_data->touch_resource;
			break;
		}
	}

	if (resource == NULL) {
		return;
	}

	wl_fixed_t fix_x = wl_fixed_from_int(x);
	wl_fixed_t fix_y = wl_fixed_from_int(y);

	compositor_post_touch_motion(focused_csfc->compositor, resource,
		0, id, fix_x, fix_y);
	compositor_post_touch_frame(focused_csfc->compositor, resource);
}
#endif

void compositor_seat_init(compositor *compositor)
{
	wl_global_create(compositor->wl_display, &wl_seat_interface, 6,
			 compositor, bind_seat);
	compositor_set_xkb_rule_names(compositor, NULL);
	compositor_build_global_keymap(compositor);

	egl_set_motion_func(mousemove_cb);
	egl_set_button_func(button_cb);
	egl_set_key_func(keyboard_cb);
#if defined(USE_TOUCH)
	egl_set_touch_down_func(touch_down_cb);
	egl_set_touch_up_func(touch_up_cb);
	egl_set_touch_motion_func(touch_motion_cb);
#endif
}
