#include "spawner.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "main.h"  // WAIT
#include "menu.h"
#include "natives.h"
#include "vec.h"
#include "vehicles.h"

namespace spawner {

Options opt;

namespace {

struct Entry {
    Hash hash = 0;
    std::string model;  // e.g. "zentorno"
    std::string name;   // in-game name, e.g. "Zentorno"
    int cls = -1;       // vehicle class (0-22)
};

bool Exempt(Vehicle v);    // see "DLC restriction" below

std::vector<Entry> g_all;  // every vehicle this game build actually has, A-Z by name
Vehicle g_last = 0;        // the last vehicle we spawned
Menu* g_root = nullptr;
Menu g_results("Search");  // filled by Search...

// Class ids from GET_VEHICLE_CLASS, listed in the order the menu shows them.
struct ClassInfo { int id; const char* name; };
const ClassInfo kClasses[] = {
    {7, "Super"}, {6, "Sports"}, {5, "Sports Classics"}, {4, "Muscle"}, {3, "Coupes"},
    {1, "Sedans"}, {0, "Compacts"}, {2, "SUVs"}, {9, "Off-Road"}, {22, "Open Wheel"},
    {8, "Motorcycles"}, {13, "Cycles"}, {12, "Vans"}, {17, "Service"}, {18, "Emergency"},
    {19, "Military"}, {10, "Industrial"}, {11, "Utility"}, {20, "Commercial"},
    {15, "Helicopters"}, {16, "Planes"}, {14, "Boats"},
};
constexpr int kTrainClass = 21;  // trains need CREATE_MISSION_TRAIN and rails; not spawnable here

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The name the game shows for a model ("Pegassi Zentorno" style label -> "Zentorno").
std::string DisplayName(Hash h, const std::string& model) {
    char* label = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(h);
    if (label && *label && UI::DOES_TEXT_LABEL_EXIST(label)) {
        const char* text = UI::_GET_LABEL_TEXT(label);
        if (text && *text && std::string(text) != "NULL") return text;
    }
    return model;
}

void BuildList() {
    if (!g_all.empty()) return;
    for (const char* model : kVehicleModels) {
        const Hash h = GAMEPLAY::GET_HASH_KEY(const_cast<char*>(model));
        if (!STREAMING::IS_MODEL_IN_CDIMAGE(h) || !STREAMING::IS_MODEL_A_VEHICLE(h)) continue;
        const int cls = VEHICLE::GET_VEHICLE_CLASS_FROM_NAME(h);
        if (cls == kTrainClass) continue;
        g_all.push_back({h, model, DisplayName(h, model), cls});
    }
    std::sort(g_all.begin(), g_all.end(), [](const Entry& a, const Entry& b) {
        const std::string la = Lower(a.name), lb = Lower(b.name);
        return la != lb ? la < lb : a.model < b.model;
    });
}

bool LoadModel(Hash model) {
    STREAMING::REQUEST_MODEL(model);
    const ULONGLONG until = GetTickCount64() + 8000;  // big DLC aircraft can take a moment
    while (!STREAMING::HAS_MODEL_LOADED(model)) {
        if (GetTickCount64() > until) return false;
        WAIT(0);
    }
    return true;
}

void DeleteVehicle(Vehicle& v) {
    if (!v || !ENTITY::DOES_ENTITY_EXIST(v)) { v = 0; return; }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v, TRUE, TRUE);
    VEHICLE::DELETE_VEHICLE(&v);
    v = 0;
}

// ------------------------------------------------------------------ spawning
Vehicle SpawnHash(Hash hash, const std::string& name) {
    if (!LoadModel(hash)) { ShowStatus("Couldn't load " + name); return 0; }

    const Ped ped = PLAYER::PLAYER_PED_ID();
    Vehicle current = PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) : 0;
    const Entity anchor = current ? current : ped;
    const float heading = ENTITY::GET_ENTITY_HEADING(anchor);
    const float speed = current ? ENTITY::GET_ENTITY_SPEED(current) : 0.0f;
    const bool airborne = ENTITY::GET_ENTITY_HEIGHT_ABOVE_GROUND(anchor) > 3.0f;

    Vector3 mn, mx;
    GAMEPLAY::GET_MODEL_DIMENSIONS(hash, &mn, &mx);
    const float halfLength = (mx.y - mn.y) * 0.5f;
    const V3 inFront = ToWorld(anchor, {0.0f, 3.0f + halfLength + (current ? 3.0f : 0.0f), 0.0f});

    // Swapping vehicles in place: the old one has to go or the two would spawn inside each other.
    // Your own last spawn, or any ordinary car, is removed; a story/mission vehicle (e.g. a
    // character's personal car) is left alone and the new one appears in front instead.
    bool inPlace = opt.spawnInside;
    if (inPlace && current) {
        const bool ours = current == g_last;
        if (ours || !ENTITY::IS_ENTITY_A_MISSION_ENTITY(current)) {
            if (ours) g_last = 0;
            DeleteVehicle(current);
        } else {
            inPlace = false;
        }
    }
    const V3 at = inPlace ? Pos(ped) : inFront;

