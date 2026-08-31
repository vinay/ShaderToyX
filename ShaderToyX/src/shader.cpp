/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#include "shader.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/*  Vertex shader: full-screen triangle strip                         */
/* ------------------------------------------------------------------ */
static const char *vertex_source =
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

/* The vertex shader never changes, so compile it once and reuse it */
static GLuint g_vertex_shader = 0;

/* ------------------------------------------------------------------ */
/*  Fragment shader header: uniforms matching ShaderToy                */
/* ------------------------------------------------------------------ */
static const char *fragment_header =
    "#version 330 core\n"
    "out vec4 _stx_FragColor;\n"
    "\n"
    "uniform vec3      iResolution;\n"
    "uniform float     iTime;\n"
    "uniform float     iTimeDelta;\n"
    "uniform int       iFrame;\n"
    "uniform vec4      iMouse;\n"
    "uniform vec4      iDate;\n"
    "uniform float     iSampleRate;\n"
    "uniform float     iChannelTime[4];\n"
    "uniform vec3      iChannelResolution[4];\n"
    "uniform sampler2D iChannel0;\n"
    "uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2;\n"
    "uniform sampler2D iChannel3;\n"
    "uniform int       iSampleOffset;\n"
    "\n"
    "#line 1\n";

/* ------------------------------------------------------------------ */
/*  Fragment shader footer: calls user's mainImage                     */
/* ------------------------------------------------------------------ */
static const char *fragment_footer =
    "\n"
    "void main()\n"
    "{\n"
    "    vec4 _stx_color = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "    mainImage(_stx_color, gl_FragCoord.xy);\n"
    "    _stx_FragColor = _stx_color;\n"
    "}\n";

/* ------------------------------------------------------------------ */
/*  Sound shader footer: calls the user's mainSound once per pixel.    */
/*  Each pixel is one stereo sample; left/right are clamped to ±1 and  */
/*  stored as 16-bit values split across the four 8-bit channels       */
/*  (left low/high in RG, right low/high in BA).                       */
/* ------------------------------------------------------------------ */
#define STX_STR2(x) #x
#define STX_STR(x)  STX_STR2(x)

static const char *sound_footer =
    "\n"
    "void main()\n"
    "{\n"
    "    int  _stx_samp = iSampleOffset + int(gl_FragCoord.y) * " STX_STR(SOUND_TEX_W) " + int(gl_FragCoord.x);\n"
    "    vec2 _stx_snd  = clamp(mainSound(_stx_samp, float(_stx_samp) / iSampleRate), -1.0, 1.0);\n"
    "    vec2 _stx_u16  = floor((0.5 + 0.5 * _stx_snd) * 65535.0);\n"
    "    vec2 _stx_lo   = mod(_stx_u16, 256.0) / 255.0;\n"
    "    vec2 _stx_hi   = floor(_stx_u16 / 256.0) / 255.0;\n"
    "    _stx_FragColor = vec4(_stx_lo.x, _stx_hi.x, _stx_lo.y, _stx_hi.y);\n"
    "}\n";

/* ------------------------------------------------------------------ */
void shader_init(ShaderProgram *sp)
{
    memset(sp, 0, sizeof(ShaderProgram));
}

/* ------------------------------------------------------------------ */
void shader_destroy(ShaderProgram *sp)
{
    if (sp->program)
    {
        glDeleteProgram(sp->program);
    }
    memset(sp, 0, sizeof(ShaderProgram));
}

/* ------------------------------------------------------------------ */
void shader_shutdown(void)
{
    if (g_vertex_shader)
    {
        glDeleteShader(g_vertex_shader);
        g_vertex_shader = 0;
    }
}

/* ------------------------------------------------------------------ */
static GLuint compile_shader(GLenum type, const char *source, char *error, int error_size)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, NULL);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        glGetShaderInfoLog(s, error_size, NULL, error);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/* ------------------------------------------------------------------ */
int shader_compile(ShaderProgram *sp, const char *user_source, int is_sound)
{
    sp->compile_error[0] = '\0';

    /* Compile the shared vertex shader on first use */
    if (!g_vertex_shader)
    {
        g_vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source,
                                         sp->compile_error, SHADER_ERROR_LOG_SIZE);
        if (!g_vertex_shader)
        {
            return 0;
        }
    }

    /* Build full fragment source: header + user code + footer */
    const char *footer = is_sound ? sound_footer : fragment_footer;
    size_t hlen = strlen(fragment_header);
    size_t ulen = strlen(user_source);
    size_t flen = strlen(footer);
    size_t total = hlen + ulen + flen + 1;

    char *full_src = (char *)malloc(total);
    if (!full_src)
    {
        snprintf(sp->compile_error, SHADER_ERROR_LOG_SIZE, "Out of memory");
        return 0;
    }

    memcpy(full_src, fragment_header, hlen);
    memcpy(full_src + hlen, user_source, ulen);
    memcpy(full_src + hlen + ulen, footer, flen);
    full_src[total - 1] = '\0';

    /* Compile fragment shader */
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, full_src,
                               sp->compile_error, SHADER_ERROR_LOG_SIZE);
    free(full_src);
    if (!fs)
    {
        return 0;
    }

    /* Link program */
    GLuint prog = glCreateProgram();
    glAttachShader(prog, g_vertex_shader);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    /* The fragment shader object can be freed after linking */
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(prog, SHADER_ERROR_LOG_SIZE, NULL, sp->compile_error);
        glDeleteProgram(prog);
        return 0;
    }

    /* Swap in the new program, discard the old one */
    if (sp->program)
    {
        glDeleteProgram(sp->program);
    }

    sp->program = prog;
    sp->valid   = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
void shader_set_uniforms(ShaderProgram *sp, const ShaderUniforms *u)
{
    if (!sp->valid)
    {
        return;
    }

    GLuint p = sp->program;
    glUseProgram(p);

    glUniform3fv(glGetUniformLocation(p, "iResolution"), 1, u->iResolution);
    glUniform1f (glGetUniformLocation(p, "iTime"),       u->iTime);
    glUniform1f (glGetUniformLocation(p, "iTimeDelta"),  u->iTimeDelta);
    glUniform1i (glGetUniformLocation(p, "iFrame"),      u->iFrame);
    glUniform4fv(glGetUniformLocation(p, "iMouse"),      1, u->iMouse);
    glUniform4fv(glGetUniformLocation(p, "iDate"),       1, u->iDate);
    glUniform1f (glGetUniformLocation(p, "iSampleRate"), u->iSampleRate);
    glUniform1fv(glGetUniformLocation(p, "iChannelTime"), 4, u->iChannelTime);
    glUniform3fv(glGetUniformLocation(p, "iChannelResolution"), 4, u->iChannelResolution);
    glUniform1i (glGetUniformLocation(p, "iSampleOffset"), u->iSampleOffset);

    /* Bind texture units for iChannel0..3 */
    glUniform1i(glGetUniformLocation(p, "iChannel0"), 0);
    glUniform1i(glGetUniformLocation(p, "iChannel1"), 1);
    glUniform1i(glGetUniformLocation(p, "iChannel2"), 2);
    glUniform1i(glGetUniformLocation(p, "iChannel3"), 3);
}
