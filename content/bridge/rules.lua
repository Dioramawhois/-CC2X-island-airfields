-- SPDX-License-Identifier: GPL-3.0-or-later
-- Early read-only VM: enable the engine's human-island controller, retain AI
-- controllers and exclude human garrison spawning. No world writes here.
local cc2 = require('cc2')
assert(cc2.lua_api_version >= 2, 'Island Airfields requires Lua framework API 2')
assert(cc2.orders and cc2.orders.available(), 'Verified waypoint receive context is unavailable')
assert(cc2.events.on('island_controller', function() return true end))
assert(cc2.events.on('island_spawn', function(_, ai_team) return ai_team ~= 0 end))
return {}
