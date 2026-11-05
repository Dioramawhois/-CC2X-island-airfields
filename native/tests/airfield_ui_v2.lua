-- SPDX-License-Identifier: GPL-3.0-or-later
-- Owned screen model; never executes a stock or Revolution script.
local function point(x,z)return {x=function()return x end,y=function()return z end}end
e_game_object_type={chassis_air_wing_light=1,chassis_air_wing_heavy=2}
e_vehicle_dock_state={undocked=0,docked=4,dock_queue=5,docking=1}
e_input={pointer_1=1,action_a=2,back=3,up=4,down=5,left=6,right=7}
e_input_action={press=1,release=0}
local team,active,session=1,true,1
local data={{tile=10,index=0,x=900,z=200,runway_start_x=800,runway_start_z=100,
 runway_end_x=1000,runway_end_z=300,hangars={21,22,0,0}}}
local islands={}
islands[10]={get=function()return true end,get_id=function()return 10 end,
 get_team_control=function()return team end,get_difficulty_level=function()return 2 end,
 get_command_center_count=function()return 1 end,get_command_center_position=function()return point(-4000,-5000)end}
function update_get_screen_team_id()return 1 end
function update_get_tile_by_id(id)return islands[id]end
function update_get_tile_count()return 1 end
function update_get_tile_by_index()return islands[10]end
function update_get_is_multiplayer()return false,true end
function get_is_vehicle_air(d)return d==1 or d==2 end
local function vehicle(id,state)
 local v={orders={},state=state,changes=0}
 function v:get()return true end
 function v:get_id()return id end
 function v:get_team()return 1 end
 function v:get_definition_index()return 1 end
 function v:get_dock_state()return self.state end
 function v:get_attached_parent_id()return 0 end
 function v:get_dock_queue_vehicle_id()return 0 end
 function v:get_waypoint_count()return #self.orders end
 function v:clear_waypoints()self.orders={}end
 function v:clear_attack_target()end
 function v:add_waypoint(x,z)self.orders[#self.orders+1]={x=x,z=z};return #self.orders end
 function v:set_waypoint_type_barge_load_tile(i,t)self.orders[i].tile=t end
 function v:get_attachment_count()return 2 end
 function v:get_attachment_type()return 1 end
 return v
end
local aircraft={[21]=vehicle(21,4),[22]=vehicle(22,4),[23]=vehicle(23,0)}
function update_get_map_vehicle_by_id(id)return aircraft[id]end
function update_get_map_vehicle_count()return 3 end
function update_get_map_vehicle_by_index(i)return aircraft[21+i]end
cc2x={airfields=function()return data end,modules={read=function(_,topic)
 if topic=='active'then return active and 1 or 0 end
 if topic=='planes'then return 1 end
 if topic=='session'then return session end
end}}
local v=aircraft[23]
mm_add_waypoint(v,-4000,-5000)
assert(v.orders[1].tile==nil,'normal island move snapped to a landing')
mm_add_waypoint({extended=true,v=v},-3900,-4900)
assert(v.orders[2].tile==nil and v.orders[2].x==-3900,'proxy/group route changed')
local bases=mm_airfields()
assert(#bases==1 and bases[1].x==900 and bases[1].z==200,'marker must use the runway midpoint')
assert(#bases[1].slots==4 and bases[1].slots[1]==21 and bases[1].slots[3]==0)
assert(mm_issue_landing(v,bases[1]))
assert(#v.orders==1 and v.orders[1].tile==10 and v.orders[1].x==900)
assert(mm_launch(10,0,0,1200,1500))
assert(aircraft[21].orders[1].x==1200 and aircraft[21].orders[1].tile==nil)
assert(not mm_launch(10,0,4,1,2),'fifth bay was accepted')
assert(not mm_launch(10,0,2,1,2),'empty bay was launched')
aircraft[22].state=5
assert(not mm_launch(10,0,1,1,2),'queued or landing machine was launched')
team=2
assert(#mm_airfields()==0 and not mm_issue_landing(v,bases[1]),'foreign field accepted')
team=1;active=false
assert(#mm_airfields()==0 and not mm_issue_landing(v,bases[1]),'inactive module accepted')
active=true;data={};session=2
assert(#mm_airfields()==0,'geometry from the previous session survived')
print('PASS precise airfield positions, four slots, explicit landing, pure movement, launch and session/owner guards')

-- A fresh screen can discover an owned unloaded field before any physical visit.
session=3;active=true;data={};local requested=0
cc2x.modules.send=function(owner,topic,id,value)
 assert(owner=='island_airfields' and topic=='site_request' and id==10 and value==0);requested=requested+1;return true
end
cc2x.modules.read=function(_,topic,subject)
 if topic=='active' or topic=='planes' or topic=='tile_sites'then return 1 end
 if topic=='session'then return session end
 if topic=='layout_revision'then return 1 end
 if topic=='site_x'then return 900 end
 if topic=='site_z'then return 200 end
end
local distant=mm_airfields()
assert(#distant==1 and distant[1].x==900 and not distant[1].loaded and #distant[1].slots==4)
assert(requested==1 and not mm_launch(10,0,0,1,2),'unloaded field launched or discovery repeated')
mm_airfields();assert(requested==1)
print('PASS fresh distant-field discovery without a streamed snapshot; no invented hub position or unloaded launch')
