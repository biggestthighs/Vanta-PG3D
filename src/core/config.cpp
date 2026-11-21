#include "config.h"
#include "json.hpp"
#include <algorithm>
#ifdef CORE_BUILD
#include "../features/farm.h"
#endif
#include <fstream>
#include <filesystem>

Cfg cfg;
static std::wstring Path() { return DllDir() + L"\\config.json"; }

static bool LoadFile(const std::wstring& path, std::string& error) {
    std::ifstream f(path);
    if (!f) { error = "Could not open configuration."; return false; }
    const Cfg previous = cfg;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) throw std::runtime_error("Configuration must be a JSON object.");
        auto& a = j["aim"]; auto& t = j["trig"]; auto& v = j["vis"]; auto& m = j["misc"];
        cfg.aim.enabled = a.value("enabled", cfg.aim.enabled);
        cfg.aim.key = a.value("key", cfg.aim.key);
        cfg.aim.hold = a.value("hold", cfg.aim.hold);
        cfg.aim.fov = a.value("fov", cfg.aim.fov);
        cfg.aim.smooth = a.value("smooth", cfg.aim.smooth);
        cfg.aim.wall_check = a.value("wall_check", cfg.aim.wall_check);
        cfg.aim.bone = a.value("bone", cfg.aim.bone);
        cfg.aim.auto_sens = a.value("auto_sens", cfg.aim.auto_sens);
        cfg.aim.team_check = a.value("team_check", cfg.aim.team_check);
        cfg.aim.predict = a.value("predict", cfg.aim.predict);
        cfg.aim.max_dist = a.value("max_dist", cfg.aim.max_dist);
        cfg.aim.draw_fov = a.value("draw_fov", cfg.aim.draw_fov);
        cfg.aim.sens = a.value("sens", cfg.aim.sens);
        if (j.contains("silent")) { // absent in configs saved before silent aim existed
            auto& s = j["silent"];
            cfg.silent.enabled = s.value("enabled", cfg.silent.enabled);
            cfg.silent.hold = s.value("hold", cfg.silent.hold);
            cfg.silent.key = s.value("key", cfg.silent.key);
            cfg.silent.fov = s.value("fov", cfg.silent.fov);
            cfg.silent.bone = s.value("bone", cfg.silent.bone);
            cfg.silent.method = s.value("method", cfg.silent.method);
            cfg.silent.wallbang = s.value("wallbang", cfg.silent.wallbang);
            cfg.silent.wall_hold = s.value("wall_hold", cfg.silent.wall_hold);
            cfg.silent.wall_key = s.value("wall_key", cfg.silent.wall_key);
            cfg.silent.auto_fire = s.value("auto_fire", cfg.silent.auto_fire);
            cfg.silent.draw_fov = s.value("draw_fov", cfg.silent.draw_fov);
        }
        if (j.contains("sound")) { // absent in configs saved before sounds existed
            auto& so = j["sound"];
            cfg.sound.hit = so.value("hit", cfg.sound.hit);
            cfg.sound.kill = so.value("kill", cfg.sound.kill);
            cfg.sound.hit_file = so.value("hit_file", cfg.sound.hit_file);
            cfg.sound.kill_file = so.value("kill_file", cfg.sound.kill_file);
            cfg.sound.hit_vol = so.value("hit_vol", cfg.sound.hit_vol);
            cfg.sound.kill_vol = so.value("kill_vol", cfg.sound.kill_vol);
        }
        if (j.contains("farm")) {
            const auto& farmSettings = j["farm"];
            cfg.farm.wallbang = farmSettings.value("wallbang", false);
            cfg.farm.kill_limit = std::clamp(farmSettings.value("kill_limit", 3), 0, 100);
            cfg.farm.fov = std::clamp(farmSettings.value("fov", 360.f), 1.f, 360.f);
            cfg.farm.fire_mode = std::clamp(farmSettings.value("fire_mode", 1), 1, 2);
        }
        cfg.world = {};
        if (j.contains("world")) {
            const auto& w = j.at("world");
            cfg.world.enabled = w.value("enabled", false);
            cfg.world.sky = w.value("sky", true);
            cfg.world.stars = w.value("stars", true);
            cfg.world.fog = w.value("fog", true);
            cfg.world.particles = w.value("particles", true);
            cfg.world.lighting = w.value("lighting", true);
            cfg.world.palette = std::clamp(w.value("palette", 0), 0, 2);
            cfg.world.fogDensity = std::clamp(w.value("fogDensity", .028f), .003f, .070f);
            cfg.world.intensity = std::clamp(w.value("intensity", .90f), 0.f, 1.f);
        }
        cfg.trig.enabled = t.value("enabled", cfg.trig.enabled);
        cfg.trig.key = t.value("key", cfg.trig.key);
        cfg.trig.hold = t.value("hold", cfg.trig.hold);
        cfg.trig.delay = t.value("delay", cfg.trig.delay);
        cfg.trig.fov = t.value("fov", cfg.trig.fov);
        cfg.trig.team_check = t.value("team_check", cfg.trig.team_check);
        cfg.trig.wall_check = t.value("wall_check", cfg.trig.wall_check);
        cfg.trig.pen = t.value("pen", cfg.trig.pen);
        cfg.vis.enabled = v.value("enabled", cfg.vis.enabled);
        cfg.vis.boxes = v.value("boxes", cfg.vis.boxes);
        cfg.vis.names = v.value("names", cfg.vis.names);
        cfg.vis.dist = v.value("dist", cfg.vis.dist);
        cfg.vis.health = v.value("health", cfg.vis.health);
        cfg.vis.snaplines = v.value("snaplines", cfg.vis.snaplines);
        cfg.vis.max_dist = v.value("max_dist", cfg.vis.max_dist);
        cfg.vis.dim_hidden = v.value("dim_hidden", cfg.vis.dim_hidden);
        cfg.vis.style = v.value("style", cfg.vis.style);
        cfg.vis.scale = v.value("scale", cfg.vis.scale);
        cfg.vis.fill = v.value("fill", cfg.vis.fill);
        cfg.vis.fill_alpha = v.value("fill_alpha", cfg.vis.fill_alpha);
        cfg.vis.arrows = v.value("arrows", cfg.vis.arrows);
        cfg.vis.radar = v.value("radar", cfg.vis.radar);
        cfg.vis.radar_range = v.value("radar_range", cfg.vis.radar_range);
        cfg.vis.radar_size = v.value("radar_size", cfg.vis.radar_size);
        cfg.panic_key = j.value("panic_key", cfg.panic_key);
        cfg.misc.fov_changer = m.value("fov_changer", cfg.misc.fov_changer);
        cfg.misc.fov_value = m.value("fov_value", cfg.misc.fov_value);
        cfg.misc.no_recoil = m.value("no_recoil", cfg.misc.no_recoil);
        cfg.misc.no_spread = m.value("no_spread", cfg.misc.no_spread);
        cfg.misc.rapid_fire = m.value("rapid_fire", cfg.misc.rapid_fire);
        cfg.misc.rapid_fire_multiplier = std::clamp(m.value("rapid_fire_multiplier", 2), 1, 50);
        cfg.misc.infinite_ammo = m.value("infinite_ammo", cfg.misc.infinite_ammo);
        cfg.misc.full_auto = m.value("full_auto", cfg.misc.full_auto);
        cfg.misc.auto_heal = m.value("auto_heal", cfg.misc.auto_heal);
        cfg.misc.crosshair = m.value("crosshair", cfg.misc.crosshair);
        cfg.misc.bhop = m.value("bhop", cfg.misc.bhop);
        cfg.misc.third_person = m.value("third_person", cfg.misc.third_person);
        cfg.misc.tp_dist = m.value("tp_dist", cfg.misc.tp_dist);
        cfg.misc.tp_height = m.value("tp_height", cfg.misc.tp_height);
        cfg.misc.tp_model = m.value("tp_model", cfg.misc.tp_model);
        cfg.misc.tp_model_scale = m.value("tp_model_scale", cfg.misc.tp_model_scale);
        cfg.misc.tp_model_yaw = m.value("tp_model_yaw", cfg.misc.tp_model_yaw);
        cfg.misc.tp_model_height = m.value("tp_model_height", cfg.misc.tp_model_height);
        cfg.menu_key = j.value("menu_key", cfg.menu_key);
        Log("[cfg] loaded");
        return true;
    } catch (const std::exception& e) {
        cfg = previous;
        error = std::string("Could not load configuration: ") + e.what();
        Log("[cfg] parse failed; settings unchanged");
        return false;
    }
}

