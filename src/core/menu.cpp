#include "menu.h"
#include "atmosphere.h"
#include "../features/world.h"
#include "config.h"
#include "render.h"
#include "../sdk/game.h"
#include "../features/features.h"
#include "../features/custommodel.h"
#include "../features/farm.h"
#include <algorithm>
#include <array>

namespace menu {
    static const char* VkName(int vk) {
        switch (vk) {
        case VK_LBUTTON:  return "MB1";
        case VK_RBUTTON:  return "MB2";
        case VK_MBUTTON:  return "MB3";
        case VK_XBUTTON1: return "MB4";
        case VK_XBUTTON2: return "MB5";
        case VK_BACK:     return "BACK";
        case VK_TAB:      return "TAB";
        case VK_RETURN:   return "ENTER";
        case VK_SHIFT:    return "SHIFT";
        case VK_CONTROL:  return "CTRL";
        case VK_MENU:     return "ALT";
        case VK_ESCAPE:   return "ESC";
        case VK_SPACE:    return "SPACE";
        case VK_PRIOR:    return "PGUP";
        case VK_NEXT:     return "PGDN";
        case VK_END:      return "END";
        case VK_HOME:     return "HOME";
        case VK_LEFT:     return "LEFT";
        case VK_UP:       return "UP";
        case VK_RIGHT:    return "RIGHT";
        case VK_DOWN:     return "DOWN";
        case VK_INSERT:   return "INS";
        case VK_DELETE:   return "DEL";
        case VK_LWIN:     return "LWIN";
        case VK_RWIN:     return "RWIN";
        case VK_LSHIFT:   return "LSHIFT";
        case VK_RSHIFT:   return "RSHIFT";
        case VK_LCONTROL: return "LCTRL";
        case VK_RCONTROL: return "RCTRL";
        case VK_LMENU:    return "LALT";
        case VK_RMENU:    return "RALT";
        case VK_CAPITAL:  return "CAPS";
        case VK_PAUSE:    return "PAUSE";
        case VK_NUMLOCK:  return "NLCK";
        case VK_SCROLL:   return "SCRL";
        case VK_MULTIPLY: return "NUM*";
        case VK_ADD:      return "NUM+";
        case VK_SUBTRACT: return "NUM-";
        case VK_DECIMAL:  return "NUM.";
        case VK_DIVIDE:   return "NUM/";
        default:
            static char s[8]{};
            if (vk >= '0' && vk <= '9') { s[0] = static_cast<char>(vk); s[1] = 0; return s; }
            if (vk >= 'A' && vk <= 'Z') { s[0] = static_cast<char>(vk); s[1] = 0; return s; }
            if (vk >= VK_F1 && vk <= VK_F24) { snprintf(s, sizeof(s), "F%d", vk - VK_F1 + 1); return s; }
            if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { snprintf(s, sizeof(s), "NP%d", vk - VK_NUMPAD0); return s; }
            snprintf(s, sizeof(s), "0x%02X", vk);
            return s;
        }
    }

