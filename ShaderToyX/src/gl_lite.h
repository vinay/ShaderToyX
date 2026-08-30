/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#ifndef GL_LITE_H
#define GL_LITE_H

/*
 * gl_lite.h  -  Minimal OpenGL 3.3 Core loader for Win32/WGL.
 *
 * Only the functions actually used by ShaderToyX are declared here.
 * GL 1.1 functions come from opengl32.lib; everything newer is loaded
 * at runtime via wglGetProcAddress.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

/* ------------------------------------------------------------------ */
/*  Types not in the ancient <GL/gl.h>                                */
/* ------------------------------------------------------------------ */
typedef char          GLchar;
typedef ptrdiff_t     GLsizeiptr;
typedef ptrdiff_t     GLintptr;

typedef void (APIENTRY *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id,
                                     GLenum severity, GLsizei length,
                                     const GLchar *message, const void *userParam);

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_ARRAY_BUFFER                   0x8892
#define GL_STATIC_DRAW                    0x88E4
#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE1                       0x84C1
#define GL_TEXTURE2                       0x84C2
#define GL_TEXTURE3                       0x84C3
#define GL_FRAMEBUFFER                    0x8D40
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_RGBA16F                        0x881A
#define GL_CLAMP_TO_EDGE                  0x812F

/* KHR_debug / GL 4.3 (optional, used by Debug builds only) */
#define GL_DEBUG_OUTPUT                   0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS       0x8242
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B

/* ------------------------------------------------------------------ */
/*  Function pointer types and extern declarations                    */
/* ------------------------------------------------------------------ */
#define GL_FUNC(ret, name, ...) \
    typedef ret (APIENTRY *PFN_##name)(__VA_ARGS__); \
    extern PFN_##name name;

GL_FUNC(GLuint, glCreateShader,           GLenum type)
GL_FUNC(void,   glDeleteShader,           GLuint shader)
GL_FUNC(void,   glShaderSource,           GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)
GL_FUNC(void,   glCompileShader,          GLuint shader)
GL_FUNC(void,   glGetShaderiv,            GLuint shader, GLenum pname, GLint *params)
GL_FUNC(void,   glGetShaderInfoLog,       GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)

GL_FUNC(GLuint, glCreateProgram,          void)
GL_FUNC(void,   glDeleteProgram,          GLuint program)
GL_FUNC(void,   glAttachShader,           GLuint program, GLuint shader)
GL_FUNC(void,   glLinkProgram,            GLuint program)
GL_FUNC(void,   glGetProgramiv,           GLuint program, GLenum pname, GLint *params)
GL_FUNC(void,   glGetProgramInfoLog,      GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
GL_FUNC(void,   glUseProgram,             GLuint program)

GL_FUNC(GLint,  glGetUniformLocation,     GLuint program, const GLchar *name)
GL_FUNC(void,   glUniform1f,              GLint location, GLfloat v0)
GL_FUNC(void,   glUniform1i,              GLint location, GLint v0)
GL_FUNC(void,   glUniform1fv,             GLint location, GLsizei count, const GLfloat *value)
GL_FUNC(void,   glUniform3fv,             GLint location, GLsizei count, const GLfloat *value)
GL_FUNC(void,   glUniform4fv,             GLint location, GLsizei count, const GLfloat *value)

GL_FUNC(void,   glGenVertexArrays,        GLsizei n, GLuint *arrays)
GL_FUNC(void,   glBindVertexArray,        GLuint array)
GL_FUNC(void,   glDeleteVertexArrays,     GLsizei n, const GLuint *arrays)

GL_FUNC(void,   glGenBuffers,             GLsizei n, GLuint *buffers)
GL_FUNC(void,   glDeleteBuffers,          GLsizei n, const GLuint *buffers)
GL_FUNC(void,   glBindBuffer,             GLenum target, GLuint buffer)
GL_FUNC(void,   glBufferData,             GLenum target, GLsizeiptr size, const void *data, GLenum usage)

GL_FUNC(void,   glVertexAttribPointer,    GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)
GL_FUNC(void,   glEnableVertexAttribArray,GLuint index)

GL_FUNC(void,   glActiveTexture,          GLenum texture)

GL_FUNC(void,   glGenFramebuffers,        GLsizei n, GLuint *framebuffers)
GL_FUNC(void,   glDeleteFramebuffers,     GLsizei n, const GLuint *framebuffers)
GL_FUNC(void,   glBindFramebuffer,        GLenum target, GLuint framebuffer)
GL_FUNC(void,   glFramebufferTexture2D,   GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
GL_FUNC(GLenum, glCheckFramebufferStatus, GLenum target)

/* Optional: stays NULL if the driver does not expose KHR_debug */
GL_FUNC(void,   glDebugMessageCallback,   GLDEBUGPROC callback, const void *userParam)

#undef GL_FUNC

/* ------------------------------------------------------------------ */
/*  Loader: call once after creating an OpenGL context.               */
/*  Returns 1 on success. On failure, the name of the first missing   */
/*  required function is written to *missing (may be NULL).           */
/* ------------------------------------------------------------------ */
int gl_lite_init(const char **missing);

#endif
