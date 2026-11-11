// NineLivesCat - every cat (a_c_cat_01) gets nine lives.
// When a cat dies it stays down for a moment, then gets back up. The death that uses its last life is
// permanent. Nothing else changes: cats spawn and despawn exactly as in the vanilla game.
// Settings: NineLivesCat.ini next to the .asi (optional).
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "main.h"     // Script Hook V SDK: scriptRegister, WAIT, worldGetAllPeds
#include "natives.h"  // Script Hook V SDK: native function wrappers

namespace {

HMODULE g_module = nullptr;

struct Settings {
    int lives = 9;               // lives per cat, including the last one
    int reviveDelayMs = 3000;    // how long a dead cat stays down before getting back up
    bool notifications = true;   // feed message above the minimap when a cat loses a life
} g_cfg;

enum class State {
    Alive,
    Down,       // died, waiting to get back up
    Reviving,   // revive sent, waiting to see the cat alive again
    Gone,       // used its last life (or could not be revived); left dead like vanilla
};

struct Cat {
    int livesLeft = 9;           // includes the life it is on right now
    State state = State::Alive;
    ULONGLONG timer = 0;         // Down: when to revive. Reviving: when to check/retry.
    int reviveTries = 0;
    unsigned lastSeen = 0;       // scan stamp; cats not seen in a scan no longer exist
};

std::unordered_map<Ped, Cat> g_cats;

void LoadSettings() {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(g_module, path, MAX_PATH);
    if (char* dot = std::strrchr(path, '.')) *dot = '\0';
    strcat_s(path, ".ini");

    int lives = GetPrivateProfileIntA("NineLivesCat", "Lives", 9, path);
    int delay = GetPrivateProfileIntA("NineLivesCat", "ReviveDelayMs", 3000, path);
    int notify = GetPrivateProfileIntA("NineLivesCat", "Notifications", 1, path);
    g_cfg.lives = lives < 1 ? 1 : (lives > 999 ? 999 : lives);
    g_cfg.reviveDelayMs = delay < 0 ? 0 : (delay > 60000 ? 60000 : delay);
    g_cfg.notifications = notify != 0;
}

void Notify(const char* text) {
    if (!g_cfg.notifications) return;
    UI::_SET_NOTIFICATION_TEXT_ENTRY(const_cast<char*>("STRING"));
    UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(text));
    UI::_DRAW_NOTIFICATION(FALSE, FALSE);
}

void Revive(Ped cat) {
    FIRE::STOP_ENTITY_FIRE(cat);                 // otherwise a burning cat just dies again
    PED::RESURRECT_PED(cat);
    PED::REVIVE_INJURED_PED(cat);
    ENTITY::SET_ENTITY_HEALTH(cat, ENTITY::GET_ENTITY_MAX_HEALTH(cat));
    AI::CLEAR_PED_TASKS_IMMEDIATELY(cat);        // stand back up out of the death pose
    AI::TASK_WANDER_STANDARD(cat, 10.0f, 10);    // and carry on wandering
}

void OnDeath(Cat& c) {
    --c.livesLeft;
    if (c.livesLeft <= 0) {
        c.state = State::Gone;
        Notify("A cat used up its last life.");
        return;
    }
    c.state = State::Down;
    c.timer = GetTickCount64() + static_cast<ULONGLONG>(g_cfg.reviveDelayMs);
    char msg[96];
    std::snprintf(msg, sizeof msg, "A cat lost a life. %d %s left.", c.livesLeft,
                  c.livesLeft == 1 ? "life" : "lives");
    Notify(msg);
}

void UpdateCat(Ped ped, Cat& c) {
    const bool dead = ENTITY::IS_ENTITY_DEAD(ped) != 0;
    const ULONGLONG now = GetTickCount64();

    switch (c.state) {
        case State::Alive:
            if (dead) OnDeath(c);
            break;

        case State::Down:
            if (!dead) { c.state = State::Alive; break; }   // something else brought it back
            if (now >= c.timer) {
                Revive(ped);
                c.state = State::Reviving;
                c.reviveTries = 1;
                c.timer = now + 500;
            }
            break;

        case State::Reviving:
            if (!dead) { c.state = State::Alive; break; }
            if (now >= c.timer) {
                if (c.reviveTries < 3) {
                    Revive(ped);
                    ++c.reviveTries;
                    c.timer = now + 500;
                } else {
                    c.state = State::Gone;   // the game would not let it get up; leave it be
                }
            }
            break;

        case State::Gone:
            break;
    }
}

void Tick(Hash catModel, unsigned stamp) {
    static int peds[1024];
    const int count = worldGetAllPeds(peds, 1024);
    for (int i = 0; i < count; ++i) {
        const Ped ped = peds[i];
        if (ENTITY::GET_ENTITY_MODEL(ped) != catModel) continue;
        if (PED::IS_PED_A_PLAYER(ped)) continue;   // a player who is a cat dies the normal way

        auto it = g_cats.find(ped);
        if (it == g_cats.end()) {
            Cat c;
            c.livesLeft = g_cfg.lives;
            it = g_cats.emplace(ped, c).first;
        }
        it->second.lastSeen = stamp;
        UpdateCat(ped, it->second);
    }

    // Forget cats that despawned so a reused handle starts fresh.
    for (auto it = g_cats.begin(); it != g_cats.end();) {
        if (it->second.lastSeen != stamp) it = g_cats.erase(it);
        else ++it;
    }
}

void ScriptMain() {
    LoadSettings();
    g_cats.clear();
    const Hash catModel = GAMEPLAY::GET_HASH_KEY(const_cast<char*>("a_c_cat_01"));
    unsigned stamp = 0;
    for (;;) {
        Tick(catModel, ++stamp);
        WAIT(0);
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            g_module = hInstance;
            scriptRegister(hInstance, ScriptMain);
            break;
        case DLL_PROCESS_DETACH:
            scriptUnregister(hInstance);
            break;
    }
    return TRUE;
}