void ConfigLoad() {
    std::string error;
    if (!LoadFile(Path(), error)) Log("[cfg] %s", error.c_str());
}

static bool SaveFile(const std::wstring& path, std::string& error) {
    try {
    Cfg settings = cfg;
#ifdef CORE_BUILD
    farm::SettingsToSave(settings);
#endif
    const auto& cfg = settings;
    nlohmann::json j;
    j["world"] = {{"enabled", cfg.world.enabled}, {"sky", cfg.world.sky}, {"stars", cfg.world.stars},
                  {"fog", cfg.world.fog}, {"particles", cfg.world.particles}, {"lighting", cfg.world.lighting},
                  {"palette", cfg.world.palette},
                  {"fogDensity", cfg.world.fogDensity}, {"intensity", cfg.world.intensity}};
    j["farm"] = {{"wallbang", cfg.farm.wallbang}, {"kill_limit", cfg.farm.kill_limit}, {"fov", cfg.farm.fov}, {"fire_mode", cfg.farm.fire_mode}};
    j["aim"] = { {"enabled", cfg.aim.enabled}, {"key", cfg.aim.key}, {"hold", cfg.aim.hold}, {"fov", cfg.aim.fov},
                 {"smooth", cfg.aim.smooth}, {"wall_check", cfg.aim.wall_check}, {"team_check", cfg.aim.team_check},
                 {"predict", cfg.aim.predict}, {"max_dist", cfg.aim.max_dist}, {"draw_fov", cfg.aim.draw_fov},
                 {"sens", cfg.aim.sens}, {"bone", cfg.aim.bone}, {"auto_sens", cfg.aim.auto_sens} };
    j["silent"] = { {"enabled", cfg.silent.enabled}, {"hold", cfg.silent.hold}, {"key", cfg.silent.key},
                    {"fov", cfg.silent.fov}, {"bone", cfg.silent.bone}, {"method", cfg.silent.method}, {"wallbang", cfg.silent.wallbang}, {"wall_hold", cfg.silent.wall_hold}, {"wall_key", cfg.silent.wall_key}, {"auto_fire", cfg.silent.auto_fire},
                    {"draw_fov", cfg.silent.draw_fov} };
    j["sound"] = { {"hit", cfg.sound.hit}, {"kill", cfg.sound.kill}, {"hit_file", cfg.sound.hit_file},
                   {"kill_file", cfg.sound.kill_file}, {"hit_vol", cfg.sound.hit_vol}, {"kill_vol", cfg.sound.kill_vol} };
    j["trig"] = { {"enabled", cfg.trig.enabled}, {"key", cfg.trig.key}, {"hold", cfg.trig.hold}, {"delay", cfg.trig.delay},
                  {"fov", cfg.trig.fov}, {"team_check", cfg.trig.team_check}, {"wall_check", cfg.trig.wall_check},
                  {"pen", cfg.trig.pen} };
    j["vis"] = { {"enabled", cfg.vis.enabled}, {"boxes", cfg.vis.boxes}, {"names", cfg.vis.names}, {"dist", cfg.vis.dist},
                 {"health", cfg.vis.health}, {"snaplines", cfg.vis.snaplines}, {"max_dist", cfg.vis.max_dist},
                 {"dim_hidden", cfg.vis.dim_hidden}, {"style", cfg.vis.style}, {"scale", cfg.vis.scale}, {"fill", cfg.vis.fill},
                 {"fill_alpha", cfg.vis.fill_alpha}, {"arrows", cfg.vis.arrows}, {"radar", cfg.vis.radar},
                 {"radar_range", cfg.vis.radar_range}, {"radar_size", cfg.vis.radar_size} };
    j["misc"] = { {"fov_changer", cfg.misc.fov_changer}, {"fov_value", cfg.misc.fov_value}, {"no_recoil", cfg.misc.no_recoil},
                  {"no_spread", cfg.misc.no_spread}, {"rapid_fire", cfg.misc.rapid_fire}, {"rapid_fire_multiplier", cfg.misc.rapid_fire_multiplier}, {"infinite_ammo", cfg.misc.infinite_ammo},
                  {"full_auto", cfg.misc.full_auto}, {"auto_heal", cfg.misc.auto_heal}, {"crosshair", cfg.misc.crosshair}, {"bhop", cfg.misc.bhop},
                  {"third_person", cfg.misc.third_person}, {"tp_dist", cfg.misc.tp_dist}, {"tp_height", cfg.misc.tp_height},
                  {"tp_model", cfg.misc.tp_model}, {"tp_model_scale", cfg.misc.tp_model_scale},
                  {"tp_model_yaw", cfg.misc.tp_model_yaw}, {"tp_model_height", cfg.misc.tp_model_height} };
    j["menu_key"] = cfg.menu_key;
    j["panic_key"] = cfg.panic_key;
    const std::wstring temporary = path + L".tmp";
    std::ofstream f(temporary, std::ios::binary | std::ios::trunc);
    if (!f) { error = "Could not write configuration."; return false; }
    f << j.dump(2);
    f.close();
    if (!f || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        error = "Could not finish saving configuration; previous file kept.";
        return false;
    }
    return true;
    } catch (const std::exception& e) {
        error = std::string("Could not save configuration: ") + e.what();
        return false;
    }
}

