// Aimbot that rotates the game's own camera (no mouse input), plus learning which weapons
// have Wall break. The probe located the shot method: a Player_move_c method that calls
// Physics.RaycastAll(V3,V3,float,int) and Physics.Raycast(V3,V3,RaycastHit,float,int)
// when the local player fires. Both are hooked as pure pass-throughs that only note which
// one a weapon uses; nothing is rewritten.
#include "features.h"
#include "../core/config.h"
#include "farm.h"
#include "../core/render.h"
#include "../sdk/il2cpp.h"
#include "MinHook.h"
#include <intrin.h>
#include <algorithm>
#include <unordered_map>

namespace rage {
namespace {
    using RayFn = bool(__fastcall*)(Vec3*, Vec3*, void*, float, int, void*);
    using AllFn = void*(__fastcall*)(Vec3*, Vec3*, float, int, void*);
    RayFn oRay = nullptr;
    AllFn oAll = nullptr;
    uintptr_t g_shotLo = 0, g_shotHi = 0;
    void* g_fnRotate = nullptr;

    constexpr uintptr_t kShotRva = 0x1FC33B9; // inside the shot method (probe, build of 2026-11-19)
    constexpr float kRad = 57.29578f;

    std::mutex g_mtx;
    std::unordered_map<void*, int> g_seen; // weapon -> bit1 saw RaycastAll, bit2 saw Raycast
    std::atomic<int> g_curWb{ -1 };

    std::mutex g_tmtx;
    std::string g_tName;
    Vec3 g_tAim{};
    bool g_tValid = false;
    void* g_lastObj = nullptr;
    // A killed target keeps its object for a while and its body falls to the floor, so
    // without this the crosshair follows the corpse down. Skip it and pause briefly.
    void* g_deadObj = nullptr;
    DWORD g_deadUntil = 0, g_coolUntil = 0;

    Vec3 Norm(Vec3 v) {
        const float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        return l > 1e-5f ? Vec3{ v.x / l, v.y / l, v.z / l } : Vec3{ 0, 0, 1 };
    }
    Vec3 Sub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    float Angle(const Vec3& a, const Vec3& b) { return acosf(std::max(-1.f, std::min(1.f, Dot(a, b)))); }

    // Best target inside a cone of cfg.aim.fov degrees around `fwd`. Game thread only.
    bool Choose(const Vec3& from, const Vec3& fwd, Vec3& aim, std::string& name) {
        const auto snap = game::Snapshot();
        const bool wb = g_curWb == 1;
        const float half = std::min(cfg.aim.fov * 0.5f, 180.f);
        const int lo = cfg.aim.bone == BONE_NEAREST ? 0 : cfg.aim.bone;
        const int hi = cfg.aim.bone == BONE_NEAREST ? BONE_NEAREST : cfg.aim.bone + 1;
        float best = 1e9f;
        const game::Player* pick = nullptr;
        for (const auto& p : snap) {
            if (p.local || !p.alive) continue;
            if (cfg.aim.team_check && p.ally) continue;
            if (p.obj == g_deadObj && GetTickCount() < g_deadUntil) continue;
            if (cfg.aim.wall_check && !p.vis && !wb) continue;
            const float d = Dist(p.pos, game::CamPosCache());
            if (d < 1.5f || d > cfg.aim.max_dist) continue; // under ~1.5u is us
            for (int b = lo; b < hi; b++) {
                const float ang = Angle(Norm(Sub(game::Bone(p, BoneFrac(b)), from)), fwd) * kRad;
                if (ang > half) continue;
                // sticky: hold the current target so it does not flick between two enemies
                const float score = (p.obj == g_lastObj) ? ang * 0.35f : ang;
                if (score < best) { best = score; pick = &p; }
            }
        }
        if (!pick) { g_lastObj = nullptr; return false; }
        // snapshot positions are up to 33 ms old; re-read the one we aim at
        game::Player live = *pick;
        if (!game::Refresh(live)) { g_lastObj = nullptr; return false; }
        float bs = 1e9f;
        Vec3 pt = game::Bone(live, BoneFrac(lo));
        for (int b = lo; b < hi; b++) {
            const Vec3 c = game::Bone(live, BoneFrac(b));
            const float a = Angle(Norm(Sub(c, from)), fwd);
            if (a < bs) { bs = a; pt = c; }
        }
        aim = pt;
        name = live.name;
        g_lastObj = live.obj;
        return true;
    }

