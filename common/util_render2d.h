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

#ifndef _UTIL_RENDER_2D_H_
#define _UTIL_RENDER_2D_H_

#define RENDER2D_FLIP_V (1 << 0)
#define RENDER2D_FLIP_H (1 << 1)
#define M_PId180f (3.1415926f / 180.0f)

#ifdef __cplusplus
extern "C" {
#endif

int set_2d_projection_matrix(int w, int h);
int init_2d_renderer(int w, int h);
int draw_2d_texture(int texid, int x, int y, int w, int h, int upsidedown);

#ifdef __cplusplus
}
#endif
#endif /* _UTIL_RENDER_2D_H_ */
