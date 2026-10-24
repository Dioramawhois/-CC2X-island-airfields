Island Airfields v44 - release candidate
Carrier Command 2 1.5.18, Windows (32-bit game)
=============================================

This is an experimental release. Planes and helicopters can land on captured
islands, use hangars for repair/refuel/rearm, and take off on a new order.
Ground taxiing is unfinished: aircraft are repositioned between runway/pad
and hangar. Flight control is assisted by the plugin.

INSTALL
1. Close the game. In Steam: Manage > Browse local files.
2. Copy the three files from "game folder" next to carrier_command.exe:
   dinput8.dll, cc2x_island_airfields.dll, cc2x_island_airfields.ini.
   Back up files already using these names. Do not overwrite another mod's
   dinput8.dll: CC2X and another DirectInput loader cannot share that filename.
   When updating, keep your existing INI if you want to keep your settings.
3. Copy "mods/Island Airfields" into:
   %APPDATA%\Carrier Command 2\mods\
   Enable Island Airfields in the game's mod menu. If you already use its
   Workshop or development copy, update that copy or disable it before enabling
   the packaged copy; use only one copy of the Lua mod.
4. Start the game and load a session. cc2x.log in the game folder should contain
   "CC2X 0.2.0 ready", "Island Airfields v44 active", and "session started".

PLAY
- Own islands with an airfield show a marker on the map.
- Give a flying aircraft a single waypoint near such an island to land.
- Give a parked aircraft a new route to take off.
- Planes share the runway; others wait their turn.

SETTINGS (read at game startup)
cc2x_island_airfields.ini is next to the game EXE.
  experimental_planes=1 : enable fixed-wing aircraft; 0 : helicopters only.
  plane_assist=1        : keep the plugin's flight assistance enabled.
This candidate enables both settings by default. Restart after changes.

DISABLE / ENABLE
Close the game before renaming DLLs.
- Disable just this plugin: rename cc2x_island_airfields.dll to
  cc2x_island_airfields.dll.disabled. Disable Island Airfields in the mod menu
  as well to remove its interface. Rename it back and enable the mod to return.
- Disable all CC2X plugins: rename dinput8.dll to dinput8.dll.disabled.
  Restore its name to enable CC2X again. Other CC2X plugins also depend on it.
- Disabling only the Lua mod in the menu does not unload the native plugin.

UNINSTALL
Close the game. Remove this plugin DLL and its INI; disable/remove the Lua mod.
Remove dinput8.dll only if no other installed mod needs CC2X. If another loader
was backed up during installation, restore it.

SCOPE AND REPORTS
- Windows game version 1.5.18. VR, dedicated server and macOS are not supported.
- Intended for single player. Multiplayer compatibility is not release-tested.
- Back up a save before trying this candidate; save/load needs a final release
  regression. Disabling the mod does not undo changes already saved in a world.
- Report the plugin version, aircraft type, island, exact steps, whether it is
  a loaded save, other installed mods, and attach cc2x.log immediately after
  the failure. The log is overwritten at the next game start.

The archive includes SHA256SUMS.txt and release-manifest.json to identify the
exact packaged files. Packaging checks do not replace an in-game flight test.
Russian instructions and release status: RELEASE_NOTES.md.