    static void KeyRow(const char* label, int* key) {
        ImGui::Text("%s", label);
        ImGui::SameLine(150);
        char buf[32]{};
        snprintf(buf, sizeof(buf), "%s", VkName(*key));
        ImGui::PushID(label);
        if (ImGui::Button(buf, { 60, 0 })) ImGui::OpenPopup(label);
        if (ImGui::BeginPopup(label)) {
            ImGui::TextUnformatted("press a key, esc cancels");
            for (int i = 1; i < 255; i++) {
                if (i == VK_ESCAPE) { if (input::Pressed(i)) ImGui::CloseCurrentPopup(); continue; }
                if (input::Pressed(i)) { *key = i; ImGui::CloseCurrentPopup(); break; }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    static const ImVec4 kAccent{.72f,.63f,.99f,1.f};
    static const ImVec4 kMint{.49f,.91f,.80f,1.f};
    static ImFont *titleFont = nullptr, *labelFont = nullptr, *smallFont = nullptr;

    void LoadFonts() {
        auto& io = ImGui::GetIO();
        io.FontDefault = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 16.f);
        titleFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeuib.ttf", 30.f);
        labelFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeuib.ttf", 16.f);
        smallFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 13.f);
    }

    void ApplyStyle() {
        auto& s = ImGui::GetStyle();
        s.WindowRounding = 16; s.ChildRounding = 12; s.FrameRounding = 8; s.GrabRounding = 8;
        s.PopupRounding = 10; s.ScrollbarRounding = 8; s.TabRounding = 8;
        s.WindowPadding = {0, 0}; s.FramePadding = {11, 7}; s.ItemSpacing = {11, 9};
        s.WindowBorderSize = 1.f; s.ChildBorderSize = 1.f; s.FrameBorderSize = 0;
        s.ScrollbarSize = 10; s.GrabMinSize = 11;
        auto* c = s.Colors;
        c[ImGuiCol_WindowBg] = {.059f,.052f,.088f,1.f};
        c[ImGuiCol_ChildBg] = {.089f,.076f,.127f,1.f};
        c[ImGuiCol_PopupBg] = {.092f,.078f,.139f,1.f};
        c[ImGuiCol_Border] = {.51f,.45f,.70f,.27f};
        c[ImGuiCol_Text] = {.94f,.92f,.98f,1.f};
        c[ImGuiCol_TextDisabled] = {.68f,.64f,.76f,1.f};
        c[ImGuiCol_FrameBg] = {.132f,.112f,.192f,1.f};
        c[ImGuiCol_FrameBgHovered] = {.181f,.154f,.256f,1.f};
        c[ImGuiCol_FrameBgActive] = {.224f,.181f,.311f,1.f};
        c[ImGuiCol_Button] = {.165f,.135f,.236f,1.f};
        c[ImGuiCol_ButtonHovered] = {.239f,.193f,.344f,1.f};
        c[ImGuiCol_ButtonActive] = {.316f,.240f,.454f,1.f};
        c[ImGuiCol_SliderGrab] = kAccent;
        c[ImGuiCol_SliderGrabActive] = kMint;
        c[ImGuiCol_CheckMark] = kMint;
        c[ImGuiCol_Header] = {.255f,.186f,.383f,1.f};
        c[ImGuiCol_HeaderHovered] = {.331f,.256f,.473f,1.f};
        c[ImGuiCol_HeaderActive] = {.390f,.290f,.558f,1.f};
        c[ImGuiCol_Separator] = {.49f,.41f,.67f,.22f};
        c[ImGuiCol_ScrollbarBg] = {0,0,0,0};
        c[ImGuiCol_ScrollbarGrab] = {.29f,.24f,.40f,1.f};
        c[ImGuiCol_NavCursor] = kMint;
    }

    // animated switch; returns true when it changed
    static bool Toggle(const char* label, bool* v) {
        ImGui::PushID(label);
        const float h = 24.f, w = 43.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rowWidth = std::clamp(ImGui::GetContentRegionAvail().x, 1.f, w + 18.f + ImGui::CalcTextSize(label).x);
        const bool changed = ImGui::InvisibleButton("##sw", {rowWidth, h}, ImGuiButtonFlags_EnableNav);
        if (changed) *v = !*v;
        float& a = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("a"), *v ? 1.f : 0.f);
        a += ((*v ? 1.f : 0.f) - a) * std::min(1.f, ImGui::GetIO().DeltaTime * 16.f);
        auto* dl = ImGui::GetWindowDrawList();
        const ImVec4 off{.22f,.18f,.32f,1.f};
        ImVec4 col{ off.x + (kAccent.x - off.x) * a, off.y + (kAccent.y - off.y) * a, off.z + (kAccent.z - off.z) * a, 1.f };
        if (ImGui::IsItemHovered()) { col.x += 0.05f; col.y += 0.05f; col.z += 0.05f; }
        dl->AddRectFilled(p, { p.x + w, p.y + h }, ImGui::GetColorU32(col), h * 0.5f);
        dl->AddCircleFilled({ p.x + h * 0.5f + (w - h) * a, p.y + h * 0.5f }, h * 0.5f - 3.f, ImGui::GetColorU32({1.f, .97f, 1.f, .96f}));
        dl->AddText({p.x + w + 12.f, p.y + (h - ImGui::GetTextLineHeight()) * .5f}, ImGui::GetColorU32(ImGuiCol_Text), label);
        ImGui::PopID();
        return changed;
    }

    static void Section(const char* t) {
        ImGui::Dummy({ 0, 2 });
        ImGui::PushFont(labelFont);
        ImGui::TextColored(kAccent, "%s", t);
        ImGui::PopFont();
        ImGui::Separator();
    }

