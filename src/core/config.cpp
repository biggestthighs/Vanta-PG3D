#include "config.h"
#include "json.hpp"
#include <algorithm>
#ifdef CORE_BUILD
#include "../features/farm.h"
#endif
#include <fstream>

Cfg cfg;
static std::wstring Path() { return DllDir() + L"\\config.json"; }

void ConfigLoad() {
    std::ifstream f(Path());
    if (!f) { Log("[cfg] no config, defaults"); return; }
    try {
        nlohmann::json j;
        f >> j;
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
            cfg.farm.kill_limit = std::clamp(farmSettings.value("kill_limit", 3), 0, 100);
            cfg.farm.fov = std::clamp(farmSettings.value("fov", 360.f), 1.f, 360.f);
            cfg.farm.fire_mode = std::clamp(farmSettings.value("fire_mode", 1), 1, 2);
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
    } catch (...) { Log("[cfg] parse failed"); }
}

void ConfigSave() {
    Cfg settings = cfg;
#ifdef CORE_BUILD
    farm::SettingsToSave(settings);
#endif
    const auto& cfg = settings;
    nlohmann::json j;
    j["farm"] = {{"kill_limit", cfg.farm.kill_limit}, {"fov", cfg.farm.fov}, {"fire_mode", cfg.farm.fire_mode}};
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
                  {"no_spread", cfg.misc.no_spread}, {"rapid_fire", cfg.misc.rapid_fire}, {"infinite_ammo", cfg.misc.infinite_ammo},
                  {"full_auto", cfg.misc.full_auto}, {"auto_heal", cfg.misc.auto_heal}, {"crosshair", cfg.misc.crosshair}, {"bhop", cfg.misc.bhop},
                  {"third_person", cfg.misc.third_person}, {"tp_dist", cfg.misc.tp_dist}, {"tp_height", cfg.misc.tp_height},
                  {"tp_model", cfg.misc.tp_model}, {"tp_model_scale", cfg.misc.tp_model_scale},
                  {"tp_model_yaw", cfg.misc.tp_model_yaw}, {"tp_model_height", cfg.misc.tp_model_height} };
    j["menu_key"] = cfg.menu_key;
    j["panic_key"] = cfg.panic_key;
    std::ofstream f(Path());
    if (f) f << j.dump(2);
}

