-- SPDX-License-Identifier: GPL-3.0-or-later
local width,height=256,256
local function frame()
 texts,text_calls,drawn,images={},{},{},{};update(width,height,1)
end
local function click(x,y)
 g_pointer_pos_x,g_pointer_pos_y=x,y;frame()
 input_event(e_input.pointer_1,1);input_event(e_input.pointer_1,0);frame()
end
local function has(s)for _,t in ipairs(texts)do if t:find(s,1,true)then return true end end end
local function click_label(label)
 for _,t in ipairs(text_calls)do if t.text==label then click(t.x+1,t.y+1);return end end
 error('Missing visible control on '..width..'x'..height..': '..label)
end
local function capture(label)
 local folder=os.getenv('IAF_UI_CAPTURE_DIR');if not folder then return end
 local file=assert(io.open(folder..'/'..label..'.tsv','w'))
 file:write('size\t'..width..'\t'..height..'\n')
 for _,r in ipairs(drawn)do
  file:write(('rect\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n'):format(r[1],r[2],r[3],r[4],r[5][1],r[5][2],r[5][3]))
 end
 for _,r in ipairs(images)do file:write(('icon\t%d\t%d\t%d\n'):format(r[1],r[2],r[3]))end
 for _,t in ipairs(text_calls)do
  file:write(('text\t%d\t%d\t%d\t%d\t%d\t%s\n'):format(t.x,t.y,t.c[1],t.c[2],t.c[3],t.text))
 end
 file:close()
