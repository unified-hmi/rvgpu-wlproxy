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
#include <unistd.h>
#include <wayland-server-protocol.h>
#include <xdg-shell-server-protocol.h>
#include <wayland-server.h>
#include "util_egl.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include "util_render2d.h"
#include "util_log.h"
#include "compositor.h"
#include <string.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#define info(...) fprintf(stdout, __VA_ARGS__)

uint32_t getCurrentTimeMs(void);

typedef enum { TEX_FREE, TEX_WRITING, TEX_COMPLETE } TexStatus;

compositor_surface *focused_csfc = NULL;

compositor_surface *get_top_compositor_surface(compositor *compositor)
{
	if (compositor == NULL)
		return NULL;
	if (!wl_list_empty(&compositor->surface_list)) {
		struct wl_list *tail = compositor->surface_list.prev;
		compositor_surface *csfc = wl_container_of(tail, csfc, link);
		return csfc;
	}
	return NULL;
}

typedef struct appopt_t {
	int win_w, win_h;
	char *socket_name;
	bool sfc_fullscreen;
	bool windowed;
	bool vsync;
} appopt_t;

static PFNEGLBINDWAYLANDDISPLAYWL eglBindWaylandDisplayWL;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
static PFNEGLQUERYWAYLANDBUFFERWL eglQueryWaylandBufferWL;
static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;

/*--------------------------------------------------------------------------- *
 *  wl_surface
 *--------------------------------------------------------------------------- */
static void surface_destroy(struct wl_client *client,
			    struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void surface_attach(struct wl_client *client,
			   struct wl_resource *resource,
			   struct wl_resource *buffer_resource, int32_t sx,
			   int32_t sy)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_surface *csfc = wl_resource_get_user_data(resource);
	csfc->wl_buffer = buffer_resource;
}

static void surface_damage(struct wl_client *client,
			   struct wl_resource *resource, int32_t x, int32_t y,
			   int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static void destroy_frame_callback(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_frame_callback *cb = wl_resource_get_user_data(resource);
	wl_list_remove(&cb->link);
	free(cb);
}

static void surface_frame(struct wl_client *client,
			  struct wl_resource *resource, uint32_t callback)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_surface *csfc = wl_resource_get_user_data(resource);

	compositor_frame_callback *cb = malloc(sizeof *cb);
	if (cb == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_resource_post_no_memory(resource);
		return;
	}

	cb->resource =
		wl_resource_create(client, &wl_callback_interface, 1, callback);
	if (cb->resource == NULL) {
		ELOG("%s\n", __FUNCTION__);
		free(cb);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(cb->resource, NULL, cb,
				       destroy_frame_callback);

	wl_list_insert(csfc->pending_frame_callback_list.prev, &cb->link);
}

static void surface_set_opaque_region(struct wl_client *client,
				      struct wl_resource *resource,
				      struct wl_resource *region_resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void surface_set_input_region(struct wl_client *client,
				     struct wl_resource *resource,
				     struct wl_resource *region_resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_send_configure_event(void *user_data)
{
	DLOG("%s\n", __FUNCTION__);
	shell_surface *shell_surface = user_data;
	xdg_toplevel *toplevel = shell_surface->toplevel;

	uint32_t *s;
	struct wl_array states;

	wl_array_init(&states);
	if (toplevel->pending.state.maximized) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_MAXIMIZED;
	}
	//TODO only support XDG_TOPLEVEL_STATE_FULLSCREEN now
	if (toplevel->pending.state.fullscreen) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_FULLSCREEN;
	}

	if (toplevel->pending.state.resizing) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_RESIZING;
	}
	if (toplevel->pending.state.activated) {
		s = wl_array_add(&states, sizeof(uint32_t));
		*s = XDG_TOPLEVEL_STATE_ACTIVATED;
	}

	pthread_mutex_lock(&shell_surface->csfc->compositor->event_mutex);
	xdg_toplevel_send_configure(toplevel->resource,
				    toplevel->pending.size.width,
				    toplevel->pending.size.height, &states);
	wl_array_release(&states);

	uint32_t serial = wl_display_next_serial(
		shell_surface->csfc->compositor->wl_display);
	xdg_surface_send_configure(shell_surface->resource, serial);
	pthread_mutex_unlock(&shell_surface->csfc->compositor->event_mutex);
}

