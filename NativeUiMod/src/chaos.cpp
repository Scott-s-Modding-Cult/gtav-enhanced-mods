#include "chaos.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "cheats.h"
#include "main.h"  // worldGetAll*
#include "menu.h"
#include "natives.h"
#include "vec.h"

namespace chaos {

Settings s;

namespace {

// ================================================================== shared helpers
ULONGLONG g_now = 0;
float g_dt = 0.016f;
Player g_player = 0;
Ped g_me = 0;
Vehicle g_myVeh = 0;

Hash H(const char* name) { return GAMEPLAY::GET_HASH_KEY(const_cast<char*>(name)); }
char* C(const char* s) { return const_cast<char*>(s); }
float Rand(float lo, float hi) { return GAMEPLAY::GET_RANDOM_FLOAT_IN_RANGE(lo, hi); }
int RandInt(int lo, int hiExclusive) { return GAMEPLAY::GET_RANDOM_INT_IN_RANGE(lo, hiExclusive); }
V3 Up(float z) { return {0.0f, 0.0f, z}; }
V3 Norm(V3 a) {
    const float l = Len(a);
    return l > 1e-4f ? a * (1.0f / l) : V3{};
}
V3 CapSpeed(V3 v, float max) {
    const float l = Len(v);
    return l > max ? v * (max / l) : v;
}
bool IsMine(Entity e) { return e == g_me || (g_myVeh && e == g_myVeh); }

V3 CamPos() { return From(CAM::GET_GAMEPLAY_CAM_COORD()); }
V3 CamDir() {
    const V3 r = From(CAM::GET_GAMEPLAY_CAM_ROT(2));
    const float pitch = r.x * 0.0174533f, yaw = r.z * 0.0174533f;
    return {-std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
}

constexpr int kRayWorld = 1, kRayVehicles = 2, kRayPedsSimple = 4, kRayPeds = 8, kRayObjects = 16, kRayFoliage = 256;
constexpr int kRayAll = kRayWorld | kRayVehicles | kRayPedsSimple | kRayPeds | kRayObjects | kRayFoliage;

struct Hit { bool hit = false; V3 pos; Entity entity = 0; };
Hit Ray(V3 from, V3 to, int flags, Entity ignore) {
    Hit h;
    const int ray = WORLDPROBE::_CAST_RAY_POINT_TO_POINT(from.x, from.y, from.z, to.x, to.y, to.z, flags, ignore, 7);
    BOOL hit = FALSE;
    Vector3 end, normal;
    Entity ent = 0;
    WORLDPROBE::_GET_RAYCAST_RESULT(ray, &hit, &end, &normal, &ent);
    h.hit = hit != FALSE;
    h.pos = From(end);
    h.entity = ent;
    return h;
}
// Whatever the crosshair is on, up to `range` m away.
Hit AimHit(float range, int flags = kRayAll) {
    const V3 from = CamPos();
    return Ray(from, from + CamDir() * range, flags, g_myVeh ? g_myVeh : g_me);
}

// Ground height under (x, y), searching down from a bit above `p`; `fallback` if not loaded.
float GroundZ(V3 p, float fallback) {
    float z = 0.0f;
    if (GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(p.x, p.y, p.z + 100.0f, &z, 0)) return z;
    return fallback;
}

bool ModelReady(Hash m) {
    if (STREAMING::HAS_MODEL_LOADED(m)) return true;
    STREAMING::REQUEST_MODEL(m);
    return false;
}

// ---- particle effects (all from the base game's core.ypt; names from gtahash.com/particles)
bool FxReady() {
    if (STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(C("core"))) return true;
    STREAMING::REQUEST_NAMED_PTFX_ASSET(C("core"));
    return false;
}
int FxLoopOn(const char* name, Entity e, V3 offset, float scale) {
    if (!FxReady()) return 0;
    GRAPHICS::_SET_PTFX_ASSET_NEXT_CALL(C("core"));
    return static_cast<int>(GRAPHICS::START_PARTICLE_FX_LOOPED_ON_ENTITY(
        C(name), e, offset.x, offset.y, offset.z, 0.0f, 0.0f, 0.0f, scale, FALSE, FALSE, FALSE));
}
void FxStop(int& handle) {
    if (handle) GRAPHICS::STOP_PARTICLE_FX_LOOPED(handle, FALSE);
    handle = 0;
}

// ---- every vehicle / ped / object in the world, gathered once per frame when needed
int g_veh[1024], g_ped[1024], g_obj[1024];
int g_nVeh = 0, g_nPed = 0, g_nObj = 0;

// ---- launching things. Ragdolls need the push repeated for a few frames to take.
struct Push { Entity e; V3 vel; int frames; };
std::vector<Push> g_pushes;

void Launch(Entity e, V3 vel) {
    if (ENTITY::IS_ENTITY_A_PED(e)) PED::SET_PED_TO_RAGDOLL(e, 4000, 7000, 0, FALSE, FALSE, FALSE);
    SetVel(e, vel);
    g_pushes.push_back({e, vel, 4});
}

void UpdatePushes() {
    for (size_t i = 0; i < g_pushes.size();) {
        Push& p = g_pushes[i];
        if (!ENTITY::DOES_ENTITY_EXIST(p.e) || --p.frames <= 0) {
            g_pushes[i] = g_pushes.back();
            g_pushes.pop_back();
            continue;
        }
        SetVel(p.e, p.vel);
        ++i;
    }
}

void DeleteEntity(Entity e) {
    if (!ENTITY::DOES_ENTITY_EXIST(e)) return;
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, TRUE, TRUE);
    ENTITY::DELETE_ENTITY(&e);
}

// ================================================================== one-punch launch
// Anyone (or any vehicle) you damage with fists or a melee weapon goes flying.
void UpdateOnePunch() {
    if (!s.onePunch || g_myVeh) return;
    const V3 me = Pos(g_me);
    const V3 fwd = Norm2D(From(ENTITY::GET_ENTITY_FORWARD_VECTOR(g_me)));
    // With explosive punches on, the punch's explosion replaces melee as the last damage type, so
    // then any damage from you counts while your fists / a melee weapon are out.
    Hash weapon = 0;
    WEAPON::GET_CURRENT_PED_WEAPON(g_me, &weapon, TRUE);
    const Hash group = WEAPON::GET_WEAPONTYPE_GROUP(weapon);
    const bool meleeOut = weapon == H("WEAPON_UNARMED") || group == H("GROUP_UNARMED") || group == H("GROUP_MELEE");
    auto meleeHit = [meleeOut](Entity e) {
        if (!ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(e, g_me, TRUE)) return false;
        const bool melee = WEAPON::HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON(e, 0, 1) != FALSE ||  // 1 = any melee
                           (s.explosivePunches && meleeOut);
        ENTITY::CLEAR_ENTITY_LAST_DAMAGE_ENTITY(e);
        WEAPON::CLEAR_ENTITY_LAST_WEAPON_DAMAGE(e);
        return melee;
    };
    auto away = [&](Entity e) {
        V3 d = Norm2D(Pos(e) - me);
        return (d.x == 0.0f && d.y == 0.0f) ? fwd : d;
    };
    for (int i = 0; i < g_nPed; ++i) {
        const Ped p = g_ped[i];
        if (p == g_me || Len(Pos(p) - me) > 4.0f || !meleeHit(p)) continue;
        Launch(p, away(p) * 32.0f + Up(14.0f));
        CAM::SHAKE_GAMEPLAY_CAM(C("JOLT_SHAKE"), 0.35f);
    }
    for (int i = 0; i < g_nVeh; ++i) {
        const Vehicle v = g_veh[i];
        if (Len(Pos(v) - me) > 7.0f || !meleeHit(v)) continue;
        Launch(v, away(v) * 30.0f + Up(11.0f));
        CAM::SHAKE_GAMEPLAY_CAM(C("JOLT_SHAKE"), 0.5f);
    }
}

// ================================================================== gravity gun
// Aim at a ped, vehicle or prop and shoot to grab it; it floats where you aim. Shoot again to
// throw it, scroll to pull it in / push it out, let go of aim to drop it.
constexpr int kInputAttack = 24, kInputAim = 25;
constexpr int kInputWheelNext = 14, kInputWheelPrev = 15;
Entity g_held = 0;
float g_holdDist = 10.0f;
ULONGLONG g_grabCooldown = 0;

void BlockFiring() {
    for (int c : {24, 257, 140, 141, 142, 263, 264}) CONTROLS::DISABLE_CONTROL_ACTION(0, c, TRUE);
}

bool Grabbable(Entity e) {
    if (!e || !ENTITY::DOES_ENTITY_EXIST(e) || IsMine(e)) return false;
    // Never anything attached to something else: above all the gun in your own hand (the camera
    // ray passes right by it) or a ped's weapon / phone. Forcing physics onto an attached object
    // corrupts it and crashes the game later — the likely cause of the 2026-11-11 crash.
    if (ENTITY::IS_ENTITY_ATTACHED(e)) return false;
    if (ENTITY::IS_ENTITY_A_PED(e)) return !PED::IS_PED_IN_ANY_VEHICLE(e, FALSE);  // grab their car instead
    return ENTITY::IS_ENTITY_A_VEHICLE(e) || ENTITY::IS_ENTITY_AN_OBJECT(e);
}

Entity AimedEntity() {
    // Start the ray just past you, so it can't pick up your own body or what you're holding.
    const V3 cam = CamPos(), dir = CamDir();
    const float skip = Len(Pos(g_me) - cam) + 1.0f;
    const Hit h = Ray(cam + dir * skip, cam + dir * 150.0f, kRayAll, g_me);
    if (h.hit && Grabbable(h.entity)) return h.entity;
    Entity e = 0;
    if (PLAYER::GET_ENTITY_PLAYER_IS_FREE_AIMING_AT(g_player, &e) && Grabbable(e)) return e;
    return 0;
}

void UpdateGravityGun() {
    if (!s.gravityGun || g_myVeh) { g_held = 0; return; }
    const bool aiming = CONTROLS::IS_CONTROL_PRESSED(0, kInputAim) ||
                        CONTROLS::IS_DISABLED_CONTROL_PRESSED(0, kInputAim) || PLAYER::IS_PLAYER_FREE_AIMING(g_player);
    if (!g_held) {
        if (!aiming || g_now < g_grabCooldown) return;
        const Entity t = AimedEntity();
        if (!t) return;
        BlockFiring();  // pointing at something grabbable: the trigger grabs instead of firing
        if (!CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, kInputAttack)) return;
        g_held = t;
        g_holdDist = Clamp(Len(Pos(t) - CamPos()), 5.0f, 40.0f);
        if (ENTITY::IS_ENTITY_AN_OBJECT(t)) {
            ENTITY::FREEZE_ENTITY_POSITION(t, FALSE);
            ENTITY::SET_ENTITY_DYNAMIC(t, TRUE);  // (no forced ACTIVATE_PHYSICS: unsafe on some props)
        }
        ShowStatus("Grabbed: shoot to throw, scroll to move it, release aim to drop", 3500);
        return;
    }
    // Drop it if it's gone, you stop aiming, or something else took it (got attached / got in a car).
    if (!aiming || !Grabbable(g_held)) { g_held = 0; return; }

