#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include "gl_lite.h"
#include "shader.h"
#include "renderer.h"
#include "editor.h"

/* ------------------------------------------------------------------ */
/*  WGL extensions for a modern context                               */
/* ------------------------------------------------------------------ */
typedef HGLRC (APIENTRY *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);
typedef BOOL  (APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int);

#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

/* ------------------------------------------------------------------ */
/*  Globals                                                           */
/* ------------------------------------------------------------------ */
static HWND   g_hwnd    = NULL;   /* main frame window */
static HWND   g_glview  = NULL;   /* child window for GL rendering */
static HDC    g_hdc     = NULL;
static HGLRC  g_hglrc   = NULL;
static bool   g_running   = true;
static int    g_gl_width  = 1024;
static int    g_gl_height = 768;

#define DEFAULT_CANVAS_W  1024
#define DEFAULT_CANVAS_H  768
#define EDITOR_PANEL_W    620

/* Forward declaration — the editor is referenced by the wnd_proc */
static Editor *g_editor = NULL;

/* ------------------------------------------------------------------ */
/*  Reposition GL view and editor panel side-by-side                  */
/* ------------------------------------------------------------------ */
static void layout_children(void)
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int total_w = rc.right;
    int total_h = rc.bottom;

    int editor_w = 0;
    if (g_editor && g_editor->show_editor)
    {
        editor_w = 620;
        if (g_editor->panel)
        {
            editor_layout(g_editor);
        }
    }

    int gl_x = editor_w;
    int gl_w = total_w - editor_w;
    if (gl_w < 1) gl_w = 1;

    g_gl_width  = gl_w;
    g_gl_height = total_h;

    if (g_glview)
    {
        MoveWindow(g_glview, gl_x, 0, gl_w, total_h, TRUE);
    }
}

/* ------------------------------------------------------------------ */
static LRESULT CALLBACK glview_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            layout_children();
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F1 && g_editor)
        {
            editor_toggle(g_editor);
            layout_children();
            return 0;
        }
        if (wParam == VK_F5 && g_editor)
        {
            editor_sync_from_control(g_editor);
            g_editor->needs_compile = true;
            return 0;
        }
        break;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        /* Compute minimum window size so the canvas is at least DEFAULT_CANVAS_W x DEFAULT_CANVAS_H */
        int min_client_w = DEFAULT_CANVAS_W + (g_editor && g_editor->show_editor ? EDITOR_PANEL_W : 0);
        int min_client_h = DEFAULT_CANVAS_H;
        RECT r = { 0, 0, min_client_w, min_client_h };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }

    case WM_CLOSE:
        g_running = false;
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  Create a Win32 window + OpenGL 3.3 Core context                   */
/* ------------------------------------------------------------------ */
static int create_window_and_context(HINSTANCE hInstance)
{
    /* ---- Main frame window ---- */
    WNDCLASSA wc = {};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName  = "ShaderToyX";

    if (!RegisterClassA(&wc))
    {
        return 0;
    }

    RECT rc = { 0, 0, DEFAULT_CANVAS_W + EDITOR_PANEL_W, DEFAULT_CANVAS_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowExA(
        0, wc.lpszClassName, "ShaderToyX",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwnd)
    {
        return 0;
    }

    /* ---- GL view child window ---- */
    WNDCLASSA glwc = {};
    glwc.style         = CS_OWNDC;
    glwc.lpfnWndProc   = glview_proc;
    glwc.hInstance      = hInstance;
    glwc.lpszClassName  = "ShaderToyX_GLView";

    if (!RegisterClassA(&glwc))
    {
        return 0;
    }

    g_glview = CreateWindowExA(
        0, glwc.lpszClassName, NULL,
        WS_CHILD | WS_VISIBLE,
        EDITOR_PANEL_W, 0, DEFAULT_CANVAS_W, DEFAULT_CANVAS_H,
        g_hwnd, NULL, hInstance, NULL
    );

    if (!g_glview)
    {
        return 0;
    }

    g_hdc = GetDC(g_glview);

    /* ---- Pixel format ---- */
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;

    int pf = ChoosePixelFormat(g_hdc, &pfd);
    if (!pf || !SetPixelFormat(g_hdc, pf, &pfd))
    {
        return 0;
    }

    /* ---- Legacy context (needed to bootstrap WGL extensions) ---- */
    HGLRC legacy = wglCreateContext(g_hdc);
    if (!legacy || !wglMakeCurrent(g_hdc, legacy))
    {
        return 0;
    }

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (!wglCreateContextAttribsARB)
    {
        return 0;
    }

    /* ---- Modern 3.3 Core context ---- */
    int attribs[] =
    {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0
    };

    g_hglrc = wglCreateContextAttribsARB(g_hdc, NULL, attribs);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);

    if (!g_hglrc || !wglMakeCurrent(g_hdc, g_hglrc))
    {
        return 0;
    }

    /* ---- V-Sync ---- */
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT)
    {
        wglSwapIntervalEXT(1);
    }

    return 1;
}