    Vehicle v = VEHICLE::CREATE_VEHICLE(hash, at.x, at.y, at.z, heading, FALSE, TRUE);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(hash);
    if (!v) { ShowStatus("Couldn't spawn " + name); return 0; }
    const bool exempt = Exempt(v);  // before anything else can notice it

    if (!airborne) VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(v);
    VEHICLE::SET_VEHICLE_DIRT_LEVEL(v, 0.0f);
    VEHICLE::SET_VEHICLE_IS_STOLEN(v, FALSE);
    VEHICLE::SET_VEHICLE_NEEDS_TO_BE_HOTWIRED(v, FALSE);
    VEHICLE::SET_VEHICLE_HAS_BEEN_OWNED_BY_PLAYER(v, TRUE);

    if (opt.spawnInside) {
        PED::SET_PED_INTO_VEHICLE(ped, v, -1);
        if (!PED::IS_PED_IN_VEHICLE(ped, v, FALSE)) {
            // No driver's seat (trailers etc.): move it out in front of you instead.
            ENTITY::SET_ENTITY_COORDS(v, inFront.x, inFront.y, inFront.z, FALSE, FALSE, FALSE, FALSE);
            VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(v);
        } else {
            VEHICLE::SET_VEHICLE_ENGINE_ON(v, TRUE, TRUE, FALSE);
            if (VEHICLE::IS_THIS_MODEL_A_HELI(hash)) VEHICLE::SET_HELI_BLADES_FULL_SPEED(v);
            // Keep your momentum when swapping on the move (e.g. mid-flight plane swap).
            if (speed > 1.0f) VEHICLE::SET_VEHICLE_FORWARD_SPEED(v, speed);
        }
    }

    // The previous spawn: delete it (unless you're in it), or let the game clean it up later.
    if (g_last && g_last != v && ENTITY::DOES_ENTITY_EXIST(g_last)) {
        if (opt.deletePrevious && !PED::IS_PED_IN_VEHICLE(ped, g_last, FALSE)) DeleteVehicle(g_last);
        else ENTITY::SET_VEHICLE_AS_NO_LONGER_NEEDED(&g_last);
    }
    g_last = v;
    if (opt.unlockDlc && !exempt) ShowStatus("Spawned " + name + " (couldn't unlock: MPBitset missing)", 4000);
    else ShowStatus("Spawned " + name);
    return v;
}

void Spawn(const Entry& e) { SpawnHash(e.hash, e.name); }

void AddEntry(Menu& m, const Entry& e) {
    const Entry copy = e;
    m.AddAction(e.name, [copy] { Spawn(copy); }, e.model);
}

// ------------------------------------------------------------------ search
void Search() {
    std::string q;
    if (!AskText(q, 40)) return;
    const std::string lq = Lower(q);
    g_results.Clear();
    g_results.SetTitle("\"" + q + "\"");
    // Exact model name first, then everything whose name or model contains the text.
    for (const Entry& e : g_all)
        if (e.model == lq) AddEntry(g_results, e);
    for (const Entry& e : g_all)
        if (e.model != lq && (Lower(e.name).find(lq) != std::string::npos ||
                              e.model.find(lq) != std::string::npos))
            AddEntry(g_results, e);
    if (g_results.Count() == 0) { ShowStatus("No vehicles match \"" + q + "\""); return; }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%zu matches", g_results.Count());
    ShowStatus(buf);
    g_root->Show(g_results);
}

// ------------------------------------------------------------------ menu
void Fill(Menu& menu) {
    BuildList();
    menu.AddAction("Search (name or model)...", Search);
    menu.AddToggle("Spawn inside", &opt.spawnInside);
    menu.AddToggle("Delete previous spawn", &opt.deletePrevious);
    menu.AddToggle("Unlock Online-only vehicles", &opt.unlockDlc);
    menu.AddToggle("Stop the game kicking you out", &opt.stayInside);
    menu.AddAction("Delete last spawned", [] {
        if (!g_last || !ENTITY::DOES_ENTITY_EXIST(g_last)) { ShowStatus("Nothing to delete"); return; }
        DeleteVehicle(g_last);
        ShowStatus("Deleted");
    });

    char title[64];
    Menu& all = menu.AddSubmenu("All vehicles (A-Z)", "All Vehicles");
    for (const Entry& e : g_all) AddEntry(all, e);
    for (const ClassInfo& c : kClasses) {
        size_t n = 0;
        for (const Entry& e : g_all) n += e.cls == c.id;
        if (!n) continue;
        std::snprintf(title, sizeof title, "%s (%zu)", c.name, n);
        Menu& sub = menu.AddSubmenu(title, c.name);
        for (const Entry& e : g_all)
            if (e.cls == c.id) AddEntry(sub, e);
    }
    std::snprintf(title, sizeof title, "%zu vehicles available", g_all.size());
    ShowStatus(title);
}

