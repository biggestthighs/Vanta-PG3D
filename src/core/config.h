#pragma once
#include "common.h"
#include "imgui.h"

// Aim points as a fraction of the measured span: 0 = feet, 1 = head collider centre.
enum AimBone { BONE_HEAD = 0, BONE_NECK, BONE_CHEST, BONE_BODY, BONE_PELVIS, BONE_NEAREST, BONE_COUNT };

inline float BoneFrac(int b) {
    static const float f[] = { 1.00f, 0.93f, 0.82f, 0.68f, 0.52f };
    return f[(b >= 0 && b < BONE_NEAREST) ? b : 0];
}
inline const char* BoneName(int b) {
    static const char* n[] = { "head", "neck", "chest", "body", "pelvis", "nearest" };
    return n[(b >= 0 && b < BONE_COUNT) ? b : 0];
}

struct AimCfg {
    bool enabled = false;
    int key = VK_XBUTTON2;
    bool hold = true;
    float fov = 60.f;   // cone in degrees around the crosshair
    float smooth = 0.2f;
    bool wall_check = true;
    bool team_check = true;
    bool predict = false;
    float max_dist = 300.f;
    bool draw_fov = true;
    int bone = BONE_HEAD;
    bool auto_sens = true; // measure px -> mouse counts in flight instead of guessing it
    float sens = 1.f;      // user multiplier on top of the measured scale
};

// Silent aim: your view never moves. When your own shot fires, its ray is pointed at the
// chosen target instead (rage.cpp, inside the shot method only).
struct SilentCfg {
    bool enabled = false;
    bool hold = false;       // false = always on while enabled
    int key = VK_XBUTTON2;
    float fov = 90.f;        // cone in degrees around the crosshair; 360 = anywhere
    int bone = BONE_HEAD;
    int method = 0;          // 0 redirect the game's shot rays, 1 turn the character around the update (older)
    bool wallbang = false;   // shoot through walls: the test ray ignores the world. The server can see kills through walls: opt-in
    bool wall_hold = true;   // wallbang only while wall_key is held (on = rare, deliberate use; off = whenever the toggle is on)
    int wall_key = VK_XBUTTON1;
    int auto_fire = 0;       // 0 off, 1 hold fire, 2 click (semi-auto weapons)
    bool draw_fov = true;    // ring on screen showing the cone
};

struct TrigCfg {
    bool enabled = false;
    int key = VK_XBUTTON1;
    bool hold = true;
    int delay = 40;
    float fov = 4.f;
    bool team_check = true;
    bool wall_check = true;
    int pen = 1; // 0 = always wait for a clear line, 1 = only when the weapon penetrates, 2 = never wait
};

struct VisCfg {
    bool enabled = true;
    bool boxes = true;
    bool names = true;
    bool dist = true;
    bool health = true;
    bool snaplines = false;
    float max_dist = 300.f;
    bool dim_hidden = true; // recolour players with no clear line (cfg.vis.hidden)
    float scale = 1.5f;     // text, lines and markers
    int style = 0;          // 0 corner box, 1 full box
    bool fill = true;       // translucent wash inside the box
    float fill_alpha = 0.16f;
    bool arrows = true;     // edge-of-screen pointers to enemies you can't see
    bool radar = true;
    float radar_range = 60.f;
    float radar_size = 170.f;
    ImVec4 enemy{ 1.f, 0.35f, 0.35f, 1.f };
    ImVec4 hidden{ 0.62f, 0.48f, 1.f, 0.9f };
    ImVec4 ally{ 0.45f, 0.75f, 1.f, 1.f };
};

// Files live in <dll folder>\sounds (16-bit PCM WAV), shared by both pickers.
struct SoundCfg {
    bool hit = false;
    bool kill = false;
    std::string hit_file = "bameware.wav";
    std::string kill_file = "fatality.wav";
    float hit_vol = 0.7f;
    float kill_vol = 0.8f;
};

struct MiscCfg {
    bool fov_changer = false;
    float fov_value = 100.f;
    bool no_recoil = false;
    bool no_spread = false;
    bool rapid_fire = false;
    bool infinite_ammo = false;
    bool full_auto = false;
    bool auto_heal = false;
    bool crosshair = true;
    bool bhop = false;
    bool third_person = false; // view only: the camera sits behind you and your character is drawn
    float tp_dist = 3.5f;
    float tp_height = 0.4f;
    int tp_model = 0;
    float tp_model_scale = 1.f;
    float tp_model_yaw = 0.f;
    float tp_model_height = 0.f;
};

struct FarmCfg {
    bool wallbang = false;
    int kill_limit = 3;
    float fov = 360.f;
    int fire_mode = 1;
};

struct Cfg {
    AimCfg aim;
    SilentCfg silent;
    TrigCfg trig;
    VisCfg vis;
    SoundCfg sound;
    MiscCfg misc;
    FarmCfg farm;
    int menu_key = VK_INSERT;
    int panic_key = VK_DELETE;
    bool panic = false; // runtime only: hides every overlay and pauses aimbot/triggerbot
};

extern Cfg cfg;

void ConfigLoad();
void ConfigSave();


std::vector<std::string> ConfigProfiles(std::string& error);
bool ConfigSaveProfile(const std::string& name, std::string& error);
bool ConfigLoadProfile(const std::string& name, std::string& error);
