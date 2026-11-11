#include "garage.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "cheats.h"
#include "menu.h"
#include "natives.h"
#include "spawner.h"
#include "vehextra.h"
#include "vehicles.h"

namespace garage {
namespace {

// ------------------------------------------------------------------ storage
using Props = std::map<std::string, std::string>;
struct Saved { std::string name; Props props; };
std::vector<Saved> g_saved;
bool g_loaded = false;
Menu* g_root = nullptr;

std::string Folder() {
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&Folder), &self);
    char path[MAX_PATH] = {};
    GetModuleFileNameA(self, path, MAX_PATH);
    std::string dir(path);
    dir = dir.substr(0, dir.find_last_of("\\/")) + "\\NativeUiMod";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}
std::string FilePath() { return Folder() + "\\garage.ini"; }

std::string Trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

void Load() {
    g_saved.clear();
    std::ifstream in(FilePath());
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            g_saved.push_back({line.substr(1, line.size() - 2), {}});
        } else if (!g_saved.empty()) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos) g_saved.back().props[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
        }
    }
    g_loaded = true;
}

bool Save() {
    std::ofstream out(FilePath(), std::ios::trunc);
    if (!out) { ShowStatus("Couldn't write " + FilePath(), 5000); return false; }
    out << "# NativeUiMod saved vehicles. One [name] section per vehicle; edit with care.\n";
    for (const Saved& s : g_saved) {
        out << "\n[" << s.name << "]\n";
        for (const auto& kv : s.props) out << kv.first << "=" << kv.second << "\n";
    }
    return true;
}

Saved* Find(const std::string& name) {
    for (Saved& s : g_saved) if (s.name == name) return &s;
    return nullptr;
}

// Names can't contain the characters the file format uses.
std::string CleanName(std::string n) {
    for (char& c : n) if (c == '[' || c == ']' || c == '\n' || c == '\r' || c == '=') c = ' ';
    return Trim(n);
}

// ------------------------------------------------------------------ helpers
std::string Join(std::initializer_list<int> v) {
    std::string s;
    for (int x : v) { if (!s.empty()) s += ","; s += std::to_string(x); }
    return s;
}
std::vector<int> Ints(const Props& p, const char* key) {
    std::vector<int> out;
    auto it = p.find(key);
    if (it == p.end()) return out;
    std::stringstream ss(it->second);
    std::string part;
    while (std::getline(ss, part, ',')) out.push_back(std::atoi(part.c_str()));
    return out;
}
bool Has(const Props& p, const char* key) { return p.count(key) != 0; }
int Int(const Props& p, const char* key, int def = 0) {
    auto it = p.find(key);
    return it == p.end() ? def : static_cast<int>(std::strtol(it->second.c_str(), nullptr, 0));
}

std::string ModelName(Hash h) {
    for (const char* m : kVehicleModels)
        if (GAMEPLAY::GET_HASH_KEY(const_cast<char*>(m)) == h) return m;
    return {};
}

std::string DisplayName(Hash h) {
    char* label = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(h);
    if (label && *label && UI::DOES_TEXT_LABEL_EXIST(label)) {
        const char* t = UI::_GET_LABEL_TEXT(label);
        if (t && *t && std::strcmp(t, "NULL") != 0) return t;
    }
    const std::string m = ModelName(h);
    return m.empty() ? "Vehicle" : m;
}

Vehicle CurrentVehicle() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    return PED::IS_PED_IN_ANY_VEHICLE(ped, FALSE) ? PED::GET_VEHICLE_PED_IS_IN(ped, FALSE) : 0;
}

constexpr int kModTypes = 50;
constexpr int kTurbo = 18, kTyreSmoke = 20, kXenon = 22, kFrontWheels = 23, kRearWheels = 24;
bool IsToggle(int t) { return t == kTurbo || t == kTyreSmoke || t == kXenon; }