void ConfigSave() {
    std::string error;
    if (!SaveFile(Path(), error)) Log("[cfg] %s", error.c_str());
}

static bool ProfilePath(const std::string& name, std::wstring& path, std::string& error) {
    if (name.empty() || name.size() > 48 || name.front() == ' ' || name.back() == ' ') {
        error = "Use a name of 1-48 characters, without leading or trailing spaces.";
        return false;
    }
    for (unsigned char c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '-')) {
            error = "Names can contain letters, numbers, spaces, hyphens and underscores.";
            return false;
        }
    }
    path = DllDir() + L"\\configs\\" + std::wstring(name.begin(), name.end()) + L".json";
    return true;
}

std::vector<std::string> ConfigProfiles(std::string& error) {
    std::vector<std::string> names;
    try {
        const auto directory = std::filesystem::path(DllDir()) / "configs";
        if (!std::filesystem::exists(directory)) return names;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            const std::string name = entry.path().stem().string();
            std::wstring path;
            std::string ignored;
            if (ProfilePath(name, path, ignored)) names.push_back(name);
        }
        std::sort(names.begin(), names.end());
    } catch (const std::exception& e) { error = std::string("Could not list configurations: ") + e.what(); }
    return names;
}

bool ConfigSaveProfile(const std::string& name, std::string& error) {
    error.clear();
    std::wstring path;
    if (!ProfilePath(name, path, error)) return false;
    try { std::filesystem::create_directories(std::filesystem::path(DllDir()) / "configs"); }
    catch (const std::exception& e) { error = e.what(); return false; }
    return SaveFile(path, error);
}

bool ConfigLoadProfile(const std::string& name, std::string& error) {
    error.clear();
    std::wstring path;
    return ProfilePath(name, path, error) && LoadFile(path, error);
}

