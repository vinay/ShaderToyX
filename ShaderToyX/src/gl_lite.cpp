#include "gl_lite.h"
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Define function pointers (storage)                                */
/* ------------------------------------------------------------------ */
#define GL_FUNC(ret, name, ...) PFN_##name name = NULL;

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

#undef GL_FUNC

/* ------------------------------------------------------------------ */
static void *get_proc(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if (!p || p == (void *)0x1 || p == (void *)0x2 || p == (void *)0x3 || p == (void *)-1)
    {
        HMODULE module = LoadLibraryA("opengl32.dll");
        if (module)
        {
            p = (void *)GetProcAddress(module, name);
        }
    }
    return p;
}

/* ------------------------------------------------------------------ */
int gl_lite_init(void)
{
    int ok = 1;

#define LOAD(name) \
    name = (PFN_##name)get_proc(#name); \
    if (!name) { fprintf(stderr, "Failed to load GL function: %s\n", #name); ok = 0; }

    LOAD(glCreateShader)
    LOAD(glDeleteShader)
    LOAD(glShaderSource)
    LOAD(glCompileShader)
    LOAD(glGetShaderiv)
    LOAD(glGetShaderInfoLog)

    LOAD(glCreateProgram)
    LOAD(glDeleteProgram)
    LOAD(glAttachShader)
    LOAD(glLinkProgram)
    LOAD(glGetProgramiv)
    LOAD(glGetProgramInfoLog)
    LOAD(glUseProgram)

    LOAD(glGetUniformLocation)
    LOAD(glUniform1f)
    LOAD(glUniform1i)
    LOAD(glUniform1fv)
    LOAD(glUniform3fv)
    LOAD(glUniform4fv)

    LOAD(glGenVertexArrays)
    LOAD(glBindVertexArray)
    LOAD(glDeleteVertexArrays)

    LOAD(glGenBuffers)
    LOAD(glDeleteBuffers)
    LOAD(glBindBuffer)
    LOAD(glBufferData)

    LOAD(glVertexAttribPointer)
    LOAD(glEnableVertexAttribArray)

    LOAD(glActiveTexture)

    LOAD(glGenFramebuffers)
    LOAD(glDeleteFramebuffers)
    LOAD(glBindFramebuffer)
    LOAD(glFramebufferTexture2D)
    LOAD(glCheckFramebufferStatus)

#undef LOAD

    return ok;
}
