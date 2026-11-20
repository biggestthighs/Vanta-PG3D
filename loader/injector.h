#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <string>

namespace vanta {
struct Result { bool ok; std::wstring message; };
inline DWORD FindGame() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (!_wcsicmp(entry.szExeFile, L"Pixel Gun 3D.exe") || !_wcsicmp(entry.szExeFile, L"PixelGun3D.exe")) {
            pid = entry.th32ProcessID; break;
        }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return pid;
}
inline bool HasCore(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W entry{sizeof(entry)};
    bool found = false;
    if (Module32FirstW(snapshot, &entry)) do {
        if (!_wcsicmp(entry.szModule, L"pg3d_core.dll")) { found = true; break; }
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found;
}
inline std::filesystem::path Folder() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return std::filesystem::path(path).parent_path();
}
inline Result Inject(const std::filesystem::path& dll) {
    if (!std::filesystem::is_regular_file(dll))
        return {false, L"Missing pg3d_core.dll. Extract the entire ZIP before opening Vanta."};
    DWORD pid = FindGame();
    if (!pid) return {false, L"Start Pixel Gun 3D and wait for its main menu, then try again."};
    if (HasCore(pid)) return {true, L"Vanta is already loaded. INSERT opens the menu; END unloads it."};
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) return {false, L"Could not access the game (Windows error " + std::to_wstring(GetLastError()) + L"). Check that both programs run at the same privilege level."};
    USHORT targetMachine = 0, nativeMachine = 0;
    if (!IsWow64Process2(process, &targetMachine, &nativeMachine) ||
        targetMachine != IMAGE_FILE_MACHINE_UNKNOWN || nativeMachine != IMAGE_FILE_MACHINE_AMD64) {
        CloseHandle(process); return {false, L"Vanta requires the Windows x64 edition of the game."};
    }
    const auto full = std::filesystem::absolute(dll).wstring();
    SIZE_T bytes = (full.size() + 1) * sizeof(wchar_t), written = 0;
    void* memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!memory || !WriteProcessMemory(process, memory, full.c_str(), bytes, &written) || written != bytes) {
        if (memory) VirtualFreeEx(process, memory, 0, MEM_RELEASE);
        CloseHandle(process); return {false, L"Could not prepare the DLL path in the game."};
    }
    auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = load ? CreateRemoteThread(process, nullptr, 0, load, memory, 0, nullptr) : nullptr;
    if (!thread) {
        VirtualFreeEx(process, memory, 0, MEM_RELEASE); CloseHandle(process);
        return {false, L"Windows could not start the DLL loader in the game."};
    }
    const DWORD wait = WaitForSingleObject(thread, 15000);
    // A loader thread may still be reading its argument after a timeout.
    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, memory, 0, MEM_RELEASE);
    CloseHandle(thread); CloseHandle(process);
    if (wait != WAIT_OBJECT_0) return {false, L"Loading has not finished. Do not retry immediately; check the game and pg3d_core.log."};
    if (!HasCore(pid)) return {false, L"The DLL did not load. Keep the DLL beside Vanta.exe and check pg3d_core.log."};
    return {true, L"Vanta loaded. Return to the game and press INSERT. Press END to unload."};
}
}
