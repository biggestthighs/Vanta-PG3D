#pragma once
#include "../sdk/game.h"

namespace aimbot {
    void DrawFov();
    bool Target(std::string& name, float& px, float& py);
}

namespace rage {
    void Init();                 // after probe::Init (needs its method table)
    void Step();                 // game thread, once per frame, from the local player's Update
    bool Target(std::string& name, Vec3& world);
    int WallBreak();             // current weapon: 1 yes, 0 no, -1 not learned yet
    void WrapBegin(void* self, bool weapon); // around the local Player_move_c / weapon Update
    void WrapEnd();
    void Release();              // unload: let go of any button auto shoot is holding
    bool SilentTarget(Vec3& world, bool* hidden = nullptr); // where the next shot will be sent, if silent aim has one
    int SilentShots();           // shots redirected this session
}

namespace triggerbot {
    void Run();
    int PenState(); // 1 has Wall break, 0 does not, -1 unknown / not checked
}

namespace visuals {
    void Draw();
}

namespace hitsound {
    void Frame();                              // render thread, once per frame
    void Init();                               // game thread, after il2cpp + MinHook: hooks the game's hit / kill clip playback
    void Tick();                               // game thread, once per frame
    void Shutdown();                           // unload: wait for playing sounds
    void NoteHit(void* enemy);                 // game thread: the game applied damage to this enemy
    void NoteTarget(void* enemy);              // game thread: a shot was just sent at this enemy
    void Stats(int& clipCalls, int& hitClips, int& killClips);
    std::vector<std::string> Files(bool kill); // .wav files in sounds\hit or sounds\kill
    void Preview(bool kill);
    void Import(bool kill);                    // file dialog on its own thread; copies into the folder
    int ImportCount();                         // bumps after each successful import, to refresh lists
    std::string LastMessage();
    void OpenFolder();
}

namespace probe {
    void Init();
    void Shutdown();
    void OnTick();
    bool MethodRange(uintptr_t addr, uintptr_t& lo, uintptr_t& hi, std::string& name);
    uintptr_t GaBase();
}

namespace misc {
    void Tick();
    void DrawOverlay();
    void ReleaseInput();
}
