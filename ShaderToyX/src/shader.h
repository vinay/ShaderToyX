/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef SHADER_H
#define SHADER_H

#include "gl_lite.h"

#define SHADER_ERROR_LOG_SIZE 4096

typedef struct ShaderUniforms
{
    float iResolution[3];
    float iTime;
    float iTimeDelta;
    int   iFrame;
    float iMouse[4];
    float iDate[4];
    float iSampleRate;
    float iChannelTime[4];
    float iChannelResolution[12]; /* 4 x vec3 packed */
} ShaderUniforms;

typedef struct ShaderProgram
{
    GLuint program;
    char   compile_error[SHADER_ERROR_LOG_SIZE];
    int    valid;
} ShaderProgram;

void shader_init(ShaderProgram *sp);
void shader_destroy(ShaderProgram *sp);
int  shader_compile(ShaderProgram *sp, const char *user_source);
void shader_set_uniforms(ShaderProgram *sp, const ShaderUniforms *uniforms);

/* Release resources shared by all programs (the cached vertex shader).
   Call once at shutdown while the GL context is still current. */
void shader_shutdown(void);

#endif