// ------------------------------------------------------------------ capture / apply
Props Capture(Vehicle v) {
    Props p;
    VEHICLE::SET_VEHICLE_MOD_KIT(v, 0);
    const Hash model = ENTITY::GET_ENTITY_MODEL(v);
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08X", static_cast<unsigned>(model));
    p["model"] = hex;
    const std::string mn = ModelName(model);
    if (!mn.empty()) p["modelName"] = mn;

    int a = 0, b = 0, c = 0;
    VEHICLE::GET_VEHICLE_COLOURS(v, &a, &b);
    p["colours"] = Join({a, b});
    VEHICLE::GET_VEHICLE_EXTRA_COLOURS(v, &a, &b);
    p["extraColours"] = Join({a, b});
    p["interiorColour"] = std::to_string(vehx::GetInteriorColour(v));
    p["dashboardColour"] = std::to_string(vehx::GetDashboardColour(v));
    if (VEHICLE::GET_IS_VEHICLE_PRIMARY_COLOUR_CUSTOM(v)) {
        VEHICLE::GET_VEHICLE_CUSTOM_PRIMARY_COLOUR(v, &a, &b, &c);
        p["customPrimary"] = Join({a, b, c});
    }
    if (VEHICLE::GET_IS_VEHICLE_SECONDARY_COLOUR_CUSTOM(v)) {
        VEHICLE::GET_VEHICLE_CUSTOM_SECONDARY_COLOUR(v, &a, &b, &c);
        p["customSecondary"] = Join({a, b, c});
    }

    p["wheelType"] = std::to_string(VEHICLE::GET_VEHICLE_WHEEL_TYPE(v));
    for (int t = 0; t < kModTypes; ++t) {
        if (IsToggle(t)) {
            if (VEHICLE::IS_TOGGLE_MOD_ON(v, t)) p["toggle" + std::to_string(t)] = "1";
            continue;
        }
        const int m = VEHICLE::GET_VEHICLE_MOD(v, t);
        if (m < 0) continue;
        if (t == kFrontWheels || t == kRearWheels)
            p["mod" + std::to_string(t)] = Join({m, VEHICLE::GET_VEHICLE_MOD_VARIATION(v, t) ? 1 : 0});
        else
            p["mod" + std::to_string(t)] = std::to_string(m);
    }

    VEHICLE::GET_VEHICLE_TYRE_SMOKE_COLOR(v, &a, &b, &c);
    p["tyreSmoke"] = Join({a, b, c});
    p["bulletproofTyres"] = cheats::TyresCanBurst(v) ? "0" : "1";
    p["neon"] = Join({VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 0) ? 1 : 0, VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 1) ? 1 : 0,
                      VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 2) ? 1 : 0, VEHICLE::_IS_VEHICLE_NEON_LIGHT_ENABLED(v, 3) ? 1 : 0});
    VEHICLE::_GET_VEHICLE_NEON_LIGHTS_COLOUR(v, &a, &b, &c);
    p["neonColour"] = Join({a, b, c});
    p["xenonColour"] = std::to_string(vehx::GetXenonColour(v));
    p["windowTint"] = std::to_string(VEHICLE::GET_VEHICLE_WINDOW_TINT(v));
    p["plateStyle"] = std::to_string(VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v));
    if (const char* plate = VEHICLE::GET_VEHICLE_NUMBER_PLATE_TEXT(v)) p["plateText"] = Trim(plate);
    if (VEHICLE::GET_VEHICLE_LIVERY_COUNT(v) > 0) p["livery"] = std::to_string(VEHICLE::GET_VEHICLE_LIVERY(v));

    std::string extras;
    for (int id = 0; id <= 20; ++id) {
        if (!VEHICLE::DOES_EXTRA_EXIST(v, id)) continue;
        if (!extras.empty()) extras += ",";
        extras += std::to_string(id) + ":" + (VEHICLE::IS_VEHICLE_EXTRA_TURNED_ON(v, id) ? "1" : "0");
    }
    if (!extras.empty()) p["extras"] = extras;
    return p;
}

