#include "cheats.h"

#include <windows.h>

#include <cmath>
#include <cstdio>

#include "main.h"     // worldGetAllVehicles / Peds / Objects
#include "natives.h"
#include "crush.h"
#include "menu.h"     // ShowStatus
#include "vec.h"
#include "vehextra.h"  // rocket boost / nitrous natives

namespace cheats {

Settings g;

namespace {

// ------------------------------------------------------------------ on-foot movement
// The game's own on-foot speeds (m/s) — used as the baseline the multiplier scales, so the
// result never compounds frame over frame.
constexpr float kWalkSpeed = 1.6f;
constexpr float kRunSpeed = 5.0f;
constexpr float kSprintSpeed = 7.0f;

bool OnFootGrounded(Ped ped) {
    return !PED::IS_PED_IN_ANY_VEHICLE(ped, TRUE) && !PED::IS_PED_RAGDOLL(ped) &&
           !PED::IS_PED_SWIMMING(ped) && !PED::IS_PED_CLIMBING(ped) && !PED::IS_PED_FALLING(ped) &&
           !PED::IS_PED_JUMPING(ped) && !ENTITY::IS_ENTITY_IN_AIR(ped) &&
           !PED::IS_PED_IN_PARACHUTE_FREE_FALL(ped);
}

// Horizontal velocity the player is *trying* to move at (walk/run/sprint, scaled by super speed).
// Returns false when not walking/running on the ground.
bool IntendedVelocity(Ped ped, V3& out) {
    if (!OnFootGrounded(ped)) return false;
    float base;
    if (AI::IS_PED_SPRINTING(ped)) base = kSprintSpeed;
    else if (AI::IS_PED_RUNNING(ped)) base = kRunSpeed;
    else if (AI::IS_PED_WALKING(ped)) base = kWalkSpeed;
    else return false;
    if (g.superSpeed) base *= g.speedMultiplier;
    // Follow the direction we're actually moving (handles strafing while aiming);
    // fall back to facing direction when nearly stopped.
    V3 dir = Norm2D(Vel(ped));
    if (Len2D(Vel(ped)) < 0.5f) dir = Norm2D(From(ENTITY::GET_ENTITY_FORWARD_VECTOR(ped)));
    out = dir * base;
    return true;
}

// ------------------------------------------------------------------ super speed
// Speeds up the ped's own movement animations instead of pushing the body around, so your legs
// keep up with the ground, you turn as sharply as normal and you go wherever you steer — no
// sliding. The game resets the override every frame, so it has to be set each frame.
bool g_speedApplied = false;

void UpdateSuperSpeed(Ped ped) {
    const bool want = g.superSpeed && !PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE);
    if (want) {
        PED::SET_PED_MOVE_RATE_OVERRIDE(ped, g.speedMultiplier);  // valid range 0-10
    } else if (g_speedApplied) {
        PED::SET_PED_MOVE_RATE_OVERRIDE(ped, 1.0f);
    }
    g_speedApplied = want;
}

// ------------------------------------------------------------------ raycast
// Line of sight test against the map, vehicles and props (synchronous).
bool RayHits(V3 from, V3 to, Entity ignore) {
    const int ray = WORLDPROBE::_CAST_RAY_POINT_TO_POINT(from.x, from.y, from.z, to.x, to.y, to.z,
                                                         1 | 2 | 16 /* map, vehicles, objects */, ignore, 7);
    BOOL hit = FALSE;
    Vector3 end, normal;
    Entity what = 0;
    WORLDPROBE::_GET_RAYCAST_RESULT(ray, &hit, &end, &normal, &what);
    return hit != FALSE;
}

// ------------------------------------------------------------------ super jump
// Uses the game's own super jump — what the HOPTOIT cheat turns on — by calling
// SET_SUPER_JUMP_THIS_FRAME every frame. The game then plays its proper super-jump take-off,
// air and landing animations (hold jump to go higher, as with the cheat), so there's no
// frozen pose mid-air and no fighting the game's jump/fall tasks.
//
// The slider multiplies the cheat's own jump: at 1x it is exactly HOPTOIT. Above 1x we wait
// for the game to finish its take-off push (upward speed stops rising), then scale that
// upward speed once. Nothing touches the jump after that.
bool g_jumpBoosted = false;            // in the air from a super jump, not landed yet
ULONGLONG g_jumpProtectUntil = 0;      // keep fall protection a moment after landing
bool g_takeoff = false;                // watching the take-off push to find its peak
float g_takeoffPrevVz = 0.0f;
ULONGLONG g_takeoffStartMs = 0;

void UpdateSuperJump(Player player, Ped ped) {
    const bool airborne = PED::IS_PED_JUMPING(ped) || ENTITY::IS_ENTITY_IN_AIR(ped) ||
                          PED::IS_PED_FALLING(ped) || PED::IS_PED_IN_PARACHUTE_FREE_FALL(ped);
    if (g_jumpBoosted && !airborne) {  // landed
        g_jumpBoosted = false;
        g_takeoff = false;
        g_jumpProtectUntil = GetTickCount64() + 1500;
    }
    if (!g.superJump) return;

    if (PED::IS_PED_IN_ANY_VEHICLE(ped, TRUE)) return;  // on foot only
    GAMEPLAY::SET_SUPER_JUMP_THIS_FRAME(player);       // the HOPTOIT cheat

    const V3 v = Vel(ped);
    if (!g_jumpBoosted) {
        // Leaving the ground on a jump.
        if (!PED::IS_PED_JUMPING(ped) || v.z < 1.0f) return;
        g_jumpBoosted = true;
        g_takeoff = g.jumpMultiplier > 1.0f;
        g_takeoffPrevVz = v.z;
        g_takeoffStartMs = GetTickCount64();
        return;
    }
    if (!g_takeoff) return;
    // The game's push is over once upward speed stops climbing (or after a short safety window).
    const bool peaked = v.z <= g_takeoffPrevVz || GetTickCount64() - g_takeoffStartMs > 300;
    if (peaked) {
        const float vz = v.z > g_takeoffPrevVz ? v.z : g_takeoffPrevVz;
        SetVel(ped, {v.x, v.y, vz * g.jumpMultiplier});
        g_takeoff = false;
    } else {
        g_takeoffPrevVz = v.z;
    }
}

bool JumpProtectionActive() {
    return g_jumpBoosted || GetTickCount64() < g_jumpProtectUntil;
}

// ------------------------------------------------------------------ dumptruck mode
// Every frame, look at things touching (or about to touch) the player. Along the contact normal,
// nothing is allowed to move toward the player faster than the player moves toward it:
//  - walk into a car  -> the car is shoved along at your speed, you don't slow down
//  - a car drives into you -> it stops dead against you
// What that does to the things involved (crushing, crashes) lives in crush.cpp.
constexpr float kPlayerRadius = 0.35f;
constexpr float kPedRadius = 0.35f;
constexpr float kShoveGap = 0.6f;  // extra m/s so the pushed thing stays just ahead of you

// Distance from (c) along direction (d) to where it leaves the slab [lo, hi].
float SlabExit(float c, float d, float lo, float hi) {
    if (std::fabs(d) < 1e-4f) return 1e9f;
    return ((d > 0.0f ? hi : lo) - c) / d;
}

// Box-shaped entity (vehicle or prop). `push` = how fast the player is moving horizontally.
void ShoveBox(Entity e, bool isVehicle, V3 playerPos, V3 push, float dt) {
    const V3 c = Pos(e);
    if (Len2D(c - playerPos) > 30.0f || std::fabs(c.z - playerPos.z) > 15.0f) return;
    if (isVehicle && VEHICLE::GET_VEHICLE_CLASS(e) == 21) return;  // trains run on rails

    Vector3 mnRaw, mxRaw;
    GAMEPLAY::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(e), &mnRaw, &mxRaw);
    const V3 mn = From(mnRaw), mx = From(mxRaw);
    const V3 local = ToLocal(e, playerPos);

