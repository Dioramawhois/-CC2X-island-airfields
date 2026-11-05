# Airfield controls: 2.2.0 candidate

Status: unreleased UX revision; fresh graphical UI acceptance pending. The owner
confirmed beta 4 save/reload with three arrivals and free equipment changes on
2026-11-06. This revision preserves that flight policy and shared SDK binary.
This document supersedes the 2.1.x automatic first-waypoint landing UI.

## Player flow

An airfield has an owned aircraft silhouette at its actual facility origin.
Like the island hub, it uses a 16-pixel marker, a one-pixel black edge, the
island team colour and white hover highlighting. It is separate from production.
The island hub retains its original icon and production actions. The airfield
hit area has a fixed seven-pixel radius; ordinary ground points remain movement
orders at every map scale.

Click the airfield icon for exactly four numbered aircraft cards. Click a parked
aircraft once to open its loadout directly. Empty, busy and unavailable bays are
labelled and cannot open another aircraft. Narrow cards use the stock chassis
symbol and abbreviation; larger cards use the chassis image. Arrival counts
remain visible separately from parked aircraft.

The aircraft editor calls the existing carrier
`imgui_vehicle_chassis_loadout` widget through an owned input adapter. It uses
the game's chassis image, hardpoint positions, installed equipment symbols and
supply bars. No carrier screen or copied widget implementation is shipped.
The adapter binds each physical hit box to its actual attachment index; layouts
come from `get_ui_vehicle_chassis_attachments`, not invented point positions.
Missing/unsupported widget data is shown explicitly rather than guessed.

Click a point on the aircraft to choose compatible equipment by its picture.
The focused/hovered option has a readable name; the fitted option is marked.
Hidden carrier options remain excluded. The picture strip is paged, with long
names wrapping over two lines. Taller displays retain the aircraft diagram
above the equipment pictures; the 128-pixel display gives the picker its own
view. **Back** returns to the aircraft diagram, then to the airfield.

The aircraft editor has a separate **Launch** button at the bottom. It returns
to the map to choose the destination. The editor and launch operation retain
the exact aircraft identity, preventing a different occupant from inheriting an
open editor or launch destination. Pending refits block another change or launch.

There is no landing button, landing list or extra loadout-action step in the
airfield menu. Issue landing on the vehicle-control map by dragging the aircraft
onto the explicit airfield icon. Ground clicks, first route points, later points
and group orders remain ordinary movement. The accepted flight circuit, queue
recovery and serialized landing intent remain unchanged.

Weapon changes are free in this minor release, by the owner's decision. The new
attachment is created with full ammunition; no warehouse, island, carrier or barge
inventory is consumed or refunded. Stock-compatible attachments can be replaced
or cleared. Chassis changes and aircraft creation are outside this operation.
The paid resource model in planning item IAF-003 remains a separate task.

## Data and command contract

The owned UI reads the public `cc2x.airfields()` snapshots. Each field is addressed
by `(tile, index)` and must report exactly four hangar IDs. Loaded positions use
the airfield object's `x/z`; no island-hub fallback is used. Before streaming,
the UI requests placement discovery for team-owned islands through the host-local
`site_request` mailbox. The host spreads `cc2.tiles.sites(tile, 0, 3)` queries over
updates, selecting active environment layers from the stock CPU terrain result.
Only an unambiguous single airfield publishes `site_x/site_z` and advances
`layout_revision`. Its transform matches the origin used by the native airfield
builder. This neither loads a simulation island nor creates aircraft.

The CPU preview is session-cached by the SDK, bounded and exact-executable guarded.
An initial query is synchronous and may hitch; actual latency and alignment must
be checked in game. Refused/ambiguous previews do not invent a location. The UI
retains observed positions until the session or ownership changes. An unloaded
field can accept an explicit landing order, but cannot launch or change loadout.
The mailbox is host-local; distant discovery on remote clients is unverified.

