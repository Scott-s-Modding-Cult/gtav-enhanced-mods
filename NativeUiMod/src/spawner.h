#pragma once
// Vehicle spawner: every vehicle in the game (story and GTA Online DLC), browsable by class or
// searchable by name/model, spawned in front of you or with you in the driver's seat.
#include <string>

#include "types.h"  // Vehicle, Hash

class Menu;

namespace spawner {

struct Options {
    bool spawnInside = true;     // put you in the driver's seat
    bool deletePrevious = true;  // remove the last vehicle you spawned when spawning a new one
    bool unlockDlc = true;       // exempt vehicles from story mode's Online-vehicle removal
    bool stayInside = true;      // undo the game pulling you out of your spawned vehicle
};
extern Options opt;

// Fills `menu` (a submenu of `root`). The vehicle lists are built the first time it's opened.
void Setup(Menu& root, Menu& menu);
// Spawns `model` with the current options (seat, unlock, previous-spawn cleanup). 0 on failure.
Vehicle SpawnModel(Hash model, const std::string& name);
// Call every frame (runs the stay-in guard).
void Tick();

}  // namespace spawner
