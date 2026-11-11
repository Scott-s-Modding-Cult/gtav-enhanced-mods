#pragma once
// Chaos effects: weapon madness, things falling from the sky, hostile crowds, maniac traffic and
// vortexes. Toggles live in `chaos::s`; call chaos::Tick() once per frame.
class Menu;

namespace chaos {

struct Settings {
    bool fireBullets = false;      // every shot sets things alight
    bool explosivePunches = false; // melee hits explode
    bool onePunch = false;         // anyone/anything you hit with fists or a melee weapon flies
    bool gravityGun = false;       // aim + shoot to grab, shoot again to throw
    bool airstrikeGun = false;     // the flare gun calls in an airstrike where the flare lands
    bool meteors = false;          // flaming boulders rain down around you
    bool hostile = false;          // everyone nearby gets a gun and comes for you
    bool hostileInfighting = true; // ...and they also fight each other and bystanders
    bool zombies = false;          // everyone nearby shambles after you (overrides hostile)
    bool maniacTraffic = false;    // every driver floors it; some try to ram you
    bool randomLaunches = false;   // nearby cars randomly get launched or flipped
    bool blackHole = false;        // pulls everything into a point where you aimed
    bool tornado = false;          // wanders toward you, picks things up and flings them
};
extern Settings s;

// Fills the Chaos submenu.
void Setup(Menu& menu);
void Tick();

}  // namespace chaos
