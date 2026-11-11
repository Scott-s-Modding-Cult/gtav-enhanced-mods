#include "custom.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "cheats.h"
#include "colors.h"
#include "menu.h"
#include "natives.h"
#include "vehextra.h"

namespace custom {
namespace {

using vehx::GetDashboardColour;
using vehx::GetInteriorColour;
using vehx::GetXenonColour;
using vehx::SetDashboardColour;
using vehx::SetInteriorColour;
using vehx::SetXenonColour;

// ------------------------------------------------------------------ mod slots
enum ModType {
    kEngine = 11, kBrakes = 12, kTransmission = 13, kHorn = 14, kSuspension = 15, kArmor = 16,
    kTurbo = 18, kTyreSmoke = 20, kXenon = 22, kFrontWheels = 23, kRearWheels = 24, kLivery = 48,
};
constexpr int kModTypeCount = 50;

const char* const kSlotNames[kModTypeCount] = {
    "Spoiler", "Front Bumper", "Rear Bumper", "Skirts", "Exhaust", "Roll Cage", "Grille", "Hood",
    "Left Fender", "Right Fender", "Roof", "Engine", "Brakes", "Transmission", "Horn", "Suspension",
    "Armor", "Nitrous", "Turbo", "Subwoofer", "Tyre Smoke", "Hydraulics", "Xenon Lights", "Wheels",
    "Rear Wheels", "Plate Holder", "Vanity Plates", "Trim Design", "Ornaments", "Dashboard",
    "Dial Design", "Door Speakers", "Seats", "Steering Wheel", "Shifter Lever", "Plaques",
    "Speakers", "Trunk", "Hydraulics", "Engine Block", "Air Filter", "Struts", "Arch Covers",
    "Aerials", "Trim", "Tank", "Windows", "Mod 47", "Livery", "Mod 49",
};
const int kPerformance[] = {kEngine, kBrakes, kTransmission, kSuspension, kArmor};
bool IsPerformance(int t) {
    for (int p : kPerformance) if (p == t) return true;
    return false;
}
bool IsSpecial(int t) {  // handled in their own menus
    return IsPerformance(t) || t == kTurbo || t == kTyreSmoke || t == kXenon || t == kFrontWheels ||
           t == kRearWheels;
}

std::string Label(const char* label) {
    if (!label || !*label) return {};
    char* l = const_cast<char*>(label);
    if (!UI::DOES_TEXT_LABEL_EXIST(l)) return {};
    const char* t = UI::_GET_LABEL_TEXT(l);
    if (!t || !*t || std::strcmp(t, "NULL") == 0) return {};
    return t;
}

std::string SlotName(Vehicle v, int type) {
    std::string s = Label(VEHICLE::GET_MOD_SLOT_NAME(v, type));
    return s.empty() ? kSlotNames[type] : s;
}

std::string ModName(Vehicle v, int type, int i) {
    if (i < 0) return "Stock";
    std::string s = Label(VEHICLE::GET_MOD_TEXT_LABEL(v, type, i));
    if (!s.empty()) return s;
    char buf[64];
    static const char* const kBrakeNames[] = {"Street Brakes", "Sport Brakes", "Race Brakes"};
    static const char* const kTransNames[] = {"Street Transmission", "Sports Transmission", "Race Transmission"};
    static const char* const kSuspNames[] = {"Lowered Suspension", "Street Suspension", "Sport Suspension",
                                        "Competition Suspension", "Race Suspension"};
    switch (type) {
        case kEngine: std::snprintf(buf, sizeof buf, "EMS Upgrade, Level %d", i + 1); return buf;
        case kBrakes: if (i < 3) return kBrakeNames[i]; break;
        case kTransmission: if (i < 3) return kTransNames[i]; break;
        case kSuspension: if (i < 5) return kSuspNames[i]; break;
        case kArmor: std::snprintf(buf, sizeof buf, "Armor Upgrade %d%%", (i + 1) * 20); return buf;
        case kHorn: std::snprintf(buf, sizeof buf, "Horn %d", i + 1); return buf;
    }
    std::snprintf(buf, sizeof buf, "Option %d", i + 1);
    return buf;
}

bool Alive(Vehicle v) {
    if (v && ENTITY::DOES_ENTITY_EXIST(v)) { VEHICLE::SET_VEHICLE_MOD_KIT(v, 0); return true; }
    ShowStatus("That vehicle is gone");
    return false;
}

void ApplyMod(Vehicle v, int type, int i) {
    if (i < 0) { VEHICLE::REMOVE_VEHICLE_MOD(v, type); return; }
    const BOOL custom = (type == kFrontWheels || type == kRearWheels) ? VEHICLE::GET_VEHICLE_MOD_VARIATION(v, type) : FALSE;
    VEHICLE::SET_VEHICLE_MOD(v, type, i, custom);
}

// A list of every option in one slot, with the fitted one marked and selected.
void ModList(Menu& m, Vehicle v, int type) {
    m.SetOnOpen([v, type](Menu& self) {
        if (!Alive(v)) return;
        const int cur = VEHICLE::GET_VEHICLE_MOD(v, type);
        const int n = VEHICLE::GET_NUM_VEHICLE_MODS(v, type);
        Menu* me = &self;
        for (int i = -1; i < n; ++i)
            self.AddAction(ModName(v, type, i), [v, type, i, me] { if (Alive(v)) ApplyMod(v, type, i); me->Refresh(); },
                           i == cur ? "Installed" : "");
        self.Select(cur + 1);
    });
}

void AddModSlot(Menu& parent, Vehicle v, int type) {
    ModList(parent.AddSubmenu(SlotName(v, type), SlotName(v, type)), v, type);
}

// On/off row that rebuilds its menu so the ON/OFF text stays true.
void AddSwitch(Menu& m, const std::string& label, bool on, std::function<void()> flip) {
    Menu* me = &m;
    m.AddAction(label, [flip, me] { flip(); me->Refresh(); }, on ? "ON" : "OFF");
}

// ------------------------------------------------------------------ paint
enum class Paint { Primary, Secondary, Pearl, Wheel, Interior, Dashboard };

int GetPaint(Vehicle v, Paint p) {
    int a = 0, b = 0;
    switch (p) {
        case Paint::Primary: VEHICLE::GET_VEHICLE_COLOURS(v, &a, &b); return a;
        case Paint::Secondary: VEHICLE::GET_VEHICLE_COLOURS(v, &a, &b); return b;
        case Paint::Pearl: VEHICLE::GET_VEHICLE_EXTRA_COLOURS(v, &a, &b); return a;
        case Paint::Wheel: VEHICLE::GET_VEHICLE_EXTRA_COLOURS(v, &a, &b); return b;
        case Paint::Interior: return GetInteriorColour(v);
        case Paint::Dashboard: return GetDashboardColour(v);
    }
    return 0;
}

void SetPaint(Vehicle v, Paint p, int c) {
    int a = 0, b = 0;
    switch (p) {
        case Paint::Primary:
            VEHICLE::CLEAR_VEHICLE_CUSTOM_PRIMARY_COLOUR(v);
            VEHICLE::GET_VEHICLE_COLOURS(v, &a, &b);
            VEHICLE::SET_VEHICLE_COLOURS(v, c, b);
            break;
        case Paint::Secondary:
            VEHICLE::CLEAR_VEHICLE_CUSTOM_SECONDARY_COLOUR(v);
            VEHICLE::GET_VEHICLE_COLOURS(v, &a, &b);
            VEHICLE::SET_VEHICLE_COLOURS(v, a, c);
            break;
        case Paint::Pearl:
            VEHICLE::GET_VEHICLE_EXTRA_COLOURS(v, &a, &b);
            VEHICLE::SET_VEHICLE_EXTRA_COLOURS(v, c, b);
            break;
        case Paint::Wheel:
            VEHICLE::GET_VEHICLE_EXTRA_COLOURS(v, &a, &b);
            VEHICLE::SET_VEHICLE_EXTRA_COLOURS(v, a, c);
            break;
        case Paint::Interior: SetInteriorColour(v, c); break;
        case Paint::Dashboard: SetDashboardColour(v, c); break;
    }
}

void PaintList(Menu& m, Vehicle v, Paint p, const colours::Section& sec) {
    m.SetOnOpen([v, p, sec](Menu& self) {
        if (!Alive(v)) return;
        const int cur = GetPaint(v, p);
        Menu* me = &self;
        for (int i = 0; i < sec.count; ++i) {
            const int c = sec.list[i].index;
            self.AddAction(sec.list[i].name, [v, p, c, me] { if (Alive(v)) SetPaint(v, p, c); me->Refresh(); },
                           c == cur ? "Current" : "");
            if (c == cur) self.Select(i);
        }
    });
}

// Classic / Metallic / Matte / Metals / Chrome / Chameleon, each a list.
void AddPaintSections(Menu& parent, const std::string& label, Vehicle v, Paint p) {
    Menu& m = parent.AddSubmenu(label, label);
    for (const colours::Section& s : colours::kSections) PaintList(m.AddSubmenu(s.name, s.name), v, p, s);
}

// ------------------------------------------------------------------ wheels
const char* const kWheelTypes[] = {
    "Sport", "Muscle", "Lowrider", "SUV", "Offroad", "Tuner", "Bike", "High End",
    "Benny's Original", "Benny's Bespoke", "Open Wheel", "Street", "Track",
};
constexpr int kBikeWheels = 6;

bool IsBike(Vehicle v) {
    const Hash m = ENTITY::GET_ENTITY_MODEL(v);
    return VEHICLE::IS_THIS_MODEL_A_BIKE(m) != FALSE;
}

void WheelList(Menu& m, Vehicle v, int wheelType) {
    m.SetOnOpen([v, wheelType](Menu& self) {
        if (!Alive(v)) return;
        // Listing another wheel type means switching to it; put the car back the way it was after.
        const int origType = VEHICLE::GET_VEHICLE_WHEEL_TYPE(v);
        const int origMod = VEHICLE::GET_VEHICLE_MOD(v, kFrontWheels);
        const BOOL origVar = VEHICLE::GET_VEHICLE_MOD_VARIATION(v, kFrontWheels);
        VEHICLE::SET_VEHICLE_WHEEL_TYPE(v, wheelType);
        const int n = VEHICLE::GET_NUM_VEHICLE_MODS(v, kFrontWheels);
        Menu* me = &self;
        const bool bike = IsBike(v);
        for (int i = 0; i < n; ++i) {
            const bool fitted = origType == wheelType && origMod == i;
            self.AddAction(ModName(v, kFrontWheels, i), [v, wheelType, i, bike, me] {
                if (!Alive(v)) return;
                const BOOL custom = VEHICLE::GET_VEHICLE_MOD_VARIATION(v, kFrontWheels);
                VEHICLE::SET_VEHICLE_WHEEL_TYPE(v, wheelType);
                VEHICLE::SET_VEHICLE_MOD(v, kFrontWheels, i, custom);
                if (bike) VEHICLE::SET_VEHICLE_MOD(v, kRearWheels, i, custom);
                me->Refresh();
            }, fitted ? "Installed" : "");
            if (fitted) self.Select(i);
        }
        if (n == 0) self.AddAction("(none for this vehicle)", [] {});
        VEHICLE::SET_VEHICLE_WHEEL_TYPE(v, origType);
        if (origMod >= 0) VEHICLE::SET_VEHICLE_MOD(v, kFrontWheels, origMod, origVar);
        else VEHICLE::REMOVE_VEHICLE_MOD(v, kFrontWheels);
    });
}

struct Rgb { const char* name; int r, g, b; };
const Rgb kSmokeColours[] = {
    {"White", 254, 254, 254}, {"Black", 1, 1, 1}, {"Blue", 0, 174, 239}, {"Yellow", 252, 238, 0},
    {"Purple", 132, 102, 226}, {"Orange", 255, 127, 0}, {"Green", 114, 204, 114},
    {"Red", 226, 6, 6}, {"Pink", 203, 54, 148}, {"Brown", 180, 130, 97},
};
const Rgb kNeon[] = {
    {"White", 222, 222, 255}, {"Blue", 2, 21, 255}, {"Electric Blue", 3, 83, 255},
    {"Mint Green", 0, 255, 140}, {"Lime Green", 94, 255, 1}, {"Yellow", 255, 255, 0},
    {"Golden Shower", 255, 150, 5}, {"Orange", 255, 62, 0}, {"Red", 255, 1, 1},
    {"Pony Pink", 255, 50, 100}, {"Hot Pink", 255, 5, 190}, {"Purple", 35, 1, 255},
    {"Blacklight", 15, 3, 255},
};

void BuildWheels(Menu& m, Vehicle v) {
    Menu* me = &m;
    m.AddAction("Stock wheels", [v, me] {
        if (!Alive(v)) return;
        VEHICLE::REMOVE_VEHICLE_MOD(v, kFrontWheels);
        VEHICLE::REMOVE_VEHICLE_MOD(v, kRearWheels);
        me->Refresh();
    });
    const bool bike = IsBike(v);
    for (int t = 0; t < static_cast<int>(sizeof kWheelTypes / sizeof *kWheelTypes); ++t) {
        if (bike != (t == kBikeWheels)) continue;  // bikes only take bike wheels, cars everything else
        WheelList(m.AddSubmenu(kWheelTypes[t], kWheelTypes[t]), v, t);
    }
    AddSwitch(m, "Custom tyres", VEHICLE::GET_VEHICLE_MOD_VARIATION(v, kFrontWheels) != FALSE, [v] {
        if (!Alive(v)) return;
        const int cur = VEHICLE::GET_VEHICLE_MOD(v, kFrontWheels);
        if (cur < 0) { ShowStatus("Pick a wheel first"); return; }
        const BOOL on = !VEHICLE::GET_VEHICLE_MOD_VARIATION(v, kFrontWheels);
        VEHICLE::SET_VEHICLE_MOD(v, kFrontWheels, cur, on);
        const int rear = VEHICLE::GET_VEHICLE_MOD(v, kRearWheels);
        if (rear >= 0) VEHICLE::SET_VEHICLE_MOD(v, kRearWheels, rear, on);
    });
    AddSwitch(m, "Bulletproof tyres", !cheats::TyresCanBurst(v), [v] {
        if (Alive(v)) cheats::SetTyresCanBurst(v, !cheats::TyresCanBurst(v));
    });
    AddPaintSections(m, "Wheel colour", v, Paint::Wheel);

    Menu& smoke = m.AddSubmenu("Tyre smoke", "Tyre Smoke");
    smoke.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        AddSwitch(self, "Custom tyre smoke", VEHICLE::IS_TOGGLE_MOD_ON(v, kTyreSmoke) != FALSE, [v] {
            if (Alive(v)) VEHICLE::TOGGLE_VEHICLE_MOD(v, kTyreSmoke, !VEHICLE::IS_TOGGLE_MOD_ON(v, kTyreSmoke));
        });
        for (const Rgb& c : kSmokeColours) {
            const Rgb col = c;
            self.AddAction(c.name, [v, col] {
                if (!Alive(v)) return;
                VEHICLE::TOGGLE_VEHICLE_MOD(v, kTyreSmoke, TRUE);
                VEHICLE::SET_VEHICLE_TYRE_SMOKE_COLOR(v, col.r, col.g, col.b);
                ShowStatus(std::string("Tyre smoke: ") + col.name);
            });
        }
    });
}