    // Player body spans ~1 m below to ~0.8 m above the ped origin (pelvis).
    const float feet = local.z - 1.0f, head = local.z + 0.8f;
    if (feet > mx.z - 0.25f) return;  // standing on top of it
    if (head < mn.z) return;          // underneath it (wing, trailer bed...)

    // Closest point of the box footprint to the player.
    const V3 cl{Clamp(local.x, mn.x, mx.x), Clamp(local.y, mn.y, mx.y), Clamp(local.z, mn.z, mx.z)};
    V3 outL{local.x - cl.x, local.y - cl.y, 0.0f};  // vehicle space, box -> player
    float dist = Len2D(outL);
    if (dist < 1e-3f) {  // inside the footprint: push out from the centre
        outL = {local.x, local.y, 0.0f};
        dist = 0.0f;
    }
    outL = Norm2D(outL);
    if (outL.x == 0.0f && outL.y == 0.0f) return;

    // Contact normal in world space (horizontal only).
    const V3 a = ToWorld(e, cl);
    const V3 n = Norm2D(ToWorld(e, cl + outL) - a);
    if (n.x == 0.0f && n.y == 0.0f) return;
    const V3 into = n * -1.0f;  // direction the player pushes it

    const V3 ev = Vel(e);
    const float playerInto = Dot(push, into);  // player's speed toward the box
    const float boxAway = Dot(ev, into);       // box's speed away from the player
    const float closing = playerInto - boxAway;
    if (closing <= 0.0f) return;

