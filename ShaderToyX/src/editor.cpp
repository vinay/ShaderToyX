/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#include "editor.h"
#include <stdio.h>
#include <string.h>
#include <commctrl.h>

/* ------------------------------------------------------------------ */
/*  Default shaders                                                   */
/* ------------------------------------------------------------------ */
static const char *default_shader =
    "//\r\n"
    "//  ShaderToyX default shader\r\n"
    "//\r\n"
    "//  Click and drag on the canvas to move the light (iMouse).\r\n"
    "//  Edit the code, then press F5 or click Compile.\r\n"
    "//\r\n"
    "\r\n"
    "// Cosine gradient: maps t in [0,1) to a smooth colour cycle\r\n"
    "vec3 palette(float t)\r\n"
    "{\r\n"
    "    vec3 phase = vec3(0.00, 0.33, 0.67);\r\n"
    "    return 0.5 + 0.5 * cos(6.28318 * (t + phase));\r\n"
    "}\r\n"
    "\r\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord)\r\n"
    "{\r\n"
    "    // Aspect-corrected coordinates centred on the canvas\r\n"
    "    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;\r\n"
    "\r\n"
    "    // Light position: follows the mouse while dragging, otherwise orbits\r\n"
    "    vec2 light;\r\n"
    "    if (iMouse.z > 0.0)\r\n"
    "        light = (iMouse.xy - 0.5 * iResolution.xy) / iResolution.y;\r\n"
    "    else\r\n"
    "        light = 0.3 * vec2(cos(iTime * 0.6), sin(iTime * 0.8));\r\n"
    "\r\n"
    "    vec2  p = uv - light;\r\n"
    "    float d = length(p);\r\n"
    "    float a = atan(p.y, p.x);\r\n"
    "\r\n"
    "    // Layered waves: rings around the light, angular petals, diagonal drift\r\n"
    "    float w = 0.0;\r\n"
    "    w += sin(d * 18.0 - iTime * 2.5);\r\n"
    "    w += 0.5 * sin(a * 6.0 + iTime * 1.5);\r\n"
    "    w += 0.5 * sin((uv.x * 1.7 + uv.y * 1.1) * 9.0 - iTime);\r\n"
    "    w *= 0.5;\r\n"
    "\r\n"
    "    vec3 col = palette(0.5 * w + 0.15 * iTime + d);\r\n"
    "\r\n"
    "    // Warm glow around the light\r\n"
    "    col += vec3(1.0, 0.85, 0.6) * 0.04 / (d + 0.02);\r\n"
    "\r\n"
    "    // Vignette\r\n"
    "    col *= 1.0 - 0.6 * dot(uv, uv);\r\n"
    "\r\n"
    "    fragColor = vec4(col, 1.0);\r\n"
    "}\r\n";

static const char *default_buffer_shader =
    "void mainImage(out vec4 fragColor, in vec2 fragCoord)\r\n"
    "{\r\n"
    "    fragColor = vec4(0.0, 0.0, 0.0, 1.0);\r\n"
    "}\r\n";

/* ------------------------------------------------------------------ */
/*  Layout constants (in 96-DPI pixels; scaled via sc() at runtime)   */
/* ------------------------------------------------------------------ */
#define TOOLBAR_H      32
#define TAB_BAR_H      28
#define TAB_W          80
#define ERROR_H       100
#define PAD             4
#define ICON_BTN_W     28
#define COMPILE_BTN_W 100
#define FPS_LABEL_W   250
#define MONO_FONT_H    16
#define TAB_FONT_H     14
#define TAB_CLOSE_W    20   /* clickable close region on buffer tabs */

static const char *panel_class = "ShaderToyX_EditorPanel";
static bool panel_class_registered = false;

static const char *tab_names[NUM_TABS] = { "Image", "Buf A", "Buf B", "Buf C", "Buf D" };

/* Scale a 96-DPI pixel value to the editor's current DPI */
static int sc(const Editor *e, int v)
{
    return MulDiv(v, e->dpi, 96);
}

