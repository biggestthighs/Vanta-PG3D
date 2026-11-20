#include "features.h"
#include "../core/config.h"

// Only features that work without guessing at obfuscated fields live here. The weapon and
// health memory writers were removed: their field offsets were never confirmed, so on a
// wrong guess they would have corrupted unrelated game state.
namespace misc {
    static DWORD s_jump = 0;

    void Tick() {
        game::ApplyFov();
    }

    // Space press/release across frames (render thread, once per frame); input::Tap would
    // Sleep inside Present and stutter the game.
    static bool s_keyDown = false;
    static void Key(bool down) {
        if (down && !input::CanAutoClick()) return;
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = VK_SPACE;
        in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(in));
        s_keyDown = down;
    }
    void ReleaseInput() { if (s_keyDown) Key(false); }
    static void Bhop() {
        const DWORD now = GetTickCount();
        if (s_keyDown) { if (now >= s_jump) { Key(false); s_jump = now + 40; } return; }
        if (cfg.misc.bhop && input::Down(VK_SPACE) && now >= s_jump) { Key(true); s_jump = now + 20; }
    }

    void DrawOverlay() {
        Bhop();
        if (!cfg.misc.crosshair) return;
        auto* dl = ImGui::GetForegroundDrawList();
        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        const ImVec2 c{ disp.x * 0.5f, disp.y * 0.5f };
        const ImU32 col = IM_COL32(255, 255, 255, 200);
        dl->AddLine({ c.x - 7, c.y }, { c.x - 2, c.y }, col, 1.4f);
        dl->AddLine({ c.x + 2, c.y }, { c.x + 7, c.y }, col, 1.4f);
        dl->AddLine({ c.x, c.y - 7 }, { c.x, c.y - 2 }, col, 1.4f);
        dl->AddLine({ c.x, c.y + 2 }, { c.x, c.y + 7 }, col, 1.4f);
    }
}
