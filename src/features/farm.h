#pragma once
#include <string>

struct Cfg;

namespace farm {
    struct Status {
        bool active = false;
        bool combat = false;
        int kills = 0;
        int completed = 0;
        int totalKills = 0;
        double seconds = 0;
        std::string message = "Stopped";
    };
    void Init();
    void Tick();
    void Shutdown();
    void Start();
    void Stop();
    void Inspect();
    bool Active();
    bool SuppressFeatures();
    Status GetStatus();
    void SettingsToSave(Cfg& settings);
}