    BlockFiring();
    for (int c : {14, 15, 16, 17}) CONTROLS::DISABLE_CONTROL_ACTION(0, c, TRUE);  // scroll = distance
    if (CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, kInputWheelPrev)) g_holdDist += 2.0f;
    if (CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, kInputWheelNext)) g_holdDist -= 2.0f;
    g_holdDist = Clamp(g_holdDist, 3.0f, 60.0f);

    if (CONTROLS::IS_DISABLED_CONTROL_JUST_PRESSED(0, kInputAttack)) {
        Launch(g_held, CamDir() * 70.0f + Up(4.0f));
        g_held = 0;
        g_grabCooldown = g_now + 400;
        CAM::SHAKE_GAMEPLAY_CAM(C("JOLT_SHAKE"), 0.3f);
        return;
    }
    const V3 target = CamPos() + CamDir() * g_holdDist;
    const V3 v = CapSpeed((target - Pos(g_held)) * 9.0f, 50.0f);
    // Ragdoll once (long enough to hold them), rather than restarting the ragdoll every frame.
    if (ENTITY::IS_ENTITY_A_PED(g_held) && !PED::IS_PED_RAGDOLL(g_held))
        PED::SET_PED_TO_RAGDOLL(g_held, 10000, 10000, 0, FALSE, FALSE, FALSE);
    SetVel(g_held, v);
}

// ================================================================== airstrikes
// A burst of airstrike rockets fired from the sky at a point, from one direction.
struct Strike {
    V3 at; V3 from; float radius; int left; ULONGLONG next;
    bool needGround = false;       // far away: wait for collision to load to find the real ground
    ULONGLONG groundDeadline = 0;
};
std::vector<Strike> g_strikes;
const char* const kRocket = "WEAPON_AIRSTRIKE_ROCKET";  // gtahash.com/weapons
ULONGLONG g_airstrikeGunReady = 0;

V3 StrikeOrigin(V3 at) {
    const float a = Rand(0.0f, 6.2832f);
    return at + V3{std::cos(a) * 140.0f, std::sin(a) * 140.0f, 160.0f};
}

void StartStrike(V3 at, int rockets, float radius, bool needGround = false, int delayMs = 500) {
    WEAPON::REQUEST_WEAPON_ASSET(H(kRocket), 31, 0);
    Strike st{at, StrikeOrigin(at), radius, rockets, g_now + static_cast<ULONGLONG>(delayMs)};
    st.needGround = needGround;
    st.groundDeadline = g_now + 4000;
    g_strikes.push_back(st);
}

