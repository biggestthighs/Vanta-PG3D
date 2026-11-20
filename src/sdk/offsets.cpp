#include "offsets.h"
#include "json.hpp"
#include <fstream>

namespace offsets {
    static std::unordered_map<std::string, void*> s_fns;
    static std::unordered_map<std::string, int> s_fields;

    static bool Valid(uintptr_t p) {
        if (!InModule(p, L"GameAssembly.dll")) return false;
        const uint8_t b = *reinterpret_cast<uint8_t*>(p);
        return b != 0xCC && b != 0x00;
    }

    bool Load() {
        const std::wstring path = DllDir() + L"\\offsets.json";
        std::ifstream f(path);
        if (!f) { Log("[off] offsets.json missing"); return false; }
        try {
            nlohmann::json j;
            f >> j;
            const uintptr_t base = ModuleBase(L"GameAssembly.dll");
            if (!base) { Log("[off] GameAssembly not loaded"); return false; }
            for (auto& kv : j["rva"].items()) {
                const uintptr_t p = base + kv.value().get<uint64_t>();
                if (Valid(p)) { s_fns[kv.key()] = reinterpret_cast<void*>(p); Log("[off] rva %s ok", kv.key().c_str()); }
                else Log("[off] rva %s stale", kv.key().c_str());
            }
            for (auto& kv : j["fields"].items()) s_fields[kv.key()] = kv.value().get<int>();
            return true;
        } catch (...) { Log("[off] parse failed"); return false; }
    }

    void* Fn(const char* key) {
        auto it = s_fns.find(key);
        return it == s_fns.end() ? nullptr : it->second;
    }

    int Field(const char* key) {
        auto it = s_fields.find(key);
        return it == s_fields.end() ? -1 : it->second;
    }
}
