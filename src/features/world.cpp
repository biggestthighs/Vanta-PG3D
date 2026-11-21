#include "world.h"
#include "../core/atmosphere.h"
#include "../sdk/il2cpp.h"
#include "../sdk/game.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace world {
namespace {
    using Invoke = void*(*)(void*, void*, void**, void**);
    using Handle = uintptr_t;
    Invoke invoke = nullptr;
    void* (*unbox)(void*) = nullptr;
    void* (*arrayNew)(void*, uintptr_t) = nullptr;
    Handle (*retain)(void*, bool) = nullptr;
    void (*release)(Handle) = nullptr;

    void *activeScene, *getSky, *setSky, *getFog, *setFog, *getMode, *setMode;
    void *getDensity, *setDensity, *getColor, *setColor;
    void *findShader, *destroy, *matType, *matCtor, *matSetTexture, *matSetColor, *matQueue;
    void *texType, *byteType, *texCtor, *texLoad, *texApply;
    void *objectAlive, *cameraGetClear, *cameraSetClear, *getComponent, *skyboxType;
    void *allCameras, *cameraDepth, *cameraDisplay, *cameraTarget, *cameraRect, *cameraMask, *cameraFar;
    void *goTransform, *transformPosition;
    void *getLocalSky, *setLocalSky;
    void *getAmbientMode, *setAmbientMode, *getSun, *getSunColor, *setSunColor;
    std::array<void*,3> getAmbient{}, setAmbient{};
    void *goType, *meshType, *vecType, *uvType, *colorType, *intType;
    void *filterType, *rendererType, *goCtor, *goAdd, *goLayer, *meshCtor, *meshDynamic;
    void *meshVertices, *meshUv, *meshIndices, *meshColors, *meshBounds;
    void *filterMesh, *rendererMaterial, *rendererEnabled, *rendererShadows, *rendererReceive;
    std::atomic<bool> stopping{false}, stopped{true};
    std::atomic<const char*> skyStatus{"Waiting for a match"}, fogStatus{"Waiting for a match"};
    std::atomic<const char*> particleStatus{"Waiting for a match"}, lightStatus{"Waiting for a match"};
    std::mutex optionsMutex;
    WorldCfg published;
    bool paused = false, nativeSky = false, nativeFog = false;
    bool skyFailed = false, fogFailed = false;
    bool nativeParticles = false, nativeLighting = false, particlesFailed = false, lightFailed = false;
    ULONGLONG nextTick = 0;
    ULONGLONG particleEpoch = 0;
    std::optional<int> scene;

    struct Object {
        void* value = nullptr;
        Handle handle = 0;
        void Clear() {
            if (handle) release(handle);
            value = nullptr;
            handle = 0;
        }
        void Set(void* ptr) {
            Clear();
            if (!ptr) return;
            handle = retain(ptr, true);
            if (!handle) throw std::runtime_error("Unity object could not be retained");
            value = ptr;
        }
        ~Object() { Clear(); }
        Object() = default;
        Object(const Object&) = delete;
    };
    Object previousSky, material, texture, cameraRef, skyboxRef, previousLocalSky, backgroundCamera;
    void* lastMainCamera = nullptr;
    ULONGLONG nextCameraScan = 0;
    Object particleRoot, particleMesh, particleMaterial, particleTexture, particleRenderer, vertices, colors;
    Object sceneSun;
    Object domeRoot, domeMesh, domeMaterial, domeRenderer;
    float domeRadius = 0.f;
    bool domeVisible = false;
    void HideSkyDome();
    void ClearSkyDome();
    void ApplySkyDome(void* camera);
    std::optional<int> previousClear;
    bool assignedLocalSky = false, particlesVisible = false;
    int particleLayer = -1;
    bool assignedSky = false, panoramic = false, skyShadersResolved = false;
    int materialKey = -1;
    struct Fog { bool enabled; int mode; float density; Col color; };
    std::optional<Fog> previousFog, appliedFog;
    struct LightState { int mode; std::array<Col,3> ambient; Col sun; };
    std::optional<LightState> previousLight, appliedLight;
    constexpr int PerCell = 88;
    constexpr int ParticleCount = 27 * PerCell;

    void* Class(const char* ns, const char* name) {
        size_t count = 0;
        auto assemblies = static_cast<void**>(il2::api.domain_get_assemblies(il2::api.domain_get(), &count));
        for (size_t i = 0; assemblies && i < count; ++i)
            if (auto* type = il2::api.class_from_name(il2::api.assembly_get_image(assemblies[i]), ns, name)) return type;
        return nullptr;
    }

    void* Method(void* type, const char* name, std::initializer_list<const char*> signature = {}) {
        if (!type) return nullptr;
        void* iterator = nullptr;
        while (void* method = il2::api.class_get_methods(type, &iterator)) {
            if (std::strcmp(il2::api.method_get_name(method), name) ||
                il2::api.method_get_param_count(method) != signature.size()) continue;
            bool matches = true;
            unsigned index = 0;
            for (const char* expected : signature) {
                auto* actual = il2::api.class_from_type(il2::api.method_get_param(method, index++));
                if (!actual || std::strcmp(il2::api.class_get_name(actual), expected)) {
                    matches = false;
                    break;
                }
            }
            if (matches) return method;
        }
        return nullptr;
    }

