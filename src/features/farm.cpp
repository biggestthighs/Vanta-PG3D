#include "farm.h"
#include "farm_state.h"
#include "features.h"
#include "../core/config.h"
#include "../core/render.h"
#include "../sdk/il2cpp.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace farm {
namespace {
    using Invoke = void*(*)(void*, void*, void**, void**);
    using HandleNew = uintptr_t(*)(void*, bool);
    using HandleFree = void(*)(uintptr_t);
    Invoke invoke = nullptr;
    HandleNew handleNew = nullptr;
    HandleFree handleFree = nullptr;
    std::atomic<int> command{0};
    std::atomic<bool> running{false}, combat{false}, inspect{false};
    std::mutex statusMutex, profileMutex;
    Status status;
    Cfg profile;
    logic::Controller controller;
    logic::ActionGate actionGate;
    bool ready = false, profileCombat = false;
    DWORD startedAt = 0, tickAt = 0, scanAt = 0;
    logic::Watchdog watchdog;
    std::atomic<bool> watchFrames{false};
    std::atomic<uint64_t> frameHash{0};
    std::atomic<DWORD> frameChangedAt{0};
    DWORD lastFrameRecovery = 0;
    int reportedKills = -1;
    rewards::Tracker rewardTracker;
    unsigned rewardMatch = 0, rewardClaim = 0;
    std::vector<rewards::Entry> matchRewards;
    using FieldType = void*(*)(void*);
    using FieldFlags = int(*)(void*);
    using FieldRead = void(*)(void*, void*, void*);
    using StaticRead = void(*)(void*, void*);
    using ReturnType = void*(*)(void*);
    FieldType fieldType = nullptr;
    FieldFlags fieldFlags = nullptr;
    FieldRead fieldRead = nullptr;
    StaticRead staticRead = nullptr;
    ReturnType returnType = nullptr;
    void *statsInstanceField, *killRateField, *killsGetter, *decimalGetter, *decimalToInt;
    bool statsReady = false;

    struct Root {
        void* pointer;
        uintptr_t handle;
        explicit Root(void* object) : pointer(object), handle(object ? handleNew(object, true) : 0) {
            if (object && !handle) throw std::runtime_error("could not retain UI object");
        }
        ~Root() { if (handle) handleFree(handle); }
        Root(const Root&) = delete;
    };

    struct PendingReward {
        std::string event, path;
        bool progress = false;
        std::vector<rewards::Entry> entries;
        std::unique_ptr<Root> owner;
        logic::Confirmation confirmation;
    };
    struct PendingClick {
        logic::Action action;
        std::string path;
        std::unique_ptr<Root> owner;
        logic::Confirmation confirmation;
    };
    std::optional<PendingClick> pendingClick;
    unsigned confirmedTransitions = 0;

    std::optional<PendingReward> pendingReward;
    struct Backend {
        void* type = nullptr;
        void* click = nullptr;
        void* enabled = nullptr;
        bool unity = false;
        bool dismissOnly = false;
        bool direct = false;
    };
    struct TextType { void* type; void* getter; };
    struct Control {
        void* object;
        void* gameObject;
        Backend backend;
        std::string path, label;
        logic::Action action = logic::Action::None;
        bool modal = false;
        int depth = 0;
        // Snapshots can outlive the discovery array. Keep both managed wrappers
        // alive until the snapshot is discarded, including during scene changes.
        std::unique_ptr<Root> objectRoot, gameObjectRoot;
    };
    std::vector<Backend> backends;
    std::vector<TextType> textTypes;
    std::vector<Control> controls;
    void *findObjects, *componentGo, *goTransform, *goActive, *objectName, *transformParent, *goChildren;
    void *hudType, *unityEventGetter, *unityEventInvoke, *goSendMessage;
    void *goComponents, *behaviourType, *behaviourEnabled;
    void *widgetType, *widgetAlpha, *executeDelegates;
    void *rectType, *rectAlpha, *rectCalculateAlpha, *frameCount, *panelType, *panelDepth;
    struct UiVisibility { bool visible = true; int depth = 0; };
    UiVisibility Visibility(void* gameObject);
    bool VisibleControl(void* gameObject);
    void SetStatus(const std::string& message);
    void *transformPosition, *goLayer, *uiFindCamera, *uiCachedCamera, *worldToScreen, *screenWidth, *screenHeight;
    int buttonEventsOffset = 0;
    bool hudPresent = false, modalPresent = false;
    std::unique_ptr<Root> prompt; // visible "Press anywhere to close" label from the last scan
    logic::PromptTapper tapper;
    POINT tapPoint{};

