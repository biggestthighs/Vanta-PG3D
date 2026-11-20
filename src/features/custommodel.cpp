#include "custommodel.h"
#include "../core/config.h"
#include "../sdk/il2cpp.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace custommodel {
    static const char* names[] = { "Original", "Tung Tung Sahur", "Minecraft Steve", "Verity Monster", "PDF Teto", "SCP-096", "Hatsune Miku" };
    static const wchar_t* files[] = { L"", L"tungtung.pgm", L"steve.pgm", L"verity.pgm", L"teto.pgm", L"scp096.pgm", L"miku.pgm" };
    const char* Name(int i) { return names[i >= 0 && i < Count ? i : 0]; }

    using Invoke = void*(*)(void*, void*, void**, void**);
    using ArrayNew = void*(*)(void*, uintptr_t);
    // Unity 6 IL2CPP GC handles are pointer-sized, including the return value.
    // Truncating one to uint32_t crashes il2cpp_gchandle_free on x64.
    using GCHandle = uintptr_t;
    static_assert(sizeof(GCHandle) == sizeof(void*));
    using HandleNew = GCHandle(*)(void*, bool);
    using HandleFree = void(*)(GCHandle);
    static Invoke invoke;
    static ArrayNew arrayNew;
    static HandleNew handleNew;
    static HandleFree handleFree;
    static bool ready = false;
    static std::atomic<bool> stopping{false}, stopped{true};

    struct Root {
        void* p = nullptr;
        GCHandle h = 0;
        explicit Root(void* v) : p(v), h(v ? handleNew(v, true) : 0) {
            if (!p || !h) throw std::runtime_error("managed allocation failed");
        }
        ~Root() { if (h) handleFree(h); }
        Root(const Root&) = delete;
    };

    // Exact namespace lookup prevents a game class named Material/Mesh shadowing Unity's class.
    static void* Klass(const char* name, const char* ns = "UnityEngine") {
        size_t n = 0;
        void** assemblies = static_cast<void**>(il2::api.domain_get_assemblies(il2::api.domain_get(), &n));
        for (size_t i = 0; assemblies && i < n; ++i) {
            void* k = il2::api.class_from_name(il2::api.assembly_get_image(assemblies[i]), ns, name);
            if (k) return k;
        }
        throw std::runtime_error(std::string("missing class ") + ns + "." + name);
    }
    static void* Method(void* k, const char* name, std::initializer_list<const char*> types) {
        void* iter = nullptr;
        while (void* m = il2::api.class_get_methods(k, &iter)) {
            if (strcmp(il2::api.method_get_name(m), name) || il2::api.method_get_param_count(m) != types.size()) continue;
            bool match = true; unsigned i = 0;
            for (const char* t : types) {
                void* pk = il2::api.class_from_type(il2::api.method_get_param(m, i++));
                if (!pk || strcmp(il2::api.class_get_name(pk), t)) { match = false; break; }
            }
            if (match) return m;
        }
        throw std::runtime_error(std::string("missing method ") + il2::api.class_get_name(k) + "." + name);
    }
    static void* Call(void* m, void* self, std::initializer_list<void*> args = {}) {
        void* ex = nullptr;
        void* ret = invoke(m, self, const_cast<void**>(args.begin()), &ex);
        if (ex) throw std::runtime_error(std::string("Unity exception in ") + il2::api.method_get_name(m));
        return ret;
    }

    static void *goK, *meshK, *texK, *matK, *v3K, *v2K, *intK, *byteK;
    static void *goCtor, *goAdd, *goTr, *goActive, *goLayer, *objDestroy, *objKeep;
    static void *trParent, *trPos, *trRot, *trScale, *camMask, *meshCtor, *meshIndex, *meshVerts, *meshNormals, *meshUv, *meshTris, *meshBounds, *meshUpload;
    static void *filterMesh, *rendererMat, *texCtor, *texLoad, *texApply, *texFilter, *matCtor, *matTexture, *shaderFind;
    static void *filterType, *rendererType;
    struct Resource { void* p; GCHandle handle; };
    static std::vector<Resource> resources;
    static void* root = nullptr;
    static void* rootTr = nullptr;
    static int selected = -1;
    static bool visible = false;
    static DWORD retryAt = 0;
    static int layer = 0;
    static float bounds[6]{};

    bool Init() {
        try {
            HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
            invoke = reinterpret_cast<Invoke>(GetProcAddress(ga, "il2cpp_runtime_invoke"));
            arrayNew = reinterpret_cast<ArrayNew>(GetProcAddress(ga, "il2cpp_array_new"));
            handleNew = reinterpret_cast<HandleNew>(GetProcAddress(ga, "il2cpp_gchandle_new"));
            handleFree = reinterpret_cast<HandleFree>(GetProcAddress(ga, "il2cpp_gchandle_free"));
            if (!invoke || !arrayNew || !handleNew || !handleFree) throw std::runtime_error("missing IL2CPP allocation exports");
            goK = Klass("GameObject"); meshK = Klass("Mesh"); texK = Klass("Texture2D"); matK = Klass("Material");
            v3K = Klass("Vector3"); v2K = Klass("Vector2"); intK = Klass("Int32", "System"); byteK = Klass("Byte", "System");
            goCtor = Method(goK, ".ctor", {"String"}); goAdd = Method(goK, "AddComponent", {"Type"});
            goTr = Method(goK, "get_transform", {}); goActive = Method(goK, "SetActive", {"Boolean"}); goLayer = Method(goK, "set_layer", {"Int32"});
            void* obj = Klass("Object");
            objDestroy = Method(obj, "Destroy", {"Object"}); objKeep = Method(obj, "DontDestroyOnLoad", {"Object"});
            void* tr = Klass("Transform");
            trParent = Method(tr, "SetParent", {"Transform", "Boolean"}); trPos = Method(tr, "set_position", {"Vector3"});
            trRot = Method(tr, "set_rotation", {"Quaternion"}); trScale = Method(tr, "set_localScale", {"Vector3"});
            camMask = Method(Klass("Camera"), "get_cullingMask", {});
            meshCtor = Method(meshK, ".ctor", {}); meshIndex = Method(meshK, "set_indexFormat", {"IndexFormat"});
            meshVerts = Method(meshK, "set_vertices", {"Vector3[]"}); meshNormals = Method(meshK, "set_normals", {"Vector3[]"});
            meshUv = Method(meshK, "set_uv", {"Vector2[]"}); meshTris = Method(meshK, "set_triangles", {"Int32[]"});
            meshBounds = Method(meshK, "RecalculateBounds", {}); meshUpload = Method(meshK, "UploadMeshData", {"Boolean"});
            void* fk = Klass("MeshFilter"); filterType = il2::TypeObject(fk); filterMesh = Method(fk, "set_sharedMesh", {"Mesh"});
            rendererType = il2::TypeObject(Klass("MeshRenderer")); rendererMat = Method(Klass("Renderer"), "set_sharedMaterial", {"Material"});
            texCtor = Method(texK, ".ctor", {"Int32", "Int32", "TextureFormat", "Boolean"});
            texLoad = Method(texK, "LoadRawTextureData", {"Byte[]"}); texApply = Method(texK, "Apply", {"Boolean", "Boolean"});
            texFilter = Method(Klass("Texture"), "set_filterMode", {"FilterMode"});
            matCtor = Method(matK, ".ctor", {"Shader"}); matTexture = Method(matK, "set_mainTexture", {"Texture"});
            shaderFind = Method(Klass("Shader"), "Find", {"String"});
            if (!filterType || !rendererType) throw std::runtime_error("missing component type objects");
            ready = true; stopped = false;
            Log("[models] Unity mesh/material/texture API ready: six custom models available; GC handles=%zu-bit", sizeof(GCHandle) * 8);
        } catch (const std::exception& e) { Log("[models] unavailable: %s", e.what()); }
        return ready;
    }
    static void* Track(void* p) {
        if (!p) throw std::runtime_error("Unity allocation returned null");
        GCHandle h = handleNew(p, true);
        if (!h) throw std::runtime_error("could not retain Unity object");
        resources.push_back({p, h});
        return p;
    }
    static void* New(void* klass) { return Track(il2::api.object_new(klass)); }
    void Hide() {
        if (!ready || !root || !visible) return;
        try { bool off = false; Call(goActive, root, {&off}); } catch (...) {}
        visible = false;
    }
    void Clear() {
        Hide();
        // Children/components are owned by the root. Meshes, materials and textures need explicit destruction.
        for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
            try { Call(objDestroy, nullptr, {it->p}); } catch (...) {}
            handleFree(it->handle);
        }
        resources.clear(); root = rootTr = nullptr;
    }
    void RequestShutdown() { stopping = true; if (!ready) stopped = true; }
    bool ShutdownDone() { return stopped.load(); }
    void ServiceShutdown() {
        if (stopping && !stopped) { Clear(); stopped = true; }
    }

    struct Reader {
        std::vector<uint8_t> bytes; size_t at = 0;
        explicit Reader(const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary | std::ios::ate);
            if (!in) throw std::runtime_error("model file missing");
            auto n = in.tellg();
            if (n < 36 || n > 128 * 1024 * 1024) throw std::runtime_error("invalid model size");
            bytes.resize(static_cast<size_t>(n)); in.seekg(0);
            if (!in.read(reinterpret_cast<char*>(bytes.data()), n)) throw std::runtime_error("model read failed");
        }
        const uint8_t* Take(size_t n) {
            if (n > bytes.size() - at) throw std::runtime_error("truncated model");
            auto p = bytes.data() + at; at += n; return p;
        }
        template<class T> T Get() { T v; memcpy(&v, Take(sizeof(T)), sizeof(T)); return v; }
    };
    static void FillArray(void* array, const void* src, size_t bytes) {
        // IL2CPP x64 single-dimensional array: object header, bounds pointer, max_length, payload.
        memcpy(static_cast<uint8_t*>(array) + 32, src, bytes);
    }
    static void* Shader(const char* name) {
        Root s(il2::api.string_new(name));
        return Call(shaderFind, nullptr, {s.p});
    }
    static void* ModelShader() {
        for (const char* candidate : {"Unlit/Texture", "Unlit/Transparent Cutout", "Sprites/Default"}) {
            if (void* shader = Shader(candidate)) {
                Log("[models] using shader '%s'", candidate);
                return shader;
            }
        }
        throw std::runtime_error("no compatible texture shader is available in this game");
    }
    static void* MakeTexture(int width, int height, const uint8_t* pixels, bool pixelated) {
        void* texture = New(texK); int rgba32 = 4; bool mip = false;
        Call(texCtor, texture, {&width, &height, &rgba32, &mip});
        const size_t bytes = size_t(width) * height * 4;
        Root array(arrayNew(byteK, bytes)); FillArray(array.p, pixels, bytes); Call(texLoad, texture, {array.p});
        bool discard = true; Call(texApply, texture, {&mip, &discard});
        int filter = pixelated ? 0 : 1; Call(texFilter, texture, {&filter});
        return texture;
    }
    static void Build(int index) {
        Log("[models] loading %s", Name(index));
        Reader r(std::filesystem::path(DllDir()) / L"models" / files[index]);
        if (memcmp(r.Take(4), "PGM1", 4)) throw std::runtime_error("bad model signature");
        uint32_t ns = r.Get<uint32_t>(), nt = r.Get<uint32_t>();
        if (!ns || ns > 128 || nt > 128) throw std::runtime_error("invalid model counts");
        for (float& b : bounds) { b = r.Get<float>(); if (!std::isfinite(b)) throw std::runtime_error("invalid bounds"); }
        if (bounds[4] - bounds[1] < 0.0001f) throw std::runtime_error("zero model height");
        Clear();
        Root shader(ModelShader());
        root = New(goK); Root name(il2::api.string_new("CustomThirdPerson")); Call(goCtor, root, {name.p});
        bool off = false; Call(goActive, root, {&off}); Call(objKeep, nullptr, {root});
        rootTr = Call(goTr, root);
        std::vector<void*> textures;
        for (uint32_t i = 0; i < nt; ++i) {
            int w = r.Get<uint32_t>(), h = r.Get<uint32_t>();
            if (w < 1 || h < 1 || w > 4096 || h > 4096) throw std::runtime_error("invalid texture size");
            size_t n = size_t(w) * h * 4; const uint8_t* px = r.Take(n);
            textures.push_back(MakeTexture(w, h, px, index == 2));
        }
        uint64_t total = 0;
        for (uint32_t s = 0; s < ns; ++s) {
            int ti = r.Get<int32_t>(); Col color;
            color.r = r.Get<float>(); color.g = r.Get<float>(); color.b = r.Get<float>(); color.a = r.Get<float>();
            if (!std::isfinite(color.r + color.g + color.b + color.a)) throw std::runtime_error("invalid material color");
            uint32_t nv = r.Get<uint32_t>(); total += nv;
            if (ti < -1 || ti >= int(nt) || !nv || nv % 3 || nv > 3000000 || total > 6000000) throw std::runtime_error("invalid submesh");
            const uint8_t* data = r.Take(size_t(nv) * 32);
            std::vector<float> pos(nv * 3), normal(nv * 3), uv(nv * 2); std::vector<int> tri(nv);
            // Centre horizontally and put the feet at y=0, then scale uniformly at runtime.
            const float cx = (bounds[0] + bounds[3]) * .5f, cz = (bounds[2] + bounds[5]) * .5f;
            for (uint32_t j = 0; j < nv; ++j) {
                float v[8]; memcpy(v, data + size_t(j) * 32, 32);
                for (float a : v) if (!std::isfinite(a)) throw std::runtime_error("nonfinite mesh vertex");
                pos[j*3] = v[0] - cx; pos[j*3+1] = v[1] - bounds[1]; pos[j*3+2] = v[2] - cz;
                memcpy(&normal[j*3], v+3, 12); memcpy(&uv[j*2], v+6, 8);
                // Blender's front faces are counterclockwise; Unity's are clockwise.
                tri[j] = (j / 3) * 3 + (j % 3 == 1 ? 2 : j % 3 == 2 ? 1 : 0);
            }
            void* mesh = New(meshK); Call(meshCtor, mesh); int u32 = 1; Call(meshIndex, mesh, {&u32});
            Root pa(arrayNew(v3K, nv)), na(arrayNew(v3K, nv)), ua(arrayNew(v2K, nv)), ia(arrayNew(intK, nv));
            FillArray(pa.p, pos.data(), pos.size()*4); FillArray(na.p, normal.data(), normal.size()*4);
            FillArray(ua.p, uv.data(), uv.size()*4); FillArray(ia.p, tri.data(), tri.size()*4);
            Call(meshVerts, mesh, {pa.p}); Call(meshNormals, mesh, {na.p}); Call(meshUv, mesh, {ua.p}); Call(meshTris, mesh, {ia.p});
            Call(meshBounds, mesh); bool discard = true; Call(meshUpload, mesh, {&discard});
            void* texture = nullptr;
            if (ti >= 0) texture = textures[ti];
            else {
                uint8_t pixel[4];
                const float channels[] = {color.r, color.g, color.b, color.a};
                for (size_t channel = 0; channel < 4; ++channel)
                    pixel[channel] = static_cast<uint8_t>(std::clamp(channels[channel], 0.f, 1.f) * 255.f + .5f);
                texture = MakeTexture(1, 1, pixel, true);
            }
            void* mat = New(matK); Call(matCtor, mat, {shader.p});
            Call(matTexture, mat, {texture});
            // Child GameObjects belong to root; do not also destroy their components individually.
            Root child(il2::api.object_new(goK)); Call(goCtor, child.p, {name.p});
            void* tr = Call(goTr, child.p); bool local = false; Call(trParent, tr, {rootTr, &local});
            Vec3 zero{}, one{1,1,1}; Quat identity{};
            Call(trPos, tr, {&zero}); Call(trRot, tr, {&identity}); Call(trScale, tr, {&one});
            Call(goLayer, child.p, {&layer});
            Root mf(Call(goAdd, child.p, {filterType})), mr(Call(goAdd, child.p, {rendererType}));
            Call(filterMesh, mf.p, {mesh}); Call(rendererMat, mr.p, {mat});
        }
        if (r.at != r.bytes.size()) throw std::runtime_error("trailing data in model");
        Log("[models] loaded %s: %u submeshes, %u textures, %llu triangles", Name(index), ns, nt, total/3);
    }
    bool Update(const game::Player& p, void* camera) {
        ServiceShutdown();
        if (!ready || stopping) return false;
        int i = cfg.misc.tp_model;
        if (cfg.panic || !cfg.misc.third_person || !p.alive || i <= 0 || i >= Count) { Hide(); return false; }
        try {
            if (selected != i) { Clear(); selected = i; retryAt = 0; }
            if (!root) {
                if (retryAt && GetTickCount() - retryAt < 5000) return false;
                retryAt = GetTickCount();
                // Use a layer already visible to this camera, with no change to its mask.
                void* boxed = Call(camMask, camera);
                int mask = 1; if (boxed) memcpy(&mask, static_cast<uint8_t*>(boxed) + 16, 4);
                layer = 0; for (int b = 0; b < 32; ++b) if (uint32_t(mask) & (1u << b)) { layer = b; break; }
                Build(i);
            }
            float span = p.haveHead ? p.headPos.y - p.pos.y + .15f : 1.8f;
            float size = std::clamp(cfg.misc.tp_model_scale, .25f, 3.f) * std::clamp(span, .7f, 4.f) / (bounds[4] - bounds[1]);
            Vec3 scale{size,size,size}, pos = p.pos; pos.y += std::clamp(cfg.misc.tp_model_height, -2.f, 2.f);
            Vec3 f = game::Forward(game::GetRotation(p.tr));
            const float modelFacing = i == 1 ? -90.f : 180.f;
            float yaw = atan2f(f.x, f.z) * 57.2957795f + modelFacing + cfg.misc.tp_model_yaw;
            Quat q = EulerToQuat(0, yaw, 0);
            Call(trPos, rootTr, {&pos}); Call(trRot, rootTr, {&q}); Call(trScale, rootTr, {&scale});
            if (!visible) { bool on = true; Call(goActive, root, {&on}); visible = true; }
            return true;
        } catch (const std::exception& e) {
            Log("[models] %s: %s (keeping original model)", Name(i), e.what()); Clear(); retryAt = GetTickCount(); return false;
        }
    }
}
