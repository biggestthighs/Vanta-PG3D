// Regression harness for the real loader's temporary and retained GC handles.
// No Unity process is required; only the allocation/invocation exports are mocked.
#ifndef CUSTOM_MODEL_SOURCE
#define CUSTOM_MODEL_SOURCE "../src/features/custommodel.cpp"
#endif
#include CUSTOM_MODEL_SOURCE
#include <deque>
#include <iostream>

// These unrelated entry points must never be called by the handle checks.
Cfg cfg{};
void Log(const char*, ...) {}
std::wstring DllDir() { throw std::runtime_error("unexpected file access"); }
Quat EulerToQuat(float, float, float) { throw std::runtime_error("unexpected rotation"); }
namespace game {
    Quat GetRotation(void*) { throw std::runtime_error("unexpected Unity call"); }
    Vec3 Forward(const Quat&) { throw std::runtime_error("unexpected Unity call"); }
}
namespace il2 {
    Api api{};
    void* TypeObject(void*) { throw std::runtime_error("unexpected reflection"); }
}

namespace {
    constexpr uintptr_t highHandle = 0x00000283C4934030ull;
    std::vector<uintptr_t> allocated, released;
    std::vector<void*> destroyed;
    std::deque<std::string> managedStrings;
    std::vector<std::string> shaderQueries;
    std::string availableShader;

    void* FakeString(const char* text) {
        managedStrings.emplace_back(text);
        return &managedStrings.back();
    }

    uintptr_t FakeNew(void*, bool) {
        const auto h = highHandle + allocated.size() * 16;
        allocated.push_back(h);
        return h;
    }
    void FakeFree(uintptr_t h) { released.push_back(h); }
    void* FakeInvoke(void* method, void*, void** args, void** exception) {
        *exception = nullptr;
        if (method == custommodel::shaderFind) {
            const auto& name = *static_cast<std::string*>(args[0]);
            shaderQueries.push_back(name);
            return name == availableShader ? reinterpret_cast<void*>(0x7000) : nullptr;
        }
        destroyed.push_back(args[0]);
        return nullptr;
    }
    void Require(bool pass, const char* message) {
        if (!pass) throw std::runtime_error(message);
    }
}

int main() {
    using namespace custommodel;
    // Cast deliberately: the old API declaration must reproduce the truncation.
    handleNew = reinterpret_cast<HandleNew>(&FakeNew);
    handleFree = reinterpret_cast<HandleFree>(&FakeFree);
    invoke = &FakeInvoke;
    shaderFind = reinterpret_cast<void*>(0x1010);
    il2::api.string_new = &FakeString;
    try {
        {
            Root temporary(reinterpret_cast<void*>(0x1000));
            Require(temporary.h == highHandle, "temporary handle lost its upper 32 bits");
        }
        Require(released.size() == 1 && released[0] == allocated[0], "temporary cleanup truncated handle");
        void* a = reinterpret_cast<void*>(0x2000);
        void* b = reinterpret_cast<void*>(0x3000);
        Track(a);
        Track(b);
        Require(resources[0].handle == allocated[1] && resources[1].handle == allocated[2],
                "retained handle lost its upper 32 bits");
        Clear();
        Require(released.size() == 3 && released[1] == allocated[2] && released[2] == allocated[1],
                "resource cleanup truncated handle or released it twice");
        Require(destroyed.size() == 2 && destroyed[0] == b && destroyed[1] == a && resources.empty(),
                "resource cleanup did not destroy objects in reverse order");
        Clear();
        Require(released.size() == 3, "repeated cleanup released handles twice");
        availableShader = "Unlit/Texture";
        Require(ModelShader() != nullptr && shaderQueries.size() == 1,
                "textured model unexpectedly requires another shader");
        shaderQueries.clear();
        availableShader = "Unlit/Transparent Cutout";
        Require(ModelShader() != nullptr && shaderQueries.size() == 2,
                "texture shader fallback failed");
        shaderQueries.clear();
        availableShader = "Sprites/Default";
        Require(ModelShader() != nullptr && shaderQueries.size() == 3,
                "included sprite shader fallback failed");
        shaderQueries.clear();
        availableShader.clear();
        bool rejected = false;
        try { ModelShader(); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "missing shaders must retain the original model");
        std::cout << "PASS: 64-bit handle cleanup, shader selection without Unlit/Color, and shader fallbacks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
