-- SPDX-License-Identifier: GPL-3.0-or-later
-- Owned UI model: no stock or Revolution script is executed.
e_vehicle_dock_state={undocked=0,docking=1,docking_taxi=2,docked=4,dock_queue=5,undocking=6}
function get_is_vehicle_air(def)return def==1 or def==2 end
local function vehicle(def,state,parent)
 local v={state=state,parent=parent,valid=true}
 function v:get()return self.valid end
 function v:get_definition_index()return def end
 function v:get_dock_state()return self.state end
 function v:get_attached_parent_id()return self.parent end
 return v
end
local plane=vehicle(1,4,0)
assert(mm_is_parked_at_airfield(plane),'parked island aircraft was treated as airborne')
assert(mm_is_parked_at_airfield({extended=true,v=plane}),'Revolution wrapper bypassed parked check')
for _,state in ipairs({0,1,2,5,6})do
 plane.state=state
 assert(not mm_is_parked_at_airfield(plane),'arrival/taxi/launch was hidden: '..state)
end
plane.state=4;plane.parent=100
assert(not mm_is_parked_at_airfield(plane),'carrier aircraft was treated as island parking')
plane.parent=0;plane.valid=false
assert(not mm_is_parked_at_airfield(plane) and not mm_is_parked_at_airfield(nil))
assert(not mm_is_parked_at_airfield(vehicle(3,4,0)),'ground/naval vehicle was hidden')
plane.valid=true
function update_get_map_vehicle_by_id(id)if id==21 then return plane end end
local function selection(id)
 return {vehicle_id=id,clears=0,clear=function(self)self.vehicle_id=0;self.clears=self.clears+1 end}
end
g_selection,g_highlighted,g_drag=selection(21),selection(21),selection(21)
update(256,256,1)
assert(g_selection.clears==1 and g_highlighted.clears==1 and g_drag.clears==1,
 'hidden parked aircraft retained map selection/highlight/drag')
plane.state=0;g_selection=selection(21)
update(256,256,1)
assert(g_selection.clears==0,'active aircraft selection was cleared')
print('PASS island parking, Revolution wrapper, landing/launch visibility and stale map selection')
