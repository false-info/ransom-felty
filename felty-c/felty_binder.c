/* Message loop */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (g_unlocked) break;
    }

    /* Cleanup */
    KillTimer(g_hwnd, 1);
    if (g_kb_hook) UnhookWindowsHookEx(g_kb_hookalert);

    /* Restore taskbar */
    HWND hTaskBar = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hTaskBar) ShowWindow(hTaskBar, SW_SHOW);

    DestroyWindow(g_hwnd);
    CloseHandle(hm);
    return 0;
}