void UpdateStrikes() {
    const Hash rocket = H(kRocket);
    for (size_t i = 0; i < g_strikes.size();) {
        Strike& st = g_strikes[i];
        if (st.needGround) {
            STREAMING::REQUEST_COLLISION_AT_COORD(st.at.x, st.at.y, st.at.z);
            float z = 0.0f;
            const bool found = GAMEPLAY::GET_GROUND_Z_FOR_3D_COORD(st.at.x, st.at.y, 1000.0f, &z, 0) != FALSE;
            if (found || g_now > st.groundDeadline) {
                if (found) st.at.z = z;
                st.from = StrikeOrigin(st.at);
                st.needGround = false;
                st.next = g_now + 300;
            }
            ++i;
            continue;
        }
        if (g_now >= st.next) {
            V3 to = st.at + V3{Rand(-st.radius, st.radius), Rand(-st.radius, st.radius), 0.0f};
            to.z = GroundZ(to, st.at.z);
            const V3 from = st.from + V3{Rand(-20.0f, 20.0f), Rand(-20.0f, 20.0f), Rand(-10.0f, 10.0f)};
            if (WEAPON::HAS_WEAPON_ASSET_LOADED(rocket))
                GAMEPLAY::SHOOT_SINGLE_BULLET_BETWEEN_COORDS(from.x, from.y, from.z, to.x, to.y, to.z, 250, TRUE,
                                                             rocket, 0, TRUE, FALSE, 400.0f);
            else
                FIRE::ADD_EXPLOSION(to.x, to.y, to.z, 4 /* rocket */, 1.0f, TRUE, FALSE, 1.0f);
            --st.left;
            st.next = g_now + static_cast<ULONGLONG>(Rand(90.0f, 260.0f));
        }
        if (st.left <= 0) { g_strikes[i] = g_strikes.back(); g_strikes.pop_back(); }
        else ++i;
    }
}

void AirstrikeAtAim() {
    const Hit h = AimHit(1500.0f);
    if (!h.hit) { ShowStatus("Aim at the ground or something solid first"); return; }
    StartStrike(h.pos, 14, 14.0f);
    ShowStatus("Airstrike incoming!");
}

void AirstrikeAtMarker() {
    const Blip b = UI::GET_FIRST_BLIP_INFO_ID(8);  // 8 = map waypoint
    if (!UI::DOES_BLIP_EXIST(b)) { ShowStatus("Set a map marker first"); return; }
    V3 c = From(UI::GET_BLIP_INFO_ID_COORD(b));
    c.z = GroundZ({c.x, c.y, 900.0f}, Pos(g_me).z);
    StartStrike(c, 18, 18.0f, /*needGround=*/true);
    ShowStatus("Airstrike on your marker!");
}

// The airstrike gun is its own weapon, WEAPON_AIRSTRIKEGUN: a clone of the flare gun added by the
// "airstrikegun" DLC pack (tools\make-airstrikegun-*.{py,ps1}). It always calls in strikes. If the
// pack isn't loaded, the plain flare gun stands in, and only while the Chaos toggle is on.
const char* const kAirstrikeGun = "WEAPON_AIRSTRIKEGUN";
const char* const kFallbackTool = "WEAPON_FLAREGUN";

bool HaveAirstrikeGun() { return WEAPON::IS_WEAPON_VALID(H(kAirstrikeGun)) != FALSE; }

void GiveAirstrikeTool() {
    const Ped me = PLAYER::PLAYER_PED_ID();
    const bool custom = HaveAirstrikeGun();
    const Hash tool = H(custom ? kAirstrikeGun : kFallbackTool);
    WEAPON::GIVE_WEAPON_TO_PED(me, tool, 20, FALSE, TRUE);
    WEAPON::SET_CURRENT_PED_WEAPON(me, tool, TRUE);
    WEAPON::REQUEST_WEAPON_ASSET(H(kRocket), 31, 0);
    if (custom) {
        ShowStatus(UI::DOES_TEXT_LABEL_EXIST(C("PIM_AIRSTRIKE"))
                       ? "Airstrike gun: fire a flare to call in rockets"
                       : "Airstrike gun ready (its weapon-wheel name may show blank)", 4000);
    } else {
        s.airstrikeGun = true;  // flare gun stands in
        ShowStatus("Add-on airstrike gun not loaded - using the flare gun", 4500);
    }
}

void UpdateAirstrikeGun() {
    if (g_now < g_airstrikeGunReady || !PED::IS_PED_SHOOTING(g_me)) return;
    Hash weapon = 0;
    if (!WEAPON::GET_CURRENT_PED_WEAPON(g_me, &weapon, TRUE)) return;
    const bool isTool = weapon == H(kAirstrikeGun) || (s.airstrikeGun && weapon == H(kFallbackTool));
    if (!isTool) return;
    // Target: where the flare hit if the game reports it, else where you're aiming.
    V3 target;
    Vector3 c;
    if (WEAPON::GET_PED_LAST_WEAPON_IMPACT_COORD(g_me, &c)) {
        target = From(c);
    } else {
        const Hit h = AimHit(300.0f);
        if (!h.hit) return;
        target = h.pos;
    }
    StartStrike(target, 12, 11.0f, false, 1800);  // give the flare a moment to land and glow
    g_airstrikeGunReady = g_now + 2500;
    ShowStatus("Airstrike called in - clear the area!");
}

// ================================================================== meteor shower
// Flaming boulders flown along a falling arc by the script (so props without physics still
// fly), raycasting ahead each frame; on impact: big explosion, and the rock stays burning.
const char* const kMeteorModels[] = {"prop_test_boulder_01", "prop_test_boulder_02",
                                     "prop_test_boulder_03", "prop_test_boulder_04"};  // gtahash.com/objects
struct Meteor {
    Object obj = 0;
    V3 pos, vel, rot, spin;
    int fire = 0, trail = 0;
    bool landed = false;
    ULONGLONG born = 0, removeAt = 0;
};
std::vector<Meteor> g_meteors;
ULONGLONG g_nextMeteor = 0;

void SpawnMeteor() {
    const Hash model = H(kMeteorModels[RandInt(0, 4)]);
    if (!ModelReady(model) || !FxReady()) return;
    const V3 me = Pos(g_me);
    // Land anywhere within 90 m — but not right on you unless you're in god mode.
    const float ang = Rand(0.0f, 6.2832f), dist = Rand(cheats::g.godMode ? 0.0f : 15.0f, 90.0f);
    V3 land = me + V3{std::cos(ang) * dist, std::sin(ang) * dist, 0.0f};
    land.z = GroundZ(land, me.z);
    const float from = Rand(0.0f, 6.2832f);
    const V3 start = land + V3{std::cos(from) * 70.0f, std::sin(from) * 70.0f, 170.0f};

    Meteor m;
    m.obj = OBJECT::CREATE_OBJECT(model, start.x, start.y, start.z, FALSE, TRUE, FALSE);
    if (!m.obj) return;
    ENTITY::FREEZE_ENTITY_POSITION(m.obj, TRUE);
    ENTITY::SET_ENTITY_COLLISION(m.obj, FALSE, FALSE);
    m.pos = start;
    m.vel = Norm(land - start) * Rand(55.0f, 75.0f);
    m.spin = {Rand(-240.0f, 240.0f), Rand(-240.0f, 240.0f), Rand(-240.0f, 240.0f)};
    m.fire = FxLoopOn("fire_wrecked_plane_cockpit", m.obj, {}, 2.5f);
    m.trail = FxLoopOn("proj_missile_trail", m.obj, {}, 4.0f);
    m.born = g_now;
    g_meteors.push_back(m);
}

