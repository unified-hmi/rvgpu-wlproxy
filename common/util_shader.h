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

#ifndef SHADER_UTIL_H
#define SHADER_UTIL_H

typedef struct shader_obj_t {
	GLuint program;
	GLint loc_vtx;
	GLint loc_nrm;
	GLint loc_clr;
	GLint loc_uv;
	GLint loc_tex;
	GLint loc_mtx;
	GLint loc_mtx_nrm;
} shader_obj_t;

#ifdef __cplusplus
extern "C" {
#endif

int generate_shader(shader_obj_t *sobj, char *str_vs, char *str_fs);

#ifdef __cplusplus
}
#endif

#endif /* SHADER_UTIL_H */
