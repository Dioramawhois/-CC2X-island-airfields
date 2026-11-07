# Landing intent across save/reload

Status: owner-confirmed on 2026-11-06 for beta 4, commit `99031db`, with the
matching runtime `d83d509d...`. The owner confirmed loading a save with three
landing orders and changing aircraft equipment. New graphical UI revisions
require their own visual acceptance; broader multiplayer acceptance is absent.

Earlier candidates erased `docking_queue_data.tile_node_id/hangar_index` when
taking over an approach and cleared the explicit waypoint when native docking
accepted it. Active intent then lived only in the Lua flight table. A deferred
command was also converted to an ordinary move before the island streamed.
The screen-local landing map disappeared whenever its Lua state was recreated.

Actual owner save inspection confirmed an active arrival with no saved docking
destination but an airfield hangar reservation, and a queued arrival with a saved
destination but no matching entry in the airfield's saved queue. Private saves
and research evidence remain outside distribution artifacts.

## Durable state and recovery

- A future terminal landing remains a typed waypoint after the earlier route
  points. It does not reserve a runway, clear the route or begin docking until
  it becomes the first waypoint. Pending route IDs are tracked separately from
  the existing unstreamed-island landing queue. On session recovery, the host
  reconstructs future intent from validated saved typed waypoints. Unknown
  incoming orders do not acquire this authorization.
- During approach, holding, rollout and parking, retain the real native docking
  tile and airfield index. Native serialization already saves these fields and
  the airfield hangar IDs/queues; no sidecar files or save-file rewrites are used.
- Before the island streams, retain the explicit typed landing waypoint. On the
  first tick of a new session, validate saved typed commands and restore deferred
  work. Ordinary movement waypoints never imply landing. New network orders still
  require the verified receive context; unattributed events cannot cancel arrivals.
- Reconcile loaded airfields against human-owned aircraft and exact saved targets.
  Restore missing queue entries once, and adopt an active arrival from its target
  or an existing saved reservation. Never choose an airfield by proximity alone.
- Existing queue order is retained. An orphan entry is appended; its original
  position cannot be recovered when the save contains no queue record for it.
- Legacy active saves with cleared docking fields can recover from their exact
  saved hangar reservation. If both associations were lost, the destination is
  not guessed. A new ordinary route or an explicit empty route cancels intent.

The detailed Lua phase is reconstructed from the current aircraft body state.
This does not promise continuation at an identical simulation tick or preserve
an in-progress takeoff phase. Three arrivals use the existing shared-runway lock;
waiting aircraft must remain visible as assigned arrivals.

## Map metadata

The host publishes `landing(vehicle_id)=tile_id` and `landing_stage(vehicle_id)`:
0 clear, 1 deferred/en route, 2 native queue, 3 holding, 4 approach, 5 rollout,
6 future landing after preceding route points.
This is display state, not a command transport or a replacement for native saves.
The map and holomap draw a dashed connection to the known airfield position,
QUEUE/HOLD/LAND/TAXI labels, destination text on hover and an arrival count beside
the marker and in its menu. Stage 6 uses ROUTE and the original waypoint path,
without an extra direct approach line. The screen-local click cache is only a fallback.
No approximate hub position is substituted when facility coordinates are absent.

The vehicle-control map resolves the typed terminal marker to the same known
airfield position. The engine's generic tile-waypoint getter otherwise returns
the island centre even when the order was created at the airfield coordinates.
An owned presentation adapter keeps the marker, route segments, hit testing and
drag origin attached to the field; the serialized token and flight policy stay
unchanged. Tests emulate this native centre-position behavior rather than only
storing the coordinates passed to `add_waypoint`.

## Validation and acceptance

Owned real-Lua/SDK fixtures discard the session's Lua state and restore only
engine values representing saved fields. They cover three arrivals, a missing
queue entry, legacy missing docking fields, complete landings into three bays,
deferred reload/streaming, explicit cancellation, unauthenticated empty events,
and refusal to adopt an unrelated nearby aircraft. Owned screen fixtures rebuild
three QUEUE/HOLD/LAND links without prior UI clicks and clear them on cancellation.
These fixtures do not execute the game's native serializer or prove runtime UI.

Before publication, use the exact candidate at x1: issue three arrivals, save
while one approaches and two wait, reload, inspect all three links/statuses and
confirm all three park in distinct bays. Repeat before the destination streams,
after cancellation and on the older owner save. Verify that saved parked aircraft
still expose the correct loadout/launch actions. Preserve the original saves.
For the terminal-marker correction, create one movement point, drag a second
point onto the airfield silhouette (including its upper nose/edge), and verify
that the line ends at that icon. Check the attachment again after screen reload
and save/reload, and confirm the first movement/attack point remains intact.