    void Learn(uintptr_t ret, bool all) {
        if (ret < g_shotLo || ret >= g_shotHi) return;
        void* w = game::LocalWeapon();
        if (!w) return;
        const int bit = all ? 1 : 2;
        std::lock_guard<std::mutex> g(g_mtx);
        int& m = g_seen[w];
        if (!(m & bit)) {
            m |= bit;
            Log("[rage] weapon %p first shot via %s  (seen mask %d)", w, all ? "RaycastAll" : "Raycast", m);
        }
    }

    // ---- silent aim ------------------------------------------------------------------
    // Game thread only (Step and the hooks below both run on it), except the UI copy.
    game::Player g_sPick{};
    float g_sFrac = 1.f;
    bool g_sHave = false;
    Vec3 g_sUi{};
    bool g_sUiOk = false;     // guarded by g_tmtx
    bool g_sUiHidden = false; // the pick is behind a wall (wallbang)
    std::atomic<int> g_sShots{ 0 };

    // The game's shot ray: Physics.Raycast(Ray, out RaycastHit, float, int).
    struct RayT { Vec3 o, d; };
    using RayRFn = bool(__fastcall*)(RayT*, void*, float, int, void*);
    RayRFn oRayR = nullptr;

    // Range and layer mask of a shot ray that really hit an enemy, so a test between shots asks
    // the same question the game does.
    bool g_pHave = false;
    float g_pMd = 1000.f;
    int g_pMask = -5;
    void* g_pMi = nullptr;
    bool g_sLand = false; // the shown pick was tested with a real ray and will be hit
    std::atomic<int> g_stShot{ 0 }, g_stOk{ 0 }, g_stNone{ 0 }, g_stHit{ 0 }, g_stRet{ 0 }, g_stDmgAll{ 0 };

    struct Cand { game::Player p; float frac; };

    // Shots through walls are deliberate: the toggle must be on and, unless turned off, its key held.
    bool WallbangOn() { return cfg.silent.wallbang && (!cfg.silent.wall_hold || input::Down(cfg.silent.wall_key)); }

    // Every enemy inside the cone with its best hitbox, best first. Visible ones always come before
    // hidden ones, and hidden ones only count with wallbang. Game thread.
    std::vector<Cand> SilentCandidates() {
        const Vec3 from = game::CamPos();
        const Vec3 fwd = game::Forward(game::CamRotation());
        const float half = std::min(cfg.silent.fov * 0.5f, 180.f);
        const int lo = cfg.silent.bone == BONE_NEAREST ? 0 : cfg.silent.bone;
        const int hi = cfg.silent.bone == BONE_NEAREST ? BONE_NEAREST : cfg.silent.bone + 1;
        std::vector<std::pair<float, Cand>> v;
        for (const auto& p : game::Snapshot()) {
            if (p.local || !p.alive) continue;
            if (cfg.aim.team_check && p.ally) continue;
            if (p.obj == g_deadObj && GetTickCount() < g_deadUntil) continue;
            if (!p.vis && !WallbangOn()) continue; // behind a wall: only with wallbang
            const float d = Dist(p.pos, game::CamPosCache());
            if (d < 1.5f || d > cfg.aim.max_dist) continue;
            float bestAng = 1e9f, bestFrac = 1.f;
            for (int b = lo; b < hi; b++) {
                const float ang = Angle(Norm(Sub(game::Bone(p, BoneFrac(b)), from)), fwd) * kRad;
                if (ang < bestAng) { bestAng = ang; bestFrac = BoneFrac(b); }
            }
            if (bestAng <= half) v.push_back({ bestAng + (p.vis ? 0.f : 1000.f), { p, bestFrac } });
        }
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<Cand> out;
        out.reserve(v.size());
        for (auto& e : v) out.push_back(std::move(e.second));
        return out;
    }

