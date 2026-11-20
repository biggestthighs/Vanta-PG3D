#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>

struct Vec3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Col  { float r = 1, g = 1, b = 1, a = 1; };

extern HMODULE g_self;
void RequestUnload();

void LogInit();
void Log(const char* fmt, ...);

std::wstring DllDir();
uintptr_t ModuleBase(const wchar_t* name);
size_t ModuleSize(const wchar_t* name);
bool InModule(uintptr_t p, const wchar_t* name);
bool Ok(const void* p);

inline float Dist(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

Quat EulerToQuat(float pitch, float yaw, float roll);
Quat Slerp(const Quat& a, const Quat& b, float t);

namespace input {
    void Update();
    bool Down(int vk);
    bool Pressed(int vk);
}
