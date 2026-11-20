// Read-only reverse-engineering probe. Changes no game behaviour: it counts calls to a
// few Unity APIs and names the (obfuscated) game method that made each call, so the
// shot / look / move code can be located from one match instead of guessed.
#include "features.h"
#include "../sdk/il2cpp.h"
#include "MinHook.h"
#include <intrin.h>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace probe {
namespace {
    struct Ent { uintptr_t p; const char* cls; const char* m; };
    std::vector<Ent> g_tab;
    uintptr_t g_gaLo = 0, g_gaHi = 0;

    constexpr int kMax = 96;
    void* g_orig[kMax]{};
    std::vector<std::string> g_label;
    std::vector<void*> g_targets;

    struct Rec { uint32_t slot; uintptr_t ret; uintptr_t self; };
    constexpr LONG kRing = 8192;
    Rec g_ring[kRing]{};
    volatile LONG g_head = 0;

    std::atomic<uintptr_t> g_localTr{ 0 }, g_camTr{ 0 };
    bool g_only[kMax]{};
    std::atomic<bool> g_run{ false };
    HANDLE g_thread = nullptr;
    PVOID g_veh = nullptr;

    // Only integer work before the call: float arguments (Raycast maxDistance) live in
    // xmm registers, and a function call here could clobber them.
    template <int N>
    void* __fastcall Stub(void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h) {
        // hot getters/setters are only worth recording when they touch the local rig
        if (!g_only[N] || reinterpret_cast<uintptr_t>(a) == g_localTr || reinterpret_cast<uintptr_t>(a) == g_camTr) {
            const LONG i = _InterlockedIncrement(&g_head) & (kRing - 1);
            g_ring[i].slot = N;
            g_ring[i].ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
            g_ring[i].self = reinterpret_cast<uintptr_t>(a);
        }
        using F = void* (__fastcall*)(void*, void*, void*, void*, void*, void*, void*, void*);
        return reinterpret_cast<F>(g_orig[N])(a, b, c, d, e, f, g, h);
    }

    template <size_t... I>
    std::array<void*, kMax> MakeStubs(std::index_sequence<I...>) {
        return { reinterpret_cast<void*>(&Stub<static_cast<int>(I)>)... };
    }

    // il2cpp_method_get_pointer is not exported by this build; MethodInfo's first field is the pointer
    void* MPtr(void* m) { return il2::api.method_get_pointer ? il2::api.method_get_pointer(m) : *reinterpret_cast<void**>(m); }

    // il2cpp reflection can fault on odd classes; a bad one must cost a table row, not the game.
    void* NextMethod(void* k, void** it, const char*& name, void*& ptr) {
        __try {
            void* m = il2::api.class_get_methods(k, it);
            if (!m) return nullptr;
            name = il2::api.method_get_name(m);
            ptr = MPtr(m);
            return m;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            *it = nullptr;
            return nullptr;
        }
    }

    void BuildTable() {
        void* img = il2::Image("Assembly-CSharp");
        auto& a = il2::api;
        if (!img || !a.image_get_class_count || !a.image_get_class || !a.class_get_name ||
            !a.class_get_methods || !a.method_get_name) {
            Log("[probe] table skipped: img=%p cnt=%p cls=%p cname=%p meths=%p mname=%p mptr=%p", img,
                a.image_get_class_count, a.image_get_class, a.class_get_name, a.class_get_methods,
                a.method_get_name, il2::api.method_get_pointer);
            return;
        }
        const size_t n = a.image_get_class_count(img);
        Log("[probe] building table over %zu classes", n);
        for (size_t i = 0; i < n; i++) {
            void* k = a.image_get_class(img, i);
            if (!k) continue;
            const char* cn = a.class_get_name(k);
            void* it = nullptr;
            const char* mn = nullptr;
            void* p = nullptr;
            while (NextMethod(k, &it, mn, p)) {
                const uintptr_t pp = reinterpret_cast<uintptr_t>(p);
                if (pp < g_gaLo || pp >= g_gaHi) continue;
                g_tab.push_back({ pp, cn ? cn : "?", mn });
            }
        }
        std::sort(g_tab.begin(), g_tab.end(), [](const Ent& x, const Ent& y) { return x.p < y.p; });
        Log("[probe] method table: %zu entries", g_tab.size());
    }

    std::string Resolve(uintptr_t addr) {
        char buf[200];
        if (addr >= g_gaLo && addr < g_gaHi && !g_tab.empty()) {
            auto it = std::upper_bound(g_tab.begin(), g_tab.end(), addr,
                                       [](uintptr_t v, const Ent& e) { return v < e.p; });
            if (it != g_tab.begin()) {
                --it;
                snprintf(buf, sizeof(buf), "%s::%s +0x%llX [GA+0x%llX]", it->cls, it->m ? it->m : "?",
                         static_cast<unsigned long long>(addr - it->p),
                         static_cast<unsigned long long>(addr - g_gaLo));
                return buf;
            }
        }
        const uintptr_t self = reinterpret_cast<uintptr_t>(g_self);
        if (addr >= self && addr < self + 0x400000) {
            snprintf(buf, sizeof(buf), "pg3d_core+0x%llX", static_cast<unsigned long long>(addr - self));
            return buf;
        }
        snprintf(buf, sizeof(buf), "%p", reinterpret_cast<void*>(addr));
        return buf;
    }