/* ------------------------------------------------------------------ */
static void add_tooltip(HWND parent, HWND control, HINSTANCE hInst, const char *text)
{
    HWND tip = CreateWindowExA(
        0, TOOLTIPS_CLASSA, NULL,
        WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        parent, NULL, hInst, NULL
    );
    if (!tip) return;

    TTTOOLINFOA ti = {};
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd     = parent;
    ti.uId      = (UINT_PTR)control;
    ti.lpszText = (LPSTR)text;
    SendMessageA(tip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

/* ------------------------------------------------------------------ */
/*  Subclass proc for the EDIT controls — handles Ctrl+A              */
/* ------------------------------------------------------------------ */
static WNDPROC g_orig_edit_proc = NULL;

static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        SendMessageA(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }
    return CallWindowProcA(g_orig_edit_proc, hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  Update tab button visual states                                   */
/* ------------------------------------------------------------------ */
static void update_tab_visuals(Editor *e)
{
    for (int i = 0; i < NUM_TABS; i++)
    {
        if (e->tab_btns[i])
        {
            InvalidateRect(e->tab_btns[i], NULL, TRUE);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Push the active tab's error log to the error box, only if changed */
/* ------------------------------------------------------------------ */
static void refresh_error_display(Editor *e)
{
    int t = e->active_tab;
    const char *want = (t >= 0 && t < NUM_TABS && e->error_log[t][0])
                       ? e->error_log[t]
                       : "OK - compile successful";

    if (strcmp(want, e->shown_error) != 0)
    {
        strncpy(e->shown_error, want, EDITOR_ERROR_LOG_SIZE - 1);
        e->shown_error[EDITOR_ERROR_LOG_SIZE - 1] = '\0';
        SetWindowTextA(e->error_edit, e->shown_error);
    }
}

/* ------------------------------------------------------------------ */
/*  Owner-draw helpers for the toolbar icons                          */
/* ------------------------------------------------------------------ */
static void draw_icon_button(Editor *e, DRAWITEMSTRUCT *dis)
{
    RECT rc = dis->rcItem;
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    /* Dark background */
    HBRUSH bg = CreateSolidBrush(RGB(45, 45, 45));
    FillRect(dis->hDC, &rc, bg);
    DeleteObject(bg);

    COLORREF iconClr = RGB(180, 180, 180);

    /* Pause / Play button */
    if (dis->CtlID == IDC_PAUSE_BTN)
    {
        if (e->paused)
        {
            /* Play triangle: right-pointing */
            POINT tri[3];
            tri[0] = { cx - sc(e, 4), cy - sc(e, 6) };
            tri[1] = { cx - sc(e, 4), cy + sc(e, 6) };
            tri[2] = { cx + sc(e, 5), cy };
            HBRUSH fb = CreateSolidBrush(iconClr);
            HPEN np = CreatePen(PS_SOLID, 1, iconClr);
            HGDIOBJ ob = SelectObject(dis->hDC, fb);
            HGDIOBJ op = SelectObject(dis->hDC, np);
            Polygon(dis->hDC, tri, 3);
            SelectObject(dis->hDC, ob);
            SelectObject(dis->hDC, op);
            DeleteObject(fb);
            DeleteObject(np);
        }
        else
        {
            /* Pause: two vertical bars */
            HBRUSH fb = CreateSolidBrush(iconClr);
            RECT bar1 = { cx - sc(e, 5), cy - sc(e, 5), cx - sc(e, 2), cy + sc(e, 5) };
            RECT bar2 = { cx + sc(e, 2), cy - sc(e, 5), cx + sc(e, 5), cy + sc(e, 5) };
            FillRect(dis->hDC, &bar1, fb);
            FillRect(dis->hDC, &bar2, fb);
            DeleteObject(fb);
        }
    }

    /* Reset / Rewind button: |< skip-back icon */
    if (dis->CtlID == IDC_RESET_BTN)
    {
        HBRUSH fb = CreateSolidBrush(iconClr);
        HPEN np = CreatePen(PS_SOLID, 1, iconClr);
        HGDIOBJ ob = SelectObject(dis->hDC, fb);
        HGDIOBJ op = SelectObject(dis->hDC, np);

        /* Left bar */
        RECT bar = { cx - sc(e, 6), cy - sc(e, 5), cx - sc(e, 4), cy + sc(e, 5) };
        FillRect(dis->hDC, &bar, fb);

        /* Left-pointing triangle */
        POINT tri[3];
        tri[0] = { cx + sc(e, 5), cy - sc(e, 6) };
        tri[1] = { cx + sc(e, 5), cy + sc(e, 6) };
        tri[2] = { cx - sc(e, 3), cy };
        Polygon(dis->hDC, tri, 3);

        SelectObject(dis->hDC, ob);
        SelectObject(dis->hDC, op);
        DeleteObject(fb);
        DeleteObject(np);
    }

    /* Record button: filled red circle */
    if (dis->CtlID == IDC_REC_BTN)
    {
        HBRUSH rb = CreateSolidBrush(RGB(200, 60, 60));
        HPEN rp = CreatePen(PS_SOLID, 1, RGB(200, 60, 60));
        HGDIOBJ ob = SelectObject(dis->hDC, rb);
        HGDIOBJ op = SelectObject(dis->hDC, rp);
        Ellipse(dis->hDC, cx - sc(e, 5), cy - sc(e, 5), cx + sc(e, 5), cy + sc(e, 5));
        SelectObject(dis->hDC, ob);
        SelectObject(dis->hDC, op);
        DeleteObject(rb);
        DeleteObject(rp);
    }

    /* Speaker button: speaker cone + sound waves */
    if (dis->CtlID == IDC_SPEAKER_BTN)
    {
        HPEN ip = CreatePen(PS_SOLID, 1, iconClr);
        HBRUSH ib = CreateSolidBrush(iconClr);
        HGDIOBJ op = SelectObject(dis->hDC, ip);
        HGDIOBJ ob = SelectObject(dis->hDC, ib);

        /* Speaker body (small rectangle) */
        RECT body = { cx - sc(e, 7), cy - sc(e, 3), cx - sc(e, 3), cy + sc(e, 3) };
        FillRect(dis->hDC, &body, ib);

        /* Speaker cone (triangle) */
        POINT cone[3];
        cone[0] = { cx - sc(e, 3), cy - sc(e, 3) };
        cone[1] = { cx - sc(e, 3), cy + sc(e, 3) };
        cone[2] = { cx + sc(e, 1), cy };
        Polygon(dis->hDC, cone, 3);

        /* Sound wave arcs */
        SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Arc(dis->hDC, cx + sc(e, 1), cy - sc(e, 4), cx + sc(e, 7), cy + sc(e, 4),
            cx + sc(e, 1), cy - sc(e, 4), cx + sc(e, 1), cy + sc(e, 4));
        Arc(dis->hDC, cx + sc(e, 3), cy - sc(e, 7), cx + sc(e, 11), cy + sc(e, 7),
            cx + sc(e, 3), cy - sc(e, 7), cx + sc(e, 3), cy + sc(e, 7));

        SelectObject(dis->hDC, ob);
        SelectObject(dis->hDC, op);
        DeleteObject(ip);
        DeleteObject(ib);
    }

    /* Fullscreen button: four corner brackets */
    if (dis->CtlID == IDC_FULLSCR_BTN)
    {
        HPEN ip = CreatePen(PS_SOLID, sc(e, 2), iconClr);
        HGDIOBJ op = SelectObject(dis->hDC, ip);

        int m = sc(e, 5); /* margin from center */
        int s = sc(e, 4); /* stroke length */

        /* Top-left corner */
        MoveToEx(dis->hDC, cx - m, cy - m + s, NULL);
        LineTo(dis->hDC, cx - m, cy - m);
        LineTo(dis->hDC, cx - m + s, cy - m);

        /* Top-right corner */
        MoveToEx(dis->hDC, cx + m - s, cy - m, NULL);
        LineTo(dis->hDC, cx + m, cy - m);
        LineTo(dis->hDC, cx + m, cy - m + s);

        /* Bottom-left corner */
        MoveToEx(dis->hDC, cx - m, cy + m - s, NULL);
        LineTo(dis->hDC, cx - m, cy + m);
        LineTo(dis->hDC, cx - m + s, cy + m);

        /* Bottom-right corner */
        MoveToEx(dis->hDC, cx + m - s, cy + m, NULL);
        LineTo(dis->hDC, cx + m, cy + m);
        LineTo(dis->hDC, cx + m, cy + m - s);

        SelectObject(dis->hDC, op);
        DeleteObject(ip);
    }
}

/* ------------------------------------------------------------------ */
/*  Panel window procedure                                            */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Editor *e = (Editor *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_COMMAND:
        if (e)
        {
            int id = LOWORD(wParam);
            int notify = HIWORD(wParam);

            if (id == IDC_COMPILE_BTN && notify == BN_CLICKED)
            {
                editor_sync_from_control(e);
                e->needs_compile = true;
                return 0;
            }

            if (id == IDC_PAUSE_BTN && notify == BN_CLICKED)
            {
                e->paused = !e->paused;
                InvalidateRect(e->pause_btn, NULL, TRUE);
                return 0;
            }

            if (id == IDC_RESET_BTN && notify == BN_CLICKED)
            {
                e->reset_time = true;
                return 0;
            }

            /* Tab button clicks — check for close "x" on buffer tabs */
            if (id >= IDC_TAB_IMAGE && id <= IDC_TAB_BUF_D && notify == BN_CLICKED)
            {
                int tab = id - IDC_TAB_IMAGE;

                /* For buffer tabs, check if the click was in the close region */
                if (tab != TAB_IMAGE)
                {
                    POINT pt;
                    GetCursorPos(&pt);
                    ScreenToClient(e->tab_btns[tab], &pt);
                    RECT rc;
                    GetClientRect(e->tab_btns[tab], &rc);
                    if (pt.x >= rc.right - sc(e, TAB_CLOSE_W))
                    {
                        editor_remove_tab(e, tab);
                        return 0;
                    }
                }

                editor_switch_tab(e, tab);
                return 0;
            }

            /* "+" button adds next buffer tab */
            if (id == IDC_ADD_TAB_BTN && notify == BN_CLICKED)
            {
                editor_add_tab(e);
                return 0;
            }

            /* IDC_REC_BTN, IDC_SPEAKER_BTN, IDC_FULLSCR_BTN: not implemented yet */
        }
        break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkColor(hdc, RGB(30, 30, 30));
        static HBRUSH dark_brush = CreateSolidBrush(RGB(30, 30, 30));
        return (LRESULT)dark_brush;
    }

    case WM_CTLCOLORBTN:
    {
        static HBRUSH btn_brush = CreateSolidBrush(RGB(40, 40, 40));
        return (LRESULT)btn_brush;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
        if (e && dis->CtlID >= IDC_TAB_IMAGE && dis->CtlID <= IDC_TAB_BUF_D)
        {
            int tab = dis->CtlID - IDC_TAB_IMAGE;
            bool active = (tab == e->active_tab);

            /* Background */
            HBRUSH bg = CreateSolidBrush(active ? RGB(60, 60, 70) : RGB(40, 40, 40));
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            /* Bottom accent line for active tab */
            if (active)
            {
                RECT accent = dis->rcItem;
                accent.top = accent.bottom - sc(e, 2);
                HBRUSH line = CreateSolidBrush(RGB(97, 175, 239));
                FillRect(dis->hDC, &accent, line);
                DeleteObject(line);
            }

            /* Text — offset left to make room for close button on buffer tabs */
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, active ? RGB(255, 255, 255) : RGB(160, 160, 160));
            HGDIOBJ of = SelectObject(dis->hDC, e->tab_font);
            RECT textRect = dis->rcItem;
            if (tab != TAB_IMAGE) textRect.right -= sc(e, 16); /* leave room for X */
            DrawTextA(dis->hDC, tab_names[tab], -1, &textRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* Draw close "x" on buffer tabs (not Image) */
            if (tab != TAB_IMAGE)
            {
                RECT xRect = dis->rcItem;
                xRect.left = xRect.right - sc(e, 18);
                xRect.top += sc(e, 2);
                xRect.bottom -= sc(e, 2);
                SetTextColor(dis->hDC, active ? RGB(180, 180, 180) : RGB(120, 120, 120));
                DrawTextA(dis->hDC, "x", -1, &xRect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(dis->hDC, of);

            return TRUE;
        }

        /* Draw the "+" add-tab button */
        if (e && dis->CtlID == IDC_ADD_TAB_BTN)
        {
            HBRUSH bg = CreateSolidBrush(RGB(50, 50, 50));
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(140, 140, 140));
            HGDIOBJ of = SelectObject(dis->hDC, e->tab_font);
            DrawTextA(dis->hDC, "+", -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, of);
            return TRUE;
        }

        /* Toolbar icon buttons */
        if (e && (dis->CtlID == IDC_PAUSE_BTN   || dis->CtlID == IDC_RESET_BTN ||
                  dis->CtlID == IDC_REC_BTN     || dis->CtlID == IDC_SPEAKER_BTN ||
                  dis->CtlID == IDC_FULLSCR_BTN))
        {
            draw_icon_button(e, dis);
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(40, 40, 40));
        FillRect((HDC)wParam, &rc, bg);
        DeleteObject(bg);
        return 1;
    }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

/* ------------------------------------------------------------------ */
/*  (Re)create fonts for the current DPI and apply them               */
/* ------------------------------------------------------------------ */
static void create_fonts(Editor *e)
{
    if (e->mono_font) DeleteObject(e->mono_font);
    if (e->tab_font)  DeleteObject(e->tab_font);

    e->mono_font = CreateFontA(
        sc(e, MONO_FONT_H), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas"
    );

    e->tab_font = CreateFontA(
        sc(e, TAB_FONT_H), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );

    if (e->code_edit)   SendMessageA(e->code_edit,   WM_SETFONT, (WPARAM)e->mono_font, TRUE);
    if (e->error_edit)  SendMessageA(e->error_edit,  WM_SETFONT, (WPARAM)e->mono_font, TRUE);
    if (e->compile_btn) SendMessageA(e->compile_btn, WM_SETFONT, (WPARAM)e->tab_font,  TRUE);
    if (e->fps_label)   SendMessageA(e->fps_label,   WM_SETFONT, (WPARAM)e->tab_font,  TRUE);
}

/* ------------------------------------------------------------------ */
static HWND create_child(HWND parent, HINSTANCE hInstance, const char *cls,
                         const char *text, DWORD style, DWORD ex_style, int id)
{
    /* All children are created at 0,0 with zero size; editor_layout()
       positions everything for the current DPI. */
    return CreateWindowExA(ex_style, cls, text, style, 0, 0, 0, 0,
                           parent, (HMENU)(UINT_PTR)id, hInstance, NULL);
}

/* ------------------------------------------------------------------ */
void editor_init(Editor *e, HWND parent, HINSTANCE hInstance, int dpi)
{
    memset(e, 0, sizeof(Editor));

    e->dpi = (dpi > 0) ? dpi : 96;

    /* Initialize Image tab with default shader */
    strncpy(e->code[TAB_IMAGE], default_shader, EDITOR_CODE_SIZE - 1);

    /* Initialize buffer tabs with empty shaders */
    for (int i = TAB_BUF_A; i <= TAB_BUF_D; i++)
    {
        strncpy(e->code[i], default_buffer_shader, EDITOR_CODE_SIZE - 1);
    }

    e->needs_compile = false;
    e->show_editor   = true;
    e->paused        = false;
    e->reset_time    = false;
    e->active_tab    = TAB_IMAGE;

    /* Only Image tab visible by default */
    e->tab_visible[TAB_IMAGE] = true;
    for (int i = TAB_BUF_A; i <= TAB_BUF_D; i++)
    {
        e->tab_visible[i] = false;
    }

    /* Make sure the common-controls classes (tooltips) are registered */
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    /* Register panel window class once */
    if (!panel_class_registered)
    {
        WNDCLASSA wc = {};
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = panel_proc;
        wc.hInstance     = hInstance;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = panel_class;
        RegisterClassA(&wc);
        panel_class_registered = true;
    }

    /* Panel (child of the main window, sits on the left) */
    e->panel = CreateWindowExA(
        0, panel_class, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        parent, NULL, hInstance, NULL
    );
    SetWindowLongPtrA(e->panel, GWLP_USERDATA, (LONG_PTR)e);

    const DWORD btn   = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;
    const DWORD odbtn = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;

    /* Toolbar */
    e->compile_btn = create_child(e->panel, hInstance, "BUTTON", "Compile (F5)", btn,   0, IDC_COMPILE_BTN);
    e->pause_btn   = create_child(e->panel, hInstance, "BUTTON", "",             odbtn, 0, IDC_PAUSE_BTN);
    e->reset_btn   = create_child(e->panel, hInstance, "BUTTON", "",             odbtn, 0, IDC_RESET_BTN);
    e->fps_label   = create_child(e->panel, hInstance, "STATIC", "",
                                  WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_FPS_LABEL);
    e->fullscr_btn = create_child(e->panel, hInstance, "BUTTON", "",             odbtn, 0, IDC_FULLSCR_BTN);
    e->speaker_btn = create_child(e->panel, hInstance, "BUTTON", "",             odbtn, 0, IDC_SPEAKER_BTN);
    e->rec_btn     = create_child(e->panel, hInstance, "BUTTON", "",             odbtn, 0, IDC_REC_BTN);

    add_tooltip(e->panel, e->pause_btn,   hInstance, "Pause / Resume");
    add_tooltip(e->panel, e->reset_btn,   hInstance, "Reset Time");
    add_tooltip(e->panel, e->rec_btn,     hInstance, "Record (not implemented yet)");
    add_tooltip(e->panel, e->speaker_btn, hInstance, "Sound On/Off (not implemented yet)");
    add_tooltip(e->panel, e->fullscr_btn, hInstance, "Fullscreen (not implemented yet)");

    /* Tab buttons (owner-draw for custom look); visibility set in layout */
    for (int i = 0; i < NUM_TABS; i++)
    {
        e->tab_btns[i] = create_child(e->panel, hInstance, "BUTTON", tab_names[i],
                                      WS_CHILD | BS_OWNERDRAW, 0, IDC_TAB_IMAGE + i);
    }
    e->add_tab_btn = create_child(e->panel, hInstance, "BUTTON", "+", odbtn, 0, IDC_ADD_TAB_BTN);

    /* Code editor (multiline EDIT) */
    e->code_edit = create_child(e->panel, hInstance, "EDIT", e->code[TAB_IMAGE],
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
        WS_EX_CLIENTEDGE, IDC_CODE_EDIT);
    {
        int tab_stop = 16; /* dialog units, so not DPI-dependent */
        SendMessageA(e->code_edit, EM_SETTABSTOPS, 1, (LPARAM)&tab_stop);
    }
    SendMessageA(e->code_edit, EM_SETLIMITTEXT, EDITOR_CODE_SIZE - 1, 0);

    /* Error log (read-only EDIT) */
    e->error_edit = create_child(e->panel, hInstance, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        WS_EX_CLIENTEDGE, IDC_ERROR_EDIT);

    /* Subclass edit controls so Ctrl+A works (both share the EDIT class proc) */
    g_orig_edit_proc = (WNDPROC)SetWindowLongPtrA(e->code_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);
    SetWindowLongPtrA(e->error_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);

    create_fonts(e);
    editor_layout(e);
    refresh_error_display(e);
}

/* ------------------------------------------------------------------ */
void editor_destroy(Editor *e)
{
    if (e->panel)
    {
        DestroyWindow(e->panel);
        e->panel = NULL;
    }
    if (e->mono_font)
    {
        DeleteObject(e->mono_font);
        e->mono_font = NULL;
    }
    if (e->tab_font)
    {
        DeleteObject(e->tab_font);
        e->tab_font = NULL;
    }
}

/* ------------------------------------------------------------------ */
void editor_toggle(Editor *e)
{
    e->show_editor = !e->show_editor;
    ShowWindow(e->panel, e->show_editor ? SW_SHOW : SW_HIDE);
}

/* ------------------------------------------------------------------ */
int editor_panel_width(const Editor *e)
{
    return e->show_editor ? sc(e, EDITOR_PANEL_W) : 0;
}

/* ------------------------------------------------------------------ */
void editor_set_dpi(Editor *e, int dpi)
{
    if (dpi <= 0 || dpi == e->dpi)
    {
        return;
    }
    e->dpi = dpi;
    create_fonts(e);
    editor_layout(e);
    InvalidateRect(e->panel, NULL, TRUE);
}

/* ------------------------------------------------------------------ */
void editor_layout(Editor *e)
{
    if (!e->panel)
    {
        return;
    }

    HWND parent = GetParent(e->panel);
    RECT prc;
    GetClientRect(parent, &prc);
    int panel_h = prc.bottom;
    int panel_w = sc(e, EDITOR_PANEL_W);

    MoveWindow(e->panel, 0, 0, panel_w, panel_h, TRUE);

    /* ---- Toolbar ---- */
    int pad    = sc(e, PAD);
    int tb_h   = sc(e, TOOLBAR_H);
    int btn_h  = tb_h - pad * 2;
    int icon_w = sc(e, ICON_BTN_W);
    int x      = pad;

    MoveWindow(e->compile_btn, x, pad, sc(e, COMPILE_BTN_W), btn_h, TRUE);
    x += sc(e, COMPILE_BTN_W) + pad;
    MoveWindow(e->pause_btn, x, pad, icon_w, btn_h, TRUE);
    x += icon_w + pad;
    MoveWindow(e->reset_btn, x, pad, icon_w, btn_h, TRUE);
    x += icon_w + pad + sc(e, 2);
    MoveWindow(e->fps_label, x, pad + sc(e, 4), sc(e, FPS_LABEL_W), btn_h, TRUE);

    /* Right-justified toolbar buttons: Rec, Speaker, Fullscreen */
    int rx = panel_w - pad - icon_w;
    MoveWindow(e->fullscr_btn, rx, pad, icon_w, btn_h, TRUE);
    rx -= icon_w + sc(e, 2);
    MoveWindow(e->speaker_btn, rx, pad, icon_w, btn_h, TRUE);
    rx -= icon_w + sc(e, 2);
    MoveWindow(e->rec_btn, rx, pad, icon_w, btn_h, TRUE);

    /* ---- Tab bar: only lay out visible tabs, then the "+" button ---- */
    int tab_h = sc(e, TAB_BAR_H);
    int tab_w = sc(e, TAB_W);
    int tab_x = pad;
    bool all_visible = true;
    for (int i = 0; i < NUM_TABS; i++)
    {
        if (e->tab_visible[i])
        {
            MoveWindow(e->tab_btns[i], tab_x, tb_h, tab_w, tab_h, TRUE);
            ShowWindow(e->tab_btns[i], SW_SHOW);
            tab_x += tab_w;
        }
        else
        {
            ShowWindow(e->tab_btns[i], SW_HIDE);
            all_visible = false;
        }
    }

    /* Show "+" button only if there are still hidden tabs */
    if (!all_visible)
    {
        MoveWindow(e->add_tab_btn, tab_x, tb_h, tab_h, tab_h, TRUE);
        ShowWindow(e->add_tab_btn, SW_SHOW);
    }
    else
    {
        ShowWindow(e->add_tab_btn, SW_HIDE);
    }

    /* ---- Code editor and error log ---- */
    int err_h    = sc(e, ERROR_H);
    int code_top = tb_h + tab_h;
    int code_h   = panel_h - code_top - err_h - pad;
    if (code_h < sc(e, 50)) code_h = sc(e, 50);

    MoveWindow(e->code_edit, pad, code_top, panel_w - pad * 2, code_h, TRUE);
    MoveWindow(e->error_edit, pad, panel_h - err_h, panel_w - pad * 2, err_h - pad, TRUE);
}

/* ------------------------------------------------------------------ */
void editor_update(Editor *e, float elapsed_time, float fps, int canvas_w, int canvas_h)
{
    /* FPS label with dimensions — only touch the control when the text changes */
    char buf[96];
    snprintf(buf, sizeof(buf), "%.0f FPS | %.1fs | %dx%d", fps, elapsed_time, canvas_w, canvas_h);
    if (strcmp(buf, e->shown_fps) != 0)
    {
        strncpy(e->shown_fps, buf, sizeof(e->shown_fps) - 1);
        e->shown_fps[sizeof(e->shown_fps) - 1] = '\0';
        SetWindowTextA(e->fps_label, buf);
    }

    refresh_error_display(e);
}

/* ------------------------------------------------------------------ */
void editor_sync_from_control(Editor *e)
{
    GetWindowTextA(e->code_edit, e->code[e->active_tab], EDITOR_CODE_SIZE);
}

/* ------------------------------------------------------------------ */
void editor_set_error(Editor *e, int tab, const char *text)
{
    if (tab < 0 || tab >= NUM_TABS)
    {
        return;
    }

    /* GL info logs use bare '\n'; the EDIT control needs "\r\n" */
    char *dst = e->error_log[tab];
    size_t n = 0;
    for (const char *s = text ? text : ""; *s && n < EDITOR_ERROR_LOG_SIZE - 1; s++)
    {
        if (*s == '\r')
        {
            continue; /* drop any existing CR; we add our own */
        }
        if (*s == '\n')
        {
            if (n + 2 > EDITOR_ERROR_LOG_SIZE - 1) break;
            dst[n++] = '\r';
        }
        dst[n++] = *s;
    }
    dst[n] = '\0';
}

/* ------------------------------------------------------------------ */
void editor_switch_tab(Editor *e, int tab)
{
    if (tab < 0 || tab >= NUM_TABS || tab == e->active_tab)
    {
        return;
    }

    /* Save current tab's text (active_tab is -1 while removing a tab) */
    if (e->active_tab >= 0 && e->active_tab < NUM_TABS)
    {
        editor_sync_from_control(e);
    }

    /* Switch */
    e->active_tab = tab;

    /* Load new tab's text into the EDIT control */
    SetWindowTextA(e->code_edit, e->code[tab]);

    update_tab_visuals(e);
    refresh_error_display(e);
}

/* ------------------------------------------------------------------ */
void editor_add_tab(Editor *e)
{
    /* Find the next hidden buffer tab and make it visible */
    for (int i = TAB_BUF_A; i <= TAB_BUF_D; i++)
    {
        if (!e->tab_visible[i])
        {
            e->tab_visible[i] = true;
            editor_layout(e);
            editor_switch_tab(e, i);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
bool editor_is_tab_visible(Editor *e, int tab)
{
    if (tab < 0 || tab >= NUM_TABS) return false;
    return e->tab_visible[tab];
}

/* ------------------------------------------------------------------ */
void editor_remove_tab(Editor *e, int tab)
{
    /* Image tab cannot be removed */
    if (tab == TAB_IMAGE || tab < 0 || tab >= NUM_TABS)
    {
        return;
    }

    if (!e->tab_visible[tab])
    {
        return;
    }

    /* Hide the tab */
    e->tab_visible[tab] = false;

    /* Clear its shader code and error log */
    strncpy(e->code[tab], default_buffer_shader, EDITOR_CODE_SIZE - 1);
    e->code[tab][EDITOR_CODE_SIZE - 1] = '\0';
    e->error_log[tab][0] = '\0';

    /* If we just closed the active tab, switch to another visible one */
    if (e->active_tab == tab)
    {
        /* Find the nearest visible tab to the left, falling back to Image */
        int new_tab = TAB_IMAGE;
        for (int i = tab - 1; i >= 0; i--)
        {
            if (e->tab_visible[i]) { new_tab = i; break; }
        }
        e->active_tab = -1; /* force switch without saving the closed tab */
        editor_switch_tab(e, new_tab);
    }

    /* Trigger recompile so the removed buffer's program is cleared */
    e->needs_compile = true;

    editor_layout(e);
}
