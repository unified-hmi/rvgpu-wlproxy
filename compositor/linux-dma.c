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

#include <unistd.h>
#include <string.h>
#include <wayland-server-protocol.h>
#include <linux-dmabuf-unstable-v1-server-protocol.h>
#include <drm/drm_fourcc.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include "util_egl.h"
#include "util_log.h"
#include "compositor.h"
#include "linux-dma.h"

extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
extern PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
extern PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;

static void
linux_dmabuf_buffer_destroy(struct linux_dmabuf_buffer *buffer)
{
	int i;
        for (i = 0; i < buffer->attributes.n_planes; i++) {
                close(buffer->attributes.fd[i]);
                buffer->attributes.fd[i] = -1;
        }

        buffer->attributes.n_planes = 0;
	free(buffer);
}

static void
params_destroy(struct wl_client *client, struct wl_resource *resource)
{
	DLOG("%s\n", __FUNCTION__);
        wl_resource_destroy(resource);
}


static void
params_add(struct wl_client *client,
           struct wl_resource *params_resource,
           int32_t name_fd,
           uint32_t plane_idx,
           uint32_t offset,
           uint32_t stride,
           uint32_t modifier_hi,
           uint32_t modifier_lo)
{
	DLOG("%s\n", __FUNCTION__);
        struct linux_dmabuf_buffer *buffer;
        buffer = wl_resource_get_user_data(params_resource);
        if (!buffer) {
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                        "params was already used to create a wl_buffer");
                close(name_fd);
                return;
        }

        if (plane_idx >= MAX_DMABUF_PLANES) {
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                        "plane index %u is too high", plane_idx);
                close(name_fd);
                return;
        }

        if (buffer->attributes.fd[plane_idx] != -1) {
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                        "a dmabuf has already been added for plane %u",
                        plane_idx);
                close(name_fd);
                return;
        }

        buffer->attributes.fd[plane_idx] = name_fd;
        buffer->attributes.offset[plane_idx] = offset;
        buffer->attributes.stride[plane_idx] = stride;

        if (wl_resource_get_version(params_resource) < ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
                buffer->attributes.modifier[plane_idx] = DRM_FORMAT_MOD_INVALID;
        else
                buffer->attributes.modifier[plane_idx] = ((uint64_t)modifier_hi << 32) |
                                                         modifier_lo;

        buffer->attributes.n_planes++;
}



static void
linux_dmabuf_wl_buffer_destroy(struct wl_client *client,
                               struct wl_resource *resource)
{
        wl_resource_destroy(resource);
}


static const struct wl_buffer_interface linux_dmabuf_buffer_implementation = {
        linux_dmabuf_wl_buffer_destroy
};

static void
destroy_linux_dmabuf_wl_buffer(struct wl_resource *resource)
{
        struct linux_dmabuf_buffer *buffer;

        buffer = wl_resource_get_user_data(resource);

        if (buffer->user_data_destroy_func)
                buffer->user_data_destroy_func(buffer);

        linux_dmabuf_buffer_destroy(buffer);
}


bool get_cap_dma_buf_import_extensions(EGLDisplay dpy)
{
        const char *eglExtensions = eglQueryString(dpy, EGL_EXTENSIONS);
        bool ret = true;
        if (strstr(eglExtensions, "EGL_EXT_image_dma_buf_import") == NULL) {
                fprintf(stderr,
                        "EGL_EXT_image_dma_buf_import is not supported\n");
                ret = false;
        }

        if (strstr(eglExtensions, "EGL_KHR_image_base") == NULL) {
                fprintf(stderr, "EGL_KHR_image_base is not supported\n");
                ret = false;
        }

        if (strstr(eglExtensions, "EGL_KHR_gl_texture_2D_image") == NULL) {
                fprintf(stderr,
                        "EGL_KHR_gl_texture_2D_image is not supported\n");
                ret = false;
        }
        if (strstr(eglExtensions, "EGL_EXT_image_dma_buf_import_modifiers") ==
            NULL) {
                fprintf(stderr,
                        "EGL_EXT_image_dma_buf_import_modifiers is not supported\n");
                ret = false;
        }
        return ret;
}

