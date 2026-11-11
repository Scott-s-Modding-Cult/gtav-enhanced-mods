#pragma once
// Extra population: the game's own density settings top out at 1.0 (normal), so above that this
// spawns extra pedestrians (on pavements) and traffic (on roads, with drivers) around you, then
// hands them to the game as ordinary ambient people/cars. Call population::Tick() every frame.

namespace population {

struct Settings {
    bool boost = false;
    float multiplier = 2.0f;  // 1x = the game's normal density, up to 5x
};
extern Settings s;

void Tick();

}  // namespace population
