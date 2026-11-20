#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace farm::logic {
    enum class Action { None, Reward, Replay, Deathmatch, Battle, Start, Respawn, Classic, NuclearCity };
    enum class Phase { Waiting, Fighting, Idle, Rewards, Queueing, Paused };

    struct Observation {
        bool match = false;
        bool alive = false;
        bool paused = false;
        bool reward = false;
        bool replay = false;
        bool deathmatch = false;
        bool classic = false;
        bool nuclearCity = false;
        bool battle = false;
        bool start = false;
        bool respawn = false;
        int matchKills = -1;
        uintptr_t matchId = 0;
    };

    struct Decision {
        Phase phase = Phase::Waiting;
        Action action = Action::None;
        bool combat = false;
        bool newMatch = false;
    };

    struct Controller {
        bool inMatch = false;
        bool modeSelected = false;
        bool mapSelected = false;
        uintptr_t matchId = 0;
        bool quotaReached = false;
        int kills = 0;
        int completed = 0;
        int totalKills = 0;

        Decision Tick(const Observation& observation, int quota) {
            Decision result;
            const bool match = (observation.match || observation.respawn) && !observation.replay;
            const bool changedMatch = observation.matchId && matchId && observation.matchId != matchId;
            if (match && (!inMatch || changedMatch)) {
                if (inMatch && changedMatch) ++completed;
                kills = 0;
                inMatch = true;
                modeSelected = true;
                matchId = observation.matchId;
                quotaReached = false;
                result.newMatch = true;
            }
            if (inMatch) {
                if (!matchId) matchId = observation.matchId;
                const int count = std::max(0, observation.matchKills);
                totalKills += std::max(0, count - kills);
                kills = std::max(kills, count);
                quotaReached = quotaReached || kills >= std::max(0, quota);
                if (!match && (observation.replay || observation.reward || observation.deathmatch || observation.battle)) {
                    ++completed;
                    inMatch = false;
                }
            }
            if (observation.paused) { result.phase = Phase::Paused; return result; }
            if (observation.reward) { result.phase = Phase::Rewards; result.action = Action::Reward; return result; }
            if (observation.replay) { result.phase = Phase::Queueing; result.action = Action::Replay; return result; }
            if (match) {
                result.phase = quotaReached ? Phase::Idle : observation.matchKills < 0 ? Phase::Waiting : Phase::Fighting;
                result.combat = result.phase == Phase::Fighting && observation.alive && !observation.respawn;
                if (observation.respawn) result.action = Action::Respawn;
                return result;
            }
            result.phase = Phase::Queueing;
            if (observation.deathmatch && !modeSelected) result.action = Action::Deathmatch;
            else if (observation.classic) result.action = Action::Classic;
            else if (observation.nuclearCity && modeSelected && !mapSelected) result.action = Action::NuclearCity;
            else if (observation.start && modeSelected) result.action = Action::Start;
            else if (observation.battle) result.action = Action::Battle;
            return result;
        }

        void Clicked(Action action) {
            if (action == Action::Deathmatch) modeSelected = true;
            if (action == Action::NuclearCity) mapSelected = true;
            if (action == Action::Battle) { modeSelected = false; mapSelected = false; }
        }
    };

    inline std::string Normalize(const std::string& value) {
        std::string normalized;
        bool markup = false;
        for (unsigned char character : value) {
            if (character == '[' || character == '<') { markup = true; continue; }
            if (character == ']' || character == '>') { markup = false; continue; }
            if (markup) continue;
            if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
            if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
                normalized.push_back(static_cast<char>(character));
        }
        return normalized;
    }

    inline Action Classify(const std::string& path, const std::string& label) {
        const std::string name = Normalize(path), text = Normalize(label);
        const std::string leaf = Normalize(path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1));
        const bool weaponNotice = name.find("newweapons") != std::string::npos || name.find("newguns") != std::string::npos ||
                                  name.find("availableweapons") != std::string::npos || name.find("weaponsavailable") != std::string::npos ||
                                  name.find("levelupweapons") != std::string::npos;
        if (weaponNotice && (leaf == "close" || leaf == "btnclose" || leaf == "buttonclose" || leaf == "closebutton"))
            return Action::Reward;
        // Dismiss only the notice's close surface, never its weapon tiles.
        if (text == "pressanywheretoclose" || text == "tapanywheretoclose") return Action::Reward;
        for (const char* blocked : {"purchase", "buy", "shop", "upgrade", "unlock", "watchvideo", "advert", "doublethe", "doubleyour", "openchest", "gems", "subscription", "spend"})
            if (name.find(blocked) != std::string::npos || text.find(blocked) != std::string::npos) return Action::None;
        // Trophy Continue and the match-reward OK share EndInterface. Their
        // names do not necessarily contain "reward"; StartInterface is behind them.
        const bool matchResults = name.find("networkstarttablengui") != std::string::npos &&
                                  name.find("endinterface") != std::string::npos;
        const bool reward = matchResults || name.find("reward") != std::string::npos || name.find("levelup") != std::string::npos ||
                            name.find("award") != std::string::npos || name.find("progressroad") != std::string::npos;
        if (reward && (text == "claim" || text == "collect" || text == "getreward" || text == "continue" ||
                       text == "ok" || text == "close" || name.find("buttonclose") != std::string::npos ||
                       name.find("closebutton") != std::string::npos)) return Action::Reward;
        if (text == "playagain" || text == "battleagain" || text == "rematch" || name.find("playagain") != std::string::npos)
            return Action::Replay;
        if (name.find("networkstarttablengui") != std::string::npos &&
            name.find("startinterface") != std::string::npos && leaf == "startbutton" && text == "tobattle")
            return Action::Replay;
        if (text.rfind("nuclearcity", 0) == 0) return Action::NuclearCity;
        if (text.rfind("classicmodes", 0) == 0) return Action::Classic;
        if (text == "deathmatch" || (name.find("modetab") != std::string::npos && text.rfind("deathmatch", 0) == 0) ||
            name.find("buttondeathmatch") != std::string::npos || name.find("deathmatchbutton") != std::string::npos)
            return Action::Deathmatch;
        if (text == "respawn" || name.find("buttonrespawn") != std::string::npos ||
            (text == "tobattle" && name.find("respawnwindow") != std::string::npos))
            return Action::Respawn;
        if (leaf == "buttonbattle" && name.find("bottombuttonsholder") != std::string::npos) return Action::Start;
        if (text == "battle" || text == "battlenow" || text == "tobattle" || leaf == "btnbattlea" || name.find("buttonbattle") != std::string::npos)
            return Action::Battle;
        if (text == "start" || text == "play" || text == "go" || text == "fight" || text == "findmatch")
            return Action::Start;
        return Action::None;
    }

    inline bool IsModal(const std::string& path) {
        const std::string name = Normalize(path);
        if (name.find("networkstarttablengui") != std::string::npos &&
            name.find("endinterface") != std::string::npos) return true;
        for (const char* marker : {"popup", "dialog", "levelup", "rewardwindow", "rewardpanel", "rewardsscreen", "awardwindow", "progressroad", "newweapons", "newguns", "leveling"})
            if (name.find(marker) != std::string::npos) return true;
        return false;
    }

    // Native click callbacks bypass hit testing. Require a stable foreground
    // target so Battle cannot run underneath a reward or between its animations.
    struct ActionGate {
        Action candidate = Action::None;
        std::string identity;
        uint32_t candidateAt = 0, clickedAt = 0;
        bool clicked = false;

        bool Update(Action action, const std::string& target, bool paused, uint32_t now) {
            if (paused || action == Action::None || action != candidate || target != identity) {
                candidate = action;
                identity = target;
                candidateAt = now;
                return false;
            }
            const uint32_t settle = action == Action::Replay ? 3000 : 1000;
            return now - candidateAt >= settle && (!clicked || now - clickedAt >= 2000);
        }
        void Clicked(uint32_t now) { clicked = true; clickedAt = now; }
    };
}
