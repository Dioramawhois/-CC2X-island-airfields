-- SPDX-License-Identifier: GPL-3.0-or-later
e_game_object_type={chassis_air_wing_light=1,chassis_air_wing_heavy=2}
e_vehicle_dock_state={undocked=0,docked=4,dock_queue=5,docking=1}
e_input={pointer_1=1,action_a=2,back=3,up=4,down=5,left=6,right=7}
e_input_action={press=1,release=0}
original_calls,drawn,texts,mail,images,text_calls=0,{}, {}, {},{},{}
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
function a:get_repair_factor()return .75 end
function a:get_fuel_factor()return .5 end
function a:get_ammo_factor()return 1 end
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
function update_ui_image(x,y,icon,c)
 assert(icon~=1,'The 8px factory icon must not be used as a facility marker')
 images[#images+1]={x,y,icon,c}
end
function update_ui_circle()end
function update_ui_rectangle(x,y,w,h,c)drawn[#drawn+1]={x,y,w,h,c}end
function update_ui_text(x,y,text,w,_,c)texts[#texts+1]=text;text_calls[#text_calls+1]={x=x,y=y,text=text,w=w,c=c}end
function update_get_attachment_option_count()return 7 end
function update_get_attachment_option(_,i)return 30+i end
function update_get_attachment_option_hidden(definition)return definition==36 end
function get_chassis_data_by_definition_index()return 'Manta',77,'MNT' end
function get_attachment_data_by_definition_index(definition)return {name='Test weapon '..definition,icon16=78}end
atlas_icons={map_icon_factory_chassis_air=1,icon_attachment_16_none=79}
cc2x={airfields=function()return {{tile=10,index=0,x=900,z=200,runway_start_x=800,runway_start_z=100,
 runway_end_x=1000,runway_end_z=300,hangars={21,0,0,0}}}end,modules={
 read=function(_,topic)if topic=='active' or topic=='planes' or topic=='equipment'then return 1 end
 if topic=='session'then return session end;if topic=='loadout_reply'then return reply end end,
 send=function(_,topic,id,payload)mail[#mail+1]={topic,id,payload};return true end}}
function update(w,h)if visible then mm_draw_airfields(w,h,false)end end
function input_event()original_calls=original_calls+1 end