    void* Call(void* method, void* instance = nullptr, std::initializer_list<void*> args = {}) {
        void* exception = nullptr;
        void* result = invoke(method, instance, const_cast<void**>(args.begin()), &exception);
        if (exception) throw std::runtime_error(il2::api.method_get_name(method));
        return result;
    }
    template<class T> T Unbox(void* box) {
        auto* data = box ? unbox(box) : nullptr;
        if (!data) throw std::runtime_error("Unity value is missing");
        T value;
        std::memcpy(&value, data, sizeof(value));
        return value;
    }
    template<class T> T Get(void* method, void* self = nullptr) { return Unbox<T>(Call(method,self)); }
    template<class T> void Set(void* method, T value, void* self = nullptr) { Call(method,self,{&value}); }
    bool Alive(const Object& object) {
        return object.value && Unbox<bool>(Call(objectAlive,nullptr,{object.value}));
    }
    bool SameColor(Col a, Col b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }
    Fog ReadFog() { return {Get<bool>(getFog), Get<int>(getMode), Get<float>(getDensity), Get<Col>(getColor)}; }

    void RestoreFog() {
        if (!appliedFog) return;
        const auto before = *previousFog;
        const auto last = *appliedFog;
        const auto current = ReadFog();
        // Preserve any setting the scene changed after our last write.
        if (current.enabled == last.enabled) Set(setFog, before.enabled);
        if (current.mode == last.mode) Set(setMode, before.mode);
        if (current.density == last.density) Set(setDensity, before.density);
        if (SameColor(current.color, last.color)) Set(setColor, before.color);
        appliedFog.reset();
        previousFog.reset();
    }

    void ApplyFog(const WorldCfg& options) {
        const auto current = ReadFog();
        if (!previousFog) previousFog = current;
        if (appliedFog) {
            if (current.enabled != appliedFog->enabled) previousFog->enabled = current.enabled;
            if (current.mode != appliedFog->mode) previousFog->mode = current.mode;
            if (current.density != appliedFog->density) previousFog->density = current.density;
            if (!SameColor(current.color, appliedFog->color)) previousFog->color = current.color;
        }
        const auto color = atmosphere::Colors(options.palette).mist;
        constexpr int exponential = 2;
        float density = options.fogDensity;
        if (previousFog->enabled && previousFog->mode == exponential)
            density = std::max(density, previousFog->density);
        appliedFog = Fog{true, exponential, density, color};
        if (current.mode != exponential) Set(setMode, exponential);
        if (current.density != density) Set(setDensity, density);
        if (!SameColor(current.color, color)) Set(setColor, color);
        if (!current.enabled) Set(setFog, true);
        fogStatus = "Active / distance fog";
    }

