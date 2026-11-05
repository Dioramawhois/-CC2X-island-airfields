-- SPDX-License-Identifier: GPL-3.0-or-later
e_game_object_type={chassis_air_wing_light=1,chassis_air_wing_heavy=2}
e_vehicle_dock_state={undocked=0,docked=4,dock_queue=5,docking=1}
e_input={pointer_1=1,action_a=2,back=3,up=4,down=5,left=6,right=7}
e_input_action={press=1,release=0}
original_calls,drawn,texts,mail=0,{}, {}, {}
visible,session,reply=true,1,nil
local a={}
function a:get()return true end
function a:get_id()return 21 end
function a:get_team()return 1 end
function a:get_definition_index()return 1 end
function a:get_dock_state()return 4 end
function a:get_attached_parent_id()return 0 end
function a:get_position_xz()return {x=function()return 900 end,y=function()return 200 end}end
function a:clear_waypoints()self.target=nil end
function a:clear_attack_target()end
function a:add_waypoint(x,z)self.target={x,z};return 0 end
function a:get_waypoint_count()return 0 end
function a:get_attachment_count()return 8 end
function a:get_attachment_type()return 1 end
function a:get_attachment()return {get=function()return false end}end
test_aircraft=a
function update_get_map_vehicle_by_id(id)if id==21 then return a end end
function update_get_map_vehicle_count()return 1 end
function update_get_map_vehicle_by_index()return a end
function update_get_screen_team_id()return 1 end
function update_get_tile_by_id()return {get=function()return true end,get_team_control=function()return 1 end}end
function get_is_vehicle_air()return true end
function update_get_is_multiplayer()return false,true end
function get_screen_from_world(x,z)return 30+x*.1,40+z*.1 end
function color8(...)return {...}end
function update_get_team_color()return color8(0,255,0,255)end
function update_ui_image(x,y)drawn[#drawn+1]={x,y}end
function update_ui_circle()end
function update_ui_rectangle()end
function update_ui_text(x,y,text)texts[#texts+1]=text end
function update_get_attachment_option_count()return 1 end
function update_get_attachment_option()return 30 end
function get_attachment_data_by_definition_index()return {name='Test weapon'}end
atlas_icons={map_icon_factory_chassis_air=1}
cc2x={airfields=function()return {{tile=10,index=0,runway_start_x=800,runway_start_z=100,
 runway_end_x=1000,runway_end_z=300,hangars={21,0,0,0}}}end,modules={
 read=function(_,topic)if topic=='active' or topic=='planes' or topic=='equipment'then return 1 end
 if topic=='session'then return session end;if topic=='loadout_reply'then return reply end end,
 send=function(_,topic,id,payload)mail[#mail+1]={topic,id,payload};return true end}}
function update(w,h)if visible then mm_draw_airfields(w,h,false)end end
function input_event()original_calls=original_calls+1 end
