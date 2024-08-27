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
#include <math.h>
#include <GLES2/gl2.h>
#include "assertgl.h"
#include "util_log.h"

static char *GetGLErrMsg(int nCode)
{
	switch (nCode) {
	case 0x0000:
		return "GL_NO_ERROR";
		break;
	case 0x0500:
		return "GL_INVALID_ENUM";
		break;
	case 0x0501:
		return "GL_INVALID_VALUE";
		break;
	case 0x0502:
		return "GL_INVALID_OPERATION";
		break;
	case 0x0503:
		return "GL_STACK_OVERFLOW";
		break;
	case 0x0504:
		return "GL_STACK_UNDERFLOW";
		break;
	case 0x0505:
		return "GL_OUT_OF_MEMORY";
		break;
	default:
		return "UNKNOWN ERROR";
		break;
	}
}

void AssertGLError(const char *lpFile, int nLine)
{
	int error;

	while ((error = glGetError()) != GL_NO_ERROR) {
		ELOG("[GL ASSERT ERR] \"%s\"(%d):0x%04x(%s)\n", lpFile, nLine,
		     error, GetGLErrMsg(error));
	}
}
