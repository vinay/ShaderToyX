/*
 * ShaderToyX - a native Win32/OpenGL ShaderToy-style shader playground.
 * Copyright (c) 2026 Vinay Menon
 * SPDX-License-Identifier: MIT
 */

#include "editor.h"
#include <stdio.h>
#include <string.h>
#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>

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

static const char *default_sound_shader =
    "//\r\n"
    "//  Sound shader: return the stereo sample (each in -1..1) for `time`.\r\n"
    "//  Press F5 to compile; the speaker button mutes/unmutes.\r\n"
    "//\r\n"
    "\r\n"
    "vec2 mainSound(int samp, float time)\r\n"
    "{\r\n"
    "    // A gentle A3 + C#4 dyad with a little vibrato\r\n"
    "    float v = 1.0 + 0.004 * sin(6.28318 * 5.0 * time);\r\n"
    "    float s = 0.30 * sin(6.28318 * 220.00 * time * v)\r\n"
    "            + 0.20 * sin(6.28318 * 277.18 * time);\r\n"
    "    return vec2(s);\r\n"
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

#define TIMER_RECOLOR   1   /* debounce timer for re-highlighting after edits */
#define RECOLOR_DELAY  150  /* ms of typing quiet before recoloring */

static const char *panel_class = "ShaderToyX_EditorPanel";
static bool panel_class_registered = false;
static HMODULE msftedit_dll = NULL;

static const char *tab_names[NUM_TABS] = { "Image", "Buf A", "Buf B", "Buf C", "Buf D", "Sound" };

/* Scale a 96-DPI pixel value to the editor's current DPI */
static int sc(const Editor *e, int v)
{
    return MulDiv(v, e->dpi, 96);
}

/* ================================================================== */
/*  GLSL syntax highlighting                                          */
/*                                                                    */
/*  A single-pass lexer assigns every character a color class; the    */
/*  classes are applied to the RichEdit control through TOM ranges    */
/*  (so the user's undo stack is untouched), diffed against the last  */
/*  applied classes so steady-state typing only recolors what changed.*/
/* ================================================================== */

enum
{
    HL_DEFAULT = 0,  /* identifiers, operators, punctuation */
    HL_COMMENT,
    HL_PREPROC,
    HL_KEYWORD,
    HL_TYPE,
    HL_BUILTIN,      /* built-in functions */
    HL_STVAR,        /* Shadertoy/GLSL built-in variables and entry points */
    HL_NUMBER,
    HL_CLASS_COUNT
};

static const COLORREF hl_colors[HL_CLASS_COUNT] =
{
    RGB(220, 220, 220),  /* HL_DEFAULT */
    RGB(106, 153,  85),  /* HL_COMMENT */
    RGB(197, 134, 192),  /* HL_PREPROC */
    RGB(198, 120, 221),  /* HL_KEYWORD */
    RGB( 97, 175, 239),  /* HL_TYPE (the tab-accent blue) */
    RGB( 86, 182, 194),  /* HL_BUILTIN */
    RGB(224, 108, 117),  /* HL_STVAR */
    RGB(209, 154, 102),  /* HL_NUMBER */
};

static const char *hl_keywords[] =
{
    "break", "case", "const", "continue", "default", "discard", "do", "else",
    "false", "flat", "for", "highp", "if", "in", "inout", "invariant",
    "layout", "lowp", "mediump", "out", "precision", "return", "smooth",
    "struct", "switch", "true", "uniform", "while",
};

static const char *hl_types[] =
{
    "void", "bool", "int", "uint", "float", "double",
    "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
    "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4",
    "mat2", "mat3", "mat4",
    "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4",
    "mat4x2", "mat4x3", "mat4x4",
    "sampler1D", "sampler2D", "sampler3D", "samplerCube",
    "sampler2DArray", "sampler2DShadow", "samplerCubeShadow",
    "isampler2D", "usampler2D",
};