void Apply(Vehicle v, const Props& p) {
    VEHICLE::SET_VEHICLE_MOD_KIT(v, 0);

    // Wheel type first: wheel mod indices only mean something for a given type.
    if (Has(p, "wheelType")) VEHICLE::SET_VEHICLE_WHEEL_TYPE(v, Int(p, "wheelType"));
    for (int t = 0; t < kModTypes; ++t) {
        const std::string ts = std::to_string(t);
        if (IsToggle(t)) {
            VEHICLE::TOGGLE_VEHICLE_MOD(v, t, Int(p, ("toggle" + ts).c_str()) != 0);
            continue;
        }
        const std::vector<int> m = Ints(p, ("mod" + ts).c_str());
        if (m.empty()) { VEHICLE::REMOVE_VEHICLE_MOD(v, t); continue; }
        VEHICLE::SET_VEHICLE_MOD(v, t, m[0], m.size() > 1 && m[1] != 0);
    }

    std::vector<int> c = Ints(p, "colours");
    if (c.size() == 2) {
        VEHICLE::CLEAR_VEHICLE_CUSTOM_PRIMARY_COLOUR(v);
        VEHICLE::CLEAR_VEHICLE_CUSTOM_SECONDARY_COLOUR(v);
        VEHICLE::SET_VEHICLE_COLOURS(v, c[0], c[1]);
    }
    c = Ints(p, "extraColours");
    if (c.size() == 2) VEHICLE::SET_VEHICLE_EXTRA_COLOURS(v, c[0], c[1]);
    if (Has(p, "interiorColour")) vehx::SetInteriorColour(v, Int(p, "interiorColour"));
    if (Has(p, "dashboardColour")) vehx::SetDashboardColour(v, Int(p, "dashboardColour"));
    c = Ints(p, "customPrimary");
    if (c.size() == 3) VEHICLE::SET_VEHICLE_CUSTOM_PRIMARY_COLOUR(v, c[0], c[1], c[2]);
    c = Ints(p, "customSecondary");
    if (c.size() == 3) VEHICLE::SET_VEHICLE_CUSTOM_SECONDARY_COLOUR(v, c[0], c[1], c[2]);

    c = Ints(p, "tyreSmoke");
    if (c.size() == 3) VEHICLE::SET_VEHICLE_TYRE_SMOKE_COLOR(v, c[0], c[1], c[2]);
    cheats::SetTyresCanBurst(v, Int(p, "bulletproofTyres") == 0);
    c = Ints(p, "neon");
    for (int s = 0; s < 4; ++s) VEHICLE::_SET_VEHICLE_NEON_LIGHT_ENABLED(v, s, s < static_cast<int>(c.size()) && c[s] != 0);
    c = Ints(p, "neonColour");
    if (c.size() == 3) VEHICLE::_SET_VEHICLE_NEON_LIGHTS_COLOUR(v, c[0], c[1], c[2]);
    if (Has(p, "xenonColour")) vehx::SetXenonColour(v, Int(p, "xenonColour", 255));
    if (Has(p, "windowTint")) VEHICLE::SET_VEHICLE_WINDOW_TINT(v, Int(p, "windowTint"));
    if (Has(p, "plateStyle")) VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(v, Int(p, "plateStyle"));
    auto plate = p.find("plateText");
    if (plate != p.end() && !plate->second.empty())
        VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(v, const_cast<char*>(plate->second.c_str()));
    if (Has(p, "livery")) VEHICLE::SET_VEHICLE_LIVERY(v, Int(p, "livery"));

    auto ex = p.find("extras");
    if (ex != p.end()) {
        std::stringstream ss(ex->second);
        std::string part;
        while (std::getline(ss, part, ',')) {
            const size_t colon = part.find(':');
            if (colon == std::string::npos) continue;
            const int id = std::atoi(part.substr(0, colon).c_str());
            const bool on = part.substr(colon + 1) == "1";
            if (VEHICLE::DOES_EXTRA_EXIST(v, id)) VEHICLE::SET_VEHICLE_EXTRA(v, id, on ? 0 : 1);  // arg = disable
        }
    }
}

Hash ModelOf(const Props& p) { return static_cast<Hash>(std::strtoul(p.at("model").c_str(), nullptr, 0)); }