    static void Hint(const char* t) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", t);
        ImGui::PopStyleColor();
    }

    static void BoneCombo(const char* label, int* bone) {
        const char* bones[BONE_COUNT]{};
        for (int i = 0; i < BONE_COUNT; i++) bones[i] = BoneName(i);
        ImGui::SetNextItemWidth(180);
        ImGui::Combo(label, bone, bones, BONE_COUNT);
    }

    static void TabAimbot() {
        Section("WEAPON / EXPERIMENTAL");
        ImGui::BeginDisabled(!game::RapidFireReady());
        Toggle("rapid fire", &cfg.misc.rapid_fire);
        ImGui::SetNextItemWidth(300);
        ImGui::SliderInt("fire rate multiplier", &cfg.misc.rapid_fire_multiplier, 1, 50, "%dx");
        ImGui::TextDisabled("1x Normal   |   2x, 3x...   |   50x Super fast (requested)");
        ImGui::EndDisabled();
        Hint(game::RapidFireReady() ? "Scales the effective cooldown and the newer weapon system's per-shot coroutine wait: 1x leaves it normal, 2x halves the delay, 3x divides it by three. This is a requested multiplier, not a measured firing rate. Hold fire for at least three seconds at 1x, then 50x: read-only ammo-rate diagnostics are written to pg3d_core.log. Other weapon timers or server rules can limit shots. Farm pauses this setting." : "Rapid fire unavailable: this build's effective cooldown method could not be verified.");
        Section("AIMBOT");
        Toggle("enabled", &cfg.aim.enabled);
        KeyRow("key", &cfg.aim.key);
        Toggle("hold to aim", &cfg.aim.hold);
        BoneCombo("hitbox", &cfg.aim.bone);
        Section("TUNING");
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("fov", &cfg.aim.fov, 5, 180, "%.0f deg");
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("smoothing", &cfg.aim.smooth, 0.f, 0.95f, "%.2f");
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("max distance", &cfg.aim.max_dist, 20, 600, "%.0f");
        Section("FILTERS");
        Toggle("wall check", &cfg.aim.wall_check);
        Toggle("team check", &cfg.aim.team_check);
        Toggle("target line", &cfg.aim.draw_fov);
        Hint("Drags the game's own crosshair, no mouse input. Smoothing 0 is a hard lock. First person only.");
        Section("SILENT AIM");
        Toggle("silent aim", &cfg.silent.enabled);
        Toggle("hold a key (off = always on)", &cfg.silent.hold);
        if (cfg.silent.hold) KeyRow("silent key", &cfg.silent.key);
        const char* sm[] = { "redirect shot rays (recommended)", "turn character (older)" };
        ImGui::SetNextItemWidth(260); ImGui::Combo("silent method", &cfg.silent.method, sm, 2);
        BoneCombo("silent hitbox", &cfg.silent.bone);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("silent fov", &cfg.silent.fov, 5, 360, "%.0f deg");
        Toggle("wallbang: shoot through walls (risky)", &cfg.silent.wallbang);
        if (cfg.silent.wallbang) {
            Toggle("only while a key is held", &cfg.silent.wall_hold);
            if (cfg.silent.wall_hold) KeyRow("wallbang key", &cfg.silent.wall_key);
        }
        const char* af[] = { "off", "hold fire (full-auto weapons)", "click (semi-auto weapons)" };
        ImGui::SetNextItemWidth(260); ImGui::Combo("auto shoot", &cfg.silent.auto_fire, af, 3);
        Toggle("draw fov ring", &cfg.silent.draw_fov);
        ImGui::Text("shots redirected  %d", rage::SilentShots());
        Hint("Redirects eligible shot rays toward a target in the selected cone. Uses the team filter and range above. Auto-fire requires the game to be focused and the OS cursor to be over its client area. Alt/Windows keys suspend automatic clicks.");
    }

    static void TabTrigger() {
        Section("TRIGGERBOT");
        Toggle("enabled", &cfg.trig.enabled);
        KeyRow("key", &cfg.trig.key);
        Toggle("hold to arm", &cfg.trig.hold);
        ImGui::SetNextItemWidth(260); ImGui::SliderInt("delay ms", &cfg.trig.delay, 0, 250);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("fov gate", &cfg.trig.fov, 1, 60, "%.0f px");
        Section("WALLS");
        Toggle("team check", &cfg.trig.team_check);
        Toggle("wall check", &cfg.trig.wall_check);
        const char* pen[] = { "off - always wait for a clear line", "auto - shoot through with Wall break",
                              "always - shoot through walls" };
        ImGui::SetNextItemWidth(300);
        ImGui::Combo("wall break", &cfg.trig.pen, pen, 3);
        const int ps = triggerbot::PenState();
        ImGui::Text("weapon has Wall break:  %s", ps == 1 ? "yes" : ps == 0 ? "no" : "not learned yet");
        Hint("Learned from the weapon's own shots: fire once with a weapon and it is remembered. Until then auto waits for a clear line.");
    }

    static void TabVisuals() {
        Section("ESP");
        Toggle("enabled", &cfg.vis.enabled);
        Toggle("boxes", &cfg.vis.boxes);
        const char* styles[] = { "corners", "full box" };
        ImGui::SetNextItemWidth(180); ImGui::Combo("box style", &cfg.vis.style, styles, 2);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("esp size", &cfg.vis.scale, 0.8f, 3.f, "%.1fx");
        Toggle("glow fill", &cfg.vis.fill);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("fill strength", &cfg.vis.fill_alpha, 0.02f, 0.5f, "%.2f");
        Toggle("names", &cfg.vis.names);
        Toggle("distance", &cfg.vis.dist);
        Toggle("health", &cfg.vis.health);
        Toggle("snaplines", &cfg.vis.snaplines);
        Toggle("recolour players behind cover", &cfg.vis.dim_hidden);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("max distance", &cfg.vis.max_dist, 20, 600, "%.0f");
        Section("AWARENESS");
        Toggle("off-screen arrows", &cfg.vis.arrows);
        Toggle("radar", &cfg.vis.radar);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("radar range", &cfg.vis.radar_range, 20, 200, "%.0f m");
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("radar size", &cfg.vis.radar_size, 100, 300, "%.0f px");
        Section("COLORS");
        ImGui::ColorEdit4("enemy", &cfg.vis.enemy.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("behind cover", &cfg.vis.hidden.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("ally", &cfg.vis.ally.x, ImGuiColorEditFlags_NoInputs);
        Hint(game::HeadTracked() ? "Box height is measured from the head collider." : "Box height is estimated (no head collider yet).");
    }

    static void TabMisc() {
        Section("VIEW");
        Toggle("crosshair", &cfg.misc.crosshair);
        Toggle("fov changer", &cfg.misc.fov_changer);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("fov value", &cfg.misc.fov_value, 60, 140, "%.0f");
        Section("THIRD PERSON / EXPERIMENTAL MODELS");
        Toggle("third person", &cfg.misc.third_person);
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("distance", &cfg.misc.tp_dist, 1.f, 8.f, "%.1f");
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("height", &cfg.misc.tp_height, -0.5f, 2.f, "%.1f");
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("character model", custommodel::Name(cfg.misc.tp_model))) {
            for (int i = 0; i < custommodel::Count; ++i) {
                if (ImGui::Selectable(custommodel::Name(i), cfg.misc.tp_model == i)) cfg.misc.tp_model = i;
                if (cfg.misc.tp_model == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (cfg.misc.tp_model > 0) {
            ImGui::SetNextItemWidth(260); ImGui::SliderFloat("model size", &cfg.misc.tp_model_scale, .25f, 3.f, "%.2fx");
            ImGui::SetNextItemWidth(260); ImGui::SliderFloat("model rotation", &cfg.misc.tp_model_yaw, -180.f, 180.f, "%.0f degrees");
            ImGui::SetNextItemWidth(260); ImGui::SliderFloat("model vertical offset", &cfg.misc.tp_model_height, -2.f, 2.f, "%.2f");
            Hint("Custom models are local visuals. These imports use a static pose and follow your position and facing.");
        }
        Hint("View only: the camera sits behind you and your character is drawn. Aim and shots still come from the first-person position, so the crosshair is slightly off at close range. It does not stop the camera clipping through walls.");
        Section("MOVEMENT");
        Toggle("bunny hop (hold space)", &cfg.misc.bhop);
    }

    // one sound block: on/off, which file, volume, preview, import
    static void SoundBlock(const char* title, bool kill, bool* on, std::string* file, float* vol) {
        static std::vector<std::string> list[2];
        static int seen[2] = { -1, -1 };
        const int k = kill ? 1 : 0;
        if (seen[k] != hitsound::ImportCount()) { list[k] = hitsound::Files(kill); seen[k] = hitsound::ImportCount(); }
        Section(title);
        ImGui::PushID(kill);
        Toggle("enabled", on);
        ImGui::SetNextItemWidth(360);
        if (ImGui::BeginCombo("file", file->empty() ? "(none)" : file->c_str())) {
            for (const auto& n : list[k])
                if (ImGui::Selectable(n.c_str(), n == *file)) { *file = n; hitsound::Preview(kill); }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(260); ImGui::SliderFloat("volume", vol, 0.f, 1.f, "%.2f");
        if (ImGui::Button("preview", { 90, 0 })) hitsound::Preview(kill);
        ImGui::SameLine();
        if (ImGui::Button("import wav", { 110, 0 })) hitsound::Import(kill);
        ImGui::SameLine();
        if (ImGui::Button("refresh", { 90, 0 })) { list[k] = hitsound::Files(kill); }
        ImGui::PopID();
    }

    static void TabSounds() {
        SoundBlock("HIT SOUND", false, &cfg.sound.hit, &cfg.sound.hit_file, &cfg.sound.hit_vol);
        SoundBlock("KILL SOUND", true, &cfg.sound.kill, &cfg.sound.kill_file, &cfg.sound.kill_vol);
        const std::string msg = hitsound::LastMessage();
        if (!msg.empty()) { ImGui::Dummy({ 0, 4 }); ImGui::TextUnformatted(msg.c_str()); }
        ImGui::Dummy({ 0, 6 });
        if (ImGui::Button("open sounds folder", { 170, 0 })) hitsound::OpenFolder();
        Hint("A hit plays when an enemy near where you aim loses health while you fire; a kill plays when one dies. Your own sounds: import a 16-bit PCM .wav, or drop it into the sounds folder and press refresh. Every sound in the folder can be picked for either.");
    }

    static void Clock(const char* label, double sec) {
        const int s = static_cast<int>(sec);
        ImGui::Text("%s  %02d:%02d:%02d", label, s / 3600, (s / 60) % 60, s % 60);
    }

    static void TabFarm() {
        const auto status = farm::GetStatus();
        Section("DEATHMATCH FARM");
        ImGui::TextWrapped("%s", status.message.c_str());
        ImGui::SliderInt("kills per match", &cfg.farm.kill_limit, 0, 20);
        Toggle("wallbang (shoot through walls)", &cfg.farm.wallbang);
        Hint("Uses the existing wallbang mode while Farm is fighting, without holding a key. It stops at the kill limit. Server and weapon rules can still limit hits.");
        ImGui::SliderFloat("farm aim cone", &cfg.farm.fov, 1.f, 360.f, "%.0f degrees");
        int fireMode = cfg.farm.fire_mode - 1;
        if (ImGui::Combo("weapon fire", &fireMode, "hold (automatic)\0click (semi-auto)\0")) cfg.farm.fire_mode = fireMode + 1;
        if (status.active) {
            ImGui::Text("This match: %d / %d kills", status.kills, cfg.farm.kill_limit);
            const float progress = cfg.farm.kill_limit > 0 ? std::clamp(float(status.kills) / cfg.farm.kill_limit, 0.f, 1.f) : 1.f;
            ImGui::ProgressBar(progress, {-1, 16}, status.combat ? "Getting kills" : "Assistance paused");
            ImGui::Text("completed: %d matches, %d kills", status.completed, status.totalKills);
            Clock("farm time    ", status.seconds);
            if (ImGui::Button("stop farm", {140, 0})) farm::Stop();
        } else if (ImGui::Button("start farm", {140, 0})) farm::Start();
        ImGui::SameLine();
        if (ImGui::Button("inspect game buttons", {180, 0})) farm::Inspect();
        Hint("Start from Deathmatch or the lobby, then close this menu. Farm uses silent aim and auto-fire until the kill limit, then switches gameplay cheats off until the next match. Set zero kills for an idle run. Rewards, replay and respawn use recognized game controls. Your normal settings return when you stop. Keep the game focused.");
        ImGui::Dummy({0, 6});
        Section("REWARDS THIS RUN");
        if (ImGui::BeginTable("##farm-rewards",4,ImGuiTableFlags_SizingStretchProp)) {
            for (const char* heading : {"Reward","Match","Progress Road","Total"}) ImGui::TableSetupColumn(heading);
            ImGui::TableHeadersRow();
            for (const auto& total : status.rewards) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextWrapped("%s%s%s",total.kind.c_str(),total.item.empty()?"":" / ",total.item.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%lld",static_cast<long long>(total.match));
                ImGui::TableNextColumn(); ImGui::Text("%lld",static_cast<long long>(total.progress));
                ImGui::TableNextColumn(); ImGui::Text("%lld",static_cast<long long>(total.match+total.progress));
            }
            ImGui::EndTable();
        }
        if (status.rewards.empty()) ImGui::TextDisabled("Waiting for recognized rewards");
        if (status.unreadRewards) ImGui::TextWrapped("%d reward screens had unrecognized amounts. Totals may be incomplete.",status.unreadRewards);
        Hint("Counts recognized rewards after their claim screen closes. Resets when you start a new Farm run. Available-for-purchase notices are excluded.");
        Section("THIS SESSION");
        Clock("session time ", render::SessionSec);
        Clock("in a match   ", render::MatchSec);
        const double share = render::SessionSec > 1 ? render::MatchSec / render::SessionSec * 100.0 : 0.0;
        ImGui::Text("time in matches  %.0f%%", share);
        Hint("Menus and queues don't earn XP, so this is the number to push up. Low means you're waiting more than playing.");
    }

    static void TabDebug() {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const Vec3 cam = game::CamPosCache();
        Section("STATE");
        ImGui::Text("display   %.0f x %.0f", disp.x, disp.y);
        ImGui::Text("cam pos   %.2f %.2f %.2f", cam.x, cam.y, cam.z);
        ImGui::Text("local     %s", game::Local() ? "yes" : "no");
        const auto snap = game::Snapshot();
        ImGui::Text("tracked   %d", static_cast<int>(snap.size()));
        const game::Player* best = nullptr;
        float bd = 1e9f;
        for (auto& p : snap) {
            if (p.local || !p.alive) continue;
            const float d = Dist(p.pos, cam);
            if (d > 1.5f && d < bd) { bd = d; best = &p; }
        }
        Section("NEAREST ENEMY");
        if (!best) {
            ImGui::TextUnformatted("none tracked");
        } else {
            ImGui::Text("%s  %.1fm   hp %.0f%s", best->name.c_str(), bd, best->hp, best->haveHp ? "" : " (none)");
            ImGui::Text("head      %s", best->haveHead ? "measured" : "estimated");
            ImGui::Text("visible   %s", best->vis ? "yes" : "behind cover");
            ImGui::Text("ally      %s", best->ally ? "yes" : "no");
        }
        std::string tn; float tx = 0, ty = 0;
        if (aimbot::Target(tn, tx, ty)) ImGui::Text("aim target %s", tn.c_str());
        else ImGui::TextUnformatted("aim target none");
    }

    static void TabConfig() {
        static char name[49] = "My config";
        static std::string selected, message;
        static std::vector<std::string> profiles;
        static bool refresh = true;
        if (refresh) {
            std::string error;
            profiles = ConfigProfiles(error);
            if (!error.empty()) message = error;
            refresh = false;
        }
        Section("SAVED CONFIGURATIONS");
        Hint("Save separate presets for your settings. Configurations are stored in the configs folder beside Vanta.");
        if (ImGui::BeginListBox("##configs", {-1, 170})) {
            for (const auto& profile : profiles) {
                if (ImGui::Selectable(profile.c_str(), selected == profile)) {
                    selected = profile;
                    snprintf(name, sizeof(name), "%s", profile.c_str());
                }
            }
            ImGui::EndListBox();
        }
        if (profiles.empty()) ImGui::TextDisabled("No saved configurations yet.");
        ImGui::InputText("config name", name, sizeof(name));
        const bool exists = std::find(profiles.begin(), profiles.end(), name) != profiles.end();
        if (ImGui::Button(exists ? "Overwrite config" : "Save config", {160, 0})) {
            std::string error;
            if (ConfigSaveProfile(name, error)) {
                selected = name;
                message = "Saved " + selected + ".";
                refresh = true;
            } else message = error;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(selected.empty() || farm::Active());
        if (ImGui::Button("Load selected", {140, 0})) {
            std::string error;
            message = ConfigLoadProfile(selected, error) ? "Loaded " + selected + "." : error;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh", {90, 0})) refresh = true;
        if (farm::Active()) Hint("Stop Farm before loading a configuration. Saving is available while Farm runs.");
        if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
        Hint("Closing the menu also saves your current settings for the next launch. Named presets change only when you save or overwrite them.");
    }

    static void TabSettings() {
        Section("MENU");
        KeyRow("menu key", &cfg.menu_key);
        KeyRow("panic key", &cfg.panic_key);
        Hint("Panic hides the menu and every overlay and pauses aimbot and triggerbot until you press it again.");
        if (ImGui::Button("save", { 90, 0 })) ConfigSave();
        ImGui::SameLine();
        ImGui::BeginDisabled(farm::Active());
        if (ImGui::Button("reload", { 90, 0 })) ConfigLoad();
        ImGui::EndDisabled();
        Hint("END unloads. The log is pg3d_core.log next to the dll. Settings save when the menu closes.");
    }

    enum Page { Overview, World, Visuals, Camera, Sounds, Aim, Trigger, Farm, Config, Settings, Debug, PageCount };
    constexpr const char* names[] = {"Overview", "World", "Visuals", "Camera & models", "Sounds",
        "Aim & fire", "Triggerbot", "Farm", "Config", "Settings", "Debug"};
    static_assert(IM_ARRAYSIZE(names) == PageCount);

    static void Banner(const char* heading, const char* subtitle, const WorldCfg& options, float height) {
        const auto position = ImGui::GetCursorScreenPos();
        const ImVec2 size{ImGui::GetContentRegionAvail().x, height};
        auto& draw = *ImGui::GetWindowDrawList();
        atmosphere::Preview(draw,position,size,options,ImGui::GetTime());
        const ImVec2 right{position.x+size.x, position.y+height};
        draw.AddRectFilledMultiColor(position,{position.x+size.x*.82f,right.y},
            IM_COL32(15,12,29,250),IM_COL32(15,12,29,0),IM_COL32(15,12,29,0),IM_COL32(15,12,29,249));
        draw.AddRect(position,right,IM_COL32(171,151,230,85),8.f);
        draw.AddText(titleFont ? titleFont : ImGui::GetFont(),titleFont ? titleFont->FontSize : ImGui::GetFontSize(),
            {position.x+21,position.y+height*.34f},IM_COL32(246,241,255,255),heading);
        draw.AddText({position.x+22,position.y+height*.34f+36.f},IM_COL32(205,193,230,255),subtitle);
        ImGui::Dummy(size);
    }

    static void TabWorld() {
        auto& options = cfg.world;
        Banner(atmosphere::Colors(options.palette).name, atmosphere::Colors(options.palette).description, options, ImGui::GetWindowHeight() < 530.f ? 116.f : 152.f);
        Toggle("Enable atmosphere in matches", &options.enabled);
        const float width = (ImGui::GetContentRegionAvail().x - 2.f*ImGui::GetStyle().ItemSpacing.x) / 3.f;
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            if (options.palette == i) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
            const bool selected = options.palette == i;
            if (ImGui::Button(atmosphere::Colors(i).name, {width, 33.f})) {
                options = {};
                options.palette = i;
                options.enabled = true;
                options.fogDensity = i == 2 ? .040f : .028f;
            }
            if (selected) ImGui::PopStyleColor();
        }
        if (ImGui::BeginTable("##world-controls", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            Section("SCENE");
            Toggle("Panoramic sky", &options.sky);
            ImGui::BeginDisabled(!options.sky);
            Toggle("Stars", &options.stars);
            ImGui::EndDisabled();
            Toggle("Distance fog", &options.fog);
            ImGui::SetNextItemWidth(-1.f);
            ImGui::SliderFloat("##fog-density", &options.fogDensity, .003f, .070f, "Fog density  %.3f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::TableNextColumn();
            Section("AMBIENCE");
            Toggle("World particles", &options.particles);
            Toggle("Lighting tint", &options.lighting);
            Hint("Particles drift at different depths. Walls and props can hide them.");
            ImGui::SetNextItemWidth(-1.f);
            ImGui::SliderFloat("##weather-intensity", &options.intensity, 0.f, 1.f, "Effect strength  %.2f",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndTable();
        }
        if (ImGui::BeginTable("##world-status",2,ImGuiTableFlags_SizingStretchSame)) {
            auto readout = [](const char* name, const char* status) {
                ImGui::TableNextColumn();
                ImGui::PushTextWrapPos();
                ImGui::TextDisabled("%s  %s",name,status);
                ImGui::PopTextWrapPos();
            };
            readout("Sky",world::SkyStatus());
            readout("Particles",world::ParticleStatus());
            readout("Fog",world::FogStatus());
            readout("Lighting",world::LightingStatus());
            ImGui::EndTable();
        }
        if (ImGui::Button("Reset atmosphere")) options = {};
        ImGui::SameLine();
        if (ImGui::Button("Save settings")) ConfigSave();
    }

    static void TabOverview(int& page) {
        Banner("Make the map yours", "Sky, weather and atmosphere", cfg.world, 189.f);
        if (ImGui::Button("Open World", {142,36})) page = World;
        ImGui::SameLine();
        if (ImGui::Button("Saved configs", {142,36})) page = Config;
        Section("SESSION");
        if (ImGui::BeginTable("##session", 3, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Game");
            ImGui::TextUnformatted(game::Local() ? "In a match" : "Waiting");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("World");
            ImGui::TextUnformatted(cfg.world.enabled ? atmosphere::Colors(cfg.world.palette).name : "Map defaults");
            ImGui::TableNextColumn();
            ImGui::TextDisabled("Farm");
            ImGui::TextUnformatted(farm::Active() ? "Running" : "Stopped");
            ImGui::EndTable();
        }
        Section("SHORTCUTS");
        ImGui::Text("%s   Menu", VkName(cfg.menu_key));
        ImGui::Text("%s   Panic", VkName(cfg.panic_key));
        ImGui::TextUnformatted("END   Unload");
    }

    static void NavItem(int& page, Page item, float width) {
        ImGui::PushID(item);
        const bool selected = page == item;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4{.234f,.175f,.344f,1.f} : ImVec4{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4{.96f,.93f,1,1} : ImVec4{.74f,.69f,.84f,1});
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0,.5f});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {18,4});
        const auto p = ImGui::GetCursorScreenPos();
        if (ImGui::Button(names[item], {width,29})) page = item;
        if (selected) ImGui::GetWindowDrawList()->AddRectFilled({p.x+4,p.y+7},{p.x+7,p.y+22},ImGui::GetColorU32(kAccent),2);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }

    static void NavGroup(const char* heading, int& page, std::initializer_list<Page> entries) {
        ImGui::SetCursorPosX(17);
        ImGui::PushFont(smallFont);
        ImGui::TextDisabled("%s", heading);
        ImGui::PopFont();
        for (Page entry : entries) {
            ImGui::SetCursorPosX(9);
            NavItem(page,entry,150);
        }
        ImGui::Dummy({0,5});
    }

    void Render() {
        static int page = Overview;
        const auto screen = ImGui::GetIO().DisplaySize;
        const ImVec2 maximum{std::max(400.f,screen.x-24.f), std::max(320.f,screen.y-24.f)};
        const ImVec2 minimum{std::min(682.f,maximum.x),std::min(455.f,maximum.y)};
        ImGui::SetNextWindowSize({std::min(939.f, maximum.x),std::min(686.f, maximum.y)},ImGuiCond_Once);
        ImGui::SetNextWindowPos({screen.x*.5f,screen.y*.5f},ImGuiCond_Once,{.5f,.5f});
        ImGui::SetNextWindowSizeConstraints(minimum,maximum);
        if (!ImGui::Begin("##vanta",nullptr,ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
            ImGui::End();
            return;
        }
        const auto p = ImGui::GetWindowPos();
        const float width = ImGui::GetWindowWidth();
        auto& draw = *ImGui::GetWindowDrawList();
        draw.AddRectFilled(p,{p.x+width,p.y+65},IM_COL32(26,21,42,255),16,ImDrawFlags_RoundCornersTop);
        draw.AddLine({p.x,p.y+65},{p.x+width,p.y+65},IM_COL32(132,108,185,79));
        const ImVec2 mark{p.x+34,p.y+33};
        draw.AddCircleFilled(mark,15,IM_COL32(83,58,128,255),24);
        draw.AddPolyline(std::array<ImVec2,3>{{{mark.x-6,mark.y-4},{mark.x,mark.y+5},{mark.x+6,mark.y-4}}}.data(),
            3,IM_COL32(226,208,255,255),0,2.1f);
        ImGui::SetCursorPos({58,15});
        ImGui::PushFont(titleFont);
        ImGui::TextUnformatted("Vanta");
        ImGui::PopFont();
        ImGui::SetCursorPos({width-120,17});
        if (ImGui::Button("Hide menu",{101,31})) {
            ConfigSave();
            render::MenuOpen = false;
        }
        if (ImGui::IsMouseHoveringRect(p,{p.x+width-136,p.y+65}) && ImGui::IsMouseDragging(0) && !ImGui::IsAnyItemActive())
            ImGui::SetWindowPos({p.x+ImGui::GetIO().MouseDelta.x,p.y+ImGui::GetIO().MouseDelta.y});
        ImGui::SetCursorPos({0,66.f});

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8,1});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,{.078f,.065f,.116f,1});
        ImGui::BeginChild("##navigation",{174,0},0,ImGuiWindowFlags_NoSavedSettings);
        ImGui::Dummy({0,13});
        NavGroup("PLAY",page,{Overview,World,Visuals,Camera,Sounds});
        NavGroup("CONTROLS",page,{Aim,Trigger,Farm});
        NavGroup("PREFERENCES",page,{Config,Settings,Debug});
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::SameLine(0,0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{24,17});
        ImGui::PushStyleColor(ImGuiCol_ChildBg,{0,0,0,0});
        ImGui::BeginChild(page+200,{0,0},ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PushFont(titleFont);
        ImGui::TextUnformatted(names[page]);
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
        switch (page) {
        case Overview: TabOverview(page); break;
        case World: TabWorld(); break;
        case Visuals: TabVisuals(); break;
        case Camera: TabMisc(); break;
        case Sounds: TabSounds(); break;
        case Aim: TabAimbot(); break;
        case Trigger: TabTrigger(); break;
        case Farm: TabFarm(); break;
        case Config: TabConfig(); break;
        case Settings: TabSettings(); break;
        case Debug: TabDebug(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::End();
    }
}
