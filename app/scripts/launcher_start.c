// SPDX-License-Identifier: GPL-2.0-or-later
// GUI entry point: Explorer icon and console-free Python/Tk startup.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wchar.h>
#include <stdio.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    (void)instance; (void)previous; (void)args; (void)show;
    wchar_t directory[32768], script[32768], python[32768], command[65536];
    DWORD length = GetModuleFileNameW(NULL, directory, 32768);
    if (!length || length >= 32768) return 1;
    wchar_t *slash = wcsrchr(directory, L'\\');
    if (!slash) return 1;
    *slash = L'\0';
    if (swprintf(script, 32768, L"%ls\\GravityRushLauncher.py", directory) < 0) return 1;
    if (GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        if (swprintf(script, 32768, L"%ls\\app\\GravityRushLauncher.py", directory) < 0 ||
            GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(NULL, L"Cannot find app\\GravityRushLauncher.py beside this launcher.",
                        L"Lian Port", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    // Source-development entry point; player packages use the frozen launcher.
    wcscpy(directory, script);
    *wcsrchr(directory, L'\\') = L'\0';
    length = SearchPathW(NULL, L"pythonw.exe", NULL, 32768, python, NULL);
    if (!length || length >= 32768) {
        MessageBoxW(NULL, L"Python's windowed runtime (pythonw.exe) was not found. Install Python with Tkinter and add it to PATH.",
                    L"Lian Port", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (swprintf(command, 65536, L"\"%ls\" \"%ls\"", python, script) < 0) return 1;
    STARTUPINFOW startup = {0};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {0};
    if (!CreateProcessW(python, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, directory, &startup, &process)) {
        wchar_t error[256];
        swprintf(error, 256, L"Unable to start the launcher (Windows error %lu).", GetLastError());
        MessageBoxW(NULL, error, L"Lian Port", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
