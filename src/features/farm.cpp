#include "farm.h"
#include "farm_state.h"
#include "features.h"
#include "../core/config.h"
#include "../core/render.h"
#include "../sdk/il2cpp.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_set>

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
    std::string lastAction;
    int actionAttempts = 0;
    int reportedKills = -1;
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

    struct Backend {
        void* type = nullptr;
        void* click = nullptr;
        void* enabled = nullptr;
        bool unity = false;
    };
    struct TextType { void* type; void* getter; };
    struct Control {
        void* object;
        void* gameObject;
        Backend backend;
        std::string path, label;
        logic::Action action = logic::Action::None;
        bool modal = false;
        // Snapshots can outlive the discovery array. Keep both managed wrappers
        // alive until the snapshot is discarded, including during scene changes.
        std::unique_ptr<Root> objectRoot, gameObjectRoot;
    };
    std::vector<Backend> backends;
    std::vector<TextType> textTypes;
    std::vector<Control> controls;
    void *findObjects, *componentGo, *goTransform, *goActive, *objectName, *transformParent, *goChildren;
    void *hudType, *unityEventGetter, *unityEventInvoke, *goSendMessage;
    bool hudPresent = false, modalPresent = false;

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
        for (const auto& text : textTypes) {
            Root labels(Call(goChildren, gameObject, {text.type, &inactive}));
            for (void* label : Array(labels.pointer)) {
                const std::string value = il2::Str(Call(text.getter, label));
                if (value.empty()) continue;
                if (!seen.insert(logic::Normalize(value)).second) continue;
                if (!result.empty()) result += ' ';
                result += value;
                if (result.size() > 1024) break;
            }
        }
        return result;
    }
    void Scan(bool diagnostic) {
        controls.clear();
        hudPresent = modalPresent = false;
        Root hud(Find(hudType));
        for (void* object : Array(hud.pointer))
            if (ActiveObject(Call(componentGo, object))) { hudPresent = true; break; }
        std::unordered_set<void*> seen;
        for (const auto& backend : backends) {
            Root objects(Find(backend.type));
            for (void* object : Array(objects.pointer)) {
                void* gameObject = Call(componentGo, object);
                Root retainedGo(gameObject);
                if (!ActiveObject(gameObject) || !Bool(backend.enabled, object)) continue;
                if (!seen.insert(gameObject).second) continue;
                Control control{object, gameObject, backend, Path(gameObject), Label(gameObject)};
                control.objectRoot = std::make_unique<Root>(object);
                control.gameObjectRoot = std::make_unique<Root>(gameObject);
                control.action = logic::Classify(control.path, control.label);
                control.modal = logic::IsModal(control.path) || control.action == logic::Action::Reward;
                modalPresent |= control.modal;
                if (diagnostic) Log("[farm] button '%s' text='%s' action=%d", control.path.c_str(), control.label.c_str(), int(control.action));
                controls.push_back(std::move(control));
            }
        }
        if (diagnostic) Log("[farm] scan: %zu enabled buttons; match HUD=%d modal=%d", controls.size(), hudPresent, modalPresent);
    }
    bool Eligible(const Control& control) { return !modalPresent || control.modal; }
    const Control* Target(logic::Action action) {
        if (action == logic::Action::None) return nullptr;
        const auto found = std::find_if(controls.begin(), controls.end(), [&](const auto& control) { return Eligible(control) && control.action == action; });
        return found == controls.end() ? nullptr : &*found;
    }
    bool Has(logic::Action action) {
        return std::any_of(controls.begin(), controls.end(), [&](const auto& control) { return Eligible(control) && control.action == action; });
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
        cfg.silent.wallbang = false;
        cfg.silent.auto_fire = std::clamp(cfg.farm.fire_mode, 1, 2);
        cfg.aim.team_check = false;
        profileCombat = true;
    }
    void RestoreProfile(bool applyFov = true) {
        if (!running) return;
        combat = false;
        rage::Release();
        const auto farmSettings = cfg.farm;
        const bool panic = cfg.panic;
        {
            std::lock_guard<std::mutex> lock(profileMutex);
            cfg = profile;
        }
        cfg.farm = farmSettings;
        cfg.panic = panic;
        running = false;
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
    void Click(logic::Action action, DWORD now) {
        const auto* found = Target(action);
        if (!found) return;
        const std::string identity = found->path + "|" + found->label;
        if (identity != lastAction) { lastAction = identity; actionAttempts = 0; }
        if (actionAttempts >= 3) { SetStatus("The game screen did not change; waiting for manual input"); return; }
        if (!ActiveObject(found->gameObject) || !Bool(found->backend.enabled, found->object)) return;
        // Copy the target before invoking game code. A callback may replace the
        // UI, so never read the discovery vector again after dispatch.
        const std::string path = found->path, label = found->label;
        void* object = found->object;
        void* gameObject = found->gameObject;
        const Backend backend = found->backend;
        Root retainedObject(object), retainedGo(gameObject);
        rage::Release();
        if (backend.unity) {
            Root event(Call(unityEventGetter, object));
            Call(unityEventInvoke, event.pointer);
        } else {
            Root message(il2::api.string_new("OnClick"));
            int dontRequireReceiver = 1;
            Call(goSendMessage, gameObject, {message.pointer, &dontRequireReceiver});
        }
        controller.Clicked(action);
        actionGate.Clicked(now);
        scanAt = 0;
        ++actionAttempts;
        controls.clear();
        Log("[farm] clicked '%s' text='%s'", path.c_str(), label.c_str());
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
        hudType = il2::TypeObject(Klass("InGameGUI", ""));
        if (!findObjects || !objectName || !componentGo || !goTransform || !goActive || !goChildren || !transformParent || !hudType)
            throw std::runtime_error("game UI discovery unavailable");
        if (void* button = Klass("UIButton", "")) {
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
        for (const auto& entry : {std::pair{"UILabel", ""}, std::pair{"Text", "UnityEngine.UI"}, std::pair{"TMP_Text", "TMPro"}}) {
            void* klass = Klass(entry.first, entry.second);
            void* getter = Method(klass, "get_text", {});
            if (klass && getter) textTypes.push_back({il2::TypeObject(klass), getter});
        }
        if (backends.empty() || textTypes.empty()) throw std::runtime_error("no supported game buttons or labels found");
        ready = true;
        Log("[farm] UI ready: %zu button backends, %zu label types", backends.size(), textTypes.size());
        try { InitStats(); }
        catch (const std::exception& error) { Log("[farm] match kills unavailable: %s", error.what()); }
    } catch (const std::exception& error) {
        Log("[farm] unavailable: %s", error.what());
        SetStatus(error.what());
    }
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
    std::lock_guard<std::mutex> lock(profileMutex);
    settings = profile;
    settings.farm = farmSettings;
}
void Shutdown() { command = 0; RestoreProfile(false); controls.clear(); SetStatus("Stopped"); }

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
        running = true;
        combat = false;
        startedAt = now;
        scanAt = 0;
        actionGate = {};
        actionAttempts = 0;
        lastAction.clear();
        reportedKills = -1;
        DisableFeatures();
        Log("[farm] started; kill limit=%d", cfg.farm.kill_limit);
    }
    const bool diagnostic = inspect.exchange(false);
    if (!ready || (!running && !diagnostic)) return;
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
        const MatchStats stats = ReadStats();
        observation.matchKills = stats.kills;
        observation.matchId = stats.identity;
        if (modalPresent && !observation.reward && !observation.respawn) observation.paused = true;
        const auto decision = controller.Tick(observation, cfg.farm.kill_limit);
        combat = decision.combat;
        if (combat) EnableCombat();
        else if (profileCombat || decision.newMatch || cfg.silent.enabled || cfg.aim.enabled || cfg.trig.enabled) DisableFeatures();
        if (decision.newMatch) Log("[farm] match started; kill counter reset");
        if (controller.inMatch && (decision.newMatch || controller.kills != reportedKills)) {
            reportedKills = controller.kills;
            Log("[farm] game kills=%d limit=%d combat=%d", controller.kills, cfg.farm.kill_limit, combat.load());
        }
        SetStatus(decision.phase == logic::Phase::Waiting && observation.match ? "Waiting for the game's kill counter" : Message(decision.phase));
        const auto* target = Target(decision.action);
        const std::string identity = target ? target->path + "|" + target->label : "";
        if (identity != actionGate.identity || decision.action != actionGate.candidate) {
            lastAction.clear();
            actionAttempts = 0;
        }
        const bool canClick = actionGate.Update(decision.action, identity, observation.paused, now);
        if (decision.action == logic::Action::Replay && !canClick)
            SetStatus("Waiting for trophy, match and XP rewards to finish");
        if (scanned && target && canClick) Click(decision.action, now);
    } catch (const std::exception& error) {
        combat = false;
        if (running && profileCombat) DisableFeatures();
        scanAt = now;
        SetStatus(std::string("Waiting: ") + error.what());
        Log("[farm] %s", error.what());
    }
}
}
