// pg3d_dumper.dll - writes pg3d_dump.txt with the metadata the cheat needs
#include "../src/core/common.h"
#include "../src/sdk/il2cpp.h"

static const char* kKeys[] = { "Player", "Weapon", "Camera", "Game", "Damage", "Team", "Health", "Ammo", "Bullet", "Recoil", "Spread" };

static bool Matches(const char* n) {
    if (!n) return false;
    for (const char* k : kKeys) if (strstr(n, k)) return true;
    return false;
}

// game.cpp calls this; the dumper has no aimbot, so satisfy the linker with a no-op
namespace aimbot { void ApplyNow() {} }

static void Dump(FILE* f, void* klass) {
    const char* cn = il2::api.class_get_name ? il2::api.class_get_name(klass) : nullptr;
    fprintf(f, "\nclass %s\n", cn ? cn : "?");
    void* it = nullptr;
    while (void* fld = il2::api.class_get_fields(klass, &it)) {
        const char* fn = il2::api.field_get_name(fld);
        fprintf(f, "  field %s +0x%X\n", fn ? fn : "?", il2::api.field_get_offset(fld));
    }
    it = nullptr;
    int n = 0;
    while (void* m = il2::api.class_get_methods(klass, &it)) {
        const char* mn = il2::api.method_get_name(m);
        if (mn) { fprintf(f, "  method %s\n", mn); if (++n > 400) break; }
    }
}

static DWORD WINAPI Run(LPVOID) {
    for (int i = 0; i < 150; i++) {
        if (GetModuleHandleW(L"GameAssembly.dll")) break;
        Sleep(200);
    }
    if (!il2::Init()) {
        MessageBoxW(nullptr, L"GameAssembly.dll missing or its exports are stripped", L"dumper", MB_ICONERROR);
        return 0;
    }

    const std::wstring path = DllDir() + L"\\pg3d_dump.txt";
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"w");
    if (!f) return 0;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fprintf(f, "target: %ls\n", exe);
    fprintf(f, "modules: d3d11=%d d3d12=%d vulkan=%d\n",
        GetModuleHandleW(L"d3d11.dll") != nullptr,
        GetModuleHandleW(L"d3d12.dll") != nullptr,
        GetModuleHandleW(L"vulkan-1.dll") != nullptr);

    fprintf(f, "\nexports:\n");
    const HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
    const char* names[] = { "il2cpp_domain_get", "il2cpp_class_from_name", "il2cpp_field_get_offset",
                            "il2cpp_class_get_method_from_name", "il2cpp_method_get_pointer", "il2cpp_thread_attach" };
    for (const char* n : names) fprintf(f, "  %s = %p\n", n, GetProcAddress(ga, n));

    void* img = il2::Image("Assembly-CSharp");
    const size_t total = (img && il2::api.image_get_class_count) ? il2::api.image_get_class_count(img) : 0;
    fprintf(f, "\nimage=%p classes=%zu\n", img, total);

    if (total) {
        fprintf(f, "\n--- interesting classes ---\n");
        for (size_t i = 0; i < total; i++) {
            void* k = il2::api.image_get_class(img, i);
            if (k && Matches(il2::api.class_get_name(k))) Dump(f, k);
        }
        fprintf(f, "\n--- full class list ---\n");
        for (size_t i = 0; i < total; i++) {
            void* k = il2::api.image_get_class(img, i);
            const char* cn = k ? il2::api.class_get_name(k) : nullptr;
            if (cn) fprintf(f, "%s\n", cn);
        }
    }

    fclose(f);
    MessageBoxW(nullptr, path.c_str(), L"dump written", MB_ICONINFORMATION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = mod;
        DisableThreadLibraryCalls(mod);
        HANDLE h = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
