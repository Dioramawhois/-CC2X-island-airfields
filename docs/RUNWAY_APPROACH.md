# Runway entry approach: 2.1.2 client beta 1

The optional Island Airfields Lua policy guides a fixed-wing aircraft through
its landing circuit after the native landing queue hands it over. An oblique
arrival previously aimed at a point beside the runway before flying outward
again to join downwind, base and final. This could look like a preliminary trip
to the island center.

## Changed target

The inbound phase now aims at the outer approach gate. It uses the selected
runway's actual world-space start/end points, not the map marker or island
origin. In the runway coordinate frame, positive distance is outward before the
landing threshold. The gate is 600 metres beyond the calculated final-approach
entry and two nominal turning radii to the selected side, approximately
1,071 metres at 75 m/s and the existing 0.14 rad/s turn limit.

An aircraft already inside the existing alignment cone can still enter final
directly. The existing downwind/base transitions, runway lock and holding
pattern remain in place. The target change belongs to this independent Lua mod;
it adds no gameplay defaults or API changes to CC2X SDK.

## Separate phases that remain unchanged

- The map marker identifies the owned island. Its command-center or island-center
  coordinate is not a claimed runway position. For a loaded island, an accepted
  landing request clears that selector waypoint before the flight policy takes
  control.
- Assistance still begins within the existing six-kilometre distance from the
  airfield origin. Before takeover, the aircraft remains in the native queue.
  This change does not remove that bound or take physics control at arbitrary
  distances.
- If the island has not streamed, the existing deferred request temporarily
  retains an ordinary move toward its map anchor. The mod cannot derive a real
  runway entry from unavailable geometry. Replacement orders still cancel that
  deferred request.
- Helicopter approaches, takeoff, runway reservations and ordinary-order
  cancellation retain their existing policy.

## Regression evidence and limits

The owned flat-world fixture runs the real Lua policy through the actual SDK
Host. Before the change, an accepted aircraft at `(3000, 300, 3000)` beside a
runway from `(75, 0, -140)` to `(-150, 0, -140)` received its first assisted
target at `(-225, 931.429)` in the horizontal plane. The new target must be
outside the landing threshold on the approach side, rather than near the
island origin.

All 12 full-flight cases reach the parked state: three oblique arrivals covering
both lateral sides, translated/rotated geometry and terrain-selected reverse
direction; seven near-gate cases covering landward, away and perpendicular
headings; and two aligned arrivals in normal/reversed directions. The oblique
first target is 2,850.53 metres outward before the threshold and 1,071.43 metres
to the selected side. All cases park within the 24,000-tick fixture limit; the
longest observed case takes 2,569 ticks. Aligned arrivals retain a centerline
final target instead of detouring to the lateral gate.

The original complete helicopter, shared-runway, plane landing/launch,
cancellation, deferred-order, reused-session and ownership scenarios also pass.
A separate far-queue case remains under native control and cancellable. The
18 Python profile/packaging checks pass. These are synthetic integration checks:
they do not model the game's complete terrain, aerodynamics, collision or
network behavior.

The offline tests do not change a game process, Workshop package or installed
mod. Separately, the owner tested 2.1.2 in a fresh ordinary-host session and
confirmed that the oblique arrival now joins the approach before the runway.
That confirms the reported route correction. It does not establish every
terrain/physics case, complete touchdown/service/relaunch acceptance for this
exact release, remote-client multiplayer or dedicated-server support.

Further acceptance should cover both sides, a runway approached in each
direction, an already aligned aircraft, runway contention and cancellation.
Observe the aircraft trajectory as well as the map marker; the marker remains
an island selector. Native queued guidance beyond six kilometres and deferred
orders for unstreamed islands retain their existing behavior.
