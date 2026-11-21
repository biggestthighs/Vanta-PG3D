#pragma once
#include "../core/config.h"

namespace world {
    void Init(bool cameraCallbackReady);
    void Publish(const WorldCfg& settings, bool paused);
    void Tick(void* mainCamera, bool inMatch);
    void RequestShutdown();
    bool ShutdownDone();
    const char* SkyStatus();
    const char* FogStatus();
    const char* ParticleStatus();
    const char* LightingStatus();
}
