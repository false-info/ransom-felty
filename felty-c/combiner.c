/*
 * combiner.c – Single EXE launcher for FELTY components.
 * Compile with: 
 *   x86_64-w64-mingw32-gcc -Os -s -o combiner.exe combiner.c -mwindows
 *   (the embedded headers provide the data)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* Include the generated headers */
#include "felty_loader_exe.h"
#include "felty_binder_exe.h"
#include "felty_decrypt_exe.h"

/* Run an executable from memory */
BOOL RunFromMemory(unsigned char *data, unsigned int len, const wchar_t *cmdLine)
{
    wchar_t tempPath[MAX_PATH];
    wchar_t tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"FEL", 0, tempFile);

    /* Write the binary to a temporary file */
    HANDLE hFile = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written;
    WriteFile(hFile, data, len, &written, NULL);
    CloseHandle(hFile);

    /* Build command line */
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

    /* Clean up temp file */
    DeleteFileW(tempFile);
    return success;
}

/* Show a simple message box if no argument is given */
void ShowUsage()
{
    MessageBoxW(NULL, L"Usage:\r\n  combiner.exe binder\r\n  combiner.exe loader\r\n  combiner.exe decrypt",
                L"FELTY Launcher", MB_ICONINFORMATION);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc < 2) {
        ShowUsage();
        return 0;
    }

    const wchar_t *mode = argv[1];
    if (_wcsicmp(mode, L"binder") == 0) {
        RunFromMemory(felty_binder_exe, felty_binder_exe_len, NULL);
    } else if (_wcsicmp(mode, L"loader") == 0) {
        RunFromMemory(felty_loader_exe, felty_loader_exe_len, NULL);
    } else if (_wcsicmp(mode, L"decrypt") == 0) {
        RunFromMemory(felty_decrypt_exe, felty_decrypt_exe_len, NULL);
    } else {
        ShowUsage();
    }

    LocalFree(argv);
    return 0;
}
