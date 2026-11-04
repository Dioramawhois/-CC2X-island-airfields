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
    function value:set_waypoint_type_barge_load_tile(index,id) self.orders[index].tile=id end
    return value
end
local v = aircraft(20)
local index = mm_add_waypoint(v,110,0)
assert(index==1 and v.orders[1].tile==10 and v.orders[1].x==100)
assert(g_mm_landing_orders[20]==10)
mm_add_waypoint(v,120,0)
assert(v.orders[2].tile==nil and v.orders[2].x==120, 'appending a route must remain a move')
local far = aircraft(21)
mm_add_waypoint(far,20000,0)
assert(far.orders[1].tile==nil and far.orders[1].x==20000)
local surface = aircraft(22,4)
mm_add_waypoint(surface,110,0)
assert(surface.orders[1].tile==nil, 'ground group member must remain a move')
local cached = aircraft(23)
mm_add_waypoint({extended=true,v=cached,get_waypoint_count=function() return 99 end},110,0)
assert(cached.orders[1].tile==10, 'proxy writes and checks must use the real vehicle')
planes=false
local disabled = aircraft(24)
mm_add_waypoint(disabled,110,0)
assert(disabled.orders[1].tile==nil)
planes=true
active=false
local absent = aircraft(25)
mm_add_waypoint(absent,110,0)
assert(absent.orders[1].tile==nil, 'missing local module must preserve ordinary orders')
multiplayer,host=true,false
local remote = aircraft(26)
mm_add_waypoint(remote,110,0)
assert(remote.orders[1].tile==10, 'remote request must use the replicated typed waypoint')
active,multiplayer,host=true,false,true
local vehicles,images,circles,lines = {},{},{},{}
function update_get_map_vehicle_count() return #vehicles end
function update_get_map_vehicle_by_index(i) return vehicles[i+1] end
function color8(r,g,b,a) return {r=function()return r end,g=function()return g end,b=function()return b end,a=function()return a end} end
function update_get_team_color() return color8(1,2,3,255) end
function get_holomap_from_world(x,y) return x+10000,y+10000 end
function get_screen_from_world(x,y) return x+20000,y+20000 end
function update_ui_image(x,y) images[#images+1]={x,y} end
function update_ui_circle(...) circles[#circles+1]={...} end
function render_dashed_line(...) lines[#lines+1]={...} end
atlas_icons={map_icon_factory_chassis_air=1}
mm_draw_airfields(512,512,true)
assert(#images==1 and images[1][1]==10092, 'only own runway marker, using Revolution holomap transform')
images={}
local parked, queued = aircraft(27), aircraft(28)
parked.state,queued.state=2,5
vehicles={parked,queued}
g_mm_landing_orders[28]=10
mm_draw_airfields(512,512,false)
assert(#images==1 and images[1][1]==20092)
assert(#circles==1 and #lines==1, 'parked and queued statuses must survive the adapter')
print('PASS: own markers, Revolution transforms, normal/append/group/proxy orders, module absence, remote encoding and airfield statuses (synthetic UI)')

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
