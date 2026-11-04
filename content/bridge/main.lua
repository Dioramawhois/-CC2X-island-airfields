-- SPDX-License-Identifier: GPL-3.0-or-later
local cc2 = require('cc2')
local policy
return {
    on_load = function()
        assert(cc2.lua_api_version >= 2, 'Island Airfields requires Lua framework API 2')
        assert(cc2.orders and cc2.orders.available(), 'Verified waypoint receive context is unavailable')
        for _, name in ipairs({'waypoints', 'airfield_tick', 'hangar_choice'}) do
            assert(cc2.events.available(name), 'Island Airfields event unavailable: ' .. name)
        end
        policy = require('bridge.policy').new(cc2, require('bridge.settings'))
        assert(cc2.events.on('waypoints', policy.waypoints))
        assert(cc2.events.on('airfield_tick', policy.airfield))
        assert(cc2.events.on('hangar_choice', policy.hangar))
    end,
    on_update = function() if policy then policy.tick() end end,
    on_unload = function()
        if policy then policy.unload() end
        for _, name in ipairs({'waypoints', 'airfield_tick', 'hangar_choice'}) do cc2.events.off(name) end
    end,
}