static const char *hl_builtins[] =
{
    "abs", "acos", "acosh", "all", "any", "asin", "asinh", "atan", "atanh",
    "ceil", "clamp", "cos", "cosh", "cross", "degrees", "determinant",
    "dFdx", "dFdy", "distance", "dot", "equal", "exp", "exp2", "faceforward",
    "floor", "fract", "fwidth", "greaterThan", "greaterThanEqual", "inverse",
    "inversesqrt", "isinf", "isnan", "length", "lessThan", "lessThanEqual",
    "log", "log2", "matrixCompMult", "max", "min", "mix", "mod", "modf",
    "normalize", "not", "notEqual", "outerProduct", "pow", "radians",
    "reflect", "refract", "round", "roundEven", "sign", "sin", "sinh",
    "smoothstep", "sqrt", "step", "tan", "tanh", "texelFetch", "texture",
    "textureGrad", "textureLod", "textureProj", "textureSize", "transpose",
    "trunc",
};

static const char *hl_stvars[] =
{
    "iResolution", "iTime", "iTimeDelta", "iFrame", "iMouse", "iDate",
    "iSampleRate", "iChannelTime", "iChannelResolution",
    "iChannel0", "iChannel1", "iChannel2", "iChannel3",
    "mainImage", "mainSound", "gl_FragCoord", "gl_FragDepth",
};

static bool hl_in_list(const char *w, int len, const char **list, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (strncmp(list[i], w, len) == 0 && list[i][len] == '\0')
        {
            return true;
        }
    }
    return false;
}

static unsigned char hl_classify(const char *w, int len)
{
    #define HL_LIST(t) t, (int)(sizeof(t) / sizeof((t)[0]))
    if (hl_in_list(w, len, HL_LIST(hl_keywords))) return HL_KEYWORD;
    if (hl_in_list(w, len, HL_LIST(hl_types)))    return HL_TYPE;
    if (hl_in_list(w, len, HL_LIST(hl_builtins))) return HL_BUILTIN;
    if (hl_in_list(w, len, HL_LIST(hl_stvars)))   return HL_STVAR;
    #undef HL_LIST
    return HL_DEFAULT;
}

static bool hl_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool hl_ident_char(char c)
{
    return hl_ident_start(c) || (c >= '0' && c <= '9');
}

