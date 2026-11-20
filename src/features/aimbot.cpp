#include "features.h"
#include "../core/config.h"
#include <algorithm>
#include <cmath>

// The aim itself lives in rage.cpp (it rotates the game's own camera, no mouse input).
namespace aimbot {
    bool Target(std::string& name, float& px, float& py) {
        Vec3 w{}, s{};
        if (!rage::Target(name, w) || !game::W2S(w, s)) return false;
        px = s.x;
        py = ImGui::GetIO().DisplaySize.y - s.y;
        return true;
    }

    void DrawFov() {
        const ImVec2 d = ImGui::GetIO().DisplaySize;
        auto* dl = ImGui::GetBackgroundDrawList();

        // A cone of `deg` degrees around the crosshair is a circle on screen:
        // radius = tan(half angle) / tan(vertical fov / 2) * half the screen height.
        const float vf = game::CamFov() * 0.5f * 0.0174533f;
        auto ring = [&](float deg, ImU32 col) {
            const float half = std::min(deg, 360.f) * 0.5f * 0.0174533f;
            if (half >= 1.4f) return; // 80+ degrees: wider than any screen
            const float r = tanf(half) / tanf(vf) * d.y * 0.5f;
            if (r > 4.f) dl->AddCircle({ d.x * 0.5f, d.y * 0.5f }, r, col, 96, 1.4f);
        };
        if (cfg.aim.enabled && cfg.aim.draw_fov) ring(cfg.aim.fov, IM_COL32(255, 220, 90, 120));
        if (cfg.silent.enabled && cfg.silent.draw_fov) ring(cfg.silent.fov, IM_COL32(90, 220, 255, 120));

        std::string n; float x, y;
        if (cfg.aim.enabled && cfg.aim.draw_fov && Target(n, x, y))
            dl->AddLine({ d.x * 0.5f, d.y * 0.5f }, { x, y }, IM_COL32(255, 220, 90, 160), 1.2f);

        // where silent aim will send your next shot
        Vec3 w{}, s{};
        bool hidden = false;
        if (cfg.silent.enabled && rage::SilentTarget(w, &hidden) && game::W2S(w, s)) {
            const float r = 10.f * d.y / 1080.f;
            const ImVec2 c{ s.x, d.y - s.y };
            dl->AddCircle(c, r + 1, IM_COL32(0, 0, 0, 170), 24, 3.f);
            // cyan = clear line, orange = through a wall (wallbang)
            dl->AddCircle(c, r, hidden ? IM_COL32(255, 160, 60, 240) : IM_COL32(90, 220, 255, 235), 24, 1.6f);
        }
    }
}
