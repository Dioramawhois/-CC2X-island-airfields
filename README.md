# Island Airfields

A Carrier Command 2 mod: player aircraft land on the airfields of captured islands, are repaired,
refuelled and rearmed in the hangars and take off again on a new order. It is a CC2X plugin (the
flight and airfield logic, C) plus two of the game's Lua screens (landing orders and map markers).

Players: see [README_INSTALL.txt](README_INSTALL.txt).

## Building

Needs the CC2X SDK next to this repository (`../cc2x`, see `cc2x-sdk.json`) and the game installed.

1. `python lua/build_lua.py` - builds `content/scripts/*.lua`: the game's own scripts from the
   official mod dev kit (`<game>/mod_dev_kit/source/scripts`) plus our patches `lua/*.patch`.
   After editing `content/scripts`, `python lua/build_lua.py --diff` rewrites the patches.
2. `python native/build.py` - builds the plugin `cc2x_island_airfields.dll`.

`native/package.py` builds the finished mod folder. Developer notes, tools and testing:
[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md). Changes: [CHANGELOG.md](CHANGELOG.md).

## License

GNU General Public License v3.0 or later ([LICENSE](LICENSE)) for everything in this repository.
The game's scripts are not in it and are not covered: `lua/*.patch` contain only our changes, the
game's own code is taken from the local installation at build time.
