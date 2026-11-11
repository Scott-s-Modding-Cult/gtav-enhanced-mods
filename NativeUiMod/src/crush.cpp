#include "crush.h"

#include <windows.h>

#include <iterator>
#include <unordered_map>

namespace crush {
namespace {

// ------------------------------------------------------------------ tuning
// Crushing: depth (m) squeezed per second = push speed (m/s) * kCrushRate / stiffness.
// Walking (1.6 m/s) into the side of a sedan pinned on a wall: ~2.5 s to blow it up.
constexpr float kCrushRate = 0.25f;
// A vehicle explodes when squeezed to this fraction of its depth along the push line.
constexpr float kCrushLimitFraction = 0.55f;
// Impacts: crush depth added = speed^2 * kImpactDepth / stiffness.
// 15 m/s (54 km/h) head-on into you ~ 20% crushed; ~35 m/s (125 km/h) ~ destroyed.
constexpr float kImpactDepth = 0.002f;
constexpr float kImpactMinSpeed = 4.0f;   // m/s; slower bumps don't count as crashes
constexpr float kRestitution = 0.12f;     // how much of the impact speed bounces back
constexpr float kEjectSpeed = 15.0f;      // m/s; front-end hits harder than this throw people out
// SET_VEHICLE_DAMAGE strength: crush dents grow with progress, crash dents with speed^2.
constexpr float kCrushDentDamage = 400.0f;
constexpr float kImpactDentPerSpeed2 = 25.0f;
constexpr ULONGLONG kCrushDentEveryMs = 80;
constexpr ULONGLONG kSparksEveryMs = 110;
// People pinned against a wall: health lost per second per m/s of push.
constexpr float kPedCrushPerSpeed = 45.0f;

// Particle effects, all one-shot effects in the base game's core.ypt
// (data\effects\ptfx.rpf\core.ypt; names from gtahash.com/particles).
const char* const kFxAsset = "core";
const char* const kSparkFx = "bang_carmetal";        // car-metal impact: sparks, shards, bolts, smoke
const char* const kDebrisFx = "ent_brk_metal_frag";  // burst of metal fragments
const char* const kGlassFx = "glass_windscreen";     // windscreen shattering outward

// ------------------------------------------------------------------ per-entity state
struct State {
    float depth = 0.0f;          // metres crushed so far
    float limit = 2.0f;          // depth at which it explodes
    int stage = 0;               // damage stages already applied
    bool done = false;           // exploded (or was a wreck already)
    float lastAway = 0.0f;       // away-speed we gave it last time
    ULONGLONG lastShoveMs = 0;
    ULONGLONG lastImpactMs = 0;
    ULONGLONG nextDentMs = 0;
    ULONGLONG nextFxMs = 0;
    float pedDamage = 0.0f;      // fractional ped damage not yet applied
    bool bloodied = false;
};

std::unordered_map<Entity, State> g_states;
ULONGLONG g_now = 0;
ULONGLONG g_shieldUntil = 0;
ULONGLONG g_nextPruneMs = 0;
unsigned g_rng = 0x9E3779B9u;

float Rand(float lo, float hi) {  // xorshift, plenty for jittering dents
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return lo + (hi - lo) * (g_rng & 0xFFFFFF) / float(0xFFFFFF);
}

ULONGLONG Now() { return GetTickCount64(); }

// ------------------------------------------------------------------ vehicle knowledge
// How hard a vehicle is to squash, by vehicle class. Bigger = slower to crush.
float Stiffness(Vehicle v) {
    switch (VEHICLE::GET_VEHICLE_CLASS(v)) {
        case 8: case 13: return 0.5f;                              // motorcycles, bicycles
        case 14: case 15: case 16: return 0.9f;                    // boats, helis, planes
        case 2: case 4: case 9: case 12: case 17: case 18: return 1.25f;  // SUVs, muscle, off-road, vans, service, emergency
        case 10: case 11: case 20: return 1.7f;                    // industrial, utility, commercial
        case 19: return 3.0f;                                      // military (tanks take a while)
        default: return 1.0f;                                      // cars
    }
}

bool IsBike(Vehicle v) {
    const Hash m = ENTITY::GET_ENTITY_MODEL(v);
    return VEHICLE::IS_THIS_MODEL_A_BIKE(m) || VEHICLE::IS_THIS_MODEL_A_BICYCLE(m);
}

// Window / door / wheel indices on each face.
// Windows: 0 FL, 1 FR, 2 RL, 3 RR, 6 windscreen, 7 rear screen. Doors: 0-3 as windows, 4 bonnet,
// 5 boot. Wheels: 0 FL, 1 FR, 2 ML, 3 MR, 4 RL, 5 RR.
struct Parts { int windows[2]; int doors[2]; int wheels[3]; };
const Parts kParts[4] = {
    /* Front */ {{6, -1}, {4, -1}, {0, 1, -1}},
    /* Rear  */ {{7, -1}, {5, -1}, {4, 5, -1}},
    /* Left  */ {{0, 2}, {0, 2}, {0, 2, 4}},
    /* Right */ {{1, 3}, {1, 3}, {1, 3, 5}},
};

template <typename F>
void ForEachOccupant(Vehicle v, F fn) {
    const int seats = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(v);
    for (int seat = -1; seat < seats; ++seat) {
        const Ped p = VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, seat);
        if (p && ENTITY::DOES_ENTITY_EXIST(p) && !PED::IS_PED_A_PLAYER(p)) fn(p, seat);
    }
}

void HurtPed(Ped p, int amount, const char* bloodPack) {
    if (ENTITY::IS_ENTITY_DEAD(p)) return;
    if (bloodPack) PED::APPLY_PED_DAMAGE_PACK(p, const_cast<char*>(bloodPack), 0.0f, 1.0f);
    PED::APPLY_DAMAGE_TO_PED(p, amount, FALSE);
}

// ------------------------------------------------------------------ effects
bool FxReady() {
    if (STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(const_cast<char*>(kFxAsset))) return true;
    STREAMING::REQUEST_NAMED_PTFX_ASSET(const_cast<char*>(kFxAsset));
    return false;
}

void Fx(const char* name, V3 at, V3 dir, float scale) {
    if (!FxReady()) return;
    const float heading = std::atan2(-dir.x, dir.y) * 57.2958f;
    GRAPHICS::_SET_PTFX_ASSET_NEXT_CALL(const_cast<char*>(kFxAsset));
    GRAPHICS::START_PARTICLE_FX_NON_LOOPED_AT_COORD(const_cast<char*>(name), at.x, at.y, at.z,
                                                    0.0f, 0.0f, heading, scale, FALSE, FALSE, FALSE);
}

void Shake(const char* camShake, float camAmount, int padMs, int padStrength) {
    if (camShake) CAM::SHAKE_GAMEPLAY_CAM(const_cast<char*>(camShake), camAmount);
    CONTROLS::SET_PAD_SHAKE(0, padMs, padStrength);
}

// ------------------------------------------------------------------ deformation
// Damage sphere starts just outside the surface and travels into the body (focusOnModel).
void Dent(Vehicle v, V3 surface, V3 outward, float damage, float radius) {
    const V3 p = surface + outward * 0.3f;
    VEHICLE::SET_VEHICLE_DAMAGE(v, p.x, p.y, p.z, damage, radius, TRUE);
}

// A point on the face being pushed, jittered sideways/vertically so dents spread over the panel
// instead of drilling one hole. `spread` 0..1 = how far across the face to wander.
V3 JitterOnFace(const VehicleContact& c, V3 point, float spread) {
    const V3 perp{-c.localInto.y, c.localInto.x, 0.0f};
    const float halfFace = 0.5f * (std::fabs(perp.x) * (c.mx.x - c.mn.x) +
                                   std::fabs(perp.y) * (c.mx.y - c.mn.y));
    V3 p = point + perp * (Rand(-1.0f, 1.0f) * halfFace * spread);
    const float h = c.mx.z - c.mn.z;
    p.z = Rand(c.mn.z + 0.25f * h, c.mx.z - 0.1f * h);
    p.x = Clamp(p.x, c.mn.x, c.mx.x);
    p.y = Clamp(p.y, c.mn.y, c.mx.y);
    return p;
}

// Squeeze from both ends: your side, the far side (whatever it's pinned against), and once it's
// well on its way, the roof buckles.
void CrushDents(const VehicleContact& c, float f) {
    const float dmg = kCrushDentDamage * (0.5f + f);
    const float radius = 0.7f + 0.6f * f;
    const V3 outNear = c.localInto * -1.0f;
    const float spread = 0.3f + 0.6f * f;
    // Near face, pushed in as far as it's been crushed so far.
    const float sink = Clamp(c.thickness * f * kCrushLimitFraction, 0.0f, c.thickness * 0.6f);
    Dent(c.veh, JitterOnFace(c, c.localPoint + c.localInto * sink, spread), outNear, dmg, radius);
    // Far face (the wall side).
    const V3 farFace = c.localPoint + c.localInto * c.thickness;
    Dent(c.veh, JitterOnFace(c, farFace, spread), c.localInto, dmg * 0.7f, radius);
    // Roof buckling.
    if (f > 0.45f) {
        const V3 top{Rand(c.mn.x * 0.5f, c.mx.x * 0.5f), Rand(c.mn.y * 0.4f, c.mx.y * 0.4f), c.mx.z};
        Dent(c.veh, top, V3{0.0f, 0.0f, 1.0f}, dmg * 0.6f, radius);
    }
}

// ------------------------------------------------------------------ damage stages
// Thresholds on crush fraction; everything up to the current fraction is applied, so a big crash
// can jump straight through several stages at once.
constexpr float kStageAt[] = {0.08f, 0.20f, 0.35f, 0.55f, 0.80f};

void ApplyStage(const VehicleContact& c, int stage) {
    const Vehicle v = c.veh;
    const Parts& side = kParts[c.side];
    switch (stage) {
        case 0: {  // first creak: glass on this side goes, the alarm or the horn goes off
            for (int w : side.windows) if (w >= 0) VEHICLE::SMASH_VEHICLE_WINDOW(v, w);
            const Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1);
            if (driver && !ENTITY::IS_ENTITY_DEAD(driver)) {
                VEHICLE::START_VEHICLE_HORN(v, 4000, GAMEPLAY::GET_HASH_KEY(const_cast<char*>("HELDDOWN")), FALSE);
            } else {
                VEHICLE::SET_VEHICLE_ALARM(v, TRUE);
                VEHICLE::START_VEHICLE_ALARM(v);
            }
            Shake("JOLT_SHAKE", 0.2f, 120, 120);
            break;
        }
        case 1:  // tyres on this side pop
            for (int t : side.wheels) if (t >= 0) VEHICLE::SET_VEHICLE_TYRE_BURST(v, t, FALSE, 1000.0f);
            Shake("JOLT_SHAKE", 0.25f, 150, 150);
            break;
        case 2:  // doors on this side tear off, all glass gone, people inside get hurt
            for (int d : side.doors) if (d >= 0) VEHICLE::SET_VEHICLE_DOOR_BROKEN(v, d, FALSE);
            for (int w = 0; w <= 7; ++w) VEHICLE::SMASH_VEHICLE_WINDOW(v, w);
            Fx(kGlassFx, ToWorld(v, {0.0f, 0.0f, c.mx.z}), V3{0.0f, 0.0f, 1.0f}, 1.0f);
            ForEachOccupant(v, [](Ped p, int) { HurtPed(p, 25, "BigHitByVehicle"); });
            Fx(kDebrisFx, c.worldPoint, c.worldInto * -1.0f, 1.2f);
            Shake("JOLT_SHAKE", 0.35f, 200, 180);
            break;
        case 3:  // every tyre down to the rim, bonnet and boot off, it's not driving anywhere
            for (int t = 0; t <= 5; ++t) VEHICLE::SET_VEHICLE_TYRE_BURST(v, t, TRUE, 1000.0f);
            VEHICLE::SET_VEHICLE_DOOR_BROKEN(v, 4, FALSE);
            VEHICLE::SET_VEHICLE_DOOR_BROKEN(v, 5, FALSE);
            VEHICLE::SET_VEHICLE_UNDRIVEABLE(v, TRUE);
            ForEachOccupant(v, [](Ped p, int) { HurtPed(p, 60, "BigHitByVehicle"); });
            Fx(kDebrisFx, c.worldPoint, c.worldInto * -1.0f, 1.5f);
            Shake("JOLT_SHAKE", 0.45f, 250, 220);
            break;
        case 4:  // nobody inside survives this
            ForEachOccupant(v, [](Ped p, int) {
                PED::APPLY_PED_DAMAGE_PACK(p, const_cast<char*>("BigHitByVehicle"), 0.0f, 1.0f);
                ENTITY::SET_ENTITY_HEALTH(p, 0);
            });
            Shake("JOLT_SHAKE", 0.5f, 250, 255);
            break;
    }
}