    // Look ahead by how far the gap closes this frame, so fast cars are caught before impact.
    const float gap = dist - kPlayerRadius;
    if (gap > 0.15f + closing * dt * 2.0f) return;

    crush::Response r;
    if (isVehicle) {
        crush::VehicleContact vc;
        vc.veh = e;
        vc.mn = mn; vc.mx = mx;
        vc.localPoint = cl;
        vc.localInto = outL * -1.0f;
        vc.worldPoint = a;
        vc.worldInto = into;
        vc.side = std::fabs(outL.x) > std::fabs(outL.y) ? (outL.x < 0.0f ? crush::Left : crush::Right)
                                                         : (outL.y > 0.0f ? crush::Front : crush::Rear);
        vc.thickness = std::fmin(SlabExit(cl.x, vc.localInto.x, mn.x, mx.x),
                                 SlabExit(cl.y, vc.localInto.y, mn.y, mx.y));
        vc.velBefore = ev;
        vc.playerInto = playerInto;
        vc.boxAway = boxAway;
        r = crush::OnVehicleContact(vc, dt);
        if (r.skipShove) return;
    }

    const float away = (playerInto > 0.2f ? playerInto + kShoveGap : 0.0f) + r.bounce;
    SetVel(e, ev + into * (away - boxAway) + V3{0.0f, 0.0f, r.lift});
    crush::NoteShove(e, away);
}

// Is there something solid right behind this ped (in the push direction)?
bool SolidBehind(Ped p, V3 at, V3 into) { return RayHits(at, at + into * 0.75f, p); }

void ShovePed(Ped other, V3 playerPos, V3 push, float dt) {
    if (PED::IS_PED_IN_ANY_VEHICLE(other, FALSE)) return;  // their vehicle gets shoved instead
    const V3 c = Pos(other);
    if (std::fabs(c.z - playerPos.z) > 1.5f) return;
    const V3 d = c - playerPos;
    const float dist = Len2D(d);
    if (dist > 4.0f) return;
    const V3 into = dist > 1e-3f ? Norm2D(d) : Norm2D(push);

    // Only when you walk into them — peds bumping into a standing player just get blocked.
    const V3 ev = Vel(other);
    const float playerInto = Dot(push, into), pedAway = Dot(ev, into);
    if (playerInto < 0.5f) return;
    const float closing = playerInto - pedAway;
    if (closing <= 0.5f) return;
    if (dist - kPlayerRadius - kPedRadius > 0.1f + closing * dt * 2.0f) return;

    // Pinned = last shove didn't move them AND there's actually a wall/car/prop behind them.
    const bool pinned = crush::OnPedContact(other, playerInto, pedAway, SolidBehind(other, c, into), dt);
    if (!PED::IS_PED_RAGDOLL(other)) PED::SET_PED_TO_RAGDOLL(other, 1500, 3000, 0, FALSE, FALSE, FALSE);
    const float away = playerInto + 1.5f;
    SetVel(other, ev + into * (away - pedAway) + V3{0.0f, 0.0f, pinned ? 0.0f : 1.0f});
    crush::NoteShove(other, away);
}