EGLImageKHR create_egl_image_from_dmabuf(EGLDisplay dpy,
                                         const struct dmabuf_attributes *a)
{

    if (a->n_planes <= 0 || a->n_planes > 4) {
        return EGL_NO_IMAGE_KHR;
    }
    if (a->width <= 0 || a->height <= 0) {
        return EGL_NO_IMAGE_KHR;
    }
    for (int i = 0; i < a->n_planes; i++) {
        if (a->fd[i] < 0)
            return EGL_NO_IMAGE_KHR;
        if (a->stride[i] == 0)
            return EGL_NO_IMAGE_KHR;
    }

    EGLint attrs[64];
    int k = 0;

    attrs[k++] = EGL_WIDTH;                attrs[k++] = (EGLint)a->width;
    attrs[k++] = EGL_HEIGHT;               attrs[k++] = (EGLint)a->height;
    attrs[k++] = EGL_LINUX_DRM_FOURCC_EXT; attrs[k++] = (EGLint)a->format;


    for (int i = 0; i < a->n_planes; i++) {
        EGLint fd_key =
            (i == 0) ? EGL_DMA_BUF_PLANE0_FD_EXT :
            (i == 1) ? EGL_DMA_BUF_PLANE1_FD_EXT :
            (i == 2) ? EGL_DMA_BUF_PLANE2_FD_EXT :
                       EGL_DMA_BUF_PLANE3_FD_EXT;
        EGLint off_key =
            (i == 0) ? EGL_DMA_BUF_PLANE0_OFFSET_EXT :
            (i == 1) ? EGL_DMA_BUF_PLANE1_OFFSET_EXT :
            (i == 2) ? EGL_DMA_BUF_PLANE2_OFFSET_EXT :
                       EGL_DMA_BUF_PLANE3_OFFSET_EXT;
        EGLint pitch_key =
            (i == 0) ? EGL_DMA_BUF_PLANE0_PITCH_EXT :
            (i == 1) ? EGL_DMA_BUF_PLANE1_PITCH_EXT :
            (i == 2) ? EGL_DMA_BUF_PLANE2_PITCH_EXT :
                       EGL_DMA_BUF_PLANE3_PITCH_EXT;

        attrs[k++] = fd_key;    attrs[k++] = (EGLint)a->fd[i];
        attrs[k++] = off_key;   attrs[k++] = (EGLint)a->offset[i];
        attrs[k++] = pitch_key; attrs[k++] = (EGLint)a->stride[i];

        if (a->modifier[i] != DRM_FORMAT_MOD_INVALID) {
            EGLint mod_hi_key =
                (i == 0) ? EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT :
                (i == 1) ? EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT :
                (i == 2) ? EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT :
                           EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
            EGLint mod_lo_key =
                (i == 0) ? EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT :
                (i == 1) ? EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT :
                (i == 2) ? EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT :
                           EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
            uint64_t mod = a->modifier[i];
            EGLint hi = (EGLint)(mod >> 32);
            EGLint lo = (EGLint)(mod & 0xffffffffu);
            attrs[k++] = mod_hi_key; attrs[k++] = hi;
            attrs[k++] = mod_lo_key; attrs[k++] = lo;
        }
    }

    attrs[k++] = EGL_NONE;

    EGLImageKHR image = eglCreateImageKHR(
        dpy,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        (EGLClientBuffer)NULL,
        attrs
    );
    return image;
}

static void dmabuf_user_data_destroy(struct linux_dmabuf_buffer *buffer)
{
    struct imported_egl_tex *imp = (struct imported_egl_tex *)buffer->user_data;
    if (!imp) return;

    if (imp->tex) {
        glDeleteTextures(1, &imp->tex);
        imp->tex = 0;
    }
    if (imp->image != EGL_NO_IMAGE_KHR) {
        if (eglDestroyImageKHR)
            eglDestroyImageKHR(imp->dpy, imp->image);
        imp->image = EGL_NO_IMAGE_KHR;
    }
    free(imp);
    buffer->user_data = NULL;
}

