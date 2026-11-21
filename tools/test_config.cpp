#include "../src/core/config.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static std::wstring directory;
std::wstring DllDir() { return directory; }
void Log(const char*, ...) {}
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("Vanta-config-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    directory = root.wstring();
    try {
        std::string error;
        Require(ConfigProfiles(error).empty(), "missing profile directory should be empty");
        cfg.aim.fov = 73.f; cfg.sound.hit_file = "bell.wav"; cfg.farm.kill_limit = 7; cfg.farm.wallbang = true; cfg.misc.rapid_fire = true; cfg.misc.rapid_fire_multiplier = 7;
        cfg.world.enabled = true; cfg.world.palette = 2; cfg.world.stars = false;
        cfg.world.lighting = false;
        cfg.world.fogDensity = .019f; cfg.world.intensity = .31f;
        Require(ConfigSaveProfile("Test preset", error), "save failed");
        cfg.aim.fov = 12.f; cfg.sound.hit_file = "click.wav"; cfg.farm.kill_limit = 1; cfg.farm.wallbang = false; cfg.misc.rapid_fire = false; cfg.misc.rapid_fire_multiplier = 1;
        cfg.world = {};
        Require(ConfigLoadProfile("Test preset", error), "load failed");
        Require(cfg.aim.fov == 73.f && cfg.sound.hit_file == "bell.wav" && cfg.farm.kill_limit == 7 && cfg.farm.wallbang && cfg.misc.rapid_fire && cfg.misc.rapid_fire_multiplier == 7,
                "configuration round trip lost settings");
        Require(cfg.world.enabled && cfg.world.palette == 2 && !cfg.world.stars && !cfg.world.lighting &&
                cfg.world.fogDensity == .019f && cfg.world.intensity == .31f,
                "configuration round trip lost atmosphere settings");
        cfg.aim.fov = 91.f;
        Require(ConfigSaveProfile("Test preset", error), "overwrite failed");
        cfg.aim.fov = 1.f;
        Require(ConfigLoadProfile("Test preset", error) && cfg.aim.fov == 91.f, "overwrite not loaded");
        const auto names = ConfigProfiles(error);
        Require(names.size() == 1 && names.front() == "Test preset", "saved list is incorrect");
        Require(!ConfigSaveProfile("../escape", error), "path traversal was accepted");
        Require(!ConfigLoadProfile("missing", error) && cfg.aim.fov == 91.f, "missing file changed settings");
        {
            std::ofstream bad(root / "configs" / "Broken.json");
            bad << R"({"aim":{"fov":18},"trig":{"enabled":"invalid"},"vis":{},"misc":{}})";
        }
        Require(!ConfigLoadProfile("Broken", error) && cfg.aim.fov == 91.f, "failed load must roll back partial changes");
        {
            std::ofstream old(root / "configs" / "Legacy.json");
            old << R"({"aim":{},"trig":{},"vis":{},"misc":{}})";
        }
        Require(ConfigLoadProfile("Legacy", error) && !cfg.world.enabled && cfg.world.palette == 0,
                "legacy settings must use map defaults");
        ConfigSave();
        cfg.aim.fov = 1.f;
        ConfigLoad();
        Require(cfg.aim.fov == 91.f, "startup configuration no longer works");
        std::filesystem::remove(root / "configs" / "Test preset.json");
        std::filesystem::remove(root / "configs" / "Broken.json");
        std::filesystem::remove(root / "configs" / "Legacy.json");
        std::filesystem::remove(root / "configs");
        std::filesystem::remove(root / "config.json");
        std::filesystem::remove(root);
        std::cout << "PASS: named configs, overwrite, list, traversal rejection, transactional loading and startup config\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
