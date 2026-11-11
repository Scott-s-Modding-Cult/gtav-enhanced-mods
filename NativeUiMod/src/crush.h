#pragma once
// Damage side of dumptruck mode: what happens to things that meet an immovable object.
//  - Vehicles pinned between you and something solid get crushed: dents spread across the body,
//    glass pops, tyres burst, doors fall off, the engine smokes then burns, the people inside are
//    hurt then killed, and finally it explodes. How fast depends on how hard you push and on how
//    big and sturdy the vehicle is along the direction you push.
//  - Vehicles that hit you (or that you sprint into) take a real crash: damage grows with the
//    square of the impact speed, they bounce off, bikers are thrown, and at high speed the front
//    occupants go through the windscreen.
//  - People pinned against a wall are crushed to death.
#include "vec.h"

namespace crush {

enum Side { Front, Rear, Left, Right };

// One frame of contact between the player and a vehicle, worked out by dumptruck mode.
struct VehicleContact {
    Vehicle veh = 0;
    V3 mn, mx;            // vehicle model bounding box (vehicle space)
    V3 localPoint;        // closest point of the box to the player (vehicle space)
    V3 localInto;         // push direction in vehicle space (horizontal unit vector)
    V3 worldPoint;
    V3 worldInto;         // push direction in world space (horizontal unit vector)
    Side side = Front;    // which face of the vehicle you are touching
    float thickness = 0;  // how deep the vehicle is along the push line from the contact point
    V3 velBefore;         // vehicle velocity before dumptruck mode touched it this frame
    float playerInto = 0; // how fast the player is pushing along worldInto (m/s)
    float boxAway = 0;    // how fast the vehicle is moving away from the player (m/s)
};

struct Response {
    float bounce = 0.0f;  // extra away-speed to add (crash rebound)
    float lift = 0.0f;    // extra upward speed (big hits pop the car up a little)
    bool skipShove = false;  // it just exploded: let the explosion move it
};

Response OnVehicleContact(const VehicleContact& c, float dt);
// Ped being shoved; `solidBehind` = a raycast found a wall/car/prop right behind them.
// Returns true if they're pinned (and being crushed), so don't lift them up the wall.
bool OnPedContact(Ped ped, float playerInto, float pedAway, bool solidBehind, float dt);
// Record the away-speed dumptruck mode just gave an entity, so next frame we can tell whether it
// actually moved (free) or stayed put (pinned against something).
void NoteShove(Entity e, float awaySpeed);

void Tick();           // housekeeping: effect asset streaming, forgetting deleted entities
bool ShieldPlayer();   // true while you're crushing something or just made it explode
                       // (used to make you fire/explosion-proof for that moment)

}  // namespace crush
