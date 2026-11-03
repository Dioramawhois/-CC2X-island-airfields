# Island Airfields

Development version **2.0.0-client-beta.1** moves the complete airfield policy into
its own Lua/content package. Aircraft approach, runway sharing, hangars, servicing,
launch and cancellation use public CC2X APIs. The player package contains no mod
DLL. Workshop identity **3826940003** is retained.

This client beta has passed user-reported ordinary-host gameplay and map checks. It requires the matching CC2X
**0.8.0-client-beta.1**, Lua API 2 and the guarded `waypoint-origin-v1` capability.
The older C implementation remains a regression reference under `native/`.
See the Workshop description and README_INSTALL.txt for installation.
Build tools do not replace installed games or existing mods.

Remote clients submit landing requests through the game's existing typed waypoint
channel. The host validates incoming requests and owns the flight policy. Local
mailboxes carry information between local screens and the mod; they are not RPC.
A remote marker does not confirm host support or occupancy. A separate licensed
client and a reviewed dedicated profile remain acceptance requirements.

## Developer build and validation

Use the isolated external SDK `../cc2x-network`; ModLoader work is independent.
The game argument supplies legitimate official Mod SDK screen sources. Patches
are applied in memory, and the package records their source hashes. Output must
be a new absolute directory. These commands do not install or launch the game.

```text
python package_lua.py --sdk ../cc2x-network --game GAME --luac LUAC --output ABSOLUTE-NEW-DIRECTORY
python native/tests/run_lua_test.py --sdk ../cc2x-network --zig ZIG
python native/tests/run_order_test.py --sdk ../cc2x-network --zig ZIG --report native/dist/order-test-results.json
python -m unittest discover -s native/tests -p test_*.py
```

The Lua fixture runs the actual mod through the actual SDK Host with a synthetic
engine, including helicopter/plane landing and launch, runway sharing,
cancellation, session replacement and ownership loss. It is not a network or
PhysX test. The native order fixture separately preserves the reference handler.
See [multiplayer findings and acceptance](docs/MULTIPLAYER.md) and
[CHANGELOG](CHANGELOG.md). Test-only runtime drivers are outside the player package.

## License

GNU General Public License v3.0 or later ([LICENSE](LICENSE)) for everything in this repository.
The game's scripts are not in it and are not covered: `lua/*.patch` contain only our changes, the
game's own code is taken from the local installation at build time.