// ------------------------------------------------------------------ menu
void BuildEntry(Menu& m, const std::string& name, Menu* list);

void BuildList(Menu& m) {
    if (!g_loaded) Load();
    Menu* me = &m;
    m.AddAction("Save current vehicle...", [me] {
        const Vehicle v = CurrentVehicle();
        if (!v) { ShowStatus("Get in a vehicle first"); return; }
        std::string name;
        if (!AskText(name, 32, DisplayName(ENTITY::GET_ENTITY_MODEL(v)))) return;
        name = CleanName(name);
        if (name.empty()) return;
        const Props props = Capture(v);
        if (Saved* s = Find(name)) { s->props = props; if (Save()) ShowStatus("Updated \"" + name + "\""); }
        else { g_saved.push_back({name, props}); if (Save()) ShowStatus("Saved \"" + name + "\""); }
        me->Refresh();
    });
    for (const Saved& s : g_saved) {
        Menu& entry = m.AddSubmenu(s.name, s.name);
        const std::string name = s.name;
        entry.SetOnOpen([name, me](Menu& self) { BuildEntry(self, name, me); });
    }
    if (g_saved.empty()) m.AddAction("(nothing saved yet)", [] {});
}

void BuildEntry(Menu& m, const std::string& name, Menu* list) {
    Saved* s = Find(name);
    if (!s) { m.AddAction("(deleted)", [] {}); return; }
    const Hash model = ModelOf(s->props);

    m.AddAction("Spawn", [name] {
        Saved* sv = Find(name);
        if (!sv) return;
        const Hash h = ModelOf(sv->props);
        if (!STREAMING::IS_MODEL_IN_CDIMAGE(h) || !STREAMING::IS_MODEL_A_VEHICLE(h)) {
            ShowStatus("This game doesn't have that model");
            return;
        }
        const Vehicle v = spawner::SpawnModel(h, name);
        if (v) Apply(v, sv->props);
    }, DisplayName(model));
    m.AddAction("Apply to current vehicle", [name] {
        Saved* sv = Find(name);
        const Vehicle v = CurrentVehicle();
        if (!sv || !v) { ShowStatus("Get in a vehicle first"); return; }
        if (ENTITY::GET_ENTITY_MODEL(v) != ModelOf(sv->props))
            ShowStatus("Different model - parts may not match", 3500);
        Apply(v, sv->props);
        ShowStatus("Applied \"" + name + "\"");
    });
    m.AddAction("Overwrite with current vehicle", [name] {
        Saved* sv = Find(name);
        const Vehicle v = CurrentVehicle();
        if (!sv || !v) { ShowStatus("Get in a vehicle first"); return; }
        sv->props = Capture(v);
        if (Save()) ShowStatus("Updated \"" + name + "\"");
    });
    m.AddAction("Rename...", [name, list] {
        Saved* sv = Find(name);
        if (!sv) return;
        std::string n;
        if (!AskText(n, 32, name)) return;
        n = CleanName(n);
        if (n.empty() || n == name) return;
        if (Find(n)) { ShowStatus("\"" + n + "\" already exists"); return; }
        sv->name = n;
        if (Save()) ShowStatus("Renamed to \"" + n + "\"");
        g_root->Back();
        list->Refresh();
    });
    // Delete needs two presses within a few seconds.
    static std::string armed;
    static ULONGLONG armedUntil = 0;
    m.AddAction("Delete", [name, list] {
        if (armed != name || GetTickCount64() > armedUntil) {
            armed = name;
            armedUntil = GetTickCount64() + 3000;
            ShowStatus("Press Delete again to remove \"" + name + "\"", 3000);
            return;
        }
        armed.clear();
        for (size_t i = 0; i < g_saved.size(); ++i)
            if (g_saved[i].name == name) { g_saved.erase(g_saved.begin() + i); break; }
        if (Save()) ShowStatus("Deleted \"" + name + "\"");
        g_root->Back();
        list->Refresh();
    });
}

}  // namespace

void Setup(Menu& root, Menu& menu) {
    g_root = &root;
    menu.SetOnOpen(BuildList);
}

}  // namespace garage