/* ------------------------------------------------------------------ */
static double get_time(void)
{
    static LARGE_INTEGER freq  = {};
    static LARGE_INTEGER start = {};
    if (freq.QuadPart == 0)
    {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

/* ------------------------------------------------------------------ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    if (!create_window_and_context(hInstance))
    {
        return 1;
    }

    if (!gl_lite_init())
    {
        return 1;
    }

    /* ---- Application state ---- */
    Renderer renderer;
    renderer_init(&renderer);

    Editor editor;
    editor_init(&editor, g_hwnd, hInstance);
    g_editor = &editor;
    layout_children();

    /* One shader program per tab: Image + 4 buffers */
    ShaderProgram programs[NUM_TABS];
    for (int i = 0; i < NUM_TABS; i++)
    {
        shader_init(&programs[i]);
    }

    /* Compile visible tabs on startup */
    for (int i = 0; i < NUM_TABS; i++)
    {
        if (editor_is_tab_visible(&editor, i) && editor.code[i][0])
        {
            if (!shader_compile(&programs[i], editor.code[i]))
            {
                strncpy(editor.error_log[i], programs[i].compile_error,
                        EDITOR_ERROR_LOG_SIZE - 1);
            }
        }
    }

    /* Create initial FBOs */
    renderer_resize_buffers(&renderer, g_gl_width, g_gl_height);

    double start_time  = get_time();
    double last_time   = start_time;
    float  shader_time = 0.0f;
    int    frame_count = 0;
    double fps_accum   = 0.0;
    int    fps_frames  = 0;
    float  display_fps = 0.0f;

    /* Mouse tracking for iMouse uniform */
    float mouse_x = 0.0f, mouse_y = 0.0f;
    float click_x = 0.0f, click_y = 0.0f;
    bool  was_pressed = false;

    /* ---- Main loop ---- */
    while (g_running)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (msg.message == WM_QUIT)
            {
                g_running = false;
            }
        }

        if (!g_running)
        {
            break;
        }

        /* Timing */
        double now     = get_time();
        float  delta   = (float)(now - last_time);
        last_time = now;

        /* Handle reset */
        if (editor.reset_time)
        {
            shader_time = 0.0f;
            frame_count = 0;
            editor.reset_time = false;
        }

        /* Advance shader time only when not paused */
        if (!editor.paused)
        {
            shader_time += delta;
        }

        float elapsed = shader_time;

        /* Simple FPS counter (update every 0.5s) */
        fps_accum += delta;
        fps_frames++;
        if (fps_accum >= 0.5)
        {
            display_fps = (float)fps_frames / (float)fps_accum;
            fps_accum  = 0.0;
            fps_frames = 0;
        }

        /* Mouse input */
        bool lmb_down = false;
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(g_glview, &pt);

            lmb_down = (GetKeyState(VK_LBUTTON) & 0x8000) != 0;

            if (lmb_down)
            {
                /* Convert to GL coords (origin bottom-left) */
                mouse_x = (float)pt.x;
                mouse_y = (float)(g_gl_height - 1 - pt.y);

                if (!was_pressed)
                {
                    click_x = mouse_x;
                    click_y = mouse_y;
                }
            }
        }
        was_pressed = lmb_down;

        /* Recompile on request — only visible tabs */
        if (editor.needs_compile)
        {
            for (int i = 0; i < NUM_TABS; i++)
            {
                editor.error_log[i][0] = '\0';
                if (editor_is_tab_visible(&editor, i) && editor.code[i][0])
                {
                    if (!shader_compile(&programs[i], editor.code[i]))
                    {
                        strncpy(editor.error_log[i], programs[i].compile_error,
                                EDITOR_ERROR_LOG_SIZE - 1);
                        editor.error_log[i][EDITOR_ERROR_LOG_SIZE - 1] = '\0';
                    }
                }
                else if (!editor_is_tab_visible(&editor, i))
                {
                    /* Destroy program for hidden tabs */
                    shader_destroy(&programs[i]);
                }
            }
            editor.needs_compile = false;
        }

        /* Update editor labels */
        editor_update(&editor, elapsed, display_fps, g_gl_width, g_gl_height);

        /* Fill uniforms */
        ShaderUniforms uniforms;
        memset(&uniforms, 0, sizeof(uniforms));

        uniforms.iResolution[0] = (float)g_gl_width;
        uniforms.iResolution[1] = (float)g_gl_height;
        uniforms.iResolution[2] = 1.0f;
        uniforms.iTime          = elapsed;
        uniforms.iTimeDelta     = delta;
        uniforms.iFrame         = frame_count;
        uniforms.iSampleRate    = 44100.0f;

        uniforms.iMouse[0] = mouse_x;
        uniforms.iMouse[1] = mouse_y;
        uniforms.iMouse[2] = lmb_down ?  click_x : -click_x;
        uniforms.iMouse[3] = lmb_down ?  click_y : -click_y;

        time_t tnow = time(NULL);
        struct tm *lt = localtime(&tnow);
        if (lt)
        {
            uniforms.iDate[0] = (float)(lt->tm_year + 1900);
            uniforms.iDate[1] = (float)(lt->tm_mon);
            uniforms.iDate[2] = (float)(lt->tm_mday);
            uniforms.iDate[3] = (float)(lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
        }

        /* Resize FBOs if viewport changed */
        renderer_resize_buffers(&renderer, g_gl_width, g_gl_height);

        /* Render buffer passes A–D, then the Image pass */
        glViewport(0, 0, g_gl_width, g_gl_height);

        /* Buffer passes: each reads from all 4 buffer textures (previous frame) */
        for (int b = 0; b < NUM_BUFFERS; b++)
        {
            if (!editor_is_tab_visible(&editor, TAB_BUF_A + b))
            {
                continue;
            }
            if (!programs[TAB_BUF_A + b].valid)
            {
                continue;
            }

            GLuint channels[4];
            for (int c = 0; c < 4; c++)
            {
                channels[c] = renderer_get_buffer_texture(&renderer, c);
            }

            GLuint fbo = renderer_get_buffer_fbo(&renderer, b);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer_draw_pass(&renderer, fbo, &programs[TAB_BUF_A + b],
                               &uniforms, channels);
        }

        /* Swap all buffer ping-pong targets */
        for (int b = 0; b < NUM_BUFFERS; b++)
        {
            renderer_swap_buffer(&renderer, b);
        }

        /* Image pass: renders to screen, reads from all 4 buffer outputs */
        {
            GLuint channels[4];
            for (int c = 0; c < 4; c++)
            {
                channels[c] = renderer_get_buffer_texture(&renderer, c);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            renderer_draw_pass(&renderer, 0, &programs[TAB_IMAGE],
                               &uniforms, channels);
        }

        SwapBuffers(g_hdc);
        frame_count++;
    }

    /* ---- Cleanup ---- */
    g_editor = NULL;
    for (int i = 0; i < NUM_TABS; i++)
    {
        shader_destroy(&programs[i]);
    }
    renderer_destroy(&renderer);
    editor_destroy(&editor);

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(g_hglrc);
    ReleaseDC(g_glview, g_hdc);
    DestroyWindow(g_hwnd);

    return 0;
}