void RemoveMeteor(Meteor& m) {
    FxStop(m.fire);
    FxStop(m.trail);
    if (ENTITY::DOES_ENTITY_EXIST(m.obj)) OBJECT::DELETE_OBJECT(&m.obj);
}

void UpdateMeteors() {
    if (s.meteors) {
        for (const char* name : kMeteorModels) ModelReady(H(name));  // keep them streamed in
        int flying = 0;
        for (const Meteor& m : g_meteors) flying += !m.landed;
        if (g_now >= g_nextMeteor && flying < 10) {
            SpawnMeteor();
            g_nextMeteor = g_now + static_cast<ULONGLONG>(Rand(350.0f, 1000.0f));
        }
    }
    for (size_t i = 0; i < g_meteors.size();) {
        Meteor& m = g_meteors[i];
        bool remove = !ENTITY::DOES_ENTITY_EXIST(m.obj) || (m.landed && g_now >= m.removeAt);
        if (!remove && !m.landed) {
            m.vel.z -= 9.8f * g_dt;
            const V3 step = m.vel * g_dt;
            const Hit h = Ray(m.pos, m.pos + step * 1.3f, kRayWorld | kRayVehicles | kRayObjects, m.obj);
            if (h.hit || g_now - m.born > 9000) {
                const V3 at = h.hit ? h.pos : m.pos;
                FIRE::ADD_EXPLOSION(at.x, at.y, at.z, 8 /* plane */, 1.0f, TRUE, FALSE, 1.2f);
                FIRE::ADD_EXPLOSION(at.x + Rand(-3.0f, 3.0f), at.y + Rand(-3.0f, 3.0f), at.z, 5 /* tank shell */,
                                    1.0f, TRUE, FALSE, 0.6f);
                FxStop(m.trail);  // keep the flames: it lies there burning
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(m.obj, at.x, at.y, at.z - 0.8f, FALSE, FALSE, FALSE);
                ENTITY::SET_ENTITY_COLLISION(m.obj, TRUE, FALSE);
                m.landed = true;
                m.removeAt = g_now + 20000;
            } else {
                m.pos = m.pos + step;
                m.rot = m.rot + m.spin * g_dt;
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(m.obj, m.pos.x, m.pos.y, m.pos.z, FALSE, FALSE, FALSE);
                ENTITY::SET_ENTITY_ROTATION(m.obj, m.rot.x, m.rot.y, m.rot.z, 2, TRUE);
            }
        }
        if (remove) {
            RemoveMeteor(m);
            g_meteors[i] = g_meteors.back();
            g_meteors.pop_back();
        } else {
            ++i;
        }
    }
}

// ================================================================== hostile crowds / zombies
enum Role { kNone, kHostile, kZombie };
struct Converted {
    Role role; Hash origGroup; ULONGLONG nextTask;
    Hash givenGun = 0;     // hostile: the gun we handed out (taken back on revert)
    bool targetsYou = true; // hostile: hunts you specifically (others fight whoever's nearest)
    int origMaxHealth = 0; // zombie: their max health before we buffed it
};
std::unordered_map<Ped, Converted> g_crowd;
std::unordered_map<Ped, ULONGLONG> g_toldToLeave;  // zombies-to-be still in vehicles
// Armed NPCs are split into rival gangs. With infighting on, every gang hates every other gang,
// its own members, bystanders and you, so it turns into a free-for-all; with it off they only
// hate you.
constexpr int kGangCount = 4;
Hash g_gangs[kGangCount] = {};
Hash g_zombieGroup = 0;
// Ordinary groups armed NPCs will also shoot at (one-way: bystanders still just run).
const char* const kBystanderGroups[] = {
    "CIVMALE", "CIVFEMALE", "COP", "SECURITY_GUARD", "PRIVATE_SECURITY", "FIREMAN", "MEDIC", "ARMY",
    "GANG_1", "GANG_2", "GANG_9", "GANG_10", "AMBIENT_GANG_LOST", "AMBIENT_GANG_MEXICAN",
    "AMBIENT_GANG_FAMILY", "AMBIENT_GANG_BALLAS", "AMBIENT_GANG_MARABUNTE", "AMBIENT_GANG_CULT",
    "AMBIENT_GANG_SALVA", "AMBIENT_GANG_WEICHENG", "AMBIENT_GANG_HILLBILLY", "DEALER", "PRISONER",
};
ULONGLONG g_nextCrowd = 0;
const char* const kZombieWalk = "move_m@drunk@verydrunk";
const char* const kCrowdGuns[] = {
    "WEAPON_PISTOL", "WEAPON_COMBATPISTOL", "WEAPON_MICROSMG", "WEAPON_SMG", "WEAPON_ASSAULTRIFLE",
    "WEAPON_CARBINERIFLE", "WEAPON_PUMPSHOTGUN", "WEAPON_SAWNOFFSHOTGUN", "WEAPON_COMBATMG", "WEAPON_MG",
    "WEAPON_RPG", "WEAPON_MOLOTOV", "WEAPON_KNIFE", "WEAPON_BAT",
};

constexpr int kRelRespect = 1, kRelNeutral = 3, kRelHate = 5;

void EnsureGroups() {
    for (int i = 0; i < kGangCount; ++i)
        if (!g_gangs[i]) {
            char name[32];
            std::snprintf(name, sizeof name, "CHAOS_GANG_%d", i + 1);
            PED::ADD_RELATIONSHIP_GROUP(name, &g_gangs[i]);
        }
    if (!g_zombieGroup) PED::ADD_RELATIONSHIP_GROUP(C("CHAOS_ZOMBIE"), &g_zombieGroup);

    const Hash player = H("PLAYER");
    PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelHate, g_zombieGroup, player);
    PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelHate, player, g_zombieGroup);
    const bool brawl = s.hostileInfighting;
    for (int i = 0; i < kGangCount; ++i) {
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelHate, g_gangs[i], player);
        PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelHate, player, g_gangs[i]);
        for (int j = 0; j < kGangCount; ++j)  // includes itself: members turn on each other too
            PED::SET_RELATIONSHIP_BETWEEN_GROUPS(brawl ? kRelHate : kRelRespect, g_gangs[i], g_gangs[j]);
        for (const char* by : kBystanderGroups)
            PED::SET_RELATIONSHIP_BETWEEN_GROUPS(brawl ? kRelHate : kRelNeutral, g_gangs[i], H(by));
    }
}

bool CrowdCandidate(Ped p) {
    return p != g_me && ENTITY::DOES_ENTITY_EXIST(p) && !ENTITY::IS_ENTITY_DEAD(p) && PED::IS_PED_HUMAN(p) &&
           !PED::IS_PED_A_PLAYER(p) && !ENTITY::IS_ENTITY_A_MISSION_ENTITY(p) &&  // leave story/mission peds alone
           !PED::IS_PED_GROUP_MEMBER(p, PLAYER::GET_PLAYER_GROUP(g_player));
}