    void RestoreCameraSky() {
        if (assignedLocalSky && Alive(skyboxRef) && Call(getLocalSky,skyboxRef.value) == material.value)
            Call(setLocalSky,skyboxRef.value,{previousLocalSky.value});
        if (previousClear && Alive(cameraRef) && Get<int>(cameraGetClear,cameraRef.value) == 1)
            Set(cameraSetClear,*previousClear,cameraRef.value);
        assignedLocalSky = false;
        previousClear.reset();
        previousLocalSky.Clear();
        skyboxRef.Clear();
        cameraRef.Clear();
    }
    void RestoreSky() {
        HideSkyDome();
        RestoreCameraSky();
        if (assignedSky && Call(getSky) == material.value) Call(setSky, nullptr, {previousSky.value});
        assignedSky = false;
        previousSky.Clear();
    }
    void Destroy(Object& object) {
        if (object.value) Call(destroy, nullptr, {object.value});
        object.Clear();
    }
    void* Shader(const char* name) {
        Object text;
        text.Set(il2::api.string_new(name));
        return Call(findShader, nullptr, {text.value});
    }
    void UploadTexture(Object& output, int width, int height, const std::vector<uint8_t>& pixels) {
        int rgba32 = 4;
        bool mip = false, makeUnreadable = true;
        output.Set(il2::api.object_new(texType));
        if (!output.value) throw std::runtime_error("Unity texture allocation failed");
        Call(texCtor,output.value,{&width,&height,&rgba32,&mip});
        Object data;
        data.Set(arrayNew(byteType,pixels.size()));
        if (!data.value) throw std::runtime_error("Unity texture data allocation failed");
        std::memcpy(static_cast<uint8_t*>(data.value) + 32,pixels.data(),pixels.size());
        Call(texLoad,output.value,{data.value});
        Call(texApply,output.value,{&mip,&makeUnreadable});
    }
    void MakeSky(const WorldCfg& options, int key) {
        RestoreSky();
        ClearSkyDome();
        Destroy(material);
        Destroy(texture);
        materialKey = -1;
        Object shader;
        shader.Set(Shader("Skybox/Panoramic"));
        panoramic = shader.value != nullptr;
        if (!panoramic) shader.Set(Shader("Skybox/Procedural"));
        skyShadersResolved = true;
        if (!shader.value) throw std::runtime_error("skybox shaders were stripped from this build");
        material.Set(il2::api.object_new(matType));
        if (!material.value) throw std::runtime_error("sky material allocation failed");
        Call(matCtor, material.value, {shader.value});
        Object property;
        constexpr int width = 2048, height = 1024;
        std::vector<uint8_t> pixels;
        atmosphere::Panorama(pixels, width, height, options.palette, options.stars);
        UploadTexture(texture,width,height,pixels);
        if (panoramic) {
            property.Set(il2::api.string_new("_MainTex"));
            Call(matSetTexture, material.value, {property.value, texture.value});
        } else {
            auto color = atmosphere::Colors(options.palette).horizon;
            property.Set(il2::api.string_new("_SkyTint"));
            Call(matSetColor, material.value, {property.value, &color});
            color = atmosphere::Colors(options.palette).zenith;
            property.Set(il2::api.string_new("_GroundColor"));
            Call(matSetColor, material.value, {property.value, &color});
        }
        materialKey = key;
    }
    void* SkyCamera(void* main, ULONGLONG now) {
        if (!objectAlive || !cameraGetClear || !cameraSetClear) return nullptr;
        const int flags = Get<int>(cameraGetClear,main);
        if (flags == 1 || flags == 2) return main;
        if (!allCameras || !cameraDepth || !cameraDisplay || !cameraTarget || !cameraRect) return nullptr;
        if (lastMainCamera == main && now < nextCameraScan)
            return Alive(backgroundCamera) ? backgroundCamera.value : nullptr;
        lastMainCamera = main;
        nextCameraScan = now+500;
        const auto* previous = backgroundCamera.value;
        backgroundCamera.Clear();
        Object cameras;
        cameras.Set(Call(allCameras));
        if (!cameras.value) return nullptr;
        auto* raw = static_cast<uint8_t*>(cameras.value);
        uintptr_t count = 0;
        std::memcpy(&count,raw+24,sizeof(count));
        if (count > 64) return nullptr;
        auto** entries = reinterpret_cast<void**>(raw+32);
        struct Rect { float x,y,w,h; };
        auto* output = Call(cameraTarget,main);
        const auto display = Get<int>(cameraDisplay,main);
        const auto viewport = Get<Rect>(cameraRect,main);
        const auto foregroundDepth = Get<float>(cameraDepth,main);
        auto bestDepth = -1000000.f;
        for (uintptr_t i=0; i<count; ++i) {
            auto* candidate = entries[i];
            if (!candidate || candidate == main ||
                !Unbox<bool>(Call(objectAlive,nullptr,{candidate}))) continue;
            const auto clear = Get<int>(cameraGetClear,candidate);
            if (clear != 1 && clear != 2) continue;
            const auto depth = Get<float>(cameraDepth,candidate);
            if (depth >= foregroundDepth || depth <= bestDepth ||
                Get<int>(cameraDisplay,candidate) != display || Call(cameraTarget,candidate) != output) continue;
            const auto rect = Get<Rect>(cameraRect,candidate);
            if (rect.x != viewport.x || rect.y != viewport.y || rect.w != viewport.w || rect.h != viewport.h) continue;
            backgroundCamera.Set(candidate);
            bestDepth = depth;
        }
        if (backgroundCamera.value && backgroundCamera.value != previous)
            Log("[world] selected an earlier background camera for the main view");
        return backgroundCamera.value;
    }
    void ApplySky(const WorldCfg& options, void* mainCamera, ULONGLONG now) {
        const int key = options.palette * 2 + options.stars;
        if (materialKey != key)
            MakeSky(options, key);
        auto* camera = SkyCamera(mainCamera,now);
        if (cameraRef.value && cameraRef.value != camera) RestoreSky();
        auto* current = Call(getSky);
        if (current != material.value) {
            previousSky.Set(current);
            assignedSky = true;
            Call(setSky, nullptr, {material.value});
            Log("[world] applied %s map sky",panoramic ? "panoramic" : "procedural");
        }
        if (camera) {
            if (!cameraRef.value) {
                cameraRef.Set(camera);
                if (getComponent && skyboxType && getLocalSky && setLocalSky)
                    skyboxRef.Set(Call(getComponent,camera,{skyboxType}));
            }
            const int clear = Get<int>(cameraGetClear,camera);
            if (clear == 2) { // SolidColor is a base-camera background. Depth-only cameras retain their role.
                previousClear = clear;
                Set(cameraSetClear,1,camera);
            }
            if (Alive(skyboxRef) && Call(getLocalSky,skyboxRef.value) != material.value) {
                previousLocalSky.Set(Call(getLocalSky,skyboxRef.value));
                assignedLocalSky = true;
                Call(setLocalSky,skyboxRef.value,{material.value});
                Log("[world] applied camera skybox override");
            }
        }
        if (!camera && cameraGetClear && Get<int>(cameraGetClear,mainCamera) >= 3) {
            skyStatus = "Map background camera not identified";
            return;
        }
        if (!panoramic) ApplySkyDome(mainCamera);
        skyStatus = panoramic ? "Applied / panoramic sky" :
            domeVisible ? "Applied / textured sky dome" : "Applied / sky tint (dome unavailable)";
    }

