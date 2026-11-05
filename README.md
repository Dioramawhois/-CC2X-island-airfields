# Island Airfields

**2.2.0-client-beta.5** is an unreleased development candidate of the separate Lua addon for stock Carrier Command 2 and
**Revolution 1.6-19 and 1.6-20**. Free loadout changes require the matching **CC2X 0.9.0-dev equipment candidate**. Workshop
identity [3826940003](https://steamcommunity.com/sharedfiles/filedetails/?id=3826940003)
is retained.

The published [2.1.2-client-beta.1 download](https://github.com/Dioramawhois/-CC2X-island-airfields/releases/tag/v2.1.2-client-beta.1)
remains the accepted runway-approach release. Version 2.2 has not been published.

Click the airfield icon for four aircraft bays, then click a parked aircraft to
open its loadout directly. Select hardpoints on the **same graphical widget used
by the carrier**, and choose compatible equipment by its picture. A separate
**Launch** button at the bottom returns to the map for the destination.

Landing is issued on the map by dragging the aircraft onto the airfield icon;
there is no landing menu button. Ordinary ground waypoints remain movement
orders. Equipment changes remain free and newly fitted attachments start full.
Arrival links and QUEUE/HOLD/LAND statuses remain available after save/reload.
The owner confirmed three-arrival reload and equipment changes in beta 4; this
new graphical UI still needs fresh in-game acceptance. Release preparation is
paused for this UX revision. [Landing persistence](docs/LANDING_SAVE_RELOAD.md).

The host runs the flight policy through public SDK APIs. The mod has no private
gameplay DLL and contains no Revolution files or complete game screens.
[UI contract, current limits and acceptance steps](docs/AIRFIELD_UI_2_2.md).

## Install

This development candidate is prepared for isolated testing. It needs its exact
matching equipment SDK binary; SDK 0.8.1 provides no weapon replacement primitive.
Do not replace the accepted installation to test it without preserving a backup.


Subscribe to Island Airfields and [CC2X SDK](https://steamcommunity.com/sharedfiles/filedetails/?id=3833014670).
With the game closed, copy the SDK's `content/dinput8.dll` beside
`carrier_command.exe`. Enable Island Airfields in Mods and restart.

For Revolution, subscribe to the [original Revolution item](https://steamcommunity.com/sharedfiles/filedetails/?id=3460002110)
and enable **both Revolution and Island Airfields**. No combined repack or
replacement of Revolution files is needed. Without Revolution, enable Island
Airfields normally. Automatic installation requires a ModLoader release that
explicitly supports this exact SDK binary.

[Illustrated installation](workshop/installation/README.md).

## Screen integration

The player package contains independent gameplay, owned UI helpers and hash-pinned
screen-edit declarations. SDK 0.8.1 checks the actual screen's canonical SHA-256
and applies reviewed edits only in memory. Unknown screen versions are skipped
and logged. The addon shares no resource paths with the reviewed Revolution
package, so neither mod replaces the other's screens.

Profiles cover stock CC2 1.5.18-r2 and Revolution 1.6-19/1.6-20 map/control screens. The
carrier deck display checks the destination carrier, excluding island landings
and other carriers' traffic. [Compatibility design and evidence](docs/REVOLUTION_COMPATIBILITY.md).

## Validation scope

The candidate passes owned real-Lua UI/callback and loadout-policy fixtures,
actual-SDK synthetic flight regressions, syntax-only screen delivery for stock
and both reviewed Revolution versions, and packaging safety checks. The SDK's
native equipment path also passes static checks against the exact executable.
The owner confirmed beta 4 save/reload with three arrivals and equipment changes
in an ordinary-host game. The new carrier-widget adapter has owned substitute
tests and static stock/Revolution interface inspection; its visuals and controls
need fresh in-game acceptance. Remote multiplayer and dedicated servers remain
unverified.

Distant host-side markers use the matching SDK CPU placement preview, with
no hub-based fallback. They share the hub marker style. Preview latency and
alignment still need in-game acceptance. Remote equipment commands are
disabled. Remote multiplayer and dedicated servers remain unverified.

## Developer build

Use the matching verified development equipment SDK, pinned Zig 0.15.2 and real Lua 5.3 syntax checker.

```text
python package_lua.py --sdk SDK --game GAME --luac LUAC --output NEW-ABSOLUTE-DIRECTORY
python -m unittest discover -s native/tests -p test_*.py
python native/tests/run_lua_test.py --sdk SDK --zig ZIG
python native/tests/run_revolution_ui_test.py --sdk SDK --zig ZIG
python native/tests/run_addon_delivery_test.py --sdk SDK --zig ZIG --game GAME --revolution REVOLUTION_1_6_19 --revolution REVOLUTION_1_6_20
```

Profile authoring reads legitimate local sources and ships only hashes and owned
insertions. It never executes Revolution Lua or writes to game/Workshop files.
Historical native/Wasm and whole-screen patches are engineering references.

## License

Owned code: GNU GPL-3.0-or-later; retain [LICENSE](LICENSE). Revolution remains a
separate third-party subscription with its own authors and distribution terms.
