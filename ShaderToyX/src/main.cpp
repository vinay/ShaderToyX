/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
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
#include "audio.h"
#include "recorder.h"

/* ------------------------------------------------------------------ */
/*  WGL extensions for a modern context                               */
/* ------------------------------------------------------------------ */
typedef HGLRC (APIENTRY *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int *);
typedef BOOL  (APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int);

#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_FLAGS_ARB             0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB         0x00000001
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001

/* ------------------------------------------------------------------ */
/*  Globals                                                           */
/* ------------------------------------------------------------------ */
static HWND   g_hwnd      = NULL;   /* main frame window */
static HWND   g_glview    = NULL;   /* child window for GL rendering */
static HDC    g_hdc       = NULL;
static HGLRC  g_hglrc     = NULL;
static bool   g_running   = true;
static int    g_gl_width  = 1024;
static int    g_gl_height = 768;
static int    g_dpi       = 96;

/* Borderless-fullscreen state: what to restore when leaving fullscreen */
static bool            g_fullscreen           = false;
static DWORD           g_saved_style          = 0;
static WINDOWPLACEMENT g_saved_placement      = { sizeof(WINDOWPLACEMENT) };
static bool            g_saved_editor_visible = false;

/* Default canvas size at 96 DPI */
#define DEFAULT_CANVAS_W  1024
#define DEFAULT_CANVAS_H  768

/* The editor is referenced by the wnd_proc */
static Editor *g_editor = NULL;

/* Mouse state for the iMouse uniform, in GL pixel coords (origin bottom-left).
   Updated from the GL view's window messages so clicks in the editor panel
   never leak into the shader. */
static float g_mouse_x = 0.0f, g_mouse_y = 0.0f;   /* current position (while dragging) */
static float g_click_x = 0.0f, g_click_y = 0.0f;   /* position of the last button-down */
static bool  g_mouse_down    = false;
static bool  g_mouse_clicked = false;              /* true for exactly one frame after button-down */

/* Scale a 96-DPI pixel value to the window's current DPI */
static int sc(int v)
{
    return MulDiv(v, g_dpi, 96);
}

/* ------------------------------------------------------------------ */
static void fatal(const char *msg)
{
    MessageBoxA(g_hwnd, msg, "ShaderToyX", MB_OK | MB_ICONERROR);
}

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
    if (g_editor)
    {
        editor_w = editor_panel_width(g_editor);
        if (editor_w > 0)
        {
            editor_layout(g_editor);
        }
    }

    int gl_x = editor_w;
    int gl_w = total_w - editor_w;
    if (gl_w < 1) gl_w = 1;
    if (total_h < 1) total_h = 1;

    g_gl_width  = gl_w;
    g_gl_height = total_h;

    if (g_glview)
    {
        MoveWindow(g_glview, gl_x, 0, gl_w, total_h, TRUE);
    }
}