int g_handles[1024];

void UpdateDumptruck(Ped ped) {
    if (!g.dumptruck || PED::IS_PED_IN_ANY_VEHICLE(ped, TRUE)) return;

    const float dt = Clamp(GAMEPLAY::GET_FRAME_TIME(), 0.005f, 0.1f);
    const V3 pos = Pos(ped);
    // Push with whichever is bigger: how fast we're moving, or how fast we're *trying* to move
    // (the physics contact itself can slow the capsule for a frame).
    V3 push = Flat(Vel(ped));
    V3 want;
    if (IntendedVelocity(ped, want) && Len2D(want) > Len2D(push)) push = want;

    int n = worldGetAllVehicles(g_handles, 1024);
    for (int i = 0; i < n; ++i) ShoveBox(g_handles[i], true, pos, push, dt);

    n = worldGetAllObjects(g_handles, 1024);
    for (int i = 0; i < n; ++i) {
        // Skip weapons/phones/props held by peds, and frozen world props (setting velocity
        // on those does nothing anyway).
        if (ENTITY::IS_ENTITY_ATTACHED(g_handles[i])) continue;
        ShoveBox(g_handles[i], false, pos, push, dt);
    }

    n = worldGetAllPeds(g_handles, 1024);
    for (int i = 0; i < n; ++i)
        if (g_handles[i] != ped) ShovePed(g_handles[i], pos, push, dt);
}

// ------------------------------------------------------------------ god mode / proofs / ragdoll
// Each of these only touches the game's state while we want it on, plus once when switching off,
// so missions that set them themselves aren't fought every frame.
bool g_wasInvincible = false;
Ped g_proofsPed = 0;
bool g_proofsGod = false, g_proofsDump = false, g_proofsShield = false;
bool g_wasNoRagdoll = false;

void UpdateProtection(Player player, Ped ped) {
    // Invincibility: god mode, or briefly during/after a super jump so the landing can't kill you.
    const bool invincible = g.godMode || JumpProtectionActive();
    if (invincible) {
        PLAYER::SET_PLAYER_INVINCIBLE(player, TRUE);
        if (g.godMode) {
            const int maxHp = ENTITY::GET_ENTITY_MAX_HEALTH(ped);
            if (ENTITY::GET_ENTITY_HEALTH(ped) < maxHp) ENTITY::SET_ENTITY_HEALTH(ped, maxHp);
        }
    } else if (g_wasInvincible) {
        PLAYER::SET_PLAYER_INVINCIBLE(player, FALSE);
    }
    g_wasInvincible = invincible;

    // Proofs: god mode = everything; dumptruck = immune to collisions (cars hitting you), plus
    // fire/explosion-proof only while you're crushing something and just after it blows up.
    const bool god = g.godMode, dump = g.dumptruck;
    const bool shield = dump && crush::ShieldPlayer();
    const bool changed = ped != g_proofsPed ? (god || dump || shield)
                                            : (god != g_proofsGod || dump != g_proofsDump || shield != g_proofsShield);
    if (changed) {
        ENTITY::SET_ENTITY_PROOFS(ped, god, god || shield, god || shield, god || dump, god, god, god, god);
    }
    g_proofsPed = ped; g_proofsGod = god; g_proofsDump = dump; g_proofsShield = shield;

    // No ragdoll: dumptruck (nothing knocks you over) and super-jump landings.
    const bool noRagdoll = g.dumptruck || JumpProtectionActive();
    if (noRagdoll) {
        PED::SET_PED_CAN_RAGDOLL(ped, FALSE);
        PED::SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE(ped, 1);  // 1 = never
    } else if (g_wasNoRagdoll) {
        PED::SET_PED_CAN_RAGDOLL(ped, TRUE);
        PED::SET_PED_CAN_BE_KNOCKED_OFF_VEHICLE(ped, 0);
    }
    g_wasNoRagdoll = noRagdoll;
}