    Col Tint(Col stock, Col shade, float strength) {
        return {stock.r * std::lerp(1.f, shade.r, strength),
                stock.g * std::lerp(1.f, shade.g, strength),
                stock.b * std::lerp(1.f, shade.b, strength), stock.a};
    }
    LightState ReadLight() {
        LightState light{};
        light.mode = Get<int>(getAmbientMode);
        for (size_t i=0; i<3; ++i) light.ambient[i] = Get<Col>(getAmbient[i]);
        if (Alive(sceneSun)) light.sun = Get<Col>(getSunColor,sceneSun.value);
        return light;
    }
    void RestoreLight() {
        if (!appliedLight) return;
        const auto current = ReadLight();
        for (size_t i=0; i<3; ++i)
            if (SameColor(current.ambient[i],appliedLight->ambient[i]))
                Set(setAmbient[i],previousLight->ambient[i]);
        if (current.mode == appliedLight->mode) Set(setAmbientMode,previousLight->mode);
        if (Alive(sceneSun) && SameColor(current.sun,appliedLight->sun))
            Set(setSunColor,previousLight->sun,sceneSun.value);
        previousLight.reset();
        appliedLight.reset();
        sceneSun.Clear();
    }
    void ApplyLight(const WorldCfg& options) {
        if (getSun && getSunColor && setSunColor) {
            auto* sun = Call(getSun);
            if (sun != sceneSun.value) {
                RestoreLight();
                sceneSun.Set(sun);
            }
        }
        const auto current = ReadLight();
        if (!previousLight) {
            previousLight = current;
            Log("[world] map lighting enabled (sun=%d)", Alive(sceneSun));
        } else if (appliedLight) {
            if (current.mode != appliedLight->mode) previousLight->mode = current.mode;
            for (size_t i=0; i<3; ++i)
                if (!SameColor(current.ambient[i],appliedLight->ambient[i])) previousLight->ambient[i] = current.ambient[i];
            if (!SameColor(current.sun,appliedLight->sun)) previousLight->sun = current.sun;
        }
        const auto& palette = atmosphere::Colors(options.palette);
        const auto amount = std::clamp(options.intensity,0.f,1.f);
        constexpr int gradientLighting = 1;
        appliedLight = *previousLight;
        appliedLight->mode = gradientLighting;
        const std::array<Col,3> tint{palette.sparks,palette.horizon,palette.mist};
        for (size_t i=0; i<3; ++i) {
            const auto stock = previousLight->ambient[i];
            const float blend = amount * .92f;
            const float brightness = i == 0 ? .72f : i == 1 ? .46f : .20f;
            appliedLight->ambient[i] = {std::lerp(stock.r,tint[i].r*brightness,blend),
                std::lerp(stock.g,tint[i].g*brightness,blend),
                std::lerp(stock.b,tint[i].b*brightness,blend),stock.a};
            if (!SameColor(current.ambient[i],appliedLight->ambient[i]))
                Set(setAmbient[i],appliedLight->ambient[i]);
        }
        if (current.mode != gradientLighting) Set(setAmbientMode,gradientLighting);
        if (Alive(sceneSun)) {
            appliedLight->sun = Tint(previousLight->sun,palette.horizon,amount*.94f);
            if (!SameColor(current.sun,appliedLight->sun)) Set(setSunColor,appliedLight->sun,sceneSun.value);
        }
        lightStatus = "Applied / live scene lighting";
    }

