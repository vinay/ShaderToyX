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
int shader_compile(ShaderProgram *sp, const char *user_source)
{
    sp->compile_error[0] = '\0';

    /* Build full fragment source: header + user code + footer */
    size_t hlen = strlen(fragment_header);
    size_t ulen = strlen(user_source);
    size_t flen = strlen(fragment_footer);
    size_t total = hlen + ulen + flen + 1;

    char *full_src = (char *)malloc(total);
    if (!full_src)
    {
        snprintf(sp->compile_error, SHADER_ERROR_LOG_SIZE, "Out of memory");
        return 0;
    }

    memcpy(full_src, fragment_header, hlen);
    memcpy(full_src + hlen, user_source, ulen);
    memcpy(full_src + hlen + ulen, fragment_footer, flen);
    full_src[total - 1] = '\0';

    /* Compile vertex shader */
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_source,
                               sp->compile_error, SHADER_ERROR_LOG_SIZE);
    if (!vs)
    {
        free(full_src);
        return 0;
    }

    /* Compile fragment shader */
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, full_src,
                               sp->compile_error, SHADER_ERROR_LOG_SIZE);
    if (!fs)
    {
        glDeleteShader(vs);
        free(full_src);
        return 0;
    }

    /* Link program */
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    /* Shaders can be freed after linking */
    glDeleteShader(vs);
    glDeleteShader(fs);
    free(full_src);

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

    /* Bind texture units for iChannel0..3 (no textures loaded yet) */
    glUniform1i(glGetUniformLocation(p, "iChannel0"), 0);
    glUniform1i(glGetUniformLocation(p, "iChannel1"), 1);
    glUniform1i(glGetUniformLocation(p, "iChannel2"), 2);
    glUniform1i(glGetUniformLocation(p, "iChannel3"), 3);
}