bool compositor_import_dmabuf(compositor *c, struct linux_dmabuf_buffer *buffer)
{
    EGLDisplay dpy = egl_get_display();

    if (!get_cap_dma_buf_import_extensions(dpy)) {
            return false;
    }

    EGLImageKHR image = create_egl_image_from_dmabuf(dpy, &buffer->attributes);
    if (image == EGL_NO_IMAGE_KHR) {
        return false;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) {
        if (eglDestroyImageKHR)
            eglDestroyImageKHR(dpy, image);
        return false;
    }

    GLenum target = GL_TEXTURE_2D;
    glBindTexture(target, tex);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (!glEGLImageTargetTexture2DOES) {
        glDeleteTextures(1, &tex);
        if (eglDestroyImageKHR)
            eglDestroyImageKHR(dpy, image);
        return false;
    }

    glEGLImageTargetTexture2DOES(target, image);

    GLenum glerr = glGetError();
    if (glerr != GL_NO_ERROR) {
        glDeleteTextures(1, &tex);
        if (eglDestroyImageKHR)
            eglDestroyImageKHR(dpy, image);
        return false;
    }

    struct imported_egl_tex *imp = calloc(1, sizeof(struct imported_egl_tex));
    if (!imp) {
        glDeleteTextures(1, &tex);
        if (eglDestroyImageKHR)
            eglDestroyImageKHR(dpy, image);
        return false;
    }
    imp->dpy = dpy;
    imp->image = image;
    imp->tex = tex;
    imp->target = target;
    imp->width = buffer->attributes.width;
    imp->height = buffer->attributes.height;

    buffer->user_data = imp;
    buffer->user_data_destroy_func = dmabuf_user_data_destroy;

    return true;
}

static void
params_create_common(struct wl_client *client,
                     struct wl_resource *params_resource,
                     uint32_t buffer_id,
                     int32_t width,
                     int32_t height,
                     uint32_t format,
                     uint32_t flags)
{
        DLOG("%s\n", __FUNCTION__);
        struct linux_dmabuf_buffer *buffer;
        int i;

        buffer = wl_resource_get_user_data(params_resource);

        if (!buffer) {
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                        "params was already used to create a wl_buffer");
                return;
        }

        wl_resource_set_user_data(buffer->params_resource, NULL);
        buffer->params_resource = NULL;

        if (!buffer->attributes.n_planes) {
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                        "no dmabuf has been added to the params");
                goto err_out;
        }

        for (i = 0; i < buffer->attributes.n_planes; i++) {
                if (buffer->attributes.fd[i] == -1) {
                        wl_resource_post_error(params_resource,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                                "no dmabuf has been added for plane %i", i);
                        goto err_out;
                }
        }

        buffer->attributes.width = width;
        buffer->attributes.height = height;
        buffer->attributes.format = format;
        buffer->attributes.flags = flags;
        if (width < 1 || height < 1) {
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
                        "invalid width %d or height %d", width, height);
                goto err_out;
        }

        for (i = 0; i < buffer->attributes.n_planes; i++) {
                off_t size;

                if ((uint64_t) buffer->attributes.offset[i] + buffer->attributes.stride[i] > UINT32_MAX) {
                        wl_resource_post_error(params_resource,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                "size overflow for plane %i", i);
                        goto err_out;
                }

                if (i == 0 &&
                   (uint64_t) buffer->attributes.offset[i] +
                   (uint64_t) buffer->attributes.stride[i] * height > UINT32_MAX) {
                        wl_resource_post_error(params_resource,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                "size overflow for plane %i", i);
                        goto err_out;
                }

                size = lseek(buffer->attributes.fd[i], 0, SEEK_END);
                if (size == -1)
                        continue;

                if (buffer->attributes.offset[i] >= size) {
                        wl_resource_post_error(params_resource,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                "invalid offset %i for plane %i",
                                buffer->attributes.offset[i], i);
                        goto err_out;
                }

                if (buffer->attributes.offset[i] + buffer->attributes.stride[i] > size) {
                        wl_resource_post_error(params_resource,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                "invalid stride %i for plane %i",
                                buffer->attributes.stride[i], i);
                        goto err_out;
                }

                if (i == 0 &&
                    buffer->attributes.offset[i] + buffer->attributes.stride[i] * height > size) {
                        wl_resource_post_error(params_resource,
                                ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                                "invalid buffer stride or height for plane %i", i);
                        goto err_out;
                }
        }

        if (!compositor_import_dmabuf(buffer->compositor, buffer)) {
                goto err_failed;
        }

        buffer->buffer_resource = wl_resource_create(client,
                                                     &wl_buffer_interface,
                                                     1, buffer_id);
        if (!buffer->buffer_resource) {
                wl_resource_post_no_memory(params_resource);
                goto err_buffer;
        }

        wl_resource_set_implementation(buffer->buffer_resource,
                                       &linux_dmabuf_buffer_implementation,
                                       buffer, destroy_linux_dmabuf_wl_buffer);

        if (buffer_id == 0)
                zwp_linux_buffer_params_v1_send_created(params_resource,
                                                buffer->buffer_resource);

        return;
