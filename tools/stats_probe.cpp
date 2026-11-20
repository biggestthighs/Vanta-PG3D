#include "../src/core/common.h"
#include "../src/sdk/il2cpp.h"
#include <fstream>
#include <set>
#include <cstring>
#include <stdexcept>

static DWORD WINAPI Probe(LPVOID module) {
    {
        std::ofstream output(DllDir() + L"\\stats_probe.txt");
        if (il2::Init() && output) {
            HMODULE assembly = GetModuleHandleW(L"GameAssembly.dll");
            auto fieldType = reinterpret_cast<void*(*)(void*)>(GetProcAddress(assembly, "il2cpp_field_get_type"));
            auto fieldFlags = reinterpret_cast<int(*)(void*)>(GetProcAddress(assembly, "il2cpp_field_get_flags"));
            auto returnType = reinterpret_cast<void*(*)(void*)>(GetProcAddress(assembly, "il2cpp_method_get_return_type"));
            auto typeName = reinterpret_cast<char*(*)(void*)>(GetProcAddress(assembly, "il2cpp_type_get_name"));
            auto freeMemory = reinterpret_cast<void(*)(void*)>(GetProcAddress(assembly, "il2cpp_free"));
            auto parentClass = reinterpret_cast<void*(*)(void*)>(GetProcAddress(assembly, "il2cpp_class_get_parent"));
            auto nameOf = [&](void* type) {
                if (!type || !typeName || !freeMemory) return std::string("?");
                char* name = typeName(type);
                std::string result = name ? name : "?";
                if (name) freeMemory(name);
                return result;
            };
            std::vector<void*> pending;
            for (const char* name : {"Player_move_c", "PlayerScoreController", "InGameGUI", "GameScores", "PlayerStatsController", "PlayerStats", "PunPlayerScores", "KillRateObject"})
                if (void* klass = il2::Class(name)) pending.push_back(klass);
            for (const auto& entry : {std::pair{"PhotonPlayer", ""}, std::pair{"Player", "Photon.Realtime"}, std::pair{"PhotonNetwork", "Photon.Pun"}})
                if (void* klass = il2::Class(entry.first, entry.second)) pending.push_back(klass);
            std::set<void*> seen;
            for (size_t index = 0; index < pending.size() && index < 25; ++index) {
                void* klass = pending[index];
                if (!klass || !seen.insert(klass).second) continue;
                const std::string className = il2::api.class_get_name(klass);
                output << "\nCLASS " << className << '\n';
                if (parentClass) {
                    output << "PARENT " << nameOf(il2::api.class_get_type(parentClass(klass))) << '\n';
                    if (className == "PlayerStatsController") pending.push_back(parentClass(klass));
                }
                void* iterator = nullptr;
                while (void* field = il2::api.class_get_fields(klass, &iterator)) {
                    const std::string name = il2::api.field_get_name(field);
                    void* type = fieldType ? fieldType(field) : nullptr;
                    output << "FIELD +0x" << std::hex << il2::api.field_get_offset(field) << std::dec << ' '
                           << nameOf(type) << ' ' << name << " flags=" << (fieldFlags ? fieldFlags(field) : 0) << '\n';
                    if (className == "Player_move_c" && (name == "myPersonNetwork" || name == "myScoreController" || name == "photonView"))
                        pending.push_back(il2::api.class_from_type(type));
                    if (className == "PlayerStats" && name == "KillRate") pending.push_back(il2::api.class_from_type(type));
                    if (className == "KillRateObject" && name == "_kills") pending.push_back(il2::api.class_from_type(type));
                }
                if (className == "Player_move_c" || className == "InGameGUI") continue;
                iterator = nullptr;
                while (void* method = il2::api.class_get_methods(klass, &iterator)) {
                    if (className == "PhotonView" && std::string(il2::api.method_get_name(method)) == "get_owner")
                        pending.push_back(il2::api.class_from_type(returnType(method)));
                    output << "METHOD " << nameOf(returnType ? returnType(method) : nullptr) << ' ' << il2::api.method_get_name(method) << '(';
                    const auto count = il2::api.method_get_param_count(method);
                    for (uint32_t parameter = 0; parameter < count; ++parameter) {
                        if (parameter) output << ", ";
                        output << nameOf(il2::api.method_get_param(method, parameter));
                    }
                    const auto pointer = reinterpret_cast<uintptr_t>(*static_cast<void**>(method));
                    output << ") RVA=0x" << std::hex << (pointer - reinterpret_cast<uintptr_t>(assembly)) << std::dec << '\n';
                }
            }
            try {
                auto fieldRead = reinterpret_cast<void(*)(void*, void*, void*)>(GetProcAddress(assembly, "il2cpp_field_get_value"));
                auto staticRead = reinterpret_cast<void(*)(void*, void*)>(GetProcAddress(assembly, "il2cpp_field_static_get_value"));
                auto invoke = reinterpret_cast<void*(*)(void*, void*, void**, void**)>(GetProcAddress(assembly, "il2cpp_runtime_invoke"));
                auto call = [&](void* method, void* self, void** arguments = nullptr) {
                    if (!method) throw std::runtime_error("method unavailable");
                    void* exception = nullptr;
                    void* result = invoke(method, self, arguments, &exception);
                    if (exception) throw std::runtime_error("managed exception");
                    return result;
                };
                void* statsClass = il2::Class("PlayerStatsController");
                void* singleton = parentClass(statsClass);
                void* iterator = nullptr;
                void* instance = nullptr;
                while (void* field = il2::api.class_get_fields(singleton, &iterator))
                    if ((fieldFlags(field) & 16) && il2::api.class_from_type(fieldType(field)) == statsClass) staticRead(field, &instance);
                if (!instance) throw std::runtime_error("no current stats controller");
                iterator = nullptr;
                void* rate = nullptr;
                while (void* field = il2::api.class_get_fields(statsClass, &iterator))
                    if (nameOf(fieldType(field)) == "KillRateObject") fieldRead(instance, field, &rate);
                if (!rate) throw std::runtime_error("no current kill-rate object");
                void* rateClass = il2::api.object_get_class(rate);
                void* boxed = call(il2::api.class_get_method_from_name(rateClass, "get_kills", 0), rate);
                void* wrapperClass = il2::api.object_get_class(boxed);
                iterator = nullptr;
                void* decimalGetter = nullptr;
                while (void* method = il2::api.class_get_methods(wrapperClass, &iterator))
                    if (!il2::api.method_get_param_count(method) && nameOf(returnType(method)) == "System.Decimal") decimalGetter = method;
                void* decimal = call(decimalGetter, static_cast<uint8_t*>(boxed) + 16);
                void* decimalClass = il2::api.object_get_class(decimal);
                void* argument = static_cast<uint8_t*>(decimal) + 16;
                void* integer = call(il2::api.class_get_method_from_name(decimalClass, "ToInt32", 1), nullptr, &argument);
                int count = -1;
                if (integer) memcpy(&count, static_cast<uint8_t*>(integer) + 16, sizeof(count));
                output << "\nLIVE MATCH KILLS " << count << '\n';
            } catch (const std::exception& error) { output << "\nLIVE READ " << error.what() << '\n'; }
        }
    }
    FreeLibraryAndExitThread(static_cast<HMODULE>(module), 0);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, Probe, module, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
