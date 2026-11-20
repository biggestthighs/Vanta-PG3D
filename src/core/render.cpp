#include "render.h"
#include "config.h"
#include "../features/farm.h"
#include "menu.h"
#include "../sdk/game.h"
#include "../features/features.h"
#include "../sdk/il2cpp.h"
#include "MinHook.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace render {
    bool MenuOpen = false;
    double SessionSec = 0, MatchSec = 0;
    HWND Hwnd = nullptr;

    static ID3D11Device* s_dev = nullptr;
    static ID3D11DeviceContext* s_ctx = nullptr;
    static ID3D11RenderTargetView* s_rtv = nullptr;
    static IDXGISwapChain* s_swap = nullptr;
    static WNDPROC s_wnd = nullptr;
    static bool s_init = false;
    static bool s_imgui = false;
    static bool s_menuWasOpen = false;

    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using ResizeFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    static PresentFn oPresent = nullptr;
    static ResizeFn oResize = nullptr;
    static void** s_vt = nullptr;

    static void CreateRtv() {
        ID3D11Texture2D* back = nullptr;
        if (FAILED(s_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) return;
        s_dev->CreateRenderTargetView(back, nullptr, &s_rtv);
        back->Release();
    }

    // ---- keep the game's own input away while the menu is open ----------------------------------
    // The WndProc below never sees this game's mouse clicks as Windows messages, so swallowing
    // them there does nothing. The block sits where the game consumes the mouse instead: NGUI's
    // UICamera turns mouse and touch state into presses, hovers and clicks every frame, and the
    // weapon fire button reads UnityEngine.Input. While the menu is open both see nothing.
    using Fn4 = void(__fastcall*)(void*, void*, void*, void*);
    using BtnFn = bool(__fastcall*)(int, void*);
    static Fn4 oProcMouse = nullptr, oProcTouches = nullptr;
    static BtnFn oBtn = nullptr, oBtnDown = nullptr, oBtnUp = nullptr;
    static void __fastcall hkProcMouse(void* s, void* a, void* b, void* c) { if (!MenuOpen) oProcMouse(s, a, b, c); }
    static void __fastcall hkProcTouches(void* s, void* a, void* b, void* c) { if (!MenuOpen) oProcTouches(s, a, b, c); }
    static bool __fastcall hkBtn(int i, void* mi) { return !MenuOpen && oBtn(i, mi); }
    static bool __fastcall hkBtnDown(int i, void* mi) { return !MenuOpen && oBtnDown(i, mi); }
    static bool __fastcall hkBtnUp(int i, void* mi) { return !MenuOpen && oBtnUp(i, mi); }

    static void HookMethod(void* cls, const char* name, int argc, void* detour, void** orig) {
        void* m = il2::MethodPtr(cls, name, argc);
        const MH_STATUS s = m ? MH_CreateHook(m, detour, orig) : MH_ERROR_NOT_CREATED;
        Log("[render] game input block: %s %p create=%d enable=%d", name, m, s, s == MH_OK ? MH_EnableHook(m) : -1);
    }

    void InstallGameInputBlock() {
        void* ui = il2::Class("UICamera");
        HookMethod(ui, "ProcessMouse", -1, &hkProcMouse, reinterpret_cast<void**>(&oProcMouse));
        HookMethod(ui, "ProcessTouches", -1, &hkProcTouches, reinterpret_cast<void**>(&oProcTouches));
        void* in = il2::Class("Input", "UnityEngine");
        HookMethod(in, "GetMouseButton", 1, &hkBtn, reinterpret_cast<void**>(&oBtn));
        HookMethod(in, "GetMouseButtonDown", 1, &hkBtnDown, reinterpret_cast<void**>(&oBtnDown));
        HookMethod(in, "GetMouseButtonUp", 1, &hkBtnUp, reinterpret_cast<void**>(&oBtnUp));
    }

    LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_KEYDOWN && wp == VK_END) RequestUnload();
        if (s_imgui) ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp);

        if (msg == WM_KEYDOWN && wp == static_cast<WPARAM>(cfg.menu_key)) {
            MenuOpen = !MenuOpen;
            if (!MenuOpen) ConfigSave();
        }
        if (MenuOpen) {
            // Show arrow cursor; prevents game from resetting it via WM_SETCURSOR
            if (msg == WM_SETCURSOR) {
                SetCursor(LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW)));
                return TRUE;
            }
            if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP ||
                msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP || msg == WM_MOUSEWHEEL ||
                msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP ||
                (msg >= WM_KEYDOWN && msg <= WM_KEYUP))
                return 1;
        }
        return CallWindowProcW(s_wnd, h, msg, wp, lp);
    }

    HRESULT __stdcall hkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
        if (!s_init) {
            s_swap = sc;
            if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_dev))) && s_dev) {
                s_dev->GetImmediateContext(&s_ctx);
                DXGI_SWAP_CHAIN_DESC d{};
                sc->GetDesc(&d);
                Hwnd = d.OutputWindow;
                CreateRtv();
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                io.IniFilename = nullptr;
                ImGui::StyleColorsDark();
                menu::ApplyStyle();
                // Segoe UI reads far better than ImGui's built-in bitmap-ish font; keep the default if absent
                if (ImFont* f = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.f)) io.FontDefault = f;
                ImGui_ImplWin32_Init(Hwnd);
                ImGui_ImplDX11_Init(s_dev, s_ctx);
                s_imgui = true;
                s_wnd = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
                s_init = true;
                Log("[render] overlay up");
            }
        }

        if (s_imgui) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            input::Update();
            SessionSec += ImGui::GetIO().DeltaTime;
            if (game::Ready() && game::Local()) MatchSec += ImGui::GetIO().DeltaTime;
            if (!MenuOpen && input::Pressed(cfg.panic_key)) cfg.panic = !cfg.panic;

            // Handle cursor visibility transitions
            if (MenuOpen != s_menuWasOpen) {
                if (MenuOpen) { while (ShowCursor(TRUE) < 0) {} }
                else          { while (ShowCursor(FALSE) >= 0) {} }
                s_menuWasOpen = MenuOpen;
            }
            if (MenuOpen) {
                // Unity re-locks cursor every frame; free it every frame
                ClipCursor(nullptr);
                // Feed real cursor position and button state to ImGui directly —
                // Unity's input system intercepts WM messages so we bypass it here
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(Hwnd, &p);
                ImGuiIO& io = ImGui::GetIO();
                io.MousePos = { static_cast<float>(p.x), static_cast<float>(p.y) };
                io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
                io.MouseDrawCursor = true;  // ImGui draws its own cursor — avoids fighting Unity's cursor lock
            } else {
                ImGui::GetIO().MouseDrawCursor = false;
            }

            // game::Tick() and misc::Tick() run on the game thread (hkUpdate). Aimbot and
            // triggerbot run here because this is the only genuine once-per-frame hook, and
            // they need nothing but W2S off-thread.
            if (cfg.panic) MenuOpen = false;
            if (game::Ready()) triggerbot::Run();
            if (game::Ready() && !cfg.panic && !farm::SuppressFeatures()) {
                hitsound::Frame();
                visuals::Draw();
                aimbot::DrawFov();
                misc::DrawOverlay();
            }
            if (MenuOpen) menu::Render();

            ImGui::Render();
            s_ctx->OMSetRenderTargets(1, &s_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
        return oPresent(sc, sync, flags);
    }

    HRESULT __stdcall hkResize(IDXGISwapChain* sc, UINT a, UINT b, UINT c, DXGI_FORMAT f, UINT g) {
        if (s_rtv) { s_rtv->Release(); s_rtv = nullptr; }
        const HRESULT r = oResize(sc, a, b, c, f, g);
        if (SUCCEEDED(r) && s_init) { s_swap = sc; CreateRtv(); }
        return r;
    }

    bool Init() {
        // Always use an independent window so we don't conflict with the game's existing swapchain.
        // 100x100 satisfies FLIP_DISCARD minimum size; WS_POPUP keeps it invisible.
        HWND wnd = CreateWindowExW(0, L"STATIC", nullptr, WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, nullptr, nullptr);
        if (!wnd) { Log("[render] dummy window failed"); return false; }

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = wnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        // Try flip model first (Unity 2019+); fall back to legacy discard
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        ID3D11Device* d = nullptr;
        ID3D11DeviceContext* c = nullptr;
        IDXGISwapChain* sc = nullptr;
        D3D_FEATURE_LEVEL fl{};
        const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2,
            D3D11_SDK_VERSION, &sd, &sc, &d, &fl, &c))) {
            sd.BufferCount = 1;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2,
                D3D11_SDK_VERSION, &sd, &sc, &d, &fl, &c))) {
                DestroyWindow(wnd);
                Log("[render] dummy device failed");
                return false;
            }
        }
        Log("[render] dummy swap effect=%d", static_cast<int>(sd.SwapEffect));
        DestroyWindow(wnd);

        s_vt = *reinterpret_cast<void***>(sc);
        void* pPresent = s_vt[8];
        void* pResize = s_vt[13];

        // Assign originals BEFORE patching so hkPresent never sees oPresent==null
        oPresent = reinterpret_cast<PresentFn>(pPresent);
        oResize  = reinterpret_cast<ResizeFn>(pResize);

        DWORD old = 0;
        VirtualProtect(s_vt, sizeof(void*) * 20, PAGE_READWRITE, &old);
        s_vt[8]  = &hkPresent;
        s_vt[13] = &hkResize;
        VirtualProtect(s_vt, sizeof(void*) * 20, old, &old);

        sc->Release();
        d->Release();
        c->Release();
        Log("[render] hooked present=%p", pPresent);
        return true;
    }

    bool Ready() { return s_init; }

    void Shutdown() {
        if (s_wnd && Hwnd) { SetWindowLongPtrW(Hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_wnd)); s_wnd = nullptr; }
        if (s_vt && oPresent) {
            DWORD old = 0;
            VirtualProtect(s_vt, sizeof(void*) * 20, PAGE_READWRITE, &old);
            s_vt[8] = reinterpret_cast<void*>(oPresent);
            s_vt[13] = reinterpret_cast<void*>(oResize);
            VirtualProtect(s_vt, sizeof(void*) * 20, old, &old);
        }
        if (s_imgui) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            s_imgui = false;
        }
        s_init = false;
    }
}