// ------------------------------------------------------------------ infinite ammo
// Infinite clip = no reloading and the magazine never empties (covers throwables too). The
// weapon in your hands is also kept topped up to its max, so the reserve shows full and stays
// full if you turn the cheat off. Set every frame so it survives character/model switches.
bool g_ammoApplied = false;

void UpdateInfiniteAmmo(Ped ped) {
    if (!g.infiniteAmmo) {
        if (g_ammoApplied) WEAPON::SET_PED_INFINITE_AMMO_CLIP(ped, FALSE);
        g_ammoApplied = false;
        return;
    }
    WEAPON::SET_PED_INFINITE_AMMO_CLIP(ped, TRUE);
    g_ammoApplied = true;
    Hash weapon = 0;
    if (!WEAPON::GET_CURRENT_PED_WEAPON(ped, &weapon, TRUE)) return;
    int maxAmmo = 0;
    if (WEAPON::GET_MAX_AMMO(ped, weapon, &maxAmmo) && WEAPON::GET_AMMO_IN_PED_WEAPON(ped, weapon) < maxAmmo)
        WEAPON::SET_PED_AMMO(ped, weapon, maxAmmo);
}

// ------------------------------------------------------------------ god mode: your vehicle
// While god mode is on, whatever you're in can't be damaged, dented, burst, set on fire or blown
// up, and any damage that slips through is repaired. When you get out (or turn god mode off) the
// vehicle goes back to normal; its bulletproof-tyres setting is restored to what it was.
Vehicle g_godVehicle = 0;
bool g_godVehTyresCouldBurst = true;

void ProtectVehicle(Vehicle v, bool on) {
    ENTITY::SET_ENTITY_INVINCIBLE(v, on);
    ENTITY::SET_ENTITY_PROOFS(v, on, on, on, on, on, on, on, on);
    VEHICLE::SET_VEHICLE_CAN_BE_VISIBLY_DAMAGED(v, !on);
    VEHICLE::SET_VEHICLE_CAN_BREAK(v, !on);
    VEHICLE::SET_VEHICLE_WHEELS_CAN_BREAK(v, !on);
    VEHICLE::SET_VEHICLE_ENGINE_CAN_DEGRADE(v, !on);
    VEHICLE::SET_VEHICLE_EXPLODES_ON_HIGH_EXPLOSION_DAMAGE(v, !on);
    VEHICLE::SET_DISABLE_VEHICLE_PETROL_TANK_DAMAGE(v, on);
    VEHICLE::SET_DISABLE_VEHICLE_PETROL_TANK_FIRES(v, on);
    VEHICLE::SET_VEHICLE_TYRES_CAN_BURST(v, on ? FALSE : g_godVehTyresCouldBurst);
}

void UpdateVehicleGod(Ped ped) {
    const Vehicle now = g.godMode && PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) : 0;
    if (now != g_godVehicle) {
        if (g_godVehicle && ENTITY::DOES_ENTITY_EXIST(g_godVehicle)) ProtectVehicle(g_godVehicle, false);
        g_godVehicle = now;
        if (now) {
            g_godVehTyresCouldBurst = VEHICLE::GET_VEHICLE_TYRES_CAN_BURST(now) != FALSE;
            ProtectVehicle(now, true);
        }
    }
    if (!now) return;
    // Belt and braces: undo anything that got through (scripted damage, a crash before proofs).
    if (VEHICLE::_IS_VEHICLE_DAMAGED(now) || VEHICLE::GET_VEHICLE_ENGINE_HEALTH(now) < 1000.0f ||
        VEHICLE::GET_VEHICLE_BODY_HEALTH(now) < 1000.0f || VEHICLE::GET_VEHICLE_PETROL_TANK_HEALTH(now) < 1000.0f) {
        VEHICLE::SET_VEHICLE_FIXED(now);
        VEHICLE::SET_VEHICLE_DEFORMATION_FIXED(now);
        VEHICLE::SET_VEHICLE_ENGINE_HEALTH(now, 1000.0f);
        VEHICLE::SET_VEHICLE_BODY_HEALTH(now, 1000.0f);
        VEHICLE::SET_VEHICLE_PETROL_TANK_HEALTH(now, 1000.0f);
    }
}

