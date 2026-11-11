// NativeUiMod — sample Script Hook V plugin with a menu and HUD drawn using game natives.
// F5 opens the menu. Story mode only.
#include <windows.h>

#include <cstdio>
#include <string>

#include "main.h"     // Script Hook V SDK: scriptRegister, WAIT, ...
#include "natives.h"  // Script Hook V SDK: native function wrappers
#include "chaos.h"
#include "cheats.h"
#include "custom.h"
#include "garage.h"
#include "menu.h"
#include "population.h"
#include "spawner.h"
#include "ui.h"

namespace {

bool g_showCoords = false;
bool g_neverWanted = false;

void HealPlayer() {
    Ped ped = PLAYER::PLAYER_PED_ID();
    ENTITY::SET_ENTITY_HEALTH(ped, ENTITY::GET_ENTITY_MAX_HEALTH(ped));
    PED::ADD_ARMOUR_TO_PED(ped, 100);
    ShowStatus("Health and armor restored");
}

void ClearWeather() {
    GAMEPLAY::SET_WEATHER_TYPE_NOW_PERSIST(const_cast<char*>("EXTRASUNNY"));
    ShowStatus("Weather set to extra sunny");
}

void DrawCoordsHud() {
    Ped ped = PLAYER::PLAYER_PED_ID();
    Vector3 p = ENTITY::GET_ENTITY_COORDS(ped, 1);
    float heading = ENTITY::GET_ENTITY_HEADING(ped);
    char buf[128];
    std::snprintf(buf, sizeof buf, "X %.1f   Y %.1f   Z %.1f   H %.0f", p.x, p.y, p.z, heading);
    ui::Rect(0.38f, 0.955f, 0.24f, 0.03f, ui::colors::Panel);
    ui::Text(buf, 0.50f, 0.958f, 0.32f, ui::colors::White, true, ui::FontChalet);
}

// ---------------------------------------------------------------- speedometer
// Bottom-right MPH readout while the player is in any vehicle.
bool g_showSpeedo = true;

void DrawSpeedometer() {
    Ped ped = PLAYER::PLAYER_PED_ID();
    if (!PED::IS_PED_IN_ANY_VEHICLE(ped, 0) || ENTITY::IS_ENTITY_DEAD(ped)) return;
    if (UI::IS_HUD_HIDDEN() || UI::IS_PAUSE_MENU_ACTIVE()) return;
    Vehicle veh = PED::GET_VEHICLE_PED_IS_IN(ped, 0);
    if (!veh) return;

    const float mph = ENTITY::GET_ENTITY_SPEED(veh) * 2.236936f;  // m/s -> mph
    float maxMph = VEHICLE::_GET_VEHICLE_MAX_SPEED(ENTITY::GET_ENTITY_MODEL(veh)) * 2.236936f;
    if (maxMph < 30.0f) maxMph = 150.0f;
    float fill = mph / maxMph;
    if (fill > 1.0f) fill = 1.0f;

    constexpr float w = 0.105f, h = 0.085f;
    constexpr float x = 0.985f - w, y = 0.89f - h;
    ui::Rect(x, y, w, h, ui::colors::Panel);

    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", static_cast<int>(mph + 0.5f));
    ui::TextRight(buf, x + w - 0.040f, y + 0.002f, 0.85f, ui::colors::White, ui::FontPricedown);
    ui::Text("MPH", x + w - 0.036f, y + 0.032f, 0.40f, ui::Color{200, 200, 200, 255}, false,
             ui::FontChalet);

    // Speed bar along the bottom: green -> yellow -> red as it approaches top speed.
    const float barX = x + 0.006f, barY = y + h - 0.014f, barW = w - 0.012f, barH = 0.007f;
    ui::Rect(barX, barY, barW, barH, ui::Color{60, 60, 60, 200});
    const int r = fill < 0.5f ? static_cast<int>(fill * 2 * 255) : 255;
    const int g = fill < 0.5f ? 200 : static_cast<int>((1.0f - fill) * 2 * 200);
    if (fill > 0.0f) ui::Rect(barX, barY, barW * fill, barH, ui::Color{r, g, 60, 230});
}

// ---------------------------------------------------------------- freemode body test
// The mpf_sfm_body pack replaces mp_f_freemode_01's default top (component 11, drawable 0) with the
// nude body and empties her default torso/legs/shoes/undershirt; texture 0..15 of the top = skin tone.
constexpr int kTopComponent = 11;
Hash g_originalModel = 0;
int g_bodySkin = 0;
int g_faceSkin = 21;   // head-blend skin parent id (0-45); 21 = first female parent
Ped g_testPed = 0;

Hash FreemodeFemale() { return GAMEPLAY::GET_HASH_KEY(const_cast<char*>("mp_f_freemode_01")); }

bool LoadModel(Hash model) {
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(model)) return false;
    STREAMING::REQUEST_MODEL(model);
    const ULONGLONG until = GetTickCount64() + 5000;
    while (!STREAMING::HAS_MODEL_LOADED(model)) {
        if (GetTickCount64() > until) return false;
        WAIT(0);
    }
    return true;
}