// Point a converted ped at you, or (brawlers) at whoever they hate that's closest.
void GiveCombatTask(Ped p, bool targetsYou) {
    if (targetsYou) AI::TASK_COMBAT_PED(p, g_me, 0, 16);
    else AI::TASK_COMBAT_HATED_TARGETS_AROUND_PED(p, 120.0f, 0);
}

void Aggro(Ped p, bool targetsYou = true) {
    PED::SET_PED_COMBAT_ATTRIBUTES(p, 46, TRUE);  // always fight
    PED::SET_PED_COMBAT_ATTRIBUTES(p, 5, TRUE);   // fight armed targets even when unarmed
    GiveCombatTask(p, targetsYou);
    PED::SET_PED_KEEP_TASK(p, TRUE);
}

void MakeHostile(Ped p, Converted& c) {
    const Hash gun = H(kCrowdGuns[RandInt(0, static_cast<int>(sizeof kCrowdGuns / sizeof *kCrowdGuns))]);
    if (!WEAPON::HAS_PED_GOT_WEAPON(p, gun, FALSE)) c.givenGun = gun;  // don't take away one they had
    WEAPON::GIVE_WEAPON_TO_PED(p, gun, 9999, FALSE, TRUE);
    WEAPON::SET_CURRENT_PED_WEAPON(p, gun, TRUE);
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, g_gangs[RandInt(0, kGangCount)]);
    PED::SET_PED_COMBAT_ABILITY(p, 1);
    PED::SET_PED_ACCURACY(p, 25);
    // With infighting about a third still hunt you; the rest go for whoever's nearest.
    c.targetsYou = !s.hostileInfighting || Rand(0.0f, 1.0f) < 0.35f;
    Aggro(p, c.targetsYou);
}

void MakeZombie(Ped p, Converted& c) {
    c.origMaxHealth = ENTITY::GET_ENTITY_MAX_HEALTH(p);
    WEAPON::REMOVE_ALL_PED_WEAPONS(p, TRUE);
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, g_zombieGroup);
    if (STREAMING::HAS_ANIM_SET_LOADED(C(kZombieWalk))) PED::SET_PED_MOVEMENT_CLIPSET(p, C(kZombieWalk), 1.0f);
    PED::APPLY_PED_DAMAGE_PACK(p, C("BigHitByVehicle"), 0.0f, 1.0f);  // bloodied
    ENTITY::SET_ENTITY_MAX_HEALTH(p, 400);
    ENTITY::SET_ENTITY_HEALTH(p, 400);
    PED::SET_PED_COMBAT_MOVEMENT(p, 2);  // will advance: keep closing in
    PED::SET_PED_SEEING_RANGE(p, 120.0f);
    PED::SET_PED_HEARING_RANGE(p, 120.0f);
    AUDIO::DISABLE_PED_PAIN_AUDIO(p, TRUE);
    AUDIO::STOP_PED_SPEAKING(p, TRUE);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, TRUE);  // don't flinch away from gunfire
    Aggro(p);
}

void Revert(Ped p, const Converted& c) {
    if (!ENTITY::DOES_ENTITY_EXIST(p)) return;
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(p, c.origGroup);
    PED::SET_PED_COMBAT_ATTRIBUTES(p, 46, FALSE);
    PED::SET_PED_COMBAT_ATTRIBUTES(p, 5, FALSE);
    if (c.givenGun) WEAPON::REMOVE_WEAPON_FROM_PED(p, c.givenGun);
    if (c.role == kZombie) {
        PED::RESET_PED_MOVEMENT_CLIPSET(p, 0.0f);
        AUDIO::DISABLE_PED_PAIN_AUDIO(p, FALSE);
        AUDIO::STOP_PED_SPEAKING(p, FALSE);
        if (c.origMaxHealth > 0) {
            ENTITY::SET_ENTITY_MAX_HEALTH(p, c.origMaxHealth);
            if (ENTITY::GET_ENTITY_HEALTH(p) > c.origMaxHealth) ENTITY::SET_ENTITY_HEALTH(p, c.origMaxHealth);
        }
    }
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p, FALSE);
    PED::SET_PED_KEEP_TASK(p, FALSE);
    if (!ENTITY::IS_ENTITY_DEAD(p)) {
        AI::CLEAR_PED_TASKS(p);
        // Drivers carry on driving instead of abandoning the car in the road.
        if (PED::IS_PED_IN_ANY_VEHICLE(p, FALSE)) {
            const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(p, FALSE);
            if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1) == p) AI::TASK_VEHICLE_DRIVE_WANDER(p, v, 15.0f, 786603);
        } else {
            AI::TASK_WANDER_STANDARD(p, 10.0f, 10);
        }
    }
}

void UpdateCrowd() {
    const Role want = s.zombies ? kZombie : (s.hostile ? kHostile : kNone);
    if (want == kNone) {
        for (auto& kv : g_crowd) Revert(kv.first, kv.second);
        g_crowd.clear();
        g_toldToLeave.clear();
        return;
    }
    if (want == kZombie) STREAMING::REQUEST_ANIM_SET(C(kZombieWalk));
    if (g_now < g_nextCrowd) return;
    g_nextCrowd = g_now + 500;
    EnsureGroups();

    for (auto it = g_crowd.begin(); it != g_crowd.end();)
        it = ENTITY::DOES_ENTITY_EXIST(it->first) ? std::next(it) : g_crowd.erase(it);
    for (auto it = g_toldToLeave.begin(); it != g_toldToLeave.end();)
        it = ENTITY::DOES_ENTITY_EXIST(it->first) && PED::IS_PED_IN_ANY_VEHICLE(it->first, FALSE) ? std::next(it)
                                                                                                    : g_toldToLeave.erase(it);

    const V3 me = Pos(g_me);
    int converted = 0;
    for (int i = 0; i < g_nPed; ++i) {
        const Ped p = g_ped[i];
        auto it = g_crowd.find(p);
        if (it != g_crowd.end()) {
            Converted& c = it->second;
            if (ENTITY::IS_ENTITY_DEAD(p)) continue;
            if (c.role != want) {  // switched between hostile and zombie: convert again next pass
                Revert(p, c);
                g_crowd.erase(it);
                continue;
            }
            if (g_now >= c.nextTask) {
                c.nextTask = g_now + 3000;
                if (c.role == kHostile && !s.hostileInfighting) c.targetsYou = true;  // infighting switched off
                // Lost their target / ran out of hated people nearby: give them a new fight.
                const bool idle = c.targetsYou
                    ? !PED::IS_PED_IN_COMBAT(p, g_me)
                    : AI::GET_SCRIPT_TASK_STATUS(p, H("SCRIPT_TASK_COMBAT_HATED_TARGETS_AROUND_PED")) == 7;
                if (idle) GiveCombatTask(p, c.targetsYou);
            }
            continue;
        }
        if (converted >= 12 || !CrowdCandidate(p) || Len(Pos(p) - me) > 110.0f) continue;
        if (want == kZombie && PED::IS_PED_IN_ANY_VEHICLE(p, FALSE)) {
            // Out you get (asked once every few seconds, so the exit isn't restarted each pass);
            // they turn once they're on foot.
            ULONGLONG& asked = g_toldToLeave[p];
            if (g_now >= asked) { AI::TASK_LEAVE_ANY_VEHICLE(p, 0, 0); asked = g_now + 5000; }
            continue;
        }
        Converted& conv = g_crowd[p];
        conv = Converted{want, PED::GET_PED_RELATIONSHIP_GROUP_HASH(p), g_now + 3000};
        if (want == kHostile) MakeHostile(p, conv); else MakeZombie(p, conv);
        ++converted;
    }
}