    template<class T> T* Array(Object& output, void* type, size_t size) {
        output.Set(arrayNew(type,size));
        if (!output.value) throw std::runtime_error("Unity mesh array allocation failed");
        return reinterpret_cast<T*>(static_cast<uint8_t*>(output.value) + 32);
    }
    void ParticleVisibility(bool show) {
        if (particlesVisible == show) return;
        if (Alive(particleRenderer)) Set(rendererEnabled,show,particleRenderer.value);
        particlesVisible = show;
    }
    void ClearParticles() {
        ParticleVisibility(false);
        particleLayer = -1;
        particleRenderer.Clear();
        Destroy(particleRoot);
        Destroy(particleMesh);
        Destroy(particleMaterial);
        Destroy(particleTexture);
        vertices.Clear();
        colors.Clear();
    }
    void HideSkyDome() {
        if (domeVisible && Alive(domeRenderer)) Set(rendererEnabled,false,domeRenderer.value);
        domeVisible = false;
    }
    void ClearSkyDome() {
        HideSkyDome();
        domeRenderer.Clear();
        Destroy(domeRoot);
        Destroy(domeMesh);
        Destroy(domeMaterial);
        domeRadius = 0.f;
    }
    void ApplySkyDome(void* camera) {
        if (!nativeParticles || !cameraFar || !goTransform || !transformPosition || !cameraMask || !goLayer || !matQueue)
            return;
        const float farClip = Get<float>(cameraFar,camera);
        const auto mask = Get<uint32_t>(cameraMask,camera);
        if (!std::isfinite(farClip) || farClip < 2.f || !mask) { HideSkyDome(); return; }
        const float radius = farClip * .90f;
        if (!Alive(domeRoot) || !Alive(domeRenderer) || domeRadius != radius) {
            ClearSkyDome();
            Object shader, name;
            shader.Set(Shader("Sprites/Default"));
            if (!shader.value) return;
            name.Set(il2::api.string_new("Vanta Atmosphere Sky"));
            domeRoot.Set(il2::api.object_new(goType));
            domeMesh.Set(il2::api.object_new(meshType));
            domeMaterial.Set(il2::api.object_new(matType));
            Call(goCtor,domeRoot.value,{name.value});
            Object filter;
            filter.Set(Call(goAdd,domeRoot.value,{filterType}));
            domeRenderer.Set(Call(goAdd,domeRoot.value,{rendererType}));
            Set(rendererEnabled,false,domeRenderer.value);
            if (rendererShadows) Set(rendererShadows,0,domeRenderer.value);
            if (rendererReceive) Set(rendererReceive,false,domeRenderer.value);
            Call(meshCtor,domeMesh.value);
            constexpr int segments = 96, rings = 48;
            constexpr int count = (segments+1)*(rings+1);
            Object positions, uv, triangles, tint;
            auto* points = Array<Vec3>(positions,vecType,count);
            auto* texcoords = Array<ImVec2>(uv,uvType,count);
            auto* shades = Array<Col>(tint,colorType,count);
            auto* indices = Array<int>(triangles,intType,segments*rings*6);
            for (int y=0; y<=rings; ++y) for (int x=0; x<=segments; ++x) {
                const float u=float(x)/segments, v=float(y)/rings;
                const float latitude=(v-.5f)*3.14159265f;
                const float angle=u*6.2831853f;
                const int i=y*(segments+1)+x;
                points[i]={radius*std::cos(latitude)*std::sin(angle),radius*std::sin(latitude),
                    radius*std::cos(latitude)*std::cos(angle)};
                texcoords[i]={u,v};
                shades[i]={1,1,1,1};
            }
            int index=0;
            for (int y=0; y<rings; ++y) for (int x=0; x<segments; ++x) {
                const int a=y*(segments+1)+x, b=a+segments+1;
                indices[index++]=a; indices[index++]=b; indices[index++]=a+1;
                indices[index++]=a+1; indices[index++]=b; indices[index++]=b+1;
            }
            Call(meshVertices,domeMesh.value,{positions.value});
            Call(meshUv,domeMesh.value,{uv.value});
            Call(meshColors,domeMesh.value,{tint.value});
            Call(meshIndices,domeMesh.value,{triangles.value});
            Call(meshBounds,domeMesh.value);
            Call(matCtor,domeMaterial.value,{shader.value});
            // Draw after opaque map geometry and before transparent world particles.
            Set(matQueue,2490,domeMaterial.value);
            name.Set(il2::api.string_new("_MainTex"));
            Call(matSetTexture,domeMaterial.value,{name.value,texture.value});
            Call(filterMesh,filter.value,{domeMesh.value});
            Call(rendererMaterial,domeRenderer.value,{domeMaterial.value});
            domeRadius = radius;
            Log("[world] textured sky dome enabled (panoramic shader unavailable)");
        }
        Set(goLayer,int(std::countr_zero(mask)),domeRoot.value);
        auto* transform=Call(goTransform,domeRoot.value);
        const auto position=game::CamPos();
        Set(transformPosition,position,transform);
        Set(rendererEnabled,true,domeRenderer.value);
        domeVisible = true;
    }
    void MakeParticles() {
        ClearParticles();
        Object shader;
        // Sprites/Default is transparent and uses the scene depth test.
        shader.Set(Shader("Sprites/Default"));
        if (!shader.value) throw std::runtime_error("transparent particle shader unavailable");
        Object name;
        name.Set(il2::api.string_new("Vanta World Particles"));
        particleRoot.Set(il2::api.object_new(goType));
        particleMesh.Set(il2::api.object_new(meshType));
        particleMaterial.Set(il2::api.object_new(matType));
        if (!particleRoot.value || !particleMesh.value || !particleMaterial.value)
            throw std::runtime_error("world particle allocation failed");
        Call(goCtor,particleRoot.value,{name.value});
        Object filter;
        filter.Set(Call(goAdd,particleRoot.value,{filterType}));
        particleRenderer.Set(Call(goAdd,particleRoot.value,{rendererType}));
        if (!filter.value || !particleRenderer.value) throw std::runtime_error("world particle renderer unavailable");
        Set(rendererEnabled,false,particleRenderer.value);
        if (rendererShadows) Set(rendererShadows,0,particleRenderer.value);
        if (rendererReceive) Set(rendererReceive,false,particleRenderer.value);
        Call(meshCtor,particleMesh.value);
        if (meshDynamic) Call(meshDynamic,particleMesh.value);
        auto* points = Array<Vec3>(vertices,vecType,ParticleCount*4);
        std::fill_n(points,ParticleCount*4,Vec3{});
        auto* tint = Array<Col>(colors,colorType,ParticleCount*4);
        std::fill_n(tint,ParticleCount*4,Col{1,1,1,0});
        Object uv, triangles;
        auto* uvData = Array<ImVec2>(uv,uvType,ParticleCount*4);
        auto* indices = Array<int>(triangles,intType,ParticleCount*6);
        for (int i=0; i<ParticleCount; ++i) {
            const int j=i*4, k=i*6;
            uvData[j]={0,0}; uvData[j+1]={0,1}; uvData[j+2]={1,1}; uvData[j+3]={1,0};
            indices[k]=j; indices[k+1]=j+1; indices[k+2]=j+2;
            indices[k+3]=j; indices[k+4]=j+2; indices[k+5]=j+3;
        }
        Call(meshVertices,particleMesh.value,{vertices.value});
        Call(meshUv,particleMesh.value,{uv.value});
        Call(meshColors,particleMesh.value,{colors.value});
        Call(meshIndices,particleMesh.value,{triangles.value});
        Call(matCtor,particleMaterial.value,{shader.value});
        constexpr int size=48;
        std::vector<uint8_t> sparkle(size*size*4,255);
        for (int y=0; y<size; ++y) for (int x=0; x<size; ++x) {
            const float a=std::abs((x+.5f)*2.f/size-1.f), b=std::abs((y+.5f)*2.f/size-1.f);
            const float d=a*a+b*b;
            const float halo=.40f*std::exp(-d*8.f);
            const float core=.63f*std::exp(-d*52.f);
            const float rays=.42f*std::exp(-std::min(a,b)*32.f - std::max(a,b)*2.8f);
            sparkle[(y*size+x)*4+3] = static_cast<uint8_t>(
                255*std::clamp((halo+core+rays)*(1.f-std::clamp(d,0.f,1.f)),0.f,1.f));
        }
        UploadTexture(particleTexture,size,size,sparkle);
        name.Set(il2::api.string_new("_MainTex"));
        Call(matSetTexture,particleMaterial.value,{name.value,particleTexture.value});
        Call(filterMesh,filter.value,{particleMesh.value});
        Call(rendererMaterial,particleRenderer.value,{particleMaterial.value});
        Log("[world] created %d depth-tested world particles",ParticleCount);
    }
    float Random(uint32_t seed) {
        seed ^= seed >> 16; seed *= 0x7feb352d;
        seed ^= seed >> 15; seed *= 0x846ca68b; seed ^= seed >> 16;
        return static_cast<float>(seed & 0xffffffu) / 16777216.f;
    }
    void ApplyParticles(const WorldCfg& options, ULONGLONG now, void* camera) {
        if (options.intensity <= 0.f) { ParticleVisibility(false); return; }
        if (!Alive(particleRoot) || !Alive(particleRenderer)) MakeParticles();
        if (cameraMask && goLayer) {
            const auto mask = Get<uint32_t>(cameraMask,camera);
            if (!mask) {
                ParticleVisibility(false);
                particleStatus = "Camera has no visible scene layers";
                return;
            }
            const int layer = std::countr_zero(mask);
            if (layer != particleLayer) {
                Set(goLayer,layer,particleRoot.value);
                particleLayer = layer;
            }
        }
        const auto cam = game::CamPos();
        const auto q = game::CamRotation();
        if (!std::isfinite(cam.x+cam.y+cam.z+q.x+q.y+q.z+q.w) ||
            std::max({std::abs(cam.x),std::abs(cam.y),std::abs(cam.z)}) > 100000.f) {
            ParticleVisibility(false);
            return;
        }
        const Vec3 right{1-2*(q.y*q.y+q.z*q.z),2*(q.x*q.y+q.w*q.z),2*(q.x*q.z-q.w*q.y)};
        const Vec3 up{2*(q.x*q.y-q.w*q.z),1-2*(q.x*q.x+q.z*q.z),2*(q.y*q.z+q.w*q.x)};
        auto* coords = reinterpret_cast<Vec3*>(static_cast<uint8_t*>(vertices.value)+32);
        auto* tints = reinterpret_cast<Col*>(static_cast<uint8_t*>(colors.value)+32);
        const auto tint = atmosphere::Colors(options.palette).sparks;
        if (!particleEpoch) particleEpoch = now;
        const float time = static_cast<float>((now-particleEpoch)/1000.0);
        constexpr float cell=16.f;
        int i=0;
        const int gx=static_cast<int>(std::floor(cam.x/cell));
        const int gy=static_cast<int>(std::floor(cam.y/cell));
        const int gz=static_cast<int>(std::floor(cam.z/cell));
        for (int x=gx-1; x<=gx+1; ++x) for (int y=gy-1; y<=gy+1; ++y) for (int z=gz-1; z<=gz+1; ++z) {
            const auto seed=uint32_t(x)*0x8da6b343u ^ uint32_t(y)*0xd8163841u ^ uint32_t(z)*0xcb1ab31fu;
            for (int n=0; n<PerCell; ++n,++i) {
                const auto s=seed ^ (uint32_t(n+1)*0x9e3779b9u);
                const float phase=time*.59f+Random(s+4)*6.2831853f;
                float fall=Random(s+1)+time*(options.palette==2 ? -.022f : .013f);
                fall -= std::floor(fall);
                const Vec3 position{(x+Random(s))*cell+.66f*std::sin(phase),
                                    (y+fall)*cell,
                                    (z+Random(s+2))*cell+.45f*std::cos(phase*.83f)};
                const auto distance=Dist(position,cam);
                const float alpha = options.intensity * (.65f+.35f*Random(s+5)) *
                    std::clamp((distance-.72f)*.9f,0.f,1.f) * std::clamp((30.f-distance)/14.f,0.f,1.f) *
                    std::clamp(std::min(fall,1.f-fall)*15.f,0.f,1.f);
                const float radius = (.055f+.20f*Random(s+3)) * (.65f+.55f*options.intensity);
                const float u[4]={-radius,-radius,radius,radius};
                const float v[4]={-radius,radius,radius,-radius};
                for (int k=0; k<4; ++k) {
                    coords[i*4+k]={position.x+right.x*u[k]+up.x*v[k],
                        position.y+right.y*u[k]+up.y*v[k],position.z+right.z*u[k]+up.z*v[k]};
                    tints[i*4+k]={tint.r,tint.g,tint.b,alpha};
                }
            }
        }
        Call(meshVertices,particleMesh.value,{vertices.value});
        Call(meshColors,particleMesh.value,{colors.value});
        Call(meshBounds,particleMesh.value);
        ParticleVisibility(true);
        particleStatus = "Applied / 3D particles";
    }
}

