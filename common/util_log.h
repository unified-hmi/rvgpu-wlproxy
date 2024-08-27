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

#ifndef _UTIL_LOG_H_
#define _UTIL_LOG_H_

#include <stdio.h>
#include <stdlib.h>
#include "util_env.h"

#define LOG(...)                                                               \
	do {                                                                   \
		fprintf(stderr, __VA_ARGS__);                                  \
	} while (0)

#define ILOG(...)                                                              \
	do {                                                                   \
		fprintf(stderr, "INFO(%s:%d) : ", __FILE__, __LINE__);         \
		fprintf(stderr, __VA_ARGS__);                                  \
	} while (0)

#define WLOG(...)                                                              \
	do {                                                                   \
		fprintf(stderr, "WARN(%s:%d) : ", __FILE__, __LINE__);         \
		fprintf(stderr, __VA_ARGS__);                                  \
	} while (0)

#define ELOG(...)                                                              \
	do {                                                                   \
		fprintf(stderr, "ERROR(%s:%d) : ", __FILE__, __LINE__);        \
		fprintf(stderr, __VA_ARGS__);                                  \
	} while (0)

#define DLOG(...)                                                              \
	do {                                                                   \
		if (getenv_int("DEBUG_LOG", 0)) {                              \
			fprintf(stderr, "DEBUG(%s:%d) : ", __FILE__,           \
				__LINE__);                                     \
			fprintf(stderr, __VA_ARGS__);                          \
		}                                                              \
	} while (0)

#define ASSERT(cond, ...)                                                      \
	do {                                                                   \
		if (!(cond)) {                                                 \
			fprintf(stderr, "ERROR(%s:%d) : ", __FILE__,           \
				__LINE__);                                     \
			fprintf(stderr, __VA_ARGS__);                          \
			exit(-1);                                              \
		}                                                              \
	} while (0)

#endif /* _UTIL_LOG_H_ */