// ================================================================== maniac traffic
// Drivers floor it with an extra torque boost, ignore the rules, and about a quarter of the ones
// near you try to ram you. Everything is undone when it's switched off.
constexpr int kStyleRushed = 1074528293, kStyleNormal = 786603;
// Rammers: rushed, minus "stop for cars" (1) and "swerve around cars" (4), plus "don't steer
// around the player" (64).
constexpr int kStyleRam = (kStyleRushed & ~5) | 64;
struct Maniac { Ped driver; };
std::unordered_map<Vehicle, Maniac> g_maniacs;
ULONGLONG g_nextTraffic = 0;

bool IsRoadVehicle(Vehicle v) {
    const Hash m = ENTITY::GET_ENTITY_MODEL(v);
    return !VEHICLE::IS_THIS_MODEL_A_HELI(m) && !VEHICLE::IS_THIS_MODEL_A_PLANE(m) &&
           !VEHICLE::IS_THIS_MODEL_A_BOAT(m) && !VEHICLE::IS_THIS_MODEL_A_TRAIN(m);
}

void CalmDown(Vehicle v, const Maniac& m) {
    if (!ENTITY::DOES_ENTITY_EXIST(v) || !ENTITY::DOES_ENTITY_EXIST(m.driver) || ENTITY::IS_ENTITY_DEAD(m.driver)) return;
    PED::SET_PED_KEEP_TASK(m.driver, FALSE);
    PED::SET_DRIVER_AGGRESSIVENESS(m.driver, 0.5f);
    if (PED::IS_PED_IN_VEHICLE(m.driver, v, FALSE)) AI::TASK_VEHICLE_DRIVE_WANDER(m.driver, v, 15.0f, kStyleNormal);
}

void UpdateTraffic() {
    if (!s.maniacTraffic) {
        for (auto& kv : g_maniacs) CalmDown(kv.first, kv.second);
        g_maniacs.clear();
        return;
    }
    // The torque boost only lasts a frame, so it's applied every frame.
    for (auto it = g_maniacs.begin(); it != g_maniacs.end();) {
        const Vehicle mv = it->first;
        if (!ENTITY::DOES_ENTITY_EXIST(mv) || ENTITY::IS_ENTITY_DEAD(mv)) { it = g_maniacs.erase(it); continue; }
        if (mv == g_myVeh || VEHICLE::GET_PED_IN_VEHICLE_SEAT(mv, -1) != it->second.driver) {  // carjacked / new driver
            it = g_maniacs.erase(it);
            continue;
        }
        VEHICLE::_SET_VEHICLE_ENGINE_TORQUE_MULTIPLIER(it->first, 3.0f);
        ++it;
    }
    if (g_now < g_nextTraffic) return;
    g_nextTraffic = g_now + 700;
    const V3 me = Pos(g_me);
    int added = 0;
    for (int i = 0; i < g_nVeh && added < 10; ++i) {
        const Vehicle v = g_veh[i];
        if (v == g_myVeh || g_maniacs.count(v) || !IsRoadVehicle(v)) continue;
        const float dist = Len(Pos(v) - me);
        if (dist > 220.0f) continue;
        const Ped d = VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1);
        if (!d || !ENTITY::DOES_ENTITY_EXIST(d) || PED::IS_PED_A_PLAYER(d) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(d) ||
            ENTITY::IS_ENTITY_DEAD(d) || !PED::IS_PED_HUMAN(d))
            continue;
        PED::SET_DRIVER_ABILITY(d, 1.0f);
        PED::SET_DRIVER_AGGRESSIVENESS(d, 1.0f);
        if (dist < 150.0f && Rand(0.0f, 1.0f) < 0.25f) {
            AI::TASK_VEHICLE_MISSION_PED_TARGET(d, v, g_me, 2 /* ram */, 70.0f, kStyleRam, 0.0f, 0.0f, TRUE);
        } else {
            AI::TASK_VEHICLE_DRIVE_WANDER(d, v, 70.0f, kStyleRushed);
            AI::SET_DRIVE_TASK_CRUISE_SPEED(d, 70.0f);
        }
        PED::SET_PED_KEEP_TASK(d, TRUE);
        g_maniacs[v] = {d};
        ++added;
    }
}

// ================================================================== car launches and flips
struct Flip { Vehicle v; float done; float dir; };
std::vector<Flip> g_flips;
ULONGLONG g_nextRandomLaunch = 0;

void LaunchCar(Vehicle v) { SetVel(v, Vel(v) + V3{Rand(-5.0f, 5.0f), Rand(-5.0f, 5.0f), Rand(18.0f, 28.0f)}); }