end
local function within_screen()
 local buttons={}
 for _,t in ipairs(text_calls)do
  assert(t.x>=0 and t.y>=0 and t.x+t.w<=width and t.y+8<=height,'label escaped the screen: '..t.text)
  assert(utf8.len(t.text)*6<=t.w,'label exceeded its own text width: '..t.text)
 end
 for _,r in ipairs(drawn)do
  assert(r[1]>=0 and r[2]>=0 and r[1]+r[3]<=width and r[2]+r[4]<=height,'panel/control escaped the screen')
  if (r[5][1]==24 and r[5][2]==34)or(r[5][1]==55 and r[5][2]==78)then buttons[#buttons+1]=r end
 end
 for i,a in ipairs(buttons)do for j=i+1,#buttons do local b=buttons[j]
  assert(a[1]+a[3]<=b[1] or b[1]+b[3]<=a[1] or a[2]+a[4]<=b[2] or b[2]+b[4]<=a[2],
      'interactive controls overlap on '..width..'x'..height..': '..a[1]..','..a[2]..' / '..b[1]..','..b[2])
 end end
end
local function click_point(slot)
 for _,point in ipairs(hardpoint_hitboxes)do if point.slot==slot then click(point.x+2,point.y+2);return end end
 error('No clickable carrier hardpoint '..slot)
end
local function click_icon(icon)
 for _,item in ipairs(images)do if item[3]==icon then click(item[1]+5,item[2]+5);return end end
 error('No equipment image '..icon)
end
local function is_diagram()
 return has('Current:') and not has('Land aircraft') and diagram_calls>0
end
g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
assert(parking_circles==0,'parked aircraft retained an active-map circle')
input_event(e_input.pointer_1,1);input_event(e_input.pointer_1,0);frame()
assert(has('SELECT AIRCRAFT') and has('Manta #21') and not has('Launch') and not has('Land aircraft') and original_calls==0,
 'airfield should open only four aircraft bays')
within_screen();capture('hangar-256x256')
local before_diagrams=diagram_calls
click_label('EMPTY');assert(diagram_calls==before_diagrams,'empty bay opened another aircraft')
click_label('Manta #21');assert(is_diagram(),'one aircraft click must directly open the carrier diagram')
within_screen();capture('aircraft-256x256')
click_point(8);assert(has('EQUIPMENT') and has('FITTED'),'image hardpoint did not open equipment pictures')
within_screen();capture('equipment-256x256')
click_icon(78)
assert(#mail==1 and mail[1][1]=='loadout' and mail[1][2]==21 and math.floor(mail[1][3]%8192/128)==8,
 'graphical hardpoint did not address its actual attachment index')
assert(not mm_set_loadout(test_aircraft,0,30),'duplicate pending mutation allowed')
local old_target=test_aircraft.target
click_label('Launch aircraft');assert(test_aircraft.target==old_target and has('Changing loadout'),'pending refit did not block launch')
reply=mail[1][3];frame();assert(mm_set_loadout(test_aircraft,0,30))
reply=-mail[2][3];frame()
input_event(e_input.back,1);frame();assert(is_diagram(),'weapon Back must return to aircraft diagram')
input_event(e_input.back,1);frame();assert(has('SELECT AIRCRAFT'),'aircraft Back must return to airfield')
click_label('Manta #21');click_label('Launch aircraft');click(140,160)
assert(test_aircraft.target and math.abs(test_aircraft.target[1]-1100)<.001 and math.abs(test_aircraft.target[2]-1200)<.001,
 'launch did not invert original map transform')
-- Every supported monitor preserves the requested direct flow and image choices.
for _,size in ipairs({{128,128},{128,256},{192,128},{256,128},{256,192},{512,256}})do
 width,height=size[1],size[2];assert(mm_open_airfield(10,0));frame();within_screen()
 assert(not has('Land aircraft') and not has('Loadout (free)'),'removed menu actions came back')
 if height==128 then capture('hangar-'..width..'x128')end
 click_label(width<220 and 'MNT' or 'Manta #21');assert(is_diagram());within_screen()
 if height==128 then capture('aircraft-'..width..'x128')end
 assert(#hardpoint_hitboxes==8,'carrier layout lost an editable hardpoint')
 click_point(8);within_screen()
 if height==128 then capture('equipment-'..width..'x128')end
 local old=get_attachment_data_by_definition_index
 function get_attachment_data_by_definition_index()return {name='Extended range infrared guided anti air missile',icon16=78}end
 frame();within_screen();get_attachment_data_by_definition_index=old
 if width==128 and height==128 then
  click_label('>');within_screen()
  local choices=0;for _,item in ipairs(images)do if item[3]==78 or item[3]==79 then choices=choices+1 end end
  assert(has('2/2') and choices==3,'last image page should contain three choices, excluding the hidden definition')
  click_label('<')
 end
 input_event(e_input.back,1);frame();assert(is_diagram())
end
width,height=256,256;assert(mm_open_airfield(10,0));g_is_mouse_mode=false;frame()
input_event(e_input.action_a,1);frame();assert(is_diagram(),'gamepad aircraft selection added an extra menu step')
input_event(e_input.action_a,1);frame();assert(has('EQUIPMENT'),'gamepad cannot select a point on the native diagram')
input_event(e_input.back,1);frame();input_event(e_input.back,1);frame()
-- Same bay occupied by a different vehicle must not retarget an open editor.
g_is_mouse_mode=true
click_label('Manta #21');local old_id=test_aircraft.get_id
function test_aircraft:get_id()return 22 end
frame();assert(not is_diagram() and has('Aircraft left'),'editor silently switched to another aircraft')
test_aircraft.get_id=old_id
assert(not mm_launch(10,0,0,140,160,22),'launch destination silently retargeted a replacement aircraft')
assert(mm_open_airfield(10,0));frame();click_label('Manta #21')
local native_diagram=imgui_vehicle_chassis_loadout;imgui_vehicle_chassis_loadout=nil
frame();assert(has('diagram unavailable'),'missing carrier widget must fail visibly')
imgui_vehicle_chassis_loadout=native_diagram
visible=false;frame();local before=original_calls;input_event(e_input.action_a,1)
assert(original_calls==before+1,'invisible map intercepted an original input')
-- Fresh UI state must reconstruct every saved arrival from host metadata,
-- without relying on the screen-local table populated by an earlier click.
local old_read,old_count,old_index=cc2x.modules.read,update_get_map_vehicle_count,update_get_map_vehicle_by_index
local queued={};local stage=2;local target=10;local links={}
for i=1,3 do
 local aircraft=setmetatable({},{__index=test_aircraft})
 function aircraft:get_id()return 20+i end
 function aircraft:get_dock_state()return e_vehicle_dock_state.dock_queue end
 function aircraft:get_position_xz()return {x=function()return 1000+i*100 end,y=function()return 400+i*100 end}end
 queued[i]=aircraft
end
function update_get_map_vehicle_count()return #queued end
function update_get_map_vehicle_by_index(i)return queued[i+1]end
cc2x.modules.read=function(owner,topic,subject)
 if topic=='landing'then return target end;if topic=='landing_stage'then return stage end
 return old_read(owner,topic,subject)
end
function render_dashed_line(x,y,tx,ty)links[#links+1]={x,y,tx,ty}end
visible=true;g_mm_landing_orders={};g_pointer_pos_x,g_pointer_pos_y=140,90;frame()
assert(#links==3 and links[1][3]==120 and links[1][4]==60 and has('QUEUE') and has('3'),
 'saved queued aircraft lost their explicit airfield link/count')
stage=6;links={};frame();assert(#links==0 and has('ROUTE'),'future landing looked like an immediate approach')
stage=3;links={};frame();assert(#links==3 and has('HOLD'))
stage=4;links={};frame();assert(#links==3 and has('LAND'))
local segment_count=0
render_dashed_line=nil
function update_ui_line(x,y,tx,ty)
 assert(x>=0 and x<256 and y>=0 and y<256 and tx>=0 and tx<256 and ty>=0 and ty<256)
 segment_count=segment_count+1
end
mm_draw_airfields(256,256,true)
assert(segment_count>0 and segment_count<512*3,'holomap needs bounded own dashed links without the control helper')
target=0;links={};frame();assert(#links==0 and not has('QUEUE'),'canceled order kept a stale map link')
cc2x.modules.read,update_get_map_vehicle_count,update_get_map_vehicle_by_index=old_read,old_count,old_index
-- Stock holomap has no Revolution helper; its own camera/aspect function is used.
get_holomap_from_world=nil;drawn={};mm_draw_airfields(256,256,true)
assert(drawn[1][1]==118 and drawn[1][2]==49 and #drawn>=6)
-- A terminal waypoint drag carries its native ID into the owned landing action.
local old_by_id=update_get_map_vehicle_by_id
local routed=setmetatable({orders={{x=-800,z=10},{x=-400,z=50},{x=20,z=80}}},{__index=test_aircraft})
function routed:get_id()return 23 end
function routed:get_dock_state()return e_vehicle_dock_state.undocked end
function routed:get_waypoint_count()return #self.orders end
function routed:get_waypoint_by_id(id)return {get=function()return self.orders[id]~=nil end}end
function routed:clear_waypoints_from(id)for i=#self.orders,id,-1 do table.remove(self.orders,i)end end
function routed:add_waypoint(x,z)self.orders[#self.orders+1]={x=x,z=z};return #self.orders end
function routed:set_waypoint_type_barge_load_tile(id,tile)self.orders[id].tile=tile end
function routed:clear_waypoints()error('terminal landing cleared the entire route')end
function routed:clear_attack_target()error('terminal landing cleared attack orders')end
function update_get_map_vehicle_by_id(id)if id==23 then return routed end;return old_by_id(id)end
g_drag={vehicle_id=23,waypoint_id=3,clear=function(self)self.vehicle_id=0;self.waypoint_id=0;self.cleared=true end}
g_pointer_pos_x,g_pointer_pos_y=120,60;frame();input_event(e_input.pointer_1,e_input_action.release)
assert(#routed.orders==3 and routed.orders[1].x==-800 and routed.orders[2].x==-400 and routed.orders[3].tile==10 and g_drag.cleared,
 'map drop failed to preserve the prefix of the dragged terminal landing')
do
 local snapshots=cc2x.airfields
 local function site(tile,x,z)return {tile=tile,index=0,x=x,z=z,runway_start_x=x-100,runway_start_z=z-100,
  runway_end_x=x+100,runway_end_z=z+100,hangars={0,0,0,0}}end
 cc2x.airfields=function()return {site(10,900,200),site(11,1700,400)}end
 local route_clears,attack_clears=0,0
 function routed:clear_waypoints()route_clears=route_clears+1;self.orders={}end
 function routed:clear_attack_target()attack_clears=attack_clears+1 end
 local function drag(wid)g_drag.vehicle_id=23;g_drag.waypoint_id=wid;g_drag.cleared=false end
 -- Aircraft-body drag keeps the accepted replacement behaviour.
 drag(0);g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
 input_event(e_input.pointer_1,e_input_action.release)
 assert(#routed.orders==1 and routed.orders[1].tile==10 and routed.orders[1].x==900 and
  routed.orders[1].z==200 and route_clears==1 and attack_clears==1,
  'aircraft-body landing must replace its old route at the selected airfield')
 -- Pointer moves between frames: choose the field at release, not last draw.
 drag(0);g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
 g_pointer_pos_x,g_pointer_pos_y=200,80
 input_event(e_input.pointer_1,e_input_action.release)
 assert(#routed.orders==1 and routed.orders[1].tile==11 and routed.orders[1].x==1700 and
  routed.orders[1].z==400,'landing used the previous frame instead of the released pointer')
 -- Dropping away from every field must fall through to the native handler.
 drag(0);g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
 g_pointer_pos_x,g_pointer_pos_y=20,200
 local events,previous=original_calls,routed.orders[1]
 input_event(e_input.pointer_1,e_input_action.press)
 input_event(e_input.pointer_1,e_input_action.release)
 assert(original_calls==events+2 and routed.orders[1]==previous and not g_drag.cleared,
  'stale airfield hover consumed an ordinary native move')
 -- A fresh hit is accepted even when no field was highlighted at draw time.
 drag(0);g_pointer_pos_x,g_pointer_pos_y=20,200;frame()
 g_pointer_pos_x,g_pointer_pos_y=200,80
 input_event(e_input.pointer_1,e_input_action.release)
 assert(g_drag.cleared and routed.orders[1].tile==11,'fresh release hit was lost')
 -- Queued landing uses the same target selection and preserves exact prefix.
 routed.orders={{x=-800,z=10,attacks={target=77}},{x=-400,z=50},{x=20,z=80}}
 local first,second=routed.orders[1],routed.orders[2]
 local clears=route_clears
 drag(3);g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
 g_pointer_pos_x,g_pointer_pos_y=200,80
 input_event(e_input.pointer_1,e_input_action.release)
 assert(#routed.orders==3 and routed.orders[1]==first and routed.orders[2]==second and
  first.attacks.target==77 and routed.orders[3].tile==11 and routed.orders[3].x==1700 and
  routed.orders[3].z==400 and route_clears==clears,'queued landing moved/erased its earlier route or target')
 -- The nose and black edge are visible above the old seven-pixel hit circle.
 -- A two-point route must attach to the field when dropped anywhere on its glyph.
 for _,offset in ipairs({{0,-9},{-1,-11},{-8,-5},{7,-5},{4,2}})do
  routed.orders={{x=-800,z=10,attacks={target=77}},{x=20,z=80}}
  local prefix=routed.orders[1];local events=original_calls
  drag(2);g_pointer_pos_x,g_pointer_pos_y=120+offset[1],60+offset[2];frame()
  local highlighted=false
  for _,rect in ipairs(drawn)do
   if rect[1]==119 and rect[2]==50 and rect[3]==2 and rect[4]==12 and
      rect[5][1]==255 and rect[5][2]==255 and rect[5][3]==255 then highlighted=true end
  end
  assert(highlighted,'visible airfield glyph did not highlight as a landing target')
  input_event(e_input.pointer_1,e_input_action.release)
  assert(#routed.orders==2 and routed.orders[1]==prefix and prefix.attacks.target==77 and
   routed.orders[2].tile==10 and routed.orders[2].x==900 and routed.orders[2].z==200 and
   g_drag.cleared and original_calls==events,
   'drop on visible airfield glyph became an ordinary moved waypoint')
 end
 assert(mm_issue_landing(routed,{tile=11,index=0,x=-99999,z=88888}))
 assert(#routed.orders==1 and routed.orders[1].x==1700 and routed.orders[1].z==400,
  'stale field geometry replaced the current destination')
 cc2x.airfields=snapshots
end
update_get_map_vehicle_by_id=old_by_id
-- Parked model silhouettes are independent of incoming hangar reservations.
local old_airfields,old_chassis,old_project=cc2x.airfields,get_chassis_data_by_definition_index,get_screen_from_world
local parked_heavy=setmetatable({},{__index=test_aircraft})
function parked_heavy:get_id()return 23 end
function parked_heavy:get_definition_index()return 2 end
function parked_heavy:get_dock_state()return self.state or e_vehicle_dock_state.docked end
local reserved=setmetatable({},{__index=parked_heavy})
function reserved:get_id()return 24 end
function reserved:get_dock_state()return e_vehicle_dock_state.dock_queue end
local taxi=setmetatable({},{__index=reserved})
function taxi:get_id()return 25 end
function taxi:get_dock_state()return e_vehicle_dock_state.docking end
function update_get_map_vehicle_by_id(id)
 if id==23 then return parked_heavy elseif id==24 then return reserved elseif id==25 then return taxi end
 return old_by_id(id)
end
local hangars={21,23,24,25}
cc2x.airfields=function()return {{tile=10,index=0,x=900,z=200,runway_start_x=800,runway_start_z=100,
 runway_end_x=1000,runway_end_z=300,hangars=hangars}}end
local occupied_airfields=cc2x.airfields
get_chassis_data_by_definition_index=function(def)return def==1 and 'Albatross'or'Manta',176+def end
local function silhouettes(w,h)
 images,texts,drawn={},{},{};mm_draw_airfields(w,h,false)
 local models=mm_parked_aircraft(mm_airfields()[1]);local count=0
 for i=1,4 do if models[i]then count=count+1 end end
 local box={left=math.huge,top=math.huge,right=-math.huge,bottom=-math.huge}
 for _,item in ipairs(drawn)do if item[5][1]==72 and item[5][2]==72 and item[5][3]==72 then
  box.left=math.min(box.left,item[1]);box.top=math.min(box.top,item[2])
  box.right=math.max(box.right,item[1]+item[3]);box.bottom=math.max(box.bottom,item[2]+item[4])
 end end
 return count,models,box
end
local n,models,box=silhouettes(256,256)
assert(n==2 and models[1].definition==1 and models[2].definition==2 and #texts==0 and #images==0,
 'parked readout must use small dark owned glyphs without full-size atlas images or counts')
assert(box.right-box.left==14 and box.bottom-box.top==6 and box.right<112,
 'two occupied top bays did not form a small row to the left')
g_pointer_pos_x,g_pointer_pos_y=box.left+2,box.top+2;frame()
assert(has('Parked: Albatross'),'parked model tooltip is missing')
parked_heavy.state=e_vehicle_dock_state.undocked
assert(silhouettes(256,256)==1,'departing aircraft retained a parked silhouette')
hangars={21,21,0,0};assert(silhouettes(256,256)==1,'duplicate hangar ID doubled the parked count')
cc2x.airfields=function()return {}end;assert(silhouettes(256,256)==0,'stale unloaded hangars appeared occupied')
cc2x.airfields=occupied_airfields;hangars={21,23,24,25};parked_heavy.state=e_vehicle_dock_state.docked
reserved.get_dock_state=function()return e_vehicle_dock_state.docked end
taxi.get_dock_state=reserved.get_dock_state
get_screen_from_world=function()return 6,6 end
n,models,box=silhouettes(128,128)
assert(n==4 and box.right-box.left==14 and box.bottom-box.top==14,'full hangar is not a 14x14 grid')
for _,item in ipairs(drawn)do if item[5][1]==72 or (item[5][1]==0 and item[5][4]==180)then
 assert(item[1]>=0 and item[2]>=0 and item[1]+item[3]<=128 and item[2]+item[4]<=128,
  'parked glyph escaped a small screen')
end end
local full_left=box.left
hangars={0,23,0,25};n,models,box=silhouettes(128,128)
assert(n==2 and models[1]==nil and models[2].id==23 and models[3]==nil and models[4].id==25 and
 box.left==full_left+8 and box.right-box.left==6 and box.bottom-box.top==14,
 'empty left bays were compressed instead of preserving their 2x2 positions')
cc2x.airfields=old_airfields;get_chassis_data_by_definition_index=old_chassis
get_screen_from_world=old_project;update_get_map_vehicle_by_id=old_by_id
print('PASS four-bay direct aircraft flow, native carrier widget adapter and real hardpoint indices, picture choices, bottom launch, no landing menu, pending/bay identity guards, mouse/gamepad, monitor bounds, saved queue links and holomap fallback')
print('PASS terminal landing drag preserves route; grey parked-model icons, reservations, departure, duplicates and unloaded data')
print('PASS original aircraft-body landing replacement, queued prefix/attacks, actual release target and native clicks away from fields')
do
 local native=test_aircraft
 local state=e_vehicle_dock_state.undocked
 local dock= native.get_dock_state
 native.get_dock_state=function()return state end
 local strict=setmetatable({},{__index=function(_,key)
  local member=rawget(native,key)
  if type(member)=='function'then return function(_,...)return member(native,...)end end
  error('MapVehicle has no member: '..tostring(key))
 end})
 assert(not mm_is_parked_at_airfield(strict),'airborne native view misclassified as parked')
 local by_id=update_get_map_vehicle_by_id
 update_get_map_vehicle_by_id=function(id)if id==21 then return strict end;return by_id(id)end
 g_selection={vehicle_id=21,clear=function(self)self.vehicle_id=0 end}
 g_highlighted={vehicle_id=21,clear=g_selection.clear}
 g_drag={vehicle_id=21,waypoint_id=9,clear=g_selection.clear}
 visible=false;update(256,256,1)
 assert(g_selection.vehicle_id==21 and g_highlighted.vehicle_id==21 and g_drag.vehicle_id==21,
  'native airborne selection or drag was erased')
 state=e_vehicle_dock_state.docked;update(256,256,1)
 assert(g_selection.vehicle_id==0 and g_highlighted.vehicle_id==0 and g_drag.vehicle_id==0,
  'parked-only selection cleanup stopped working')
 native.get_dock_state=dock;update_get_map_vehicle_by_id=by_id
end
print('PASS strict native map views preserve airborne drags and clear only parked selections without proxy field probes')
do
 local invalid=setmetatable({},{__index=function()error('injected map-view inspection failure')end})
 assert(mm_is_parked_at_airfield(invalid)==false,'failed parking inspection must keep native map visibility')
 local by_id=update_get_map_vehicle_by_id
 update_get_map_vehicle_by_id=function()error('injected stale map lookup')end
 g_drag.vehicle_id=21
 update(256,256,1)
 assert(g_drag.vehicle_id==21,'failed cleanup consumed native drag')
 update_get_map_vehicle_by_id=by_id
end
print('PASS failed parking inspection/cleanup preserves native map visibility and drag state')