err_buffer:
        if (buffer->user_data_destroy_func)
                buffer->user_data_destroy_func(buffer);
err_failed:
        if (buffer_id == 0)
                zwp_linux_buffer_params_v1_send_failed(params_resource);
        else
                wl_resource_post_error(params_resource,
                        ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                        "importing the supplied dmabufs failed");

err_out:
        linux_dmabuf_buffer_destroy(buffer);
}


static void
params_create(struct wl_client *client,
              struct wl_resource *params_resource,
              int32_t width,
              int32_t height,
              uint32_t format,
              uint32_t flags)
{
	DLOG("%s\n", __FUNCTION__);
        params_create_common(client, params_resource, 0, width, height, format,
                             flags);
}

static void
params_create_immed(struct wl_client *client,
                    struct wl_resource *params_resource,
                    uint32_t buffer_id,
                    int32_t width,
                    int32_t height,
                    uint32_t format,
                    uint32_t flags)
{
	DLOG("%s\n", __FUNCTION__);
        params_create_common(client, params_resource, buffer_id, width, height,
                             format, flags);
}

static const struct zwp_linux_buffer_params_v1_interface
zwp_linux_buffer_params_implementation = {
        params_destroy,
        params_add,
        params_create,
        params_create_immed
};

static void
linux_dmabuf_destroy(struct wl_client *client, struct wl_resource *resource)
{
        wl_resource_destroy(resource);
}

static void
destroy_params(struct wl_resource *params_resource)
{
        DLOG("%s\n", __FUNCTION__);
        struct linux_dmabuf_buffer *buffer;

        buffer = wl_resource_get_user_data(params_resource);

        if (!buffer)
                return;

        linux_dmabuf_buffer_destroy(buffer);
}

static void
linux_dmabuf_create_params(struct wl_client *client,
                           struct wl_resource *linux_dmabuf_resource,
                           uint32_t params_id)
{
	DLOG("%s\n", __FUNCTION__);
        compositor *compositor;
        struct linux_dmabuf_buffer *buffer;
        uint32_t version;
        int i;
        version = wl_resource_get_version(linux_dmabuf_resource);
        compositor = wl_resource_get_user_data(linux_dmabuf_resource);

	buffer = calloc(1, sizeof(struct linux_dmabuf_buffer));
        if (!buffer)
                goto err_out;

        buffer->attributes.n_planes = 0;
        for (i = 0; i < MAX_DMABUF_PLANES; i++) {
                buffer->attributes.fd[i] = -1;
		buffer->attributes.modifier[i] = DRM_FORMAT_MOD_INVALID;
	}

        buffer->compositor = compositor;
        buffer->direct_display = false;
        buffer->params_resource =
                wl_resource_create(client,
                                   &zwp_linux_buffer_params_v1_interface,
                                   version, params_id);
        if (!buffer->params_resource)
                goto err_dealloc;

        wl_resource_set_implementation(buffer->params_resource,
                                       &zwp_linux_buffer_params_implementation,
                                       buffer, destroy_params);
        return;

err_dealloc:
    free(buffer);
err_out:
    wl_resource_post_no_memory(linux_dmabuf_resource);
}


static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_implementation = {
        linux_dmabuf_destroy,
        linux_dmabuf_create_params
};

static void
bind_linux_dmabuf(struct wl_client *client,
                  void *data, uint32_t version, uint32_t id)
{
	DLOG("%s\n", __FUNCTION__);
        compositor *compositor = data;
        struct wl_resource *resource;
        const uint64_t *modifiers;
        unsigned int num_modifiers;
        unsigned int i;

	compositor->sfc_bind_zwp_linux_dmabuf_v1 = true;
        resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface,
                                      version, id);
        if (resource == NULL) {
                wl_client_post_no_memory(client);
                return;
        }
        wl_resource_set_implementation(resource, &linux_dmabuf_implementation,
                                       compositor, NULL);

        const uint32_t fmt = DRM_FORMAT_ARGB8888;
        const uint64_t mod = DRM_FORMAT_MOD_LINEAR;
        zwp_linux_dmabuf_v1_send_format(resource, fmt);
}

int
linux_dmabuf_setup(compositor *compositor)
{
        if (!wl_global_create(compositor->wl_display,
                              &zwp_linux_dmabuf_v1_interface, 1,
                              compositor, bind_linux_dmabuf))
                return -1;

        return 0;
}
