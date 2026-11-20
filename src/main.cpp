#include "core/common.h"
#include "core/config.h"
#include "core/render.h"
#include "sdk/offsets.h"
#include "sdk/game.h"
#include "sdk/il2cpp.h"

static std::atomic<bool> g_run{ true };
void RequestUnload() { g_run = false; }

static DWORD WINAPI Main(LPVOID) {
    LogInit();
    Log("=== pg3d_core injected ===");

    for (int i = 0; i < 300 && g_run; i++) {
        if (GetModuleHandleW(L"GameAssembly.dll")) { il2::Attach(); break; }
        Sleep(200);
    }

    offsets::Load();
    ConfigLoad();

    for (int i = 0; i < 300 && g_run; i++) {
        if (game::Init()) break;
        Sleep(200);
    }
    if (!game::Ready()) Log("[main] game init failed - overlay only");

    for (int i = 0; i < 100 && g_run; i++) {
        if (render::Init()) break;
        Sleep(200);
    }

    while (g_run) {
        input::Update();
        if (input::Pressed(VK_END)) break;
        Sleep(10);
    }

    render::Shutdown();
    game::Shutdown();
    Log("=== unloaded ===");
    FreeLibraryAndExitThread(g_self, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = mod;
        DisableThreadLibraryCalls(mod);
        HANDLE h = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_run = false;
    }
    return TRUE;
}
