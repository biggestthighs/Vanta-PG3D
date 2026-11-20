// pg3d_loader.exe <process> <dll>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstdio>

static DWORD FindPid(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static void EnablePriv() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &t)) return;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege",&tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(t, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(t);
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* proc = argc > 1 ? argv[1] : L"PixelGun3D.exe";
    wchar_t dll[MAX_PATH]{};
    GetFullPathNameW(argc > 2 ? argv[2] : L"pg3d_core.dll", MAX_PATH, dll, nullptr);

    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        wprintf(L"[x] dll not found: %s\n", dll);
        return 1;
    }
    EnablePriv();

    DWORD pid = 0;
    for (int i = 0; i < 60 && !pid; i++) {
        pid = FindPid(proc);
        if (!pid) Sleep(500);
    }
    if (!pid) { wprintf(L"[x] %s is not running\n", proc); return 1; }
    wprintf(L"[*] target pid %lu, injecting %s\n", pid, dll);

    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!p) { wprintf(L"[x] OpenProcess failed (%lu) - run as admin\n", GetLastError()); return 1; }

    const SIZE_T bytes = (wcslen(dll) + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(p, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { wprintf(L"[x] VirtualAllocEx failed\n"); CloseHandle(p); return 1; }

    WriteProcessMemory(p, mem, dll, bytes, nullptr);
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));

    HANDLE th = CreateRemoteThread(p, nullptr, 0, loadLib, mem, 0, nullptr);
    if (!th) { wprintf(L"[x] CreateRemoteThread failed (%lu)\n", GetLastError()); CloseHandle(p); return 1; }

    WaitForSingleObject(th, 15000);
    DWORD code = 0;
    GetExitCodeThread(th, &code);
    if (code) wprintf(L"[+] injected\n");
    else wprintf(L"[x] LoadLibrary returned 0 (arch mismatch?)\n");

    CloseHandle(th);
    VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    CloseHandle(p);
    return code ? 0 : 1;
}
