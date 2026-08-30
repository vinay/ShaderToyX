/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#include "renderer.h"
#include <string.h>

/* Full-screen quad (triangle strip) */
static float quad_vertices[] =
{
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
};

/* ------------------------------------------------------------------ */
static void create_buffer_pass(BufferPass *bp, int width, int height)
{
    bp->width   = width;
    bp->height  = height;
    bp->current = 0;

    for (int i = 0; i < 2; i++)
    {
        /* Create texture */
        glGenTextures(1, &bp->tex[i]);
        glBindTexture(GL_TEXTURE_2D, bp->tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0,
                     GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        /* Create FBO */
        glGenFramebuffers(1, &bp->fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bp->fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, bp->tex[i], 0);

        /* glTexImage2D with NULL data leaves the contents undefined.
           Buffers that are never rendered to (hidden tabs) are still
           sampled via iChannelN, so clear them to a known value. */
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ------------------------------------------------------------------ */
static void destroy_buffer_pass(BufferPass *bp)
{
    for (int i = 0; i < 2; i++)
    {
        if (bp->fbo[i]) glDeleteFramebuffers(1, &bp->fbo[i]);
        if (bp->tex[i]) glDeleteTextures(1, &bp->tex[i]);
    }
    memset(bp, 0, sizeof(BufferPass));
}

/* ------------------------------------------------------------------ */
void renderer_init(Renderer *r)
{
    memset(r, 0, sizeof(Renderer));

    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);

    glBindVertexArray(r->vao);

    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

/* ------------------------------------------------------------------ */
void renderer_destroy(Renderer *r)
{
    glDeleteVertexArrays(1, &r->vao);
    glDeleteBuffers(1, &r->vbo);

    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        destroy_buffer_pass(&r->buffers[i]);
    }
}

/* ------------------------------------------------------------------ */
void renderer_resize_buffers(Renderer *r, int width, int height)
{
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    for (int i = 0; i < NUM_BUFFERS; i++)
    {
        BufferPass *bp = &r->buffers[i];

        /* Skip if already the right size */
        if (bp->fbo[0] && bp->width == width && bp->height == height)
        {
            continue;
        }

        destroy_buffer_pass(bp);
        create_buffer_pass(bp, width, height);
    }
}

/* ------------------------------------------------------------------ */
void renderer_draw_pass(Renderer *r, GLuint fbo, ShaderProgram *sp,
                        const ShaderUniforms *uniforms,
                        GLuint channel_textures[4])
{
    if (!sp->valid)
    {
        return;
    }

    shader_set_uniforms(sp, uniforms);

    /* Bind channel textures */
    for (int i = 0; i < 4; i++)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, channel_textures[i]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindVertexArray(r->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ------------------------------------------------------------------ */
void renderer_swap_buffer(Renderer *r, int buf_index)
{
    if (buf_index >= 0 && buf_index < NUM_BUFFERS)
    {
        r->buffers[buf_index].current ^= 1;
    }
}

/* ------------------------------------------------------------------ */
GLuint renderer_get_buffer_texture(Renderer *r, int buf_index)
{
    if (buf_index >= 0 && buf_index < NUM_BUFFERS)
    {
        /* `current` is the side that was written most recently
           (renderer_swap_buffer flips it after each frame's passes) */
        return r->buffers[buf_index].tex[r->buffers[buf_index].current];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
GLuint renderer_get_buffer_fbo(Renderer *r, int buf_index)
{
    if (buf_index >= 0 && buf_index < NUM_BUFFERS)
    {
        /* Write to the side that is NOT currently readable */
        int write_side = r->buffers[buf_index].current ^ 1;
        return r->buffers[buf_index].fbo[write_side];
    }
    return 0;
}
