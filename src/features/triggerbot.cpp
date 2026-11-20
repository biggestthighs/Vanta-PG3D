#include "features.h"
#include "../core/config.h"
#include "farm.h"
#include "../core/render.h"

namespace triggerbot {
    static DWORD s_next = 0;
    static int s_lastPen = -1;

    int PenState() { return s_lastPen; }

    // pen: 0 = always wait for a clear line, 1 = only wait when the weapon does not
    // penetrate, 2 = never wait. When the weapon field is missing (-1) mode 1 falls
    // back to waiting, which is the safe half of the guess.
    static bool MayShoot(const game::Player& p) {
        if (!cfg.trig.wall_check) return true;
        if (p.vis) return true;
        if (cfg.trig.pen >= 2) return true;
        if (cfg.trig.pen == 1) return s_lastPen == 1;
        return false;
    }

    // Press now, release a few frames later. A blocking Sleep here would stall Present
    // (and so the whole frame) for the full delay on every shot.
    static bool s_down = false;
    static DWORD s_upAt = 0;
    static void Mouse(bool down) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(in));
        s_down = down;
    }

    void Run() {
        if (s_down && (farm::SuppressFeatures() || !cfg.trig.enabled || cfg.panic || render::MenuOpen)) { Mouse(false); return; }
        if (s_down) { if (GetTickCount() >= s_upAt) Mouse(false); return; }
        if (!cfg.trig.enabled || cfg.panic || render::MenuOpen || farm::SuppressFeatures()) return;
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid != GetCurrentProcessId()) return; // never click while tabbed out
        if (cfg.trig.hold && !input::Down(cfg.trig.key)) return;
        if (GetTickCount() < s_next) return;

        // learned from the weapon's own shots (rage.cpp); the field guess is only a fallback
        const int wb = rage::WallBreak();
        s_lastPen = (cfg.trig.wall_check && cfg.trig.pen == 1) ? (wb >= 0 ? wb : game::WeaponPenetrates()) : -1;

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const ImVec2 mid{ disp.x * 0.5f, disp.y * 0.5f };
        const Vec3 cam = game::CamPosCache();
        const float gate = cfg.trig.fov * (disp.y / 1080.f);

        auto snap = game::Snapshot();
        for (auto& p : snap) {
            if (p.local || !p.alive) continue;
            if (cfg.trig.team_check && p.ally) continue;
            const float d = Dist(p.pos, cam);
            if (d < 1.5f || d > cfg.vis.max_dist) continue;
            if (!MayShoot(p)) continue;

            // sample up the body so the gate catches torso hits, not just one point
            for (float f = 0.15f; f <= 1.05f; f += 0.15f) {
                Vec3 s{};
                if (!game::W2S(game::Bone(p, f), s)) continue;
                s.y = disp.y - s.y;
                const float dx = s.x - mid.x, dy = s.y - mid.y;
                if (sqrtf(dx * dx + dy * dy) <= gate) {
                    Mouse(true);
                    s_upAt = GetTickCount() + 30;
                    s_next = s_upAt + static_cast<DWORD>(cfg.trig.delay);
                    return;
                }
            }
        }
    }
}