    void* Klass(const char* name, const char* space = "UnityEngine") {
        size_t count = 0;
        auto assemblies = static_cast<void**>(il2::api.domain_get_assemblies(il2::api.domain_get(), &count));
        for (size_t index = 0; assemblies && index < count; ++index)
            if (void* klass = il2::api.class_from_name(il2::api.assembly_get_image(assemblies[index]), space, name)) return klass;
        return nullptr;
    }
    void* Method(void* klass, const char* name, std::initializer_list<const char*> parameters) {
        void* iterator = nullptr;
        while (klass) {
            while (void* method = il2::api.class_get_methods(klass, &iterator)) {
                if (strcmp(il2::api.method_get_name(method), name) || il2::api.method_get_param_count(method) != parameters.size()) continue;
                bool matches = true;
                unsigned index = 0;
                for (const char* parameter : parameters) {
                    void* parameterClass = il2::api.class_from_type(il2::api.method_get_param(method, index++));
                    if (!parameterClass || strcmp(il2::api.class_get_name(parameterClass), parameter)) { matches = false; break; }
                }
                if (matches) return method;
            }
            using Parent = void*(*)(void*);
            static auto parent = reinterpret_cast<Parent>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_class_get_parent"));
            klass = parent ? parent(klass) : nullptr;
            iterator = nullptr;
        }
        return nullptr;
    }
    void* Call(void* method, void* self, std::initializer_list<void*> arguments = {}) {
        if (!method) throw std::runtime_error("required UI method unavailable");
        void* exception = nullptr;
        void* result = invoke(method, self, const_cast<void**>(arguments.begin()), &exception);
        if (exception) throw std::runtime_error(std::string("UI exception in ") + il2::api.method_get_name(method));
        return result;
    }
    bool Bool(void* method, void* object) {
        void* boxed = Call(method, object);
        return boxed && *(static_cast<uint8_t*>(boxed) + 16) != 0;
    }
    void InitStats() {
        HMODULE assembly = GetModuleHandleW(L"GameAssembly.dll");
        fieldType = reinterpret_cast<FieldType>(GetProcAddress(assembly, "il2cpp_field_get_type"));
        fieldFlags = reinterpret_cast<FieldFlags>(GetProcAddress(assembly, "il2cpp_field_get_flags"));
        fieldRead = reinterpret_cast<FieldRead>(GetProcAddress(assembly, "il2cpp_field_get_value"));
        staticRead = reinterpret_cast<StaticRead>(GetProcAddress(assembly, "il2cpp_field_static_get_value"));
        returnType = reinterpret_cast<ReturnType>(GetProcAddress(assembly, "il2cpp_method_get_return_type"));
        auto parent = reinterpret_cast<void*(*)(void*)>(GetProcAddress(assembly, "il2cpp_class_get_parent"));
        if (!fieldType || !fieldFlags || !fieldRead || !staticRead || !returnType || !parent)
            throw std::runtime_error("match-stat reflection exports unavailable");
        void* statsClass = Klass("PlayerStatsController", "");
        void* rateClass = Klass("KillRateObject", "");
        if (!statsClass || !rateClass) throw std::runtime_error("match-stat classes unavailable");
        void* singleton = parent(statsClass);
        void* iterator = nullptr;
        while (void* field = il2::api.class_get_fields(singleton, &iterator))
            if ((fieldFlags(field) & 16) && il2::api.class_from_type(fieldType(field)) == statsClass) statsInstanceField = field;
        iterator = nullptr;
        while (void* field = il2::api.class_get_fields(statsClass, &iterator))
            if (!(fieldFlags(field) & 16) && il2::api.class_from_type(fieldType(field)) == rateClass) killRateField = field;
        killsGetter = Method(rateClass, "get_kills", {});
        if (!statsInstanceField || !killRateField || !killsGetter) throw std::runtime_error("match kill counter unavailable");
        void* wrapperClass = il2::api.class_from_type(returnType(killsGetter));
        void* decimalClass = Klass("Decimal", "System");
        if (!wrapperClass || !decimalClass) throw std::runtime_error("match kill value type unavailable");
        iterator = nullptr;
        while (void* method = il2::api.class_get_methods(wrapperClass, &iterator)) {
            if (il2::api.method_get_param_count(method) || il2::api.class_from_type(returnType(method)) != decimalClass) continue;
            if (decimalGetter) throw std::runtime_error("ambiguous match kill value getter");
            decimalGetter = method;
        }
        decimalToInt = Method(decimalClass, "ToInt32", {"Decimal"});
        if (!decimalGetter || !decimalToInt) throw std::runtime_error("match kill conversion unavailable");
        statsReady = true;
        Log("[farm] kill counter: PlayerStatsController -> KillRateObject.kills (game stats)");
    }
    struct MatchStats { int kills = -1; uintptr_t identity = 0; };
    MatchStats ReadStats() {
        if (!statsReady) return {};
        void* instance = nullptr;
        staticRead(statsInstanceField, &instance);
        if (!instance) return {};
        Root owner(instance);
        void* rate = nullptr;
        fieldRead(owner.pointer, killRateField, &rate);
        if (!rate) return {};
        Root match(rate);
        Root boxed(Call(killsGetter, match.pointer));
        if (!boxed.pointer) return {};
        Root decimal(Call(decimalGetter, static_cast<uint8_t*>(boxed.pointer) + 16));
        if (!decimal.pointer) return {};
        Root integer(Call(decimalToInt, nullptr, {static_cast<uint8_t*>(decimal.pointer) + 16}));
        if (!integer.pointer) return {};
        int count = -1;
        memcpy(&count, static_cast<uint8_t*>(integer.pointer) + 16, sizeof(count));
        if (count < 0 || count > 100000) throw std::runtime_error("invalid game kill counter");
        return {count, reinterpret_cast<uintptr_t>(rate)};
    }
    std::vector<void*> Array(void* array) {
        std::vector<void*> result;
        if (!array) return result;
        uintptr_t count = 0;
        memcpy(&count, static_cast<uint8_t*>(array) + 24, sizeof(count));
        if (count > 8192) throw std::runtime_error("UI object count is invalid");
        auto entries = reinterpret_cast<void**>(static_cast<uint8_t*>(array) + 32);
        result.assign(entries, entries + count);
        return result;
    }
    void* Find(void* type) {
        bool inactive = false;
        return type ? Call(findObjects, nullptr, {type, &inactive}) : nullptr;
    }
    bool ActiveObject(void* gameObject) { return gameObject && Bool(goActive, gameObject); }
    std::string Path(void* gameObject) {
        std::string result;
        void* transform = Call(goTransform, gameObject);
        for (int depth = 0; transform && depth < 20; ++depth) {
            const std::string name = il2::Str(Call(objectName, transform));
            result = name + (result.empty() ? "" : "/" + result);
            transform = Call(transformParent, transform);
        }
        return result;
    }
    std::string Label(void* gameObject) {
        bool inactive = false;
        std::string result;
        std::unordered_set<std::string> seen;
        bool closePrompt = false;
        for (const auto& text : textTypes) {
            Root labels(Call(goChildren, gameObject, {text.type, &inactive}));
            for (void* label : Array(labels.pointer)) {
                const std::string value = il2::Str(Call(text.getter, label));
                if (value.empty()) continue;
                const bool isPrompt = logic::HasClosePrompt(value);
                if (isPrompt && !VisibleControl(Call(componentGo,label))) continue;
                closePrompt |= isPrompt;
                if (result.size() >= 1024 || !seen.insert(logic::Normalize(value)).second) continue;
                if (!result.empty()) result += ' ';
                result += value.substr(0,1024-result.size());
            }
        }
        // Keep the dismissal prompt even when a long reward list precedes it.
        if (closePrompt && !logic::HasClosePrompt(result)) result += " Press anywhere to close";
        return result;
    }
    void ScanNoticeDismissals(bool diagnostic) {
        if (!goComponents || !behaviourType || !behaviourEnabled || (hudPresent && !modalPresent)) return;
        std::unordered_set<void*> inspected;
        for (const auto& text : textTypes) {
            Root labels(Find(text.type));
            for (void* label : Array(labels.pointer)) {
                if (!logic::HasClosePrompt(il2::Str(Call(text.getter,label)))) continue;
                void* promptGo = Call(componentGo,label);
                if (!ActiveObject(promptGo) || !VisibleControl(promptGo)) continue;
                if (!prompt) prompt = std::make_unique<Root>(promptGo);
                Root retainedPrompt(promptGo);
                void* transform = Call(goTransform,promptGo);
                for (int depth=0; transform && depth<10; ++depth) {
                    void* owner = Call(componentGo,transform);
                    const auto path = Path(owner);
                    const auto content = Label(owner);
                    // Inspect only the active notice containing the actual prompt.
                    bool foundReceiver = false;
                    const auto visibility = Visibility(owner);
                    if (logic::IsNoticeDismiss(path,content) && visibility.visible) {
                        if (!inspected.insert(owner).second) break;
                        modalPresent = true;
                        Root scripts(Call(goComponents,owner,{behaviourType}));
                        for (void* script : Array(scripts.pointer)) {
                            if (!Bool(behaviourEnabled,script)) continue;
                            auto* click = Method(il2::api.object_get_class(script),"OnClick",{});
                            if (!click) continue;
                            const bool duplicate = std::any_of(controls.begin(),controls.end(),[&](const Control& c) {
                                return c.backend.direct && c.object == script;
                            });
                            if (duplicate) { foundReceiver = true; break; }
                            Backend backend{behaviourType,click,behaviourEnabled,false,true,true};
                            Control control{script,owner,backend,path,content};
                            control.objectRoot = std::make_unique<Root>(script);
                            control.gameObjectRoot = std::make_unique<Root>(owner);
                            control.action = logic::Action::Reward;
                            control.modal = true;
                            control.depth = visibility.depth;
                            if (diagnostic) Log("[farm] notice handler '%s' text='%s'",path.c_str(),content.c_str());
                            // A verified receiver takes priority over a collider that may have no OnClick.
                            controls.insert(controls.begin(),std::move(control));
                            foundReceiver = true;
                            break;
                        }
                    }
                    if (foundReceiver) break;
                    transform = Call(transformParent,transform);
                }
            }
        }
    }
    std::vector<rewards::Entry> ReadRewards(bool progress) {
        std::vector<rewards::Entry> result;
        std::unordered_set<void*> cells;
        for (const auto& type : textTypes) {
            Root labels(Find(type.type));
            for (void* label : Array(labels.pointer)) {
                void* go = Call(componentGo,label);
                if (!ActiveObject(go)) continue;
                const auto path = logic::Normalize(Path(go));
                const bool inScope = progress ? path.find("universalrewardview") != std::string::npos :
                    path.find("networkstarttablengui") != std::string::npos &&
                    (path.find("endinterface") != std::string::npos || path.find("expprogressview") != std::string::npos);
                if (!inScope || !VisibleControl(go)) continue;
                const auto value = il2::Str(Call(type.getter,label));
                if (value.find_first_of("0123456789") == std::string::npos) continue;
                void* transform = Call(goTransform,go);
                for (int depth=0; transform && depth<3; ++depth) {
                    void* cell=Call(componentGo,transform);
                    auto entry=rewards::Parse(Path(cell),Label(cell));
                    if (entry) {
                        if (cells.insert(cell).second) result.push_back(*entry);
                        break;
                    }
                    transform=Call(transformParent,transform);
                }
            }
        }
        return result;
    }
    void ObserveRewards() {
        if (!running) return;
        if (pendingReward) {
            const bool stillVisible = std::any_of(controls.begin(),controls.end(),[](const Control& c) {
                return c.gameObject == pendingReward->owner->pointer && c.path == pendingReward->path;
            });
            if (pendingReward->confirmation.Observe(stillVisible,GetTickCount())) {
                rewardTracker.Commit(pendingReward->event,pendingReward->progress,pendingReward->entries);
                for (const auto& entry : pendingReward->entries)
                    Log("[farm-rewards] %s %lld %s %s",pendingReward->event.c_str(),
                        static_cast<long long>(entry.amount),entry.kind.c_str(),entry.item.c_str());
                pendingReward.reset();
            }
        }
        const bool results=std::any_of(controls.begin(),controls.end(),[](const Control& c) {
            return c.path.find("EndInterface/") != std::string::npos;
        });
        if (results) {
            auto entries=ReadRewards(false);
            if (!entries.empty()) matchRewards=std::move(entries);
        }
    }
    float Alpha(void* boxed) {
        float alpha = 0;
        if (boxed) memcpy(&alpha,static_cast<uint8_t*>(boxed)+16,sizeof(alpha));
        return alpha;
    }
    UiVisibility Visibility(void* gameObject) {
        UiVisibility result;
        if (!ActiveObject(gameObject)) return {false,0};
        if (goComponents && rectType && (rectAlpha || (rectCalculateAlpha && frameCount))) {
            int frame = 0;
            if (frameCount) {
                auto* boxed = Call(frameCount,nullptr);
                if (boxed) memcpy(&frame,static_cast<uint8_t*>(boxed)+16,sizeof(frame));
            }
            bool foundPanel = false;
            void* transform = Call(goTransform,gameObject);
            for (int depth=0; transform && depth<20; ++depth) {
                void* owner = Call(componentGo,transform);
                Root rectangles(Call(goComponents,owner,{rectType}));
                for (void* rect : Array(rectangles.pointer)) {
                    auto* calculate = rectCalculateAlpha && frameCount ? Method(il2::api.object_get_class(rect),"CalculateFinalAlpha",{"Int32"}) : nullptr;
                    if (!calculate && !rectAlpha) continue;
                    const auto alpha = calculate ? Alpha(Call(calculate,rect,{&frame})) : Alpha(Call(rectAlpha,rect));
                    if (!logic::VisibleAlpha(alpha)) return {false,result.depth};
                }
                if (!foundPanel && panelType && panelDepth) {
                    Root panels(Call(goComponents,owner,{panelType}));
                    for (void* panel : Array(panels.pointer)) {
                        auto* boxed = Call(panelDepth,panel);
                        int value = 0;
                        if (boxed) memcpy(&value,static_cast<uint8_t*>(boxed)+16,sizeof(value));
                        result.depth = foundPanel ? std::max(result.depth,value) : value;
                        foundPanel = true;
                    }
                }
                transform = Call(transformParent,transform);
            }
            return result;
        }
        if (!widgetType || !widgetAlpha) return result;
        bool inactive = false;
        Root widgets(Call(goChildren,gameObject,{widgetType,&inactive}));
        const auto objects = Array(widgets.pointer);
        if (objects.empty()) return result;
        result.visible = std::any_of(objects.begin(),objects.end(),[](void* widget) {
            return logic::VisibleAlpha(Alpha(Call(widgetAlpha,widget)));
        });
        return result;
    }
    bool VisibleControl(void* gameObject) { return Visibility(gameObject).visible; }
    void ObserveClick() {
        if (!pendingClick) return;
        const bool present = std::any_of(controls.begin(),controls.end(),[](const Control& control) {
            return control.gameObject == pendingClick->owner->pointer && control.action == pendingClick->action;
        });
        if (pendingClick->confirmation.Observe(present,GetTickCount())) {
            ++confirmedTransitions;
            Log("[farm-confirm] screen changed after '%s'",pendingClick->path.c_str());
            pendingClick.reset();
            watchdog.screen.clear();
        }
    }
    template <class T> T Unbox(void* boxed) {
        T value{};
        if (boxed) memcpy(&value,static_cast<uint8_t*>(boxed)+16,sizeof(value));
        return value;
    }
    // Screen position of the prompt label, via the NGUI camera that renders it.
    // Falls back to bottom centre, where the prompt sits on every notice seen so far.
    bool TapPoint(POINT& point, bool& exact) {
        RECT client{};
        if (!prompt || !render::Hwnd || !GetClientRect(render::Hwnd,&client) || client.right <= 0 || client.bottom <= 0) return false;
        logic::ClientPoint result{client.right/2, client.bottom*9/10};
        exact = false;
        try {
            if (transformPosition && goLayer && uiFindCamera && uiCachedCamera && worldToScreen && screenWidth && screenHeight) {
                struct Vector3 { float x, y, z; };
                auto position = Unbox<Vector3>(Call(transformPosition,Call(goTransform,prompt->pointer)));
                int layer = Unbox<int>(Call(goLayer,prompt->pointer));
                if (void* ui = Call(uiFindCamera,nullptr,{&layer})) {
                    if (void* camera = Call(uiCachedCamera,ui)) {
                        const auto screen = Unbox<Vector3>(Call(worldToScreen,camera,{&position}));
                        exact = logic::UnityToClient(screen.x,screen.y,Unbox<int>(Call(screenWidth,nullptr)),
                                                     Unbox<int>(Call(screenHeight,nullptr)),client.right,client.bottom,result);
                    }
                }
            }
        } catch (const std::exception& error) {
            Log("[farm-tap] prompt position unavailable: %s",error.what());
        }
        point = {result.x,result.y};
        return ClientToScreen(render::Hwnd,&point) != FALSE;
    }
    void SendButton(DWORD flags) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = flags;
        SendInput(1,&input,sizeof(input));
    }
    void TapPrompt(bool paused, DWORD now) {
        switch (tapper.Update(prompt != nullptr,paused,now)) {
        case logic::PromptTapper::Step::Move: {
            bool exact = false;
            if (!TapPoint(tapPoint,exact) || !SetCursorPos(tapPoint.x,tapPoint.y)) { tapper.Cancel(now); return; }
            Log("[farm-tap] tap %u on 'Press anywhere to close' at %ld,%ld (%s)",tapper.taps+1,tapPoint.x,tapPoint.y,
                exact ? "prompt label" : "bottom centre");
            SetStatus("Tapping 'Press anywhere to close' until the notice closes");
            return;
        }
        case logic::PromptTapper::Step::Down: {
            POINT cursor{};
            // A moved cursor means the user is back: never click where they are pointing.
            if (!GetCursorPos(&cursor) || cursor.x != tapPoint.x || cursor.y != tapPoint.y || !input::CanAutoClick()) {
                Log("[farm-tap] tap skipped: cursor moved or game not focused");
                tapper.Cancel(now);
                return;
            }
            SendButton(MOUSEEVENTF_LEFTDOWN);
            return;
        }
        case logic::PromptTapper::Step::Up:
            SendButton(MOUSEEVENTF_LEFTUP);
            scanAt = 0;
            return;
        default:
            return;
        }
    }
    void ReleaseTap() {
        if (tapper.phase == 2) SendButton(MOUSEEVENTF_LEFTUP);
        tapper = {};
        prompt.reset();
    }
    void Scan(bool diagnostic) {
        controls.clear();
        prompt.reset();
        hudPresent = modalPresent = false;
        Root hud(Find(hudType));
        for (void* object : Array(hud.pointer))
            if (ActiveObject(Call(componentGo, object))) { hudPresent = true; break; }
        std::unordered_set<void*> seen;
        for (const auto& backend : backends) {
            if (backend.dismissOnly && hudPresent && !modalPresent) continue;
            Root objects(Find(backend.type));
            for (void* object : Array(objects.pointer)) {
                void* gameObject = Call(componentGo, object);
                Root retainedGo(gameObject);
                if (!ActiveObject(gameObject) || !Bool(backend.enabled, object)) continue;
                if (seen.count(gameObject)) continue;
                const auto visibility = Visibility(gameObject);
                if (!visibility.visible) continue;
                const std::string path = Path(gameObject);
                // NGUI also dispatches OnClick to collider-only backgrounds.
                // Only admit a modal with an explicit anywhere-to-close prompt.
                if (backend.dismissOnly && !logic::IsModal(path) &&
                    !logic::IsNoticeDismiss(path, "Press anywhere to close")) continue;
                std::string label = Label(gameObject);
                if (backend.dismissOnly && !logic::IsNoticeDismiss(path, label) && logic::IsNoticeBackdrop(path)) {
                    // The prompt can be a sibling of the hit-test background.
                    // Look only within its modal, retaining the actual collider
                    // as the OnClick receiver, rather than a weapon tile.
                    void* transform = Call(goTransform, gameObject);
                    for (int level = 0; transform && level < 3; ++level) {
                        transform = Call(transformParent, transform);
                        if (!transform) break;
                        void* parent = Call(componentGo, transform);
                        if (!parent || !logic::IsModal(Path(parent))) break;
                        label = Label(parent);
                        if (logic::IsNoticeDismiss(path, label)) break;
                    }
                }
                if (backend.dismissOnly && !logic::IsNoticeDismiss(path, label)) continue;
                seen.insert(gameObject);
                Control control{object, gameObject, backend, path, label};
                control.objectRoot = std::make_unique<Root>(object);
                control.gameObjectRoot = std::make_unique<Root>(gameObject);
                control.depth = visibility.depth;
                control.action = logic::Classify(control.path, control.label);
                control.modal = logic::IsModal(control.path) || control.action == logic::Action::Reward;
                modalPresent |= control.modal;
                if (diagnostic) Log("[farm] button '%s' text='%s' action=%d", control.path.c_str(), control.label.c_str(), int(control.action));
                controls.push_back(std::move(control));
            }
        }
        ScanNoticeDismissals(diagnostic);
        ObserveClick();
        try { ObserveRewards(); }
        catch (const std::exception& error) { Log("[farm-rewards] observation unavailable: %s",error.what()); }
        // A foreground notice must close before a Continue button underneath.
        std::stable_sort(controls.begin(), controls.end(), [](const Control& left, const Control& right) {
            return logic::BeforeControl(left.depth,logic::IsNoticeDismiss(left.path,left.label),
                                        right.depth,logic::IsNoticeDismiss(right.path,right.label));
        });
        if (diagnostic) Log("[farm] scan: %zu enabled buttons; match HUD=%d modal=%d", controls.size(), hudPresent, modalPresent);
    }
    bool Eligible(const Control& control) {
        return logic::EligibleAtDepth(modalPresent,control.modal,control.depth,logic::ForegroundDepth(controls));
    }
    const Control* Target(logic::Action action) {
        if (action == logic::Action::None) return nullptr;
        const auto found = std::find_if(controls.begin(), controls.end(), [&](const auto& control) { return Eligible(control) && control.action == action; });
        if (found == controls.end()) return nullptr;
        std::vector<const Control*> candidates;
        // After repeated unconfirmed clicks, cycle every button with this action at any depth.
        const bool escalate = watchdog.attempts >= 4;
        for (const auto& control : controls)
            if (control.action == action && (escalate || (Eligible(control) && control.depth == found->depth &&
                logic::Normalize(control.label) == logic::Normalize(found->label) &&
                logic::IsNoticeDismiss(control.path,control.label) == logic::IsNoticeDismiss(found->path,found->label))))
                candidates.push_back(&control);
        return candidates[watchdog.attempts % candidates.size()];
    }
    // Controls are sorted foreground first, so retries walk from the top layer down.
    const Control* FallbackTarget() {
        std::vector<const Control*> candidates;
        for (const auto& control : controls)
            if (logic::SafeDismiss(control.path,control.label)) candidates.push_back(&control);
        return candidates.empty() ? nullptr : candidates[watchdog.attempts % candidates.size()];
    }
    bool Has(logic::Action action) {
        return std::any_of(controls.begin(), controls.end(), [&](const auto& control) { return Eligible(control) && control.action == action; });
    }
    // Read-only cadence telemetry in normal gameplay. Getter queries are not
    // shots: compare the player's visible ammunition counters instead.
    void MeasureRapidAmmo(DWORD now) {
        struct AmmoSample { int clip; int reserve; };
        static std::unordered_map<std::string, AmmoSample> previous;
        static DWORD sampledAt = 0, windowAt = 0;
        static unsigned ammoUsed = 0, samples = 0;
        static int multiplier = 0;
        const auto* local = game::Local();
        const bool eligible = cfg.misc.rapid_fire && !cfg.panic && !render::MenuOpen && local && local->alive &&
            GetForegroundWindow() == render::Hwnd && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
        if (!eligible || multiplier != cfg.misc.rapid_fire_multiplier) {
            previous.clear(); windowAt = now; ammoUsed = samples = 0;
            multiplier = cfg.misc.rapid_fire_multiplier;
            if (!eligible) return;
        }
        if (now - sampledAt < 200) return;
        sampledAt = now;
        Scan(false);
        std::unordered_map<std::string, AmmoSample> current;
        for (const auto& control : controls) {
            if (control.path.find("WeaponToggles/") == std::string::npos) continue;
            int clip = -1, reserve = -1; char extra = 0;
            if (sscanf(control.label.c_str(), "%d/%d %c", &clip, &reserve, &extra) != 2 || clip < 0 || reserve < 0) continue;
            current[control.path] = {clip, reserve};
            const auto found = previous.find(control.path);
            if (found == previous.end() || found->second.reserve != reserve) continue;
            const int consumed = found->second.clip - clip;
            if (consumed > 0) ammoUsed += consumed;
        }
        previous = std::move(current);
        controls.clear();
        ++samples;
        if (now - windowAt >= 3000) {
            const float seconds = float(now - windowAt) / 1000.f;
            Log("[rapid-rate] requested=%dx displayedAmmoUsed=%u heldSeconds=%.2f ammoUnitsPerSecond=%.2f counters=%zu samples=%u (read-only UI sampling; reload/reserve changes excluded; not server damage)",
                multiplier, ammoUsed, seconds, ammoUsed / seconds, previous.size(), samples);
            windowAt = now; ammoUsed = samples = 0;
        }
    }
    void DisableFeatures() {
        misc::ReleaseInput();
        cfg.aim.enabled = cfg.silent.enabled = cfg.trig.enabled = false;
        cfg.silent.auto_fire = 0;
        cfg.silent.wallbang = false;
        cfg.vis.enabled = false;
        cfg.sound.hit = cfg.sound.kill = false;
        cfg.misc = MiscCfg{};
        cfg.misc.crosshair = false;
        rage::Release();
        profileCombat = false;
        game::ApplyFov();
    }
    void EnableCombat() {
        cfg.silent.enabled = true;
        cfg.silent.hold = false;
        cfg.silent.fov = std::clamp(cfg.farm.fov, 1.f, 360.f);
        cfg.silent.method = 0;
        cfg.silent.wallbang = cfg.farm.wallbang;
        cfg.silent.wall_hold = false;
        cfg.silent.auto_fire = std::clamp(cfg.farm.fire_mode, 1, 2);
        cfg.aim.team_check = false;
        profileCombat = true;
    }
    void RestoreProfile(bool applyFov = true) {
        if (!running) return;
        combat = false;
        rage::Release();
        const auto farmSettings = cfg.farm;
        const auto worldSettings = cfg.world;
        const bool panic = cfg.panic;
        {
            std::lock_guard<std::mutex> lock(profileMutex);
            cfg = profile;
        }
        cfg.farm = farmSettings;
        cfg.world = worldSettings;
        cfg.panic = panic;
        running = false;
        pendingClick.reset();
        ReleaseTap();
        watchFrames = false;
        profileCombat = false;
        if (applyFov) game::ApplyFov();
    }
    void SetStatus(const std::string& message) {
        std::lock_guard<std::mutex> lock(statusMutex);
        status.active = running;
        status.combat = combat;
        status.kills = controller.kills;
        status.completed = controller.completed;
        status.totalKills = controller.totalKills;
        status.seconds = startedAt ? (GetTickCount() - startedAt) / 1000.0 : 0;
        status.message = message;
        status.rewards = rewardTracker.Snapshot();
        status.unreadRewards = static_cast<int>(rewardTracker.unread.size());
        status.recoveries = watchdog.recoveries;
        status.confirmedTransitions = confirmedTransitions;
        status.awaitingTransition = pendingClick.has_value();
        const auto changed = frameChangedAt.load();
        status.frameStillSeconds = watchFrames && frameHash.load() && changed ? (GetTickCount()-changed)/1000 : 0;
    }
    std::string Message(logic::Phase phase) {
        switch (phase) {
        case logic::Phase::Fighting: return "Getting kills";
        case logic::Phase::Idle: return "Kill limit reached; gameplay cheats are off";
        case logic::Phase::Rewards: return "Collecting match rewards";
        case logic::Phase::Queueing: return "Waiting for replay or Deathmatch controls";
        case logic::Phase::Paused: return "Paused while the menu is open, the game is unfocused, or panic is on";
        default: return "Waiting for a match";
        }
    }
    void Click(const Control* found, logic::Action action, bool fallback, DWORD now) {
        if (!found) return;
        if (!watchdog.CanAttempt(now)) return;
        DWORD foreground = 0;
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if (render::MenuOpen || cfg.panic || foreground != GetCurrentProcessId()) return;
        if (!ActiveObject(found->gameObject) || !Bool(found->backend.enabled, found->object)) return;
        const auto visibility = Visibility(found->gameObject);
        if (!visibility.visible || visibility.depth != found->depth) { scanAt = 0; return; }
        // Copy the target before invoking game code. A callback may replace the
        // UI, so never read the discovery vector again after dispatch.
        const std::string path = found->path, label = found->label;
        void* object = found->object;
        void* gameObject = found->gameObject;
        const Backend backend = found->backend;
        const auto current = Label(gameObject);
        if (!backend.dismissOnly && (fallback ? !logic::SafeDismiss(path,current) : logic::Classify(path,current) != action)) {
            scanAt = 0;
            return;
        }
        Root retainedObject(object), retainedGo(gameObject);
        std::optional<PendingReward> collected;
        try {
            if (action == logic::Action::Reward && !pendingReward) {
                const auto normalized = logic::Normalize(path);
                const bool progress = normalized.find("universalrewardview") != std::string::npos;
                const bool match = normalized.find("endinterface") != std::string::npos;
                if (progress || match) {
                    PendingReward pending;
                    pending.event = progress ? "claim/"+std::to_string(++rewardClaim) : "match/"+std::to_string(rewardMatch);
                    pending.path = path;
                    pending.progress = progress;
                    pending.entries = progress ? ReadRewards(true) : matchRewards;
                    pending.owner = std::make_unique<Root>(gameObject);
                    pending.confirmation.Start(now);
                    collected = std::move(pending);
                }
            }
        } catch (const std::exception& error) {
            Log("[farm-rewards] claim snapshot unavailable: %s",error.what());
        }
        std::optional<PendingClick> dispatched;
        // Fallback controls have no recognized action to confirm; the watchdog's screen identity tracks them.
        if (!fallback && !pendingClick && action != logic::Action::Deathmatch && action != logic::Action::Classic && action != logic::Action::NuclearCity) {
            PendingClick click{action,path,std::make_unique<Root>(gameObject),{}};
            click.confirmation.Start(now);
            dispatched = std::move(click);
        }
        GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
        if (render::MenuOpen || cfg.panic || foreground != GetCurrentProcessId()) return;
        const bool useEventFallback = watchdog.attempts >= 2;
        rage::Release();
        watchdog.Attempted(now);
        actionGate.Clicked(now);
        if (backend.direct) {
            Call(backend.click,object);
        } else if (backend.unity) {
            Root event(Call(unityEventGetter, object));
            Call(unityEventInvoke, event.pointer);
        } else if (useEventFallback && !backend.dismissOnly && executeDelegates && buttonEventsOffset > 0) {
            void* events = nullptr;
            memcpy(&events,static_cast<uint8_t*>(object)+buttonEventsOffset,sizeof(events));
            int count = 0;
            if (events) memcpy(&count,static_cast<uint8_t*>(events)+24,sizeof(count));
            if (count > 0 && count < 4096) {
                Root retainedEvents(events);
                Call(executeDelegates,nullptr,{events});
                Log("[farm-watchdog] invoked the recognized button's event list");
            } else {
                Root message(il2::api.string_new("OnClick"));
                int options = 1;
                Call(goSendMessage,gameObject,{message.pointer,&options});
            }
        } else {
            Root message(il2::api.string_new("OnClick"));
            int dontRequireReceiver = 1;
            Call(goSendMessage, gameObject, {message.pointer, &dontRequireReceiver});
        }
        if (dispatched) pendingClick = std::move(dispatched);
        if (collected) pendingReward = std::move(collected);
        controller.Clicked(action);
        scanAt = 0;
        controls.clear();
        Log("[farm] %s '%s' text='%s'", fallback ? "last-resort dispatched" : "dispatched", path.c_str(), label.c_str());
    }
}