    // Does this exact query, aimed at `aim`, end inside enemy `p`? It is the query the game is about
    // to run, with the same range and mask, so a yes means the shot lands. RaycastHit starts with
    // its hit point; the point has to be in the enemy's column and near the aim point, which
    // rules out a wall in front of it and a wall behind it.
    bool Lands(const RayT& base, float len, const game::Player& p, const Vec3& aim, float md, int mask, void* mi) {
        alignas(16) uint8_t hit[128]{};
        const Vec3 dir = Norm(Sub(aim, base.o));
        RayT t{ base.o, { dir.x * len, dir.y * len, dir.z * len } };
        if (!oRayR(&t, hit, md, mask, mi)) return false;
        const Vec3 hp = *reinterpret_cast<const Vec3*>(hit);
        const float dx = hp.x - p.pos.x, dz = hp.z - p.pos.z;
        return dx * dx + dz * dz < 1.44f && Dist(hp, aim) < 1.5f;
    }

    // First hitbox of `c` that a ray from base.o lands in: the chosen one, then the others. With
    // wallbang a second pass masks the ray to the enemy's own layers so the world does not block it.
    bool Solve(const RayT& base, float len, Cand& c, float md, int mask, void* mi, Vec3& aim, int& useMask) {
        if (!game::Refresh(c.p)) return false;
        static const float kTry[] = { 0.82f, 0.68f, 0.93f, 0.52f, 1.0f, 0.3f };
        float fr[7];
        int n = 0;
        fr[n++] = c.frac;
        for (const float f : kTry) if (fabsf(f - c.frac) > 0.04f) fr[n++] = f;
        for (int pass = 0; pass < (WallbangOn() ? 2 : 1); pass++) {
            const int m = pass ? (mask & game::LayerBits(c.p)) : mask;
            if (!m || (pass && m == mask)) break;
            for (int i = 0; i < n; i++) {
                const Vec3 a = game::Bone(c.p, fr[i]);
                if (Lands(base, len, c.p, a, md, m, mi)) { aim = a; useMask = m; return true; }
            }
        }
        return false;
    }

    // Best enemy first, the first one a ray from base.o can really land in. Capped: every try is a raycast.
    bool Pick(std::vector<Cand>& c, const RayT& base, float len, float md, int mask, void* mi, Cand& out, Vec3& aim, int& useMask) {
        for (size_t i = 0; i < c.size() && i < 4; i++)
            if (Solve(base, len, c[i], md, mask, mi, aim, useMask)) { out = c[i]; return true; }
        return false;
    }

    void SilentUpdate() {
        g_sHave = false;
        g_sLand = false;
        game::Player* l = game::Local();
        if (cfg.panic || farm::SuppressFeatures() || !cfg.silent.enabled || !l || (cfg.silent.hold && !input::Down(cfg.silent.key))) {
            std::lock_guard<std::mutex> g(g_tmtx);
            g_sUiOk = false;
            return;
        }
        auto c = SilentCandidates();
        Cand pick{};
        Vec3 aim{};
        if (!c.empty()) {
            g_sHave = true;
            pick = c[0];
            aim = game::Bone(pick.p, pick.frac);
            if (g_pHave && oRayR) { // show the enemy a ray from here would actually land in
                const RayT base{ game::CamPos(), game::Forward(game::CamRotation()) };
                Cand got{};
                Vec3 a{};
                int m = 0;
                if (Pick(c, base, 1.f, g_pMd, g_pMask, g_pMi, got, a, m)) { pick = got; aim = a; g_sLand = true; }
            }
            g_sPick = pick.p;
            g_sFrac = pick.frac;
        }
        std::lock_guard<std::mutex> g(g_tmtx);
        g_sUiOk = g_sHave;
        if (g_sHave) { g_sUi = aim; g_sUiHidden = !pick.p.vis; }
    }

    // Presses and releases the left button for auto shoot. Non-blocking (called every frame
    // from the game thread). Only ever releases a press it made itself.
    bool g_autoDown = false;
    DWORD g_autoT = 0;
    void MouseLeft(bool down) {
        if (down && !input::CanAutoClick()) return;
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(in));
        g_autoDown = down;
        g_autoT = GetTickCount();
    }
    void AutoFire(bool want) {
        if (want && !input::CanAutoClick()) {
            static DWORD lastBlocked = 0;
            if (GetTickCount() - lastBlocked >= 5000) {
                lastBlocked = GetTickCount();
                Log("[input] auto-fire blocked: cursor outside foreground game client, focus changed, or Alt/Win held");
            }
            want = false;
        }
        if (!want) { if (g_autoDown) MouseLeft(false); return; }
        if (cfg.silent.auto_fire == 1) { if (!g_autoDown) MouseLeft(true); return; } // hold: full-auto weapons
        const DWORD now = GetTickCount();                                            // click: semi-auto weapons
        if (g_autoDown) { if (now - g_autoT >= 45) MouseLeft(false); }
        else if (now - g_autoT >= 55) MouseLeft(true);
    }