// Health follows the crush: body wrecked steadily, engine smokes (<300) then burns (<0),
// fuel tank starts leaking/burning late. Only ever lowers health, never repairs.
void ApplyHealth(Vehicle v, float f) {
    const float body = 1000.0f * (1.0f - f);
    if (VEHICLE::GET_VEHICLE_BODY_HEALTH(v) > body) VEHICLE::SET_VEHICLE_BODY_HEALTH(v, body);
    if (f > 0.3f) {
        const float engine = Lerp(1000.0f, -3000.0f, Clamp((f - 0.3f) / 0.7f, 0.0f, 1.0f));
        if (VEHICLE::GET_VEHICLE_ENGINE_HEALTH(v) > engine) VEHICLE::SET_VEHICLE_ENGINE_HEALTH(v, engine);
    }
    if (f > 0.55f) {
        const float tank = Lerp(1000.0f, 0.0f, Clamp((f - 0.55f) / 0.45f, 0.0f, 1.0f));
        if (VEHICLE::GET_VEHICLE_PETROL_TANK_HEALTH(v) > tank) VEHICLE::SET_VEHICLE_PETROL_TANK_HEALTH(v, tank);
    }
}

// Returns true if it exploded.
bool Progress(const VehicleContact& c, State& s) {
    const float f = s.depth / s.limit;
    while (s.stage < 5 && f >= kStageAt[s.stage]) ApplyStage(c, s.stage++);
    ApplyHealth(c.veh, f < 1.0f ? f : 1.0f);
    if (f >= 1.0f) {
        VEHICLE::EXPLODE_VEHICLE(c.veh, TRUE, FALSE);
        s.done = true;
        g_shieldUntil = g_now + 2500;
        Shake(nullptr, 0.0f, 500, 255);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ crashes into you
void ThrowThroughWindscreen(const VehicleContact& c, float speed) {
    VEHICLE::SMASH_VEHICLE_WINDOW(c.veh, 6);
    Fx(kGlassFx, ToWorld(c.veh, {0.0f, c.mx.y * 0.35f, c.mx.z}), c.worldInto * -1.0f, 1.2f);
    ForEachOccupant(c.veh, [&](Ped p, int seat) {
        if (seat > 0) return;  // driver and front passenger only
        AI::CLEAR_PED_TASKS_IMMEDIATELY(p);  // pulls them out of the seat
        const V3 at = ToWorld(c.veh, {seat < 0 ? -0.35f : 0.35f, c.mx.y * 0.35f, c.mx.z + 0.35f});
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(p, at.x, at.y, at.z, FALSE, FALSE, FALSE);
        PED::SET_PED_TO_RAGDOLL(p, 5000, 8000, 0, FALSE, FALSE, FALSE);
        SetVel(p, c.velBefore * 0.85f + V3{0.0f, 0.0f, 2.0f + speed * 0.15f});
        HurtPed(p, static_cast<int>(speed * 3.0f), "BigHitByVehicle");
    });
}

void Crash(const VehicleContact& c, State& s, float speed, Response& r) {
    const float stiff = Stiffness(c.veh);
    s.depth += speed * speed * kImpactDepth / stiff;
    s.lastImpactMs = g_now;

    // Crumple the face that hit: a few dents spread across it, harder with speed.
    const float sev = Clamp((speed - kImpactMinSpeed) / 30.0f, 0.0f, 1.0f);
    const int dents = 2 + static_cast<int>(sev * 4.0f);
    const float dmg = Clamp(kImpactDentPerSpeed2 * speed * speed, 200.0f, 20000.0f);
    for (int i = 0; i < dents; ++i)
        Dent(c.veh, JitterOnFace(c, c.localPoint, 0.2f + 0.6f * sev), c.localInto * -1.0f, dmg,
             0.8f + 0.8f * sev);

    Fx(kSparkFx, c.worldPoint, c.worldInto * -1.0f, 0.8f + sev);
    if (sev > 0.3f) Fx(kDebrisFx, c.worldPoint, c.worldInto * -1.0f, 1.0f + sev);
    Shake("JOLT_SHAKE", 0.15f + sev * 0.8f, 150 + static_cast<int>(sev * 350),
          static_cast<int>(100 + sev * 155));

    // Riders come off bikes; car occupants hurt, and at speed the front row goes through the glass.
    if (IsBike(c.veh)) {
        ForEachOccupant(c.veh, [&](Ped p, int) {
            PED::KNOCK_PED_OFF_VEHICLE(p);
            HurtPed(p, static_cast<int>(speed * 2.0f), "BigHitByVehicle");
        });
    } else if (c.side == Front && speed > kEjectSpeed) {
        ThrowThroughWindscreen(c, speed);
    } else if (speed > 8.0f) {
        ForEachOccupant(c.veh, [&](Ped p, int) { HurtPed(p, static_cast<int>(speed * 1.5f), nullptr); });
    }

    r.bounce = speed * kRestitution;
    r.lift = Clamp(speed * 0.06f, 0.0f, 3.0f);
}

}  // namespace

// ------------------------------------------------------------------ public
Response OnVehicleContact(const VehicleContact& c, float dt) {
    Response r;
    g_now = Now();
    State& s = g_states[c.veh];
    if (s.done || ENTITY::IS_ENTITY_DEAD(c.veh)) { s.done = true; return r; }

    const float closing = c.playerInto - c.boxAway;
    const bool freshContact = g_now - s.lastShoveMs > 300;

    // Crash: it came at us (or we came at it) fast, not as part of an ongoing push.
    if (closing > kImpactMinSpeed && freshContact && g_now - s.lastImpactMs > 400) {
        s.limit = Clamp(c.thickness * kCrushLimitFraction, 0.5f, 7.0f);
        Crash(c, s, closing, r);
        if (Progress(c, s)) r.skipShove = true;
        return r;
    }

    // Crush: we're pushing and last frame's shove didn't move it -> it's pinned against something.
    if (c.playerInto > 0.3f && !freshContact && s.lastAway > 0.3f) {
        const float blocked = Clamp(1.0f - c.boxAway / s.lastAway, 0.0f, 1.0f);
        if (blocked > 0.5f) {
            s.limit = Clamp(c.thickness * kCrushLimitFraction, 0.5f, 7.0f);
            s.depth += c.playerInto * blocked * kCrushRate * dt / Stiffness(c.veh);
            g_shieldUntil = g_now + 600;
            const float f = s.depth / s.limit;
            if (g_now >= s.nextDentMs) {
                s.nextDentMs = g_now + kCrushDentEveryMs;
                CrushDents(c, f < 1.0f ? f : 1.0f);
                CONTROLS::SET_PAD_SHAKE(0, 90, static_cast<int>(60 + 120 * Clamp(f, 0.0f, 1.0f)));
            }
            if (g_now >= s.nextFxMs) {
                s.nextFxMs = g_now + kSparksEveryMs;
                Fx(kSparkFx, c.worldPoint, c.worldInto * -1.0f, 0.6f + 0.6f * Clamp(f, 0.0f, 1.0f));
                CAM::SHAKE_GAMEPLAY_CAM(const_cast<char*>("VIBRATE_SHAKE"), 0.06f + 0.12f * Clamp(f, 0.0f, 1.0f));
            }
            if (Progress(c, s)) r.skipShove = true;
        }
    }
    return r;
}

bool OnPedContact(Ped ped, float playerInto, float pedAway, bool solidBehind, float dt) {
    g_now = Now();
    State& s = g_states[ped];
    const bool recent = g_now - s.lastShoveMs < 300;
    if (!solidBehind || !recent || s.lastAway < 0.3f || playerInto < 0.3f) return false;
    const float blocked = Clamp(1.0f - pedAway / s.lastAway, 0.0f, 1.0f);
    if (blocked < 0.5f) return false;

    // Pinned against something solid: squeeze the life out of them.
    if (!ENTITY::IS_ENTITY_DEAD(ped)) {
        if (!s.bloodied) {
            PED::APPLY_PED_DAMAGE_PACK(ped, const_cast<char*>("BigHitByVehicle"), 0.0f, 1.0f);
            s.bloodied = true;
            Shake("JOLT_SHAKE", 0.15f, 120, 120);
        }
        s.pedDamage += playerInto * blocked * kPedCrushPerSpeed * dt;
        const int whole = static_cast<int>(s.pedDamage);
        if (whole > 0) {
            PED::APPLY_DAMAGE_TO_PED(ped, whole, FALSE);
            s.pedDamage -= whole;
        }
    }
    return true;
}

void NoteShove(Entity e, float awaySpeed) {
    State& s = g_states[e];
    s.lastAway = awaySpeed;
    s.lastShoveMs = Now();
}

void Tick() {
    g_now = Now();
    if (g_now < g_nextPruneMs) return;
    g_nextPruneMs = g_now + 2000;
    FxReady();  // keep the effect asset requested so the first sparks aren't skipped
    for (auto it = g_states.begin(); it != g_states.end();) {
        // Forget deleted entities (handles get reused) and untouched ones with nothing to remember.
        const bool gone = !ENTITY::DOES_ENTITY_EXIST(it->first);
        const bool stale = g_now - it->second.lastShoveMs > 5000 && it->second.depth <= 0.0f;
        it = (gone || stale) ? g_states.erase(it) : std::next(it);
    }
}

bool ShieldPlayer() { return Now() < g_shieldUntil; }

}  // namespace crush
