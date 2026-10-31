# Changelog

## Unreleased — API 5 landing submission

- Requires CC2X API 5; no fallback to legacy cross-session `server_post`.
- Direct native Lua landing now requires `land(vehicle_id, tile_id, session_token, expected_team)`
  and returns `accepted, reason`. Capture `session()` with the source UI data. Two-argument calls
  fail closed. Acceptance means queued, not successful landing.
- Deferred payload is released on execution, cancellation or enqueue rejection; cancellation
  never accesses the world. Logic-thread execution revalidates session/server and human vehicle
  and tile team before requesting landing. The SDK exposes no authenticated peer/host identity;
  this trusted local Lua entry point is not an MP authorization boundary.
- Existing UI landing waypoints are unchanged. Portable ownership tests are not Windows/in-game
  validation. `cc2x-sdk.json.tested_version` remains the historical 0.4.0 baseline, not an API 5
  runtime certification; no published SDK revision is pinned for this local change.
- Flight director kinematics (runway geometry, plane flight/rollout/takeoff roll, tick length,
  per-flight state lifecycle) moved unchanged into the platform-neutral C core
  `native/plugin/iaf_fd_core.c`, called directly by the plugin. Bit-exact native regression
  against the previous formulas; the DLL must now link `iaf_fd_core.c`. Not in-game validated.
- Plane landing phase/hold/go-around/touchdown decisions now use the same production core,
  with explicit POD observations/actions and host-ordered SDK effects. Regression vectors cover
  admission refusal, both pattern sides, all landing phases, third-attempt abort and touchdown
  thresholds. Preserve the original positive rotation predicate: NaN speed does not rotate.

## 1.0.1 (2026-10-25)

- The plugin uses only the public CC2X API (world API, CC2X 0.4+): no game addresses of its own.
  Anyone can write a mod like it with the SDK. Behaviour unchanged (same acceptance tests).
- Needs CC2X 0.4; with an older CC2X the map shows "needs a newer CC2X".

## 1.0 (2026-10-25) - first public version

- Planes and helicopters land on the airfields of captured islands, are repaired, refuelled and
  rearmed in the hangars and take off on a new order. Every phase is flown by the plugin (flight
  director): terrain-aware glide path and pattern, one plane per runway, helicopters on their own
  pads, cancel by a new order until touchdown, landings resumed after loading a save.
- Installed like any Workshop mod; the plugin lives in the mod folder (`content/cc2x/`, the part the game uploads), CC2X 0.3.1+ loads it.
  Planes on by default, optional settings in `%APPDATA%\Carrier Command 2\cc2x\island_airfields.ini`.
- The map says in one line when CC2X is missing or too old.
- Known limitation: aircraft are moved between runway / pad and hangar bay (no taxiing yet).

## Next: 1.1

- Taxiing between runway and hangar, no teleports (draft on branch `taxi-wip`).
