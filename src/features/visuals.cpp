#include "features.h"
#include <algorithm>
#include <cmath>
#include "../core/config.h"

namespace visuals {
    static ImU32 Col(ImVec4 c, float a = 1.f) { c.w *= a; return ImGui::ColorConvertFloat4ToU32(c); }

    static float g_fs = 17.f; // label font size this frame (menu font size x esp scale)

    static ImVec2 TextSize(const char* s) { return ImGui::GetFont()->CalcTextSizeA(g_fs, FLT_MAX, 0.f, s); }

    static void Label(ImDrawList* dl, ImVec2 p, ImU32 col, const char* s) {
        dl->AddText(ImGui::GetFont(), g_fs, { p.x + 1, p.y + 1 }, IM_COL32(0, 0, 0, 210), s);
        dl->AddText(ImGui::GetFont(), g_fs, p, col, s);
    }

    // text on a dark pill, centred on x
    static void Pill(ImDrawList* dl, float cx, float y, ImU32 col, const char* s) {
        const ImVec2 sz = TextSize(s);
        dl->AddRectFilled({ cx - sz.x * 0.5f - 5, y - 2 }, { cx + sz.x * 0.5f + 5, y + sz.y + 2 }, IM_COL32(10, 11, 16, 175), 5.f);
        Label(dl, { cx - sz.x * 0.5f, y }, col, s);
    }

    static void Corners(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float t) {
        const float L = std::min(b.x - a.x, b.y - a.y) * 0.28f;
        auto seg = [&](ImVec2 p, ImVec2 q) {
            dl->AddLine(p, q, IM_COL32(0, 0, 0, 170), t + 2.f);
            dl->AddLine(p, q, col, t);
        };
        seg(a, { a.x + L, a.y });          seg(a, { a.x, a.y + L });
        seg({ b.x, a.y }, { b.x - L, a.y }); seg({ b.x, a.y }, { b.x, a.y + L });
        seg({ a.x, b.y }, { a.x + L, b.y }); seg({ a.x, b.y }, { a.x, b.y - L });
        seg(b, { b.x - L, b.y });          seg(b, { b.x, b.y - L });
    }