void Init() {
    try {
        HMODULE assembly = GetModuleHandleW(L"GameAssembly.dll");
        invoke = reinterpret_cast<Invoke>(GetProcAddress(assembly, "il2cpp_runtime_invoke"));
        handleNew = reinterpret_cast<HandleNew>(GetProcAddress(assembly, "il2cpp_gchandle_new"));
        handleFree = reinterpret_cast<HandleFree>(GetProcAddress(assembly, "il2cpp_gchandle_free"));
        if (!invoke || !handleNew || !handleFree) throw std::runtime_error("IL2CPP UI exports unavailable");
        findObjects = Method(Klass("Object"), "FindObjectsOfType", {"Type", "Boolean"});
        objectName = Method(Klass("Object"), "get_name", {});
        componentGo = Method(Klass("Component"), "get_gameObject", {});
        goTransform = Method(Klass("GameObject"), "get_transform", {});
        goActive = Method(Klass("GameObject"), "get_activeInHierarchy", {});
        goChildren = Method(Klass("GameObject"), "GetComponentsInChildren", {"Type", "Boolean"});
        goSendMessage = Method(Klass("GameObject"), "SendMessage", {"String", "SendMessageOptions"});
        transformParent = Method(Klass("Transform"), "get_parent", {});
        goComponents = Method(Klass("GameObject"), "GetComponents", {"Type"});
        behaviourType = il2::TypeObject(Klass("MonoBehaviour"));
        behaviourEnabled = Method(Klass("Behaviour"), "get_enabled", {});
        if (void* widget = Klass("UIWidget","")) {
            widgetType = il2::TypeObject(widget);
            widgetAlpha = Method(widget,"get_finalAlpha",{});
        }
        if (void* rect = Klass("UIRect","")) {
            rectType = il2::TypeObject(rect);
            rectAlpha = Method(rect,"get_finalAlpha",{});
            rectCalculateAlpha = Method(rect,"CalculateFinalAlpha",{"Int32"});
            frameCount = Method(Klass("Time"),"get_frameCount",{});
        }
        if (void* panel = Klass("UIPanel","")) {
            panelType = il2::TypeObject(panel);
            panelDepth = Method(panel,"get_depth",{});
        }
        transformPosition = Method(Klass("Transform"),"get_position",{});
        goLayer = Method(Klass("GameObject"),"get_layer",{});
        worldToScreen = Method(Klass("Camera"),"WorldToScreenPoint",{"Vector3"});
        screenWidth = Method(Klass("Screen"),"get_width",{});
        screenHeight = Method(Klass("Screen"),"get_height",{});
        if (void* uiCamera = Klass("UICamera","")) {
            uiFindCamera = Method(uiCamera,"FindCameraForLayer",{"Int32"});
            uiCachedCamera = Method(uiCamera,"get_cachedCamera",{});
        }
        hudType = il2::TypeObject(Klass("InGameGUI", ""));
        if (!findObjects || !objectName || !componentGo || !goTransform || !goActive || !goChildren || !transformParent || !hudType)
            throw std::runtime_error("game UI discovery unavailable");
        if (void* button = Klass("UIButton", "")) {
            buttonEventsOffset = il2::FieldOffset(button,"onClick");
            executeDelegates = Method(Klass("EventDelegate",""),"Execute",{"List`1"});
            Backend backend{il2::TypeObject(button), Method(button, "OnClick", {}), Method(button, "get_isEnabled", {}), false};
            if (backend.type && backend.click && backend.enabled && goSendMessage) backends.push_back(backend);
        }
        if (void* button = Klass("Button", "UnityEngine.UI")) {
            Backend backend{il2::TypeObject(button), nullptr, Method(button, "IsInteractable", {}), true};
            unityEventGetter = Method(button, "get_onClick", {});
            unityEventInvoke = Method(Klass("UnityEvent", "UnityEngine.Events"), "Invoke", {});
            if (backend.type && backend.enabled && unityEventGetter && unityEventInvoke) backends.push_back(backend);
        }
        std::stable_sort(backends.begin(), backends.end(), [](const Backend& left, const Backend& right) { return left.unity > right.unity; });
        // A new-weapon notice can have no UIButton at all: NGUI routes the
        // tap through its Collider/Collider2D to OnClick on the same object.
        for (const char* name : {"Collider", "Collider2D"}) {
            if (void* collider = Klass(name)) {
                Backend backend{il2::TypeObject(collider), nullptr,
                                Method(collider, "get_enabled", {}), false, true};
                if (backend.type && backend.enabled && goSendMessage) backends.push_back(backend);
            }
        }
        for (const auto& entry : {std::pair{"UILabel", ""}, std::pair{"Text", "UnityEngine.UI"}, std::pair{"TMP_Text", "TMPro"}}) {
            void* klass = Klass(entry.first, entry.second);
            void* getter = Method(klass, "get_text", {});
            if (klass && getter) textTypes.push_back({il2::TypeObject(klass), getter});
        }
        if (backends.empty() || textTypes.empty()) throw std::runtime_error("no supported game buttons or labels found");
        ready = true;
        Log("[farm] UI ready: %zu button backends, %zu label types", backends.size(), textTypes.size());
        Log("[farm] visibility: widget=%d rect=%d calculated=%d panel-depth=%d NGUI-event-offset=%d",
            widgetAlpha != nullptr,rectAlpha != nullptr,rectCalculateAlpha != nullptr,panelDepth != nullptr,buttonEventsOffset);
        Log("[farm] prompt tap: exact position=%d",transformPosition && goLayer && uiFindCamera && uiCachedCamera && worldToScreen && screenWidth && screenHeight);
        try { InitStats(); }
        catch (const std::exception& error) { Log("[farm] match kills unavailable: %s", error.what()); }
    } catch (const std::exception& error) {
        Log("[farm] unavailable: %s", error.what());
        SetStatus(error.what());
    }
}