    void Add(void* klass, const char* name, const std::array<void*, kMax>& stubs, bool onlyLocal = false) {
        auto& a = il2::api;
        if (!klass || !a.class_get_methods || !a.method_get_param) return;
        void* it = nullptr;
        while (void* m = a.class_get_methods(klass, &it)) {
            const char* mn = a.method_get_name(m);
            if (!mn || strcmp(mn, name) != 0) continue;
            if (static_cast<int>(g_label.size()) >= kMax) return;
            void* p = MPtr(m);
            const uintptr_t pp = reinterpret_cast<uintptr_t>(p);
            if (pp < g_gaLo || pp >= g_gaHi) continue;
            std::string l = std::string(a.class_get_name(klass)) + "." + mn + "(";
            const uint32_t pc = a.method_get_param_count(m);
            for (uint32_t i = 0; i < pc; i++) {
                void* t = a.method_get_param(m, i);
                void* k = t ? a.class_from_type(t) : nullptr;
                const char* tn = k ? a.class_get_name(k) : "?";
                l += (i ? "," : "");
                l += tn ? tn : "?";
            }
            l += ")";
            const int slot = static_cast<int>(g_label.size());
            if (MH_CreateHook(p, stubs[slot], &g_orig[slot]) != MH_OK) continue;
            g_only[slot] = onlyLocal;
            g_label.push_back(l);
            g_targets.push_back(p);
        }
    }

    // flags: 1 firing, 2 moving, 4 `this` is the local player transform, 8 `this` is the camera
    using Key = std::tuple<uint32_t, uintptr_t, uint32_t>;

