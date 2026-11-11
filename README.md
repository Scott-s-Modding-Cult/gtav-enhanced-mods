# GTA V Enhanced Mods

Script Hook V mods and modding tools for **GTA V Enhanced, story mode**.

**You need your own copy of GTA V.** This repository holds no game files. Not affiliated with
Rockstar Games or Take-Two. Single player only: don't take these into GTA Online.

## Installing (players)

Get **`GTAV-Enhanced-Mods.zip`** from the [Releases page](https://github.com/Scott-s-Modding-Cult/gtav-enhanced-mods/releases).

1. Install [Script Hook V](http://www.dev-c.com/gtav/scripthookv/) (`ScriptHookV.dll` and `dinput8.dll`)
   in the game folder. To find it: right-click GTA V Enhanced in Steam, then **Manage > Browse local files**.
2. Copy the `.asi` files you want from the zip into that same folder. `NineLivesCat.ini` goes next to
   `NineLivesCat.asi`.
3. Start story mode. Press **F5** for the NativeUiMod menu.

To play Online again, remove `dinput8.dll` (and any other loader files) from the game folder.

Script Hook V refuses to load after most game updates until its author ships a new build, so expect a
short wait after each patch.

### Optional: the airstrike gun weapon

NativeUiMod's **Chaos > Give airstrike gun** uses an add-on weapon, `WEAPON_AIRSTRIKEGUN` (a flare gun
clone with its own wheel slot). It is in the zip as `optional\airstrikegun\dlc.rpf`. To use it you need
an RPF loader such as OpenRPF and a `mods\update\update.rpf` with `dlcpacks:/airstrikegun/` added to its
`dlclist.xml`; `tools\add-dlc-to-list.ps1` does the dlclist edit. Without it, **Flare gun also calls
airstrikes** gives the same effect with the normal flare gun.

## Mods

| Mod | What it does |
|---|---|
| **NativeUiMod** | An F5 menu drawn with game natives. Cheats (god mode, never wanted, infinite ammo, super speed/jump, explosive bullets, give all weapons), a speedometer and coordinates HUD, a vehicle spawner with search and an A-Z list, full vehicle customization (performance, paint, wheels, neon, plates, liveries, extras), a saved-vehicles garage, extra population, and a Chaos menu (gravity gun, airstrikes, meteor shower, zombie mode, maniac traffic, black hole, tornado and more). |
| **NineLivesCat** | Every cat (`a_c_cat_01`) gets nine lives. A dead cat stays down for a few seconds, then gets back up, until its last life. Settings in `NineLivesCat.ini`: `Lives`, `ReviveDelayMs`, `Notifications`. |

## Building (developers)

Needs Visual Studio with the C++ x64 tools, and the
[Script Hook V SDK](http://www.dev-c.com/gtav/scripthookv/) (not included here). For each mod, extract
the SDK so that `<Mod>\sdk\inc\natives.h` and `<Mod>\sdk\lib\ScriptHookV.lib` exist, then run
`build.bat`. The output is `<Mod>\build\<Mod>.asi`.

## Tools

PowerShell and Python scripts used to make the mods and test assets. Most use
[CodeWalker](https://github.com/dexyfex/CodeWalker)'s `CodeWalker.Core.dll`; every path
(`-GameDir`, `-CodeWalkerDir`, `-OutDir`, ...) is a parameter with a default you can override.

| Script | Purpose |
|---|---|
| `list-rpf.ps1`, `rpf-export.ps1` | List and export files from the game's RPF archives |
| `extract-peds.ps1` | Export ped models to raw RSC7 and CodeWalker XML/DDS |
| `ped_preview.py`, `sfm_preview.py` | Build a three.js page to preview exported models in a browser |
| `make-airstrikegun-meta.py`, `make-airstrikegun-dlc.ps1` | Build the airstrike gun add-on weapon pack |
| `add-dlc-to-list.ps1` | Add a pack to `mods\update\update.rpf`'s `dlclist.xml` |
| `make-ped-dlc.ps1`, `make-streamed-dlc.ps1` | Pack converted ped XML into a DLC pack |
| `convert-legacy-dlc.ps1` | Convert a Legacy-edition DLC pack to the Enhanced format |
| `build_nude_ped.py`, `sfm_to_freemode.py`, `source_mdl.py` | Experimental (adult content): retarget a body mesh onto a GTA ped; `source_mdl.py` reads Source engine `.mdl`/`.vtf`. They need body mods you supply yourself; none are included. |

Tip for CodeWalker scripting on Enhanced: set `[CodeWalker.GameFiles.RpfManager]::IsGen9 = $true` before
loading standalone Enhanced files, or loads fail with "illegal position!".
