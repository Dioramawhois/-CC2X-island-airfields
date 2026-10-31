# Island Airfields - development notes

Game: Carrier Command 2 1.5.18, Windows 32-bit. Needs **CC2X API 5** headers and runtime
(`../cc2x`, or the explicit SDK override; see `cc2x-sdk.json`). Older SDKs fail compilation or
leave the plugin inactive; there is no unsafe legacy deferred-work fallback. The metadata's
`tested_version: 0.4.0` records historical testing, not validation of this unreleased API 5 path.

### Direct Lua landing submission

`cc2x.island_airfields.session()` returns the active session token (0 when closed).
Capture it together with the UI command's vehicle/tile IDs and expected team, then pass
`land(vehicle_id, tile_id, token, expected_team)`. Do not refresh an old command's token.
The result is `true, "queued"` or `false, reason` (`invalid_arguments`, `session_required`,
`session_closed`, `stale_session`, `queue_full`, `out_of_memory`, `invalid_request`,
`api5_required`). Missing tokens, including old two-argument calls, fail closed.
Accepted work may subsequently be cancelled or rejected by current-world validation; there is
no asynchronous landing-success acknowledgement. IDs support uint32_t without pointer packing.

Execution resolves IDs on the logic thread, checks the current session/server and human vehicle
and expected vehicle/tile team, and uses the existing landing eligibility checks. `expected_team`
is a stale-data check, not proof of the caller's team. SDK 5 exposes no authenticated peer/host
identity; this trusted local Lua API must not be exposed as an unvalidated network RPC. Existing
map UI patches do not call it: their game waypoint order path remains unchanged.
Closed admission, including SDK fail-closed handling of repeated same-address init without
an observed end, returns `false, "session_closed"` without retaining a payload. Admission is
restored only by the SDK's confirmed recovery path (observed end then init); the UI must not
retry old IDs with a refreshed token. The portable regression covers this production lifecycle
path through cancellation, refusal, recovery and stale-token rejection.

Cancellation only frees the heap payload; it never reads plugin/world state. Callback and plugin
code must remain loaded until completion. Normal SDK session end cancels outstanding work;
abrupt process exit does not promise callbacks. Windows hook affinity and real MP authority
remain dynamic validation requirements.

Portable ownership regression uses the real `landing_post.h` helper and the actual SDK queue:

```sh
bin=$(mktemp /tmp/iaf-landing.XXXXXX)
clang -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
  -I../Carrier-Command-2-extender/include -I../Carrier-Command-2-extender/src \
  native/tools/landing_post_test.c -o "$bin"
"$bin"
```

The include paths must point to the local API 5 SDK checkout; this is not a Windows DLL build
or a simulation of world/landing behavior.

### Flight director core

`native/plugin/iaf_fd_core.{h,c}` holds the flight director's per-flight kinematic state
(`iaf_fd_kin`: reset/seed/line-up lifecycle), tick length, runway geometry, plane flight,
rollout and takeoff roll. It has no Win32, CC2X API, globals or game layouts: fixed-width POD
inputs/outputs only. `island_airfields.c` reads the world through the SDK, calls the core and
applies its `iaf_fd_cmd` via `body_set`. The plane landing state (`iaf_landing_state`) and
hold/inbound/downwind/base/final/go-around/abort/touchdown decisions also live in the core.
`iaf_landing_step` returns explicit actions and flight targets; `iaf_landing_touchdown` runs
separately so the adapter deploys gear before reading wheel contact, exactly as before.
A returned early-transition action consumes the tick, while base/downwind transitions may
still fly on the same tick. Runway acquisition/release, terrain probes, hangar docking,
helicopter steering, takeoff phase selection and logging stay in the plugin. The DLL is built from both sources
(`island_airfields.c` + `iaf_fd_core.c`). This is a source boundary, not a portable binary/SDK.

Regression (same core functions; old formulas inlined as a bit-exact baseline):

```sh
bin=$(mktemp /tmp/iaf-fdcore.XXXXXX)
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all \
  native/plugin/iaf_fd_core.c native/tools/fd_core_test.c -lm -o "$bin"
"$bin"
```

## Build, install for testing

```
set CC2X_ZIG=<path to zig.exe 0.15.2>      (or put Zig into ../cc2x/.deps)
python native/package.py --install         -> dist/Island Airfields + %APPDATA%/Carrier Command 2/mods/Island Airfields
```

Enable the mod in the game's Mods menu once. CC2X must be in the game folder (`../cc2x`, `python build.py`).
`cc2x.log` in the game folder shows what was loaded and every flight phase of every aircraft.

The DLL is built from the translation units listed in `SOURCES` in `native/build.py`
(`island_airfields.c`, `iaf_fd_core.c`); add every new `.c` there. The build first removes
`native/dist/cc2x_island_airfields.dll`, so a failed build leaves no older DLL behind.

Cross-build on macOS (developer only; players need nothing of this). Zig 0.15.2 `aarch64-macos`
from `https://ziglang.org/download/index.json`, shasum checked before unpacking into the CC2X
checkout's `.deps`:

```
python native/build.py --sdk ../Carrier-Command-2-extender --zig ../Carrier-Command-2-extender/.deps/zig-aarch64-macos-0.15.2/zig
```

