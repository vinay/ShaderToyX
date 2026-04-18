#ifndef RENDERER_H
#define RENDERER_H

#include "gl_lite.h"
#include "shader.h"

#define NUM_BUFFERS 4  /* Buffer A, B, C, D */

/* A single buffer pass with ping-pong FBOs */
typedef struct BufferPass
{
    GLuint fbo[2];       /* ping-pong framebuffers */
    GLuint tex[2];       /* color attachments */
    int    current;      /* which FBO to write to (0 or 1) */
    int    width;
    int    height;
} BufferPass;

typedef struct Renderer
{
    GLuint     vao;
    GLuint     vbo;
    BufferPass buffers[NUM_BUFFERS];
} Renderer;

void renderer_init(Renderer *r);
void renderer_destroy(Renderer *r);

/* Resize all buffer FBOs to match the viewport */
void renderer_resize_buffers(Renderer *r, int width, int height);

/* Draw a single pass (buffer or image) with the given shader + uniforms.
   channel_textures[0..3] are bound to iChannel0..3.
   If fbo == 0, renders to the default framebuffer (screen). */
void renderer_draw_pass(Renderer *r, GLuint fbo, ShaderProgram *sp,
                        const ShaderUniforms *uniforms,
                        GLuint channel_textures[4]);

/* Swap the ping-pong buffer for the given index */
void renderer_swap_buffer(Renderer *r, int buf_index);

/* Get the readable (previous frame) texture for a buffer */
GLuint renderer_get_buffer_texture(Renderer *r, int buf_index);

/* Get the writable FBO for a buffer */
GLuint renderer_get_buffer_fbo(Renderer *r, int buf_index);

#endif
