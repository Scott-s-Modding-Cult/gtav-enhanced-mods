#pragma once
// Saved vehicles: store the car you're in (model + every mod, paint, light, plate, livery and
// extra) under a name, and spawn it again later — across game sessions.
// Saved to <game folder>\NativeUiMod\garage.ini as plain text, one [name] section per vehicle.
class Menu;

namespace garage {

void Setup(Menu& root, Menu& menu);

}  // namespace garage
