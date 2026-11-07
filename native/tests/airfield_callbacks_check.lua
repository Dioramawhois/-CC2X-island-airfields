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
  assert(has('2/2') and #images==3,'last image page should contain three choices, excluding the hidden definition')
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
assert(drawn[1][1]==118 and drawn[1][2]==49 and #drawn==6)
print('PASS four-bay direct aircraft flow, native carrier widget adapter and real hardpoint indices, picture choices, bottom launch, no landing menu, pending/bay identity guards, mouse/gamepad, monitor bounds, saved queue links and holomap fallback')
