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

#define MAX_DMABUF_PLANES 4

struct linux_dmabuf_buffer;
typedef void (*dmabuf_user_data_destroy_func)(
                        struct linux_dmabuf_buffer *buffer);

struct imported_egl_tex {
    EGLDisplay dpy;
    EGLImageKHR image;
    GLuint tex;
    GLenum target;
    int width;
    int height;
};

struct dmabuf_attributes {
        int32_t width;
        int32_t height;
        uint32_t format;
        uint32_t flags;
        int n_planes;
        int fd[MAX_DMABUF_PLANES];
        uint32_t offset[MAX_DMABUF_PLANES];
        uint32_t stride[MAX_DMABUF_PLANES];
        uint64_t modifier[MAX_DMABUF_PLANES];
};

struct linux_dmabuf_buffer {
        struct wl_resource *buffer_resource;
        struct wl_resource *params_resource;
        struct weston_compositor *compositor;
        struct dmabuf_attributes attributes;

        void *user_data;
        dmabuf_user_data_destroy_func user_data_destroy_func;

        bool direct_display;
};



int
linux_dmabuf_setup(compositor *compositor);