void ApplyNudeDefaults(Ped ped) {
    PED::SET_PED_DEFAULT_COMPONENT_VARIATION(ped);                       // drawable 0 everywhere = the body
    PED::SET_PED_HEAD_BLEND_DATA(ped, 21, 0, 0, g_faceSkin, 0, 0, 0.0f, 0.0f, 0.0f, 0);  // female face, chosen skin
    PED::SET_PED_COMPONENT_VARIATION(ped, kTopComponent, 0, g_bodySkin, 0);
}

void BecomeFreemodeFemale() {
    const Hash model = FreemodeFemale();
    Ped ped = PLAYER::PLAYER_PED_ID();
    if (ENTITY::GET_ENTITY_MODEL(ped) != model) g_originalModel = ENTITY::GET_ENTITY_MODEL(ped);
    if (!LoadModel(model)) { ShowStatus("Could not load mp_f_freemode_01"); return; }
    PLAYER::SET_PLAYER_MODEL(PLAYER::PLAYER_ID(), model);
    WAIT(0);
    ApplyNudeDefaults(PLAYER::PLAYER_PED_ID());
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    ShowStatus("You are mp_f_freemode_01 (default outfit)");
}

void RestoreCharacter() {
    if (!g_originalModel) { ShowStatus("Nothing to restore"); return; }
    if (!LoadModel(g_originalModel)) { ShowStatus("Could not load original model"); return; }
    PLAYER::SET_PLAYER_MODEL(PLAYER::PLAYER_ID(), g_originalModel);
    WAIT(0);
    PED::SET_PED_DEFAULT_COMPONENT_VARIATION(PLAYER::PLAYER_PED_ID());
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(g_originalModel);
    g_originalModel = 0;
    ShowStatus("Original character restored");
}

void NextBodySkin() {
    const Hash model = FreemodeFemale();
    Ped player = PLAYER::PLAYER_PED_ID();
    Ped target = ENTITY::GET_ENTITY_MODEL(player) == model ? player
               : (g_testPed && ENTITY::DOES_ENTITY_EXIST(g_testPed) ? g_testPed : 0);
    if (!target) { ShowStatus("Become or spawn mp_f_freemode_01 first"); return; }
    int count = PED::GET_NUMBER_OF_PED_TEXTURE_VARIATIONS(target, kTopComponent, 0);
    if (count <= 0) count = 16;
    g_bodySkin = (g_bodySkin + 1) % count;
    if (ENTITY::GET_ENTITY_MODEL(player) == model)
        PED::SET_PED_COMPONENT_VARIATION(player, kTopComponent, 0, g_bodySkin, 0);
    if (g_testPed && ENTITY::DOES_ENTITY_EXIST(g_testPed))
        PED::SET_PED_COMPONENT_VARIATION(g_testPed, kTopComponent, 0, g_bodySkin, 0);
    char buf[64];
    std::snprintf(buf, sizeof buf, "Body skin %d / %d", g_bodySkin + 1, count);
    ShowStatus(buf);
}

void NextFaceSkin() {
    const Hash model = FreemodeFemale();
    g_faceSkin = (g_faceSkin + 1) % 46;
    Ped player = PLAYER::PLAYER_PED_ID();
    if (ENTITY::GET_ENTITY_MODEL(player) == model)
        PED::SET_PED_HEAD_BLEND_DATA(player, 21, 0, 0, g_faceSkin, 0, 0, 0.0f, 0.0f, 0.0f, 0);
    if (g_testPed && ENTITY::DOES_ENTITY_EXIST(g_testPed))
        PED::SET_PED_HEAD_BLEND_DATA(g_testPed, 21, 0, 0, g_faceSkin, 0, 0, 0.0f, 0.0f, 0.0f, 0);
    char buf[64];
    std::snprintf(buf, sizeof buf, "Face skin %d / 46", g_faceSkin);
    ShowStatus(buf);
}

void SpawnTestNpc() {
    const Hash model = FreemodeFemale();
    if (!LoadModel(model)) { ShowStatus("Could not load mp_f_freemode_01"); return; }
    if (g_testPed && ENTITY::DOES_ENTITY_EXIST(g_testPed)) PED::DELETE_PED(&g_testPed);
    Ped player = PLAYER::PLAYER_PED_ID();
    Vector3 p = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(player, 0.0f, 2.0f, 0.0f);
    float heading = ENTITY::GET_ENTITY_HEADING(player) + 180.0f;
    g_testPed = PED::CREATE_PED(5 /* civilian female */, model, p.x, p.y, p.z, heading, 0, 1);
    ApplyNudeDefaults(g_testPed);
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    ShowStatus("Spawned mp_f_freemode_01 in front of you");
}

