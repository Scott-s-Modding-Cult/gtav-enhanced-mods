#pragma once
// "Customize vehicle": every LS Customs / Benny's / Arena mod slot, wheels, paints, lights, tint,
// plates, liveries and extras for the vehicle you're in — story and Online vehicles alike.
class Menu;

namespace custom {

// Fills `menu`; it rebuilds itself every time it's opened, for whatever you're driving then.
void Setup(Menu& menu);

}  // namespace custom
