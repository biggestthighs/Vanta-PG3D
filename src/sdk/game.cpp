#include "game.h"
#include "il2cpp.h"
#include "offsets.h"
#include "../features/features.h"
#include "MinHook.h"
#ifdef CORE_BUILD
#include "../core/config.h"
#include "../core/render.h"
#include "../features/custommodel.h"
#include "../features/farm.h"
#endif

namespace game {
    static void* kPlayer = nullptr;
    static void* kWeapon = nullptr;
    static void* kCamera = nullptr;
    static void* kTransform = nullptr;
    static void* kTextMesh = nullptr;
    static void* kDmg = nullptr;

    static int offNick = -1, offTr = -1, offHead = -1, offDmg = -1, offHp = -1, offTeam = -1;
    static int offOwner = -1, offAmmo = -1, offRecoil = -1, offSpread = -1, offDelay = -1, offAuto = -1;
    static int offPen = -1;
    static bool s_headTracked = false;

    static void* fnW2S = nullptr;
    static void* fnCamMain = nullptr;
    static void* fnGetTr = nullptr;
    static void* fnGetPos = nullptr;
    static void* fnGetRot = nullptr;
    static void* fnSetRot = nullptr;
    static void* fnLookAt = nullptr;
    static void* fnLinecast = nullptr;
    static void* fnGetText = nullptr;
    static void* fnGetColor = nullptr;
    static void* fnEnabled = nullptr;
    static void* fnSetFov = nullptr;
    static void* fnGetFov = nullptr;
    static void* fnIsDead = nullptr;
    static void* oSetFov = nullptr;
    static float s_gameFov = 0.f;   // last FOV the game itself asked for
    static int offZoom = -1;
    static void* fnSetPos = nullptr;
    static void* fnRendGet = nullptr;
    static void* fnRendSet = nullptr;
    static void* fnBehSet = nullptr;
    static void* oWeaponLate = nullptr;
    static void* fnGetGO = nullptr;       // Component.get_gameObject
    static void* fnGoActive = nullptr;    // GameObject.get_activeInHierarchy
    static void* fnGoSetActive = nullptr; // GameObject.SetActive
    static void* fnGoLayer = nullptr;     // GameObject.get_layer
    static void* fnCamGetMask = nullptr;  // Camera.get_cullingMask
    static void* fnCamSetMask = nullptr;  // Camera.set_cullingMask
    static void* fnRendVis = nullptr;     // Renderer.get_isVisible
    static void* fnGoSelf = nullptr;      // GameObject.get_activeSelf
    static void* fnGoTr = nullptr;        // GameObject.get_transform
    static void* fnTrParent = nullptr;    // Transform.get_parent
    static void* fnRendForce = nullptr;   // Renderer.set_forceRenderingOff
    static void* fnObjName = nullptr;     // Object.get_name
    static void* fnLossy = nullptr;       // Transform.get_lossyScale
    static void* fnSmrOff = nullptr;      // SkinnedMeshRenderer.get_updateWhenOffscreen
    static void* fnSmrOffSet = nullptr;   // SkinnedMeshRenderer.set_updateWhenOffscreen
    static void* fnSmrMesh = nullptr;     // SkinnedMeshRenderer.get_sharedMesh
    static void* fnBounds = nullptr;      // Renderer.get_bounds
    static void* fnAnCull = nullptr, *fnAnCullSet = nullptr;   // Animator.cullingMode
    static void* fnAnCtl = nullptr, *fnAnCtlSet = nullptr;     // Animator.runtimeAnimatorController
    static void* fnAnSpeed = nullptr, *fnAnSpeedSet = nullptr; // Animator.speed
    static void* fnAnParCount = nullptr, *fnAnPar = nullptr;   // Animator.parameterCount / GetParameter(int)
    static void* fnAnSetFloat = nullptr, *fnAnSetBool = nullptr; // Animator.SetFloat/SetBool(int hash, ..)
    static void* fnParName = nullptr, *fnParType = nullptr, *fnParHash = nullptr; // AnimatorControllerParameter
    static void* fnStrHash = nullptr;     // Animator.StringToHash(string), static
    static void* fnAnParams = nullptr;    // Animator.get_parameters
    static void* fnFindObjs1 = nullptr, *fnFindObjs2 = nullptr; // Object.FindObjectsOfType(Type) / (Type, bool)
    static void* fnIsChildOf = nullptr;   // Transform.IsChildOf(Transform)
    static void* fnSharedMat = nullptr;   // Renderer.get_sharedMaterial
    static void* fnMatShader = nullptr;   // Material.get_shader
    static void* s_wrapType = nullptr;    // System.Type of PlayerAnimationWrapper
    static void* oUiUpdate = nullptr;     // UICamera.Update: safe main-thread UI phase
    static void* oPreCull = nullptr;      // Camera.FireOnPreCull
    static int offWrapAnim = -1, offWrapCtl = -1;   // PlayerAnimationWrapper.animator / defaultAnimatorController
    static int offRend[4] = { -1, -1, -1, -1 };  // body, head, arms, legs renderers on Player_move_c
    static int offGunCam = -1;

    static void* oUpdate = nullptr;
    static void* oLateUpdate = nullptr;
    static void* oWeaponUpdate = nullptr;

    // Update fires every frame for every live Player_move_c, so "last seen" is a
    // reliable liveness signal. Without it the list keeps dead pointers whose freed
    // IL2CPP memory is often still mapped, so Ok() passes and positions read as garbage.
    struct Seen { void* obj; DWORD t; };
    static constexpr DWORD kStaleMs = 500;

    static std::mutex s_mtx;
    static std::vector<Seen> s_objs;
    static std::vector<Seen> s_weapons;
    static std::vector<Player> s_players;
    static Player s_local;
    static bool s_hasLocal = false;
    static bool s_ready = false;
    static bool s_hooked = false;
    static Vec3 s_camPos{};
    static DWORD s_tickMs = 0;

    static void Touch(std::vector<Seen>& list, void* self, size_t cap) {
        const DWORD now = GetTickCount();
        std::lock_guard<std::mutex> g(s_mtx);
        for (auto& e : list) if (e.obj == self) { e.t = now; return; }
        if (list.size() < cap) list.push_back({ self, now });
    }

    static std::vector<void*> Live(std::vector<Seen>& list) {
        const DWORD now = GetTickCount();
        std::vector<void*> out;
        std::lock_guard<std::mutex> g(s_mtx);
        for (size_t i = list.size(); i-- > 0;)
            if (now - list[i].t > kStaleMs) list.erase(list.begin() + i);
        out.reserve(list.size());
        for (auto& e : list) out.push_back(e.obj);
        return out;
    }

#ifdef CORE_BUILD
    // ---- third person (view only) -----------------------------------------------------
    // The camera is moved back at the END of the frame (after every Update, so the game's own
    // aim and firing logic still run from the first-person pose) and put back at the start of
    // the next one. The game's body renderers are switched on so the character is drawn and
    // the first-person gun camera is switched off. All of it is undone when turned off.
    static bool s_tpOffset = false, s_tpActive = false, s_gunWas = true;
    static Vec3 s_tpPos{};
    static void* s_tpCam = nullptr;
    static void* s_tpObj = nullptr;
    static void* s_rend[4]{};
    static bool s_rendWas[4]{};
    static void* s_gunCam = nullptr;

