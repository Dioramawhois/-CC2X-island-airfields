# Revolution compatibility: separate addon

Island Airfields **2.1.1-client-beta.1** is a separate package for the
original **Revolution 1.6-19 or 1.6-20**, using **CC2X SDK 0.8.1-client-beta.1**.

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

Stock and both reviewed Revolution versions cover holomap, vehicle control and propulsion.
The map edits preserve Revolution's own update/input callbacks, fog of war,
coordinate transforms, patrols and support orders. Six individual/group waypoint
call sites use the existing typed landing channel. Optional cached vehicle
proxies are unwrapped only for the order operation.

Aircraft landing orders are issued from the vehicle-control map. A new first
waypoint or a drag that replaces the first waypoint can use the landing helper;
later route points remain ordinary movement orders. Revolution's holomap
provides carrier route editing and aircraft camera/airlift actions. Its carrier
route reconstruction calls are preserved unchanged.

The propulsion classifier follows stock carrier-parent semantics: arrivals,
queue and taxi require this carrier's docking parent; launch and holding require
this carrier's attached parent. Island traffic and other carriers cannot occupy
this flight-deck display. The host flight policy is unchanged.

## Verification

- Eighteen Python profile/packaging regressions pass, including rejection of whole
  screen caches in the separate addon.
- Real Lua executes only owned helpers and synthetic UI fixtures: markers,
  transforms, moves/routes, group/proxy handling, remote request encoding,
  parked/queued indicators and all five carrier deck states.
- The actual SDK transformer discovers the addon and transforms/compiles all
  three real stock screens and all three screens from each reviewed Revolution
  version. Revolution output must match the independently applied reviewed Git
  patches byte-for-byte before the owned helper is appended. It never executes
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
local sources and reviewed owned patches. Pass each supported version explicitly:

```text
python compatibility/generate_screen_profiles.py --game GAME --revolution REVOLUTION_1_6_19 --revolution REVOLUTION_1_6_20
```

The authoring tool checks the complete 40-resource inventory and `mod.xml` hash
against `compatibility/revolution_1_6/profile.json` and `profile_1_6_20.json`.
It requires every supported review before writing the descriptor, preserves
distinct source profiles, and refuses different edits for the same source hash.
Original source is used only locally, never included in the Workshop payload.

Revolution 1.6-20 changes only `screen_vehicle_control.lua` among those 40
resources; the other 39 are byte-identical to 1.6-19. Its new dock-state rendering
logic is preserved. The eight Island Airfields edits are freshly derived from
the reviewed 1.6-20 bytes: the first insertion shifts by 99 bytes and the seven
remaining edits shift by 81 bytes. Merely changing the 1.6-19 hash would target
incorrect positions. All six previous stock/1.6-19 profile records are retained
unchanged. An owned UTF-8 fixture also checks byte offsets and demonstrates why
hash-only repinning is invalid.

Review changed screen hashes rather than silently patching unknown versions.
The 1.6-20 checks establish offline transformation and syntax compatibility;
fresh gameplay with that version still requires a separate owner check.

## Owner check

Use SDK 0.8.1, the original Revolution subscription and Island Airfields enabled
as two separate mods. In Multiplayer > Host > Custom, check both maps and
Revolution tools, land/service/launch a helicopter and a loaded plane, cancel an
approach and issue group/mixed-unit orders. Island traffic must leave the carrier
deck display idle; real carrier arrivals/launches must retain their indications.
Save/reload and repeat. Report the first failing step and `cc2x.log`.