// ------------------------------------------------------------------ lights
void BuildLights(Menu& m, Vehicle v) {
    AddSwitch(m, "Xenon headlights", VEHICLE::IS_TOGGLE_MOD_ON(v, kXenon) != FALSE, [v] {
        if (Alive(v)) VEHICLE::TOGGLE_VEHICLE_MOD(v, kXenon, !VEHICLE::IS_TOGGLE_MOD_ON(v, kXenon));
    });
    Menu& xenon = m.AddSubmenu("Xenon colour", "Xenon Colour");
    xenon.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        const int cur = GetXenonColour(v);
        Menu* me = &self;
        self.AddAction("Stock", [v, me] { if (Alive(v)) SetXenonColour(v, 255); me->Refresh(); },
                       cur < 0 || cur > 12 ? "Current" : "");
        for (int i = 0; i < 13; ++i)
            self.AddAction(colours::kXenon[i], [v, i, me] {
                if (!Alive(v)) return;
                VEHICLE::TOGGLE_VEHICLE_MOD(v, kXenon, TRUE);  // colour only shows with xenons on
                SetXenonColour(v, i);
                me->Refresh();
            }, i == cur ? "Current" : "");
        if (cur >= 0 && cur <= 12) self.Select(cur + 1);
    });

    Menu& neon = m.AddSubmenu("Neon", "Neon");
    neon.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        static const char* const kSides[] = {"Left", "Right", "Front", "Back"};
        AddSwitch(self, "All sides", VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 0) != FALSE, [v] {
            if (!Alive(v)) return;
            const BOOL on = !VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 0);
            for (int s = 0; s < 4; ++s) VEHICLE::_SET_VEHICLE_NEON_LIGHT_ENABLED(v, s, on);
        });
        for (int s = 0; s < 4; ++s)
            AddSwitch(self, kSides[s], VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, s) != FALSE, [v, s] {
                if (Alive(v)) VEHICLE::_SET_VEHICLE_NEON_LIGHT_ENABLED(v, s, !VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, s));
            });
        int r = 0, g = 0, b = 0;
        VEHICLE::_GET_VEHICLE_NEON_LIGHTS_COLOUR(v, &r, &g, &b);
        Menu* me = &self;
        for (const Rgb& c : kNeon) {
            const Rgb col = c;
            const bool cur = c.r == r && c.g == g && c.b == b;
            self.AddAction(c.name, [v, col, me] {
                if (Alive(v)) VEHICLE::_SET_VEHICLE_NEON_LIGHTS_COLOUR(v, col.r, col.g, col.b);
                me->Refresh();
            }, cur ? "Current" : "");
        }
    });
}