static bool hl_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* Assign a color class to every character of s[0..n) */
static void hl_lex(const char *s, int n, unsigned char *cls)
{
    int  i = 0;
    bool line_start = true;  /* only whitespace seen since the last newline */

    while (i < n)
    {
        char c = s[i];

        if (c == '\r' || c == '\n')
        {
            cls[i++] = HL_DEFAULT;
            line_start = true;
            continue;
        }
        if (c == ' ' || c == '\t')
        {
            cls[i++] = HL_DEFAULT;
            continue;
        }

        /* Line comment */
        if (c == '/' && i + 1 < n && s[i + 1] == '/')
        {
            while (i < n && s[i] != '\r' && s[i] != '\n')
            {
                cls[i++] = HL_COMMENT;
            }
            continue;
        }

        /* Block comment */
        if (c == '/' && i + 1 < n && s[i + 1] == '*')
        {
            cls[i++] = HL_COMMENT;
            cls[i++] = HL_COMMENT;
            while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/'))
            {
                cls[i++] = HL_COMMENT;
            }
            if (i < n)
            {
                cls[i++] = HL_COMMENT;
                cls[i++] = HL_COMMENT;
            }
            line_start = false;
            continue;
        }

        /* Preprocessor line (comments on such lines are not sub-lexed) */
        if (c == '#' && line_start)
        {
            while (i < n && s[i] != '\r' && s[i] != '\n')
            {
                cls[i++] = HL_PREPROC;
            }
            continue;
        }

        /* Number: ints, floats, hex, exponents, suffixes */
        if (hl_digit(c) || (c == '.' && i + 1 < n && hl_digit(s[i + 1])))
        {
            while (i < n)
            {
                char d = s[i];
                if (hl_ident_char(d) || d == '.')
                {
                    cls[i++] = HL_NUMBER;
                    continue;
                }
                if ((d == '+' || d == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E'))
                {
                    cls[i++] = HL_NUMBER;
                    continue;
                }
                break;
            }
            line_start = false;
            continue;
        }

        /* Identifier / keyword */
        if (hl_ident_start(c))
        {
            int start = i;
            while (i < n && hl_ident_char(s[i]))
            {
                i++;
            }
            unsigned char k = hl_classify(s + start, i - start);
            for (int j = start; j < i; j++)
            {
                cls[j] = k;
            }
            line_start = false;
            continue;
        }

        cls[i++] = HL_DEFAULT;
        line_start = false;
    }
}

/* ------------------------------------------------------------------ */
/*  Fetch the code text with RichEdit-native line ends (a lone CR per */
/*  paragraph), so byte indices match the control's character indices */
/*  for TOM ranges and EM_EXGETSEL positions.                         */
/* ------------------------------------------------------------------ */
static int fetch_code_text(Editor *e, char *buf, int size)
{
    GETTEXTEX gt = {};
    gt.cb       = (DWORD)size;
    gt.flags    = GT_DEFAULT;
    gt.codepage = CP_ACP;
    int len = (int)SendMessageA(e->code_edit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)buf);
    if (len < 0)
    {
        len = 0;
    }
    buf[len] = '\0';
    return len;
}

/* ------------------------------------------------------------------ */
/*  Re-lex the buffer and apply color changes to the control.         */
/*  Runs with undo suspended and the display frozen; only ranges      */
/*  whose class differs from the last applied pass are touched.       */
/* ------------------------------------------------------------------ */
static void editor_recolor(Editor *e)
{
    ITextDocument *doc = (ITextDocument *)e->tom_doc;
    if (!doc)
    {
        return;
    }

    static char          text[EDITOR_CODE_SIZE];
    static unsigned char cls[EDITOR_CODE_SIZE];
    int len = fetch_code_text(e, text, sizeof(text));
    hl_lex(text, len, cls);

    /* The cache is indexed by character position, but edits shift text
       around while its colors travel along with the characters. Map the
       cache through the edit: the unchanged prefix and suffix keep their
       cached classes (the suffix shifted into place), and the edited
       middle — whose characters inherited whatever color sat at the
       insertion point — is marked unknown so it is always repainted. */
    #define HL_UNKNOWN 0xFF
    if (e->hl_valid)
    {
        int old_len = e->hl_len;
        int max_common = (old_len < len) ? old_len : len;

        int prefix = 0;
        while (prefix < max_common && text[prefix] == e->hl_text[prefix])
        {
            prefix++;
        }

        int suffix = 0;
        while (suffix < max_common - prefix &&
               text[len - 1 - suffix] == e->hl_text[old_len - 1 - suffix])
        {
            suffix++;
        }

        memmove(&e->hl_class[len - suffix], &e->hl_class[old_len - suffix],
                (size_t)suffix);
        memset(&e->hl_class[prefix], HL_UNKNOWN, (size_t)(len - suffix - prefix));
    }

    e->hl_busy = true;
    long freeze_count = 0;
    doc->Undo(tomSuspend, NULL);
    doc->Freeze(&freeze_count);

    int i = 0;
    while (i < len)
    {
        if (e->hl_valid && e->hl_class[i] == cls[i])
        {
            i++;
            continue;
        }

        int start = i;
        unsigned char k = cls[i];
        while (i < len && cls[i] == k && !(e->hl_valid && e->hl_class[i] == k))
        {
            i++;
        }

        ITextRange *range = NULL;
        if (SUCCEEDED(doc->Range(start, i, &range)) && range)
        {
            ITextFont *font = NULL;
            if (SUCCEEDED(range->GetFont(&font)) && font)
            {
                font->SetForeColor((long)hl_colors[k]);
                font->Release();
            }
            range->Release();
        }
    }

    memcpy(e->hl_class, cls, (size_t)len);
    memcpy(e->hl_text, text, (size_t)len + 1);
    e->hl_len   = len;
    e->hl_valid = true;

    doc->Unfreeze(&freeze_count);
    doc->Undo(tomResume, NULL);
    e->hl_busy = false;
}

/* ------------------------------------------------------------------ */
/*  Tab stops every 4 columns of the mono font. RichEdit ignores      */
/*  EM_SETTABSTOPS, so this goes through PARAFORMAT2 (in twips).      */
/* ------------------------------------------------------------------ */
static void apply_code_tab_stops(Editor *e)
{
    if (!e->code_rich)
    {
        return; /* the EDIT fallback got EM_SETTABSTOPS at creation */
    }

    HDC dc = GetDC(e->code_edit);
    HGDIOBJ old_font = SelectObject(dc, e->mono_font);
    TEXTMETRICA tm = {};
    GetTextMetricsA(dc, &tm);
    SelectObject(dc, old_font);
    ReleaseDC(e->code_edit, dc);

    LONG tab_twips = MulDiv(tm.tmAveCharWidth * 4, 1440, e->dpi);
    if (tab_twips < 1)
    {
        tab_twips = 720;
    }

    PARAFORMAT2 pf = {};
    pf.cbSize    = sizeof(pf);
    pf.dwMask    = PFM_TABSTOPS;
    pf.cTabCount = MAX_TAB_STOPS;
    for (int i = 0; i < MAX_TAB_STOPS; i++)
    {
        pf.rgxTabs[i] = (i + 1) * tab_twips;
    }

    ITextDocument *doc = (ITextDocument *)e->tom_doc;
    long freeze_count = 0;
    if (doc)
    {
        doc->Undo(tomSuspend, NULL);
        doc->Freeze(&freeze_count);
    }

    CHARRANGE save;
    SendMessageA(e->code_edit, EM_EXGETSEL, 0, (LPARAM)&save);
    SendMessageA(e->code_edit, EM_SETSEL, 0, -1);
    SendMessageA(e->code_edit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
    SendMessageA(e->code_edit, EM_EXSETSEL, 0, (LPARAM)&save);

    if (doc)
    {
        doc->Unfreeze(&freeze_count);
        doc->Undo(tomResume, NULL);
    }
}

/* ------------------------------------------------------------------ */
/*  Replace the control's text (tab switch / init) and re-highlight   */
/* ------------------------------------------------------------------ */
static void set_code_text(Editor *e, const char *text)
{
    SetWindowTextA(e->code_edit, text);
    e->hl_valid = false;
    apply_code_tab_stops(e);
    editor_recolor(e);
}

/* ------------------------------------------------------------------ */
/*  Auto-indent: Enter copies the current line's leading whitespace,  */
/*  plus one tab if the line so far ends with an open brace.          */
/* ------------------------------------------------------------------ */
static void code_auto_indent(Editor *e)
{
    static char text[EDITOR_CODE_SIZE];
    int len = fetch_code_text(e, text, sizeof(text));

    CHARRANGE cr;
    SendMessageA(e->code_edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    int pos = cr.cpMin;
    if (pos > len)
    {
        pos = len;
    }

    int line_start = pos;
    while (line_start > 0 &&
           text[line_start - 1] != '\r' && text[line_start - 1] != '\n')
    {
        line_start--;
    }

    char ins[256];
    int  k = 0;
    ins[k++] = '\r';
    for (int i = line_start;
         i < pos && k < (int)sizeof(ins) - 2 &&
         (text[i] == ' ' || text[i] == '\t');
         i++)
    {
        ins[k++] = text[i];
    }

    char last = 0;
    for (int i = line_start; i < pos; i++)
    {
        if (text[i] != ' ' && text[i] != '\t')
        {
            last = text[i];
        }
    }
    if (last == '{')
    {
        ins[k++] = '\t';
    }
    ins[k] = '\0';

    SendMessageA(e->code_edit, EM_REPLACESEL, TRUE, (LPARAM)ins);
}

/* ------------------------------------------------------------------ */
/*  Format document: a re-indenter. Only each line's leading          */
/*  whitespace is rewritten (tabs, from brace depth); the text itself */
/*  is never altered, so it cannot mangle code. Lines inside block    */
/*  comments are left untouched; preprocessor lines go to column 0;   */
/*  lines inside unclosed parens get one extra level.                 */
/* ------------------------------------------------------------------ */
static bool format_glsl(const char *src, int len, char *dst, int dst_size)
{
    int  di = 0, i = 0;
    int  depth = 0, paren = 0;
    bool in_block = false;

    while (i < len)
    {
        int line_end = i;
        while (line_end < len && src[line_end] != '\r' && src[line_end] != '\n')
        {
            line_end++;
        }

        int cs = i; /* content start */
        while (cs < line_end && (src[cs] == ' ' || src[cs] == '\t'))
        {
            cs++;
        }
        int ce = line_end; /* content end, trailing whitespace trimmed */
        while (ce > cs && (src[ce - 1] == ' ' || src[ce - 1] == '\t'))
        {
            ce--;
        }

        bool was_in_block = in_block;
        if (was_in_block)
        {
            /* Keep block-comment interiors exactly as written */
            for (int j = i; j < line_end && di < dst_size - 1; j++)
            {
                dst[di++] = src[j];
            }
        }
        else if (cs < ce)
        {
            int ind;
            if (src[cs] == '#')
            {
                ind = 0;
            }
            else
            {
                ind = depth + (paren > 0 ? 1 : 0);
                for (int j = cs; j < ce && src[j] == '}'; j++)
                {
                    ind--;
                }
                if (ind < 0)
                {
                    ind = 0;
                }
            }
            for (int t = 0; t < ind && di < dst_size - 1; t++)
            {
                dst[di++] = '\t';
            }
            for (int j = cs; j < ce && di < dst_size - 1; j++)
            {
                dst[di++] = src[j];
            }
        }
        /* blank line: emit nothing before the terminator */

        /* Update brace/paren/comment state from this line's code */
        {
            bool preproc = (!was_in_block && cs < ce && src[cs] == '#');
            int  j = was_in_block ? i : cs;
            while (j < line_end)
            {
                if (in_block)
                {
                    if (src[j] == '*' && j + 1 < line_end && src[j + 1] == '/')
                    {
                        in_block = false;
                        j += 2;
                        continue;
                    }
                    j++;
                    continue;
                }
                if (src[j] == '/' && j + 1 < line_end && src[j + 1] == '/')
                {
                    break;
                }
                if (src[j] == '/' && j + 1 < line_end && src[j + 1] == '*')
                {
                    in_block = true;
                    j += 2;
                    continue;
                }
                if (!preproc)
                {
                    if      (src[j] == '{') depth++;
                    else if (src[j] == '}') { if (depth > 0) depth--; }
                    else if (src[j] == '(') paren++;
                    else if (src[j] == ')') { if (paren > 0) paren--; }
                }
                j++;
            }
        }

        if (line_end < len)
        {
            if (di < dst_size - 1)
            {
                dst[di++] = '\r';
            }
            i = line_end + 1;
            if (src[line_end] == '\r' && i < len && src[i] == '\n')
            {
                i++;
            }
        }
        else
        {
            i = line_end;
        }

        if (di >= dst_size - 1)
        {
            return false;
        }
    }

    dst[di] = '\0';
    return true;
}

static void editor_format_document(Editor *e)
{
    static char src[EDITOR_CODE_SIZE];
    static char dst[EDITOR_CODE_SIZE];

    int len = fetch_code_text(e, src, sizeof(src));
    if (len <= 0)
    {
        return;
    }
    if (!format_glsl(src, len, dst, sizeof(dst)))
    {
        return;
    }
    if (strcmp(src, dst) == 0)
    {
        return;
    }

    /* Remember the caret's line so it can be restored afterwards */
    CHARRANGE cr;
    SendMessageA(e->code_edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    int line = (int)SendMessageA(e->code_edit, EM_EXLINEFROMCHAR, 0, cr.cpMin);

    /* Replace everything as one undoable action */
    SendMessageA(e->code_edit, EM_SETSEL, 0, -1);
    SendMessageA(e->code_edit, EM_REPLACESEL, TRUE, (LPARAM)dst);

    int line_count = (int)SendMessageA(e->code_edit, EM_GETLINECOUNT, 0, 0);
    if (line >= line_count)
    {
        line = line_count - 1;
    }
    int pos = (int)SendMessageA(e->code_edit, EM_LINEINDEX, line, 0);
    if (pos < 0)
    {
        pos = 0;
    }
    SendMessageA(e->code_edit, EM_SETSEL, pos, pos);
    SendMessageA(e->code_edit, EM_SCROLLCARET, 0, 0);

    e->hl_valid = false;
    editor_recolor(e);
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
/*  Subclass proc for the code and error controls: Ctrl+A everywhere; */
/*  on the RichEdit code control also auto-indent (Enter), format     */
/*  document (Ctrl+Shift+F) and plain-text paste.                     */
/* ------------------------------------------------------------------ */
static WNDPROC g_orig_code_proc  = NULL;
static WNDPROC g_orig_error_proc = NULL;

static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Editor *e = (Editor *)GetWindowLongPtrA(GetParent(hwnd), GWLP_USERDATA);
    bool is_code = e && hwnd == e->code_edit;
    bool ctrl    = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift   = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;

    if (msg == WM_KEYDOWN && wParam == 'A' && ctrl)
    {
        SendMessageA(hwnd, EM_SETSEL, 0, -1);
        return 0;
    }

    if (is_code && e->code_rich)
    {
        /* Paste as plain text so clipboard RTF cannot change formatting */
        if (msg == WM_PASTE ||
            (msg == WM_KEYDOWN && wParam == 'V' && ctrl) ||
            (msg == WM_KEYDOWN && wParam == VK_INSERT && shift))
        {
            SendMessageA(hwnd, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
            return 0;
        }

        if (msg == WM_KEYDOWN && wParam == 'F' && ctrl && shift)
        {
            editor_format_document(e);
            return 0;
        }

        if (msg == WM_CHAR && wParam == '\r')
        {
            code_auto_indent(e);
            return 0;
        }
    }

    WNDPROC orig = is_code ? g_orig_code_proc : g_orig_error_proc;
    return CallWindowProcA(orig, hwnd, msg, wParam, lParam);
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

        if (!e->sound_muted)
        {
            /* Sound wave arcs */
            SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            Arc(dis->hDC, cx + sc(e, 1), cy - sc(e, 4), cx + sc(e, 7), cy + sc(e, 4),
                cx + sc(e, 1), cy - sc(e, 4), cx + sc(e, 1), cy + sc(e, 4));
            Arc(dis->hDC, cx + sc(e, 3), cy - sc(e, 7), cx + sc(e, 11), cy + sc(e, 7),
                cx + sc(e, 3), cy - sc(e, 7), cx + sc(e, 3), cy + sc(e, 7));
        }
        else
        {
            /* Muted: an x where the waves would be */
            MoveToEx(dis->hDC, cx + sc(e, 3), cy - sc(e, 4), NULL);
            LineTo(dis->hDC, cx + sc(e, 10), cy + sc(e, 4));
            MoveToEx(dis->hDC, cx + sc(e, 10), cy - sc(e, 4), NULL);
            LineTo(dis->hDC, cx + sc(e, 3), cy + sc(e, 4));
        }

        SelectObject(dis->hDC, ob);
        SelectObject(dis->hDC, op);
        DeleteObject(ip);
        DeleteObject(ib);
    }

    /* Fullscreen button: four corner brackets. The bracket vertex sits at
       the outer corner normally ("enter fullscreen"), and at the inner
       corner while fullscreen ("exit fullscreen"). */
    if (dis->CtlID == IDC_FULLSCR_BTN)
    {
        HPEN ip = CreatePen(PS_SOLID, sc(e, 2), iconClr);
        HGDIOBJ op = SelectObject(dis->hDC, ip);

        int m = sc(e, 5); /* margin from center */
        int s = sc(e, 4); /* stroke length */

        static const int corner[4][2] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
        for (int i = 0; i < 4; i++)
        {
            int sx = corner[i][0];
            int sy = corner[i][1];
            int v  = e->fullscreen ? m - s : m; /* vertex distance from center */
            MoveToEx(dis->hDC, cx + sx * m, cy + sy * (m - s), NULL);
            LineTo(dis->hDC, cx + sx * v, cy + sy * v);
            LineTo(dis->hDC, cx + sx * (m - s), cy + sy * m);
        }

        SelectObject(dis->hDC, op);
        DeleteObject(ip);
    }
}

/* ------------------------------------------------------------------ */
/*  "+" button: pick which kind of tab to add from a popup menu       */
/* ------------------------------------------------------------------ */
static void show_add_tab_menu(Editor *e)
{
    enum { IDM_ADD_BUFFER = 1, IDM_ADD_SOUND = 2 };

    int next_buffer = -1;
    for (int i = TAB_BUF_A; i <= TAB_BUF_D; i++)
    {
        if (!e->tab_visible[i]) { next_buffer = i; break; }
    }

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }
    AppendMenuA(menu, MF_STRING | (next_buffer >= 0 ? 0 : MF_GRAYED),
                IDM_ADD_BUFFER, "Buffer");
    AppendMenuA(menu, MF_STRING | (e->tab_visible[TAB_SOUND] ? MF_GRAYED : 0),
                IDM_ADD_SOUND, "Sound");

    RECT rc;
    GetWindowRect(e->add_tab_btn, &rc);
    int cmd = (int)TrackPopupMenu(menu,
                                  TPM_RETURNCMD | TPM_NONOTIFY |
                                  TPM_LEFTALIGN | TPM_TOPALIGN,
                                  rc.left, rc.bottom, 0, e->panel, NULL);
    DestroyMenu(menu);

    if (cmd == IDM_ADD_BUFFER && next_buffer >= 0)
    {
        editor_add_tab(e, next_buffer);
    }
    else if (cmd == IDM_ADD_SOUND)
    {
        editor_add_tab(e, TAB_SOUND);
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

            /* Re-highlight shortly after the code text changes */
            if (id == IDC_CODE_EDIT && notify == EN_CHANGE)
            {
                if (!e->hl_busy)
                {
                    SetTimer(hwnd, TIMER_RECOLOR, RECOLOR_DELAY, NULL);
                }
                return 0;
            }

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

            /* Tab button clicks — check for close "x" on buffer/sound tabs */
            if (id >= IDC_TAB_IMAGE && id <= IDC_TAB_SOUND && notify == BN_CLICKED)
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

            /* "+" button offers the hidden tab kinds in a menu */
            if (id == IDC_ADD_TAB_BTN && notify == BN_CLICKED)
            {
                show_add_tab_menu(e);
                return 0;
            }

            if (id == IDC_FULLSCR_BTN && notify == BN_CLICKED)
            {
                e->toggle_fullscreen = true;
                return 0;
            }

            if (id == IDC_SPEAKER_BTN && notify == BN_CLICKED)
            {
                e->sound_muted = !e->sound_muted;
                InvalidateRect(e->speaker_btn, NULL, TRUE);
                return 0;
            }

            /* IDC_REC_BTN: not implemented yet */
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_RECOLOR)
        {
            KillTimer(hwnd, TIMER_RECOLOR);
            if (e)
            {
                editor_recolor(e);
            }
            return 0;
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
        if (e && dis->CtlID >= IDC_TAB_IMAGE && dis->CtlID <= IDC_TAB_SOUND)
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

    /* Initialize buffer tabs with empty shaders, Sound with its template */
    for (int i = TAB_BUF_A; i <= TAB_BUF_D; i++)
    {
        strncpy(e->code[i], default_buffer_shader, EDITOR_CODE_SIZE - 1);
    }
    strncpy(e->code[TAB_SOUND], default_sound_shader, EDITOR_CODE_SIZE - 1);

    e->needs_compile = false;
    e->show_editor   = true;
    e->paused        = false;
    e->reset_time    = false;
    e->toggle_fullscreen = false;
    e->fullscreen        = false;
    e->sound_muted       = false;
    e->active_tab    = TAB_IMAGE;

    /* Only Image tab visible by default */
    e->tab_visible[TAB_IMAGE] = true;
    for (int i = TAB_BUF_A; i < NUM_TABS; i++)
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
    add_tooltip(e->panel, e->speaker_btn, hInstance, "Sound On/Off");
    add_tooltip(e->panel, e->fullscr_btn, hInstance, "Fullscreen (F11, Esc to exit)");

    /* Tab buttons (owner-draw for custom look); visibility set in layout */
    for (int i = 0; i < NUM_TABS; i++)
    {
        e->tab_btns[i] = create_child(e->panel, hInstance, "BUTTON", tab_names[i],
                                      WS_CHILD | BS_OWNERDRAW, 0, IDC_TAB_IMAGE + i);
    }
    e->add_tab_btn = create_child(e->panel, hInstance, "BUTTON", "+", odbtn, 0, IDC_ADD_TAB_BTN);

    /* Code editor: a RichEdit for syntax colors, with a plain EDIT
       fallback if Msftedit.dll is somehow unavailable */
    const DWORD code_style =
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN;

    if (!msftedit_dll)
    {
        msftedit_dll = LoadLibraryA("Msftedit.dll");
    }
    e->code_edit = NULL;
    if (msftedit_dll)
    {
        e->code_edit = create_child(e->panel, hInstance, "RICHEDIT50W", "",
                                    code_style | ES_NOOLEDRAGDROP,
                                    WS_EX_CLIENTEDGE, IDC_CODE_EDIT);
    }
    e->code_rich = (e->code_edit != NULL);
    if (!e->code_edit)
    {
        e->code_edit = create_child(e->panel, hInstance, "EDIT", "",
                                    code_style, WS_EX_CLIENTEDGE, IDC_CODE_EDIT);
    }

    if (e->code_rich)
    {
        SendMessageA(e->code_edit, EM_SETEVENTMASK, 0, ENM_CHANGE);
        SendMessageA(e->code_edit, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(30, 30, 30));
        SendMessageA(e->code_edit, EM_EXLIMITTEXT, 0, EDITOR_CODE_SIZE - 1);

        /* Keep the IME from silently switching fonts */
        LRESULT lang = SendMessageA(e->code_edit, EM_GETLANGOPTIONS, 0, 0);
        SendMessageA(e->code_edit, EM_SETLANGOPTIONS, 0, lang & ~IMF_AUTOFONT);

        /* TOM interface: recoloring goes through it so highlighting can
           run with undo suspended and the display frozen */
        IUnknown *unk = NULL;
        SendMessageA(e->code_edit, EM_GETOLEINTERFACE, 0, (LPARAM)&unk);
        if (unk)
        {
            ITextDocument *doc = NULL;
            unk->QueryInterface(__uuidof(ITextDocument), (void **)&doc);
            e->tom_doc = doc;
            unk->Release();
        }
    }
    else
    {
        int tab_stop = 16; /* dialog units, so not DPI-dependent */
        SendMessageA(e->code_edit, EM_SETTABSTOPS, 1, (LPARAM)&tab_stop);
        SendMessageA(e->code_edit, EM_SETLIMITTEXT, EDITOR_CODE_SIZE - 1, 0);
    }

    /* Error log (read-only EDIT) */
    e->error_edit = create_child(e->panel, hInstance, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        WS_EX_CLIENTEDGE, IDC_ERROR_EDIT);

    /* Subclass both text controls (Ctrl+A, and the code editor's
       auto-indent / format / plain-paste handling) */
    g_orig_code_proc  = (WNDPROC)SetWindowLongPtrA(e->code_edit,  GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);
    g_orig_error_proc = (WNDPROC)SetWindowLongPtrA(e->error_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);

    create_fonts(e);

    if (e->code_rich)
    {
        /* Default text color for freshly typed characters (recolor
           passes then refine it) */
        CHARFORMAT2A cf = {};
        cf.cbSize      = sizeof(cf);
        cf.dwMask      = CFM_COLOR;
        cf.crTextColor = hl_colors[HL_DEFAULT];
        SendMessageA(e->code_edit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }

    set_code_text(e, e->code[TAB_IMAGE]);
    editor_layout(e);
    refresh_error_display(e);
}

/* ------------------------------------------------------------------ */
void editor_destroy(Editor *e)
{
    if (e->tom_doc)
    {
        ((ITextDocument *)e->tom_doc)->Release();
        e->tom_doc = NULL;
    }
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
void editor_set_fullscreen(Editor *e, bool fullscreen)
{
    if (e->fullscreen != fullscreen)
    {
        e->fullscreen = fullscreen;
        InvalidateRect(e->fullscr_btn, NULL, TRUE);
    }
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

    /* The font size changed: recompute tab stops and re-apply colors
       (WM_SETFONT reformats the whole document) */
    apply_code_tab_stops(e);
    e->hl_valid = false;
    editor_recolor(e);

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

    /* Load new tab's text into the code control and re-highlight */
    set_code_text(e, e->code[tab]);

    update_tab_visuals(e);
    refresh_error_display(e);
}

/* ------------------------------------------------------------------ */
void editor_add_tab(Editor *e, int tab)
{
    if (tab <= TAB_IMAGE || tab >= NUM_TABS || e->tab_visible[tab])
    {
        return;
    }
    e->tab_visible[tab] = true;
    editor_layout(e);
    editor_switch_tab(e, tab);
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

    /* Reset its shader code to the default template and clear the log */
    const char *def = (tab == TAB_SOUND) ? default_sound_shader : default_buffer_shader;
    strncpy(e->code[tab], def, EDITOR_CODE_SIZE - 1);
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