bool NeedsFrameCheck() { return running && watchFrames; }
void ObserveFrame(uint64_t hash, uint32_t now) {
    if (frameHash.exchange(hash) != hash || !frameChangedAt.load()) frameChangedAt = now;
}
void Start() { command = 1; }
void Stop() { command = 2; }
void Inspect() { inspect = true; }
bool Active() { return running.load(); }
bool SuppressFeatures() { return running && !combat; }
Status GetStatus() { std::lock_guard<std::mutex> lock(statusMutex); return status; }
void SettingsToSave(Cfg& settings) {
    if (!running) return;
    const auto farmSettings = settings.farm;
    const auto worldSettings = settings.world;
    std::lock_guard<std::mutex> lock(profileMutex);
    settings = profile;
    settings.farm = farmSettings;
    settings.world = worldSettings;
}
void Shutdown() { pendingClick.reset(); pendingReward.reset(); command = 0; RestoreProfile(false); controls.clear(); SetStatus("Stopped"); }

void Tick() {
    // Unity callbacks can re-enter while a button handler is running.
    static bool ticking = false;
    if (ticking) return;
    struct TickScope {
        bool& flag;
        explicit TickScope(bool& value) : flag(value) { flag = true; }
        ~TickScope() { flag = false; }
    } scope(ticking);
    const DWORD now = GetTickCount();
    if (now - tickAt < 50) return;
    tickAt = now;
    const int request = command.exchange(0);
    if (request == 2) { RestoreProfile(); SetStatus("Stopped; normal settings restored"); return; }
    if (request == 1 && !running) {
        if (!ready) { SetStatus("Game UI detection unavailable; see the log"); return; }
        {
            std::lock_guard<std::mutex> lock(profileMutex);
            profile = cfg;
        }
        controller = {};
        rewardTracker = {};
        rewardMatch = rewardClaim = 0;
        matchRewards.clear();
        pendingReward.reset();
        pendingClick.reset();
        confirmedTransitions = 0;
        running = true;
        combat = false;
        startedAt = now;
        scanAt = 0;
        actionGate = {};
        watchdog = {};
        frameHash = 0;
        frameChangedAt = now;
        lastFrameRecovery = now;
        reportedKills = -1;
        DisableFeatures();
        Log("[farm] started; kill limit=%d", cfg.farm.kill_limit);
    }
    const bool diagnostic = inspect.exchange(false);
    if (!ready) return;
    if (!running && !diagnostic) {
        try { MeasureRapidAmmo(now); }
        catch (const std::exception& error) {
            static DWORD lastError = 0;
            if (now - lastError >= 10000) { Log("[rapid-rate] unavailable: %s", error.what()); lastError = now; }
            controls.clear();
        }
        return;
    }
    try {
        static DWORD diagnosticAt = 0;
        const bool logButtons = diagnostic || now - diagnosticAt >= 30000;
        const bool scanned = logButtons || !scanAt || now - scanAt >= 750;
        if (scanned) { Scan(logButtons); scanAt = now; if (logButtons) diagnosticAt = now; }
        if (!running) { SetStatus("Visible game buttons were written to the log"); return; }
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        auto* local = game::Local();
        logic::Observation observation;
        observation.match = hudPresent;
        observation.alive = local && local->alive;
        observation.paused = render::MenuOpen || cfg.panic || foregroundProcess != GetCurrentProcessId();
        observation.reward = Has(logic::Action::Reward);
        observation.replay = Has(logic::Action::Replay);
        observation.deathmatch = Has(logic::Action::Deathmatch);
        observation.classic = Has(logic::Action::Classic);
        observation.nuclearCity = Has(logic::Action::NuclearCity);
        observation.battle = Has(logic::Action::Battle);
        observation.start = Has(logic::Action::Start);
        observation.respawn = Has(logic::Action::Respawn);
        observation.resume = Has(logic::Action::Resume);
        MatchStats stats;
        if (observation.match && !modalPresent) {
            try { stats = ReadStats(); }
            catch (const std::exception& error) {
                static DWORD lastStatsError = 0;
                if (now-lastStatsError >= 10000) {
                    Log("[farm] match stats temporarily unavailable: %s",error.what());
                    lastStatsError = now;
                }
            }
        }
        observation.matchKills = stats.kills;
        observation.matchId = stats.identity;
        if (modalPresent && !observation.reward && !observation.respawn && !observation.resume) observation.paused = true;
        const auto decision = controller.Tick(observation, cfg.farm.kill_limit);
        combat = decision.combat;
        if (combat) EnableCombat();
        else if (profileCombat || decision.newMatch || cfg.silent.enabled || cfg.aim.enabled || cfg.trig.enabled) DisableFeatures();
        if (decision.newMatch) {
            ++rewardMatch;
            matchRewards.clear();
            Log("[farm] match started; kill counter reset");
        }
        if (controller.inMatch && (decision.newMatch || controller.kills != reportedKills)) {
            reportedKills = controller.kills;
            Log("[farm] game kills=%d limit=%d combat=%d", controller.kills, cfg.farm.kill_limit, combat.load());
        }
        SetStatus(decision.phase == logic::Phase::Waiting && observation.match ? "Waiting for the game's kill counter" : Message(decision.phase));
        const auto* target = Target(decision.action);
        const std::string identity = target ? target->path + "|" + target->label : "";
        const bool externalPause = render::MenuOpen || cfg.panic || foregroundProcess != GetCurrentProcessId();
        const bool needsInput = decision.action != logic::Action::None || modalPresent || !hudPresent;
        // Screen identity uses eligible UI, so animation frames cannot hide a stalled claim.
        std::vector<std::string> visibleControls;
        for (const auto& control : controls) {
            if (Eligible(control)) visibleControls.push_back(control.path+"|"+std::to_string(int(control.action))+"|"+std::to_string(control.depth));
        }
        const auto screen = logic::ScreenIdentity(std::move(visibleControls));
        watchdog.Observe(screen,externalPause,needsInput,now);
        watchFrames = needsInput && !externalPause;
        const bool stalledUi = watchdog.Recover(now);
        const auto changedFrame = frameChangedAt.load();
        const bool stalledFrame = watchFrames && frameHash.load() && changedFrame && now-changedFrame >= 15000 && now-lastFrameRecovery >= 15000;
        if (stalledUi || stalledFrame) {
            if (stalledFrame) lastFrameRecovery = now;
            if (!stalledUi) ++watchdog.recoveries;
            inspect = true;
            scanAt = 0;
            Log("[farm-watchdog] stalled UI; attempts=%u frameStillSeconds=%u; refreshing all controls",
                watchdog.attempts,frameChangedAt.load() ? (now-frameChangedAt.load())/1000 : 0);
            SetStatus("Recovery check: rescanning visible controls and retrying recognized actions");
        }
        // Last resort: a screen that needs input has had nothing clickable for 30 s.
        // Cycle safe dismiss buttons (Continue/OK/Close/Collect...) with the normal retry backoff.
        auto action = decision.action;
        bool fallback = false;
        if (!target && !externalPause && (modalPresent || !hudPresent) && watchdog.StalledFor(now) >= 30000) {
            target = FallbackTarget();
            fallback = target != nullptr;
            if (fallback) {
                action = logic::Action::Reward;
                SetStatus("Recovery: trying safe Continue/OK/Close buttons on an unrecognized screen");
            }
        }
        TapPrompt(externalPause, now);
        const bool canClick = actionGate.Update(action, target ? target->path + "|" + target->label : identity,
                                                fallback ? externalPause : observation.paused, now);
        if (decision.action == logic::Action::Replay && !canClick)
            SetStatus("Waiting for trophy, match and XP rewards to finish");
        if (scanned && target && canClick) Click(target, action, fallback, now);
    } catch (const std::exception& error) {
        combat = false;
        if (running && profileCombat) DisableFeatures();
        scanAt = now;
        SetStatus(std::string("Waiting: ") + error.what());
        Log("[farm] %s", error.what());
    }
}
}
