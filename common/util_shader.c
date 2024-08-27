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
#include <string.h>
#include "util_gles_header.h"
#include "util_shader.h"
#include "util_log.h"
#include "assertgl.h"
#include "util_egl.h"

/* ----------------------------------------------------------- *
 *   create & compile shader
 * ----------------------------------------------------------- */

GLuint compile_shader_text(GLenum shaderType, const char *text)
{
	GLuint shader;
	GLint stat;

	shader = glCreateShader(shaderType);
	glShaderSource(shader, 1, (const char **)&text, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &stat);
	if (!stat) {
		GLsizei len;
		char *lpBuf;

		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		lpBuf = (char *)malloc(len);

		glGetShaderInfoLog(shader, len, &len, lpBuf);
		ELOG("Error: problem compiling shader.\n");
		ELOG("-----------------------------------\n");
		ELOG("%s\n", lpBuf);
		ELOG("-----------------------------------\n");

		free(lpBuf);

		return 0;
	}

	GLASSERT();
	return shader;
}

/* ----------------------------------------------------------- *
 *    link shaders
 * ----------------------------------------------------------- */
GLuint link_shaders(GLuint vertShader, GLuint fragShader)
{
	GLuint program = glCreateProgram();

	if (fragShader)
		glAttachShader(program, fragShader);
	if (vertShader)
		glAttachShader(program, vertShader);

	glLinkProgram(program);

	{
		GLint stat;
		glGetProgramiv(program, GL_LINK_STATUS, &stat);
		if (!stat) {
			GLsizei len;
			char *lpBuf;

			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
			lpBuf = (char *)malloc(len);

			glGetProgramInfoLog(program, len, &len, lpBuf);
			ELOG("Error: problem linking shader.\n");
			ELOG("-----------------------------------\n");
			ELOG("%s\n", lpBuf);
			ELOG("-----------------------------------\n");

			free(lpBuf);

			return 0;
		}
	}

	return program;
}

int generate_shader(shader_obj_t *sobj, char *str_vs, char *str_fs)
{
	GLuint fs, vs, program;

	vs = compile_shader_text(GL_VERTEX_SHADER, str_vs);
	fs = compile_shader_text(GL_FRAGMENT_SHADER, str_fs);
	if (vs == 0 || fs == 0) {
		ELOG("Failed to compile shader.\n");
		return -1;
	}

	program = link_shaders(vs, fs);
	if (program == 0) {
		ELOG("Failed to link shaders.\n");
		return -1;
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	sobj->program = program;
	sobj->loc_vtx = glGetAttribLocation(program, "a_Vertex");
	sobj->loc_nrm = glGetAttribLocation(program, "a_Normal");
	sobj->loc_clr = glGetAttribLocation(program, "a_Color");
	sobj->loc_uv = glGetAttribLocation(program, "a_TexCoord");
	sobj->loc_tex = glGetUniformLocation(program, "u_sampler");
	sobj->loc_mtx = glGetUniformLocation(program, "u_PMVMatrix");
	sobj->loc_mtx_nrm = glGetUniformLocation(program, "u_NrmMatrix");

	return 0;
}
