#pragma once
#include "../core/common.h"

namespace game {
    struct Player {
        void* obj = nullptr;
        void* tr = nullptr;
        void* head = nullptr;
        void* dmg = nullptr;   // its PlayerDamageable: the game calls ApplyDamage on it when a shot lands
        std::string name;
        bool ally = false;
        bool local = false;
        bool alive = true;
        bool haveHp = false;
        float hp = 100.f;
        Vec3 pos{};
        Vec3 headPos{};        // headCollider world position, measured on the game thread
        bool haveHead = false; // false -> Bone() falls back to a fixed 1.6u span
        bool vis = true;       // clear line from the camera to the chest (game thread)
    };

    bool Init();
    bool Ready();
    void Shutdown();
    void Tick();

    std::vector<Player> Snapshot();
    Player* Local();
    void* Camera();
    void* CamTransform();
    Vec3 CamPos();
    Vec3 CamPosCache();
    Quat CamRotation();

    // 0 = root/feet, 1 = head collider centre. Pure math on a snapshot copy, so it is
    // safe from the render thread; >1 or <0 extrapolates (used for the ESP box edges).
    Vec3 Bone(const Player& p, float frac);
    bool HeadTracked();   // headCollider measurement is live, not estimated

    bool W2S(const Vec3& world, Vec3& screen);
    bool Refresh(Player& p);   // re-read pos/headPos live; game thread only. false = don't trust it

    Quat GetRotation(void* tr);
    void SetRotation(void* tr, const Quat& q);
    void LookAt(void* tr, const Vec3& point);
    Vec3 Forward(const Quat& q);

    void* LocalWeapon();
    int WeaponOff(const char* key);
    bool RapidFireReady(); // reflected weapon delay fields available
    int WeaponPenetrates();   // 1 yes, 0 no, -1 unknown (field not found)
    void ApplyFov();
    float CamFov();   // vertical field of view in degrees, for drawing angular cones
    void SetHealth(Player& p, float hp);
    int LayerBits(const Player& p); // layer mask of the enemy's own colliders (no layer 0); core build only
}
