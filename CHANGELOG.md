# Changelog

## 2.2.0-client-beta.1 (unreleased candidate)

- Separate airfield icons from island hubs, using real runway positions.
- Replace automatic ground/first-waypoint landing with explicit airfield actions.
- Add exactly four hangar bays, aircraft selection and a launch destination picker.
- Add free compatible hardpoint replacement/clearing with full new attachments.
  Use the optional shared CC2X equipment API; inventory accounting is deferred.
- Preserve original map order call sites and the corrected 2.1.2 flight circuit.
  Stock and Revolution 1.6-19/1.6-20 remain separate, hash-pinned integrations.
- Add real-Lua UI/input and policy regression fixtures. This candidate requires
  fresh UI and ordinary-host gameplay acceptance before publication.

## 2.1.2-client-beta.1 (2026-11-05)

- Guide an oblique fixed-wing arrival toward the outer runway approach circuit
  first, instead of a point beside the island before flying back out to enter
  the circuit. Derive the target from the real runway endpoints and selected
  landing direction. Preserve the existing aligned-final shortcut.
- Keep native queue distance, streaming deferral, runway reservations, ordinary
  order cancellation, helicopter policy and takeoff behavior unchanged. This is
  independent Lua mod policy and requires no SDK or native binary change.
- Add 12 owned actual-SDK full-flight regressions for approach targets and
  convergence. The owner confirmed the corrected oblique approach in a fresh
  ordinary-host game. Broader terrain/physics, remote-client and dedicated-server
  acceptance remain unverified; see `docs/RUNWAY_APPROACH.md` for its boundaries.

## 2.1.1-client-beta.1 (2026-11-05)

- Restore the vehicle-control landing-order integration for Revolution 1.6-20.
  The previous release's 1.6-19 profile did not match the updated screen, so the
  SDK skipped its six typed landing-waypoint replacements and owned helper.

- Add the reviewed 1.6-20 vehicle-control source profile with freshly derived
  byte edits. Preserve Revolution's new dock-state rendering logic and all six
  existing stock/1.6-19 profile records. Keep the independent gameplay policy,
  Workshop identity and SDK dependency unchanged.
- Validate complete inventories and metadata for both Revolution versions during
  profile authoring; require all supported versions before replacing the
  descriptor. Reject unknown inputs and conflicting edits for an identical hash.
- Cover UTF-8 byte shifts, invalid hash-only repinning and profile retention with
  owned fixtures. Validate actual SDK transformation/syntax for stock and both
  Revolution versions without executing third-party Lua. Fresh 1.6-20 gameplay,
  remote-client and dedicated-server acceptance remain unverified.

## 2.1.0-client-beta.1 (2026-11-04)

- Deliver Island Airfields as a separate addon alongside the original Revolution
  1.6-19. Remove all complete screen resources and the private combined-package
  builder. The player payload contains no Revolution files and has zero resource
  path overlaps with it.
- Require unified SDK 0.8.1 with hash-pinned, syntax-checked in-memory screen edits.
  Ship only reviewed source hashes, owned insertions and owned helper Lua. Stock
  and Revolution profiles preserve map controls and typed host landing requests.
- Include the carrier-specific flight-deck indication fix. Keep the independent
  host gameplay unchanged. Validate actual SDK source transformation/syntax for
  three stock and three Revolution screens without executing third-party Lua.
- Earlier prototype host acceptance remains historical; fresh separate-addon
  gameplay, remote client and dedicated-server acceptance remain unverified.

## Unreleased: Revolution 1.6 local compatibility candidate

- Test candidate 2: restrict flight-deck landing/queue/taxi indications to this
  carrier's docking parent, and launch/holding indications to its attached
  aircraft. Island traffic and other carriers no longer occupy the display.
  Cover all five states and both parent types in the real-Lua UI fixture.

