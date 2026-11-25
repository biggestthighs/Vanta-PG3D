#include "../src/features/farm_state.h"
#include "../src/features/farm_rewards.h"
#include "../src/core/frame_sample.h"
#include <limits>
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
        Require(Classify("ModalBannerHolder/ProgressRewardBanner/GetReward", "Get Reward") == Action::Reward,
                "XP claim must be recognized as a reward");
        Require(Classify("ModalBannerHolder/NewWeapons/CloseButton", "") == Action::Reward,
                "new weapon notice close surface must dismiss the notice");
        Require(Classify("ModalBannerHolder/NewWeapons/BuyButton", "Available for purchase") == Action::None,
                "new weapon notice must never buy a weapon");
        Require(Classify("ModalBannerHolder/NewWeapons/Background", "Press anywhere to close") == Action::Reward,
                "new weapon notice background must dismiss the notice");
        const std::string gunNotice = "ModalBannerHolder(Clone)/NewWeaponsAvailableBanner(Clone)/Background";
        const std::string gunPrompt = "Available for purchase: Weapon A 26 Press anywhere to close";
        Require(IsNoticeBackdrop(gunNotice), "background can use a sibling close prompt");
        Require(!IsNoticeBackdrop(gunNotice + "/WeaponTile"), "weapon tile must not inherit a sibling close prompt");
        Require(IsNoticeDismiss(gunNotice, gunPrompt), "collider background must accept an aggregate gun-notice prompt");
        Require(Classify(gunNotice, gunPrompt) == Action::Reward, "purchase heading must not block notice dismissal");
        Require(!IsNoticeDismiss(gunNotice + "/WeaponTile", "Available for purchase: 26"), "weapon tile must not become a dismissal target");
        Require(!IsNoticeDismiss("Lobby/Background", "Press anywhere to close"), "collider fallback must be limited to notices");
        Require(!IsNoticeDismiss(gunNotice, ""), "an unlabelled modal collider must not be clicked");
        const std::string availablePrompt = "Now available: Available for purchase: Gun A 36 Gun B 36 New features: Dead City Map Press anywhere to close";
        const std::string availableRoot = "UI Root/Camera/LevelFeaturesView(Clone)";
        Require(IsNoticeDismiss(availableRoot,availablePrompt), "recorded level-feature notice must be recognized by its full text");
        Require(Classify(availableRoot,availablePrompt) == Action::Reward, "purchase heading on the recorded notice must not block dismissal");
        Require(!IsNoticeDismiss(availableRoot,"Now available: Available for purchase: Gun A 36 New features: Dead City"),
                "notice text without a close prompt must not authorize a click");
        Require(!IsNoticeDismiss(availableRoot + "/WeaponTile","Available for purchase: Gun A 36"),
                "weapon tile must never become a notice receiver");
        Require(HasClosePrompt(std::string(1800,'x') + " [ffffff]Press anywhere to close[-]"),
                "close prompt after long reward text must remain discoverable");
        {
            using namespace farm::rewards;
            const auto coins=Parse("Results/CoinReward/Amount","[ffff00]4[-]");
            Require(coins && coins->kind=="Coins" && coins->amount==4,"reward path must identify a bare coin amount");
            const auto gems=Parse("UniversalRewardView/Cell","Gems 5");
            Require(gems && gems->kind=="Gems" && gems->amount==5,"reward title must identify gems");
            const auto pass=Parse("Results/PassXP","15 Pass XP");
            Require(pass && pass->kind=="Pass XP","pass XP must be separate from XP");
            Require(!Parse("ProgressRoad/Cell","Coins 50"),"future route rewards must not be counted");
            Require(!Parse("Notice/Weapon","Available for purchase 36"),"purchase unlocks must not count as weapons");
            Require(!Parse("Results/Trophy","-20 Trophies"),"losses must not count as rewards");
            Require(!Parse("Reward","50 Coins 5 Gems"),"aggregate cells must not assign an ambiguous amount");
            const auto parts=Parse("Reward/WeaponParts","25 Heavy Persuader parts");
            Require(parts && parts->kind=="Weapon parts" && parts->item=="Heavy Persuader","named weapon parts must retain their identity");
            Tracker tracker;
            tracker.Commit("match/1",false,{*coins});
            tracker.Commit("match/1",false,{*coins});
            tracker.Commit("claim/1",true,{{"Coins","",50},*gems});
            Require(tracker.totals.at("Coins\n").match==4 && tracker.totals.at("Coins\n").progress==50,
                    "same claim must count once and milestone rewards must be separate");
            tracker.Commit("match/1",false,{*coins,{"XP","",20}});
            Require(tracker.totals.at("Coins\n").match==4 && tracker.totals.at("XP\n").match==20,
                    "later results must add missing rewards without duplicating earlier ones");
            tracker.Commit("claim/2",true,{});
            Require(tracker.unread.size()==1,"unrecognized claims must be reported");
            tracker.Commit("claim/2",true,{*gems});
            Require(tracker.unread.empty() && tracker.totals.at("Gems\n").progress==10,
                    "an unread screen may later resolve and identical later claims must still count");
        }
        Controller rewards;
        Observation rewardScreen;
        rewardScreen.reward = rewardScreen.replay = true;
        Require(rewards.Tick(rewardScreen, 3).action == Action::Reward, "gun notice must take priority over Battle");
        ActionGate gunGate;
        Require(!gunGate.Update(Action::Reward, gunNotice, false, 100), "gun background must settle before clicking");
        Require(gunGate.Update(Action::Reward, gunNotice, false, 1100), "stable gun background must be clickable");
        gunGate.Clicked(1100);
        Require(!gunGate.Update(Action::Reward, xp, false, 1500), "Continue must wait after gun dismissal");
        Require(gunGate.Update(Action::Reward, xp, false, 3200), "Continue must resume after gun dismissal");
        rewardScreen.reward = false;
        Require(rewards.Tick(rewardScreen, 3).action == Action::Replay, "replay must resume once all notices close");
        Require(!VisibleAlpha(0.f) && !VisibleAlpha(.01f) && VisibleAlpha(1.f),
                "transparent notice must not be selected over a visible claim");
        Require(!VisibleAlpha(std::numeric_limits<float>::quiet_NaN()) &&
                !VisibleAlpha(std::numeric_limits<float>::infinity()),"invalid alpha must not authorize a click");
        Require(BeforeControl(50,false,20,true),"foreground claim must outrank a background notice");
        Require(BeforeControl(50,true,50,false) && !BeforeControl(50,false,50,true),
                "visible notice must retain priority at the same panel depth");
        Require(BeforeControl(-2,false,-5,true),"negative panel depths must preserve foreground ordering");
        {
            static_assert(frame_sample::Bytes == 10240);
            for (const auto size : {std::pair{32u,16u},std::pair{1920u,1080u},std::pair{7680u,4320u}}) {
                for (unsigned i=0; i<frame_sample::PatchCount; ++i) {
                    const auto patch=frame_sample::SourcePatch(size.first,size.second,i);
                    Require(patch.x+frame_sample::PatchWidth <= size.first && patch.y+frame_sample::PatchHeight <= size.second,
                            "GPU sampling boxes must stay inside the source at all resolutions");
                }
            }
            const size_t tightPitch=frame_sample::Width*4, paddedPitch=tightPitch+64;
            std::vector<uint8_t> tight(frame_sample::Bytes,32), padded(paddedPitch*frame_sample::Height,255);
            for (unsigned y=0; y<frame_sample::Height; ++y)
                std::copy_n(tight.data()+y*tightPitch,tightPitch,padded.data()+y*paddedPitch);
            const auto hash=frame_sample::Hash(tight.data(),tightPitch);
            Require(hash && hash==frame_sample::Hash(padded.data(),paddedPitch),"driver row padding must not affect frame identity");
            padded[0]=128;
            Require(hash!=frame_sample::Hash(padded.data(),paddedPitch),"changed frame samples must change the hash");
            Require(frame_sample::Hash(nullptr,tightPitch)==0 && frame_sample::Hash(tight.data(),1)==0,
                    "invalid mapped buffers must be rejected");
        }
        Require(!EligibleAtDepth(true,true,10,20) && EligibleAtDepth(true,true,20,20) &&
                !EligibleAtDepth(true,false,30,20),"a higher modal must block background actions");
        {
            // Replay of the 0.1.21 stall: Progress Road "i" tiles live in a higher scroll panel.
            const std::string road = "ModalBannerHolder(Clone)/ProgressRoadHolder/ProgressRoadBannerHolder/UI Root/Camera/ProgressRoadBanner(Clone)/ProgressRoadView/";
            struct Ui { std::string path, label; bool modal; Action action; int depth; };
            std::vector<Ui> ui{{road+"EndlessScrollView/EndlessScrollDynamic/78/Holder/MainUI_Holder/Btn_cont","i",true,Action::None,40},
                               {road+"ProgressRoad_FloatingView (OFF)/body/BtnInfo","",true,Action::None,45},
                               {road+"Button_Continue/btn_Continue","Continue",true,Action::None,10}};
            for (auto& control : ui) { control.modal = IsModal(control.path); control.action = Classify(control.path,control.label); }
            Require(ui[2].modal && ui[2].action == Action::Reward && ui[0].action == Action::None,"Progress Road Continue must be a reward action");
            Require(EligibleAtDepth(true,true,ui[2].depth,ForegroundDepth(ui)),"info buttons in a higher panel must not hide Progress Road Continue");
            ui.push_back({"ModalBannerHolder(Clone)/UniversalRewardHolder/UniversalRewardBannerHolder/UI Root/Camera/UniversalRewardBanner(Clone)/UniversalRewardView/BtnCollect","Get Reward",true,Action::None,60});
            ui.back().action = Classify(ui.back().path,ui.back().label);
            Require(!EligibleAtDepth(true,true,ui[2].depth,ForegroundDepth(ui)),"a Get Reward banner above Progress Road must still go first");
            Require(SafeDismiss(road+"Button_Continue/btn_Continue","Continue") && SafeDismiss("Some/Unknown/Popup/BtnClose","") &&
                    SafeDismiss("Popup/Panel/Ok","OK") && !SafeDismiss("Shop/Offer/BtnBuy","Continue") &&
                    !SafeDismiss("Popup/Panel/Btn","Upgrade 70") && !SafeDismiss("Popup/Panel/Btn","Watch video") &&
                    !SafeDismiss("Respawn/Btn_Report","Report Player") && !SafeDismiss("Pause/BtnQuit","Quit"),
                    "last-resort clicks must stay on dismiss buttons and never spend");
            Watchdog stall;
            stall.Observe("unknown",false,true,1000);
            Require(stall.StalledFor(30999) < 30000 && stall.StalledFor(31000) >= 30000,"fallback must arm after 30 s on one screen");
            stall.Observe("unknown",true,true,32000);
            Require(stall.StalledFor(90000) == 0,"an unfocused or paused game must never arm the fallback");
        }
        {
            // Dead City / weapon unlock notices: tap the prompt until it is gone.
            using Step = PromptTapper::Step;
            PromptTapper tap;
            Require(tap.Update(true,false,1000) == Step::None && tap.Update(true,false,3900) == Step::None,
                    "the notice's own close handler gets 3 s before real taps");
            Require(tap.Update(true,false,4000) == Step::Move && tap.Update(true,false,4100) == Step::None &&
                    tap.Update(true,false,4120) == Step::Down && tap.Update(true,false,4150) == Step::None &&
                    tap.Update(true,false,4200) == Step::Up,"a tap must be move, down, up on separate frames");
            Require(tap.Update(true,false,5600) == Step::None && tap.Update(true,false,5700) == Step::Move,
                    "taps must repeat every 1.5 s while the prompt stays");
            tap.Update(true,false,5820);
            Require(tap.phase == 2 && tap.Update(true,true,5850) == Step::Up && tap.phase == 0,
                    "losing focus with the button held must release it");
            Require(tap.Update(true,true,9000) == Step::None,"no taps while paused or unfocused");
            Require(tap.Update(true,false,9100) == Step::None && tap.Update(true,false,12100) == Step::Move,
                    "returning from a pause restarts the settle delay");
            Require(tap.Update(false,false,12200) == Step::None && tap.Update(true,false,12300) == Step::None,
                    "a closed notice stops tapping; the next notice settles first");
            tap.Cancel(15400);
            Require(tap.Update(true,false,16000) == Step::None && tap.Update(true,false,16900) == Step::Move,
                    "a skipped tap retries after 1.5 s");
            ClientPoint point{};
            Require(UnityToClient(960,108,1920,1080,1920,1080,point) && point.x == 960 && point.y == 972,
                    "Unity bottom-left coordinates must flip to the client's top-left");
            Require(UnityToClient(960,540,1920,1080,1280,720,point) && point.x == 640 && point.y == 360,
                    "a scaled render target must map onto the window");
            Require(!UnityToClient(-5,10,1920,1080,1920,1080,point) && !UnityToClient(10,2000,1920,1080,1920,1080,point) &&
                    !UnityToClient(std::numeric_limits<float>::quiet_NaN(),10,1920,1080,1920,1080,point),
                    "off-screen or invalid prompt positions must never be tapped");
        }
        Require(ScreenIdentity({"claim","notice","claim"}) == ScreenIdentity({"notice","claim"}),
                "control reordering or duplicates must not reset recovery");
        Confirmation effect;
        effect.Start(100);
        Require(!effect.Observe(true,400),"dispatch must not count as a completed click");
        Require(!effect.Observe(false,500) && !effect.Observe(true,600),"temporary disappearance must not confirm a claim");
        Require(!effect.Observe(false,900) && !effect.Observe(false,1200) && effect.Observe(false,1400),
                "confirmation requires sustained disappearance across scans");
        Require(!effect.Observe(false,2000),"a screen transition must be confirmed only once");
        Confirmation wrappedEffect;
        wrappedEffect.Start(0xfffffff0u);
        Require(!wrappedEffect.Observe(false,0xfffffff0u) && wrappedEffect.Observe(false,0x300u),
                "confirmation timers must survive tick counter wraparound");
        Require(Classify("InGameGUI/Camera/PausePanel/ButtonsWidget/Grid/BtnResume","Resume") == Action::Resume,
                "the recorded pause screen must expose its Resume action");
        Require(Classify("UI/Unknown/ResumeButton","Resume") == Action::None,
                "Resume recognition must be restricted to the known pause panel");
        Controller pausedMatch;
        Observation resumeScreen;
        resumeScreen.match = resumeScreen.alive = resumeScreen.resume = true;
        resumeScreen.matchKills = 0;
        Require(pausedMatch.Tick(resumeScreen,3).action == Action::Resume && !pausedMatch.Tick(resumeScreen,3).combat,
                "in-game pause must stop combat while Resume is pending");
        resumeScreen.paused = true;
        Require(pausedMatch.Tick(resumeScreen,3).action == Action::None,"external pause must block automatic Resume");
        unsigned simulatedTransitions = 0;
        uint32_t simulatedTime = 0xffff0000u;
        for (unsigned cycle=0; cycle<2000; ++cycle) {
            Controller flow;
            const unsigned stages = cycle%3 + 2; // A match result, optional XP/notice stages, then replay.
            for (unsigned stage=0; stage<stages; ++stage) {
                Watchdog recovery;
                ActionGate settling;
                Confirmation confirmation;
                bool present = true, finished = false;
                unsigned attempts = 0;
                const unsigned failures = (cycle+stage)%8;
                const bool replayStage = stage+1==stages;
                for (unsigned tick=0; tick<1600 && !finished; ++tick) {
                    simulatedTime += 250;
                    const bool paused = tick>=10 && tick<18;
                    const auto identity = tick%2 ? ScreenIdentity({"active control","background"}) : ScreenIdentity({"background","active control"});
                    recovery.Observe(identity,paused,true,simulatedTime);
                    Observation observed;
                    observed.paused = paused;
                    observed.reward = !replayStage && present;
                    observed.replay = replayStage && present;
                    const auto decision = flow.Tick(observed,3);
                    if (confirmation.Observe(present,simulatedTime)) { finished = true; ++simulatedTransitions; break; }
                    const auto target = present ? "recognized active control" : "";
                    if (settling.Update(decision.action,target,paused,simulatedTime) && recovery.CanAttempt(simulatedTime) && present) {
                        Require(!paused,"simulated focus loss must never dispatch input");
                        settling.Clicked(simulatedTime);
                        recovery.Attempted(simulatedTime);
                        if (!confirmation.pending) confirmation.Start(simulatedTime);
                        if (++attempts>failures) present = false;
                    }
                    recovery.Recover(simulatedTime);
                }
                Require(finished && !present,"sustained delayed/failed claim simulation must recover and verify completion");
                Require(attempts==failures+1,"simulation must neither skip failures nor over-dispatch after a confirmed transition");
            }
        }
        Require(simulatedTransitions==5999,"all simulated reward and replay transitions must complete");
        std::cout << "PASS: 2000 simulated cycles, 5999 confirmed transitions with failed inputs, focus loss, optional stages and timer wraparound\n";
        Watchdog retry;
        retry.Observe("visible claim",false,true,100);
        for (uint32_t now : {1100u,3100u,5100u}) {
            Require(retry.CanAttempt(now),"initial retry must remain available");
            retry.Attempted(now);
        }
        Require(!retry.CanAttempt(7100),"three failures must back off");
        Require(retry.CanAttempt(10100),"three failures must not permanently stop Farm");
        Require(retry.Recover(10100),"unchanged claim must trigger a recovery rescan");
        Require(!retry.Recover(11000),"recovery scans must be rate limited");
        retry.Attempted(10100);
        Require(!retry.CanAttempt(12000) && retry.CanAttempt(17100),"later retry interval must increase");
        retry.Observe("visible claim",true,true,18000);
        Require(!retry.CanAttempt(30000) && !retry.Recover(30000),"unfocused or paused Farm must not recover with input");
        retry.Observe("next claim",false,true,31000);
        Require(retry.attempts==0 && retry.CanAttempt(32000),"a new screen must reset the retry budget");
        retry.Observe("idle match",false,false,40000);
        Require(!retry.Recover(90000),"normal idle matches must not be treated as stalled screens");
        Watchdog wrap;
        wrap.Observe("claim",false,true,0xfffff000u);
        Require(wrap.Recover(0x2000u),"watchdog must survive the Windows tick counter wrapping");
        Controller navigation;
        Observation menu;
        menu.deathmatch = menu.nuclearCity = menu.start = true;
        Require(navigation.Tick(menu, 3).action == Action::Deathmatch, "select Deathmatch first");
        navigation.Clicked(Action::Deathmatch);
        Require(navigation.Tick(menu, 3).action == Action::NuclearCity, "select Nuclear City before launch");
        navigation.Clicked(Action::NuclearCity);
        Require(navigation.Tick(menu, 3).action == Action::Start, "launch selected map");
        std::cout << "PASS: stored kills, cutoff, respawn, XP/new-weapon rewards, delayed replay and Nuclear City navigation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
