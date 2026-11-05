-- SPDX-License-Identifier: GPL-3.0-or-later
local function frame()texts={};update(256,256,1)end
local function click(x,y)
 g_pointer_pos_x,g_pointer_pos_y=x,y;frame()
 input_event(e_input.pointer_1,1);input_event(e_input.pointer_1,0);frame()
end
local function has(s)for _,t in ipairs(texts)do if t:find(s,1,true)then return true end end end
g_pointer_pos_x,g_pointer_pos_y=120,60;frame()
input_event(e_input.pointer_1,1);input_event(e_input.pointer_1,0);frame()
assert(has('Bay 4') and not has('Bay 5') and original_calls==0,'airfield click did not open four bays')
click(15,140);assert(has('Hardpoint 1'),'loadout button failed')
click(15,140);assert(has('Hardpoint 8'),'remaining hardpoints cannot be reached')
click(15,25);assert(has('Clear attachment'),'hardpoint choice failed')
click(15,47);assert(#mail==1 and mail[1][1]=='loadout' and mail[1][2]==21)
assert(not mm_set_loadout(test_aircraft,0,30),'duplicate pending mutation allowed')
reply=mail[1][3];frame();assert(mm_set_loadout(test_aircraft,0,30))
reply=-mail[2][3];frame()
input_event(e_input.back,1);frame();input_event(e_input.back,1);frame()
assert(mm_open_airfield(10,0));frame();click(15,158)
click(140,160);assert(test_aircraft.target and math.abs(test_aircraft.target[1]-1100)<.001 and math.abs(test_aircraft.target[2]-1200)<.001,'launch did not invert original transform')
visible=false;frame();local before=original_calls;input_event(e_input.action_a,1)
assert(original_calls==before+1,'invisible map intercepted an original input')
-- Stock holomap has no Revolution helper; its own camera/aspect function is used.
get_holomap_from_world=nil;drawn={};mm_draw_airfields(256,256,true)
assert(drawn[1][1]==112 and drawn[1][2]==52)
print('PASS actual callback wrapper, mouse modal, eight hardpoints, free mailbox request/result, pending guard, launch transform and stock fallback')
