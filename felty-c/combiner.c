#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

BOOL RunFromMemory(unsigned char *data, unsigned int len, const wchar_t *cmdLine)
{
    wchar_t tempPath[MAX_PATH];
    wchar_t tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"FEL", 0, tempFile);

    HANDLE hFile = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written;
    WriteFile(hFile, data, len, &written, NULL);
    CloseHandle(hFile);

    wchar_t cmd[512];
    wsprintfW(cmd, L"\"%s\" %s", tempFile, cmdLine ? cmdLine : L"");

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL success = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (success) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    DeleteFileW(tempFile);
    return success;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc < 2) {
        MessageBoxW(NULL, L"Usage:\r\n  combiner.exe binder\r\n  combiner.exe loader\r\n  combiner.exe decrypt",
                    L"FELTY Launcher", MB_ICONINFORMATION);
        return 0;
    }
    const wchar_t *mode = argv[1];
    if (_wcsicmp(mode, L"binder") == 0)
        RunFromMemory(felty_binder_exe, felty_binder_exe_len, NULL);
    else if (_wcsicmp(mode, L"loader") == 0)
        RunFromMemory(felty_loader_exe, felty_loader_exe_len, NULL);
    else if (_wcsicmp(mode, L"decrypt") == 0)
        RunFromMemory(felty_decrypt_exe, felty_decrypt_exe_len, NULL);
    else
        MessageBoxW(NULL, L"Unknown mode. Use: binder, loader, decrypt", L"Error", MB_ICONERROR);
    LocalFree(argv);
    return 0;
}
}