    float Wrap(float a) { while (a > 180.f) a -= 360.f; while (a < -180.f) a += 360.f; return a; }

    void Rotate(void* tr, float x, float y, float z) {
        if (Ok(tr) && g_fnRotate)
            reinterpret_cast<void(*)(void*, float, float, float, void*)>(g_fnRotate)(tr, x, y, z, nullptr);
    }

    // ---- silent aim: turn, let the game fire, turn back -------------------------------
    // While fire is held and a target is in the cone, the body yaw and camera pitch are turned
    // at the target with the same Rotate calls the aimbot uses, the game runs its own update
    // (which fires), and the exact opposite turn is applied afterwards. Relative turns mean any
    // mouse look the game applies in between is kept, and the view ends where it began.
    // Wrapped: the local Player_move_c.Update and the local weapon's Update (fire logic may
    // live in either); nested calls share one snap.
    bool g_snapped = false;
    int g_wrapDepth = 0;
    float g_snapY = 0.f, g_snapP = 0.f;
    void* g_snapTr = nullptr;
    void* g_snapCam = nullptr;
    std::atomic<int> g_nEligible{ 0 }; // wrapped updates with fire held and a target in the cone

    void Snap() {
        if (g_snapped || !g_sHave || !g_fnRotate || render::MenuOpen || farm::SuppressFeatures() || cfg.silent.method != 1) return;
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return; // read live: input::Down is a frame behind
        game::Player* l = game::Local();
        void* cam = game::CamTransform();
        game::Player live = g_sPick;
        if (!l || !Ok(cam) || !Ok(l->tr) || !game::Refresh(live)) return;
        g_nEligible++;
        const Vec3 from = game::CamPos();
        const Vec3 fwd = game::Forward(game::CamRotation());
        const Vec3 d = Norm(Sub(game::Bone(live, g_sFrac), from));
        g_snapY = Wrap((atan2f(d.x, d.z) - atan2f(fwd.x, fwd.z)) * kRad);
        g_snapP = (asinf(std::max(-1.f, std::min(1.f, -d.y))) - asinf(std::max(-1.f, std::min(1.f, -fwd.y)))) * kRad;
        g_snapTr = l->tr;
        g_snapCam = cam;
        Rotate(g_snapTr, 0.f, g_snapY, 0.f);
        Rotate(g_snapCam, g_snapP, 0.f, 0.f);
        g_snapped = true;
        if (g_sShots++ < 5) Log("[silent] turned %.0f yaw %.0f pitch toward %s", g_snapY, g_snapP, live.name.c_str());
    }

    void Unsnap() {
        if (!g_snapped) return;
        g_snapped = false;
        Rotate(g_snapCam, -g_snapP, 0.f, 0.f);
        Rotate(g_snapTr, 0.f, -g_snapY, 0.f);
    }

    // ---- silent aim (ray): point the game's own shot rays at the target -----------------
    // A normal weapon fires through Physics.Raycast(Ray, out RaycastHit, float, int), called from
    // several Player_move_c functions and a coroutine (the probe's caller list). Wrapping single
    // update functions only caught some of those shots; hooking the call itself covers every
    // caller, works at any angle, and replaces the weapon's spread with an exact direction.
    constexpr uintptr_t kCoRva = 0x27533B7; // coroutine MoveNext that raycasts while firing (build of 2026-11-19)
    uintptr_t g_coLo = 0, g_coHi = 0;
    std::mutex g_cmtx;
    std::unordered_map<uintptr_t, bool> g_callers;

