#pragma once
#include "common.h"

namespace render {
    bool Init();
    bool Ready();
    void Shutdown();
    void InstallGameInputBlock(); // after il2cpp and MinHook are up: the game sees no mouse while the menu is open
    extern bool MenuOpen;
    extern double SessionSec;  // time since the overlay came up
    extern double MatchSec;    // of which: time with a local player in the world
    extern HWND Hwnd;
}
