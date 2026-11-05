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
g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
input_event(e_input.pointer_1,1);input_event(e_input.pointer_1,0);frame()
assert(has('HANGAR') and has('Manta') and has('BAY 1') and has('HP 75%') and original_calls==0,'four-bay overview failed')
within_screen();capture('hangar-256x256')
click_label('Empty bay');click_label('Loadout (free)')
assert(not has('Hardpoint 1') and #mail==0,'empty bay opened the previous aircraft loadout')
click_label('Manta')
click_label('Loadout (free)');assert(has('Hardpoint 1'),'loadout button failed')
within_screen();capture('hardpoints-256x256')
click_label('>');assert(has('Hardpoint 8'),'remaining hardpoints cannot be reached')
click_label('Hardpoint 8');assert(has('Clear attachment') and has('FITTED'),'current empty attachment not marked')
click_label('>');assert(has('Test weapon 35') and not has('Test weapon 36'),'hidden carrier options leaked')
click_label('<');click_label('Test weapon 30')
assert(#mail==1 and mail[1][1]=='loadout' and mail[1][2]==21)
assert(not mm_set_loadout(test_aircraft,0,30),'duplicate pending mutation allowed')
reply=mail[1][3];frame();assert(mm_set_loadout(test_aircraft,0,30))
reply=-mail[2][3];frame()
input_event(e_input.back,1);frame()
assert(has('HANGAR'),'hardpoint Back did not return to hangar')
click_label('Loadout (free)');click_label('Hardpoint 1')
input_event(e_input.back,1);frame();assert(has('Hardpoint 1'),'attachment Back skipped the hardpoint list')
input_event(e_input.back,1);frame();click_label('Launch')
click(140,160)
assert(test_aircraft.target and math.abs(test_aircraft.target[1]-1100)<.001 and math.abs(test_aircraft.target[2]-1200)<.001,'launch did not invert original transform')
-- Layout and pagination stay usable on the small in-game monitor.
for _,size in ipairs({{128,128},{128,256},{192,128},{256,128},{256,192},{512,256}})do
 width,height=size[1],size[2];assert(mm_open_airfield(10,0));frame();within_screen()
 if height==128 then capture('hangar-'..width..'x128')end
 click_label(width<220 and 'Loadout' or 'Loadout (free)');within_screen()
 if height==128 then capture('hardpoints-'..width..'x128')end
 local pages=0
 while not has('Hardpoint 8')do click_label('>');within_screen();pages=pages+1;assert(pages<8)end
 click_label('Hardpoint 8');within_screen()
 -- Long translated names wrap inside rows and do not cover navigation.
 local old=get_attachment_data_by_definition_index
 function get_attachment_data_by_definition_index()return {name='Extended range infrared guided anti air missile',icon16=78}end
 frame();within_screen()
 if height==128 then capture('weapons-'..width..'x128')end
 get_attachment_data_by_definition_index=old
end
width,height=256,256;assert(mm_open_airfield(10,0));frame()
-- Gamepad paths retain action parity with the pointer; opening loadout is safe.
g_is_mouse_mode=false
for _,event in ipairs({e_input.right,e_input.left,e_input.down,e_input.down})do input_event(event,1)end
input_event(e_input.action_a,1);frame();assert(has('Hardpoint 1'),'gamepad cannot reach loadout')
input_event(e_input.back,1);frame()
visible=false;frame();local before=original_calls;input_event(e_input.action_a,1)
assert(original_calls==before+1,'invisible map intercepted an original input')
-- Stock holomap has no Revolution helper; its own camera/aspect function is used.
get_holomap_from_world=nil;drawn={};mm_draw_airfields(256,256,true)
assert(drawn[1][1]==118 and drawn[1][2]==49 and #drawn==6)
print('PASS readable four-bay overview, status, hardpoint/weapon pages, fitted and hidden choices, hierarchical Back, bounded 128/192/256px layouts, free mailbox/pending guard, gamepad, launch transform and stock fallback')