// Hop it up and roll it over onto its roof over ~0.4 s.
void FlipCar(Vehicle v) {
    SetVel(v, Vel(v) + Up(Rand(8.0f, 11.0f)));
    g_flips.push_back({v, 0.0f, Rand(0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f});
}

std::vector<Vehicle> NearbyCars(float range) {
    g_nVeh = worldGetAllVehicles(g_veh, 1024);
    std::vector<Vehicle> out;
    const V3 me = Pos(g_me);
    for (int i = 0; i < g_nVeh; ++i) {
        const Vehicle v = g_veh[i];
        if (v != g_myVeh && !ENTITY::IS_ENTITY_DEAD(v) && Len(Pos(v) - me) < range) out.push_back(v);
    }
    return out;
}

void UpdateFlips() {
    for (size_t i = 0; i < g_flips.size();) {
        Flip& f = g_flips[i];
        if (!ENTITY::DOES_ENTITY_EXIST(f.v) || f.done >= 180.0f) { g_flips[i] = g_flips.back(); g_flips.pop_back(); continue; }
        const float step = std::fmin(450.0f * g_dt, 180.0f - f.done);
        const V3 r = From(ENTITY::GET_ENTITY_ROTATION(f.v, 2));
        ENTITY::SET_ENTITY_ROTATION(f.v, r.x, r.y + step * f.dir, r.z, 2, TRUE);
        f.done += step;
        ++i;
    }
}

void UpdateRandomLaunches() {
    if (!s.randomLaunches || g_now < g_nextRandomLaunch) return;
    g_nextRandomLaunch = g_now + static_cast<ULONGLONG>(Rand(700.0f, 2500.0f));
    const std::vector<Vehicle> cars = NearbyCars(120.0f);
    if (cars.empty()) return;
    const Vehicle v = cars[RandInt(0, static_cast<int>(cars.size()))];
    if (Rand(0.0f, 1.0f) < 0.3f) FlipCar(v); else LaunchCar(v);
}

// ================================================================== black hole / tornado
struct Vortex { bool on = false; V3 c; Object anchor = 0; int fx1 = 0, fx2 = 0; Hash weatherBefore = 0; };
Vortex g_hole, g_tornado;

// Things that fall into the black hole are gone. Story/mission entities are spared (they just orbit).
void Swallow(Entity e) {
    if (ENTITY::IS_ENTITY_A_MISSION_ENTITY(e)) return;
    if (ENTITY::IS_ENTITY_A_VEHICLE(e)) {
        const int seats = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(e);
        for (int seat = -1; seat < seats; ++seat) {  // spare it entirely if anyone important is aboard
            const Ped p = VEHICLE::GET_PED_IN_VEHICLE_SEAT(e, seat);
            if (p && (PED::IS_PED_A_PLAYER(p) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(p))) return;
        }
        for (int seat = -1; seat < seats; ++seat) {
            const Ped p = VEHICLE::GET_PED_IN_VEHICLE_SEAT(e, seat);
            if (p) DeleteEntity(p);
        }
    }
    DeleteEntity(e);
}

void StartBlackHole() {
    const Hit h = AimHit(300.0f);
    const V3 fwd = Norm2D(From(ENTITY::GET_ENTITY_FORWARD_VECTOR(g_me)));
    g_hole.c = (h.hit ? h.pos : Pos(g_me) + fwd * 40.0f) + Up(8.0f);
    g_hole.on = true;
}

// Everything a vortex can move: vehicles (not trains), people on foot (people in vehicles go
// with their vehicle), and loose props. Never you or your vehicle.
template <typename F>
void ForEachMovable(F fn) {
    for (int i = 0; i < g_nVeh; ++i)
        if (!IsMine(g_veh[i]) && g_veh[i] != g_held && !VEHICLE::IS_THIS_MODEL_A_TRAIN(ENTITY::GET_ENTITY_MODEL(g_veh[i])))
            fn(g_veh[i], false);
    for (int i = 0; i < g_nPed; ++i)
        if (!IsMine(g_ped[i]) && g_ped[i] != g_held && !PED::IS_PED_IN_ANY_VEHICLE(g_ped[i], FALSE)) fn(g_ped[i], true);
    for (int i = 0; i < g_nObj; ++i)
        if (!ENTITY::IS_ENTITY_ATTACHED(g_obj[i]) && g_obj[i] != g_tornado.anchor && g_obj[i] != g_held)
            fn(g_obj[i], false);
}

void UpdateBlackHole() {
    if (!s.blackHole) { g_hole.on = false; return; }
    if (!g_hole.on) StartBlackHole();
    const V3 c = g_hole.c;
    const float spin = static_cast<float>(g_now % 3600) * 0.1f;
    // Black core with two glowing accretion rings.
    GRAPHICS::DRAW_MARKER(28, c.x, c.y, c.z, 0, 0, 0, 0, 0, 0, 4.5f, 4.5f, 4.5f, 0, 0, 0, 255, FALSE, FALSE, 2, FALSE,
                          nullptr, nullptr, FALSE);
    GRAPHICS::DRAW_MARKER(25, c.x, c.y, c.z, 0, 0, 0, 75.0f, 0, spin, 16.0f, 16.0f, 16.0f, 255, 120, 20, 170, FALSE,
                          FALSE, 2, FALSE, nullptr, nullptr, FALSE);
    GRAPHICS::DRAW_MARKER(25, c.x, c.y, c.z, 0, 0, 0, 105.0f, 0, -spin * 1.3f, 23.0f, 23.0f, 23.0f, 170, 60, 255, 120,
                          FALSE, FALSE, 2, FALSE, nullptr, nullptr, FALSE);

    constexpr float kRange = 100.0f;
    ForEachMovable([&](Entity e, bool ped) {
        const V3 d = c - Pos(e);
        const float dist = Len(d);
        if (dist > kRange) return;
        if (dist < 3.5f) { Swallow(e); return; }
        const V3 dir = d * (1.0f / dist);
        const float k = 1.0f - dist / kRange;
        const float accel = 6.0f + 70.0f * k * k;  // m/s^2, much stronger near the core
        const V3 swirl = Norm(V3{-dir.y, dir.x, 0.0f});
        V3 v = Vel(e) + (dir * accel + swirl * (accel * 0.35f)) * g_dt;
        v.z += 9.8f * g_dt * Clamp(k * 1.5f, 0.0f, 1.0f);  // cancel gravity as it gets close
        if (ped && k > 0.2f) PED::SET_PED_TO_RAGDOLL(e, 2000, 4000, 0, FALSE, FALSE, FALSE);
        SetVel(e, CapSpeed(v, 55.0f));
    });
}

void StopTornado() {
    FxStop(g_tornado.fx1);
    FxStop(g_tornado.fx2);
    if (g_tornado.anchor && ENTITY::DOES_ENTITY_EXIST(g_tornado.anchor)) OBJECT::DELETE_OBJECT(&g_tornado.anchor);
    g_tornado.anchor = 0;
    if (g_tornado.on) {
        GAMEPLAY::CLEAR_WEATHER_TYPE_PERSIST();
        if (g_tornado.weatherBefore)  // put the sky back the way it was
            GAMEPLAY::_SET_WEATHER_TYPE_TRANSITION(g_tornado.weatherBefore, g_tornado.weatherBefore, 0.0f);
    }
    g_tornado.on = false;
}

void StartTornado() {
    const Hash model = H(kMeteorModels[0]);  // an invisible prop that carries the dust effects
    if (!ModelReady(model) || !FxReady()) return;  // try again next frame
    const V3 me = Pos(g_me);
    const V3 fwd = Norm2D(From(ENTITY::GET_ENTITY_FORWARD_VECTOR(g_me)));
    V3 c = me + fwd * 70.0f;
    c.z = GroundZ(c, me.z);
    g_tornado.c = c;
    g_tornado.anchor = OBJECT::CREATE_OBJECT(model, c.x, c.y, c.z, FALSE, TRUE, FALSE);
    if (g_tornado.anchor) {
        ENTITY::SET_ENTITY_ALPHA(g_tornado.anchor, 0, FALSE);  // invisible, but its effects still draw
        ENTITY::SET_ENTITY_COLLISION(g_tornado.anchor, FALSE, FALSE);
        ENTITY::FREEZE_ENTITY_POSITION(g_tornado.anchor, TRUE);
        g_tornado.fx1 = FxLoopOn("env_dust_devil_urban_lrg", g_tornado.anchor, {}, 8.0f);
        g_tornado.fx2 = FxLoopOn("env_dust_devil_rural_lrg", g_tornado.anchor, Up(20.0f), 10.0f);
    }
    g_tornado.weatherBefore = GAMEPLAY::_GET_CURRENT_WEATHER_TYPE();
    GAMEPLAY::SET_WEATHER_TYPE_NOW_PERSIST(C("THUNDER"));
    g_tornado.on = true;
}

void UpdateTornado() {
    if (!s.tornado) { if (g_tornado.on || g_tornado.anchor) StopTornado(); return; }
    if (!g_tornado.on) { StartTornado(); if (!g_tornado.on) return; }

    // Wander toward you with a lazy sideways drift.
    V3& c = g_tornado.c;
    const V3 toMe = Flat(Pos(g_me) - c);
    const float t = static_cast<float>(g_now % 100000) * 0.001f;
    const V3 drift = (Len2D(toMe) > 1.0f ? Norm2D(toMe) * 7.0f : V3{}) + V3{std::sin(t * 0.7f) * 4.0f, std::cos(t * 0.9f) * 4.0f, 0.0f};
    c = c + drift * g_dt;
    c.z = GroundZ(c, c.z);
    if (g_tornado.anchor) ENTITY::SET_ENTITY_COORDS_NO_OFFSET(g_tornado.anchor, c.x, c.y, c.z, FALSE, FALSE, FALSE);

    // Funnel: stacked rings widening with height, each turning.
    const float spin = static_cast<float>(g_now % 3600) * 0.25f;
    for (int i = 0; i < 14; ++i) {
        const float h = i * 8.0f, r = 5.0f + h * 0.45f;
        GRAPHICS::DRAW_MARKER(25, c.x + std::sin(t * 2.0f + i * 0.4f) * 1.5f, c.y, c.z + h, 0, 0, 0, 0, 0, spin + i * 25.0f,
                              r * 2.0f, r * 2.0f, 2.0f, 95, 85, 75, 110, FALSE, FALSE, 2, FALSE, nullptr, nullptr, FALSE);
    }

    constexpr float kRadius = 45.0f, kHeight = 140.0f;
    ForEachMovable([&](Entity e, bool ped) {
        const V3 p = Pos(e);
        const V3 rel = p - c;
        const float rh = Len2D(rel), h = p.z - c.z;
        if (rh > kRadius || h > kHeight || h < -10.0f) return;
        const float k = 1.0f - rh / kRadius;
        const V3 inward = rh > 0.1f ? Flat(rel) * (-1.0f / rh) : V3{};
        const V3 tangent{-inward.y, inward.x, 0.0f};
        V3 desired = tangent * (18.0f + 22.0f * k) + inward * (6.0f + 10.0f * k) + Up(h > 80.0f ? -2.0f : 6.0f + 26.0f * k);
        if (h > 80.0f) desired = Flat(rel) * (45.0f / (rh > 1.0f ? rh : 1.0f)) + Up(10.0f);  // spat out the top
        const float blend = Clamp(g_dt * (1.0f + 3.0f * k), 0.0f, 1.0f);
        if (ped && k > 0.15f) PED::SET_PED_TO_RAGDOLL(e, 2000, 4000, 0, FALSE, FALSE, FALSE);
        const V3 v = Vel(e);
        SetVel(e, v + (desired - v) * blend);
    });
}

// ================================================================== menu
void StopAll() {
    s = Settings{};  // every toggle off; the per-frame updates undo their own changes
    g_strikes.clear();
    ShowStatus("All chaos stopped");
}

}  // namespace