static void surface_commit(struct wl_client *client,
			   struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	GLenum gl_internal_format[3] = { 0, 0, 0 };
	GLenum gl_pixel_type;
	int pitch;
	int offset = 0;
	int hsub = 1;
	int vsub = 1;
	compositor_surface *csfc = wl_resource_get_user_data(resource);

	if (csfc->wl_buffer &&
	    csfc->status[csfc->current_tex_index] == TEX_FREE) {
		for (int i = 0; i < TEX_PLANE_NUM; i++) {
			if (csfc->texid[i] == 0) {
				glGenTextures(1, &csfc->texid[i]);
				glBindTexture(GL_TEXTURE_2D, csfc->texid[i]);
				glTexParameterf(GL_TEXTURE_2D,
						GL_TEXTURE_MIN_FILTER,
						GL_LINEAR);
				glTexParameterf(GL_TEXTURE_2D,
						GL_TEXTURE_MAG_FILTER,
						GL_LINEAR);
				glTexParameterf(GL_TEXTURE_2D,
						GL_TEXTURE_WRAP_S,
						GL_CLAMP_TO_EDGE);
				glTexParameterf(GL_TEXTURE_2D,
						GL_TEXTURE_WRAP_T,
						GL_CLAMP_TO_EDGE);
			}
		}

		struct wl_shm_buffer *shm_buf =
			wl_shm_buffer_get(csfc->wl_buffer);
		if (shm_buf) {
			csfc->img_w = wl_shm_buffer_get_width(shm_buf);
			csfc->img_h = wl_shm_buffer_get_height(shm_buf);
			void *pixdata = wl_shm_buffer_get_data(shm_buf);
			uint32_t format = wl_shm_buffer_get_format(shm_buf);

			switch (format) {
			case WL_SHM_FORMAT_XRGB8888:
				pitch = wl_shm_buffer_get_stride(shm_buf) / 4;
				gl_internal_format[0] = GL_BGRA_EXT;
				gl_pixel_type = GL_UNSIGNED_BYTE;
				break;
			case WL_SHM_FORMAT_ARGB8888:
				pitch = wl_shm_buffer_get_stride(shm_buf) / 4;
				gl_internal_format[0] = GL_BGRA_EXT;
				gl_pixel_type = GL_UNSIGNED_BYTE;
				break;
			case WL_SHM_FORMAT_RGB565:
				pitch = wl_shm_buffer_get_stride(shm_buf) / 2;
				gl_internal_format[0] = GL_RGB;
				gl_pixel_type = GL_UNSIGNED_SHORT_5_6_5;
				break;
			default:
				WLOG("%s unknown shm buffer format: %08x\n",
				     __FUNCTION__, format);
			}

			GLenum gl_format;
			switch (gl_internal_format[0]) {
			case GL_R8_EXT:
				gl_format = GL_RED_EXT;
			case GL_RG8_EXT:
				gl_format = GL_RG_EXT;
			default:
				gl_format = gl_internal_format[0];
			}
			glBindTexture(GL_TEXTURE_2D,
				      csfc->texid[csfc->current_tex_index]);
			glTexImage2D(GL_TEXTURE_2D, 0, gl_internal_format[0],
				     pitch / hsub, csfc->img_h, 0, gl_format,
				     gl_pixel_type, pixdata + offset);
			glBindTexture(GL_TEXTURE_2D, 0);
			csfc->glsyncobj_tex =
				glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			csfc->status[csfc->current_tex_index] = TEX_WRITING;
		} else {
			eglQueryWaylandBufferWL(egl_get_display(),
						csfc->wl_buffer, EGL_WIDTH,
						&csfc->img_w);
			eglQueryWaylandBufferWL(egl_get_display(),
						csfc->wl_buffer, EGL_HEIGHT,
						&csfc->img_h);

			if (csfc->eglImg != EGL_NO_IMAGE_KHR) {
				eglDestroyImageKHR(egl_get_display(),
						   csfc->eglImg);
				csfc->eglImg = EGL_NO_IMAGE_KHR;
			}
			EGLint attribs = EGL_NONE;
			csfc->eglImg =
				eglCreateImageKHR(egl_get_display(),
						  EGL_NO_CONTEXT,
						  EGL_WAYLAND_BUFFER_WL,
						  csfc->wl_buffer, &attribs);

			glBindTexture(GL_TEXTURE_2D,
				      csfc->texid[csfc->current_tex_index]);
			glEGLImageTargetTexture2DOES(GL_TEXTURE_2D,
						     csfc->eglImg);
			glBindTexture(GL_TEXTURE_2D, 0);
			csfc->glsyncobj_tex =
				glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
			csfc->status[csfc->current_tex_index] = TEX_WRITING;
		}
	}
	{
		wl_list_insert_list(&csfc->frame_callback_list,
				    &csfc->pending_frame_callback_list);
		wl_list_init(&csfc->pending_frame_callback_list);
	}

	shell_surface *shell_surface = csfc->shell_surface;
	if (shell_surface) {
		if (shell_surface->added == 0 && shell_surface->toplevel) {
			struct wl_display *display =
				csfc->compositor->wl_display;
			struct wl_event_loop *loop =
				wl_display_get_event_loop(display);

			if (shell_surface->csfc->compositor->sfc_fullscreen) {
				shell_surface->toplevel->pending.state
					.fullscreen = true;
				int width =
					shell_surface->csfc->compositor->width;
				int height =
					shell_surface->csfc->compositor->height;
				shell_surface->toplevel->pending.size.width =
					width;
				shell_surface->toplevel->pending.size.height =
					height;
			}

			shell_surface->toplevel->pending.state.activated = true;
			wl_event_loop_add_idle(loop, xdg_send_configure_event,
					       shell_surface);
			shell_surface->added = true;
		}
	}
}

static void surface_set_buffer_transform(struct wl_client *client,
					 struct wl_resource *resource,
					 int transform)
{
	DLOG("%s\n", __FUNCTION__);
}

static void surface_set_buffer_scale(struct wl_client *client,
				     struct wl_resource *resource,
				     int32_t scale)
{
	DLOG("%s\n", __FUNCTION__);
}

