# Changelog

## 1.0 (2026-10-25) - first public version

- Planes and helicopters land on the airfields of captured islands, are repaired, refuelled and
  rearmed in the hangars and take off on a new order. Every phase is flown by the plugin (flight
  director): terrain-aware glide path and pattern, one plane per runway, helicopters on their own
  pads, cancel by a new order until touchdown, landings resumed after loading a save.
- Installed like any Workshop mod; the plugin lives in the mod folder (`content/cc2x/`, the part the game uploads), CC2X 0.3+ loads it.
  Planes on by default, optional settings in `%APPDATA%\Carrier Command 2\cc2x\island_airfields.ini`.
- The map says in one line when CC2X is missing or too old.
- Known limitation: aircraft are moved between runway / pad and hangar bay (no taxiing yet).

## Next: 1.1

- Taxiing between runway and hangar, no teleports (draft on branch `taxi-wip`).
