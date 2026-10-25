# Island Airfields - development notes

Game: Carrier Command 2 1.5.18, Windows 32-bit. Needs CC2X 0.4+ (`../cc2x`, see `cc2x-sdk.json`).

## Build, install for testing

```
set CC2X_ZIG=<path to zig.exe 0.15.2>      (or put Zig into ../cc2x/.deps)
python native/package.py --install         -> dist/Island Airfields + %APPDATA%/Carrier Command 2/mods/Island Airfields
```

Enable the mod in the game's Mods menu once. CC2X must be in the game folder (`../cc2x`, `python build.py`).
`cc2x.log` in the game folder shows what was loaded and every flight phase of every aircraft.

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
