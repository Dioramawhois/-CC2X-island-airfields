# Revolution compatibility: separate addon

Island Airfields **2.1.0-client-beta.1** works as a separate package alongside the
original **Revolution 1.6-19**, using **CC2X SDK 0.8.1-client-beta.1**.

Enable both mods. Island Airfields does not include, replace or redistribute
Revolution's files. The earlier private combined-package experiment is retired;
it was never published. Its builder is removed from the current source.

## Delivery contract

The addon contains five independent Bridge files, owned map/carrier UI helpers
and `content/cc2x/screen_extensions.json` (schema 2). There are no resources under
`content/scripts`, so it has zero resource overlaps with the reviewed Revolution.

For each supported screen, the declaration contains canonical UTF-8 SHA-256
profiles and small owned byte edits. The SDK strips an optional BOM, normalizes
CRLF to LF, checks the exact source and validates offsets, bounds and Lua syntax.
It modifies only its in-memory compilation buffer and appends the owned helper
atomically. Unknown sources retain their original code and receive no helper.
Installed and subscribed files are never written.

Stock and Revolution profiles cover holomap, vehicle control and propulsion.
The map edits preserve Revolution's own update/input callbacks, fog of war,
coordinate transforms, patrols and support orders. Six individual/group waypoint
call sites use the existing typed landing channel. Optional cached vehicle
proxies are unwrapped only for the order operation.

The propulsion classifier follows stock carrier-parent semantics: arrivals,
queue and taxi require this carrier's docking parent; launch and holding require
this carrier's attached parent. Island traffic and other carriers cannot occupy
this flight-deck display. The host flight policy is unchanged.

## Verification

- Eight Python profile/packaging regressions pass, including rejection of whole
  screen caches in the separate addon.
- Real Lua executes only owned helpers and synthetic UI fixtures: markers,
  transforms, moves/routes, group/proxy handling, remote request encoding,
  parked/queued indicators and all five carrier deck states.
- The actual SDK transformer discovers the addon and transforms/compiles all
  three real stock screens and three real Revolution screens. It never executes
  third-party Lua. Unknown-profile, range, overlap, syntax and atomic hook delivery
  failures also have SDK native fixtures.
- Actual SDK Host fixtures cover the unchanged helicopter/plane landing/launch,
  cancellation, shared runways, ownership and session policy on a synthetic engine.
- Package checks require zero Revolution resource overlaps, no complete game
  screens and no native gameplay binary.

The owner reported the earlier integration prototype working except for the
false carrier Landing indication. The corrected classifier passed offline tests.
That prototype result is not fresh gameplay acceptance for the new separate-addon
delivery. Remote clients and dedicated servers remain unverified.

## Maintenance

`compatibility/generate_screen_profiles.py` authors declarations from legitimate
local sources and reviewed owned patches. Original source is used only locally
for this verification, never included in the Workshop payload. Review changed
screen hashes rather than silently patching unknown versions.

## Owner check

Use SDK 0.8.1, the original Revolution subscription and Island Airfields enabled
as two separate mods. In Multiplayer > Host > Custom, check both maps and
Revolution tools, land/service/launch a helicopter and a loaded plane, cancel an
approach and issue group/mixed-unit orders. Island traffic must leave the carrier
deck display idle; real carrier arrivals/launches must retain their indications.
Save/reload and repeat. Report the first failing step and `cc2x.log`.