// ------------------------------------------------------------------ the rest
const char* const kPlates[] = {
    "Blue on White 1", "Yellow on Black", "Yellow on Blue", "Blue on White 2", "Blue on White 3",
    "North Yankton", "eCola", "Las Venturas", "Liberty City", "LS Car Meet", "LS Panic",
    "LS Pounders", "Sprunk",
};

void MaxPerformance(Vehicle v) {
    for (int t : kPerformance) {
        const int n = VEHICLE::GET_NUM_VEHICLE_MODS(v, t);
        if (n > 0) VEHICLE::SET_VEHICLE_MOD(v, t, n - 1, FALSE);
    }
    VEHICLE::TOGGLE_VEHICLE_MOD(v, kTurbo, TRUE);
    cheats::SetTyresCanBurst(v, false);
}

void MaxEverything(Vehicle v) {
    MaxPerformance(v);
    for (int t = 0; t < kModTypeCount; ++t) {
        if (IsSpecial(t)) continue;
        const int n = VEHICLE::GET_NUM_VEHICLE_MODS(v, t);
        if (n > 0) VEHICLE::SET_VEHICLE_MOD(v, t, n - 1, FALSE);
    }
    VEHICLE::TOGGLE_VEHICLE_MOD(v, kXenon, TRUE);
}

