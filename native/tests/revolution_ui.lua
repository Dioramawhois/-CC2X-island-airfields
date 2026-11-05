-- SPDX-License-Identifier: GPL-3.0-or-later
-- Own synthetic screen fixtures; do not load Revolution or game Lua.
e_game_object_type = {chassis_air_wing_light=1, chassis_air_wing_heavy=2}
e_vehicle_dock_state = {undocked=0, docked=2, dock_queue=5, docking=1}
local active, planes, multiplayer, host = true, true, false, true
function update_get_is_multiplayer() return multiplayer, host end
function update_get_screen_team_id() return 1 end
function get_is_vehicle_air(def) return def == 1 or def == 2 or def == 3 end
cc2x = {modules = {read=function(_, topic)
    if topic == 'active' then return active and 1 or 0 end
    if topic == 'planes' then return planes and 1 or 0 end
    return -1
end}}
local function point(x,y) return {x=function() return x end,y=function() return y end} end
local function island(id,team,level,x)
    return {get=function() return true end,get_id=function() return id end,
        get_team_control=function() return team end,get_difficulty_level=function() return level end,
        get_command_center_count=function() return 0 end,
        get_position_xz=function() return point(x,0) end}
end
local islands = {island(10,1,2,100), island(11,1,1,1000), island(12,2,4,500)}
function update_get_tile_count() return #islands end
function update_get_tile_by_index(i) return islands[i+1] end
function update_get_tile_by_id(id)
    for _,value in ipairs(islands) do if value:get_id()==id then return value end end
end
local function aircraft(id,def)
    local value = {orders={}, state=0}
    function value:get() return true end
    function value:get_id() return id end
    function value:get_team() return 1 end
    function value:get_definition_index() return def or 1 end
    function value:get_dock_state() return self.state end
    function value:get_waypoint_count() return #self.orders end
    function value:get_attached_parent_id() return 0 end
    function value:get_dock_queue_vehicle_id() return 0 end
    function value:get_position_xz() return point(50,0) end
    function value:add_waypoint(x,y)
        self.orders[#self.orders+1] = {x=x,y=y}
        return #self.orders
    end
    function value:clear_waypoints_from(index)
        -- Owned model of the reviewed drag path: remove the selected point
        -- and its successors before adding the point at its new position.
        for i = #self.orders, index, -1 do self.orders[i] = nil end
    end
    function value:set_waypoint_type_barge_load_tile(index,id) self.orders[index].tile=id end
    return value
end
print('Movement, field geometry and input are checked by airfield_ui_v2.lua')

-- Island or another carrier's traffic must not occupy this carrier's display.
g_flight_deck_states = {none=0, taxi=1, queued=2, landing=3, launch=4, holding=5}
e_vehicle_dock_state.undocking = 3
e_vehicle_dock_state.docking_taxi = 4
e_vehicle_dock_state.undock_holding = 6
local carrier = {get_id=function() return 100 end}
local target = {state=0, dock_parent=0, attached_parent=0}
function target:get_dock_state() return self.state end
function target:get_dock_parent_id() return self.dock_parent end
function target:get_attached_parent_id() return self.attached_parent end
for _,case in ipairs({{'undocking','launch',true}, {'undock_holding','holding',true},
                     {'docking','landing',false}, {'docking_taxi','taxi',false},
                     {'dock_queue','queued',false}}) do
    target.state = e_vehicle_dock_state[case[1]]
    target.dock_parent, target.attached_parent = 0, 0
    assert(mm_carrier_flight_deck_state(carrier,target)==0, 'island traffic leaked: '..case[1])
    target.dock_parent, target.attached_parent = 200, 200
    assert(mm_carrier_flight_deck_state(carrier,target)==0, 'another carrier leaked: '..case[1])
    target.dock_parent, target.attached_parent = 100, 100
    assert(mm_carrier_flight_deck_state(carrier,target)==g_flight_deck_states[case[2]])
    -- An arrival target cannot identify a launch parent, or vice versa.
    target.dock_parent = case[3] and 100 or 0
    target.attached_parent = case[3] and 0 or 100
    assert(mm_carrier_flight_deck_state(carrier,target)==0, 'wrong parent kind: '..case[1])
end
target.state, target.dock_parent, target.attached_parent = e_vehicle_dock_state.undocked,100,100
assert(mm_carrier_flight_deck_state(carrier,target)==0)
assert(mm_carrier_flight_deck_state({get_id=function()return 0 end},target)==0)
print('PASS: carrier-specific landing/queue/taxi/launch/holding; island and other-carrier exclusion (synthetic UI)')
