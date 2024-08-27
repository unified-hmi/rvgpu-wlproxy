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

#ifndef WINSYS_DRM_H_
#define WINSYS_DRM_h_

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#define MAX_DEVICES 100

typedef struct modeset_dev {
	struct modeset_dev *next;

	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t size;
	uint32_t handle;
	uint8_t *map;

	drmModeModeInfo mode;
	uint32_t conn;
	uint32_t encd;
	uint32_t crtc;

	int conn_idx;
	int drm_fd;
} modeset_dev_t;

typedef struct drm_fb {
	unsigned int fb_id, stride, handle;
	unsigned int width, height;
	int prime_fd;
	struct gbm_bo *bo;
	int bo_fd; /* for client */
} drm_fb_t;

#endif /* WINSYS_DRM_H_ */