The screen's original forward transform supplies projection and its affine
inverse supplies the launch destination. Stock holomap aspect/camera parameters
and Revolution's holomap helper are handled separately. Source edits only insert
owned draw calls; original order call sites and hub controls are preserved.
The owned helper wraps callbacks and delegates inputs outside its visible map.

Landing uses the existing `barge_load_tile` typed waypoint and the guarded
server receive context. The reviewed generator has one field per island. The
UI refuses ambiguous multi-field landing because the core landing request takes
an island rather than an exact field index. Launch uses a normal move order on
the selected parked aircraft, which the existing host policy interprets as takeoff.

Landing destinations remain in native serialized docking fields after acceptance;
deferred orders retain their explicit typed waypoint. Queue repair and UI
reconstruction are described in [Landing save/reload](LANDING_SAVE_RELOAD.md).
The map/holomap show QUEUE/HOLD/LAND/TAXI, a dashed link to the field and an arrival
count. Hovering an assigned aircraft names its destination. These survive a fresh
screen state through host metadata rather than requiring another click.

Loadout uses the existing host-local mailbox:

- Request owner `island_airfields`, topic `loadout`, subject `vehicle_id`.
- Value: `serial * 2^45 + vehicle_id * 8192 + hardpoint * 128 + definition + 1`.
  Serial is 1..255; hardpoint is 0..63; definition is -1..101. All values fit
  exactly below 2^53. The embedded ID must match the subject.
- Reply topic `loadout_reply`, subject `vehicle_id % 32`, value equal to the
  request on success or its negation on refusal. The bounded ring prevents
  unbounded published state. A missed acknowledgement never retries mutation.
- Topic `equipment`, subject 0: 1 if the optional SDK primitive is available.
  `protocol=2` and `session` identify the UI contract and current generation.

The UI waits for acknowledgement and blocks another change/launch while pending.
It times out with a request to inspect the current loadout. The host rechecks
human ownership, aircraft type, same-team island, exact four-bay membership,
docked/unattached state, empty waypoints and both queues. The SDK then validates
the native hardpoint catalog and replaces the attachment synchronously. The
existing sandbox and public ABI versions are retained. Remote loadout UI is
disabled: the mailbox is not an authenticated multiplayer command channel.

## Fresh game acceptance before publication

Use an isolated ordinary-host test with the exact candidate mod and matching SDK
artifact, at x1 time. Retain a save backup and the existing published installation.

1. Before approaching an owned island, confirm its airfield marker appears at
   the facility and matches the hub's size, edge, team colour and hover style.
   Approach the island and confirm the marker does not jump when it streams.
   Check the control map and holomap at several zoom levels, including Revolution;
   the hub must still open production. Record CPU-preview latency from the log.
2. Place and drag ordinary aircraft/group waypoints across the island. None should
   become a landing order. Click and drag explicit landing actions separately.
3. Confirm exactly four bays, occupied/empty state and selection of a specific bay.
   Inspect 128x128, 256x128 and 256x256 monitors: labels must fit their controls, selection
   must remain clear, and the last hardpoint/weapon page must be reachable.
   Verify nested Back, the fitted option marker and keyboard/gamepad navigation.
4. Replace a weapon, clear it and refit it. Confirm compatibility choices, full
   ammunition, unchanged inventories and correct attachment visuals/collision.
5. Launch that selected aircraft to a chosen destination and confirm the flight
   director completes departure. Other bays must remain occupied as before.
6. Land and service helicopters and fixed-wing aircraft; retain the 2.1.2 oblique
   approach. Check cancellation and capture/session changes.
7. Save/reload and confirm the weapon change survives. Test a second remote client
   separately before making replication claims; remote equipment input stays disabled.
8. Save/reload with three arrivals to one field, including a waiting aircraft and
   a deferred arrival before streaming. Confirm all links, statuses and final bays;
   repeat after clearing/replacing a route to ensure cancellation stays canceled.

Screenshots or direct owner confirmation establish UI acceptance. Offline Lua,
mock-host, syntax checks and static executable inspection establish only their
stated levels of validation.