/* ------------------------------------------------------------------ */
/*  Borderless fullscreen toggle                                      */
/* ------------------------------------------------------------------ */
static void toggle_fullscreen(void)
{
    if (!g_fullscreen)
    {
        MONITORINFO mi = { sizeof(mi) };
        if (!GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            return;
        }

        g_saved_style = (DWORD)GetWindowLongPtrA(g_hwnd, GWL_STYLE);
        g_saved_placement.length = sizeof(g_saved_placement);
        GetWindowPlacement(g_hwnd, &g_saved_placement);

        g_fullscreen = true;

        /* Hide the editor panel for an edge-to-edge canvas; F1 still works */
        g_saved_editor_visible = g_editor && g_editor->show_editor;
        if (g_saved_editor_visible)
        {
            editor_toggle(g_editor);
        }

        SetWindowLongPtrA(g_hwnd, GWL_STYLE,
                          (LONG_PTR)((g_saved_style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP));
        SetWindowPos(g_hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
    else
    {
        g_fullscreen = false;

        SetWindowLongPtrA(g_hwnd, GWL_STYLE, (LONG_PTR)g_saved_style);
        SetWindowPlacement(g_hwnd, &g_saved_placement);
        SetWindowPos(g_hwnd, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);

        if (g_saved_editor_visible && g_editor && !g_editor->show_editor)
        {
            editor_toggle(g_editor);
        }
    }

    if (g_editor)
    {
        editor_set_fullscreen(g_editor, g_fullscreen);
    }
    layout_children();
}

/* ------------------------------------------------------------------ */
/*  GL view window procedure: mouse tracking for iMouse               */
/* ------------------------------------------------------------------ */
static void update_mouse_pos(LPARAM lParam)
{
    int x = (int)(short)LOWORD(lParam);
    int y = (int)(short)HIWORD(lParam);
    g_mouse_x = (float)x;
    g_mouse_y = (float)(g_gl_height - 1 - y);
}

static LRESULT CALLBACK glview_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        update_mouse_pos(lParam);
        g_click_x = g_mouse_x;
        g_click_y = g_mouse_y;
        g_mouse_down    = true;
        g_mouse_clicked = true;
        return 0;

    case WM_MOUSEMOVE:
        if (g_mouse_down)
        {
            update_mouse_pos(lParam);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_mouse_down)
        {
            update_mouse_pos(lParam);
            g_mouse_down = false;
            ReleaseCapture();
        }
        return 0;

    case WM_CAPTURECHANGED:
        g_mouse_down = false;
        return 0;
    }

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

    case WM_GETMINMAXINFO:
    {
        /* No minimum while fullscreen: the borderless window must be free
           to match the monitor exactly, even one smaller than the minimum. */
        if (g_fullscreen)
        {
            break;
        }
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        /* Minimum window size so the canvas is at least DEFAULT_CANVAS_W x DEFAULT_CANVAS_H */
        int min_client_w = sc(DEFAULT_CANVAS_W) + (g_editor ? editor_panel_width(g_editor) : 0);
        int min_client_h = sc(DEFAULT_CANVAS_H);
        RECT r = { 0, 0, min_client_w, min_client_h };
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)g_dpi);
        mmi->ptMinTrackSize.x = r.right - r.left;
        mmi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }

    case WM_DPICHANGED:
    {
        g_dpi = HIWORD(wParam);
        if (g_editor)
        {
            editor_set_dpi(g_editor, g_dpi);
        }
        /* Windows suggests a new rect that keeps the window the same physical size */
        const RECT *suggested = (const RECT *)lParam;
        SetWindowPos(hwnd, NULL,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
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
/*  GL debug output (Debug builds, when the driver supports KHR_debug) */
/* ------------------------------------------------------------------ */
#ifdef _DEBUG
static void APIENTRY gl_debug_callback(GLenum source, GLenum type, GLuint id,
                                       GLenum severity, GLsizei length,
                                       const GLchar *message, const void *userParam)
{
    (void)source; (void)type; (void)id; (void)length; (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
    {
        return;
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "GL debug: %s\n", message);
    OutputDebugStringA(buf);
}
#endif

/* ------------------------------------------------------------------ */
/*  Create a Win32 window + OpenGL 3.3 Core context.                  */
/*  Returns NULL on success, otherwise an error message.              */
/* ------------------------------------------------------------------ */
static const char *create_window_and_context(HINSTANCE hInstance)
{
    /* Per-monitor DPI awareness. The manifest (see the vcxproj) already
       declares this; the call is a fallback for builds without it, and
       harmlessly fails if the manifest has already set it. */
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_dpi = (int)GetDpiForSystem();

    /* ---- Main frame window ---- */
    WNDCLASSA wc = {};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "ShaderToyX";

    if (!RegisterClassA(&wc))
    {
        return "RegisterClass failed for the main window.";
    }

    RECT rc = { 0, 0, sc(DEFAULT_CANVAS_W + EDITOR_PANEL_W), sc(DEFAULT_CANVAS_H) };
    AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)g_dpi);

    g_hwnd = CreateWindowExA(
        0, wc.lpszClassName, "ShaderToyX",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hwnd)
    {
        return "CreateWindow failed for the main window.";
    }

    /* The window may have landed on a monitor with a different DPI than
       the system DPI; re-read it and resize to match. */
    int window_dpi = (int)GetDpiForWindow(g_hwnd);
    if (window_dpi > 0 && window_dpi != g_dpi)
    {
        g_dpi = window_dpi;
        RECT r2 = { 0, 0, sc(DEFAULT_CANVAS_W + EDITOR_PANEL_W), sc(DEFAULT_CANVAS_H) };
        AdjustWindowRectExForDpi(&r2, WS_OVERLAPPEDWINDOW, FALSE, 0, (UINT)g_dpi);
        SetWindowPos(g_hwnd, NULL, 0, 0, r2.right - r2.left, r2.bottom - r2.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* ---- GL view child window ---- */
    WNDCLASSA glwc = {};
    glwc.style         = CS_OWNDC;
    glwc.lpfnWndProc   = glview_proc;
    glwc.hInstance     = hInstance;
    glwc.lpszClassName = "ShaderToyX_GLView";

    if (!RegisterClassA(&glwc))
    {
        return "RegisterClass failed for the GL view.";
    }

    g_glview = CreateWindowExA(
        0, glwc.lpszClassName, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 1, 1,   /* positioned by layout_children() */
        g_hwnd, NULL, hInstance, NULL
    );

    if (!g_glview)
    {
        return "CreateWindow failed for the GL view.";
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
        return "No suitable OpenGL pixel format found.";
    }

    /* ---- Legacy context (needed to bootstrap WGL extensions) ---- */
    HGLRC legacy = wglCreateContext(g_hdc);
    if (!legacy || !wglMakeCurrent(g_hdc, legacy))
    {
        return "Could not create a legacy OpenGL context.";
    }

    PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");

    if (!wglCreateContextAttribsARB)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(legacy);
        return "wglCreateContextAttribsARB is not available.\n"
               "Your graphics driver does not support modern OpenGL.";
    }

    /* ---- Modern 3.3 Core context ---- */
    int attribs[] =
    {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#ifdef _DEBUG
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_DEBUG_BIT_ARB,
#endif
        0
    };

    g_hglrc = wglCreateContextAttribsARB(g_hdc, NULL, attribs);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);

    if (!g_hglrc || !wglMakeCurrent(g_hdc, g_hglrc))
    {
        return "Could not create an OpenGL 3.3 Core context.\n"
               "ShaderToyX requires a GPU and driver with OpenGL 3.3 support.";
    }

    /* ---- V-Sync ---- */
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT =
        (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (wglSwapIntervalEXT)
    {
        wglSwapIntervalEXT(1);
    }

    return NULL;
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
/*  Compile every visible tab; destroy programs for hidden tabs        */
/* ------------------------------------------------------------------ */
static void compile_all(Editor *editor, ShaderProgram *programs)
{
    for (int i = 0; i < NUM_TABS; i++)
    {
        editor->error_log[i][0] = '\0';
        if (editor_is_tab_visible(editor, i) && editor->code[i][0])
        {
            if (!shader_compile(&programs[i], editor->code[i], i == TAB_SOUND))
            {
                editor_set_error(editor, i, programs[i].compile_error);
            }
        }
        else if (!editor_is_tab_visible(editor, i))
        {
            shader_destroy(&programs[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    const char *err = create_window_and_context(hInstance);
    if (err)
    {
        fatal(err);
        return 1;
    }

    const char *missing = NULL;
    if (!gl_lite_init(&missing))
    {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Failed to load a required OpenGL function: %s\n"
                 "ShaderToyX requires OpenGL 3.3 support.",
                 missing ? missing : "(unknown)");
        fatal(buf);
        return 1;
    }

#ifdef _DEBUG
    if (glDebugMessageCallback)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(gl_debug_callback, NULL);
    }
#endif

    /* ---- Application state ---- */
    Renderer renderer;
    renderer_init(&renderer);

    /* The editor holds ~340 KB of text buffers; keep it off the stack */
    static Editor editor;
    editor_init(&editor, g_hwnd, hInstance, g_dpi);
    g_editor = &editor;
    layout_children();

    /* Sound output. Failure is not fatal: sound shaders still compile,
       there is just nothing to hear. */
    bool audio_ok = audio_init();

    /* One shader program per tab: Image + 4 buffers */
    ShaderProgram programs[NUM_TABS];
    for (int i = 0; i < NUM_TABS; i++)
    {
        shader_init(&programs[i]);
    }

    /* Compile visible tabs on startup */
    compile_all(&editor, programs);

    /* Create initial FBOs */
    renderer_resize_buffers(&renderer, g_gl_width, g_gl_height);

    double last_time   = get_time();
    float  shader_time = 0.0f;
    int    frame_count = 0;
    double fps_accum   = 0.0;
    int    fps_frames  = 0;
    float  display_fps = 0.0f;

    /* Recording state: capture size is locked for the file's lifetime
       (H.264 cannot change resolution mid-stream) */
    double         rec_clock  = 0.0;  /* unpaused seconds since record start */
    int            rec_w      = 0;
    int            rec_h      = 0;
    unsigned char *rec_pixels = NULL;

    /* ---- Main loop ---- */
    while (g_running)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            /* Global hotkeys. These are intercepted before dispatch because
               the focused child (usually the code EDIT control) would
               otherwise swallow the key and never forward it to the frame. */
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5)
            {
                editor_sync_from_control(&editor);
                editor.needs_compile = true;
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1)
            {
                editor_toggle(&editor);
                layout_children();
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_F11)
            {
                toggle_fullscreen();
                continue;
            }
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && g_fullscreen)
            {
                toggle_fullscreen();
                continue;
            }

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
        double now   = get_time();
        float  delta = (float)(now - last_time);
        last_time = now;

        /* Fullscreen button was clicked */
        if (editor.toggle_fullscreen)
        {
            editor.toggle_fullscreen = false;
            toggle_fullscreen();
        }

        /* Handle reset */
        if (editor.reset_time)
        {
            shader_time = 0.0f;
            frame_count = 0;
            editor.reset_time = false;
            audio_reset();
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

        /* Recompile on request — only visible tabs */
        if (editor.needs_compile)
        {
            compile_all(&editor, programs);
            editor.needs_compile = false;

            /* Drop queued audio from the old sound shader; the new one
               takes over from the position that was last audible. */
            audio_flush();
        }

        /* Record button was clicked */
        if (editor.toggle_record)
        {
            editor.toggle_record = false;
            if (recorder_is_active())
            {
                recorder_stop();
            }
            else
            {
                rec_w = g_gl_width  & ~1;  /* H.264 needs even dimensions */
                rec_h = g_gl_height & ~1;
                bool with_audio = audio_ok &&
                                  editor_is_tab_visible(&editor, TAB_SOUND) &&
                                  programs[TAB_SOUND].valid;
                free(rec_pixels);
                rec_pixels = (unsigned char *)malloc((size_t)rec_w * rec_h * 4);
                if (rec_pixels && recorder_start(rec_w, rec_h, with_audio))
                {
                    /* Discard the queued audio lead so the captured audio
                       starts at what is audible right now (the generator
                       rewinds and reproduces the same samples). */
                    audio_flush();
                    rec_clock = 0.0;
                }
                else
                {
                    free(rec_pixels);
                    rec_pixels = NULL;
                }
            }
        }

        /* Stop recording if the canvas was resized (incl. fullscreen) */
        if (recorder_is_active() &&
            ((g_gl_width & ~1) != rec_w || (g_gl_height & ~1) != rec_h))
        {
            recorder_stop();
        }

        /* Keep the button icon in sync (recording can also stop itself
           on write errors) */
        editor_set_recording(&editor, recorder_is_active());

        /* Update editor labels */
        editor_update(&editor, elapsed, display_fps, g_gl_width, g_gl_height,
                      recorder_is_active() ? (float)rec_clock : -1.0f);

        /* Fill uniforms */
        ShaderUniforms uniforms;
        memset(&uniforms, 0, sizeof(uniforms));

        uniforms.iResolution[0] = (float)g_gl_width;
        uniforms.iResolution[1] = (float)g_gl_height;
        uniforms.iResolution[2] = 1.0f;
        uniforms.iTime          = elapsed;
        uniforms.iTimeDelta     = delta;
        uniforms.iFrame         = frame_count;
        uniforms.iSampleRate    = (float)AUDIO_SAMPLE_RATE;

        /* iMouse, matching Shadertoy:
             xy = current position while dragging (last position afterwards)
             z  = click x, positive while the button is held, negative after release
             w  = click y, positive only on the frame the button went down */
        uniforms.iMouse[0] = g_mouse_x;
        uniforms.iMouse[1] = g_mouse_y;
        uniforms.iMouse[2] = g_mouse_down    ? g_click_x : -g_click_x;
        uniforms.iMouse[3] = g_mouse_clicked ? g_click_y : -g_click_y;
        g_mouse_clicked = false;

        /* All four buffers share the canvas resolution */
        for (int c = 0; c < 4; c++)
        {
            uniforms.iChannelResolution[c * 3 + 0] = (float)g_gl_width;
            uniforms.iChannelResolution[c * 3 + 1] = (float)g_gl_height;
            uniforms.iChannelResolution[c * 3 + 2] = 1.0f;
        }

        time_t tnow = time(NULL);
        struct tm *lt = localtime(&tnow);
        if (lt)
        {
            uniforms.iDate[0] = (float)(lt->tm_year + 1900);
            uniforms.iDate[1] = (float)(lt->tm_mon);
            uniforms.iDate[2] = (float)(lt->tm_mday);
            uniforms.iDate[3] = (float)(lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
        }

        /* ---- Sound: keep the audio device buffer topped up ---- */
        bool sound_active = audio_ok &&
                            editor_is_tab_visible(&editor, TAB_SOUND) &&
                            programs[TAB_SOUND].valid;
        audio_set_mute(editor.sound_muted);
        audio_set_paused(editor.paused || !sound_active);
        if (sound_active && !editor.paused)
        {
            /* Block staging buffers (256 KB each); keep them off the stack */
            static unsigned char sound_rgba[SOUND_BLOCK_SAMPLES * 4];
            static short         sound_pcm[SOUND_BLOCK_SAMPLES * 2];

            while (audio_frames_writable() >= SOUND_BLOCK_SAMPLES)
            {
                uniforms.iSampleOffset = audio_next_sample();

                GLuint channels[4];
                for (int c = 0; c < 4; c++)
                {
                    channels[c] = renderer_get_buffer_texture(&renderer, c);
                }
                renderer_render_sound_block(&renderer, &programs[TAB_SOUND],
                                            &uniforms, channels, sound_rgba);

                /* Decode: 16 bits per channel split across RGBA */
                for (int s = 0; s < SOUND_BLOCK_SAMPLES; s++)
                {
                    int l = sound_rgba[s * 4 + 0] | (sound_rgba[s * 4 + 1] << 8);
                    int r = sound_rgba[s * 4 + 2] | (sound_rgba[s * 4 + 3] << 8);
                    sound_pcm[s * 2 + 0] = (short)(l - 32768);
                    sound_pcm[s * 2 + 1] = (short)(r - 32768);
                }
                audio_submit(sound_pcm, SOUND_BLOCK_SAMPLES);
                recorder_write_audio(sound_pcm, SOUND_BLOCK_SAMPLES);
            }
            uniforms.iSampleOffset = 0;
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

        /* Capture the finished frame. While paused nothing is written and
           the recording clock is frozen, so pausing pauses the recording. */
        if (recorder_is_active() && rec_pixels && !editor.paused)
        {
            glReadPixels(0, 0, rec_w, rec_h, GL_BGRA, GL_UNSIGNED_BYTE, rec_pixels);
            recorder_write_video(rec_pixels, rec_clock);
            rec_clock += delta;
        }

        SwapBuffers(g_hdc);
        frame_count++;
    }

    /* ---- Cleanup ---- */
    recorder_stop();
    free(rec_pixels);
    audio_shutdown();
    g_editor = NULL;
    for (int i = 0; i < NUM_TABS; i++)
    {
        shader_destroy(&programs[i]);
    }
    shader_shutdown();
    renderer_destroy(&renderer);
    editor_destroy(&editor);

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(g_hglrc);
    ReleaseDC(g_glview, g_hdc);
    DestroyWindow(g_hwnd);

    return 0;
}