// ------------------------------------------------------------------ vehicle weapons: no cooldown
// A script can't change a vehicle weapon's built-in reload time (tank cannon, Toreador missiles,
// rocket pods...). Instead, while you hold the vehicle's fire button, this keeps firing your
// currently selected vehicle weapon at the chosen rate, on top of the game's own shots: same
// projectile, out of the vehicle's weapon mount points (alternating left/right where there are
// several), toward your crosshair.
constexpr int kVehFireControls[] = {69 /* VEH_ATTACK */, 70 /* VEH_ATTACK2 */,
                                    114 /* VEH_FLY_ATTACK */, 331 /* VEH_FLY_ATTACK2 */};
const char* const kWeaponBones[] = {"weapon_1a", "weapon_1b", "weapon_1c", "weapon_1d",
                                    "weapon_2a", "weapon_2b", "weapon_2c", "weapon_2d"};
ULONGLONG g_nextVehShot = 0;
unsigned g_mountIndex = 0;

V3 Norm3(V3 a) {
    const float l = std::sqrt(Dot(a, a));
    return l > 1e-4f ? a * (1.0f / l) : V3{0.0f, 1.0f, 0.0f};
}

V3 GameplayCamDir() {
    const V3 r = From(CAM::GET_GAMEPLAY_CAM_ROT(2));
    const float pitch = r.x * 0.0174533f, yaw = r.z * 0.0174533f;
    return {-std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
}

// The crosshair's point in the world (skipping your own vehicle), or far ahead along it.
V3 CrosshairPoint(Vehicle own) {
    const V3 cam = From(CAM::GET_GAMEPLAY_CAM_COORD()), dir = GameplayCamDir();
    const V3 farPoint = cam + dir * 800.0f;
    const int ray = WORLDPROBE::_CAST_RAY_POINT_TO_POINT(cam.x, cam.y, cam.z, farPoint.x, farPoint.y, farPoint.z,
                                                         1 | 2 | 8 | 16 | 256, own, 7);
    BOOL hit = FALSE;
    Vector3 end, normal;
    Entity what = 0;
    WORLDPROBE::_GET_RAYCAST_RESULT(ray, &hit, &end, &normal, &what);
    return hit ? From(end) : farPoint;
}

void UpdateVehicleWeapons(Ped ped) {
    if (!g.vehNoCooldown || !PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE)) return;
    const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(ped, FALSE);
    if (!VEHICLE::DOES_VEHICLE_HAVE_WEAPONS(v)) return;
    Hash weapon = 0;
    if (!WEAPON::GET_CURRENT_PED_VEHICLE_WEAPON(ped, &weapon) || !weapon) return;

    bool firing = false;
    for (int c : kVehFireControls) firing = firing || CONTROLS::IS_CONTROL_PRESSED(0, c);
    const ULONGLONG now = GetTickCount64();
    if (!firing || now < g_nextVehShot) return;
    g_nextVehShot = now + static_cast<ULONGLONG>(1000.0f / (g.vehFireRate < 1.0f ? 1.0f : g.vehFireRate));

    // Mount point: the vehicle's weapon bones, taking turns; otherwise its top, toward the aim.
    int bones[sizeof kWeaponBones / sizeof *kWeaponBones];
    int nBones = 0;
    for (const char* name : kWeaponBones) {
        const int b = ENTITY::GET_ENTITY_BONE_INDEX_BY_NAME(v, const_cast<char*>(name));
        if (b != -1) bones[nBones++] = b;
    }
    const V3 target = CrosshairPoint(v);
    V3 from;
    float clearance = 2.0f;  // start the shot clear of your own vehicle
    if (nBones > 0) {
        from = From(ENTITY::GET_WORLD_POSITION_OF_ENTITY_BONE(v, bones[g_mountIndex++ % nBones]));
    } else {
        Vector3 mn, mx;
        GAMEPLAY::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(v), &mn, &mx);
        from = Pos(v) + V3{0.0f, 0.0f, mx.z * 0.8f};
        clearance = (mx.y - mn.y) * 0.5f + 1.5f;
    }
    const V3 dir = Norm3(target - from);
    from = from + dir * clearance;
    GAMEPLAY::SHOOT_SINGLE_BULLET_BETWEEN_COORDS(from.x, from.y, from.z, target.x, target.y, target.z, 250, TRUE,
                                                 weapon, ped, TRUE, FALSE, -1.0f);
}

