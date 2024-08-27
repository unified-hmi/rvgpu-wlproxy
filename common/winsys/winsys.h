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

#ifndef _WINSYS_H_
#define _WINSYS_H_

#include <stdbool.h>

void *winsys_init_native_display(void);
void *winsys_init_native_window(void *dpy, int win_w, int win_h);
int winsys_swap(bool vsync);
void *winsys_create_native_pixmap(int width, int height);
#endif /* _WINSYS_H_ */