    void Draw() {
        if (!cfg.vis.enabled) return;
        auto snap = game::Snapshot();
        auto* dl = ImGui::GetBackgroundDrawList();
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float k = disp.y / 1080.f;
        const float ks = k * cfg.vis.scale; // everything that belongs to a player scales with this
        g_fs = ImGui::GetFontSize() * cfg.vis.scale;
        const Vec3 cam = game::CamPosCache();

        // Camera heading on the ground plane: forward = (fx,fz), right = (fz,-fx).
        // Feeds the radar and the off-screen arrows, so neither needs W2S.
        const Vec3 f = game::Forward(game::CamRotation());
        float fl = sqrtf(f.x * f.x + f.z * f.z);
        const bool haveHeading = fl > 1e-3f;
        if (!haveHeading) fl = 1.f;
        const float fx = f.x / fl, fz = f.z / fl;

        const float R = cfg.vis.radar_size * k * 0.5f;
        const ImVec2 rc{ 24.f * k + R, disp.y * 0.30f };
        const float rscale = R / std::max(cfg.vis.radar_range, 5.f);
        if (cfg.vis.radar && haveHeading) {
            dl->AddCircleFilled(rc, R, IM_COL32(10, 11, 16, 150), 48);
            dl->AddCircle(rc, R, IM_COL32(255, 255, 255, 40), 48, 1.5f);
            dl->AddCircle(rc, R * 0.5f, IM_COL32(255, 255, 255, 18), 40, 1.f);
            dl->AddLine({ rc.x - R, rc.y }, { rc.x + R, rc.y }, IM_COL32(255, 255, 255, 14));
            dl->AddLine({ rc.x, rc.y - R }, { rc.x, rc.y + R }, IM_COL32(255, 255, 255, 14));
            dl->AddTriangleFilled({ rc.x, rc.y - 6 * k }, { rc.x - 4 * k, rc.y + 4 * k }, { rc.x + 4 * k, rc.y + 4 * k },
                                  IM_COL32(255, 255, 255, 220));
        }

        for (auto& p : snap) {
            if (p.local || !p.alive) continue;
            const float d = Dist(p.pos, cam);
            if (cfg.vis.max_dist > 0 && d > cfg.vis.max_dist) continue;

            const bool hidden = !p.vis && !p.ally && cfg.vis.dim_hidden;
            const ImVec4 base = p.ally ? cfg.vis.ally : (hidden ? cfg.vis.hidden : cfg.vis.enemy);
            const ImU32 col = Col(base);

            // ground-plane offset in camera space: x right, y forward
            const float dx = p.pos.x - cam.x, dz = p.pos.z - cam.z;
            const float rx = dx * fz - dz * fx, ry = dx * fx + dz * fz;

            if (cfg.vis.radar && haveHeading) {
                float ox = rx * rscale, oy = -ry * rscale;
                const float ol = sqrtf(ox * ox + oy * oy);
                float a = 1.f;
                if (ol > R - 4 * k) { const float s = (R - 4 * k) / ol; ox *= s; oy *= s; a = 0.6f; }
                dl->AddCircleFilled({ rc.x + ox, rc.y + oy }, (p.ally ? 3.f : 4.5f) * ks, Col(base, a));
            }

            // Box edges come off the measured head collider, slightly past it at the top
            // and just under the root at the bottom, so it stays centred without tuning.
            Vec3 head{}, feet{};
            bool on = game::W2S(game::Bone(p, 1.14f), head) && game::W2S(game::Bone(p, -0.03f), feet);
            if (on) {
                // Unity WorldToScreenPoint: y=0 at bottom; flip to DX11 overlay: y=0 at top
                head.y = disp.y - head.y;
                feet.y = disp.y - feet.y;
                const float cx = head.x, cy = (head.y + feet.y) * 0.5f;
                on = cx > 0 && cx < disp.x && cy > 0 && cy < disp.y;
            }

            if (!on) {
                if (cfg.vis.arrows && !p.ally && haveHeading) {
                    float dxn = rx, dyn = -ry;
                    const float l = sqrtf(dxn * dxn + dyn * dyn);
                    if (l > 1e-3f) {
                        dxn /= l; dyn /= l;
                        const float rad = std::min(disp.x, disp.y) * 0.36f;
                        const ImVec2 c{ disp.x * 0.5f + dxn * rad, disp.y * 0.5f + dyn * rad };
                        const float a = std::clamp(1.f - d / std::max(cfg.vis.max_dist, 1.f) * 0.6f, 0.35f, 1.f);
                        const ImVec2 tip{ c.x + dxn * 13 * ks, c.y + dyn * 13 * ks };
                        const ImVec2 b1{ c.x - dxn * 4 * ks - dyn * 9 * ks, c.y - dyn * 4 * ks + dxn * 9 * ks };
                        const ImVec2 b2{ c.x - dxn * 4 * ks + dyn * 9 * ks, c.y - dyn * 4 * ks - dxn * 9 * ks };
                        dl->AddTriangleFilled(tip, b1, b2, Col(base, a));
                        dl->AddTriangle(tip, b1, b2, IM_COL32(0, 0, 0, 160), 1.5f);
                    }
                }
                continue;
            }

            const float h0 = feet.y - head.y;
            if (h0 < 2.f) continue;
            // far players shrink to a few pixels: keep a minimum box, centred where they are
            const float h = std::max(h0, 46.f * ks);
            const float midY = (head.y + feet.y) * 0.5f;
            const float w = h * 0.42f;
            const float x = head.x - w * 0.5f;
            const ImVec2 a{ x, midY - h * 0.5f }, b{ x + w, midY + h * 0.5f };

            // translucent vertical wash: stands in for chams without touching game materials
            if (cfg.vis.fill)
                dl->AddRectFilledMultiColor(a, b, Col(base, cfg.vis.fill_alpha), Col(base, cfg.vis.fill_alpha),
                                            Col(base, cfg.vis.fill_alpha * 0.2f), Col(base, cfg.vis.fill_alpha * 0.2f));
            if (cfg.vis.boxes) {
                if (cfg.vis.style == 0) Corners(dl, a, b, col, 2.f * ks);
                else {
                    dl->AddRect({ a.x - 1, a.y - 1 }, { b.x + 1, b.y + 1 }, IM_COL32(0, 0, 0, 170), 2.f, 0, 2.f * ks + 1.f);
                    dl->AddRect(a, b, col, 2.f, 0, 2.f * ks);
                }
            }
            if (cfg.vis.health && p.haveHp) {
                const float hp = std::clamp(p.hp, 0.f, 100.f) / 100.f;
                const float bx = x - 7 * ks, bw = 4.f * ks;
                dl->AddRectFilled({ bx - 1, a.y - 1 }, { bx + bw + 1, b.y + 1 }, IM_COL32(0, 0, 0, 180), 2.f);
                const ImU32 hc = IM_COL32(static_cast<int>(235 - 150 * hp), static_cast<int>(70 + 160 * hp), 90, 240);
                dl->AddRectFilled({ bx, b.y - h * hp }, { bx + bw, b.y }, hc, 2.f);
            }
            if (cfg.vis.snaplines)
                dl->AddLine({ disp.x * 0.5f, disp.y }, { head.x, feet.y }, Col(base, 0.7f), 1.f);

            if (cfg.vis.names && !p.name.empty()) {
                Pill(dl, head.x, a.y - g_fs - 8 * ks, col, p.name.c_str());
            }
            if (cfg.vis.dist) {
                char buf[32]{};
                snprintf(buf, sizeof(buf), "%dm", static_cast<int>(d));
                Pill(dl, head.x, b.y + 4 * ks, IM_COL32(215, 218, 228, 235), buf);
            }
        }
    }
}
