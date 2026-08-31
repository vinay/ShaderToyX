/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef SHADER_H
#define SHADER_H

#include "gl_lite.h"

#define SHADER_ERROR_LOG_SIZE 4096

/* Sound shaders are evaluated on the GPU in blocks: each pixel of a
   SOUND_TEX_W x SOUND_TEX_H RGBA8 target encodes one stereo sample
   (left in RG, right in BA, 16 bits each). The width is baked into the
   sound shader wrapper, so keep these in sync with the renderer. */
#define SOUND_TEX_W          512
#define SOUND_TEX_H          128
#define SOUND_BLOCK_SAMPLES  (SOUND_TEX_W * SOUND_TEX_H)

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
    int   iSampleOffset;          /* first sample index of a sound block */
} ShaderUniforms;

typedef struct ShaderProgram
{
    GLuint program;
    char   compile_error[SHADER_ERROR_LOG_SIZE];
    int    valid;
} ShaderProgram;

void shader_init(ShaderProgram *sp);
void shader_destroy(ShaderProgram *sp);
/* is_sound selects the wrapper: mainImage (visual) or mainSound (audio) */
int  shader_compile(ShaderProgram *sp, const char *user_source, int is_sound);
void shader_set_uniforms(ShaderProgram *sp, const ShaderUniforms *uniforms);

/* Release resources shared by all programs (the cached vertex shader).
   Call once at shutdown while the GL context is still current. */
void shader_shutdown(void);

#endif