void Setup(Menu& m) {
    auto onOff = [](const char* name, const bool* v) {
        return [name, v] { ShowStatus(std::string(name) + (*v ? ": ON" : ": OFF")); };
    };
    m.AddToggle("Fire bullets", &s.fireBullets, onOff("Fire bullets", &s.fireBullets));
    m.AddToggle("Explosive punches", &s.explosivePunches, onOff("Explosive punches", &s.explosivePunches));
    m.AddToggle("One-punch launch", &s.onePunch, onOff("One-punch launch", &s.onePunch));
    m.AddToggle("Gravity gun", &s.gravityGun, [] {
        ShowStatus(s.gravityGun ? "Gravity gun: aim at something, shoot to grab" : "Gravity gun: OFF", 4000);
    });
    m.AddAction("Airstrike where you're looking", AirstrikeAtAim);
    m.AddAction("Airstrike on map marker", AirstrikeAtMarker);
    m.AddAction("Give airstrike gun", GiveAirstrikeTool);
    m.AddToggle("Flare gun also calls airstrikes", &s.airstrikeGun, onOff("Flare gun airstrikes", &s.airstrikeGun));
    m.AddToggle("Meteor shower", &s.meteors, onOff("Meteor shower", &s.meteors));
    m.AddToggle("Everyone armed and hostile", &s.hostile, onOff("Everyone hostile", &s.hostile));
    m.AddToggle("  ...and they fight each other", &s.hostileInfighting,
                onOff("Hostile infighting", &s.hostileInfighting));
    m.AddToggle("Zombie mode", &s.zombies, onOff("Zombie mode", &s.zombies));
    m.AddToggle("Maniac traffic", &s.maniacTraffic, onOff("Maniac traffic", &s.maniacTraffic));
    m.AddAction("Launch nearby cars", [] {
        const std::vector<Vehicle> cars = NearbyCars(80.0f);
        for (Vehicle v : cars) LaunchCar(v);
        ShowStatus("Launched " + std::to_string(cars.size()) + " cars");
    });
    m.AddAction("Flip nearby cars", [] {
        const std::vector<Vehicle> cars = NearbyCars(80.0f);
        for (Vehicle v : cars) FlipCar(v);
        ShowStatus("Flipped " + std::to_string(cars.size()) + " cars");
    });
    m.AddToggle("Cars randomly launch", &s.randomLaunches, onOff("Random launches", &s.randomLaunches));
    m.AddToggle("Black hole (where you're looking)", &s.blackHole, [] {
        if (s.blackHole) { StartBlackHole(); ShowStatus("Black hole opened"); }
        else ShowStatus("Black hole closed");
    });
    m.AddToggle("Tornado", &s.tornado, onOff("Tornado", &s.tornado));
    m.AddAction("Stop all chaos", StopAll);
}

void Tick() {
    g_now = GetTickCount64();
    g_dt = Clamp(GAMEPLAY::GET_FRAME_TIME(), 0.001f, 0.1f);
    g_player = PLAYER::PLAYER_ID();
    g_me = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(g_me)) return;
    g_myVeh = PED::IS_PED_IN_ANY_VEHICLE(g_me, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(g_me, FALSE) : 0;

    const bool needCars = s.onePunch || s.maniacTraffic || s.randomLaunches || s.blackHole || s.tornado || !g_maniacs.empty();
    const bool needPeds = s.onePunch || s.hostile || s.zombies || s.blackHole || s.tornado;
    const bool needProps = s.blackHole || s.tornado;
    g_nVeh = needCars ? worldGetAllVehicles(g_veh, 1024) : 0;
    g_nPed = needPeds ? worldGetAllPeds(g_ped, 1024) : 0;
    g_nObj = needProps ? worldGetAllObjects(g_obj, 1024) : 0;

    if (s.fireBullets) GAMEPLAY::SET_FIRE_AMMO_THIS_FRAME(g_player);
    if (s.explosivePunches) GAMEPLAY::SET_EXPLOSIVE_MELEE_THIS_FRAME(g_player);
    UpdateOnePunch();
    UpdateGravityGun();
    UpdateAirstrikeGun();
    UpdateStrikes();
    UpdateMeteors();
    UpdateCrowd();
    UpdateTraffic();
    UpdateRandomLaunches();
    UpdateFlips();
    UpdateBlackHole();
    UpdateTornado();
    UpdatePushes();
}

}  // namespace chaos