void BuildRoot(Menu& m) {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    const Vehicle v = PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) : 0;
    if (!v) {
        m.AddAction("Get in a vehicle first", [] { ShowStatus("Get in a vehicle first"); });
        return;
    }
    VEHICLE::SET_VEHICLE_MOD_KIT(v, 0);

    m.AddAction("Max performance", [v] { if (Alive(v)) { MaxPerformance(v); ShowStatus("Performance maxed"); } });
    m.AddAction("Max everything", [v] { if (Alive(v)) { MaxEverything(v); ShowStatus("Everything maxed"); } });
    m.AddAction("Repair and clean", [v] {
        if (!Alive(v)) return;
        VEHICLE::SET_VEHICLE_FIXED(v);
        VEHICLE::SET_VEHICLE_DEFORMATION_FIXED(v);
        VEHICLE::SET_VEHICLE_DIRT_LEVEL(v, 0.0f);
        ShowStatus("Repaired");
    });

    Menu& perf = m.AddSubmenu("Performance", "Performance");
    perf.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        for (int t : kPerformance)
            if (VEHICLE::GET_NUM_VEHICLE_MODS(v, t) > 0) AddModSlot(self, v, t);
        AddSwitch(self, "Turbo", VEHICLE::IS_TOGGLE_MOD_ON(v, kTurbo) != FALSE, [v] {
            if (Alive(v)) VEHICLE::TOGGLE_VEHICLE_MOD(v, kTurbo, !VEHICLE::IS_TOGGLE_MOD_ON(v, kTurbo));
        });
    });

    Menu& body = m.AddSubmenu("Body and interior", "Body");
    body.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        for (int t = 0; t < kModTypeCount; ++t)
            if (!IsSpecial(t) && VEHICLE::GET_NUM_VEHICLE_MODS(v, t) > 0) AddModSlot(self, v, t);
        if (self.Count() == 0) self.AddAction("(no parts for this vehicle)", [] {});
    });

    Menu& wheels = m.AddSubmenu("Wheels", "Wheels");
    wheels.SetOnOpen([v](Menu& self) { if (Alive(v)) BuildWheels(self, v); });

    Menu& paint = m.AddSubmenu("Paint", "Paint");
    paint.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        AddPaintSections(self, "Primary", v, Paint::Primary);
        AddPaintSections(self, "Secondary", v, Paint::Secondary);
        PaintList(self.AddSubmenu("Pearlescent", "Pearlescent"), v, Paint::Pearl, colours::kPearlSection);
        AddPaintSections(self, "Wheel colour", v, Paint::Wheel);
        PaintList(self.AddSubmenu("Interior", "Interior"), v, Paint::Interior, colours::kSections[0]);
        PaintList(self.AddSubmenu("Dashboard", "Dashboard"), v, Paint::Dashboard, colours::kSections[0]);
    });

    Menu& lights = m.AddSubmenu("Lights", "Lights");
    lights.SetOnOpen([v](Menu& self) { if (Alive(v)) BuildLights(self, v); });

    Menu& tint = m.AddSubmenu("Window tint", "Window Tint");
    tint.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        const int cur = VEHICLE::GET_VEHICLE_WINDOW_TINT(v);
        Menu* t = &self;
        int row = 0;
        for (const PaintColour& c : colours::kTints) {
            const int idx = c.index;
            self.AddAction(c.name, [v, idx, t] { if (Alive(v)) VEHICLE::SET_VEHICLE_WINDOW_TINT(v, idx); t->Refresh(); },
                           idx == cur ? "Current" : "");
            if (idx == cur) self.Select(row);
            ++row;
        }
    });

    Menu& plate = m.AddSubmenu("Plate", "Plate");
    plate.SetOnOpen([v](Menu& self) {
        if (!Alive(v)) return;
        self.AddAction("Set plate text...", [v] {
            std::string text;
            if (!AskText(text, 8) || !Alive(v)) return;
            VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(v, const_cast<char*>(text.c_str()));
            ShowStatus("Plate: " + text);
        });
        const int cur = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v);
        Menu* me = &self;
        for (int i = 0; i < static_cast<int>(sizeof kPlates / sizeof *kPlates); ++i)
            self.AddAction(kPlates[i], [v, i, me] {
                if (Alive(v)) VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v, i);
                me->Refresh();
            }, i == cur ? "Current" : "");
    });

    if (VEHICLE::GET_VEHICLE_LIVERY_COUNT(v) > 0) {
        Menu& liv = m.AddSubmenu("Liveries", "Liveries");
        liv.SetOnOpen([v](Menu& self) {
            if (!Alive(v)) return;
            const int n = VEHICLE::GET_VEHICLE_LIVERY_COUNT(v), cur = VEHICLE::GET_VEHICLE_LIVERY(v);
            Menu* me = &self;
            for (int i = 0; i < n; ++i) {
                std::string name = Label(VEHICLE::GET_LIVERY_NAME(v, i));
                if (name.empty()) name = "Livery " + std::to_string(i + 1);
                self.AddAction(name, [v, i, me] { if (Alive(v)) VEHICLE::SET_VEHICLE_LIVERY(v, i); me->Refresh(); },
                               i == cur ? "Current" : "");
            }
            self.Select(cur);
        });
    }

    bool anyExtra = false;
    for (int id = 0; id <= 20 && !anyExtra; ++id) anyExtra = VEHICLE::DOES_EXTRA_EXIST(v, id) != FALSE;
    if (anyExtra) {
        Menu& extras = m.AddSubmenu("Extras", "Extras");
        extras.SetOnOpen([v](Menu& self) {
            if (!Alive(v)) return;
            for (int id = 0; id <= 20; ++id) {
                if (!VEHICLE::DOES_EXTRA_EXIST(v, id)) continue;
                AddSwitch(self, "Extra " + std::to_string(id), VEHICLE::IS_VEHICLE_EXTRA_TURNED_ON(v, id) != FALSE, [v, id] {
                    // SET_VEHICLE_EXTRA's last argument is "disable": 0 = on, 1 = off.
                    if (Alive(v)) VEHICLE::SET_VEHICLE_EXTRA(v, id, VEHICLE::IS_VEHICLE_EXTRA_TURNED_ON(v, id) ? 1 : 0);
                });
            }
        });
    }
}

}  // namespace

void Setup(Menu& menu) { menu.SetOnOpen(BuildRoot); }

}  // namespace custom
