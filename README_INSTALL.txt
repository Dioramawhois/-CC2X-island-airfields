Island Airfields 2.1.0 client beta 1

Requires CC2X SDK 0.8.1 client beta 1 and the reviewed Windows CC2 1.5.18-r2 client.
Subscribe to Island Airfields and the CC2X SDK Workshop item 3833014670.
Close the game and copy the SDK item's content/dinput8.dll next to carrier_command.exe.
Keep a backup of an older identified CC2X runtime; check compatibility before replacing another loader.
Enable Island Airfields in the game's Mods menu, then restart.
For Revolution 1.6-19, subscribe to the original Revolution mod and enable
both mods. This is a separate addon; it contains no Revolution files or complete
game screens and has no resource path overlaps with the reviewed Revolution.
A ModLoader must review the exact new SDK runtime before automatic installation is supported.

Land: give an airborne aircraft one waypoint near an airfield on an owned island.
Launch: give a parked aircraft a new waypoint away from the island.
Cancel a landing: give a new order before touchdown.

The earlier integration prototype passed user-reported ordinary-host checks.
The new separate delivery passed actual-SDK syntax/source checks and owned
synthetic gameplay/UI tests; its fresh gameplay acceptance remains unverified.
Remote-client replication and dedicated-server acceptance are unverified.
The mod contains Lua/content only. The shared SDK is installed separately.
