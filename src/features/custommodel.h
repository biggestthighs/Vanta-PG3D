#pragma once
#include "../sdk/game.h"

// Unity objects are created, moved and destroyed exclusively on the game thread.
namespace custommodel {
    constexpr int Count = 7;
    const char* Name(int index);
    bool Init();
    bool Update(const game::Player& player, void* camera);
    void Hide();
    void Clear();
    void RequestShutdown();
    bool ShutdownDone();
    void ServiceShutdown();
}