    // Is this return address inside Player_move_c (or the coroutine)? Cached per address.
    bool ShotCaller(uintptr_t ret) {
        {
            std::lock_guard<std::mutex> g(g_cmtx);
            const auto it = g_callers.find(ret);
            if (it != g_callers.end()) return it->second;
        }
        uintptr_t lo = 0, hi = 0;
        std::string nm;
        const bool ok = (ret >= g_coLo && ret < g_coHi) || (probe::MethodRange(ret, lo, hi, nm) && nm.rfind("Player_move_c::", 0) == 0);
        std::lock_guard<std::mutex> g(g_cmtx);
        g_callers[ret] = ok;
        static int logged = 0;
        if (ok && logged++ < 8) Log("[silent] ray caller rva=%llX %s", static_cast<unsigned long long>(ret - probe::GaBase()), nm.c_str());
        return ok;
    }

    // The button counts as held for a moment after release: a tapped or delayed shot fires its ray
    // a little after the click, and it must still be ours.
    DWORD g_lmbT = 0;
    bool FireHeld() {
        const DWORD now = GetTickCount();
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) { g_lmbT = now; return true; } // live: input::Down is a frame behind
        return now - g_lmbT < 200;
    }

    // Only our own shot: from Player_move_c code, starting at us, pointing roughly where the camera
    // looks, while fire is held. Bots, pets, the UI and other players start elsewhere.
    bool MyShot(RayT* r, uintptr_t ret) {
        if (cfg.panic || !(cfg.silent.enabled || cfg.sound.hit || cfg.sound.kill) || !Ok(r)) return false;
        if (!FireHeld() || !ShotCaller(ret)) return false;
        game::Player* l = game::Local();
        if (!l || Dist(r->o, l->pos) > 5.f) return false;
        if (r->d.x * r->d.x + r->d.y * r->d.y + r->d.z * r->d.z < 1e-6f) return false;
        return Angle(Norm(r->d), game::Forward(game::CamRotation())) * kRad <= 35.f;
    }

    // Point the ray at the first enemy it can really land in. False = nothing could be hit, the
    // shot is left exactly as the game made it.
    bool Redirect(RayT* r, float md, int& mask, void* mi) {
        const float len = sqrtf(r->d.x * r->d.x + r->d.y * r->d.y + r->d.z * r->d.z);
        auto c = SilentCandidates();
        Cand got{};
        Vec3 aim{};
        int m = mask;
        if (!Pick(c, *r, len, md, mask, mi, got, aim, m)) return false;
        const Vec3 dir = Norm(Sub(aim, r->o));
        r->d = { dir.x * len, dir.y * len, dir.z * len };
        if (g_sShots++ < 6) Log("[silent] ray sent to %s at %.1fm%s", got.p.name.c_str(), Dist(r->o, aim), m != mask ? " (through the world)" : "");
        hitsound::NoteTarget(got.p.obj); // a kill within 2 s of this counts as yours when the game's own kill clip is unavailable
        // The ray exactly as the game is about to run it must still end where the test said; this is
        // the check that explains a send that never turns into a hit.
        static int s_chk = 0;
        if (s_chk < 8) {
            s_chk++;
            alignas(16) uint8_t h2[128]{};
            const bool ok2 = oRayR(r, h2, md, m, mi);
            const Vec3 hp2 = *reinterpret_cast<const Vec3*>(h2);
            Log("[silent] final ray check: returns %d, ends %.1f/%.1f/%.1f, aim %.1f/%.1f/%.1f (%.2fm apart), mask %08X, range %.0f",
                ok2, hp2.x, hp2.y, hp2.z, aim.x, aim.y, aim.z, Dist(hp2, aim), static_cast<unsigned>(m), md);
        }
        mask = m;
        return true;
    }

    // A ray that ends in an enemy is NOT a hit: the game also casts rays for aim and effects, and
    // every one of them lands once the shot is redirected. The game's own ApplyDamage (below) is the
    // proof of a hit. This only remembers the last ray that ended in an enemy, so that when damage
    // follows in the same moment the range and mask of the damaging query can be learned.
    struct LastRay { void* obj = nullptr; float md = 0; int mask = 0; void* mi = nullptr; DWORD t = 0; } g_lastRay;

    void NoteShotResult(const void* hit, float md, int mask0, void* mi) {
        const Vec3 hp = *reinterpret_cast<const Vec3*>(hit);
        bool haveClosest = false;
        std::string cName;
        Vec3 cPos{};
        float nearD = 1e9f;
        for (const auto& p : game::Snapshot()) {
            if (p.local || !p.alive || p.ally) continue;
            const float dx = hp.x - p.pos.x, dz = hp.z - p.pos.z;
            const float top = (p.haveHead ? p.headPos.y : p.pos.y + 1.6f) + 0.6f;
            if (dx * dx + dz * dz < 1.44f && hp.y > p.pos.y - 0.6f && hp.y < top) {
                g_stHit++;
                g_lastRay = { p.obj, md, mask0, mi, GetTickCount() };
                return;
            }
            if (dx * dx + dz * dz < nearD) { nearD = dx * dx + dz * dz; haveClosest = true; cName = p.name; cPos = p.pos; }
        }
        // no enemy there: say what this ray ended on, a few times
        static int s_miss = 0;
        if (s_miss < 8) {
            s_miss++;
            Log("[silent] shot ray ended at %.1f/%.1f/%.1f; nearest enemy %s %.1f/%.1f/%.1f (%.1fm sideways)", hp.x, hp.y, hp.z,
                haveClosest ? cName.c_str() : "none", cPos.x, cPos.y, cPos.z, haveClosest ? sqrtf(nearD) : -1.f);
        }
    }

    // ---- hit confirmation: PlayerDamageable.ApplyDamage ---------------------------------
    // Your client calls ApplyDamage on an enemy's PlayerDamageable when one of your shots (or
    // explosions, or melee) lands; that is exactly the game registering a hit, so hit sounds follow it.
    using DmgFn = void*(__fastcall*)(void*, void*, void*, void*, void*, void*, void*, void*, void*, void*);
    DmgFn oDmg = nullptr;
    std::atomic<int> g_stDmg{ 0 };

    void OnDamage(void* self, void* second) {
        if (g_stDmgAll++ < 4) {
            const char* cn = nullptr;
            std::string n;
            if (il2::ClassName(self, n)) cn = n.c_str();
            Log("[silent] ApplyDamage called on %p (%s)", self, cn ? cn : "?");
        }
        for (const auto& p : game::Snapshot()) {
            // `second`: a method that returns a struct takes a hidden return pointer first
            if (!p.dmg || (p.dmg != self && p.dmg != second)) continue;
            if (p.local || p.ally) return;
            if (g_stDmg++ < 3) Log("[silent] damage applied to %s", p.name.c_str());
            hitsound::NoteHit(p.obj);
            if (g_lastRay.obj == p.obj && GetTickCount() - g_lastRay.t < 100) {
                g_pHave = true; g_pMd = g_lastRay.md; g_pMask = g_lastRay.mask; g_pMi = g_lastRay.mi;
            }
            return;
        }
    }
    void SafeOnDamage(void* self, void* second) { __try { OnDamage(self, second); } __except (EXCEPTION_EXECUTE_HANDLER) {} }

    // Forward first: the damage is passed in a float register that any code of ours would clobber.
    void* __fastcall hkDmg(void* self, void* a, void* b, void* c, void* d, void* e, void* f, void* g, void* h, void* i) {
        void* r = oDmg(self, a, b, c, d, e, f, g, h, i);
        SafeOnDamage(self, a);
        return r;
    }

    // An exception here would take the game down: a bad read costs this shot's redirect, nothing more.
    bool SafeRedirect(RayT* r, float md, int& mask, void* mi) {
        __try { return Redirect(r, md, mask, mi); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void SafeNote(const void* hit, float md, int mask0, void* mi) {
        __try { NoteShotResult(hit, md, mask0, mi); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool __fastcall hkRayR(RayT* ray, void* hit, float md, int mask, void* mi) {
        const bool mine = MyShot(ray, reinterpret_cast<uintptr_t>(_ReturnAddress()));
        const int mask0 = mask;
        if (mine) {
            g_stShot++;
            if (cfg.silent.enabled && cfg.silent.method == 0 && g_sHave && !render::MenuOpen && !farm::SuppressFeatures()) {
                if (SafeRedirect(ray, md, mask, mi)) g_stOk++; else g_stNone++;
            }
        }
        const bool r = oRayR(ray, hit, md, mask, mi);
        if (mine && r) g_stRet++;
        if (mine && r && Ok(hit)) SafeNote(hit, md, mask0, mi);
        return r;
    }

    void AimStep() {
        // Unity Rotate: +y turns right, +x looks down. The old auto-flip heuristic misread a
        // target that moved or died as a wrong sign and inverted the axis mid-fight, so the
        // convention is fixed.
        static void* trk = nullptr; // last frame's target
        static Vec3 trkAim{};

        game::Player* l = game::Local();
        void* cam = game::CamTransform();
        const bool active = !cfg.panic && cfg.aim.enabled && (!cfg.aim.hold || input::Down(cfg.aim.key));
        auto setValid = [](bool v) { std::lock_guard<std::mutex> g(g_tmtx); g_tValid = v; };
        if (!active || !l || !cam || !g_fnRotate) { trk = nullptr; g_lastObj = nullptr; setValid(false); return; }

        const DWORD now = GetTickCount();
        // our target died or despawned since last frame: drop it and pause
        if (trk) {
            bool gone = true, dead = false;
            for (const auto& p : game::Snapshot())
                if (p.obj == trk) { gone = false; dead = !p.alive; break; }
            if (gone || dead) { g_deadObj = trk; g_deadUntil = now + 2500; g_coolUntil = now + 400; trk = nullptr; g_lastObj = nullptr; }
        }
        if (now < g_coolUntil) { setValid(false); return; }

        Vec3 aim{};
        std::string name;
        const Vec3 from = game::CamPos();
        const Vec3 fwd = game::Forward(game::CamRotation());
        bool got = Choose(from, fwd, aim, name);
        // the same target teleporting is a death, ragdoll or respawn, not movement
        if (got && g_lastObj == trk && trk && Dist(aim, trkAim) > 6.f) {
            g_deadObj = trk; g_deadUntil = now + 2500; g_coolUntil = now + 400; trk = nullptr; g_lastObj = nullptr; got = false;
        }
        {
            std::lock_guard<std::mutex> g(g_tmtx);
            g_tValid = got;
            if (got) { g_tAim = aim; g_tName = name; }
        }
        if (!got) { trk = nullptr; return; }
        trk = g_lastObj;
        trkAim = aim;

        const Vec3 d = Norm(Sub(aim, from));
        const float yawErr = Wrap((atan2f(d.x, d.z) - atan2f(fwd.x, fwd.z)) * kRad);
        const float pitErr = (asinf(std::max(-1.f, std::min(1.f, -d.y))) - asinf(std::max(-1.f, std::min(1.f, -fwd.y)))) * kRad;

        const float k = std::max(1.f - cfg.aim.smooth, 0.05f);
        auto cap = [](float v) { return std::max(-45.f, std::min(45.f, v)); };
        Rotate(l->tr, 0.f, cap(yawErr * k), 0.f); // body yaw, the same call the game's own look code makes
        Rotate(cam, cap(pitErr * k), 0.f, 0.f);   // camera pitch, ditto
    }
}

void Init() {
    void* phys = il2::Class("Physics", "UnityEngine");
    g_fnRotate = il2::MethodBySig(il2::Class("Transform", "UnityEngine"), "Rotate", { "Single", "Single", "Single" });
    void* ray = il2::MethodBySig(phys, "Raycast", { "Vector3", "Vector3", "RaycastHit", "Single", "Int32" });
    void* all = il2::MethodBySig(phys, "RaycastAll", { "Vector3", "Vector3", "Single", "Int32" });

    // silent aim: hook the Ray overload that bullets use
    {
        std::string cn;
        if (!probe::MethodRange(probe::GaBase() + kCoRva, g_coLo, g_coHi, cn)) g_coLo = g_coHi = 0; // not that method: allow none
        void* rr = il2::MethodBySig(phys, "Raycast", { "Ray", "RaycastHit", "Single", "Int32" });
        const MH_STATUS s = rr ? MH_CreateHook(rr, &hkRayR, reinterpret_cast<void**>(&oRayR)) : MH_ERROR_NOT_CREATED;
        Log("[silent] Raycast(Ray) hook=%p create=%d enable=%d  coroutine=%s", rr, s, s == MH_OK ? MH_EnableHook(rr) : -1, g_coLo ? cn.c_str() : "NOT FOUND");
    }

    // hit sounds: the game's own damage call on an enemy
    {
        void* ad = il2::MethodPtr(il2::Class("PlayerDamageable"), "ApplyDamage", -1);
        const MH_STATUS s = ad ? MH_CreateHook(ad, &hkDmg, reinterpret_cast<void**>(&oDmg)) : MH_ERROR_NOT_CREATED;
        Log("[silent] PlayerDamageable.ApplyDamage hook=%p create=%d enable=%d", ad, s, s == MH_OK ? MH_EnableHook(ad) : -1);
    }

    std::string nm;
    const bool found = probe::MethodRange(probe::GaBase() + kShotRva, g_shotLo, g_shotHi, nm);
    Log("[rage] rotate=%p ray=%p all=%p shot method=%s", g_fnRotate, ray, all, found ? nm.c_str() : "NOT FOUND");
    // obfuscated names change every game build; the class is the only stable check
    if (!found || nm.rfind("Player_move_c::", 0) != 0) { Log("[rage] shot method not where expected: wall-break learning off"); return; }
    // The Raycast overloads and the shot method were hooked here before; a normal weapon never
    // reached them (zero calls in a whole match), so silent aim wraps the local update instead.
}

void Step() {
    g_sHave = false; // set again below; stays false while tabbed out or in the menu
    FireHeld();
    hitsound::Tick();
    if (farm::SuppressFeatures()) { AutoFire(false); return; }
    static DWORD s_hookLog = 0;
    if (GetTickCount() - s_hookLog > 5000) {
        s_hookLog = GetTickCount();
        static int lastTotal = -1;
        int clipCalls = 0, hitClips = 0, killClips = 0;
        hitsound::Stats(clipCalls, hitClips, killClips);
        const int total = g_nEligible + g_sShots + g_stShot + g_stDmgAll + hitClips + killClips;
        if (total != lastTotal) {
            lastTotal = total;
            Log("[silent] your shot rays=%d (returned true %d)  sent to a target that lands=%d  nothing could be hit=%d  ending in an enemy=%d  |  ApplyDamage calls=%d (on enemies %d)  |  game sounds seen=%d, hit clips=%d, kill clips=%d",
                g_stShot.load(), g_stRet.load(), g_stOk.load(), g_stNone.load(), g_stHit.load(), g_stDmgAll.load(), g_stDmg.load(),
                clipCalls, hitClips, killClips);
        }
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) { AutoFire(false); return; } // never rotate or click while tabbed out

    void* w = game::LocalWeapon();
    static void* s_lastW = nullptr;
    if (w != s_lastW) { s_lastW = w; g_pHave = false; } // another weapon may query with another range and mask
    {
        std::lock_guard<std::mutex> g(g_mtx);
        const auto it = w ? g_seen.find(w) : g_seen.end();
        const int m = it == g_seen.end() ? 0 : it->second;
        g_curWb = (m & 1) ? 1 : (m & 2) ? 0 : -1;
    }
    if (render::MenuOpen) { AutoFire(false); return; }
    SilentUpdate();
    // once the damaging query is known, only click when a ray from here would really land
    AutoFire(g_sHave && (g_sLand || !g_pHave || cfg.silent.method != 0) && cfg.silent.auto_fire != 0 && !cfg.panic);
    AimStep();
}

// Unload: never leave the left button held down.
void Release() { if (g_autoDown) MouseLeft(false); }

// Game thread. Called around the local Player_move_c.Update and the local weapon's Update.
void WrapBegin(void* self, bool weapon) {
    g_wrapDepth++;
    if (!g_sHave || !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return; // cheap exit for every other update
    game::Player* l = game::Local();
    if (!l) return;
    if (weapon ? self != game::LocalWeapon() : self != l->obj) return;
    Snap();
}

void WrapEnd() {
    if (--g_wrapDepth <= 0) { g_wrapDepth = 0; Unsnap(); }
}

bool SilentTarget(Vec3& world, bool* hidden) {
    std::lock_guard<std::mutex> g(g_tmtx);
    if (!g_sUiOk) return false;
    world = g_sUi;
    if (hidden) *hidden = g_sUiHidden;
    return true;
}

int SilentShots() { return g_sShots; }

bool Target(std::string& name, Vec3& world) {
    std::lock_guard<std::mutex> g(g_tmtx);
    if (!g_tValid) return false;
    name = g_tName;
    world = g_tAim;
    return true;
}

int WallBreak() { return g_curWb; }
}
