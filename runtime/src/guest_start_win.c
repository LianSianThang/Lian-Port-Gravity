/* Start the emulator with bottom-up and high-entropy ASLR disabled for its process.
 * Windows otherwise places the main thread stack in the 64 GiB–1 TiB PS4 guest range,
 * which the runtime must reserve as one contiguous placeholder. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MITIGATION_HIGH_ENTROPY_ASLR_ALWAYS_OFF UINT64_C(0x00200000)
#define MITIGATION_BOTTOM_UP_ASLR_ALWAYS_OFF   UINT64_C(0x00020000)

static size_t quoted_length(const wchar_t *arg) {
    size_t n = 2;
    size_t slashes = 0;
    for (; *arg; ++arg) {
        if (*arg == L'\\') {
            ++slashes;
        } else if (*arg == L'"') {
            n += slashes * 2 + 2;
            slashes = 0;
        } else {
            n += slashes + 1;
            slashes = 0;
        }
    }
    return n + slashes * 2 + 1;
}

static wchar_t *append_quoted(wchar_t *out, const wchar_t *arg) {
    size_t slashes = 0;
    *out++ = L'"';
    for (; *arg; ++arg) {
        if (*arg == L'\\') {
            ++slashes;
        } else if (*arg == L'"') {
            for (size_t i = 0; i < slashes * 2 + 1; ++i) *out++ = L'\\';
            *out++ = L'"';
            slashes = 0;
        } else {
            for (size_t i = 0; i < slashes; ++i) *out++ = L'\\';
            *out++ = *arg;
            slashes = 0;
        }
    }
    for (size_t i = 0; i < slashes * 2; ++i) *out++ = L'\\';
    *out++ = L'"';
    return out;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 3) {
        fwprintf(stderr, L"Usage: spartanport-guest-start.exe probe.exe boot.bin [options...]\n");
        return 2;
    }

    size_t command_chars = 1;
    for (int i = 1; i < argc; ++i) command_chars += quoted_length(argv[i]) + 1;
    wchar_t *command_line = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, command_chars * sizeof(wchar_t));
    if (!command_line) return 3;
    wchar_t *cursor = command_line;
    for (int i = 1; i < argc; ++i) {
        if (i != 1) *cursor++ = L' ';
        cursor = append_quoted(cursor, argv[i]);
    }
    *cursor = L'\0';

    SIZE_T attribute_bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = (LPPROC_THREAD_ATTRIBUTE_LIST)
        HeapAlloc(GetProcessHeap(), 0, attribute_bytes);
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &attribute_bytes)) {
        fwprintf(stderr, L"Could not initialize Windows process mitigation attributes (%lu).\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, attributes);
        HeapFree(GetProcessHeap(), 0, command_line);
        return 4;
    }

    DWORD64 policy = MITIGATION_HIGH_ENTROPY_ASLR_ALWAYS_OFF | MITIGATION_BOTTOM_UP_ASLR_ALWAYS_OFF;
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                   &policy, sizeof(policy), NULL, NULL)) {
        fwprintf(stderr, L"Could not set Windows process memory policy (%lu).\n", GetLastError());
        DeleteProcThreadAttributeList(attributes);
        HeapFree(GetProcessHeap(), 0, attributes);
        HeapFree(GetProcessHeap(), 0, command_line);
        return 5;
    }

    STARTUPINFOEXW startup = {0};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process = {0};
    BOOL created = CreateProcessW(argv[1], command_line, NULL, NULL, TRUE,
                                  EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                                  &startup.StartupInfo, &process);
    DWORD create_error = created ? ERROR_SUCCESS : GetLastError();
    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);
    HeapFree(GetProcessHeap(), 0, command_line);
    if (!created) {
        fwprintf(stderr, L"Could not start Gravity Rush runtime (%lu).\n", create_error);
        return 6;
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)exit_code;
}