    DWORD WINAPI Worker(LPVOID) {
        LONG tail = g_head;
        std::set<Key> seen;
        std::map<Key, uint32_t> win;
        DWORD lastSum = GetTickCount();
        int newLines = 0;
        while (g_run) {
            Sleep(200);
            uint32_t base = 0;
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) base |= 1;
            if ((GetAsyncKeyState('W') | GetAsyncKeyState('A') | GetAsyncKeyState('S') | GetAsyncKeyState('D')) & 0x8000) base |= 2;
            const LONG head = g_head;
            if (head - tail > kRing) tail = head - kRing;
            for (; tail != head; tail++) {
                const Rec r = g_ring[tail & (kRing - 1)];
                if (r.slot >= g_label.size() || r.ret < g_gaLo || r.ret >= g_gaHi) continue;
                uint32_t fl = base;
                if (r.self && r.self == g_localTr) fl |= 4;
                if (r.self && r.self == g_camTr) fl |= 8;
                const Key k{ r.slot, r.ret, fl };
                win[k]++;
                if (newLines < 400 && seen.insert(k).second) {
                    newLines++;
                    Log("[probe] NEW %s  <- %s  fl=%u", g_label[r.slot].c_str(), Resolve(r.ret).c_str(), fl);
                }
            }
            if (GetTickCount() - lastSum > 10000) {
                lastSum = GetTickCount();
                std::vector<std::pair<uint32_t, Key>> v;
                for (auto& kv : win) v.push_back({ kv.second, kv.first });
                std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first > y.first; });
                for (size_t i = 0; i < v.size() && i < 14; i++)
                    Log("[probe] 10s n=%u %s <- %s fl=%u", v[i].first, g_label[std::get<0>(v[i].second)].c_str(),
                        Resolve(std::get<1>(v[i].second)).c_str(), std::get<2>(v[i].second));
                win.clear();
            }
        }
        return 0;
    }

    // Names the faulting method when the game dies, so a crash is a one-line diagnosis.
    LONG CALLBACK Veh(PEXCEPTION_POINTERS e) {
        static std::set<uintptr_t> logged;
        static std::mutex mtx;
        const DWORD c = e->ExceptionRecord->ExceptionCode;
        if (c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_ILLEGAL_INSTRUCTION &&
            c != EXCEPTION_STACK_OVERFLOW && c != EXCEPTION_INT_DIVIDE_BY_ZERO)
            return EXCEPTION_CONTINUE_SEARCH;
        const uintptr_t rip = e->ContextRecord->Rip;
        std::lock_guard<std::mutex> g(mtx);
        // a call through a null pointer leaves the caller's return address on top of the stack
        const uintptr_t caller = (rip == 0 && !IsBadReadPtr(reinterpret_cast<void*>(e->ContextRecord->Rsp), 8))
                                     ? *reinterpret_cast<uintptr_t*>(e->ContextRecord->Rsp) : 0;
        if (logged.size() < 64 && logged.insert(rip ? rip : caller).second)
            Log("[probe] EXCEPTION %08lX at %s  caller=%s  tid=%lu addr=%p", c, Resolve(rip).c_str(),
                Resolve(caller).c_str(), GetCurrentThreadId(),
                reinterpret_cast<void*>(e->ExceptionRecord->NumberParameters > 1 ? e->ExceptionRecord->ExceptionInformation[1] : 0));
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

void Init() {
    g_gaLo = ModuleBase(L"GameAssembly.dll");
    g_gaHi = g_gaLo + ModuleSize(L"GameAssembly.dll");
    if (!g_gaLo) return;
    g_veh = AddVectoredExceptionHandler(1, Veh);
    BuildTable();
    // Tracing hooks ~60 Unity methods; it served its purpose (see the probe round logs) and every
    // hook is a crash risk, so a normal build keeps only the method table and the crash logger.
    // Physics queries only: which game code fires a normal weapon (the previously wrapped shot
    // method was never called by one). kFullTrace adds the movement/rotation set.
    // Off: its generic hook on Raycast(Ray,...) would collide with silent aim's own hook on it, and
    // the caller list it produced is what silent aim now uses.
    constexpr bool kTrace = false;
    constexpr bool kFullTrace = false;
    if (!kTrace) return;

    const auto stubs = MakeStubs(std::make_index_sequence<kMax>{});
    void* phys = il2::Class("Physics", "UnityEngine");
    for (const char* n : { "Raycast", "RaycastAll", "RaycastNonAlloc", "SphereCast", "SphereCastAll", "CapsuleCast",
                           "OverlapSphere", "Linecast" })
        Add(phys, n, stubs);
    if (kFullTrace) {
    Add(il2::Class("CharacterController", "UnityEngine"), "Move", stubs);
    Add(il2::Class("CharacterController", "UnityEngine"), "SimpleMove", stubs);
    void* tr = il2::Class("Transform", "UnityEngine");
    for (const char* n : { "set_rotation", "set_localRotation", "set_eulerAngles", "set_localEulerAngles", "Rotate", "LookAt" })
        Add(tr, n, stubs);
    // movement writers and the rotation getters (the network sender reads the rig's rotation)
    for (const char* n : { "get_rotation", "get_eulerAngles", "set_position", "set_localPosition", "Translate" })
        Add(tr, n, stubs, true);
    void* rb = il2::Class("Rigidbody", "UnityEngine");
    for (const char* n : { "set_velocity", "MovePosition", "MoveRotation" }) Add(rb, n, stubs);
    }

    for (void* t : g_targets) MH_EnableHook(t);
    Log("[probe] hooked %zu Unity methods", g_targets.size());
    for (size_t i = 0; i < g_label.size(); i++) Log("[probe]   #%zu %s", i, g_label[i].c_str());

    g_run = true;
    g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
}

// Bounds of the game method containing `addr`, from the table built at Init.
bool MethodRange(uintptr_t addr, uintptr_t& lo, uintptr_t& hi, std::string& name) {
    auto it = std::upper_bound(g_tab.begin(), g_tab.end(), addr,
                               [](uintptr_t v, const Ent& e) { return v < e.p; });
    if (it == g_tab.begin() || it == g_tab.end()) return false;
    const Ent& e = *(it - 1);
    lo = e.p;
    hi = it->p;
    name = std::string(e.cls ? e.cls : "?") + "::" + (e.m ? e.m : "?");
    return addr >= lo && addr < hi;
}

uintptr_t GaBase() { return g_gaLo; }

void Shutdown() {
    g_run = false;
    if (g_thread) { WaitForSingleObject(g_thread, 1000); CloseHandle(g_thread); g_thread = nullptr; }
    for (void* t : g_targets) { MH_DisableHook(t); MH_RemoveHook(t); }
    g_targets.clear();
    if (g_veh) { RemoveVectoredExceptionHandler(g_veh); g_veh = nullptr; }
}

// Game thread, ~30 Hz.
void OnTick() {
    if (!g_run) return;
    if (game::Player* l = game::Local()) g_localTr = reinterpret_cast<uintptr_t>(l->tr);
    // PG3D's own camera is a child of the player rig, so remember it to tell a look
    // write on the local rig apart from the many writes to remote players.
    g_camTr = reinterpret_cast<uintptr_t>(game::CamTransform());

    // Dump each distinct local weapon object once. Diffing two weapons (one with the
    // "Wall break" tag, one without) finds the flag without a name.
    static void* lastW = nullptr;
    static int dumps = 0;
    void* w = game::LocalWeapon();
    if (w && w != lastW && dumps < 12 && !IsBadReadPtr(w, 0x500)) {
        lastW = w;
        dumps++;
        std::string cn;
        il2::ClassName(w, cn);
        Log("[probe] WEAPON #%d t=%lus obj=%p class=%s", dumps, GetTickCount() / 1000, w, cn.c_str());
        const uint8_t* b = reinterpret_cast<const uint8_t*>(w);
        for (int off = 0x10; off < 0x500; off += 32) {
            char hex[100]{};
            for (int i = 0; i < 32; i++) snprintf(hex + i * 3, 4, "%02X ", b[off + i]);
            Log("[probe] w%d +%03X %s", dumps, off, hex);
        }
    }
}
}

