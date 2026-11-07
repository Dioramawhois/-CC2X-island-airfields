# Island Airfields 2.2.2 client beta 1

Drag the final route point onto an airfield to land after the preceding movement
and attack orders. The landing waypoint and route line now attach to the actual
airfield icon instead of the island centre. The complete silhouette and its black
edge accept drops, including the nose.

🛬 **Landing and route handling**

- Preserve earlier waypoints and their attack orders. Landing begins when its
  explicit waypoint reaches the head of the route; future arrivals show ROUTE.
- Restore future landing orders after save/reload.
- Keep aircraft-body dragging as a new landing order that replaces the old route.
- Resolve the field under the pointer at release using current marker geometry.
- Preserve native map updates and dragging if parked-aircraft inspection fails.

✈️ **Parked aircraft**

Small dark-grey model silhouettes show occupied bays in a fixed 2x2 grid beside
the airfield. Empty bays keep their positions; hovering names the model. Queue
reservations, taxiing/departing aircraft and stale unloaded data do not appear
as parked aircraft. All four bays retain graphical loadout and launch controls.

**No new SDK DLL is required.** Keep an existing compatible SDK. Aircraft refitting
still requires the equipment API introduced in SDK 0.9.0-dev; SDK 0.8.1 cannot refit
aircraft. For a first installation, subscribe to [CC2X SDK](https://steamcommunity.com/sharedfiles/filedetails/?id=3833014670)
and use [CC2 ModLoader](https://github.com/Dioramawhois/CC2-ModLoader/releases/tag/latest-beta),
or install the SDK DLL manually with the game closed.

On 2026-11-08, the owner accepted this candidate in an ordinary combined
CC2 ModLoader run with eight enabled mods and the existing SDK 0.11 runtime,
then authorized release. Owned Lua, SDK and screen-delivery checks provide
separate offline regression evidence. Remote clients and dedicated servers
remain unverified. Never-streamed airfield markers may still require an approach;
native far-distance approach behavior is not universally fixed.

Separate addon for stock CC2 and original Revolution 1.6-19/1.6-20. No Revolution
files, complete game screens or private gameplay DLLs are included.

[Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3826940003)
· [Release, source and checksums](https://github.com/Dioramawhois/-CC2X-island-airfields/releases/tag/v2.2.2-client-beta.1)