void Init(bool cameraCallbackReady) {
    if (!cameraCallbackReady || !il2::api.object_new || !il2::api.string_new || !il2::api.class_from_type ||
        !il2::api.method_get_param_count || !il2::api.method_get_param || !il2::api.class_from_name) {
        skyStatus = fogStatus = "Scene effects unavailable";
        return;
    }
    auto assembly = GetModuleHandleW(L"GameAssembly.dll");
    invoke = reinterpret_cast<Invoke>(GetProcAddress(assembly, "il2cpp_runtime_invoke"));
    unbox = reinterpret_cast<decltype(unbox)>(GetProcAddress(assembly, "il2cpp_object_unbox"));
    arrayNew = reinterpret_cast<decltype(arrayNew)>(GetProcAddress(assembly, "il2cpp_array_new"));
    retain = reinterpret_cast<decltype(retain)>(GetProcAddress(assembly, "il2cpp_gchandle_new"));
    release = reinterpret_cast<decltype(release)>(GetProcAddress(assembly, "il2cpp_gchandle_free"));
    auto* settings = Class("UnityEngine", "RenderSettings");
    activeScene = Method(Class("UnityEngine.SceneManagement", "SceneManager"), "GetActiveScene");
    getFog = Method(settings,"get_fog"); setFog = Method(settings,"set_fog",{"Boolean"});
    getMode = Method(settings,"get_fogMode"); setMode = Method(settings,"set_fogMode",{"FogMode"});
    getDensity = Method(settings,"get_fogDensity"); setDensity = Method(settings,"set_fogDensity",{"Single"});
    getColor = Method(settings,"get_fogColor"); setColor = Method(settings,"set_fogColor",{"Color"});
    nativeFog = invoke && unbox && activeScene && getFog && setFog && getMode && setMode &&
        getDensity && setDensity && getColor && setColor;
    getSky = Method(settings,"get_skybox"); setSky = Method(settings,"set_skybox",{"Material"});
    matType = Class("UnityEngine","Material"); texType = Class("UnityEngine","Texture2D");
    matQueue = Method(matType,"set_renderQueue",{"Int32"});
    byteType = Class("System","Byte");
    findShader = Method(Class("UnityEngine","Shader"), "Find", {"String"});
    destroy = Method(Class("UnityEngine","Object"), "Destroy", {"Object"});
    matCtor = Method(matType,".ctor",{"Shader"});
    matSetTexture = Method(matType,"SetTexture",{"String","Texture"});
    matSetColor = Method(matType,"SetColor",{"String","Color"});
    texCtor = Method(texType,".ctor",{"Int32","Int32","TextureFormat","Boolean"});
    texLoad = Method(texType,"LoadRawTextureData",{"Byte[]"});
    texApply = Method(texType,"Apply",{"Boolean","Boolean"});
    const bool materials = invoke && unbox && activeScene && retain && release && arrayNew && byteType &&
        findShader && destroy && matCtor && matSetTexture && texCtor && texLoad && texApply;
    nativeSky = materials && getSky && setSky && matSetColor;
    objectAlive = Method(Class("UnityEngine","Object"),"op_Implicit",{"Object"});
    auto* cameraType = Class("UnityEngine","Camera");
    cameraGetClear = Method(cameraType,"get_clearFlags");
    cameraSetClear = Method(cameraType,"set_clearFlags",{"CameraClearFlags"});
    allCameras = Method(cameraType,"get_allCameras");
    cameraDepth = Method(cameraType,"get_depth"); cameraDisplay = Method(cameraType,"get_targetDisplay");
    cameraTarget = Method(cameraType,"get_targetTexture"); cameraRect = Method(cameraType,"get_rect");
    cameraMask = Method(cameraType,"get_cullingMask");
    cameraFar = Method(cameraType,"get_farClipPlane");
    auto* skyboxClass = Class("UnityEngine","Skybox");
    skyboxType = skyboxClass ? il2::TypeObject(skyboxClass) : nullptr;
    getLocalSky = Method(skyboxClass,"get_material");
    setLocalSky = Method(skyboxClass,"set_material",{"Material"});
    getComponent = Method(Class("UnityEngine","Component"),"GetComponent",{"Type"});

    getAmbientMode = Method(settings,"get_ambientMode");
    setAmbientMode = Method(settings,"set_ambientMode",{"AmbientMode"});
    getAmbient = {Method(settings,"get_ambientSkyColor"),Method(settings,"get_ambientEquatorColor"),
                  Method(settings,"get_ambientGroundColor")};
    setAmbient = {Method(settings,"set_ambientSkyColor",{"Color"}),Method(settings,"set_ambientEquatorColor",{"Color"}),
                  Method(settings,"set_ambientGroundColor",{"Color"})};
    getSun = Method(settings,"get_sun");
    auto* light = Class("UnityEngine","Light");
    getSunColor = Method(light,"get_color");
    setSunColor = Method(light,"set_color",{"Color"});
    nativeLighting = invoke && unbox && activeScene && objectAlive && retain && release &&
                     getAmbientMode && setAmbientMode &&
                     std::all_of(getAmbient.begin(),getAmbient.end(),[](void* p){return p != nullptr;}) &&
                     std::all_of(setAmbient.begin(),setAmbient.end(),[](void* p){return p != nullptr;});

    goType=Class("UnityEngine","GameObject"); meshType=Class("UnityEngine","Mesh");
    vecType=Class("UnityEngine","Vector3"); uvType=Class("UnityEngine","Vector2");
    colorType=Class("UnityEngine","Color"); intType=Class("System","Int32");
    auto* filter=Class("UnityEngine","MeshFilter");
    auto* renderer=Class("UnityEngine","MeshRenderer");
    auto* baseRenderer=Class("UnityEngine","Renderer");
    filterType=filter ? il2::TypeObject(filter) : nullptr;
    rendererType=renderer ? il2::TypeObject(renderer) : nullptr;
    goCtor=Method(goType,".ctor",{"String"}); goAdd=Method(goType,"AddComponent",{"Type"});
    goLayer=Method(goType,"set_layer",{"Int32"});
    goTransform=Method(goType,"get_transform");
    transformPosition=Method(Class("UnityEngine","Transform"),"set_position",{"Vector3"});
    meshCtor=Method(meshType,".ctor"); meshDynamic=Method(meshType,"MarkDynamic");
    meshVertices=Method(meshType,"set_vertices",{"Vector3[]"}); meshUv=Method(meshType,"set_uv",{"Vector2[]"});
    meshIndices=Method(meshType,"set_triangles",{"Int32[]"}); meshColors=Method(meshType,"set_colors",{"Color[]"});
    meshBounds=Method(meshType,"RecalculateBounds");
    filterMesh=Method(filter,"set_sharedMesh",{"Mesh"});
    rendererMaterial=Method(baseRenderer,"set_sharedMaterial",{"Material"});
    rendererEnabled=Method(baseRenderer,"set_enabled",{"Boolean"});
    rendererShadows=Method(baseRenderer,"set_shadowCastingMode",{"ShadowCastingMode"});
    rendererReceive=Method(baseRenderer,"set_receiveShadows",{"Boolean"});
    nativeParticles=materials && objectAlive && vecType && uvType && colorType && intType &&
        filterType && rendererType && goCtor && goAdd && meshCtor && meshVertices && meshUv && meshIndices &&
        meshColors && meshBounds && filterMesh && rendererMaterial && rendererEnabled;
    if (!nativeSky) skyStatus = "Sky API unavailable";
    if (!nativeFog) fogStatus = "Fog API unavailable";
    if (!nativeParticles) particleStatus = "Mesh or shader API unavailable";
    if (!nativeLighting) lightStatus = "Lighting API unavailable";
    Log("[world] sky=%d fog=%d world particles=%d lighting=%d", nativeSky,nativeFog,nativeParticles,nativeLighting);
}

