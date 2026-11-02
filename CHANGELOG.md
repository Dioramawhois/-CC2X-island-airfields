# Changelog

## 1.1.0-dev / plugin v46 (unreleased)

- Requires CC2X API 5 (0.6.0-dev). Direct native landing requests are session-bound;
  stale/absent local-server commands are rejected instead of crossing game loads.
- Remote client Lua can request landing through the game's existing typed waypoint
  channel without a local native plugin. The host retains aircraft/island validation
  and fixed-wing policy. Remote occupancy is unknown (dim marker), not a confirmed
  host response. Host receipt and replication still require a real network test.
- Lua syntax, native build and SDK queue regressions pass offline. Ordinary-host,
  dedicated-server and disconnect/save acceptance remain unverified.
- This is the next version of Island Airfields: unchanged name, plugin filename
  and Workshop item 3826940003; no separate mod/subscription.

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
