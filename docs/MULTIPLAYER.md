# Island Airfields multiplayer development (2026-11-04)

Work is isolated in `island-airfields-network`, branch
`codex/multiplayer-order-safety` (base `8b6816c`), with external SDK
`cc2x-network`, branch `codex/multiplayer-session-safety`. ModLoader, ordinary
SDK/mod worktrees and the installed game are outside these edits.

## Implemented policy and packaging

`content/bridge/policy.lua` contains the complete gameplay implementation:
helicopter approach/descent/lift, plane approach patterns and takeoff, bounded
runway reservations and deferred orders, hangar parking/service and cancellation.
It uses public generic SDK operations and retains IDs/session keys instead of
long-lived engine objects. Ownership changes stop flight control. Session changes
clear policy state. The older native director is retained as a regression
reference. No private mod DLL enters the new player package.

`package_lua.py` consumes an explicit external SDK and legitimate local official
Mod SDK screens. `package_recipe.py` applies our patches in memory and records
base/patch hashes. A conflicting generated cache is refused. Existing output
folders and game resources are preserved. Package ID remains 3826940003 and its
development version is 2.0.0-client-beta.1.

The runtime dependency is CC2X 0.8.0-client-beta.1, API 8/Lua 2 plus the optional
`waypoint-origin-v1` capability. Unsupported profiles reject the early island
rules as well as normal activation. C and Lua share the same scoped origin
record. Its core epoch differs from Host generation and is not a network ID.

## Incoming orders and publication

The game's stock typed waypoint channel carries landing requests. The guarded
host receive hook supplies an engine peer ID and vehicle ID during its callback;
the mod requires matching verified incoming context before accepting a landing.
This does not establish the peer's team/admin role or a remote host handshake.
Vehicle and target-island ownership are checked by the host policy.

A new ordinary move or clear immediately revokes an earlier pending landing.
The deferred queue is bounded at 32 and reports rejection at capacity, while an
aircraft can replace its own entry. The receive hook runs BEFORE the original
handler's broadcast. Incoming accepted/rejected/pending modifications are
published by that continuation exactly once; direct and deferred work explicitly
publish their final waypoint state. The native fixture models both stages.

Local mailboxes supply screen status only. They are not RPC. A client's local
SDK does not certify the remote host. Remote occupancy remains unknown without a
host response. Engine peer IDs are not Steam identities.

## Validation evidence and limits

The optimized native order fixture now explicitly enables assertions; previous
Zig builds could erase them while printing PASS. It covers publication ordering,
capacity/replacement, cancellation, session reset and ownership loss. The Lua
fixture executes the actual mod in the actual SDK Host with synthetic engine
objects and flat-ground velocity integration. It covers helicopter and plane
landing/launch, two-plane runway contention, unsupported origin, cancellation,
same-ID session replacement and ownership changes. These are offline regressions,
not PhysX or network acceptance. Package regressions cover fresh, matching cached
and conflicting cached screens.

Private copied-client sessions load the Lua policy with zero module errors and
exercise real session replacement. The legitimate copied executable is unchanged;
a test-only early folder adapter redirects its profile before game entry. Exact
DLL/package/fixture hashes and logs are kept under the private sandbox. Test
entry/UI drivers are outside the player package. The pause-screen vehicle getter
is read-only, so the test UI uses the proper focused vehicle-control screen.
Actual landing/physics investigation is still in progress.

On 2026-11-04 the user reported that the manual host test otherwise appeared to
work, but owned airfields were grey and appeared/disappeared only on approach.
This is user feedback, not a separate instrumented pass for every flight phase.
The follow-up UI uses checked generation metadata for owned islands immediately;
see [owned-island knowledge](OWNED_ISLAND_KNOWLEDGE.md). The user subsequently confirmed that the updated marker behavior works.

Only one licensed client is available. Second-client receipt/body replication,
authenticated sender permissions, capability handshake, disconnect/reconnect,
late join and save/reload are unverified. Dedicated bootstrap and layouts require
an independent guarded profile. Neither a local mailbox nor a local server pointer
proves remote authority. Do not publish this branch as verified multiplayer.

Developer commands are in README. Runtime fixture safety and provenance are
specified in the SDK's `docs/RUNTIME_SANDBOX.md`.