- Add a separate reviewed adapter for Revolution 1.6-19: preserve its screen
  callbacks, map transforms, fog of war and ordinary/group order paths while
  adding Island Airfields UI and typed landing requests. Keep all five host
  module files unchanged; do not include a native gameplay binary.
- Pin the original manifest and all 40 Revolution content resources. Build a
  separate local combined package with 37 byte-identical resources and three
  reviewed Git screen patches; reject changed inputs and preserve source files.
- Add profile/Git regressions, real-Lua synthetic UI checks and owner gameplay
  test cases. This candidate has no game/network acceptance or redistribution
  approval; the public 2.0 player package is unchanged.

## 2.0.0-client-beta.1 (2026-11-04)

- Own-island airfield markers use checked stock generation metadata before
  terrain streams, so ownership does not require another reconnaissance flight.
  Keep normal team colour and suppress islands whose generation has no runway.
  Actual controller data remains authoritative. Pin the reviewed engine in
  package provenance and cover both map screens with real Lua regression.

- Move complete gameplay policy to a separate Lua/content package using public
  SDK primitives: helicopters, fixed-wing flight patterns, runway locks, bounded
  deferred orders, hangars, servicing, launch and cancellation. No gameplay DLL
  is included. Keep the native implementation as a regression reference.
- Require verified incoming waypoint origin on the host and reject absent or
  stale context. Clear session state and stop control on ownership changes.
- Use the existing incoming handler's continuation for its one final broadcast;
  reserve explicit publication for deferred/direct work. The hook precedes the
  original broadcast, as checked against the actual client executable.
- Add actual-Host Lua regression and native-free reproducible package checks.
  Screens tolerate an unavailable legacy debug channel.
- Private copied-client tests have reached real sessions with the Lua policy;
  full flight/physics and remote-client acceptance are still being investigated.
  Local messages are not a host handshake. Dedicated support is unverified.

## Earlier isolated multiplayer order safety

- A new waypoint order immediately cancels an earlier deferred landing. Remove
  the 30-tick age exemption, which could allow a canceled landing to execute
  when the island loaded soon after a replacement move or cleared order.
- Report full deferred-request queues as rejected. Replacing an aircraft's own
  queued request remains possible at capacity without evicting another aircraft.
- Enable assertions explicitly in the optimized order fixture; previous Zig
  optimized builds could print PASS with their `assert` checks compiled out.
  Add regression cases for cancellation, exhaustion/replacement, rejected new
  orders, session reset and lost island ownership, plus optional hashed evidence.
- These fixes maintain the native reference alongside the Lua implementation.
  They add no transport/handshake, install nothing and do not certify multiplayer.

## 1.1.0-dev / plugin v46 (unreleased)

- Complete package builder uses the separate common SDK, records plugin/compiler/
  header/source hashes, includes redistribution notices and checks Lua syntax.
  Existing outputs are preserved; the builder no longer deletes installed mods.
  Workshop ID 3826940003 is retained in opt-in SDK dependency metadata.
- Real combined Windows client session with AI Bridge, Hold/Modes/Brake and custom
  aircraft passed native loading and unchanged-catalog aircraft spawn/save/reload.
  Island landing/flight and real multiplayer/dedicated remain separate gates.

- Requires CC2X API 4 + session extension v1 (0.6.0-dev). Direct native landing requests are session-bound;
  stale/absent local-server commands are rejected instead of crossing game loads.
- Remote client Lua can request landing through the game's existing typed waypoint
  channel without a local native plugin. The host retains aircraft/island validation
  and fixed-wing policy. Remote occupancy is unknown (dim marker), not a confirmed
  host response. Host receipt and replication still require a real network test.
- Session functions are negotiated by the named core export and checked table,
  so Native Bridge's unrelated API 5-8 tails cannot be mistaken for callbacks.
- Final waypoint changes are published exactly once after accepted, rejected,
  pending, deferred or direct landing processing. Previously the ordinary request
  handler had no explicit post-mutation publication. The regression uses the actual
  production handler against fake API values, not a game/network session.
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
