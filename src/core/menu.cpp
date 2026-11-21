#include "menu.h"
#include "config.h"
#include "render.h"
#include "../sdk/game.h"
#include "../features/features.h"
#include "../features/custommodel.h"
#include "../features/farm.h"
#include <algorithm>

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
        if (ImGui::Button(buf, { 60, 0 })) ImGui::OpenPopup(label);
        if (ImGui::BeginPopup(label)) {
            ImGui::TextUnformatted("press a key, esc cancels");
            for (int i = 1; i < 255; i++) {
                if (i == VK_ESCAPE) { if (input::Pressed(i)) ImGui::CloseCurrentPopup(); continue; }
                if (input::Pressed(i)) { *key = i; ImGui::CloseCurrentPopup(); break; }
            }
            ImGui::EndPopup();
        }
    }

    // ---- look ----------------------------------------------------------------------------
    static const ImVec4 kAccent{ 0.23f, 0.86f, 0.74f, 1.f };

    void ApplyStyle() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 12; s.ChildRounding = 9; s.FrameRounding = 7; s.GrabRounding = 7;
        s.PopupRounding = 8; s.ScrollbarRounding = 8; s.TabRounding = 7;
        s.WindowPadding = { 0, 0 }; s.FramePadding = { 10, 6 }; s.ItemSpacing = { 10, 10 };
        s.WindowBorderSize = 1; s.FrameBorderSize = 0; s.ScrollbarSize = 9; s.GrabMinSize = 12;
        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]       = { 0.065f, 0.068f, 0.085f, 0.97f };
        c[ImGuiCol_ChildBg]        = { 0.085f, 0.088f, 0.110f, 1.f };
        c[ImGuiCol_PopupBg]        = { 0.085f, 0.088f, 0.110f, 1.f };
        c[ImGuiCol_Border]         = { 1.f, 1.f, 1.f, 0.07f };
        c[ImGuiCol_Text]           = { 0.92f, 0.93f, 0.96f, 1.f };
        c[ImGuiCol_TextDisabled]   = { 0.50f, 0.52f, 0.60f, 1.f };
        c[ImGuiCol_FrameBg]        = { 0.125f, 0.130f, 0.165f, 1.f };
        c[ImGuiCol_FrameBgHovered] = { 0.160f, 0.168f, 0.210f, 1.f };
        c[ImGuiCol_FrameBgActive]  = { 0.190f, 0.200f, 0.250f, 1.f };
        c[ImGuiCol_Button]         = { 0.150f, 0.158f, 0.200f, 1.f };
        c[ImGuiCol_ButtonHovered]  = { 0.210f, 0.225f, 0.290f, 1.f };
        c[ImGuiCol_ButtonActive]   = { kAccent.x, kAccent.y, kAccent.z, 0.85f };
        c[ImGuiCol_SliderGrab]       = kAccent;
        c[ImGuiCol_SliderGrabActive] = { 0.50f, 0.64f, 1.f, 1.f };
        c[ImGuiCol_CheckMark]      = kAccent;
        c[ImGuiCol_Header]         = { kAccent.x, kAccent.y, kAccent.z, 0.25f };
        c[ImGuiCol_HeaderHovered]  = { kAccent.x, kAccent.y, kAccent.z, 0.40f };
        c[ImGuiCol_HeaderActive]   = { kAccent.x, kAccent.y, kAccent.z, 0.55f };
        c[ImGuiCol_Separator]      = { 1.f, 1.f, 1.f, 0.07f };
        c[ImGuiCol_ScrollbarBg]    = { 0, 0, 0, 0 };
        c[ImGuiCol_ScrollbarGrab]  = { 0.25f, 0.27f, 0.33f, 1.f };
    }

    // animated switch; returns true when it changed
    static bool Toggle(const char* label, bool* v) {
        ImGui::PushID(label);
        const float h = 20.f, w = 38.f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const bool changed = ImGui::InvisibleButton("##sw", { w, h });
        if (changed) *v = !*v;
        float& a = *ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("a"), *v ? 1.f : 0.f);
        a += ((*v ? 1.f : 0.f) - a) * std::min(1.f, ImGui::GetIO().DeltaTime * 16.f);
        auto* dl = ImGui::GetWindowDrawList();
        const ImVec4 off{ 0.20f, 0.21f, 0.27f, 1.f };
        ImVec4 col{ off.x + (kAccent.x - off.x) * a, off.y + (kAccent.y - off.y) * a, off.z + (kAccent.z - off.z) * a, 1.f };
        if (ImGui::IsItemHovered()) { col.x += 0.05f; col.y += 0.05f; col.z += 0.05f; }
        dl->AddRectFilled(p, { p.x + w, p.y + h }, ImGui::ColorConvertFloat4ToU32(col), h * 0.5f);
        dl->AddCircleFilled({ p.x + h * 0.5f + (w - h) * a, p.y + h * 0.5f }, h * 0.5f - 3.f, IM_COL32(255, 255, 255, 245));
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (h - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::TextUnformatted(label);
        ImGui::PopID();
        return changed;
    }

    static void Section(const char* t) {
        ImGui::Dummy({ 0, 2 });
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
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

    void Render() {
        static int tab = 0;
        static const char* names[] = { "Overview", "Aim & fire", "Triggerbot", "Visuals", "Camera & models", "Sounds", "Farm", "Debug", "Config", "Settings" };

        ImGui::SetNextWindowSize({ 880, 650 }, ImGuiCond_Once);
        ImGui::SetNextWindowSizeConstraints({720, 500}, {1400, 1000});
        ImGui::Begin("##vanta", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

        // header: draggable, title left, live status right
        const ImVec2 wp = ImGui::GetWindowPos();
        const float ww = ImGui::GetWindowWidth();
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(wp, { wp.x + ww, wp.y + 48 }, IM_COL32(18, 19, 25, 255), 12, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled({ wp.x, wp.y + 47 }, { wp.x + ww, wp.y + 48 }, IM_COL32(255, 255, 255, 14));
        dl->AddText({ wp.x + 20, wp.y + 15 }, ImGui::ColorConvertFloat4ToU32(kAccent), "VANTA");
        dl->AddText({ wp.x + 85, wp.y + 15 }, IM_COL32(150, 154, 170, 255), "0.1 preview");
        char st[96]{};
        if (!game::Ready()) {
            snprintf(st, sizeof(st), "not initialised - see pg3d_core.log");
        } else {
            const auto snap = game::Snapshot();
            int en = 0;
            for (auto& p : snap) if (!p.local && !p.ally && p.alive) en++;
            snprintf(st, sizeof(st), "%d players   %d enemies   %s", static_cast<int>(snap.size()), en,
                     game::Local() ? "in match" : "waiting for match");
        }
        const ImVec2 ts = ImGui::CalcTextSize(st);
        dl->AddText({ wp.x + ww - ts.x - 20, wp.y + 15 }, IM_COL32(150, 154, 170, 255), st);
        if (ImGui::IsMouseHoveringRect(wp, { wp.x + ww, wp.y + 48 }) && ImGui::IsMouseDragging(0) &&
            !ImGui::IsAnyItemActive())
            ImGui::SetWindowPos({ wp.x + ImGui::GetIO().MouseDelta.x, wp.y + ImGui::GetIO().MouseDelta.y });
        ImGui::SetCursorPos({ 0, 48 });

        // sidebar
        ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.075f, 0.078f, 0.098f, 1.f });
        ImGui::BeginChild("##side", { 174, 0 }, false);
        ImGui::SetCursorPos({ 12, 16 });
        for (int i = 0; i < 10; i++) {
            ImGui::SetCursorPosX(12);
            const bool sel = tab == i;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4{ kAccent.x, kAccent.y, kAccent.z, 0.22f } : ImVec4{ 0, 0, 0, 0 });
            ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4{ 1, 1, 1, 1 } : ImVec4{ 0.62f, 0.65f, 0.74f, 1.f });
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, { 0.f, 0.5f });
            if (ImGui::Button(names[i], { 150, 38 })) tab = i;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (sel) ImGui::GetWindowDrawList()->AddRectFilled({ p.x, p.y + 7 }, { p.x + 3, p.y + 27 }, ImGui::ColorConvertFloat4ToU32(kAccent), 2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // content
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0, 0, 0, 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 22, 16 });
        ImGui::BeginChild("##content", { 0, 0 }, false, ImGuiWindowFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(kAccent, "%s", names[tab]);
        ImGui::Separator();
        ImGui::Spacing();
        switch (tab) {
        case 0:
            Section("YOUR SESSION");
            ImGui::TextWrapped("Welcome to Vanta. Pick a section on the left, or start with a shortcut below.");
            ImGui::Spacing();
            ImGui::Text("Game connection: %s", game::Ready() ? "ready" : "initializing");
            ImGui::Text("Player: %s", game::Local() ? "in a match" : "waiting for a match");
            ImGui::Text("Farm: %s", farm::Active() ? "running" : "stopped");
            Section("QUICK START");
            if (ImGui::Button("Set up Farm", {190, 38})) tab = 6;
            ImGui::SameLine();
            if (ImGui::Button("Camera & models", {190, 38})) tab = 4;
            Hint("Farm starts only when you press Start farm. Close this menu and keep the game focused while it runs.");
            Section("CONTROLS");
            ImGui::Text("%s  Open / close menu", VkName(cfg.menu_key));
            ImGui::Text("%s  Pause features and hide overlays", VkName(cfg.panic_key));
            ImGui::TextUnformatted("END  Unload Vanta");
            Hint("Settings save when the menu closes. Camera models are experimental and use static meshes.");
            break;
        case 1: TabAimbot(); break;
        case 2: TabTrigger(); break;
        case 3: TabVisuals(); break;
        case 4: TabMisc(); break;
        case 5: TabSounds(); break;
        case 6: TabFarm(); break;
        case 7: TabDebug(); break;
        case 8: TabConfig(); break;
        default: TabSettings(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::End();
    }
}