// ------------------------------------------------------------------ DLC restriction
// Story mode's shop_controller script (func_1557 in the decompiled 1.73 Enhanced scripts) walks a
// list of 516 Online-only models, one per frame, every ~10 s. If you're in one it throws you out
// (CLEAR_PED_TASKS_IMMEDIATELY) and deletes it; otherwise it deletes the nearest one within 500 m.
// It skips any vehicle whose "MPBitset" decorator has bit 29 or 30 set, so we set bit 30 on what
// we spawn and on whatever you're driving. In story mode nothing else reads those bits.
// (Its global off switch, Global_4521040, moves between game builds; the decorator doesn't.)
constexpr int kDecorInt = 3;      // DECOR_TYPE_INT
constexpr int kExemptBit = 1 << 30;

bool Exempt(Vehicle v) {
    if (!opt.unlockDlc || !v || !ENTITY::DOES_ENTITY_EXIST(v)) return false;
    char* name = const_cast<char*>("MPBitset");
    if (!DECORATOR::DECOR_IS_REGISTERED_AS_TYPE(name, kDecorInt)) return false;
    const int bits = DECORATOR::DECOR_EXIST_ON(v, name) ? DECORATOR::DECOR_GET_INT(v, name) : 0;
    if (bits & kExemptBit) return true;
    return DECORATOR::DECOR_SET_INT(v, name, bits | kExemptBit) != FALSE;
}

// ------------------------------------------------------------------ stay-in guard
// Story mode pulls you out of some vehicles it doesn't expect you to have (GTA Online DLC
// models; it happens e.g. when you come back from the pause menu). If you're pulled out of the
// vehicle you spawned without pressing the exit key yourself, put you straight back in.
constexpr int kInputVehExit = 75;   // INPUT_VEH_EXIT (F / Y)
constexpr int kTaskExitVehicle = 2; // CTaskExitVehicle
ULONGLONG g_lastExitPressMs = 0;
int g_mySeat = -2;                  // seat we were last seen in; -2 = not in our vehicle

int SeatOf(Vehicle v, Ped p) {
    const int seats = VEHICLE::GET_VEHICLE_MAX_NUMBER_OF_PASSENGERS(v);
    for (int s = -1; s < seats; ++s)
        if (VEHICLE::GET_PED_IN_VEHICLE_SEAT(v, s) == p) return s;
    return -1;
}

void Guard() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    const ULONGLONG now = GetTickCount64();
    if (CONTROLS::IS_CONTROL_PRESSED(0, kInputVehExit) || CONTROLS::IS_DISABLED_CONTROL_PRESSED(0, kInputVehExit))
        g_lastExitPressMs = now;

    if (!opt.stayInside || !g_last || !ENTITY::DOES_ENTITY_EXIST(g_last) ||
        !VEHICLE::IS_VEHICLE_DRIVEABLE(g_last, FALSE) || PED::IS_PED_DEAD_OR_DYING(ped, TRUE)) {
        g_mySeat = -2;
        return;
    }
    if (UI::IS_PAUSE_MENU_ACTIVE()) return;
    // Cutscenes and missions that take control away are allowed to move you.
    if (CUTSCENE::IS_CUTSCENE_ACTIVE() || !PLAYER::IS_PLAYER_CONTROL_ON(PLAYER::PLAYER_ID())) {
        g_mySeat = -2;
        return;
    }
    const bool youAskedToLeave = now - g_lastExitPressMs < 2000;

    if (PED::IS_PED_IN_VEHICLE(ped, g_last, FALSE)) {
        g_mySeat = SeatOf(g_last, ped);
        if (!youAskedToLeave && AI::GET_IS_TASK_ACTIVE(ped, kTaskExitVehicle)) {
            PED::SET_PED_INTO_VEHICLE(ped, g_last, g_mySeat);  // cancels the exit
            ShowStatus("Game tried to kick you out - kept you in");
        }
        return;
    }
    // Already all the way out (e.g. warped out): put you back if it wasn't your doing.
    if (g_mySeat != -2 && !youAskedToLeave && !PED::IS_PED_RAGDOLL(ped) &&
        Len(Pos(ped) - Pos(g_last)) < 15.0f) {
        const int seat = VEHICLE::IS_VEHICLE_SEAT_FREE(g_last, g_mySeat) ? g_mySeat : -2;  // -2 = any free seat
        PED::SET_PED_INTO_VEHICLE(ped, g_last, seat);
        ShowStatus("Game kicked you out - put you back in");
    }
    g_mySeat = -2;
}

}  // namespace

Vehicle SpawnModel(Hash model, const std::string& name) { return SpawnHash(model, name); }

void Tick() {
    // Cover the vehicle you're in too (spawned by anything, or found in the world).
    const Ped ped = PLAYER::PLAYER_PED_ID();
    if (PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE)) Exempt(PED::GET_VEHICLE_PED_IS_IN(ped, FALSE));
    Guard();
}

void Setup(Menu& root, Menu& menu) {
    g_root = &root;
    menu.SetOnFirstOpen(Fill);
}

}  // namespace spawner
