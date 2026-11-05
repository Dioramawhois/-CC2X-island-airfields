# Island Airfields 2.2: graphical airfield loadouts

Open an airfield, click its parked aircraft and edit equipment on the same
graphical chassis/hardpoint widget used by the carrier. The separate launch
button stays at the bottom. Landing is issued on the map.

## How to use it

1. Click the airfield icon to see its four numbered hangar bays.
2. Click a parked aircraft: its loadout opens directly.
3. Click a hardpoint on the aircraft picture and choose compatible equipment
   by its icon. Read the focused option's name; fitted equipment is marked.
4. Use **Back** to return to the aircraft. Press **Launch**, then choose a map
   destination. A different occupant of that bay cannot inherit the command.
5. To land, drag an airborne aircraft onto the explicit airfield icon on the
   vehicle-control map. Ground waypoints remain movement orders; the airfield
   menu has no landing button or landing list.

Refitting is currently **free**. Attachments can be replaced or cleared; newly
fitted attachments start full. No island, warehouse, carrier or barge stock is
consumed or refunded. Resource accounting is a separate future task. Empty/busy
bays cannot create or launch aircraft. Pending refits block another change or
launch. Small monitors have paged picture choices and readable option names.

## Queues and save/reload

Assigned aircraft have dashed links and QUEUE/HOLD/LAND/TAXI labels. Arrival
counts appear on the map and in the airfield window; hover an aircraft to see
its destination. Landing targets remain in native saved data. The host restores
arrivals after loading and repairs missing queue entries. Older active arrivals
can recover from an existing saved hangar reservation. Explicit movement or
route cancellation clears the task. Lost targets are not guessed by proximity.

The owner explicitly confirmed three-arrival save/reload and equipment changes,
then tested the graphical revision and requested publication. Real-Lua UI/input,
flight/queue and packaging regressions pass separately; these offline fixtures
do not establish remote-client or dedicated-server support.

## Required SDK update

Install the matching **[CC2X SDK 0.9.0-dev preview](https://github.com/Dioramawhois/Carrier-Command-2-extender/releases/tag/0.9.0-dev)**.
The previous 0.8.1 runtime cannot replace equipment. With the game closed, update
the manually installed `dinput8.dll` beside `carrier_command.exe`, keeping a
backup of an identified older loader. Workshop downloads do not replace that
file automatically. ModLoader automatic installation requires a release that
explicitly supports this exact binary.

Subscribe to [Island Airfields](https://steamcommunity.com/sharedfiles/filedetails/?id=3826940003)
and [CC2X SDK](https://steamcommunity.com/sharedfiles/filedetails/?id=3833014670).
With Revolution, enable the original Revolution 1.6-19/1.6-20 and this addon
together. No Revolution files, complete game screens or private gameplay DLL
are bundled. [Installation guide](https://github.com/Dioramawhois/-CC2X-island-airfields/blob/v2.2.0-client-beta.5/workshop/installation/README.md).

## Known limits

- A never-streamed island may still need an approach before its exact airfield
  marker is available: the experimental distant-position query currently refuses
  unavailable tile definitions. Not every native far-distance approach is fixed.
- Reviewed Windows CC2 1.5.18-r2 and stock/Revolution screen profiles only;
  unknown screen versions are skipped.
- Remote clients, dedicated servers and broader terrain/physics coverage remain
  unverified. Remote equipment input is disabled.