void DeleteTestNpc() {
    if (g_testPed && ENTITY::DOES_ENTITY_EXIST(g_testPed)) { PED::DELETE_PED(&g_testPed); ShowStatus("Test NPC removed"); }
    g_testPed = 0;
}

void UpdateCheats() {
    if (g_neverWanted) {
        Player player = PLAYER::PLAYER_ID();
        if (PLAYER::GET_PLAYER_WANTED_LEVEL(player) > 0) {
            PLAYER::SET_PLAYER_WANTED_LEVEL(player, 0, 0);
            PLAYER::SET_PLAYER_WANTED_LEVEL_NOW(player, 0);
        }
    }
}

void ScriptMain() {
    Menu menu("Native UI Mod");
    menu.AddToggle("Show coordinates", &g_showCoords);
    menu.AddToggle("Speedometer", &g_showSpeedo);
    menu.AddToggle("Never wanted", &g_neverWanted,
                   [] { ShowStatus(g_neverWanted ? "Never wanted: ON" : "Never wanted: OFF"); });
    auto onOff = [](const char* name, const bool* v) {
        return [name, v] { ShowStatus(std::string(name) + (*v ? ": ON" : ": OFF")); };
    };
    cheats::Settings& c = cheats::g;
    menu.AddToggle("God mode", &c.godMode, onOff("God mode", &c.godMode));
    menu.AddToggle("Explosive bullets", &c.explosiveBullets,
                   onOff("Explosive bullets", &c.explosiveBullets));
    menu.AddToggle("Infinite ammo", &c.infiniteAmmo, onOff("Infinite ammo", &c.infiniteAmmo));
    menu.AddToggleSlider("Super speed", &c.superSpeed,
                         {&c.speedMultiplier, 1.0f, 10.0f, 0.5f, "%.1fx"},
                         onOff("Super speed", &c.superSpeed));
    menu.AddToggleSlider("Super jump", &c.superJump,
                         {&c.jumpMultiplier, 1.0f, 10.0f, 0.25f, "%.2fx"},
                         onOff("Super jump", &c.superJump));
    menu.AddToggle("Dumptruck mode", &c.dumptruck, onOff("Dumptruck mode", &c.dumptruck));
    menu.AddToggleSlider("No vehicle weapon cooldown", &c.vehNoCooldown,
                         {&c.vehFireRate, 1.0f, 20.0f, 1.0f, "%.0f/s"},
                         onOff("No vehicle weapon cooldown", &c.vehNoCooldown));
    menu.AddToggle("Instant ability recharge", &c.instantAbilities,
                   onOff("Instant ability recharge", &c.instantAbilities));
    menu.AddToggleSlider("Extra population", &population::s.boost,
                         {&population::s.multiplier, 1.0f, 5.0f, 0.5f, "%.1fx"},
                         onOff("Extra population", &population::s.boost));
    menu.AddAction("Give all weapons", cheats::GiveAllWeapons);
    chaos::Setup(menu.AddSubmenu("Chaos", "Chaos"));
    spawner::Setup(menu, menu.AddSubmenu("Spawn vehicle", "Vehicles"));
    custom::Setup(menu.AddSubmenu("Customize vehicle", "Customize"));
    garage::Setup(menu, menu.AddSubmenu("Saved vehicles", "Garage"));
    menu.AddAction("Heal + armor", HealPlayer);
    menu.AddAction("Clear weather", ClearWeather);
    menu.AddAction("Become freemode female", BecomeFreemodeFemale);
    menu.AddAction("Next body skin", NextBodySkin);
    menu.AddAction("Next face skin", NextFaceSkin);
    menu.AddAction("Spawn test NPC", SpawnTestNpc);
    menu.AddAction("Delete test NPC", DeleteTestNpc);
    menu.AddAction("Restore character", RestoreCharacter);

    ShowStatus("Native UI Mod loaded - press F5", 5000);

    for (;;) {
        UpdateCheats();
        cheats::Tick();
        chaos::Tick();
        spawner::Tick();
        population::Tick();
        menu.Tick();
        if (g_showCoords) DrawCoordsHud();
        if (g_showSpeedo) DrawSpeedometer();
        DrawStatus();
        WAIT(0);  // yield one frame back to the game
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hInstance, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            scriptRegister(hInstance, ScriptMain);
            break;
        case DLL_PROCESS_DETACH:
            scriptUnregister(hInstance);
            break;
    }
    return TRUE;
}
