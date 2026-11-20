#include "common.h"
#include <io.h>
#include <fcntl.h>
#pragma comment(lib, "psapi.lib")

HMODULE g_self = nullptr;
static FILE* g_log = nullptr;

std::wstring DllDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(g_self, buf, MAX_PATH);
    std::wstring s(buf);
    const auto p = s.find_last_of(L'\\');
    return p == std::wstring::npos ? std::wstring(L".") : s.substr(0, p);
}

void LogInit() {
    const std::wstring p = DllDir() + L"\\pg3d_core.log";
    // Open with shared read so external tools can tail the log while the DLL runs
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(h), 0);
        if (fd >= 0) g_log = _fdopen(fd, "w");
        else CloseHandle(h);
    }
}

void Log(const char* fmt, ...) {
    char buf[2048]{};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}

uintptr_t ModuleBase(const wchar_t* name) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(name));
}

size_t ModuleSize(const wchar_t* name) {
    const HMODULE m = GetModuleHandleW(name);
    if (!m) return 0;
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof(mi))) return 0;
    return mi.SizeOfImage;
}

bool InModule(uintptr_t p, const wchar_t* name) {
    const uintptr_t base = ModuleBase(name);
    const size_t size = ModuleSize(name);
    return base && size && p >= base && p < base + size;
}

bool Ok(const void* p) {
    return p && !IsBadReadPtr(p, sizeof(void*));
}

Quat EulerToQuat(float pitch, float yaw, float roll) {
    const float d = 0.0174532924f;
    const float cp = cosf(pitch * d * 0.5f), sp = sinf(pitch * d * 0.5f);
    const float cy = cosf(yaw * d * 0.5f),   sy = sinf(yaw * d * 0.5f);
    const float cr = cosf(roll * d * 0.5f),  sr = sinf(roll * d * 0.5f);
    Quat q;
    q.w = cy * cp * cr + sy * sp * sr;
    q.x = cy * sp * cr + sy * cp * sr;
    q.y = sy * cp * cr - cy * sp * sr;
    q.z = cy * cp * sr - sy * sp * cr;
    return q;
}

Quat Slerp(const Quat& a, const Quat& b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    Quat bb = b;
    if (d < 0.f) { d = -d; bb = { -b.x, -b.y, -b.z, -b.w }; }
    if (d > 0.9995f) {
        Quat r{ a.x + (bb.x - a.x) * t, a.y + (bb.y - a.y) * t, a.z + (bb.z - a.z) * t, a.w + (bb.w - a.w) * t };
        const float l = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
        if (l < 0.0001f) return bb;
        return { r.x / l, r.y / l, r.z / l, r.w / l };
    }
    const float th = acosf(d), s = sinf(th);
    const float wa = sinf((1.f - t) * th) / s, wb = sinf(t * th) / s;
    return { a.x * wa + bb.x * wb, a.y * wa + bb.y * wb, a.z * wa + bb.z * wb, a.w * wa + bb.w * wb };
}

namespace input {
    static bool s_cur[256]{}, s_prev[256]{};

    void Update() {
        memcpy(s_prev, s_cur, sizeof(s_cur));
        for (int i = 0; i < 256; i++)
            s_cur[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    bool Down(int vk)    { return vk > 0 && vk < 256 && s_cur[vk]; }
    bool Pressed(int vk) { return vk > 0 && vk < 256 && s_cur[vk] && !s_prev[vk]; }

    bool CanAutoClick() {
        const HWND foreground = GetForegroundWindow();
        DWORD process = 0;
        GetWindowThreadProcessId(foreground, &process);
        if (!foreground || process != GetCurrentProcessId()) return false;
        // SendInput uses the OS cursor, even when Unity hides it. Foreground
        // alone does not protect the taskbar or another window under that cursor.
        if ((GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LWIN) |
             GetAsyncKeyState(VK_RWIN)) & 0x8000) return false;
        POINT point{};
        RECT client{};
        if (!GetCursorPos(&point) || !GetClientRect(foreground, &client)) return false;
        const HWND underCursor = WindowFromPoint(point);
        if (!underCursor || GetAncestor(underCursor, GA_ROOT) != GetAncestor(foreground, GA_ROOT)) return false;
        if (!ScreenToClient(foreground, &point) || !PtInRect(&client, point)) return false;
        return foreground == GetForegroundWindow();
    }

    void Click(int holdMs) {
        if (!CanAutoClick()) return;
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &in, sizeof(in));
        Sleep(holdMs > 0 ? holdMs : 1);
        in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(in));
    }

    void Tap(int vk, int holdMs) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = static_cast<WORD>(vk);
        SendInput(1, &in, sizeof(in));
        Sleep(holdMs);
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(in));
    }
}
