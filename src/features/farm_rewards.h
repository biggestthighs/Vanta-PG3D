#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include "farm_state.h"

namespace farm::rewards {
struct Entry { std::string kind, item; int64_t amount = 0; };
struct Total { std::string kind, item; int64_t match = 0, progress = 0; };
inline std::string Plain(const std::string& text) {
    return std::regex_replace(text,std::regex(R"(\[[^\]]*\]|<[^>]*>)"),"");
}
inline std::optional<Entry> Parse(const std::string& path, const std::string& label) {
    const auto text = Plain(label);
    if (std::regex_search(text,std::regex(R"((^|\s)-\s*[0-9])"))) return {};
    const auto context = logic::Normalize(path + " " + text);
    for (const auto* blocked : {"availableforpurchase","pressanywheretoclose","progressroad","tableranks","startinterface","bonuspercent"})
        if (context.find(blocked) != std::string::npos) return {};
    static const std::regex numbers(R"((?:\+|x|\xC3\x97)?\s*([0-9]+(?:[ ,][0-9]{3})*))");
    auto begin = std::sregex_iterator(text.begin(),text.end(),numbers);
    if (begin == std::sregex_iterator()) return {};
    const auto match = *begin;
    if (++begin != std::sregex_iterator()) return {}; // Multiple amounts need separate reward cells.
    auto digits = match[1].str();
    digits.erase(std::remove_if(digits.begin(),digits.end(),[](char c){return c==' ' || c==',';}),digits.end());
    int64_t amount = 0;
    try { amount = std::stoll(digits); } catch (...) { return {}; }
    if (amount <= 0 || amount > 1000000000) return {};
    std::string kind;
    if (context.find("passxp") != std::string::npos || context.find("pixelpassexp") != std::string::npos) kind="Pass XP";
    else if (context.find("coin") != std::string::npos) kind="Coins";
    else if (context.find("gem") != std::string::npos) kind="Gems";
    else if (context.find("keys") != std::string::npos || context.find("keyreward") != std::string::npos) kind="Keys";
    else if (context.find("troph") != std::string::npos) kind="Trophies";
    else if (context.find("parts") != std::string::npos || context.find("fragment") != std::string::npos) kind="Weapon parts";
    else if (context.find("weapon") != std::string::npos || context.find("newgun") != std::string::npos) kind="Weapons";
    else if (context.find("xp") != std::string::npos || context.find("experience") != std::string::npos || context.find("expprogressview") != std::string::npos) kind="XP";
    if (kind.empty()) return {};
    std::string item;
    if (kind=="Weapons" || kind=="Weapon parts") {
        item=std::regex_replace(text,numbers,"");
        item=std::regex_replace(item,std::regex("weapon parts|fragments|parts|weapons|weapon|new gun",std::regex::icase),"");
        const auto first=item.find_first_not_of(" \t\r\n:+");
        if (first==std::string::npos) return {}; // Require a displayed item name.
        item=item.substr(first,item.find_last_not_of(" \t\r\n:+")-first+1);
    }
    return Entry{kind,item,amount};
}
struct Tracker {
    std::map<std::string,Total> totals;
    std::map<std::string,std::map<std::string,int64_t>> counted;
    std::set<std::string> unread;
    void Commit(const std::string& event, bool progress, const std::vector<Entry>& entries) {
        if (entries.empty()) { if (!counted.count(event)) unread.insert(event); return; }
        unread.erase(event);
        std::map<std::string,int64_t> amounts;
        for (const auto& entry : entries) amounts[entry.kind+"\n"+entry.item] += entry.amount;
        auto& recorded = counted[event];
        for (const auto& entry : entries) {
            const auto key=entry.kind+"\n"+entry.item;
            auto& total=totals[key]; total.kind=entry.kind; total.item=entry.item;
            (progress ? total.progress : total.match) += std::max<int64_t>(0,amounts[key]-recorded[key]);
            recorded[key]=std::max(recorded[key],amounts[key]);
        }
    }
    std::vector<Total> Snapshot() const {
        std::vector<Total> result;
        for (const auto& [key,total] : totals) result.push_back(total);
        return result;
    }
};
}