Last local build, 2026-10-31, exit code 0, no warnings. Local trace, not a release:

- Result: `native/dist/cc2x_island_airfields.dll`, PE32 DLL Intel 80386, sha256
  `1313e633061a5b3ff9236a85365b1d1ebf12dd9112669d28de2c50b72ca4bab8`.
- Exports: `cc2x_plugin_load`, `cc2x_island_airfields_debug`.
- Sources (sha256): `island_airfields.c` `c5afc92bffa599d97a60325e894bd358a5b8d7ddd8aa27cd9c43ce24af434f99`,
  `iaf_fd_core.c` `a6f1829054629b08e057c1e42205056c0bd1e61fdd490310d6c57852e6db007b`,
  `iaf_fd_core.h` `f9a5851f4d00b15801b8e7b97c959e553af35f91e423de8ac202e6b56f0b6a4e`,
  `landing_post.h` `59d5e1cdc161658c8fb9d49e59e87371281082f5719f32f25f8c9271095f1f89`.
- Not run on Windows or in the game. A Mac cross-build makes the same native Win32 DLL; it does not
  make the game logic portable.

## How it works

Captured islands have an airfield controller only for AI teams; the plugin lets the game create it for
the player too (S1/S2), skips the AI garrison spawner (S3), turns a waypoint of type
`barge_load_tile` into a landing order (H1, set by the Lua map screen), assigns hangar bays (H4) and
supervises every airfield tick (H5).

The vanilla island airfield code is unfinished for player aircraft (decompiled `FUN_00628600`): it
holds aircraft on a 3.2 km circle until they happen to be at the right angle, flies planes in at
18 m/s down a 22 deg slope (they stall into the ground), accepts a touchdown only below 0.5 m/s sink
and takes off without heading control. So the plugin flies every player aircraft itself - the
**flight director**, one explicit phase at a time (all logged), velocity and attitude set on the
PhysX body each tick, the game's autopilot gets the same target so engines and sound keep running:

- plane landing: inbound - downwind (two turn radii beside the final) - base (180 deg turn) - final
  (glide path from the terrain plan) - rollout (braked like the carrier's wire) - parked
- plane takeoff: lined up on the runway - roll (9 m/s^2) - climb - handed back to the game at 120 m
- helicopter landing: inbound (down an approach line to a 15 m hover over its pad) - descend - parked
- helicopter takeoff: on its pad - vertical lift to 40 m - handed back
- one plane on a runway at a time (others hold beside the pattern); a new order cancels a landing
  until touchdown; aircraft saved in the middle of a landing are picked up after loading

Terrain: the game's own PhysX scene query (`PxScene` vtable +0x158 raycast, actor `getScene` slot 6).
Once per airfield the plan scans 3.5 km around it: pattern height, landing end and glide slope
(flattest line to a touchdown 60 m past the threshold that clears the ground), takeoff end.

**1.0 limitation:** parking in the hangar bay and lining up on the runway / pad move the aircraft
(like the carrier elevator). 1.1 replaces this with taxiing; the draft is on branch `taxi-wip`.

## Airfield prefab (same on every island, airfield coordinates, metres)

- runway along x at z -140: start node x 75, end node x -150; turn loop south of the west end
- lane at x 0 from z -130 up into the hangar; hangar building x -31..31, z -38..35, open to the
  south and north, central aisle x 0, bays (+-20, +-20) facing the aisle; floor 0.77 m below the nodes
- helicopter pads (ours): (+-12, -70) and (+-12, -95)
- `native/tools/nodes.py` dumps the nodes, `scan_png.py` renders a ray scan (plugin debug field
  `scan_tile`) as a height map

## Hard-won lessons

- Never set the pose of an aircraft that stands on its wheels: the game destroys it. Set it down
  from above (or drive it).
- Velocity control needs feed-forward on a glide path, or the plane lags ~20 m above it.
- Fix the turn direction of a 180 deg turn when it starts; recomputing it flips halfway.
- `mods.xml` (`%APPDATA%\Carrier Command 2`) lists enabled mods with absolute paths; stale entries
  from other PCs are normal.

## Testing

All tools are in `native/tools` (Python 3, no packages; they read the running game's memory):

| tool | |
|---|---|
| `fd_test.py [ids]` | acceptance test: every parked aircraft takes off, flies out, lands; flying ones land. PASS needs no go-around, helicopter descent <= 20 s and the aircraft alive in its hangar 4 s later |
| `cancel_test.py` | landing cancelled by a new order, then landing again |
| `iaf.py` | status / island / vehicle / move / land / trace |
| `world.py` | own aircraft, carriers, islands with airfields |
| `trace_local.py`, `trace_wheels.py` | aircraft in airfield coordinates; hp, wheels, height |
| `wait_log.py` | wait for a pattern in this run's cc2x.log |
| `screen.py` | screenshots and input for menu automation (windowed mode only) |

Loading a save without a person (windowed 1920x1080): `screen.py focus; key 0x20; rel 0 100;
key 0x45; move 950 282; rel 2 2; key 0x45` opens the save list, first slot at `move 960 262;
rel 2 2; key 0x45`.