// ------------------------------------------------------------------ instant ability recharge
// Rocket boost (Oppressor Mk II, Vigilante, Scramjet, Toreador...) refills the moment a boost
// ends, and tuner nitrous is kept topped up. (No native exists to refill car jumps or KERS.)
void UpdateAbilities(Ped ped) {
    if (!g.instantAbilities || !PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE)) return;
    const Vehicle v = PED::GET_VEHICLE_PED_IS_IN(ped, FALSE);
    if (vehx::HasRocketBoost(v)) {
        vehx::SetRocketBoostRechargeTime(v, 0.0f);
        if (!vehx::RocketBoostActive(v)) vehx::SetRocketBoostFill(v, 100.0f);
    }
    if (!vehx::NitrousActive(v)) vehx::FullyChargeNitrous(v);
}

}  // namespace

void Tick() {
    const Player player = PLAYER::PLAYER_ID();
    const Ped ped = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(ped)) return;

    if (g.explosiveBullets) GAMEPLAY::SET_EXPLOSIVE_AMMO_THIS_FRAME(player);
    UpdateInfiniteAmmo(ped);

    if (!ENTITY::IS_ENTITY_DEAD(ped)) {
        UpdateSuperSpeed(ped);
        UpdateSuperJump(player, ped);
        UpdateDumptruck(ped);
    }
    crush::Tick();
    UpdateProtection(player, ped);
    UpdateVehicleGod(ped);
    UpdateVehicleWeapons(ped);
    UpdateAbilities(ped);
}

bool TyresCanBurst(Vehicle v) {
    if (v && v == g_godVehicle) return g_godVehTyresCouldBurst;
    return VEHICLE::GET_VEHICLE_TYRES_CAN_BURST(v) != FALSE;
}

void SetTyresCanBurst(Vehicle v, bool canBurst) {
    if (v && v == g_godVehicle) { g_godVehTyresCouldBurst = canBurst; return; }  // applied on exit
    VEHICLE::SET_VEHICLE_TYRES_CAN_BURST(v, canBurst);
}

