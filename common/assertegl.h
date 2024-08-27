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

#ifndef __ASSERTEGL_H__
#define __ASSERTEGL_H__

#ifdef __cplusplus
extern "C" {
#endif

void AssertEGLError(char *lpFile, int nLine);

#if 1
#define EGLASSERT() AssertEGLError(__FILE__, __LINE__)
#else
#define EGLASSERT() ((void)0)
#endif

#ifdef __cplusplus
}
#endif
#endif /* __ASSERTEGL_H__ */