void Publish(const WorldCfg& settings, bool hide) {
    std::lock_guard lock(optionsMutex);
    published = settings;
    paused = hide;
}

void Tick(void* mainCamera, bool inMatch) {
    if (!nativeFog && !nativeSky && !nativeParticles && !nativeLighting) return;
    const bool shutdown = stopping.load();
    if (shutdown && stopped.load()) return;
    if (!shutdown && !mainCamera && inMatch) return;
    const auto now = GetTickCount64();
    if (!shutdown && now < nextTick) return;
    nextTick = now + 32;
    WorldCfg options;
    {
        std::lock_guard lock(optionsMutex);
        options = published;
        options.enabled = options.enabled && !paused && inMatch && !shutdown;
    }
    if (!options.enabled && !assignedSky && !appliedFog && !appliedLight && !particlesVisible && !shutdown) return;
    stopped = false;
    try {
        int currentScene = Get<int>(activeScene);
        if (scene && *scene != currentScene) {
            // RenderSettings are per scene. The game's new scene owns its settings.
            RestoreCameraSky();
            backgroundCamera.Clear();
            lastMainCamera = nullptr;
            assignedSky = false;
            previousSky.Clear();
            previousFog.reset();
            appliedFog.reset();
            previousLight.reset();
            appliedLight.reset();
            sceneSun.Clear();
            if (nativeParticles) ClearParticles();
            ClearSkyDome();
        }
        scene = currentScene;
        if (nativeSky) {
            try {
                if (options.enabled && options.sky && !skyFailed) ApplySky(options,mainCamera,now);
                else {
                    RestoreSky();
                    backgroundCamera.Clear();
                    lastMainCamera = nullptr;
                    if (!skyFailed) skyStatus = "Map sky";
                }
                if (shutdown) { backgroundCamera.Clear(); ClearSkyDome(); Destroy(material); Destroy(texture); }
            } catch (const std::exception& e) {
                if (!skyFailed) Log("[world] sky: %s", e.what());
                skyFailed = true;
                skyStatus = skyShadersResolved ? "Sky effect unavailable on this build" : "Could not apply sky effect";
            }
        }
        if (nativeFog) {
            try {
                if (options.enabled && options.fog && !fogFailed) ApplyFog(options);
                else {
                    RestoreFog();
                    if (!fogFailed) fogStatus = "Map fog";
                }
            } catch (const std::exception& e) {
                if (!fogFailed) Log("[world] fog: %s", e.what());
                fogFailed = true;
                fogStatus = "Could not apply fog effect";
            }
        }
        if (nativeLighting) {
            try {
                if (options.enabled && options.lighting && options.intensity > 0 && !lightFailed) ApplyLight(options);
                else {
                    RestoreLight();
                    if (!lightFailed) lightStatus = "Map lighting";
                }
            } catch (const std::exception& e) {
                if (!lightFailed) Log("[world] lighting: %s", e.what());
                lightFailed = true;
                lightStatus = "Could not apply scene lighting";
            }
        }
        if (nativeParticles) {
            try {
                if (options.enabled && options.particles && options.intensity > 0 && !particlesFailed)
                    ApplyParticles(options,now,mainCamera);
                else {
                    ParticleVisibility(false);
                    if (!particlesFailed) particleStatus = "Off";
                }
                if (shutdown) ClearParticles();
            } catch (const std::exception& e) {
                if (!particlesFailed) Log("[world] particles: %s", e.what());
                particlesFailed = true;
                particleStatus = "Could not draw world particles";
                try { ClearParticles(); } catch (...) {}
            }
        }
    } catch (const std::exception& e) {
        if (!skyFailed || !fogFailed) Log("[world] could not read active scene: %s", e.what());
        skyFailed = fogFailed = lightFailed = particlesFailed = true;
        skyStatus = fogStatus = lightStatus = particleStatus = "Scene API unavailable";
    }
    if (shutdown) stopped = true;
}

void RequestShutdown() { stopping = true; }
bool ShutdownDone() { return stopped.load(); }
const char* SkyStatus() { return skyStatus.load(); }
const char* FogStatus() { return fogStatus.load(); }
const char* ParticleStatus() { return particleStatus.load(); }
const char* LightingStatus() { return lightStatus.load(); }
}
