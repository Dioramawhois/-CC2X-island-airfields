# Revolution 1.6 compatibility candidate

Status: local prototype, 2026-11-04. The owner reported the first candidate
working except for a false carrier Landing indicator during island arrivals.
Candidate 2 fixes that indication; its manual recheck, a second remote client
and dedicated servers remain unverified. The published Island Airfields 2.0
release and the installed game are unchanged.

## Findings

The subscribed Revolution **1.6-19**, Workshop `3460002110`, contains 40 content
resources. Published Island Airfields **2.0.0-client-beta.1** contains seven.
Exactly two game paths overlap: `scripts/screen_holomap.lua` and
`scripts/screen_vehicle_control.lua`.

A real Git three-way merge against the installed official Mod SDK produces
three conflicting regions in the holomap and five in vehicle control. Choosing
Airfields for conflicting hunks produces invalid control-screen Lua. Choosing
Revolution produces syntactically valid output but restores its entire holomap,
losing the Airfields markers. Valid syntax is not a compatibility result.

Revolution changes screen callbacks, coordinate transforms, group orders,
patrol tools and vehicle caching. Our host-side airfield policy already runs in
an independent Lua VM and needs no Revolution-specific engine hook.

## Prototype design

Build a separate **local combined package** from the user's subscribed files.
Preserve 37 Revolution content resources byte-for-byte. Apply three small reviewed
Git patches to the other three screens, then append owned Airfields UI helpers.
The patches insert rendering inside Revolution's normal map branches and route
six ordinary/group waypoint call sites through the existing typed landing-order
helper. Revolution's update/input functions, support orders, fog of war,
notification/background gates and other resources remain present.

Use Revolution's own holomap coordinate transform. Render only owned airfields,
plus own parked/queued aircraft on the control screen. Preserve ordinary moves
for ground units, distant destinations and appended routes. Unwrap Revolution's
optional cached vehicle proxy before order writes. The existing host Lua policy
and the stock replicated waypoint channel remain unchanged.

The propulsion-screen patch restricts flight-deck indications to the displayed
carrier. Revolution checked only team and dock state, which also includes
island arrivals. The owned classifier follows the stock screen's distinction:
arrival/queue/taxi require the matching docking parent, whereas launch/holding
require the matching attached parent. Island traffic and another carrier's
aircraft produce no deck state on this carrier. Flight policy is unchanged.

`compatibility/revolution_1_6/profile.json` pins the complete subscribed content
inventory and original manifest. New, missing or changed resources require a
new review; the builder rejects them. Inputs are read only, Lua is syntax-checked
without executing Revolution code, and output must be a new separate directory.
The existing SDK packager records source/output hashes and publishes atomically.

This is an explicit adapter for the reviewed version. A longer-term route is
the generic SDK `screen-extensions-v1` mechanism, already developed separately
for SDK 0.7.1. Published SDK 0.8 does not yet contain that mechanism. Integrating
it would remove whole-screen resources from the ordinary Airfields package, but
would still require reviewed integration with Revolution's order paths. Do not
replace the required 0.8 runtime with 0.7.1 merely to obtain screen extensions.

## Offline verification

- Eight Python packaging/profile/Git-patch regressions pass.
- Owned UI helpers execute in real Lua against synthetic screen objects:
  ownership filtering, both transforms, landing encoding, normal and appended
  moves, group/proxy vehicles, disabled planes, missing module, remote request
  encoding and parked/queued indicators. Candidate 2 additionally covers all
  five flight-deck states, island/other-carrier exclusion and parent-kind
  distinctions. No Revolution Lua is executed.
- All Lua resources in the assembled package pass the real Lua 5.3 syntax checker.
- Reversing each reviewed patch restores the entire original Revolution screen
  after line-ending normalization; the remaining 37 resources are byte-identical.
- All five Airfields bridge files are unchanged. No native binary is included.

The real-Lua fixture is not game, PhysX or network acceptance. Fresh hashes and
reports live under ignored `build/revolution-audit`; the generated package has
its own `BUILD_CHECK.json`.

## Developer reproduction

Use the external verified SDK 0.8 and the legitimate local Revolution subscription:

```text
python compatibility/revolution_1_6/package.py --sdk SDK --revolution REVOLUTION --game GAME --luac LUAC --output NEW-ABSOLUTE-DIRECTORY
python -m unittest discover -s native/tests -p test_*.py
python native/tests/run_revolution_ui_test.py --sdk SDK --zig ZIG
```

Do not publish the combined output: it contains third-party Revolution files;
redistribution terms need review. Source changes contain owned additions,
minimal patch context and hashes rather than complete third-party scripts.

## Owner gameplay checks

Use an isolated test installation with **CC2X SDK 0.8.0-client-beta.1** and a
separate save. Current ModLoader releases pinned to 0.7.1 do not establish the
required 0.8 waypoint-origin support. Do not replace a working managed runtime.
Activate only the combined candidate for this check, disabling separate
Revolution/Airfields copies, and restart.

1. Start **Multiplayer > Host**, Custom, with several starting islands. Open
   captain's holomap and vehicle control: distant owned airfields use team colour;
   enemy infrastructure is not newly revealed. Check zoom, pan and notifications.
2. Verify Revolution radar/fog, marker placement and patrol tools still work.
3. Send an airborne helicopter to an owned airfield; land, service and launch
   with a new waypoint away from the island. Repeat with a plane, including a
   loaded plane because Revolution modifies aircraft resources.
4. Replace the order during approach: landing cancels and the new route is followed.
5. Use Revolution's group selection for two aircraft, then a mixed air/ground
   group. Only eligible aircraft near an airfield receive landing requests;
   ground units retain ordinary moves. Existing routes/support orders remain usable.
6. Verify Revolution's carrier launch controls still work, then save/reload and
   repeat an island launch. Check parked/queued map indicators.
7. With no actual carrier traffic, land and launch an aircraft at an island:
   the carrier flight-deck indicator must stay idle. Then send an aircraft to
   the carrier itself: its queue, Landing and launch indications must still work.
   If another carrier is available, its traffic must not occupy this display.

One-machine host acceptance cannot certify a second remote client. Report the
first failing step with `cc2x.log`; do not describe the adapter as released or
fully compatible before those checks pass.