static void surface_damage_buffer(struct wl_client *client,
				  struct wl_resource *resource, int32_t x,
				  int32_t y, int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_surface_interface surface_interface = {
	surface_destroy,
	surface_attach,
	surface_damage,
	surface_frame,
	surface_set_opaque_region,
	surface_set_input_region,
	surface_commit,
	surface_set_buffer_transform,
	surface_set_buffer_scale,
	surface_damage_buffer
};

/*--------------------------------------------------------------------------- *
 *  wl_region
 *--------------------------------------------------------------------------- */
static void region_destroy(struct wl_client *client,
			   struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static void region_add(struct wl_client *client, struct wl_resource *resource,
		       int32_t x, int32_t y, int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static void region_subtract(struct wl_client *client,
			    struct wl_resource *resource, int32_t x, int32_t y,
			    int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_region_interface region_interface = { region_destroy,
							     region_add,
							     region_subtract };

/*--------------------------------------------------------------------------- *
 *  wl_compositor
 *--------------------------------------------------------------------------- */
compositor_surface *compositor_surface_create(compositor *compositor)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_surface *csfc = calloc(sizeof *csfc, 1);
	if (csfc == NULL)
		return NULL;

	csfc->pointer_focused = false;
	csfc->keyboard_focused = false;
	csfc->eglImg = EGL_NO_IMAGE_KHR;
	for (int i = 0; i < TEX_PLANE_NUM; i++) {
		csfc->status[i] = TEX_FREE;
	}
	csfc->glsyncobj_tex = NULL;
	csfc->current_tex_index = 0;
	csfc->updated_tex_index = -1;
	wl_list_init(&csfc->link);
	wl_list_init(&csfc->pending_frame_callback_list);
	wl_list_init(&csfc->frame_callback_list);

	return csfc;
}

void compositor_surface_destroy(compositor_surface *csfc)
{
	DLOG("%s\n", __FUNCTION__);
	if (csfc->eglImg != EGL_NO_IMAGE_KHR)
		eglDestroyImageKHR(egl_get_display(), csfc->eglImg);

	for (int i = 0; i < TEX_PLANE_NUM; i++) {
		if (csfc->texid[i] != 0) {
			glDeleteTextures(1, &csfc->texid[i]);
		}
	}

	wl_list_remove(&csfc->link);
	wl_list_init(&csfc->link);

	if (focused_csfc == csfc) {
		compositor_surface *top_csfc =
			get_top_compositor_surface(csfc->compositor);
		if (top_csfc == NULL) {
			focused_csfc = NULL;
		} else {
			focused_csfc = top_csfc;
			focused_csfc->pointer_focused = false;
			if (focused_csfc &&
			    focused_csfc->shell_surface->toplevel) {
				focused_csfc->shell_surface->toplevel->pending
					.state.activated = true;
				struct wl_display *display =
					csfc->compositor->wl_display;
				struct wl_event_loop *loop =
					wl_display_get_event_loop(display);
				wl_event_loop_add_idle(
					loop, xdg_send_configure_event,
					focused_csfc->shell_surface);
			}
		}
	}
	free(csfc);
}

static void destroy_surface_resource(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_surface *csfc = wl_resource_get_user_data(resource);

	compositor_surface_destroy(csfc);
}

static void destroy_region_resource(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_region *region = wl_resource_get_user_data(resource);
	free(region);
}

/*
 *  [client] wl_compositor_create_surface(wlCompositor)
 */
static void compositor_create_surface(struct wl_client *client,
				      struct wl_resource *resource, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	compositor *compositor = wl_resource_get_user_data(resource);
	compositor_surface *csfc;

	csfc = compositor_surface_create(compositor);
	if (csfc == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}
	csfc->client = client;
	csfc->compositor = compositor;
	csfc->resource =
		wl_resource_create(client, &wl_surface_interface,
				   wl_resource_get_version(resource), id);
	if (csfc->resource == NULL) {
		ELOG("%s\n", __FUNCTION__);
		compositor_surface_destroy(csfc);
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(csfc->resource, &surface_interface, csfc,
				       destroy_surface_resource);
}

/*
 *  [client] wl_compositor_create_region(wlCompositor)
 */
static void compositor_create_region(struct wl_client *client,
				     struct wl_resource *resource, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_region *region;

	region = calloc(sizeof(*region), 1);
	if (region == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	region->resource =
		wl_resource_create(client, &wl_region_interface, 1, id);
	if (region->resource == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}
	wl_resource_set_implementation(region->resource, &region_interface,
				       region, destroy_region_resource);
}

static const struct wl_compositor_interface compositor_interface = {
	compositor_create_surface, compositor_create_region
};

static void client_destroyed(struct wl_listener *listener, void *data)
{
	DLOG("%s\n", __FUNCTION__);
	client_data *client_data =
		wl_container_of(listener, client_data, destroy_listener);

	wl_list_remove(&client_data->link);
	free(client_data);
}

/*
 *  [client] wl_registry_bind(registry, "wl_compositor", &wl_compositor_interface, 1)
 */
static void compositor_bind(struct wl_client *client, void *data,
			    uint32_t version, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	compositor *compositor = data;

	compositor->resource = wl_resource_create(
		client, &wl_compositor_interface, version, id);
	if (compositor->resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(compositor->resource,
				       &compositor_interface, compositor, NULL);

	client_data *client_data = calloc(sizeof(*client_data), 1);
	client_data->client = client;
	wl_list_insert(&compositor->client_list, &client_data->link);
	client_data->destroy_listener.notify = client_destroyed;
	wl_client_add_destroy_listener(client, &client_data->destroy_listener);
}

/*--------------------------------------------------------------------------- *
 *  wl_output
 *--------------------------------------------------------------------------- */

static void output_release(struct wl_client *client,
			   struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static const struct wl_output_interface output_interface = {
	output_release,
};

static void output_bind(struct wl_client *client, void *data, uint32_t version,
			uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	struct wl_resource *resource;
	struct compositor *compositor = data;

	resource =
		wl_resource_create(client, &wl_output_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &output_interface, compositor,
				       NULL);

	wl_output_send_geometry(resource, 0, 0, compositor->width,
				compositor->height, WL_OUTPUT_SUBPIXEL_NONE,
				"unknown", "unknown",
				WL_OUTPUT_TRANSFORM_NORMAL);

	wl_output_send_mode(resource,
			    WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
			    compositor->width, compositor->height, 0);

	wl_output_send_done(resource);
}

/*--------------------------------------------------------------------------- *
 *  wl_shell
 *--------------------------------------------------------------------------- */
static void wl_shell_client_destroy(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	shell_client *shell_client = wl_resource_get_user_data(resource);
	struct wl_list *list = &shell_client->surface_list;
	struct wl_list *link, *tmp;

	for (link = list->next, tmp = link->next; link != list;
	     link = tmp, tmp = link->next) {
		wl_list_remove(link);
		wl_list_init(link);
	}

	free(shell_client);
}

/*  [client] wl_shell_surface_pong(wlShellSurface, serial); */
static void wl_shell_surface_pong(struct wl_client *wl_client,
				  struct wl_resource *resource, uint32_t serial)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_move(struct wl_client *wl_client,
				  struct wl_resource *resource,
				  struct wl_resource *seat_resource,
				  uint32_t serial)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_resize(struct wl_client *wl_client,
				    struct wl_resource *resource,
				    struct wl_resource *seat_resource,
				    uint32_t serial,
				    enum wl_shell_surface_resize edges)
{
	DLOG("%s\n", __FUNCTION__);
}

/*  [client] wl_shell_surface_set_toplevel(wlShellSurface); */
static void wl_shell_surface_set_toplevel(struct wl_client *wl_client,
					  struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void
wl_shell_surface_set_transient(struct wl_client *wl_client,
			       struct wl_resource *resource,
			       struct wl_resource *parent_resource, int32_t x,
			       int32_t y, enum wl_shell_surface_transient flags)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_set_fullscreen(
	struct wl_client *wl_client, struct wl_resource *resource,
	enum wl_shell_surface_fullscreen_method method, uint32_t framerate,
	struct wl_resource *output_resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_set_popup(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       struct wl_resource *seat_resource,
				       uint32_t serial,
				       struct wl_resource *parent_resource,
				       int32_t x, int32_t y,
				       enum wl_shell_surface_transient flags)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_set_maximized(struct wl_client *wl_client,
					   struct wl_resource *resource,
					   struct wl_resource *output_resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_set_title(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       const char *title)
{
	DLOG("%s\n", __FUNCTION__);
}

static void wl_shell_surface_set_class(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       const char *class_)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct wl_shell_surface_interface wl_shell_surface_implementation = {
	.pong = wl_shell_surface_pong,
	.move = wl_shell_surface_move,
	.resize = wl_shell_surface_resize,
	.set_toplevel = wl_shell_surface_set_toplevel,
	.set_transient = wl_shell_surface_set_transient,
	.set_fullscreen = wl_shell_surface_set_fullscreen,
	.set_popup = wl_shell_surface_set_popup,
	.set_maximized = wl_shell_surface_set_maximized,
	.set_title = wl_shell_surface_set_title,
	.set_class = wl_shell_surface_set_class,
};

/* 
 *  [client] wl_shell_get_shell_surface(wlShell, wlSurface);
 */
static void wl_shell_get_shell_surface(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       uint32_t id,
				       struct wl_resource *surface_resource)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_surface *csfc = wl_resource_get_user_data(surface_resource);
	shell_surface *shell_surface;

	shell_surface = calloc(sizeof(*shell_surface), 1);
	if (shell_surface == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_client_post_no_memory(wl_client);
		return;
	}
	shell_surface->csfc = csfc;
	shell_surface->resource =
		wl_resource_create(wl_client, &wl_shell_surface_interface,
				   wl_resource_get_version(resource), id);
	if (shell_surface->resource == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_client_post_no_memory(wl_client);
	}

	wl_resource_set_implementation(shell_surface->resource,
				       &wl_shell_surface_implementation,
				       shell_surface, NULL);
	if (focused_csfc && focused_csfc->shell_surface->toplevel) {
		focused_csfc->shell_surface->toplevel->pending.state.activated =
			false;
	}
	csfc->shell_surface = shell_surface;
	focused_csfc = csfc;
	wl_list_insert(csfc->compositor->surface_list.prev, &csfc->link);
}

static const struct wl_shell_interface wl_shell_implementation = {
	.get_shell_surface = wl_shell_get_shell_surface,
};

/* 
 *  [client] wl_registry_bind(registry, "wl_shell", &wl_shell_interface, 1)
 */
static void wl_shell_bind(struct wl_client *wl_client, void *data,
			  uint32_t version, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	compositor *compositor = data;
	shell_client *shell_client;

	shell_client = malloc(sizeof(*shell_client));
	if (shell_client == NULL) {
		if (wl_client != NULL)
			wl_client_post_no_memory(wl_client);
	}

	shell_client->compositor = compositor;
	shell_client->client = wl_client;
	wl_list_init(&shell_client->surface_list);

	shell_client->resource =
		wl_resource_create(wl_client, &wl_shell_interface, version, id);
	if (shell_client->resource == NULL) {
		wl_client_post_no_memory(wl_client);
		free(shell_client);
	}

	wl_resource_set_implementation(shell_client->resource,
				       &wl_shell_implementation, shell_client,
				       wl_shell_client_destroy);
}

/*--------------------------------------------------------------------------- *
 *  xdg_shell
 *--------------------------------------------------------------------------- */
void xdg_toplevel_protocol_destroy(struct wl_client *client,
				   struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static void
xdg_toplevel_protocol_set_parent(struct wl_client *wl_client,
				 struct wl_resource *resource,
				 struct wl_resource *parent_resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_set_title(struct wl_client *wl_client,
					    struct wl_resource *resource,
					    const char *title)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_set_app_id(struct wl_client *wl_client,
					     struct wl_resource *resource,
					     const char *app_id)
{
	DLOG("%s\n", __FUNCTION__);
}

static void
xdg_toplevel_protocol_show_window_menu(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       struct wl_resource *seat_resource,
				       uint32_t serial, int32_t x, int32_t y)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_move(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       struct wl_resource *seat_resource,
				       uint32_t serial)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_resize(struct wl_client *wl_client,
					 struct wl_resource *resource,
					 struct wl_resource *seat_resource,
					 uint32_t serial,
					 enum xdg_toplevel_resize_edge edges)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_set_min_size(struct wl_client *wl_client,
					       struct wl_resource *resource,
					       int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_set_max_size(struct wl_client *wl_client,
					       struct wl_resource *resource,
					       int32_t width, int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_set_maximized(struct wl_client *wl_client,
						struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_toplevel_protocol_unset_maximized(struct wl_client *wl_client,
						  struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void
xdg_toplevel_protocol_set_fullscreen(struct wl_client *wl_client,
				     struct wl_resource *resource,
				     struct wl_resource *output_resource)
{
	DLOG("%s\n", __FUNCTION__);
	shell_surface *shell_surface = wl_resource_get_user_data(resource);
	if (shell_surface != NULL) {
		shell_surface->toplevel->pending.state.fullscreen = true;
		int width = shell_surface->csfc->compositor->width;
		int height = shell_surface->csfc->compositor->height;

		shell_surface->toplevel->pending.size.width = width;
		shell_surface->toplevel->pending.size.height = height;
		struct wl_display *display =
			shell_surface->csfc->compositor->wl_display;
		struct wl_event_loop *eloop =
			wl_display_get_event_loop(display);
		wl_event_loop_add_idle(eloop, xdg_send_configure_event,
				       shell_surface);
	}
}

static void xdg_toplevel_protocol_unset_fullscreen(struct wl_client *wl_client,
						   struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	shell_surface *shell_surface = wl_resource_get_user_data(resource);
	if (shell_surface != NULL) {
		shell_surface->toplevel->pending.state.fullscreen = false;
		shell_surface->toplevel->pending.size.width = 0;
		shell_surface->toplevel->pending.size.height = 0;
		struct wl_display *display =
			shell_surface->csfc->compositor->wl_display;
		struct wl_event_loop *eloop =
			wl_display_get_event_loop(display);
		wl_event_loop_add_idle(eloop, xdg_send_configure_event,
				       shell_surface);
	}
}

static void xdg_toplevel_protocol_set_minimized(struct wl_client *wl_client,
						struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct xdg_toplevel_interface xdg_toplevel_implementation = {
	.destroy = xdg_toplevel_protocol_destroy,
	.set_parent = xdg_toplevel_protocol_set_parent,
	.set_title = xdg_toplevel_protocol_set_title,
	.set_app_id = xdg_toplevel_protocol_set_app_id,
	.show_window_menu = xdg_toplevel_protocol_show_window_menu,
	.move = xdg_toplevel_protocol_move,
	.resize = xdg_toplevel_protocol_resize,
	.set_min_size = xdg_toplevel_protocol_set_min_size,
	.set_max_size = xdg_toplevel_protocol_set_max_size,
	.set_maximized = xdg_toplevel_protocol_set_maximized,
	.unset_maximized = xdg_toplevel_protocol_unset_maximized,
	.set_fullscreen = xdg_toplevel_protocol_set_fullscreen,
	.unset_fullscreen = xdg_toplevel_protocol_unset_fullscreen,
	.set_minimized = xdg_toplevel_protocol_set_minimized,
};

static void xdg_shell_client_destroy(struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	shell_client *client = wl_resource_get_user_data(resource);
	struct wl_list *list = &client->surface_list;
	struct wl_list *link, *tmp;

	for (link = list->next, tmp = link->next; link != list;
	     link = tmp, tmp = link->next) {
		wl_list_remove(link);
		wl_list_init(link);
	}

	free(client);
}

static void xdg_shell_destroy(struct wl_client *wl_client,
			      struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_shell_create_positioner(struct wl_client *wl_client,
					struct wl_resource *resource,
					uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_surface_destroy(struct wl_client *client,
				struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
	DLOG("%s\n", __FUNCTION__);
	wl_resource_destroy(resource);
}

static void xdg_surface_get_toplevel(struct wl_client *wl_client,
				     struct wl_resource *resource, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	shell_surface *shell_surface = wl_resource_get_user_data(resource);
	xdg_toplevel *toplevel;

	toplevel = calloc(sizeof(*toplevel), 1);
	if (toplevel == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_client_post_no_memory(wl_client);
		return;
	}

	toplevel->resource =
		wl_resource_create(wl_client, &xdg_toplevel_interface,
				   wl_resource_get_version(resource), id);
	if (toplevel->resource == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_client_post_no_memory(wl_client);
	}

	wl_resource_set_implementation(toplevel->resource,
				       &xdg_toplevel_implementation,
				       shell_surface, NULL);

	shell_surface->toplevel = toplevel;
}

static void xdg_surface_get_popup(struct wl_client *wl_client,
				  struct wl_resource *resource, uint32_t id,
				  struct wl_resource *parent_resource,
				  struct wl_resource *positioner_resource)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_surface_set_window_geometry(struct wl_client *wl_client,
					    struct wl_resource *resource,
					    int32_t x, int32_t y, int32_t width,
					    int32_t height)
{
	DLOG("%s\n", __FUNCTION__);
}

static void xdg_surface_ack_configure(struct wl_client *wl_client,
				      struct wl_resource *resource,
				      uint32_t serial)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct xdg_surface_interface xdg_surface_implementation = {
	.destroy = xdg_surface_destroy,
	.get_toplevel = xdg_surface_get_toplevel,
	.get_popup = xdg_surface_get_popup,
	.set_window_geometry = xdg_surface_set_window_geometry,
	.ack_configure = xdg_surface_ack_configure,
};

static void xdg_shell_get_xdg_surface(struct wl_client *wl_client,
				      struct wl_resource *resource, uint32_t id,
				      struct wl_resource *surface_resource)
{
	DLOG("%s\n", __FUNCTION__);
	compositor_surface *csfc = wl_resource_get_user_data(surface_resource);
	shell_surface *shell_surface;

	shell_surface = calloc(sizeof(*shell_surface), 1);
	if (shell_surface == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_client_post_no_memory(wl_client);
		return;
	}
	shell_surface->csfc = csfc;
	shell_surface->resource =
		wl_resource_create(wl_client, &xdg_surface_interface,
				   wl_resource_get_version(resource), id);
	if (shell_surface->resource == NULL) {
		ELOG("%s\n", __FUNCTION__);
		wl_client_post_no_memory(wl_client);
	}

	wl_resource_set_implementation(shell_surface->resource,
				       &xdg_surface_implementation,
				       shell_surface, NULL);

	if (focused_csfc && focused_csfc->shell_surface->toplevel) {
		focused_csfc->shell_surface->toplevel->pending.state.activated =
			false;
		struct wl_display *display = csfc->compositor->wl_display;
		struct wl_event_loop *loop = wl_display_get_event_loop(display);
		wl_event_loop_add_idle(loop, xdg_send_configure_event,
				       focused_csfc->shell_surface);
	}
	csfc->shell_surface = shell_surface;
	focused_csfc = csfc;
	wl_list_insert(csfc->compositor->surface_list.prev, &csfc->link);
}

static void xdg_shell_pong(struct wl_client *wl_client,
			   struct wl_resource *resource, uint32_t serial)
{
	DLOG("%s\n", __FUNCTION__);
}

static const struct xdg_wm_base_interface xdg_shell_implementation = {
	.destroy = xdg_shell_destroy,
	.create_positioner = xdg_shell_create_positioner,
	.get_xdg_surface = xdg_shell_get_xdg_surface,
	.pong = xdg_shell_pong,
};

static void xdg_shell_bind(struct wl_client *wl_client, void *data,
			   uint32_t version, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
	compositor *compositor = data;
	shell_client *shell_client;

	shell_client = malloc(sizeof(*shell_client));
	if (shell_client == NULL) {
		if (wl_client != NULL)
			wl_client_post_no_memory(wl_client);
	}

	shell_client->compositor = compositor;
	shell_client->client = wl_client;
	wl_list_init(&shell_client->surface_list);

	shell_client->resource = wl_resource_create(
		wl_client, &xdg_wm_base_interface, version, id);
	if (shell_client->resource == NULL) {
		wl_client_post_no_memory(wl_client);
		free(shell_client);
	}

	wl_resource_set_implementation(shell_client->resource,
				       &xdg_shell_implementation, shell_client,
				       xdg_shell_client_destroy);
}

static int create_listening_socket(struct wl_display *display,
				   const char *socket_name)
{
	if (socket_name) {
		if (wl_display_add_socket(display, socket_name)) {
			ELOG("%s failed to add socket: %s\n", __FUNCTION__,
			     strerror(errno));
			return -1;
		}
	} else {
		socket_name = wl_display_add_socket_auto(display);
		if (!socket_name) {
			ELOG("%s failed to add socket: %s\n", __FUNCTION__,
			     strerror(errno));
			return -1;
		}
	}
	ILOG("------------------------------\n");
	ILOG("socket_name = %s\n", socket_name);
	ILOG("------------------------------\n");

	return 0;
}

static void usage(void)
{
	static const char program_name[] = "rvgpu-wlproxy";

	info("Usage: %s [options]\n", program_name);
	info("\t-s size       \tspecify compositor window size (default: 1024x768)\n");
	info("\t-S socket name\tspecify wayland socket name\n");
	info("\t-f fullscreen \tsend fullscreen configuration to client\n");
	info("\t-h help       \tShow this message\n");

	info("\nNote:\n");
}

static appopt_t parse_opt(int argc, char *argv[])
{
	int win_w = 1024;
	int win_h = 768;
	char *socket_name = NULL;
	bool sfc_fullscreen = false;
	bool vsync = false;
	bool windowed = false;

	{
		int c;
		const char *optstring = "s:S:fvh";
		while ((c = getopt(argc, argv, optstring)) != -1) {
			switch (c) {
			case 's':
				if (sscanf(optarg, "%dx%d", &win_w, &win_h) !=
				    2) {
					ELOG("%s invalid compositor size %s\n",
					     __FUNCTION__, optarg);
				}
				windowed = true;
				break;
			case 'S':
				socket_name = optarg;
				break;
			case 'f':
				sfc_fullscreen = true;
				break;
			case 'v':
				vsync = true;
				break;
			case 'h':
				usage();
				exit(0);
				break;
			}
		}
	}

	appopt_t appopt;
	appopt.win_w = win_w;
	appopt.win_h = win_h;
	appopt.socket_name = socket_name;
	appopt.sfc_fullscreen = sfc_fullscreen;
	appopt.windowed = windowed;
	appopt.vsync = vsync;
	return appopt;
}

bool has_writting_tex(compositor *compositor)
{
	bool has_wrtting_tex = false;
	compositor_surface *csfc;
	wl_list_for_each(csfc, &compositor->surface_list, link)
	{
		if (csfc->status[csfc->current_tex_index] == TEX_WRITING) {
			has_wrtting_tex = true;
		}
	}
	return has_wrtting_tex;
}

bool has_updated_tex(compositor *compositor)
{
	compositor_surface *csfc;
	bool has_updated_tex = false;
	wl_list_for_each(csfc, &compositor->surface_list, link)
	{
		if (csfc->status[csfc->current_tex_index] == TEX_WRITING) {
			GLenum result =
				glClientWaitSync(csfc->glsyncobj_tex,
						 GL_SYNC_FLUSH_COMMANDS_BIT, 0);
			if (result == GL_ALREADY_SIGNALED) {
				glDeleteSync(csfc->glsyncobj_tex);
				csfc->glsyncobj_tex = NULL;
				csfc->updated_tex_index =
					csfc->current_tex_index;
				csfc->current_tex_index =
					(csfc->current_tex_index + 1) % 2;
				csfc->status[csfc->current_tex_index] =
					TEX_FREE;
				csfc->status[csfc->updated_tex_index] =
					TEX_COMPLETE;
				has_updated_tex = true;

				pthread_mutex_lock(
					&csfc->compositor->event_mutex);
				if (csfc->wl_used_buffer != NULL) {
					wl_buffer_send_release(
						csfc->wl_used_buffer);
				}
				csfc->wl_used_buffer = csfc->wl_buffer;

				if (!wl_list_empty(
					    &csfc->frame_callback_list)) {
					compositor_frame_callback *cb, *cnext;
					struct wl_list frame_callback_list;
					wl_list_init(&frame_callback_list);
					wl_list_insert_list(
						&frame_callback_list,
						&csfc->frame_callback_list);
					wl_list_init(
						&csfc->frame_callback_list);
					uint32_t frame_time_msec =
						getCurrentTimeMs();
					wl_list_for_each_safe(
						cb, cnext, &frame_callback_list,
						link)
					{
						wl_callback_send_done(
							cb->resource,
							frame_time_msec);
						wl_resource_destroy(
							cb->resource);
					}
				}
				pthread_mutex_unlock(
					&csfc->compositor->event_mutex);
			}
		}
	}
	return has_updated_tex;
}

int update_surfaces(compositor *compositor, bool vsync)
{
	compositor_surface *csfc;
	int ret;
	glClear(GL_COLOR_BUFFER_BIT);
	wl_list_for_each(csfc, &compositor->surface_list, link)
	{
		if (csfc->status[csfc->updated_tex_index] == TEX_COMPLETE) {
			ret = draw_2d_texture(
				csfc->texid[csfc->updated_tex_index], 0, 0,
				csfc->img_w, csfc->img_h, 0);
			if (ret == -1)
				return ret;
		}
	}
	ret = egl_swap(vsync);
	return ret;
}

static int handle_signal(int signal_number, void *data)
{
	DLOG("%s\n", __FUNCTION__);
	compositor *compositor = data;
	egl_terminate();
	wl_display_destroy(compositor->wl_display);
	pthread_mutex_destroy(&compositor->event_mutex);
	free(compositor);
	exit(0);
	return 0;
}

/*--------------------------------------------------------------------------- *
 *      M A I N    F U N C T I O N
 *--------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
	appopt_t appopt = parse_opt(argc, argv);
	int win_w = appopt.win_w;
	int win_h = appopt.win_h;
	bool sfc_fullscreen = appopt.sfc_fullscreen;
	bool windowed = appopt.windowed;
	bool vsync = appopt.vsync;

	struct wl_display *wl_dpy = wl_display_create();
	int ret = create_listening_socket(wl_dpy, appopt.socket_name);
	if (ret == -1)
		return 0;

	ret = egl_init_with_platform_window_surface(2, 0, 0, 0, &win_w, &win_h, windowed);
	if (ret == -1)
		goto out;
	ret = egl_set_swap_interval(vsync);
	if (ret == -1)
		goto out;
	ret = egl_show_current_context_attrib();
	if (ret == -1)
		goto out;
	ret = egl_show_current_config_attrib();
	if (ret == -1)
		goto out;
	ret = egl_show_current_surface_attrib();
	if (ret == -1)
		goto out;
	ret = egl_show_gl_info();
	if (ret == -1)
		goto out;

        compositor *compositor = calloc(sizeof(*compositor), 1);
        compositor->wl_display = wl_dpy;
        compositor->width = win_w;
        compositor->height = win_h;
        compositor->sfc_fullscreen = sfc_fullscreen;
        pthread_mutex_init(&compositor->event_mutex, NULL);
        wl_list_init(&compositor->surface_list);
        wl_list_init(&compositor->client_list);

        wl_global_create(wl_dpy, &wl_compositor_interface, 4, compositor,
                         compositor_bind);
        wl_global_create(wl_dpy, &wl_output_interface, 3, compositor,
                         output_bind);
        wl_global_create(wl_dpy, &wl_shell_interface, 1, compositor,
                         wl_shell_bind);
        wl_global_create(wl_dpy, &xdg_wm_base_interface, 1, compositor,
                         xdg_shell_bind);
        wl_display_init_shm(wl_dpy);

        struct wl_event_loop *eloop = wl_display_get_event_loop(wl_dpy);
        wl_event_loop_add_signal(eloop, SIGINT, handle_signal, compositor);
        wl_event_loop_add_signal(eloop, SIGTERM, handle_signal, compositor);

	init_2d_renderer(win_w, win_h);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	compositor_seat_init(compositor);

	EGL_GET_PROC_ADDR(eglBindWaylandDisplayWL);
	EGL_GET_PROC_ADDR(glEGLImageTargetTexture2DOES);
	EGL_GET_PROC_ADDR(eglQueryWaylandBufferWL);
	EGL_GET_PROC_ADDR(eglCreateImageKHR);
	EGL_GET_PROC_ADDR(eglDestroyImageKHR);

	EGLDisplay dpy = egl_get_display();
	const char *extensions = eglQueryString(dpy, EGL_EXTENSIONS);
	if (extensions != NULL) {
		ILOG("Support EGL EXTENSIONS: %s\n", extensions);
	} else {
		ILOG("Cannot find EGL EXTENSIONS\n");
	}

	if (strstr(extensions, "EGL_WL_bind_wayland_display") != NULL) {
		eglBindWaylandDisplayWL(dpy, wl_dpy);
	} else {
		ILOG("EGL_WL_bind_wayland_display is not supported\n");
	}

	glClear(GL_COLOR_BUFFER_BIT);
	ret = egl_swap(vsync);
	if (ret == -1)
		goto out;

	int pre_csfc_num = 0;
	for (int count = 0;; count++) {
		pthread_mutex_lock(&compositor->event_mutex);
		wl_display_flush_clients(wl_dpy);
		pthread_mutex_unlock(&compositor->event_mutex);
		wl_event_loop_dispatch(eloop, -1);

		bool need_update_tex_draw = has_writting_tex(compositor);
		if (need_update_tex_draw) {
			while (has_writting_tex(compositor)) {
				if (has_updated_tex(compositor)) {
					ret = update_surfaces(compositor,
							      vsync);
				} else {
					usleep(100);
				}
			}
		} else {
			int csfc_num =
				wl_list_length(&compositor->surface_list);
			if (csfc_num != pre_csfc_num) {
				ret = update_surfaces(compositor, vsync);
				pre_csfc_num = csfc_num;
			}
		}
		if (ret == -1)
			break;
	}

out:
	egl_terminate();
	wl_display_destroy(wl_dpy);
	pthread_mutex_destroy(&compositor->event_mutex);
	free(compositor);
	return 0;
}
