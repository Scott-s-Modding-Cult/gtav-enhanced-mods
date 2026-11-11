#include "population.h"

#include <windows.h>

#include <cmath>
#include <vector>

#include "main.h"  // worldGetAll*
#include "natives.h"
#include "vec.h"

namespace population {

Settings s;

namespace {

// Each +1x on the slider adds about this many people / cars around you.
constexpr float kPedsPerStep = 22.0f;
constexpr float kCarsPerStep = 14.0f;
// Stay well inside the game's pools (256 peds / 300 vehicles) so nothing else runs out of room.
constexpr int kMaxWorldPeds = 200;
constexpr int kMaxWorldCars = 230;
// Spawned people/cars count toward the target while they're within this range of you.
constexpr float kPedRange = 120.0f;
constexpr float kCarRange = 220.0f;
constexpr ULONGLONG kSpawnEveryMs = 150;  // one of each at most per step: no frame hitches

std::vector<Ped> g_peds;      // extra people we added (handed to the game, still tracked)
std::vector<Vehicle> g_cars;  // extra cars we added
ULONGLONG g_nextSpawn = 0;
int g_buf[1024];

float Rand(float lo, float hi) { return GAMEPLAY::GET_RANDOM_FLOAT_IN_RANGE(lo, hi); }
int RandInt(int lo, int hiExclusive) { return GAMEPLAY::GET_RANDOM_INT_IN_RANGE(lo, hiExclusive); }

// Far enough away, and if fairly close, not somewhere you're looking (no popping in).
bool GoodSpot(V3 p, V3 me, float minDist) {
    const float d = Len(p - me);
    if (d < minDist) return false;
    if (d < 80.0f && CAM::IS_SPHERE_VISIBLE(p.x, p.y, p.z, 2.0f)) return false;
    return true;
}

template <typename T>
void Prune(std::vector<T>& list, V3 me, float range) {
    for (size_t i = 0; i < list.size();) {
        const T e = list[i];
        if (!ENTITY::DOES_ENTITY_EXIST(e) || ENTITY::IS_ENTITY_DEAD(e) || Len(Pos(e) - me) > range) {
            list[i] = list.back();
            list.pop_back();
        } else {
            ++i;
        }
    }
}

void SpawnPed(V3 me) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const float ang = Rand(0.0f, 6.2832f), dist = Rand(25.0f, 90.0f);
        const V3 p = me + V3{std::cos(ang) * dist, std::sin(ang) * dist, 0.0f};
        Vector3 out;
        if (!PATHFIND::GET_SAFE_COORD_FOR_PED(p.x, p.y, p.z, TRUE, &out, 16)) continue;  // on a pavement
        const V3 o = From(out);
        if ((o.x == 0.0f && o.y == 0.0f) || !GoodSpot(o, me, 20.0f)) continue;
        Ped ped = PED::CREATE_RANDOM_PED(o.x, o.y, o.z);  // the game picks a ped type for the area
        if (!ped) return;
        AI::TASK_WANDER_STANDARD(ped, 10.0f, 10);
        g_peds.push_back(ped);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);  // now an ordinary ambient pedestrian
        return;
    }
}

// A model that's already loaded and fits the area: copy a random car that's driving around nearby.
Hash PickTrafficModel(V3 me) {
    const int n = worldGetAllVehicles(g_buf, 1024);
    for (int attempt = 0; attempt < 12 && n > 0; ++attempt) {
        const Vehicle v = g_buf[RandInt(0, n)];
        if (ENTITY::IS_ENTITY_A_MISSION_ENTITY(v) || Len(Pos(v) - me) > 250.0f) continue;
        const Hash m = ENTITY::GET_ENTITY_MODEL(v);
        if (VEHICLE::IS_THIS_MODEL_A_HELI(m) || VEHICLE::IS_THIS_MODEL_A_PLANE(m) ||
            VEHICLE::IS_THIS_MODEL_A_BOAT(m) || VEHICLE::IS_THIS_MODEL_A_TRAIN(m))
            continue;
        if (!VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, -1)) continue;  // traffic, not parked cars
        if (STREAMING::HAS_MODEL_LOADED(m)) return m;
    }
    return 0;
}

void SpawnCar(V3 me) {
    const Hash model = PickTrafficModel(me);
    if (!model) return;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const float ang = Rand(0.0f, 6.2832f), dist = Rand(70.0f, 180.0f);
        const V3 p = me + V3{std::cos(ang) * dist, std::sin(ang) * dist, 0.0f};
        Vector3 out;
        float heading = 0.0f;
        Any lanes = 0;
        if (!PATHFIND::GET_NTH_CLOSEST_VEHICLE_NODE_WITH_HEADING(p.x, p.y, p.z, RandInt(1, 4), &out, &heading,
                                                                 &lanes, 0, 3.0f, 0.0f))
            continue;
        const V3 o = From(out);
        if (!GoodSpot(o, me, 60.0f)) continue;
        if (VEHICLE::GET_CLOSEST_VEHICLE(o.x, o.y, o.z, 8.0f, 0, 70)) continue;  // lane is occupied
        Vehicle v = VEHICLE::CREATE_VEHICLE(model, o.x, o.y, o.z, heading, FALSE, TRUE);
        if (!v) return;
        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(v);
        VEHICLE::SET_VEHICLE_ENGINE_ON(v, TRUE, TRUE, FALSE);
        Ped driver = PED::CREATE_RANDOM_PED_AS_DRIVER(v, TRUE);
        if (driver) {
            AI::TASK_VEHICLE_DRIVE_WANDER(driver, v, Rand(10.0f, 20.0f), 786603 /* normal */);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&driver);
        }
        g_cars.push_back(v);
        ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&v);  // ordinary ambient traffic from here on
        return;
    }
}

}  // namespace

void Tick() {
    if (!s.boost) {
        // Everything we added already belongs to the game; just stop tracking it.
        g_peds.clear();
        g_cars.clear();
        return;
    }
    // Keep the game's own population at full strength underneath.
    PED::SET_PED_DENSITY_MULTIPLIER_THIS_FRAME(1.0f);
    PED::SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME(1.0f, 1.0f);
    VEHICLE::SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(1.0f);
    VEHICLE::SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(1.0f);
    VEHICLE::SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(1.0f);

    const ULONGLONG now = GetTickCount64();
    if (now < g_nextSpawn) return;
    g_nextSpawn = now + kSpawnEveryMs;

    const Ped me = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(me)) return;
    const V3 pos = Pos(me);
    Prune(g_peds, pos, kPedRange);
    Prune(g_cars, pos, kCarRange);

    const float extra = s.multiplier - 1.0f;
    const size_t wantPeds = static_cast<size_t>(extra * kPedsPerStep);
    const size_t wantCars = static_cast<size_t>(extra * kCarsPerStep);
    if (g_peds.size() < wantPeds && worldGetAllPeds(g_buf, 1024) < kMaxWorldPeds) SpawnPed(pos);
    if (g_cars.size() < wantCars && worldGetAllVehicles(g_buf, 1024) < kMaxWorldCars) SpawnCar(pos);
}

}  // namespace population
