#include "editor.h"
#include <stdio.h>
#include <string.h>
#include <commctrl.h>

static const char *default_shader =
    "//\r\n"
    "//  Shader Source : https://www.shadertoy.com/view/MlKSWm\r\n"
    "//\r\n"
    "\r\n"
    "vec3 mod289(vec3 x) {\r\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\r\n"
    "}\r\n"
    "\r\n"
    "vec4 mod289(vec4 x) {\r\n"
    "    return x - floor(x * (1.0 / 289.0)) * 289.0;\r\n"
    "}\r\n"
    "\r\n"
    "vec4 permute(vec4 x) {\r\n"
    "    return mod289(((x*34.0)+1.0)*x);\r\n"
    "}\r\n"
    "\r\n"
    "vec4 taylorInvSqrt(vec4 r)\r\n"
    "{\r\n"
    "    return 1.79284291400159 - 0.85373472095314 * r;\r\n"
    "}\r\n"
    "\r\n"
    "float snoise(vec3 v)\r\n"
    "{\r\n"
    "    const vec2 C = vec2(1.0/6.0, 1.0/3.0);\r\n"
    "    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);\r\n"
    "\r\n"
    "    // First corner\r\n"
    "    vec3 i  = floor(v + dot(v, C.yyy));\r\n"
    "    vec3 x0 = v - i + dot(i, C.xxx);\r\n"
    "\r\n"
    "    // Other corners\r\n"
    "    vec3 g = step(x0.yzx, x0.xyz);\r\n"
    "    vec3 l = 1.0 - g;\r\n"
    "    vec3 i1 = min(g.xyz, l.zxy);\r\n"
    "    vec3 i2 = max(g.xyz, l.zxy);\r\n"
    "\r\n"
    "    vec3 x1 = x0 - i1 + C.xxx;\r\n"
    "    vec3 x2 = x0 - i2 + C.yyy;\r\n"
    "    vec3 x3 = x0 - D.yyy;\r\n"
    "\r\n"
    "    // Permutations\r\n"
    "    i = mod289(i);\r\n"
    "    vec4 p = permute(permute(permute(\r\n"
    "                 i.z + vec4(0.0, i1.z, i2.z, 1.0))\r\n"
    "               + i.y + vec4(0.0, i1.y, i2.y, 1.0))\r\n"
    "               + i.x + vec4(0.0, i1.x, i2.x, 1.0));\r\n"
    "\r\n"
    "    float n_ = 0.142857142857;\r\n"
    "    vec3 ns = n_ * D.wyz - D.xzx;\r\n"
    "\r\n"
    "    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);\r\n"
    "\r\n"
    "    vec4 x_ = floor(j * ns.z);\r\n"
    "    vec4 y_ = floor(j - 7.0 * x_);\r\n"
    "\r\n"
    "    vec4 x = x_ * ns.x + ns.yyyy;\r\n"
    "    vec4 y = y_ * ns.x + ns.yyyy;\r\n"
    "    vec4 h = 1.0 - abs(x) - abs(y);\r\n"
    "\r\n"
    "    vec4 b0 = vec4(x.xy, y.xy);\r\n"
    "    vec4 b1 = vec4(x.zw, y.zw);\r\n"
    "\r\n"
    "    vec4 s0 = floor(b0)*2.0 + 1.0;\r\n"
    "    vec4 s1 = floor(b1)*2.0 + 1.0;\r\n"
    "    vec4 sh = -step(h, vec4(0.0));\r\n"
    "\r\n"
    "    vec4 a0 = b0.xzyw + s0.xzyw*sh.xxyy;\r\n"
    "    vec4 a1 = b1.xzyw + s1.xzyw*sh.zzww;\r\n"
    "\r\n"
    "    vec3 p0 = vec3(a0.xy, h.x);\r\n"
    "    vec3 p1 = vec3(a0.zw, h.y);\r\n"
    "    vec3 p2 = vec3(a1.xy, h.z);\r\n"
    "    vec3 p3 = vec3(a1.zw, h.w);\r\n"
    "\r\n"
    "    vec4 norm = inversesqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));\r\n"
    "    p0 *= norm.x;\r\n"
    "    p1 *= norm.y;\r\n"
    "    p2 *= norm.z;\r\n"
    "    p3 *= norm.w;\r\n"
    "\r\n"
    "    vec4 m = max(0.6 - vec4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);\r\n"
    "    m = m * m;\r\n"
    "    return 42.0 * dot(m*m, vec4(dot(p0,x0), dot(p1,x1),\r\n"
    "                                dot(p2,x2), dot(p3,x3)));\r\n"
    "}\r\n"
    "\r\n"
    "//////////////////////////////////////////////////////////////\r\n"
    "\r\n"
    "// PRNG\r\n"
    "// From https://www.shadertoy.com/view/4djSRW\r\n"
    "float prng(in vec2 seed) {\r\n"
    "    seed = fract(seed * vec2(5.3983, 5.4427));\r\n"
    "    seed += dot(seed.yx, seed.xy + vec2(21.5351, 14.3137));\r\n"
    "    return fract(seed.x * seed.y * 95.4337);\r\n"
    "}\r\n"
    "\r\n"
    "//////////////////////////////////////////////////////////////\r\n"
    "\r\n"
    "float PI = 3.1415926535897932384626433832795;\r\n"
    "\r\n"
    "float noiseStack(vec3 pos, int octaves, float falloff) {\r\n"
    "    float noise = snoise(vec3(pos));\r\n"
    "    float off = 1.0;\r\n"
    "    if (octaves>1) {\r\n"
    "        pos *= 2.0;\r\n"
    "        off *= falloff;\r\n"
    "        noise = (1.0-off)*noise + off*snoise(vec3(pos));\r\n"
    "    }\r\n"
    "    if (octaves>2) {\r\n"
    "        pos *= 2.0;\r\n"
    "        off *= falloff;\r\n"
    "        noise = (1.0-off)*noise + off*snoise(vec3(pos));\r\n"
    "    }\r\n"
    "    if (octaves>3) {\r\n"
    "        pos *= 2.0;\r\n"
    "        off *= falloff;\r\n"
    "        noise = (1.0-off)*noise + off*snoise(vec3(pos));\r\n"
    "    }\r\n"
    "    return (1.0+noise)/2.0;\r\n"
    "}\r\n"
    "\r\n"
    "vec2 noiseStackUV(vec3 pos, int octaves, float falloff, float diff) {\r\n"
    "    float displaceA = noiseStack(pos, octaves, falloff);\r\n"
    "    float displaceB = noiseStack(pos+vec3(3984.293,423.21,5235.19), octaves, falloff);\r\n"
    "    return vec2(displaceA, displaceB);\r\n"
    "}\r\n"
    "\r\n"
    "void mainImage(out vec4 fragColor, in vec2 fragCoord) {\r\n"
    "    float time = iTime;\r\n"
    "    vec2 resolution = iResolution.xy;\r\n"
    "    vec2 drag = iMouse.xy;\r\n"
    "    vec2 offset = iMouse.xy;\r\n"
    "    //\r\n"
    "    float xpart = fragCoord.x/resolution.x;\r\n"
    "    float ypart = fragCoord.y/resolution.y;\r\n"
    "    //\r\n"
    "    float clip = 210.0;\r\n"
    "    float ypartClip = fragCoord.y/clip;\r\n"
    "    float ypartClippedFalloff = clamp(2.0-ypartClip,0.0,1.0);\r\n"
    "    float ypartClipped = min(ypartClip,1.0);\r\n"
    "    float ypartClippedn = 1.0-ypartClipped;\r\n"
    "    //\r\n"
    "    float xfuel = 1.0-abs(2.0*xpart-1.0);\r\n"
    "    //\r\n"
    "    float timeSpeed = 0.5;\r\n"
    "    float realTime = timeSpeed*time;\r\n"
    "    //\r\n"
    "    vec2 coordScaled = 0.01*fragCoord - 0.02*vec2(offset.x,0.0);\r\n"
    "    vec3 position = vec3(coordScaled,0.0) + vec3(1223.0,6434.0,8425.0);\r\n"
    "    vec3 flow = vec3(4.1*(0.5-xpart)*pow(ypartClippedn,4.0),-2.0*xfuel*pow(ypartClippedn,64.0),0.0);\r\n"
    "    vec3 timing = realTime*vec3(0.0,-1.7,1.1) + flow;\r\n"
    "    //\r\n"
    "    vec3 displacePos = vec3(1.0,0.5,1.0)*2.4*position+realTime*vec3(0.01,-0.7,1.3);\r\n"
    "    vec3 displace3 = vec3(noiseStackUV(displacePos,2,0.4,0.1),0.0);\r\n"
    "    //\r\n"
    "    vec3 noiseCoord = (vec3(2.0,1.0,1.0)*position+timing+0.4*displace3)/1.0;\r\n"
    "    float noise = noiseStack(noiseCoord,3,0.4);\r\n"
    "    //\r\n"
    "    float flames = pow(ypartClipped,0.3*xfuel)*pow(noise,0.3*xfuel);\r\n"
    "    //\r\n"
    "    float f = ypartClippedFalloff*pow(1.0-flames*flames*flames,8.0);\r\n"
    "    float fff = f*f*f;\r\n"
    "    vec3 fire = 1.5*vec3(f, fff, fff*fff);\r\n"
    "    //\r\n"
    "    // smoke\r\n"
    "    float smokeNoise = 0.5+snoise(0.4*position+timing*vec3(1.0,1.0,0.2))/2.0;\r\n"
    "    vec3 smoke = vec3(0.3*pow(xfuel,3.0)*pow(ypart,2.0)*(smokeNoise+0.4*(1.0-noise)));\r\n"
    "    //\r\n"
    "    // sparks\r\n"
    "    float sparkGridSize = 30.0;\r\n"
    "    vec2 sparkCoord = fragCoord - vec2(2.0*offset.x,190.0*realTime);\r\n"
    "    sparkCoord -= 30.0*noiseStackUV(0.01*vec3(sparkCoord,30.0*time),1,0.4,0.1);\r\n"
    "    sparkCoord += 100.0*flow.xy;\r\n"
    "    if (mod(sparkCoord.y/sparkGridSize,2.0)<1.0) sparkCoord.x += 0.5*sparkGridSize;\r\n"
    "    vec2 sparkGridIndex = vec2(floor(sparkCoord/sparkGridSize));\r\n"
    "    float sparkRandom = prng(sparkGridIndex);\r\n"
    "    float sparkLife = min(10.0*(1.0-min((sparkGridIndex.y+(190.0*realTime/sparkGridSize))/(24.0-20.0*sparkRandom),1.0)),1.0);\r\n"
    "    vec3 sparks = vec3(0.0);\r\n"
    "    if (sparkLife>0.0) {\r\n"
    "        float sparkSize = xfuel*xfuel*sparkRandom*0.08;\r\n"
    "        float sparkRadians = 999.0*sparkRandom*2.0*PI + 2.0*time;\r\n"
    "        vec2 sparkCircular = vec2(sin(sparkRadians),cos(sparkRadians));\r\n"
    "        vec2 sparkOffset = (0.5-sparkSize)*sparkGridSize*sparkCircular;\r\n"
    "        vec2 sparkModulus = mod(sparkCoord+sparkOffset,sparkGridSize) - 0.5*vec2(sparkGridSize);\r\n"
    "        float sparkLength = length(sparkModulus);\r\n"
    "        float sparksGray = max(0.0, 1.0 - sparkLength/(sparkSize*sparkGridSize));\r\n"
    "        sparks = sparkLife*sparksGray*vec3(1.0,0.3,0.0);\r\n"
    "    }\r\n"
    "    //\r\n"
    "    fragColor = vec4(max(fire,sparks)+smoke,1.0);\r\n"
    "}\r\n";

