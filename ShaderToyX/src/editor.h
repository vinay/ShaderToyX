#ifndef EDITOR_H
#define EDITOR_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define EDITOR_CODE_SIZE       (64 * 1024)
#define EDITOR_ERROR_LOG_SIZE  4096

#define NUM_TABS  5  /* Image, Buf A, Buf B, Buf C, Buf D */

#define TAB_IMAGE   0
#define TAB_BUF_A   1
#define TAB_BUF_B   2
#define TAB_BUF_C   3
#define TAB_BUF_D   4

/* Control IDs */
#define IDC_CODE_EDIT    1001
#define IDC_COMPILE_BTN  1002
#define IDC_STATUS_LABEL 1003
#define IDC_FPS_LABEL    1004
#define IDC_ERROR_EDIT   1005
#define IDC_TAB_IMAGE    1010
#define IDC_TAB_BUF_A    1011
#define IDC_TAB_BUF_B    1012
#define IDC_TAB_BUF_C    1013
#define IDC_TAB_BUF_D    1014
#define IDC_ADD_TAB_BTN  1015
#define IDC_PAUSE_BTN    1016
#define IDC_RESET_BTN    1017
#define IDC_REC_BTN      1018
#define IDC_SPEAKER_BTN  1019
#define IDC_FULLSCR_BTN  1020

typedef struct Editor
{
    /* Per-tab code buffers and error logs */
    char code[NUM_TABS][EDITOR_CODE_SIZE];
    char error_log[NUM_TABS][EDITOR_ERROR_LOG_SIZE];
    bool tab_visible[NUM_TABS]; /* which tabs are shown */
    bool needs_compile;      /* set true to recompile ALL tabs */
    bool show_editor;
    bool paused;             /* true = iTime frozen */
    bool reset_time;         /* set true to reset iTime to 0 */
    int  active_tab;         /* 0=Image, 1=BufA, 2=BufB, 3=BufC, 4=BufD */

    /* Win32 handles */
    HWND  panel;
    HWND  code_edit;
    HWND  compile_btn;
    HWND  pause_btn;
    HWND  reset_btn;
    HWND  fps_label;
    HWND  rec_btn;
    HWND  speaker_btn;
    HWND  fullscr_btn;
    HWND  error_edit;
    HWND  tab_btns[NUM_TABS];
    HWND  add_tab_btn;
    HFONT mono_font;
    HFONT tab_font;
} Editor;

void editor_init(Editor *e, HWND parent, HINSTANCE hInstance);
void editor_destroy(Editor *e);
void editor_toggle(Editor *e);
void editor_layout(Editor *e);
void editor_update(Editor *e, float elapsed_time, float fps, int canvas_w, int canvas_h);
void editor_sync_from_control(Editor *e);  /* save active tab's EDIT to code[] */
void editor_switch_tab(Editor *e, int tab); /* switch to the given tab */
void editor_add_tab(Editor *e);              /* show the next hidden buffer tab */
void editor_remove_tab(Editor *e, int tab);  /* hide a buffer tab */
bool editor_is_tab_visible(Editor *e, int tab);

#endif
