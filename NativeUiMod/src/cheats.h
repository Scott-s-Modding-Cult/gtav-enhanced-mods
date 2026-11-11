#pragma once
#include "types.h"  // Vehicle
// Player cheats driven from the menu. Call cheats::Tick() once per frame.

namespace cheats {

struct Settings {
    bool explosiveBullets = false;
    bool infiniteAmmo = false;     // never run out, never reload
    bool godMode = false;
    bool superSpeed = false;
    float speedMultiplier = 2.0f;  // x normal on-foot speed
    bool superJump = false;
    float jumpMultiplier = 1.0f;   // x the game's own super jump (1x = exactly the HOPTOIT cheat)
    bool dumptruck = false;        // you are an immovable, unstoppable object
    bool vehNoCooldown = false;    // vehicle weapons (tank cannon, missiles...) keep firing while held
    float vehFireRate = 5.0f;      // extra shots per second
    bool instantAbilities = false; // rocket boost / nitrous refill instantly
};

extern Settings g;

void Tick();
void GiveAllWeapons();

// The vehicle's own "tyres can burst" setting. God mode forces bulletproof tyres on the vehicle
// you're in, so read/write through these to see and change the setting it'll go back to.
bool TyresCanBurst(Vehicle v);
void SetTyresCanBurst(Vehicle v, bool canBurst);

}  // namespace cheats