static const char *default_buffer_shader =
    "void mainImage(out vec4 fragColor, in vec2 fragCoord)\r\n"
    "{\r\n"
    "    fragColor = vec4(0.0, 0.0, 0.0, 1.0);\r\n"
    "}\r\n";

#define PANEL_WIDTH   620
#define TOOLBAR_H      32
#define TAB_BAR_H      28
#define ERROR_H       100
#define PAD             4

static const char *panel_class = "ShaderToyX_EditorPanel";
static bool panel_class_registered = false;

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

static const char *tab_names[NUM_TABS] = { "Image", "Buf A", "Buf B", "Buf C", "Buf D" };

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
            /* Active tab gets a different look (we'll just change the text style) */
            InvalidateRect(e->tab_btns[i], NULL, TRUE);
        }
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
                SetWindowTextA(e->pause_btn, e->paused ? ">" : "||");
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
                    /* Close region: rightmost 20px of the tab */
                    if (pt.x >= rc.right - 20)
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
        /* Dark background for buttons too */
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
                accent.top = accent.bottom - 2;
                HBRUSH line = CreateSolidBrush(RGB(97, 175, 239));
                FillRect(dis->hDC, &accent, line);
                DeleteObject(line);
            }

            /* Text — offset left to make room for close button on buffer tabs */
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, active ? RGB(255, 255, 255) : RGB(160, 160, 160));
            SelectObject(dis->hDC, e->tab_font);
            RECT textRect = dis->rcItem;
            if (tab != TAB_IMAGE) textRect.right -= 16; /* leave room for X */
            DrawTextA(dis->hDC, tab_names[tab], -1, &textRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* Draw close "x" on buffer tabs (not Image) */
            if (tab != TAB_IMAGE)
            {
                RECT xRect = dis->rcItem;
                xRect.left = xRect.right - 18;
                xRect.top += 2;
                xRect.bottom -= 2;
                SetTextColor(dis->hDC, active ? RGB(180, 180, 180) : RGB(120, 120, 120));
                DrawTextA(dis->hDC, "x", -1, &xRect,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

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
            SelectObject(dis->hDC, e->tab_font);
            DrawTextA(dis->hDC, "+", -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }

        /* ---- Toolbar icon buttons ---- */
        if (e && (dis->CtlID == IDC_PAUSE_BTN   || dis->CtlID == IDC_RESET_BTN ||
                  dis->CtlID == IDC_REC_BTN      || dis->CtlID == IDC_SPEAKER_BTN ||
                  dis->CtlID == IDC_FULLSCR_BTN))
        {
            RECT rc = dis->rcItem;
            int cx = (rc.left + rc.right) / 2;
            int cy = (rc.top + rc.bottom) / 2;
            int w  = rc.right - rc.left;
            int h  = rc.bottom - rc.top;

            /* Dark background */
            HBRUSH bg = CreateSolidBrush(RGB(45, 45, 45));
            FillRect(dis->hDC, &rc, bg);
            DeleteObject(bg);

            /* Icon color */
            COLORREF iconClr = RGB(180, 180, 180);

            /* Pause / Play button */
            if (dis->CtlID == IDC_PAUSE_BTN)
            {
                if (e->paused)
                {
                    /* Play triangle: right-pointing */
                    POINT tri[3];
                    tri[0] = { cx - 4, cy - 6 };
                    tri[1] = { cx - 4, cy + 6 };
                    tri[2] = { cx + 5, cy };
                    HBRUSH fb = CreateSolidBrush(iconClr);
                    HPEN np = CreatePen(PS_SOLID, 1, iconClr);
                    SelectObject(dis->hDC, fb);
                    SelectObject(dis->hDC, np);
                    Polygon(dis->hDC, tri, 3);
                    DeleteObject(fb);
                    DeleteObject(np);
                }
                else
                {
                    /* Pause: two vertical bars */
                    HBRUSH fb = CreateSolidBrush(iconClr);
                    RECT bar1 = { cx - 5, cy - 5, cx - 2, cy + 5 };
                    RECT bar2 = { cx + 2, cy - 5, cx + 5, cy + 5 };
                    FillRect(dis->hDC, &bar1, fb);
                    FillRect(dis->hDC, &bar2, fb);
                    DeleteObject(fb);
                }
            }

            /* Reset / Rewind button: |<< skip-back icon */
            if (dis->CtlID == IDC_RESET_BTN)
            {
                HBRUSH fb = CreateSolidBrush(iconClr);
                HPEN np = CreatePen(PS_SOLID, 1, iconClr);
                SelectObject(dis->hDC, fb);
                SelectObject(dis->hDC, np);

                /* Left bar */
                RECT bar = { cx - 6, cy - 5, cx - 4, cy + 5 };
                FillRect(dis->hDC, &bar, fb);

                /* Left-pointing triangle */
                POINT tri[3];
                tri[0] = { cx + 5, cy - 6 };
                tri[1] = { cx + 5, cy + 6 };
                tri[2] = { cx - 3, cy };
                Polygon(dis->hDC, tri, 3);

                DeleteObject(fb);
                DeleteObject(np);
            }

            /* Record button: filled red circle */
            if (dis->CtlID == IDC_REC_BTN)
            {
                HBRUSH rb = CreateSolidBrush(RGB(200, 60, 60));
                HPEN rp = CreatePen(PS_SOLID, 1, RGB(200, 60, 60));
                SelectObject(dis->hDC, rb);
                SelectObject(dis->hDC, rp);
                Ellipse(dis->hDC, cx - 5, cy - 5, cx + 5, cy + 5);
                DeleteObject(rb);
                DeleteObject(rp);
            }

            /* Speaker button: speaker cone + sound waves */
            if (dis->CtlID == IDC_SPEAKER_BTN)
            {
                HPEN ip = CreatePen(PS_SOLID, 1, iconClr);
                HBRUSH ib = CreateSolidBrush(iconClr);
                SelectObject(dis->hDC, ip);
                SelectObject(dis->hDC, ib);

                /* Speaker body (small rectangle) */
                RECT body = { cx - 7, cy - 3, cx - 3, cy + 3 };
                FillRect(dis->hDC, &body, ib);

                /* Speaker cone (triangle) */
                POINT cone[3];
                cone[0] = { cx - 3, cy - 3 };
                cone[1] = { cx - 3, cy + 3 };
                cone[2] = { cx + 1, cy };
                Polygon(dis->hDC, cone, 3);

                /* Sound wave arcs */
                HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH);
                SelectObject(dis->hDC, nb);
                Arc(dis->hDC, cx + 1, cy - 4, cx + 7, cy + 4, cx + 1, cy - 4, cx + 1, cy + 4);
                Arc(dis->hDC, cx + 3, cy - 7, cx + 11, cy + 7, cx + 3, cy - 7, cx + 3, cy + 7);

                DeleteObject(ip);
                DeleteObject(ib);
            }

            /* Fullscreen button: four corner brackets */
            if (dis->CtlID == IDC_FULLSCR_BTN)
            {
                HPEN ip = CreatePen(PS_SOLID, 2, iconClr);
                SelectObject(dis->hDC, ip);

                int m = 5; /* margin from center */
                int s = 4; /* stroke length */

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

                DeleteObject(ip);
            }

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
void editor_init(Editor *e, HWND parent, HINSTANCE hInstance)
{
    memset(e, 0, sizeof(Editor));

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

    /* Register panel window class once */
    if (!panel_class_registered)
    {
        WNDCLASSA wc = {};
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = panel_proc;
        wc.hInstance      = hInstance;
        wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName  = panel_class;
        RegisterClassA(&wc);
        panel_class_registered = true;
    }

    /* Panel (child of the main window, sits on the left) */
    RECT prc;
    GetClientRect(parent, &prc);
    int panel_h = prc.bottom;

    e->panel = CreateWindowExA(
        0, panel_class, NULL,
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, PANEL_WIDTH, panel_h,
        parent, NULL, hInstance, NULL
    );
    SetWindowLongPtrA(e->panel, GWLP_USERDATA, (LONG_PTR)e);

    /* Monospace font */
    e->mono_font = CreateFontA(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas"
    );

    /* Tab font (smaller) */
    e->tab_font = CreateFontA(
        14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );

    /* Compile button */
    e->compile_btn = CreateWindowExA(
        0, "BUTTON", "Compile (F5)",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        PAD, PAD, 100, TOOLBAR_H - PAD * 2,
        e->panel, (HMENU)IDC_COMPILE_BTN, hInstance, NULL
    );

    /* Pause button */
    e->pause_btn = CreateWindowExA(
        0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        PAD + 104, PAD, 28, TOOLBAR_H - PAD * 2,
        e->panel, (HMENU)IDC_PAUSE_BTN, hInstance, NULL
    );

    /* Reset timer button */
    e->reset_btn = CreateWindowExA(
        0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        PAD + 136, PAD, 28, TOOLBAR_H - PAD * 2,
        e->panel, (HMENU)IDC_RESET_BTN, hInstance, NULL
    );

    /* FPS label */
    e->fps_label = CreateWindowExA(
        0, "STATIC", "",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        PAD + 170, PAD + 4, 250, TOOLBAR_H - PAD * 2,
        e->panel, (HMENU)IDC_FPS_LABEL, hInstance, NULL
    );

    /* Right-justified toolbar buttons: Rec, Speaker, Fullscreen */
    int btn_w = 28;
    int btn_h = TOOLBAR_H - PAD * 2;
    int rx = PANEL_WIDTH - PAD;

    rx -= btn_w;
    e->fullscr_btn = CreateWindowExA(
        0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        rx, PAD, btn_w, btn_h,
        e->panel, (HMENU)IDC_FULLSCR_BTN, hInstance, NULL
    );

    rx -= btn_w + 2;
    e->speaker_btn = CreateWindowExA(
        0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        rx, PAD, btn_w, btn_h,
        e->panel, (HMENU)IDC_SPEAKER_BTN, hInstance, NULL
    );

    rx -= btn_w + 2;
    e->rec_btn = CreateWindowExA(
        0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        rx, PAD, btn_w, btn_h,
        e->panel, (HMENU)IDC_REC_BTN, hInstance, NULL
    );

    /* Tooltips for toolbar buttons */
    add_tooltip(e->panel, e->pause_btn,   hInstance, "Pause / Resume");
    add_tooltip(e->panel, e->reset_btn,   hInstance, "Reset Time");
    add_tooltip(e->panel, e->rec_btn,     hInstance, "Record");
    add_tooltip(e->panel, e->speaker_btn, hInstance, "Sound On/Off");
    add_tooltip(e->panel, e->fullscr_btn, hInstance, "Fullscreen");

    /* Tab buttons (owner-draw for custom look) — only create visible ones */
    int tab_x = PAD;
    int tab_w = 80;
    for (int i = 0; i < NUM_TABS; i++)
    {
        DWORD style = WS_CHILD | BS_OWNERDRAW;
        if (e->tab_visible[i]) style |= WS_VISIBLE;

        e->tab_btns[i] = CreateWindowExA(
            0, "BUTTON", tab_names[i],
            style,
            tab_x, TOOLBAR_H,
            tab_w, TAB_BAR_H,
            e->panel, (HMENU)(UINT_PTR)(IDC_TAB_IMAGE + i), hInstance, NULL
        );
        if (e->tab_visible[i]) tab_x += tab_w;
    }

    /* "+" button to add buffer tabs */
    e->add_tab_btn = CreateWindowExA(
        0, "BUTTON", "+",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        tab_x, TOOLBAR_H,
        TAB_BAR_H, TAB_BAR_H,
        e->panel, (HMENU)(UINT_PTR)IDC_ADD_TAB_BTN, hInstance, NULL
    );

    /* Code editor (multiline EDIT) */
    int code_top = TOOLBAR_H + TAB_BAR_H;
    e->code_edit = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", e->code[TAB_IMAGE],
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
        PAD, code_top, PANEL_WIDTH - PAD * 2,
        panel_h - code_top - ERROR_H - PAD,
        e->panel, (HMENU)IDC_CODE_EDIT, hInstance, NULL
    );
    {
        int tab_stop = 16;
        SendMessageA(e->code_edit, EM_SETTABSTOPS, 1, (LPARAM)&tab_stop);
    }

    /* Error log (read-only EDIT) */
    e->error_edit = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        PAD, panel_h - ERROR_H, PANEL_WIDTH - PAD * 2, ERROR_H - PAD,
        e->panel, (HMENU)IDC_ERROR_EDIT, hInstance, NULL
    );

    /* Apply monospace font to code and error areas */
    SendMessageA(e->code_edit, WM_SETFONT, (WPARAM)e->mono_font, TRUE);
    SendMessageA(e->error_edit, WM_SETFONT, (WPARAM)e->mono_font, TRUE);

    /* Set text limit on the code editor */
    SendMessageA(e->code_edit, EM_SETLIMITTEXT, EDITOR_CODE_SIZE - 1, 0);

    /* Subclass edit controls so Ctrl+A works */
    g_orig_edit_proc = (WNDPROC)SetWindowLongPtrA(e->code_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);
    SetWindowLongPtrA(e->error_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);
}

/* ------------------------------------------------------------------ */
void editor_destroy(Editor *e)
{
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
    if (e->panel)
    {
        DestroyWindow(e->panel);
        e->panel = NULL;
    }
}

/* ------------------------------------------------------------------ */
void editor_toggle(Editor *e)
{
    e->show_editor = !e->show_editor;
    ShowWindow(e->panel, e->show_editor ? SW_SHOW : SW_HIDE);
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

    MoveWindow(e->panel, 0, 0, PANEL_WIDTH, panel_h, TRUE);

    /* Tab buttons — only layout visible ones, then the "+" button */
    int tab_w = 80;
    int tab_x = PAD;
    for (int i = 0; i < NUM_TABS; i++)
    {
        if (e->tab_visible[i])
        {
            MoveWindow(e->tab_btns[i], tab_x, TOOLBAR_H, tab_w, TAB_BAR_H, TRUE);
            ShowWindow(e->tab_btns[i], SW_SHOW);
            tab_x += tab_w;
        }
        else
        {
            ShowWindow(e->tab_btns[i], SW_HIDE);
        }
    }

    /* Show "+" button only if there are still hidden tabs */
    bool all_visible = true;
    for (int i = 0; i < NUM_TABS; i++)
    {
        if (!e->tab_visible[i]) { all_visible = false; break; }
    }
    if (!all_visible)
    {
        MoveWindow(e->add_tab_btn, tab_x, TOOLBAR_H, TAB_BAR_H, TAB_BAR_H, TRUE);
        ShowWindow(e->add_tab_btn, SW_SHOW);
    }
    else
    {
        ShowWindow(e->add_tab_btn, SW_HIDE);
    }

    /* Code editor */
    int code_top = TOOLBAR_H + TAB_BAR_H;
    int code_h = panel_h - code_top - ERROR_H - PAD;
    if (code_h < 50) code_h = 50;

    MoveWindow(e->code_edit, PAD, code_top,
               PANEL_WIDTH - PAD * 2, code_h, TRUE);

    MoveWindow(e->error_edit, PAD, panel_h - ERROR_H,
               PANEL_WIDTH - PAD * 2, ERROR_H - PAD, TRUE);
}

/* ------------------------------------------------------------------ */
void editor_update(Editor *e, float elapsed_time, float fps, int canvas_w, int canvas_h)
{
    /* Update FPS label with dimensions */
    char buf[96];
    snprintf(buf, sizeof(buf), "%.0f FPS | %.1fs | %dx%d", fps, elapsed_time, canvas_w, canvas_h);
    SetWindowTextA(e->fps_label, buf);

    /* Show error or success for the active tab */
    int t = e->active_tab;
    if (e->error_log[t][0])
    {
        SetWindowTextA(e->error_edit, e->error_log[t]);
    }
    else
    {
        SetWindowTextA(e->error_edit, "OK - compile successful");
    }
}

/* ------------------------------------------------------------------ */
void editor_sync_from_control(Editor *e)
{
    GetWindowTextA(e->code_edit, e->code[e->active_tab], EDITOR_CODE_SIZE);
}

/* ------------------------------------------------------------------ */
void editor_switch_tab(Editor *e, int tab)
{
    if (tab < 0 || tab >= NUM_TABS || tab == e->active_tab)
    {
        return;
    }

    /* Save current tab's text */
    editor_sync_from_control(e);

    /* Switch */
    e->active_tab = tab;

    /* Load new tab's text into the EDIT control */
    SetWindowTextA(e->code_edit, e->code[tab]);

    /* Update tab visuals */
    update_tab_visuals(e);

    /* Update error display for new tab */
    if (e->error_log[tab][0])
    {
        SetWindowTextA(e->error_edit, e->error_log[tab]);
    }
    else
    {
        SetWindowTextA(e->error_edit, "OK - compile successful");
    }
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
        /* Find the nearest visible tab (prefer left, fallback right, then Image) */
        int new_tab = TAB_IMAGE;
        for (int i = tab - 1; i >= 0; i--)
        {
            if (e->tab_visible[i]) { new_tab = i; break; }
        }
        e->active_tab = -1; /* force switch */
        editor_switch_tab(e, new_tab);
    }

    /* Trigger recompile so the removed buffer's program is cleared */
    e->needs_compile = true;

    editor_layout(e);
}