// ------------------------------------------------------------------ give all weapons
void GiveAllWeapons() {
    // Unknown names (e.g. DLC weapons this build doesn't have) are skipped via IS_WEAPON_VALID.
    static const char* const kWeapons[] = {
        // melee + tools
        "WEAPON_KNIFE", "WEAPON_DAGGER", "WEAPON_BAT", "WEAPON_BOTTLE", "WEAPON_CROWBAR",
        "WEAPON_FLASHLIGHT", "WEAPON_GOLFCLUB", "WEAPON_HAMMER", "WEAPON_HATCHET",
        "WEAPON_KNUCKLE", "WEAPON_MACHETE", "WEAPON_SWITCHBLADE", "WEAPON_NIGHTSTICK",
        "WEAPON_WRENCH", "WEAPON_BATTLEAXE", "WEAPON_POOLCUE", "WEAPON_STONE_HATCHET",
        "WEAPON_CANDYCANE", "WEAPON_STUNROD",
        // handguns
        "WEAPON_PISTOL", "WEAPON_PISTOL_MK2", "WEAPON_COMBATPISTOL", "WEAPON_APPISTOL",
        "WEAPON_STUNGUN", "WEAPON_PISTOL50", "WEAPON_SNSPISTOL", "WEAPON_SNSPISTOL_MK2",
        "WEAPON_HEAVYPISTOL", "WEAPON_VINTAGEPISTOL", "WEAPON_FLAREGUN", "WEAPON_MARKSMANPISTOL",
        "WEAPON_REVOLVER", "WEAPON_REVOLVER_MK2", "WEAPON_DOUBLEACTION", "WEAPON_RAYPISTOL",
        "WEAPON_CERAMICPISTOL", "WEAPON_NAVYREVOLVER", "WEAPON_GADGETPISTOL", "WEAPON_PISTOLXM3",
        // SMGs
        "WEAPON_MICROSMG", "WEAPON_SMG", "WEAPON_SMG_MK2", "WEAPON_ASSAULTSMG", "WEAPON_COMBATPDW",
        "WEAPON_MACHINEPISTOL", "WEAPON_MINISMG", "WEAPON_RAYCARBINE", "WEAPON_TECPISTOL",
        // shotguns
        "WEAPON_PUMPSHOTGUN", "WEAPON_PUMPSHOTGUN_MK2", "WEAPON_SAWNOFFSHOTGUN",
        "WEAPON_ASSAULTSHOTGUN", "WEAPON_BULLPUPSHOTGUN", "WEAPON_MUSKET", "WEAPON_HEAVYSHOTGUN",
        "WEAPON_DBSHOTGUN", "WEAPON_AUTOSHOTGUN", "WEAPON_COMBATSHOTGUN",
        // rifles
        "WEAPON_ASSAULTRIFLE", "WEAPON_ASSAULTRIFLE_MK2", "WEAPON_CARBINERIFLE",
        "WEAPON_CARBINERIFLE_MK2", "WEAPON_ADVANCEDRIFLE", "WEAPON_SPECIALCARBINE",
        "WEAPON_SPECIALCARBINE_MK2", "WEAPON_BULLPUPRIFLE", "WEAPON_BULLPUPRIFLE_MK2",
        "WEAPON_COMPACTRIFLE", "WEAPON_MILITARYRIFLE", "WEAPON_HEAVYRIFLE", "WEAPON_TACTICALRIFLE",
        "WEAPON_BATTLERIFLE",
        // machine guns
        "WEAPON_MG", "WEAPON_COMBATMG", "WEAPON_COMBATMG_MK2", "WEAPON_GUSENBERG",
        // snipers
        "WEAPON_SNIPERRIFLE", "WEAPON_HEAVYSNIPER", "WEAPON_HEAVYSNIPER_MK2",
        "WEAPON_MARKSMANRIFLE", "WEAPON_MARKSMANRIFLE_MK2", "WEAPON_PRECISIONRIFLE",
        // heavy
        "WEAPON_RPG", "WEAPON_GRENADELAUNCHER", "WEAPON_GRENADELAUNCHER_SMOKE", "WEAPON_MINIGUN",
        "WEAPON_FIREWORK", "WEAPON_RAILGUN", "WEAPON_HOMINGLAUNCHER", "WEAPON_COMPACTLAUNCHER",
        "WEAPON_RAYMINIGUN", "WEAPON_EMPLAUNCHER", "WEAPON_RAILGUNXM3",
        // throwables
        "WEAPON_GRENADE", "WEAPON_BZGAS", "WEAPON_MOLOTOV", "WEAPON_STICKYBOMB", "WEAPON_PROXMINE",
        "WEAPON_SNOWBALL", "WEAPON_PIPEBOMB", "WEAPON_BALL", "WEAPON_SMOKEGRENADE", "WEAPON_FLARE",
        "WEAPON_ACIDPACKAGE",
        // misc / tools
        "WEAPON_PETROLCAN", "WEAPON_HAZARDCAN", "WEAPON_FERTILIZERCAN", "WEAPON_FIREEXTINGUISHER",
        "WEAPON_METALDETECTOR", "WEAPON_DIGISCANNER", "GADGET_PARACHUTE",
        "WEAPON_AIRSTRIKEGUN",  // NativeUiMod add-on weapon (airstrikegun pack), skipped if not loaded
    };

    const Ped ped = PLAYER::PLAYER_PED_ID();
    int given = 0;
    for (const char* name : kWeapons) {
        const Hash h = GAMEPLAY::GET_HASH_KEY(const_cast<char*>(name));
        if (!WEAPON::IS_WEAPON_VALID(h)) continue;
        WEAPON::GIVE_WEAPON_TO_PED(ped, h, 9999, FALSE, FALSE);  // ammo is capped at each gun's max
        ++given;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "Gave %d weapons and tools", given);
    ShowStatus(buf);
}

}  // namespace cheats
