#include "../src/features/farm_state.h"
#include <iostream>
#include <stdexcept>

using namespace farm::logic;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        Controller controller;
        Observation screen;
        screen.matchKills = 0;
        screen.battle = true;
        Require(controller.Tick(screen, 3).action == Action::Battle, "lobby must open the mode picker");
        controller.Clicked(Action::Battle);
        screen.battle = false;
        screen.classic = true;
        Require(controller.Tick(screen, 3).action == Action::Classic, "mode picker must open Classic Modes");
        screen.classic = false;
        screen.deathmatch = screen.start = true;
        Require(controller.Tick(screen, 3).action == Action::Deathmatch, "must select Deathmatch before starting");
        controller.Clicked(Action::Deathmatch);
        Require(controller.Tick(screen, 3).action == Action::Start, "selected Deathmatch must start");

        screen = {};
        screen.match = screen.alive = true;
        screen.matchKills = 0;
        screen.matchId = 101;
        auto decision = controller.Tick(screen, 3);
        Require(decision.newMatch && decision.combat && controller.kills == 0, "match must start with a fresh counter");
        screen.matchKills = 2;
        Require(controller.Tick(screen, 3).combat && controller.kills == 2, "combat must continue below quota");
        screen.matchKills = 3;
        decision = controller.Tick(screen, 3);
        Require(!decision.combat && decision.phase == Phase::Idle, "quota must disable combat");

        screen.match = screen.alive = false;
        screen.respawn = true;
        decision = controller.Tick(screen, 3);
        Require(!decision.combat && !decision.newMatch && decision.action == Action::Respawn && controller.kills == 3,
                "respawn must not reset the quota or enable combat");
        screen.respawn = false;
        screen.match = screen.alive = screen.reward = true;
        decision = controller.Tick(screen, 3);
        Require(decision.action == Action::Reward && !decision.combat && controller.completed == 0,
                "a mid-match reward must not finish the match");
        screen.reward = false;
        Require(!controller.Tick(screen, 3).combat && controller.kills == 3, "closing a popup must keep the quota");

        screen.match = screen.alive = false;
        screen.reward = screen.replay = true;
        decision = controller.Tick(screen, 3);
        Require(decision.action == Action::Reward && controller.completed == 1, "rewards must precede replay");
        controller.Tick(screen, 3);
        Require(controller.completed == 1, "reward screens must not count multiple matches");
        screen.reward = false;
        Require(controller.Tick(screen, 3).action == Action::Replay, "must replay after rewards are dismissed");
        screen.replay = false;
        screen.match = screen.alive = true;
        screen.matchKills = 0;
        screen.matchId = 202;
        decision = controller.Tick(screen, 3);
        Require(decision.newMatch && decision.combat && controller.kills == 0, "next match must re-arm once");
        screen.paused = true;
        Require(!controller.Tick(screen, 3).combat, "open menu, unfocused game and panic must pause combat");

        Controller idle;
        screen.paused = false;
        Require(!idle.Tick(screen, 0).combat, "zero-kill farming must never enable combat");
        Controller storedKills;
        screen.matchId = 303;
        screen.matchKills = 7;
        decision = storedKills.Tick(screen, 3);
        Require(storedKills.kills == 7 && !decision.combat,
                "starting mid-match must include every stored kill and stop above quota");
        screen.matchKills = 8;
        Require(!storedKills.Tick(screen, 3).combat && storedKills.kills == 8,
                "kills without audio feedback must still update the displayed count");
        screen.matchKills = 0;
        Require(!storedKills.Tick(screen, 3).combat && storedKills.kills == 8,
                "a temporarily lower count must not erase kills or re-arm combat");
        Require(!storedKills.Tick(screen, 20).combat,
                "raising the quota must not re-arm a match that already reached its limit");
        screen.matchId = 404;
        screen.matchKills = 0;
        decision = storedKills.Tick(screen, 3);
        Require(decision.newMatch && decision.combat && storedKills.kills == 0,
                "a fresh game-stat match must reset the kill counter");
        Controller missingStats;
        screen.matchKills = -1;
        Require(!missingStats.Tick(screen, 3).combat, "missing game stats must pause combat instead of guessing kills");
        screen.matchKills = 0;
        Require(missingStats.Tick(screen, 3).combat, "valid match stats must resume combat below quota");
        screen.matchKills = 5;
        Require(!missingStats.Tick(screen, 3).combat && missingStats.kills == 5,
                "a burst of kills must be counted in full and stop combat");
        Require(Classify("EndGame/PlayAgainButton", "PLAY AGAIN") == Action::Replay, "replay recognition failed");
        Require(Classify("UI/RewardPopup/ClaimButton", "[ffffff]Collect[-]") == Action::Reward, "reward recognition failed");
        Require(Classify("UI/LevelUpWindow/CloseButton", "") == Action::Reward, "level-up close recognition failed");
        Require(Classify("UI/RewardPopup/BuyButton", "Claim") == Action::None, "must reject purchases");
        Require(Classify("UI/Shop/PlayAgainButton", "Play again") == Action::None, "must reject shop actions");
        Require(Classify("UI/RewardPopup/WatchVideo", "Claim") == Action::None, "must reject ad rewards");
        Require(Classify("UI/Unknown/CloseButton", "Close") == Action::None, "must leave unknown dialogs alone");
        Require(Classify("UI/RespawnWindow/Button", "To Battle") == Action::Respawn, "respawn must not open the lobby");
        Require(Classify("InGameGUI/RespawnWindowPoint/RespawnWindow/YourWeapon/RW_UpgradeHolder/RW_BtnUpgrade", "Upgrade 75 +1") == Action::None,
                "weapon upgrades inside a respawn screen must never be clicked");
        Require(Classify("UI/RespawnWindow/YourWeapon/OtherButton", "Details") == Action::None,
                "respawn screen ancestry must not make unrelated buttons eligible");
        Require(Classify("Lobby/RightAnchor/BtnBattle_A", "To Battle To Battle") == Action::Battle, "live lobby button alias failed");
        Require(Classify("GameModeHub/CategorySelector/Cell", "Classic Modes Team Fight, Deathmatch and many other modes") == Action::Classic,
                "live Classic Modes card failed");
        Require(Classify("ModeSelector/ModeTab(Clone)/Holder", "Deathmatch Deathmatch !") == Action::Deathmatch,
                "live Deathmatch tab failed");
        Require(Classify("Common/BottomButtonsHolder/GreenButtonsLayout/Button_Battle", "To Battle!") == Action::Start,
                "live launch button must start the selected mode");
        const std::string trophy = "NetworkStartTableNGUI(Clone)/Camera/EndInterface/Trophy/Ok";
        const std::string matchReward = "NetworkStartTableNGUI(Clone)/Camera/EndInterface/Ok";
        const std::string replay = "NetworkStartTableNGUI(Clone)/Camera/StartInterface/BottomRight/StartButton";
        const std::string xp = "ModalBannerHolder(Clone)/ProgressRoadHolder/ProgressRoadBannerHolder/UI Root/Camera/ProgressRoadBanner(Clone)/ProgressRoadView/Button_Continue/btn_Continue";
        Require(Classify(trophy, "Continue") == Action::Reward && IsModal(trophy),
                "recorded trophy Continue must block the Battle button behind it");
        Require(Classify(matchReward, "OK") == Action::Reward && IsModal(matchReward),
                "end-of-match OK must dismiss the match rewards");
        Require(Classify(replay, "To Battle!") == Action::Replay && !IsModal(replay),
                "results Battle must replay rather than reopen mode selection");
        Require(Classify(xp, "Continue") == Action::Reward && IsModal(xp),
                "optional XP progress reward must block replay");
        Require(Classify("NetworkStartTableNGUI/Camera/EndInterface/BuyButton", "OK") == Action::None,
                "result ancestry must not authorize a purchase");
        Require(Classify("UnknownDialog/Ok", "OK") == Action::None && IsModal("UnknownDialog/Ok"),
                "unrecognized confirmation must block navigation without being accepted");

        // Replay briefly appears between reward animations. A late XP banner
        // must restart the wait; games without that banner must still replay.
        for (const bool hasXp : {false, true}) {
            ActionGate gate;
            Require(!gate.Update(Action::Reward, trophy, false, 100), "trophy must settle before clicking");
            Require(gate.Update(Action::Reward, trophy, false, 1100), "stable trophy must be accepted");
            gate.Clicked(1100);
            Require(!gate.Update(Action::Replay, replay, false, 1500), "must not click through the trophy transition");
            Require(!gate.Update(Action::Reward, matchReward, false, 2000), "next reward must restart the target wait");
            Require(!gate.Update(Action::Reward, matchReward, false, 3000), "click cooldown must survive screen changes");
            Require(gate.Update(Action::Reward, matchReward, false, 3200), "stable match reward must be accepted");
            gate.Clicked(3200);
            Require(!gate.Update(Action::Replay, replay, false, 3500), "Battle must wait for a possible XP banner");
            Require(!gate.Update(Action::Replay, replay, false, 5400), "Battle must not run immediately after OK");
            if (hasXp) {
                Require(!gate.Update(Action::Reward, xp, false, 5500), "late XP reward must replace replay candidate");
                Require(gate.Update(Action::Reward, xp, false, 6500), "stable XP reward must be accepted");
                gate.Clicked(6500);
                Require(!gate.Update(Action::Replay, replay, false, 7000), "XP dismissal must restart Battle delay");
                Require(!gate.Update(Action::Replay, replay, false, 9900), "XP transition must finish first");
                Require(gate.Update(Action::Replay, replay, false, 10000), "replay must resume after XP reward");
            } else {
                Require(gate.Update(Action::Replay, replay, false, 6500), "must replay without requiring an XP reward every match");
            }
        }
        ActionGate pausedGate;
        pausedGate.Update(Action::Replay, replay, false, 100);
        Require(!pausedGate.Update(Action::Replay, replay, true, 4000), "paused bot must never click");
        Require(!pausedGate.Update(Action::Replay, replay, false, 4500), "resume must allow the screen to settle again");
        pausedGate.Update(Action::None, "", false, 4600);
        Require(!pausedGate.Update(Action::Replay, replay, false, 8000), "unknown modal must invalidate an earlier Battle candidate");
        Require(pausedGate.Update(Action::Replay, replay, false, 11000), "stable replay must resume after a modal closes");
        std::cout << "PASS: stored kills, latched cutoff, respawn, trophy/match/optional XP rewards, delayed replay, pause and button classification\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