    // A destroyed Unity object throws from these; a bad one must cost a frame, not the game.
    static bool RendGet(void* r) {
        __try { return reinterpret_cast<bool(*)(void*, void*)>(fnRendGet)(r, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }
    static void RendSet(void* r, bool v) {
        __try { reinterpret_cast<void(*)(void*, bool, void*)>(fnRendSet)(r, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static bool BehGet(void* b) {
        __try { return reinterpret_cast<bool(*)(void*, void*)>(fnEnabled)(b, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }
    static void BehSet(void* b, bool v) {
        __try { reinterpret_cast<void(*)(void*, bool, void*)>(fnBehSet)(b, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void PosSet(void* tr, Vec3 p) {
        __try { reinterpret_cast<void(*)(void*, Vec3&, void*)>(fnSetPos)(tr, p, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void* GoOf(void* c) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fnGetGO)(c, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static bool GoActive(void* go) {
        __try { return reinterpret_cast<bool(*)(void*, void*)>(fnGoActive)(go, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }
    static void GoSetActive(void* go, bool v) {
        __try { reinterpret_cast<void(*)(void*, bool, void*)>(fnGoSetActive)(go, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static int GoLayer(void* go) {
        __try { return reinterpret_cast<int(*)(void*, void*)>(fnGoLayer)(go, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    static int CamMask(void* c) {
        __try { return reinterpret_cast<int(*)(void*, void*)>(fnCamGetMask)(c, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    static void CamMaskSet(void* c, int m) {
        __try { reinterpret_cast<void(*)(void*, int, void*)>(fnCamSetMask)(c, m, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static bool RendSeen(void* r) {
        __try { return reinterpret_cast<bool(*)(void*, void*)>(fnRendVis)(r, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool GoSelf(void* go) {
        __try { return reinterpret_cast<bool(*)(void*, void*)>(fnGoSelf)(go, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }
    static void* GoTr(void* go) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fnGoTr)(go, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static void* TrParent(void* tr) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fnTrParent)(tr, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static void RendForceOff(void* r, bool v) {
        __try { reinterpret_cast<void(*)(void*, bool, void*)>(fnRendForce)(r, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void* ObjName(void* o) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fnObjName)(o, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static Vec3 ScaleOf(void* tr) {
        __try { return reinterpret_cast<Vec3(*)(void*, void*)>(fnLossy)(tr, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
    }

    static bool SmrOff(void* r) {
        __try { return reinterpret_cast<bool(*)(void*, void*)>(fnSmrOff)(r, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }
    static void SmrOffSet(void* r, bool v) {
        __try { reinterpret_cast<void(*)(void*, bool, void*)>(fnSmrOffSet)(r, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void* SmrMesh(void* r) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fnSmrMesh)(r, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    struct Bnds { Vec3 c, e; };
    static Bnds BoundsOf(void* r) {
        __try { return reinterpret_cast<Bnds(*)(void*, void*)>(fnBounds)(r, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
    }
    static int IntCall(void* fn, void* o) {
        __try { return reinterpret_cast<int(*)(void*, void*)>(fn)(o, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    static void IntSet(void* fn, void* o, int v) {
        __try { reinterpret_cast<void(*)(void*, int, void*)>(fn)(o, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static float FloatCall(void* fn, void* o) {
        __try { return reinterpret_cast<float(*)(void*, void*)>(fn)(o, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return 1.f; }
    }
    static void FloatSet(void* fn, void* o, float v) {
        __try { reinterpret_cast<void(*)(void*, float, void*)>(fn)(o, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void* PtrCall(void* fn, void* o) {
        __try { return reinterpret_cast<void*(*)(void*, void*)>(fn)(o, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static void PtrSet(void* fn, void* o, void* v) {
        __try { reinterpret_cast<void(*)(void*, void*, void*)>(fn)(o, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void* ParamAt(void* an, int i) {
        __try { return reinterpret_cast<void*(*)(void*, int, void*)>(fnAnPar)(an, i, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static void AnSetFloat(void* an, int h, float v) {
        __try { reinterpret_cast<void(*)(void*, int, float, void*)>(fnAnSetFloat)(an, h, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    static void AnSetBool(void* an, int h, bool v) {
        __try { reinterpret_cast<void(*)(void*, int, bool, void*)>(fnAnSetBool)(an, h, v, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    // Class name of an arbitrary pointer; a pointer that is not an object must not take the game down.
    static const char* KlassName(void* p) {
        __try {
            void* k = il2::api.object_get_class ? il2::api.object_get_class(p) : nullptr;
            return k && il2::api.class_get_name ? il2::api.class_get_name(k) : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    // The player's PlayerAnimationWrapper is a field of Player_move_c at an obfuscated name: find it by class.
    static void* FindWrapper(void* obj) {
        __try {
            for (int off = 0x20; off < 0xE00; off += 8) {
                void* p = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + off);
                if (!p || !Ok(p)) continue;
                const char* n = KlassName(p);
                if (n && !strcmp(n, "PlayerAnimationWrapper")) return p;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return nullptr;
    }

    static void* FindObjs(void* type) {
        __try {
            if (fnFindObjs2) return reinterpret_cast<void*(*)(void*, bool, void*)>(fnFindObjs2)(type, true, nullptr);
            return reinterpret_cast<void*(*)(void*, void*)>(fnFindObjs1)(type, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    static bool IsChildOfTr(void* tr, void* parent) {
        __try { return reinterpret_cast<bool(*)(void*, void*, void*)>(fnIsChildOf)(tr, parent, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    // The PlayerAnimationWrapper that sits above the given body renderer in the object tree. Nothing on
    // the player object points at it, so ask Unity for all of them and pick the one that contains the mesh.
    static void* FindWrapperByTree(void* bodyRenderer) {
        if ((!fnFindObjs1 && !fnFindObjs2) || !fnIsChildOf || !s_wrapType || !fnGetTr || !Ok(bodyRenderer)) return nullptr;
        __try {
            void* arr = FindObjs(s_wrapType);
            if (!Ok(arr)) return nullptr;
            const size_t n = *reinterpret_cast<size_t*>(reinterpret_cast<uint8_t*>(arr) + 0x18); // Il2CppArray length
            void* rtr = reinterpret_cast<void*(*)(void*, void*)>(fnGetTr)(bodyRenderer, nullptr);
            for (size_t i = 0; i < n && i < 64; i++) {
                void* w = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(arr) + 0x20 + i * 8);
                if (!Ok(w)) continue;
                void* wtr = reinterpret_cast<void*(*)(void*, void*)>(fnGetTr)(w, nullptr);
                if (Ok(wtr) && IsChildOfTr(rtr, wtr)) return w;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return nullptr;
    }

    // The enemy's own layers (root, head collider, transform), without layer 0: a ray masked to
    // these ignores the world, which is what makes a shot through a wall possible.
    int LayerBits(const Player& p) {
        int bits = 0;
        for (void* c : { p.obj, p.head, p.tr }) {
            void* go = Ok(c) && fnGetGO ? GoOf(c) : nullptr;
            const int l = Ok(go) && fnGoLayer ? GoLayer(go) : 0;
            if (l > 0 && l < 32) bits |= 1 << l;
        }
        return bits;
    }

    // The body renderers can sit under objects the game keeps switched off in first person, and
    // switching only the renderer's own object does nothing then. Walk up and switch on every
    // off ancestor (never the player object itself); they are remembered to be switched back off.
    static void* s_actGo[12]{};
    static int s_nAct = 0;
    static void ActivateChain(void* go, void* stopGo) {
        if (!fnGoActive || !fnGoSetActive || !fnGoTr || !fnTrParent || GoActive(go)) return;
        void* g = go;
        for (int depth = 0; depth < 10 && Ok(g) && g != stopGo; depth++) {
            if (fnGoSelf && !GoSelf(g)) {
                GoSetActive(g, true);
                bool known = false;
                for (int i = 0; i < s_nAct; i++) known |= s_actGo[i] == g;
                if (!known && s_nAct < 12) s_actGo[s_nAct++] = g;
            }
            if (GoActive(go)) return;
            void* up = TrParent(GoTr(g));
            g = Ok(up) ? GoOf(up) : nullptr;
        }
    }

    // ---- the character's animation ------------------------------------------------------------
    // In first person nothing animates the body. Find its Animator through the game's own
    // PlayerAnimationWrapper, make sure it runs, and feed its speed / strafe / grounded parameters
    // from how the player really moves. A parameter the game drives itself is simply overwritten.
    struct TpPar { int hash; int kind; }; // kind 1 forward speed, 2 sideways, 3 on the ground
    static void* s_anim = nullptr;
    static TpPar s_par[24];
    static int s_nPar = 0;
    static bool s_anWasOn = true;
    static int s_anWasCull = 0;
    static float s_anWasSpeed = 1.f;
    static bool s_smrWas[4]{};
    static Vec3 s_pvPos{};
    static DWORD s_pvT = 0;
    static float s_fwdV = 0.f, s_sideV = 0.f;

    static bool Has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

    static void TpFindAnimator(void* obj) {
        s_anim = nullptr;
        s_nPar = 0;
        s_pvT = 0;
        void* wrap = FindWrapper(obj);
        const char* how = "a field of the player object";
        if (!wrap) { wrap = FindWrapperByTree(s_rend[0]); how = "the object tree above the body mesh"; }
        if (!wrap || offWrapAnim < 0) { Log("[tp] no PlayerAnimationWrapper found (not a player field, not above the body mesh)"); return; }
        Log("[tp] PlayerAnimationWrapper %p found in %s", wrap, how);
        void* an = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(wrap) + offWrapAnim);
        const char* cn = Ok(an) ? KlassName(an) : nullptr;
        if (!cn || strcmp(cn, "Animator")) { Log("[tp] wrapper %p has no Animator (field holds %s)", wrap, cn ? cn : "nothing"); return; }
        s_anim = an;
        s_anWasOn = BehGet(an);
        s_anWasCull = fnAnCull ? IntCall(fnAnCull, an) : 0;
        s_anWasSpeed = fnAnSpeed ? FloatCall(fnAnSpeed, an) : 1.f;
        void* ctl = fnAnCtl ? PtrCall(fnAnCtl, an) : nullptr;
        void* def = offWrapCtl > 0 ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(wrap) + offWrapCtl) : nullptr;
        if (!ctl && Ok(def) && fnAnCtlSet) { PtrSet(fnAnCtlSet, an, def); ctl = fnAnCtl ? PtrCall(fnAnCtl, an) : nullptr; }
        const int n = fnAnParCount ? IntCall(fnAnParCount, an) : 0;
        Log("[tp] animator %p: enabled=%d culling=%d speed=%.2f controller=%p (wrapper default %p), %d parameters", an, s_anWasOn,
            s_anWasCull, s_anWasSpeed, ctl, def, n);
        void* parArr = nullptr; // Animator.parameters, for when GetParameter(int) gives nothing
        for (int i = 0; i < n && i < 24 && (fnAnPar || fnAnParams) && fnParName && fnParType && (fnParHash || fnStrHash); i++) {
            void* pr = fnAnPar ? ParamAt(an, i) : nullptr;
            if (!Ok(pr) && fnAnParams) {
                if (!parArr) parArr = PtrCall(fnAnParams, an);
                if (Ok(parArr) && static_cast<size_t>(i) < *reinterpret_cast<size_t*>(reinterpret_cast<uint8_t*>(parArr) + 0x18))
                    pr = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(parArr) + 0x20 + i * 8);
            }
            if (!Ok(pr)) { if (i == 0) Log("[tp] animator parameters are unreadable (GetParameter and parameters both failed)"); continue; }
            void* nameObj = PtrCall(fnParName, pr);
            std::string nm = il2::Str(nameObj);
            const int type = IntCall(fnParType, pr), hash = fnParHash ? IntCall(fnParHash, pr) : IntCall(fnStrHash, nameObj); // static: arg is the string
            for (auto& c : nm) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            int kind = 0;
            if (type == 4 && Has(nm, "ground")) kind = 3;
            else if (type == 1 && (Has(nm, "strafe") || Has(nm, "side") || Has(nm, "horiz") || Has(nm, "right"))) kind = 2;
            else if (type == 1 && (Has(nm, "speed") || Has(nm, "move") || Has(nm, "walk") || Has(nm, "run") || Has(nm, "forward") ||
                                   Has(nm, "vert") || Has(nm, "vel"))) kind = 1;
            Log("[tp]   parameter '%s' type %d%s", nm.c_str(), type, kind ? " (driven)" : "");
            if (kind && s_nPar < 24) s_par[s_nPar++] = { hash, kind };
        }
    }

    static void TpAnimate() {
        void* an = s_anim;
        if (!Ok(an)) return;
        if (fnEnabled && fnBehSet && !BehGet(an)) BehSet(an, true);
        if (fnAnCull && fnAnCullSet && IntCall(fnAnCull, an) != 0) IntSet(fnAnCullSet, an, 0); // always animate, even unseen
        if (fnAnSpeed && fnAnSpeedSet && FloatCall(fnAnSpeed, an) < 0.5f) FloatSet(fnAnSpeedSet, an, 1.f);
        const DWORD now = GetTickCount();
        if (s_pvT && now > s_pvT && Ok(s_local.tr)) {
            const float dt = (now - s_pvT) / 1000.f;
            if (dt > 0.002f && dt < 0.25f) {
                const float vx = (s_local.pos.x - s_pvPos.x) / dt, vz = (s_local.pos.z - s_pvPos.z) / dt;
                const Vec3 f = Forward(GetRotation(s_local.tr));
                const float fl = std::max(sqrtf(f.x * f.x + f.z * f.z), 1e-3f);
                const float fwd = (vx * f.x + vz * f.z) / fl, side = (vx * f.z - vz * f.x) / fl;
                s_fwdV += (fwd - s_fwdV) * 0.35f;
                s_sideV += (side - s_sideV) * 0.35f;
            }
        }
        s_pvPos = s_local.pos;
        s_pvT = now;
        for (int i = 0; i < s_nPar; i++) {
            if (s_par[i].kind == 1 && fnAnSetFloat) AnSetFloat(an, s_par[i].hash, s_fwdV);
            else if (s_par[i].kind == 2 && fnAnSetFloat) AnSetFloat(an, s_par[i].hash, s_sideV);
            else if (s_par[i].kind == 3 && fnAnSetBool) AnSetBool(an, s_par[i].hash, true);
        }
    }

    // The main camera's culling mask is widened for the render only (the body may sit on a layer
    // the camera skips) and put back at the start of the next frame with the camera position.
    static bool s_maskSaved = false;
    static int s_maskOrig = 0;
    static void* s_maskCam = nullptr;
    static void MaskRestore() {
        if (!s_maskSaved) return;
        s_maskSaved = false;
        if (fnCamSetMask && Ok(s_maskCam)) CamMaskSet(s_maskCam, s_maskOrig);
    }

    // Start of the local player's Update: put the camera back where the game left it.
    static void TpEarly() {
        MaskRestore();
        if (!s_tpOffset) return;
        s_tpOffset = false;
        if (Ok(s_tpCam) && fnSetPos) PosSet(s_tpCam, s_tpPos);
    }

    static void TpRestore() {
        custommodel::Hide();
        s_tpActive = false;
        MaskRestore();
        // after a respawn the old objects are gone; touching them would throw
        if (!s_hasLocal || s_local.obj != s_tpObj) { s_nAct = 0; s_anim = nullptr; return; }
        if (fnRendSet)
            for (int i = 0; i < 4; i++) if (Ok(s_rend[i])) RendSet(s_rend[i], s_rendWas[i]);
        if (fnSmrOffSet)
            for (int i = 0; i < 4; i++) if (Ok(s_rend[i])) SmrOffSet(s_rend[i], s_smrWas[i]);
        if (Ok(s_anim)) {
            if (fnBehSet) BehSet(s_anim, s_anWasOn);
            if (fnAnCullSet) IntSet(fnAnCullSet, s_anim, s_anWasCull);
            if (fnAnSpeedSet) FloatSet(fnAnSpeedSet, s_anim, s_anWasSpeed);
        }
        s_anim = nullptr;
        for (int i = 0; i < s_nAct; i++) if (Ok(s_actGo[i])) GoSetActive(s_actGo[i], false);
        s_nAct = 0;
        if (fnBehSet && Ok(s_gunCam) && s_gunWas) BehSet(s_gunCam, true);
    }

    // End of the frame (local weapon's LateUpdate): draw the character and move the camera back.
    static void TpLate() {
        if (!cfg.misc.third_person || cfg.panic || farm::SuppressFeatures() || !s_hasLocal || !s_local.alive) { custommodel::Hide(); if (s_tpActive) TpRestore(); return; }
        if (s_tpOffset || !fnSetPos || !fnRendGet || !fnRendSet) return;
        void* obj = s_local.obj;
        void* cam = CamTransform();
        if (!Ok(obj) || !Ok(cam)) return;
        if (s_tpActive && s_tpObj != obj) { s_tpActive = false; s_nAct = 0; } // respawned
        const bool first = !s_tpActive;
        static int s_tpLog = 0;
        static int s_diagN = 0;
        static DWORD s_diagT = 0;
        const bool diag = s_diagN < 12 && GetTickCount() - s_diagT > 2000;
        if (diag) { s_diagT = GetTickCount(); s_diagN++; }
        const Vec3 camNow = reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(cam, nullptr);
        void* stopGo = fnGetGO ? GoOf(obj) : nullptr;
        int addMask = 0;
        for (int i = 0; i < 4; i++) {
            void* r = offRend[i] > 0 ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + offRend[i]) : nullptr;
            if (!Ok(r)) { if (first && s_tpLog < 4) Log("[tp] renderer %d is null", i); continue; }
            if (first) { s_rend[i] = r; s_rendWas[i] = RendGet(r); s_smrWas[i] = fnSmrOff ? SmrOff(r) : false; }
            if (!RendGet(r)) RendSet(r, true);
            if (fnRendForce) RendForceOff(r, false);
            // a skinned mesh whose bounds do not follow its bones (the arms) is frustum-culled while it is in plain view
            if (fnSmrOff && fnSmrOffSet && !SmrOff(r)) SmrOffSet(r, true);
            // the body can also be switched off at the object level or sit on a layer the camera skips
            void* go = fnGetGO ? GoOf(r) : nullptr;
            int layer = -1;
            bool active = true, activeNow = true;
            if (Ok(go)) {
                if (fnGoActive && fnGoSetActive) { active = GoActive(go); if (!active) ActivateChain(go, stopGo); activeNow = GoActive(go); }
                if (fnGoLayer) { layer = GoLayer(go); if (layer >= 0 && layer < 32) addMask |= 1 << layer; }
            }
            if (first && s_tpLog < 4) {
                std::string cn;
                il2::ClassName(r, cn);
                Log("[tp] renderer %d %s: was enabled=%d, object active=%d->%d, layer=%d, seen by a camera=%d", i, cn.c_str(),
                    s_rendWas[i], active, activeNow, layer, fnRendVis ? RendSeen(r) : -1);
            }
            if (diag) {
                void* rtr = fnGetTr ? reinterpret_cast<void*(*)(void*, void*)>(fnGetTr)(r, nullptr) : nullptr;
                const Vec3 rp = Ok(rtr) ? reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(rtr, nullptr) : Vec3{};
                const Vec3 sc = Ok(rtr) && fnLossy ? ScaleOf(rtr) : Vec3{};
                const Bnds b = fnBounds ? BoundsOf(r) : Bnds{};
                std::string shader = "?";
                if (fnSharedMat && fnMatShader && fnObjName) {
                    void* mat = PtrCall(fnSharedMat, r);
                    if (Ok(mat)) shader = il2::Str(ObjName(PtrCall(fnMatShader, mat)));
                }
                Log("[tp] r%d '%s' active=%d enabled=%d seen=%d  %.1fm from the first-person camera, scale %.2f/%.2f/%.2f, %d objects switched on; mesh=%p, bounds %.1fm away, size %.1f/%.1f/%.1f, shader '%s'",
                    i, fnObjName && Ok(go) ? il2::Str(ObjName(go)).c_str() : "?", activeNow, RendGet(r), fnRendVis ? RendSeen(r) : -1,
                    Dist(rp, camNow), sc.x, sc.y, sc.z, s_nAct, fnSmrMesh ? SmrMesh(r) : nullptr, Dist(b.c, camNow), b.e.x * 2, b.e.y * 2, b.e.z * 2,
                    shader.c_str());
            }
        }
        const bool custom = custommodel::Update(s_local, Camera());
        if (custom) {
            for (void* r : s_rend) if (Ok(r)) RendSet(r, false);
        } else {
            if (first || (diag && !Ok(s_anim))) TpFindAnimator(obj);
            TpAnimate();
        }
        if (fnCamGetMask && fnCamSetMask && addMask) {
            void* mc = Camera();
            if (Ok(mc)) {
                if (!s_maskSaved) { s_maskCam = mc; s_maskOrig = CamMask(mc); s_maskSaved = true; }
                CamMaskSet(mc, s_maskOrig | addMask);
                if (first && s_tpLog < 4) Log("[tp] camera culling mask %08X -> %08X", static_cast<unsigned>(s_maskOrig), static_cast<unsigned>(s_maskOrig | addMask));
            }
        }
        if (first) s_tpLog++;
        void* gc = offGunCam > 0 ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(obj) + offGunCam) : nullptr;
        if (fnBehSet && fnEnabled && Ok(gc)) {
            if (first) { s_gunCam = gc; s_gunWas = BehGet(gc); }
            if (BehGet(gc)) BehSet(gc, false);
        }
        s_tpActive = true;
        s_tpObj = obj;
        const Vec3 p0 = camNow;
        const Vec3 f = Forward(GetRotation(cam));
        const float d = std::max(1.f, std::min(cfg.misc.tp_dist, 10.f));
        s_tpCam = cam;
        s_tpPos = p0;
        // pull the camera in when a wall is behind you, so it does not end up inside the geometry
        Vec3 want{};
        for (const float k : { 1.f, 0.7f, 0.45f, 0.3f }) {
            want = { p0.x - f.x * d * k, p0.y - f.y * d * k + cfg.misc.tp_height, p0.z - f.z * d * k };
            Vec3 a = p0, b = want;
            const bool blocked = fnLinecast && reinterpret_cast<bool(*)(Vec3*, Vec3*, int, void*)>(fnLinecast)(&a, &b, -5, nullptr);
            if (!blocked) break;
        }
        PosSet(cam, want);
        s_tpOffset = true;
    }

    // Right before the main camera culls: the last moment the game's own scripts could have switched a
    // body part back off after TpLate ran. The arms are the part that never showed.
    static int s_preFix[4]{};
    static void TpPreCull(void* camObj) {
        if (!s_tpActive || !cfg.misc.third_person || farm::SuppressFeatures() || !s_hasLocal || camObj != Camera()) return;
        void* stopGo = fnGetGO ? GoOf(s_local.obj) : nullptr;
        if (custommodel::Update(s_local, camObj)) {
            for (void* r : s_rend) if (Ok(r)) RendSet(r, false);
            return;
        }
        for (int i = 0; i < 4; i++) {
            void* r = s_rend[i];
            if (!Ok(r)) continue;
            bool fixedUp = false;
            if (!RendGet(r)) { RendSet(r, true); fixedUp = true; }
            void* go = fnGetGO ? GoOf(r) : nullptr;
            if (Ok(go) && fnGoActive && !GoActive(go)) { ActivateChain(go, stopGo); fixedUp = true; }
            if (fixedUp && s_preFix[i]++ < 4) Log("[tp] the game switched renderer %d back off before the camera culled; switched on again", i);
        }
    }
#endif

#ifdef CORE_BUILD
    using FireDelayFn = float(__fastcall*)(void*, void*);
    static FireDelayFn originalFireDelay = nullptr;
    static std::atomic<bool> rapidReady{false};
    static std::atomic<unsigned> delayCalls{0}, localDelayCalls{0}, reducedDelayCalls{0};
    static std::atomic<float> lastNormalDelay{0.f}, lastReturnedDelay{0.f};
    bool RapidFireReady() { return rapidReady; }
    static bool LocalRapidWeapon(void* weapon) {
        return s_hasLocal && offOwner >= 0 && Ok(weapon) &&
            !IsBadReadPtr(static_cast<uint8_t*>(weapon) + offOwner, sizeof(void*)) &&
            *reinterpret_cast<void**>(static_cast<uint8_t*>(weapon) + offOwner) == s_local.obj;
    }
    static float __fastcall hkFireDelay(void* weapon, void* method) {
        const float normal = originalFireDelay(weapon, method);
        ++delayCalls;
        if (!LocalRapidWeapon(weapon)) return normal;
        ++localDelayCalls;
        lastNormalDelay = normal;
        float result = normal;
        if (cfg.misc.rapid_fire && !cfg.panic && !render::MenuOpen &&
            !farm::SuppressFeatures() && s_local.alive && GetForegroundWindow() == render::Hwnd &&
            std::isfinite(normal) && normal > 0.f && normal <= 60.f) {
            result = std::min(normal, 0.005f);
            if (result < normal) ++reducedDelayCalls;
        }
        lastReturnedDelay = result;
        return result;
    }
    static void InitRapidFire() {
        // Static analysis for this build traced this WeaponSounds float getter
        // into Player_move_c's next-shot deadline (including its shot routine).
        // Verify runtime metadata and bytes before installing; no guessed writes.
        constexpr uintptr_t verifiedRva = 0x87DC80;
        const uint8_t prefix[] = {0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,0xEC,0x40};
        const auto base = ModuleBase(L"GameAssembly.dll");
        void* target = reinterpret_cast<void*>(base + verifiedRva);
        auto returnType = reinterpret_cast<void*(*)(void*)>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_method_get_return_type"));
        auto methodFlags = reinterpret_cast<uint32_t(*)(void*, uint32_t*)>(GetProcAddress(GetModuleHandleW(L"GameAssembly.dll"), "il2cpp_method_get_flags"));
        if (!kWeapon || !returnType || !methodFlags || !InModule(reinterpret_cast<uintptr_t>(target), L"GameAssembly.dll") ||
            memcmp(target, prefix, sizeof(prefix))) {
            Log("[rapid] effective cooldown hook unavailable: build signature mismatch"); return;
        }
        void* iterator = nullptr;
        while (void* method = il2::api.class_get_methods(kWeapon, &iterator)) {
            void* pointer = il2::api.method_get_pointer ? il2::api.method_get_pointer(method) : *reinterpret_cast<void**>(method);
            if (pointer != target || il2::api.method_get_param_count(method) != 0) continue;
            uint32_t implementation = 0;
            if (methodFlags(method, &implementation) & 16) continue;
            void* type = il2::api.class_from_type(returnType(method));
            if (!type || strcmp(il2::api.class_get_name(type), "Single")) continue;
            const auto created = MH_CreateHook(target, &hkFireDelay, reinterpret_cast<void**>(&originalFireDelay));
            const auto enabled = created == MH_OK ? MH_EnableHook(target) : MH_ERROR_NOT_CREATED;
            rapidReady = created == MH_OK && enabled == MH_OK;
            Log("[rapid] effective cooldown getter=%s rva=%llX create=%d enable=%d ready=%d",
                il2::api.method_get_name(method), static_cast<unsigned long long>(verifiedRva), created, enabled, rapidReady.load());
            return;
        }
        Log("[rapid] effective cooldown hook unavailable: runtime method signature not verified");
    }
    static void LogRapidFire() {
        static DWORD lastLog = 0;
        static bool wasEnabled = false;
        const bool enabled = cfg.misc.rapid_fire;
        const DWORD now = GetTickCount();
        if (enabled == wasEnabled && (!enabled || now - lastLog < 3000)) return;
        wasEnabled = enabled; lastLog = now;
        Log("[rapid] enabled=%d ready=%d getterCalls=%u localCalls=%u reduced=%u lastDelay=%.6f->%.6f focus=%d menu=%d panic=%d farmPaused=%d alive=%d ownerOffset=%d",
            enabled, rapidReady.load(), delayCalls.load(), localDelayCalls.load(), reducedDelayCalls.load(),
            lastNormalDelay.load(), lastReturnedDelay.load(), GetForegroundWindow() == render::Hwnd,
            render::MenuOpen, cfg.panic, farm::SuppressFeatures(), s_local.alive, offOwner);
    }
#endif

    static void __fastcall hkUpdate(void* self, void*) {
        if (self) Touch(s_objs, self, 256);
#ifdef CORE_BUILD
        const bool wrap = s_ready && s_hasLocal && self == s_local.obj;
        if (wrap) { TpEarly(); rage::WrapBegin(self, false); }
#endif
        if (oUpdate) reinterpret_cast<void(__fastcall*)(void*, void*)>(oUpdate)(self, nullptr);
#ifdef CORE_BUILD
        if (wrap) rage::WrapEnd();
#endif
        if (!s_ready) return;
#ifdef CORE_BUILD
        // once per frame: the local player's own Update drives the aimbot and auto-fire
        if (s_hasLocal && self == s_local.obj) rage::Step();
#endif
        const DWORD now = GetTickCount();
        if (now - s_tickMs > 8) { // ~once per frame; 33 ms made the ESP lag behind the camera
            s_tickMs = now;
            Tick();
#ifdef CORE_BUILD
            misc::Tick();
            LogRapidFire();
            probe::OnTick();
#endif
        }
    }

    static void __fastcall hkLateUpdate(void* self, void*) {
        if (oLateUpdate) reinterpret_cast<void(__fastcall*)(void*, void*)>(oLateUpdate)(self, nullptr);
    }

    static void __fastcall hkWeaponUpdate(void* self, void*) {
        if (self) Touch(s_weapons, self, 64);
#ifdef CORE_BUILD
        custommodel::ServiceShutdown();
        const bool wrap = s_ready && s_hasLocal;
        if (wrap) rage::WrapBegin(self, true);
#endif
        if (oWeaponUpdate) reinterpret_cast<void(__fastcall*)(void*, void*)>(oWeaponUpdate)(self, nullptr);
#ifdef CORE_BUILD
        if (wrap) rage::WrapEnd();
#endif
    }

#ifdef CORE_BUILD
    // Scope / ADS zoom is the game's own FOV change; never overwrite it.
    static bool Scoped(float requested) {
        if (requested < 45.f) return true;
        void* w = LocalWeapon();
        return Ok(w) && offZoom > 0 && *(reinterpret_cast<uint8_t*>(w) + offZoom) == 1;
    }

    // The game sets the camera FOV itself, so writing it from our own tick fought it frame
    // by frame (flicker, and a violent zoom whenever a scope opened). Substitute the value
    // at the one place the game writes it, for the main camera only, and only unscoped.
    static void __fastcall hkSetFov(void* cam, float v, void* mi) {
        s_gameFov = v;
        if (cfg.misc.fov_changer && !farm::SuppressFeatures() && cam == Camera() && !Scoped(v)) v = cfg.misc.fov_value;
        reinterpret_cast<void(__fastcall*)(void*, float, void*)>(oSetFov)(cam, v, mi);
    }
#endif

#ifdef CORE_BUILD
    static void __fastcall hkUiUpdate(void* self, void* mi) {
        // Dispatch after NGUI has finished its input processing, never from a
        // camera callback: respawn/replay can destroy cameras and scene objects.
        if (oUiUpdate) reinterpret_cast<void(__fastcall*)(void*, void*)>(oUiUpdate)(self, mi);
        if (s_ready) farm::Tick();
    }

    static void __fastcall hkPreCull(void* cam, void* mi) {
        custommodel::ServiceShutdown();
        if (!s_hasLocal || !cfg.misc.third_person || cfg.panic) custommodel::Hide();
        if (oPreCull) reinterpret_cast<void(__fastcall*)(void*, void*)>(oPreCull)(cam, mi);
        TpPreCull(cam);
    }

    static void __fastcall hkWeaponLate(void* self, void*) {
        if (oWeaponLate) reinterpret_cast<void(__fastcall*)(void*, void*)>(oWeaponLate)(self, nullptr);
        // the local weapon's LateUpdate runs once per frame after every Update: the last word before render
        if (s_ready && s_hasLocal && (cfg.misc.third_person || s_tpActive) && self == LocalWeapon()) TpLate();
    }
#endif

    bool Init() {
        if (!il2::Init()) return false;

        kPlayer = il2::Class("Player_move_c");
        kWeapon = il2::Class("WeaponSounds");
        kCamera    = il2::Class("Camera",    "UnityEngine");
        kTransform = il2::Class("Transform", "UnityEngine");
        kTextMesh  = il2::Class("TextMesh",  "UnityEngine");
        kDmg = il2::Class("PlayerDamageable");
        Log("[game] classes player=%p weapon=%p camera=%p", kPlayer, kWeapon, kCamera);
        if (!kPlayer || !kCamera || !kTransform) { Log("[game] core classes missing"); return false; }

        offNick = il2::FieldOffset(kPlayer, "nickLabel");
        offTr = il2::FieldOffset(kPlayer, "myPlayerTransform");
        offHead = il2::FieldOffset(kPlayer, "headCollider");
        offDmg = il2::FieldOffset(kPlayer, "playerDamageable");
        offTeam = il2::FieldOffsetAny(kPlayer, { "team", "myTeam", "teamId", "commandId", "teamNumber" });
        if (offTr < 0) offTr = offsets::Field("myPlayerTransform");
        if (offNick < 0) offNick = offsets::Field("nickLabel");
        if (offHead < 0) offHead = offsets::Field("headCollider");
        if (offDmg < 0) offDmg = offsets::Field("playerDamageable");
        Log("[game] player offs nick=%d tr=%d head=%d dmg=%d team=%d", offNick, offTr, offHead, offDmg, offTeam);

        fnW2S = il2::MethodPtr(kCamera, "WorldToScreenPoint", 2); // 2-arg: (Vector3, Eye); call site passes eye=2
        if (!fnW2S) fnW2S = offsets::Fn("world_to_screen");
        fnCamMain = il2::MethodPtr(kCamera, "get_main", 0);
        if (!fnCamMain) fnCamMain = offsets::Fn("camera_get_main");
        fnGetTr = il2::MethodPtr(il2::Class("Component", "UnityEngine"), "get_transform", 0);
        if (!fnGetTr) fnGetTr = offsets::Fn("get_transform");
        fnGetPos = il2::MethodPtr(kTransform, "get_position", 0);
        if (!fnGetPos) fnGetPos = offsets::Fn("get_position");
        fnGetRot = il2::MethodPtr(kTransform, "get_rotation", 0);
        if (!fnGetRot) fnGetRot = offsets::Fn("get_rotation");
        fnSetRot = il2::MethodPtr(kTransform, "set_rotation", 1);
        if (!fnSetRot) fnSetRot = offsets::Fn("set_rotation");
        fnLookAt = il2::MethodPtr(kTransform, "LookAt", 1);
        if (!fnLookAt) fnLookAt = offsets::Fn("look_at");
        // This Unity build declares Linecast with default arguments, so there is no
        // 2-arg overload and the 3-arg one is ambiguous: (Vector3,Vector3,int layerMask)
        // and (Vector3,Vector3,out RaycastHit) share an arg count. Match on types.
        void* kPhys = il2::Class("Physics", "UnityEngine");
        fnLinecast = il2::MethodBySig(kPhys, "Linecast", { "Vector3", "Vector3", "Int32" });
        Log("[game] physics=%p linecast=%p", kPhys, fnLinecast);
        fnGetText = il2::MethodPtr(kTextMesh, "get_text", 0);
        if (!fnGetText) fnGetText = offsets::Fn("text_get_text");
        fnGetColor = il2::MethodPtr(kTextMesh, "get_color", 0);
        if (!fnGetColor) fnGetColor = offsets::Fn("text_get_color");
        fnEnabled = il2::MethodPtr(il2::Class("Behaviour", "UnityEngine"), "get_enabled", 0);
        if (!fnEnabled) fnEnabled = offsets::Fn("behaviour_enabled");
        fnSetFov = il2::MethodPtr(kCamera, "set_fieldOfView", 1);
        if (!fnSetFov) fnSetFov = offsets::Fn("set_fov");
        fnGetFov = il2::MethodPtr(kCamera, "get_fieldOfView", 0);
        fnIsDead = kDmg ? il2::MethodPtr(kDmg, "IsDead", 0) : nullptr;
        Log("[game] fov set=%p get=%p  isDead=%p", fnSetFov, fnGetFov, fnIsDead);
        fnSetPos = il2::MethodPtr(kTransform, "set_position", 1);
        if (void* kRend = il2::Class("Renderer", "UnityEngine")) {
            fnRendGet = il2::MethodPtr(kRend, "get_enabled", 0);
            fnRendSet = il2::MethodPtr(kRend, "set_enabled", 1);
        }
        fnBehSet = il2::MethodPtr(il2::Class("Behaviour", "UnityEngine"), "set_enabled", 1);
        fnGetGO = il2::MethodPtr(il2::Class("Component", "UnityEngine"), "get_gameObject", 0);
        if (void* kGo = il2::Class("GameObject", "UnityEngine")) {
            fnGoActive = il2::MethodPtr(kGo, "get_activeInHierarchy", 0);
            fnGoSetActive = il2::MethodPtr(kGo, "SetActive", 1);
            fnGoLayer = il2::MethodPtr(kGo, "get_layer", 0);
        }
        fnCamGetMask = il2::MethodPtr(kCamera, "get_cullingMask", 0);
        fnCamSetMask = il2::MethodPtr(kCamera, "set_cullingMask", 1);
        if (void* kR2 = il2::Class("Renderer", "UnityEngine")) {
            fnRendVis = il2::MethodPtr(kR2, "get_isVisible", 0);
            fnRendForce = il2::MethodPtr(kR2, "set_forceRenderingOff", 1);
            fnBounds = il2::MethodPtr(kR2, "get_bounds", 0);
        }
        if (void* kSmr = il2::Class("SkinnedMeshRenderer", "UnityEngine")) {
            fnSmrOff = il2::MethodPtr(kSmr, "get_updateWhenOffscreen", 0);
            fnSmrOffSet = il2::MethodPtr(kSmr, "set_updateWhenOffscreen", 1);
            fnSmrMesh = il2::MethodPtr(kSmr, "get_sharedMesh", 0);
        }
        if (void* kAn = il2::Class("Animator", "UnityEngine")) {
            fnAnCull = il2::MethodPtr(kAn, "get_cullingMode", 0);
            fnAnCullSet = il2::MethodPtr(kAn, "set_cullingMode", 1);
            fnAnCtl = il2::MethodPtr(kAn, "get_runtimeAnimatorController", 0);
            fnAnCtlSet = il2::MethodPtr(kAn, "set_runtimeAnimatorController", 1);
            fnAnSpeed = il2::MethodPtr(kAn, "get_speed", 0);
            fnAnSpeedSet = il2::MethodPtr(kAn, "set_speed", 1);
            fnAnParCount = il2::MethodPtr(kAn, "get_parameterCount", 0);
            fnAnPar = il2::MethodPtr(kAn, "GetParameter", 1);
            fnAnSetFloat = il2::MethodBySig(kAn, "SetFloat", { "Int32", "Single" });
            fnAnSetBool = il2::MethodBySig(kAn, "SetBool", { "Int32", "Boolean" });
            fnStrHash = il2::MethodPtr(kAn, "StringToHash", 1);
            fnAnParams = il2::MethodPtr(kAn, "get_parameters", 0);
        }
        if (void* kObj = il2::Class("Object", "UnityEngine")) {
            fnFindObjs2 = il2::MethodBySig(kObj, "FindObjectsOfType", { "Type", "Boolean" });
            fnFindObjs1 = il2::MethodBySig(kObj, "FindObjectsOfType", { "Type" });
        }
        fnIsChildOf = il2::MethodPtr(kTransform, "IsChildOf", 1);
        if (void* kR3 = il2::Class("Renderer", "UnityEngine")) fnSharedMat = il2::MethodPtr(kR3, "get_sharedMaterial", 0);
        if (void* kMat = il2::Class("Material", "UnityEngine")) fnMatShader = il2::MethodPtr(kMat, "get_shader", 0);
        s_wrapType = il2::TypeObject(il2::Class("PlayerAnimationWrapper"));
        Log("[game] wrapper lookup: findObjects=%p/%p isChildOf=%p type=%p shader=%p/%p", fnFindObjs1, fnFindObjs2, fnIsChildOf, s_wrapType,
            fnSharedMat, fnMatShader);
        if (void* kPar = il2::Class("AnimatorControllerParameter", "UnityEngine")) {
            fnParName = il2::MethodPtr(kPar, "get_name", 0);
            fnParType = il2::MethodPtr(kPar, "get_type", 0);
            fnParHash = il2::MethodPtr(kPar, "get_nameHash", 0);
        }
        if (void* kWrap = il2::Class("PlayerAnimationWrapper")) {
            offWrapAnim = il2::FieldOffset(kWrap, "animator");
            offWrapCtl = il2::FieldOffset(kWrap, "defaultAnimatorController");
        }
        Log("[game] third person animation: updateOffscreen=%p bounds=%p cull=%p ctl=%p setFloat=%p setBool=%p params=%p/%p wrapper animator=%d ctl=%d",
            fnSmrOffSet, fnBounds, fnAnCullSet, fnAnCtlSet, fnAnSetFloat, fnAnSetBool, fnAnPar, fnParHash ? fnParHash : fnStrHash, offWrapAnim, offWrapCtl);
        if (void* kGo2 = il2::Class("GameObject", "UnityEngine")) {
            fnGoSelf = il2::MethodPtr(kGo2, "get_activeSelf", 0);
            fnGoTr = il2::MethodPtr(kGo2, "get_transform", 0);
        }
        fnTrParent = il2::MethodPtr(kTransform, "get_parent", 0);
        fnLossy = il2::MethodPtr(kTransform, "get_lossyScale", 0);
        fnObjName = il2::MethodPtr(il2::Class("Object", "UnityEngine"), "get_name", 0);
        Log("[game] third person chain self=%p goTr=%p parent=%p force=%p name=%p scale=%p", fnGoSelf, fnGoTr, fnTrParent,
            fnRendForce, fnObjName, fnLossy);
        Log("[game] third person extras goGet=%p active=%p setActive=%p layer=%p mask=%p/%p vis=%p", fnGetGO, fnGoActive,
            fnGoSetActive, fnGoLayer, fnCamGetMask, fnCamSetMask, fnRendVis);
        {
            const char* rn[4] = { "playerBodyRenderer", "playerHeadRenderer", "playerArmsRenderer", "playerLegsRenderer" };
            for (int i = 0; i < 4; i++) offRend[i] = il2::FieldOffset(kPlayer, rn[i]);
            offGunCam = il2::FieldOffset(kPlayer, "gunCamera");
        }
        Log("[game] third person setPos=%p rendGet=%p rendSet=%p behSet=%p rend=%d/%d/%d/%d gunCam=%d", fnSetPos, fnRendGet,
            fnRendSet, fnBehSet, offRend[0], offRend[1], offRend[2], offRend[3], offGunCam);

        if (kWeapon) {
            offOwner = il2::FieldOffset(kWeapon, "player_move_c");
            if (offOwner < 0) offOwner = offsets::Field("weaponSoundsPlayerMoveC");
            offAmmo = il2::FieldOffsetAny(kWeapon, { "ammo", "ammoInClip", "inClip", "bulletsLeft" });
            offRecoil = il2::FieldOffsetAny(kWeapon, { "recoil", "recoilStrength", "recoilAmount" });
            offSpread = il2::FieldOffsetAny(kWeapon, { "spread", "spreadMin", "spreadMax" });
            offDelay = il2::FieldOffsetAny(kWeapon, { "bulletDelay", "delay", "fireRate" });
            offAuto = il2::FieldOffsetAny(kWeapon, { "isAutomatic", "automatic", "fullAuto" });
            offPen = il2::FieldOffsetAny(kWeapon, { "penetration", "isPenetrating", "canPenetrate",
                                                    "bulletPenetration", "penetrationPower", "wallBang" });
            offZoom = il2::FieldOffset(kWeapon, "isZooming");
            if (offDelay < 0) offDelay = offsets::Field("bulletDelay");
            Log("[game] weapon offs owner=%d ammo=%d recoil=%d spread=%d delay=%d auto=%d pen=%d",
                offOwner, offAmmo, offRecoil, offSpread, offDelay, offAuto, offPen);
        }

        const void* upd = il2::MethodPtr(kPlayer, "Update", 0);
        const void* late = il2::MethodPtr(kPlayer, "LateUpdate", 0);
        const void* wupd = kWeapon ? il2::MethodPtr(kWeapon, "Update", 0) : nullptr;
        const MH_STATUS mhs = MH_Initialize();
        if (mhs == MH_OK || mhs == MH_ERROR_ALREADY_INITIALIZED) {
#ifdef CORE_BUILD
            InitRapidFire();
#endif
            if (upd) MH_CreateHook(const_cast<void*>(upd), &hkUpdate, &oUpdate);
            if (late) MH_CreateHook(const_cast<void*>(late), &hkLateUpdate, &oLateUpdate);
            if (wupd) MH_CreateHook(const_cast<void*>(wupd), &hkWeaponUpdate, &oWeaponUpdate);
#ifdef CORE_BUILD
            if (fnSetFov && MH_CreateHook(fnSetFov, &hkSetFov, &oSetFov) != MH_OK) oSetFov = nullptr;
            if (void* wl = kWeapon ? const_cast<void*>(il2::MethodPtr(kWeapon, "LateUpdate", 0)) : nullptr)
                if (MH_CreateHook(wl, &hkWeaponLate, &oWeaponLate) != MH_OK) oWeaponLate = nullptr;
            {
                void* pc = il2::MethodPtr(kCamera, "FireOnPreCull", 1);
                const MH_STATUS ps = pc ? MH_CreateHook(pc, &hkPreCull, &oPreCull) : MH_ERROR_NOT_CREATED;
                if (ps != MH_OK) oPreCull = nullptr;
                Log("[game] Camera.FireOnPreCull hook=%p create=%d", pc, ps);
            }
#endif
            MH_EnableHook(MH_ALL_HOOKS);
            s_hooked = true;
            Log("[game] hooks upd=%p late=%p weapon=%p", upd, late, wupd);
#ifdef CORE_BUILD
            probe::Init();
            rage::Init();
            hitsound::Init();
            custommodel::Init();
            // Fail closed if the UI update driver cannot be installed. Camera
            // render callbacks must not be used as a fallback for UI mutations.
            void* uiUpdate = il2::MethodPtr(il2::Class("UICamera"), "Update", 0);
            MH_STATUS uiStatus = uiUpdate ? MH_CreateHook(uiUpdate, &hkUiUpdate, &oUiUpdate) : MH_ERROR_NOT_CREATED;
            if (uiStatus == MH_OK) uiStatus = MH_EnableHook(uiUpdate);
            Log("[farm] UICamera.Update driver=%p status=%d", uiUpdate, uiStatus);
            if (uiStatus == MH_OK) farm::Init();
            else Log("[farm] disabled: safe UI update driver unavailable");
            render::InstallGameInputBlock();
#endif
        }

        s_ready = true;
        return true;
    }

    bool Ready() { return s_ready; }

    void Shutdown() {
#ifdef CORE_BUILD
        farm::Stop();
        for (int attempt = 0; attempt < 60 && farm::Active(); ++attempt) Sleep(10);
        farm::Shutdown();
        // Put the camera and the character's renderers back through the game thread (the hooks
        // are still live) before anything is unhooked; otherwise the camera could stay behind you.
        cfg.misc.third_person = false;
        custommodel::RequestShutdown();
        for (int i = 0; i < 60 && (s_tpOffset || s_tpActive); i++) Sleep(10);
        for (int i = 0; i < 300 && !custommodel::ShutdownDone(); ++i) Sleep(10);
        rage::Release();
        hitsound::Shutdown();
#endif
        s_ready = false;
#ifdef CORE_BUILD
        probe::Shutdown();
#endif
        if (s_hooked) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_RemoveHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            s_hooked = false;
        }
    }

    // No headCollider -> assume a 1.6u span from the root. Only used when the measurement
    // is unavailable; everything else rides on the real collider position.
    static constexpr float kFallbackSpan = 1.6f;

    Vec3 Bone(const Player& p, float frac) {
        const float tx = p.haveHead ? p.headPos.x : p.pos.x;
        const float tz = p.haveHead ? p.headPos.z : p.pos.z;
        const float ty = p.haveHead ? p.headPos.y : p.pos.y + kFallbackSpan;
        return { p.pos.x + (tx - p.pos.x) * frac,
                 p.pos.y + (ty - p.pos.y) * frac,
                 p.pos.z + (tz - p.pos.z) * frac };
    }

    bool HeadTracked() { return s_headTracked; }

    // False when the live read can't be trusted (dead or despawning players report a head
    // that is no longer above the root); callers must not aim at it then.
    bool Refresh(Player& p) {
        if (!Ok(p.tr) || !fnGetPos) return false;
        p.pos = reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(p.tr, nullptr);
        if (!std::isfinite(p.pos.x + p.pos.y + p.pos.z)) return false;
        p.haveHead = false;
        if (Ok(p.head) && fnGetTr) {
            void* htr = reinterpret_cast<void*(*)(void*, void*)>(fnGetTr)(p.head, nullptr);
            if (Ok(htr)) {
                const Vec3 hp = reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(htr, nullptr);
                const float span = hp.y - p.pos.y;
                if (std::isfinite(span) && span > 0.3f && span < 4.f) { p.headPos = hp; p.haveHead = true; }
            }
        }
        return p.haveHead || offHead <= 0;
    }

    // Game thread only: Unity physics queries are not safe from Present.
    static bool Visible(const Vec3& from, const Vec3& to) {
        if (!fnLinecast) return true; // no wall check available -> never gate a feature on it
        Vec3 d{ to.x - from.x, to.y - from.y, to.z - from.z };
        const float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if (len < 1.2f) return true;
        const float inv = 1.f / len;
        d.x *= inv; d.y *= inv; d.z *= inv;
        // start past our own hull, stop short of the target's own collider
        Vec3 a{ from.x + d.x * 0.45f, from.y + d.y * 0.45f, from.z + d.z * 0.45f };
        Vec3 b{ to.x - d.x * 0.55f,  to.y - d.y * 0.55f,  to.z - d.z * 0.55f };
        // -5 is Unity's DefaultRaycastLayers (everything but Ignore Raycast). The world
        // layer is unknown, so other players can block a line; the end trims cover that.
        return !reinterpret_cast<bool(*)(Vec3*, Vec3*, int, void*)>(fnLinecast)(&a, &b, -5, nullptr);
    }

    int WeaponPenetrates() {
        if (offPen < 0) return -1;
        void* w = LocalWeapon();
        if (!Ok(w)) return -1;
        // the candidate names cover both a 1-byte bool flag and a float strength
        const uint8_t b = *(reinterpret_cast<uint8_t*>(w) + offPen);
        const float f = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(w) + offPen);
        if (b == 1) return 1;
        return (std::isfinite(f) && f > 0.001f && f < 1000.f) ? 1 : 0;
    }

    void Tick() {
        if (!s_ready) return;
        void* cam = Camera();
        if (!Ok(cam)) return;
        const Vec3 camPos = CamPos();
        s_camPos = camPos;

        // Player_move_c.Update is obfuscated so the hook can't install by name.
        // Auto-discover the WeaponSounds→Player_move_c back-ref once we have a live weapon.
        // Retry until found: a single attempt at start-up ran before any weapon was live and
        // never recovered, which left LocalWeapon() null for the whole session.
        static DWORD s_ownerTry = 0;
        if (offOwner < 0 && GetTickCount() - s_ownerTry > 1500 && kPlayer && il2::api.object_get_class) {
            s_ownerTry = GetTickCount();
            std::vector<void*> ws = Live(s_weapons);
            for (void* w : ws) {
                if (!Ok(w)) continue;
                for (int off = 0x20; off + 8 <= 0x900; off += 8) {
                    void* cand = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(w) + off);
                    if (!Ok(cand)) continue;
                    if (il2::api.object_get_class(cand) == kPlayer) {
                        offOwner = off;
                        Log("[game] auto-discovered offOwner=%d (0x%X)", off, off);
                        break;
                    }
                }
                if (offOwner >= 0) break;
            }
        }

        std::vector<void*> objs = Live(s_objs);

        // Fall back to weapon owners when the Update hook couldn't install
        if (objs.empty() && offOwner >= 0) {
            std::vector<void*> ws = Live(s_weapons);
            for (void* w : ws) {
                if (!Ok(w)) continue;
                void* pl = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(w) + offOwner);
                if (!Ok(pl)) continue;
                bool dup = false;
                for (void* o : objs) if (o == pl) { dup = true; break; }
                if (!dup) objs.push_back(pl);
            }
        }

        // PlayerDamageable field on Player_move_c is obfuscated; scan for it once
        static bool s_dmgSearched = false;
        if (offDmg < 0 && !s_dmgSearched && kDmg && il2::api.object_get_class && !objs.empty()) {
            s_dmgSearched = true;
            for (void* o : objs) {
                if (!Ok(o)) continue;
                for (int off = 0x20; off + 8 <= 0x1800; off += 8) {
                    void* cand = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(o) + off);
                    if (!Ok(cand)) continue;
                    if (il2::api.object_get_class(cand) == kDmg) {
                        offDmg = off;
                        offHp  = 0x20; // first field in PlayerDamageable = currentHealth
                        Log("[game] auto-discovered offDmg=%d (0x%X) offHp=0x20", off, off);
                        break;
                    }
                }
                if (offDmg >= 0) break;
            }
        }

        std::vector<Player> out;
        out.reserve(objs.size());
        float bestLocal = 1e9f;
        Player local{};

        for (void* o : objs) {
            if (!Ok(o)) continue;
            Player p;
            p.obj = o;
            p.tr = offTr > 0 ? *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(o) + offTr) : nullptr;
            if (!Ok(p.tr)) continue;
            p.pos = reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(p.tr, nullptr);
            if (offHead > 0) p.head = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(o) + offHead);
            // Measure the head instead of guessing a height: this is what keeps the ESP
            // centred through crouching, different models and map scale.
            if (Ok(p.head) && fnGetTr && fnGetPos) {
                void* htr = reinterpret_cast<void*(*)(void*, void*)>(fnGetTr)(p.head, nullptr);
                if (Ok(htr)) {
                    const Vec3 hp = reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(htr, nullptr);
                    const float span = hp.y - p.pos.y;
                    if (std::isfinite(span) && span > 0.3f && span < 4.f) {
                        p.headPos = hp;
                        p.haveHead = true;
                        s_headTracked = true;
                    }
                }
            }
            if (fnEnabled) p.alive = reinterpret_cast<bool(*)(void*, void*)>(fnEnabled)(o, nullptr);
            p.vis = Visible(camPos, Bone(p, 0.82f)); // chest
            if (offDmg > 0) {
                void* dmg = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(o) + offDmg);
                if (Ok(dmg)) {
                    p.dmg = dmg;
                    // the player object stays enabled after a kill, so ask the game
                    if (fnIsDead && reinterpret_cast<bool(*)(void*, void*)>(fnIsDead)(dmg, nullptr)) p.alive = false;
                    if (offHp <= 0) {
                        std::string cn;
                        if (il2::ClassName(dmg, cn)) {
                            if (void* dk = il2::Class(cn.c_str()))
                                offHp = il2::FieldOffsetAny(dk, { "health", "hp", "curHealth", "currentHealth" });
                            Log("[game] hp field on %s -> %d", cn.c_str(), offHp);
                        }
                    }
                    if (offHp > 0) {
                        p.hp = *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(dmg) + offHp);
                        p.haveHp = true;
                    }
                }
            }
            if (offNick > 0 && fnGetText) {
                void* lbl = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(o) + offNick);
                if (Ok(lbl)) p.name = il2::Str(reinterpret_cast<void*(*)(void*, void*)>(fnGetText)(lbl, nullptr));
            }
            const float d = Dist(p.pos, camPos);
            if (d < bestLocal) { bestLocal = d; local = p; }
            out.push_back(p);
        }

        // Only team-check and the misc writes depend on this; aimbot/triggerbot no longer
        // do, since it goes false whenever the camera detaches (death, spectate, 3rd person).
        s_hasLocal = bestLocal < 4.f;
        // third person parks the camera several units back: keep the player we already
        // identified in first person instead of losing the local player
        if (!s_hasLocal) {
            for (auto& p : out)
                if (p.obj == s_local.obj && Dist(p.pos, camPos) < 14.f) { local = p; s_hasLocal = true; break; }
        }
        if (s_hasLocal) {
            local.local = true;
            s_local = local;
            for (auto& p : out) if (p.obj == local.obj) p.local = true;

            int myTeam = -1;
            if (offTeam > 0) myTeam = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(local.obj) + offTeam);
            Col myCol{};
            bool haveCol = false;
            if (offNick > 0 && fnGetColor) {
                void* lbl = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(local.obj) + offNick);
                if (Ok(lbl)) { myCol = reinterpret_cast<Col(*)(void*, void*)>(fnGetColor)(lbl, nullptr); haveCol = true; }
            }
            for (auto& p : out) {
                if (p.local) { p.ally = true; continue; }
                if (myTeam >= 0 && offTeam > 0) {
                    const int t = *reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(p.obj) + offTeam);
                    p.ally = t == myTeam;
                    continue;
                }
                if (haveCol && offNick > 0 && fnGetColor) {
                    void* lbl = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(p.obj) + offNick);
                    if (Ok(lbl)) {
                        const Col c = reinterpret_cast<Col(*)(void*, void*)>(fnGetColor)(lbl, nullptr);
                        const float dd = fabsf(c.r - myCol.r) + fabsf(c.g - myCol.g) + fabsf(c.b - myCol.b);
                        p.ally = dd < 0.15f;
                    }
                }
            }
        }

        // The wall check cannot be eyeballed from here, so report the mix: all-hidden
        // means the line trims are wrong for this map's colliders, not that it works.
        static DWORD s_visLog = 0;
        if (fnLinecast && GetTickCount() - s_visLog > 3000) {
            s_visLog = GetTickCount();
            int vis = 0, tot = 0, head = 0;
            for (auto& p : out) {
                if (p.local) continue;
                tot++;
                if (p.vis) vis++;
                if (p.haveHead) head++;
            }
            if (tot) Log("[game] vis %d/%d  head-measured %d/%d", vis, tot, head, tot);
        }

#ifdef CORE_BUILD
        custommodel::ServiceShutdown();
        if (!s_hasLocal || !cfg.misc.third_person || cfg.panic) custommodel::Hide();
#endif
        std::lock_guard<std::mutex> g(s_mtx);
        s_players = std::move(out);
    }

    std::vector<Player> Snapshot() {
        std::lock_guard<std::mutex> g(s_mtx);
        return s_players;
    }

    Player* Local() { return s_hasLocal ? &s_local : nullptr; }

    void* Camera() {
        if (!fnCamMain) return nullptr;
        return reinterpret_cast<void*(*)(void*)>(fnCamMain)(nullptr);
    }

    void* CamTransform() {
        void* c = Camera();
        if (!Ok(c) || !fnGetTr) return nullptr;
        return reinterpret_cast<void*(*)(void*, void*)>(fnGetTr)(c, nullptr);
    }

    Vec3 CamPos() {
        void* ct = CamTransform();
        if (ct && fnGetPos)
            return reinterpret_cast<Vec3(*)(void*, void*)>(fnGetPos)(ct, nullptr);
        return {};
    }

    Vec3 CamPosCache() { return s_camPos; }

    Quat CamRotation() { return GetRotation(CamTransform()); }

    Quat GetRotation(void* tr) {
        if (Ok(tr) && fnGetRot)
            return reinterpret_cast<Quat(*)(void*, void*)>(fnGetRot)(tr, nullptr);
        return {};
    }

    void SetRotation(void* tr, const Quat& q) {
        if (Ok(tr) && fnSetRot) {
            Quat c = q;
            reinterpret_cast<void(*)(void*, Quat&, void*)>(fnSetRot)(tr, c, nullptr);
        }
    }

    void LookAt(void* tr, const Vec3& point) {
        if (Ok(tr) && fnLookAt) {
            Vec3 p = point;
            reinterpret_cast<void(*)(void*, Vec3&, void*)>(fnLookAt)(tr, p, nullptr);
        }
    }

    Vec3 Forward(const Quat& q) {
        return { 2.f * (q.w * q.y + q.z * q.x), 2.f * (q.z * q.y - q.w * q.x), 1.f - 2.f * (q.x * q.x + q.y * q.y) };
    }

    bool W2S(const Vec3& world, Vec3& screen) {
        void* cam = Camera();
        if (!Ok(cam) || !fnW2S) return false;
        Vec3 in = world;
        screen = reinterpret_cast<Vec3(*)(void*, Vec3*, int, void*)>(fnW2S)(cam, &in, 2, nullptr);
        return screen.z > 0.f && std::isfinite(screen.x) && std::isfinite(screen.y);
    }

    void* LocalWeapon() {
        if (!s_hasLocal || offOwner < 0) return nullptr;
        std::vector<void*> ws = Live(s_weapons);
        for (void* w : ws) {
            if (!Ok(w)) continue;
            if (*reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(w) + offOwner) == s_local.obj) return w;
        }
        return nullptr;
    }

    int WeaponOff(const char* key) {
        if (!strcmp(key, "recoil")) return offRecoil;
        if (!strcmp(key, "spread")) return offSpread;
        if (!strcmp(key, "delay"))  return offDelay;
        if (!strcmp(key, "ammo"))   return offAmmo;
        if (!strcmp(key, "auto"))   return offAuto;
        return -1;
    }

    float CamFov() {
        void* c = Camera();
        if (!Ok(c) || !fnGetFov) return 60.f;
        const float f = reinterpret_cast<float(*)(void*, void*)>(fnGetFov)(c, nullptr);
        return (std::isfinite(f) && f > 5.f && f < 170.f) ? f : 60.f;
    }

    // Game thread. The hook keeps the value applied; this only pushes a change (toggle or
    // slider) through immediately, and hands the game's own FOV back when switched off.
    void ApplyFov() {
#ifdef CORE_BUILD
        static bool lastOn = false;
        static float lastVal = -1.f;
        const bool on = cfg.misc.fov_changer && !farm::SuppressFeatures();
        if (on == lastOn && (!on || cfg.misc.fov_value == lastVal)) return;
        void* c = Camera();
        if (!oSetFov || !Ok(c)) return;
        if (s_gameFov < 1.f && fnGetFov) s_gameFov = reinterpret_cast<float(*)(void*, void*)>(fnGetFov)(c, nullptr);
        const float v = (on && !Scoped(s_gameFov)) ? cfg.misc.fov_value : s_gameFov;
        if (v < 1.f) return;
        reinterpret_cast<void(__fastcall*)(void*, float, void*)>(oSetFov)(c, v, nullptr);
        lastOn = on; lastVal = cfg.misc.fov_value;
#endif
    }

    void SetHealth(Player& p, float hp) {
        if (offDmg < 0 || offHp < 0) return;
        void* dmg = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(p.obj) + offDmg);
        if (Ok(dmg)) *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(dmg) + offHp) = hp;
    }
}
