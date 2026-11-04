# Island Airfields

**2.1.2-client-beta.1** is a local runway-entry test candidate for stock Carrier Command 2 and
**Revolution 1.6-19 and 1.6-20**. It requires **CC2X SDK 0.8.1-client-beta.1**. Workshop
identity [3826940003](https://steamcommunity.com/sharedfiles/filedetails/?id=3826940003)
is retained.

The published release is [2.1.1-client-beta.1](https://github.com/Dioramawhois/-CC2X-island-airfields/releases/tag/v2.1.1-client-beta.1).
This candidate changes the first assisted approach target and requires a fresh
in-game check before publication. See [Runway approach](docs/RUNWAY_APPROACH.md).

Aircraft land on owned island airfields, receive hangar service and launch on a
new order. The host runs the flight policy through public SDK APIs. The mod has
no private gameplay DLL and contains no Revolution files or complete game screens.

## Install

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

The unreleased runway-entry source candidate and its separate validation limits
are described in [Runway approach](docs/RUNWAY_APPROACH.md).

The owner accepted ordinary-host airfield behavior in the earlier integration
prototype and reported its carrier-status defect. The fix has real-Lua synthetic
coverage. The separate addon passes actual-SDK source transformation and syntax
checks for stock/Revolution, plus host-policy and UI fixtures. These checks do
not certify fresh separate-addon gameplay, a second remote client, dedicated
servers or arbitrary screen modifications.

## Developer build

Use an external verified SDK 0.8.1, pinned Zig 0.15.2 and real Lua 5.3 syntax checker.

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
