-- SPDX-License-Identifier: GPL-3.0-or-later
-- Owned map/airfield UI. Engine policy stays in the independent module.
local cache, cache_session = {}, nil
local site_requested,site_revision,last_site_scan={},nil,-60
local frame = {visible=false, holomap=false, w=256, h=256, hover=nil}
local ui = {base=nil, slot=0, mode='base', controls={}, focus=1, message=nil, route=nil}
local request_serial=0
g_mm_landing_orders = {}
local function finite(v) return type(v)=='number' and v==v and math.abs(v)<2000000000 end
local function integer(v) return finite(v) and v%1==0 and v>=0 and v<=4294967295 end
local function read(topic,subject)
    local m=cc2x and cc2x.modules
    return m and type(m.read)=='function' and m.read('island_airfields',topic,subject) or nil
end
function mm_plugin()
    if read('active',0)==1 then return {
        count=function(id)return read('count',id)end,
        planes=function()return read('planes',0)==1 end,
    } end
end
function mm_remote_host()
    if type(update_get_is_multiplayer)~='function' then return false end
    local multiplayer,host=update_get_is_multiplayer();return multiplayer==true and host==false
end
function mm_airfield_count(island)
    local p=mm_plugin();if p==nil and not mm_remote_host()then return nil end
    local n=p and p.count(island:get_id()) or -1
    if n~=nil and n>=0 then return n end
    if island:get_team_control()==update_get_screen_team_id() and type(island.get_difficulty_level)=='function' then
        local level=island:get_difficulty_level();if type(level)=='number' and level>=1 and level<=4 then return level>=2 and 1 or 0 end
    end
    return -1
end
function mm_is_marked_airfield(island)
    local n=mm_airfield_count(island);if n==nil or n<0 then return true end;return n>0
end
local function own(tile)
    local t=update_get_tile_by_id(tile)
    return t and t:get() and t:get_team_control()==update_get_screen_team_id()
end
local function real(v) return type(v)=='table' and v.extended==true and v.v or v end
function mm_can_land(vehicle)
    vehicle=real(vehicle);if not vehicle or not vehicle:get() then return false end
    local p=mm_plugin();if not p and not mm_remote_host()then return false end
    local def=vehicle:get_definition_index()
    if def==e_game_object_type.chassis_air_wing_light or def==e_game_object_type.chassis_air_wing_heavy then
        return mm_remote_host() or p.planes()
    end
    return get_is_vehicle_air(def)
end
function mm_airfields()
    if not mm_plugin() and not mm_remote_host()then cache={};return {} end
    local session=read('session',0)
    if session~=cache_session then cache,cache_session={},session;g_mm_landing_orders={};site_requested,site_revision,last_site_scan={},nil,-60 end
    local revision=read('layout_revision',0)
    local serial=frame.number or 0
    if read('tile_sites',0)==1 and (revision~=site_revision or serial-last_site_scan>=60)then
        last_site_scan=serial;site_revision=revision
        for i=0,update_get_tile_count()-1 do
            local t=update_get_tile_by_index(i);local tid=t:get_id()
            if own(tid)then
                if not site_requested[tid] and cc2x.modules and type(cc2x.modules.send)=='function'then
                    site_requested[tid]=cc2x.modules.send('island_airfields','site_request',tid,0)==true
                end
                local x,z=read('site_x',tid),read('site_z',tid)
                if finite(x) and finite(z)then
                    local key=tid..':0';if not cache[key]then cache[key]={tile=tid,index=0,x=x,z=z,slots={0,0,0,0},loaded=false}end
                end
            end
        end
    end
    for _,b in pairs(cache)do b.loaded=false end
    local snapshots=cc2x and type(cc2x.airfields)=='function' and cc2x.airfields() or {}
    for _,a in ipairs(snapshots or {})do
        if integer(a.tile) and integer(a.index) and a.index<8 and own(a.tile) and
           finite(a.x) and finite(a.z) and finite(a.runway_start_x) and finite(a.runway_start_z) and finite(a.runway_end_x) and finite(a.runway_end_z) and
           (a.runway_start_x-a.runway_end_x)^2+(a.runway_start_z-a.runway_end_z)^2>1 and
           type(a.hangars)=='table' and #a.hangars==4 then
            local slots={};local valid=true
            for i=1,4 do slots[i]=a.hangars[i];valid=valid and integer(slots[i])end
            if valid then cache[a.tile..':'..a.index]={tile=a.tile,index=a.index,
                x=a.x,z=a.z,
                slots=slots,loaded=true} end
        end
    end
    local result={}
    for k,b in pairs(cache)do if own(b.tile)then result[#result+1]=b else cache[k]=nil end end
    table.sort(result,function(a,b)return a.tile==b.tile and a.index<b.index or a.tile<b.tile end)
    return result
end
local function base(tile,index)
    for _,b in ipairs(mm_airfields())do if b.tile==tile and b.index==index then return b end end
end
function mm_island_pos(island)
    for _,b in ipairs(mm_airfields())do if b.tile==island:get_id()then return b.x,b.z end end
end
-- Ordinary points, including first points, dragged routes and group/proxy orders, stay moves.
function mm_add_waypoint(vehicle,x,z) return real(vehicle):add_waypoint(x,z) end
function mm_issue_landing(vehicle,b)
    vehicle=real(vehicle)
    if not b or b.index~=0 or not base(b.tile,b.index) or not own(b.tile) or not mm_can_land(vehicle) or
       vehicle:get_team()~=update_get_screen_team_id() or vehicle:get_dock_state()~=e_vehicle_dock_state.undocked or
       vehicle:get_attached_parent_id()~=0 then return false end
    -- The reviewed generator creates one runway. Refuse ambiguous multi-runway
    -- islands until the public landing primitive addresses an exact index.
    local total=0;for _,candidate in ipairs(mm_airfields())do if candidate.tile==b.tile then total=total+1 end end
    if total~=1 then return false end
    vehicle:clear_waypoints();vehicle:clear_attack_target()
    local waypoint=vehicle:add_waypoint(b.x,b.z)
    vehicle:set_waypoint_type_barge_load_tile(waypoint,b.tile)
    g_mm_landing_orders[vehicle:get_id()]=b.tile;return true
end
function mm_is_parked_at_airfield(v)
    return v and v:get() and get_is_vehicle_air(v:get_definition_index()) and
        v:get_dock_state()==e_vehicle_dock_state.docked and v:get_attached_parent_id()==0
end
function mm_is_queued_at_airfield(v)
    local s=v:get_dock_state()
    return get_is_vehicle_air(v:get_definition_index()) and (s==e_vehicle_dock_state.dock_queue or s==e_vehicle_dock_state.docking) and
        v:get_dock_queue_vehicle_id()==0 and v:get_attached_parent_id()==0
end
local function parked(b,slot)
    if not b or not b.loaded or not integer(slot) or slot>3 then return nil end
    local id=b.slots[slot+1];if not id or id==0 then return nil end
    local v=update_get_map_vehicle_by_id(id)
    return mm_is_parked_at_airfield(v) and v:get_team()==update_get_screen_team_id() and v or nil
end
function mm_launch(tile,index,slot,x,z)
    local b=base(tile,index);local v=parked(b,slot)
    if not own(tile) or not v or not finite(x) or not finite(z)then return false end
    v:clear_waypoints();v:clear_attack_target();v:add_waypoint(x,z);return true
end
local function project(x,z)
    if frame.holomap then
        if type(get_holomap_from_world)=='function'then return get_holomap_from_world(x,z,frame.w,frame.h)end
        return get_screen_from_world(x,z,(g_map_x or 0)+(g_map_x_offset or 0),(g_map_z or 0)+(g_map_z_offset or 0),
            (g_map_size or 1)+(g_map_size_offset or 0),frame.w,frame.h,2.6/1.6)
    end
    return get_screen_from_world(x,z,g_camera_pos_x,g_camera_pos_y,g_camera_size,frame.w,frame.h)
end
local function unproject(x,y)
    -- Invert the screen's own affine transform, including Revolution HD/rotation.
    local ox,oy=project(0,0);local ax,ay=project(1000,0);local bx,by=project(0,1000)
    ax,ay,bx,by=ax-ox,ay-oy,bx-ox,by-oy;local d=ax*by-ay*bx
    if not finite(d) or math.abs(d)<0.000000001 then return nil end
    return ((x-ox)*by-(y-oy)*bx)*1000/d,(ax*(y-oy)-ay*(x-ox))*1000/d
end
local function cursor()
    if g_is_mouse_mode~=false and finite(g_pointer_pos_x) and finite(g_pointer_pos_y)then return g_pointer_pos_x,g_pointer_pos_y end
    return g_cursor_pos_x or frame.w/2,g_cursor_pos_y or frame.h/2
end
local function colour(r,g,b,a)return color8(r,g,b,a or 255)end
local function facility_colour(hovered)
    if hovered then return colour(255,255,255) end
    if type(get_island_team_color)=='function'then return get_island_team_color(update_get_screen_team_id())end
    return update_get_team_color(update_get_screen_team_id())
end
local function airfield_marker(x,y,c)
    -- The hub is a 16px team-coloured silhouette with a one-pixel black edge.
    -- Draw an owned aircraft silhouette in that same style and anchor. The
    -- production-category icon is only 8px and is not a facility marker.
    local parts={{7,1,2,12},{1,6,14,2},{4,11,8,2}}
    for _,p in ipairs(parts)do update_ui_rectangle(x-8+p[1]-1,y-11+p[2]-1,p[3]+2,p[4]+2,colour(0,0,0))end
    for _,p in ipairs(parts)do update_ui_rectangle(x-8+p[1],y-11+p[2],p[3],p[4],c)end
end
function mm_draw_airfields(w,h,holomap)
    frame.visible,frame.holomap,frame.w,frame.h,frame.hover=true,holomap,w,h,nil
    local px,py=cursor();local best=49
    for _,b in ipairs(mm_airfields())do
        local x,y=project(b.x,b.z);local dx,dy=x-px,y-py;local distance=dx*dx+dy*dy
        if distance<best then frame.hover,best=b,distance end
        airfield_marker(x,y,facility_colour(distance<49))
    end
    if holomap then return end
    for i=0,update_get_map_vehicle_count()-1 do
        local v=update_get_map_vehicle_by_index(i)
        if v:get() and v:get_team()==update_get_screen_team_id() and get_is_vehicle_air(v:get_definition_index())then
            local p=v:get_position_xz();local x,y=project(p:x(),p:y());local c=update_get_team_color(v:get_team())
            if mm_is_parked_at_airfield(v)then update_ui_circle(x,y,7,12,c)
            elseif mm_is_queued_at_airfield(v)then
                local tid=g_mm_landing_orders[v:get_id()]
                if tid then
                    local t=update_get_tile_by_id(tid)
                    if t and t:get()then local tx,tz=mm_island_pos(t)
                        if tx and tz then local sx,sy=project(tx,tz);render_dashed_line(x,y,sx,sy,c)end
                    end
                end
            end
        end
    end
end
function mm_draw_cc2x_notice(w,h)
    if mm_plugin() or mm_remote_host()then return end
    update_ui_rectangle(0,h-13,w,13,colour(0,0,0,220))
    update_ui_text(4,h-11,'Island Airfields: SDK/module unavailable',w-8,0,colour(255,190,0),0)
end
local function name(v)
    if type(get_chassis_data_by_definition_index)=='function'then return get_chassis_data_by_definition_index(v:get_definition_index())end
    return 'Aircraft '..v:get_id()
end
local function close()ui.base=nil;ui.mode='base';ui.controls={};ui.message=nil end
function mm_open_airfield(tile,index)
    local b=base(tile,index);if not b then return false end
    ui.base={tile=tile,index=index};ui.slot=0;ui.mode='base';ui.focus=1;ui.message=nil;ui.land_page=0;ui.hardpoint_page=0
    if g_selection and type(g_selection.clear)=='function'then g_selection:clear()end
    return true
end
local function button(text,enabled,action)
    local n=#ui.controls+1;local step=math.min(22,(frame.h-48)/8);local y=25+(n-1)*step
    ui.controls[n]={x=8,y=y,w=frame.w-16,h=step-3,enabled=enabled,action=action}
    update_ui_rectangle(8,y,frame.w-16,step-3,enabled and (ui.focus==n and colour(65,95,100) or colour(32,43,48)) or colour(22,25,28))
    update_ui_text(12,y+5,text,frame.w-24,0,enabled and colour(235,245,245) or colour(105,110,110),0)
end
local function equipment(v)
    return read('equipment',0)==1 and cc2x and cc2x.modules and type(cc2x.modules.send)=='function' and
        v and mm_is_parked_at_airfield(v) and v:get_team()==update_get_screen_team_id() and
        v:get_waypoint_count()==0 and not mm_remote_host() and not ui.pending
end
function mm_set_loadout(v,slot,definition)
    if not equipment(v) or not integer(slot) or slot>=64 or not finite(definition) or definition%1~=0 or definition< -1 or definition>101 then return false end
    request_serial=request_serial%255+1
    local payload=request_serial*35184372088832+v:get_id()*8192+slot*128+definition+1
    if math.abs(read('loadout_reply',v:get_id()%32) or 0)==payload then
        request_serial=request_serial%255+1;payload=request_serial*35184372088832+v:get_id()*8192+slot*128+definition+1
    end
    if not cc2x.modules.send('island_airfields','loadout',v:get_id(),payload)then return false end
    ui.pending={vehicle=v:get_id(),payload=payload,session=read('session',0),age=0}
    return true
end
local function result(ticks)
    local p=ui.pending;if not p then return end
    if p.session~=read('session',0)then ui.pending=nil;ui.message='Session changed';return end
    p.age=p.age+math.max(1,ticks or 1)
    local reply=read('loadout_reply',p.vehicle%32)
    if reply==p.payload or reply==-p.payload then
        ui.pending=nil;ui.message=reply>0 and 'Loadout changed (free)' or 'Loadout change refused'
    elseif p.age>300 then ui.pending=nil;ui.message='Result unavailable; inspect current loadout' end
end
local function panel()
    if not ui.base then
        if ui.route then update_ui_text(5,5,'Choose launch destination (Back cancels)',frame.w-10,0,colour(255,215,100),0)end
        return
    end
    local b=base(ui.base.tile,ui.base.index);if not b then close();return end
    ui.controls={}
    update_ui_rectangle(0,0,frame.w,frame.h,colour(8,14,18,250))
    local t=update_get_tile_by_id(b.tile);local label=type(t.get_name)=='function' and t:get_name() or tostring(b.tile)
    update_ui_text(8,7,'Airfield: '..label,frame.w-16,0,colour(235,245,245),0)
    local v=parked(b,ui.slot)
    if ui.mode=='land'then
        local airborne={}
        for i=0,update_get_map_vehicle_count()-1 do
            local a=update_get_map_vehicle_by_index(i)
            if mm_can_land(a) and a:get_team()==update_get_screen_team_id() and a:get_dock_state()==e_vehicle_dock_state.undocked and a:get_attached_parent_id()==0 then airborne[#airborne+1]=a end
        end
        local pages=math.max(1,math.ceil(#airborne/5));ui.land_page=(ui.land_page or 0)%pages
        for i=ui.land_page*5+1,math.min(#airborne,ui.land_page*5+5)do local a=airborne[i]
                button(name(a)..' #'..a:get_id(),true,function()
                    ui.message=mm_issue_landing(a,b) and 'Landing ordered' or 'Landing unavailable';ui.mode='base'
                end)
        end
        if pages>1 then button('Next page',true,function()ui.land_page=(ui.land_page+1)%pages;ui.focus=1 end)end
        if #airborne==0 then button('No airborne aircraft available',false,function()end)end
        button('Back',true,function()ui.mode='base';ui.focus=1 end)
    elseif ui.mode=='loadout' and v then
        if ui.attachment~=nil then
            local kind=v:get_attachment_type(ui.attachment)
            local options={{definition=-1,name='Clear attachment'}}
            if type(update_get_attachment_option_count)=='function'then
                for i=0,math.min(update_get_attachment_option_count(kind),255)-1 do
                    local definition=update_get_attachment_option(kind,i)
                    local info=type(get_attachment_data_by_definition_index)=='function' and get_attachment_data_by_definition_index(definition)
                    options[#options+1]={definition=definition,name=info and info.name or ('Attachment '..definition)}
                end
            end
            ui.page=math.max(0,math.min(ui.page or 0,math.max(0,math.ceil(#options/5)-1)))
            for i=ui.page*5+1,math.min(#options,ui.page*5+5)do local option=options[i]
                button(option.name,equipment(v),function()
                    local ok=mm_set_loadout(v,ui.attachment,option.definition)
                    ui.message=ok and 'Changing loadout...' or 'Change refused';ui.attachment=nil
                end)
            end
            if #options>5 then button('Next page',true,function()ui.page=(ui.page+1)%math.ceil(#options/5)end)end
            button('Back',true,function()ui.attachment=nil end)
        else
            local total=math.min(v:get_attachment_count(),64);local pages=math.max(1,math.ceil(total/5))
            ui.hardpoint_page=(ui.hardpoint_page or 0)%pages
            for i=ui.hardpoint_page*5,math.min(total,ui.hardpoint_page*5+5)-1 do local slot=i
                local a=v:get_attachment(i);local current='Empty'
                if a and a:get() and type(get_attachment_data_by_definition_index)=='function'then
                    local info=get_attachment_data_by_definition_index(a:get_definition_index());current=info and info.name or current
                end
                button('Hardpoint '..(i+1)..': '..current,equipment(v),function()ui.attachment=slot;ui.page=0;ui.focus=1 end)
            end
            if pages>1 then button('Next page',true,function()ui.hardpoint_page=(ui.hardpoint_page+1)%pages;ui.focus=1 end)end
            button('Back',true,function()ui.mode='base';ui.focus=1 end)
        end
    else
        ui.mode='base'
        for i=0,3 do local slot=i;local id=b.slots[i+1] or 0;local a=id>0 and update_get_map_vehicle_by_id(id)
            local label=id==0 and 'Empty' or (a and a:get() and name(a)..' #'..id or 'Unavailable')
            button((ui.slot==i and '> ' or '  ')..'Bay '..(i+1)..': '..label,b.loaded,function()ui.slot=slot end)
        end
        button('Land aircraft',b.index==0,function()ui.mode='land';ui.focus=1 end)
        button('Loadout (free)',equipment(v),function()ui.mode='loadout';ui.attachment=nil;ui.focus=1 end)
        button('Launch selected aircraft',v~=nil and not ui.pending,function()ui.route={tile=b.tile,index=b.index,slot=ui.slot};close()end)
        button('Close',true,close)
    end
    if not b.loaded then ui.message='Airfield not loaded; waiting for current data'
    elseif ui.pending then ui.message='Changing loadout...'
    elseif v and not equipment(v) and ui.mode=='base'then ui.message='Loadout needs the equipment SDK capability'end
    if ui.message then update_ui_text(8,frame.h-19,ui.message,frame.w-16,0,colour(255,210,100),0)end
end
local original_update,original_event=update,input_event
if type(original_update)=='function'then
    function update(w,h,ticks)
        frame.visible,frame.hover=false,nil;frame.w,frame.h=w,h;frame.number=(frame.number or 0)+1
        original_update(w,h,ticks)
        result(ticks)
        if frame.visible then panel()else close();ui.route=nil end
    end
end
local function pointed(x,y)
    for i,b in ipairs(ui.controls)do if x>=b.x and x<b.x+b.w and y>=b.y and y<b.y+b.h then return i,b end end
end
if type(original_event)=='function'then
    function input_event(event,action)
        if ui.suppress_release==event and action==e_input_action.release then ui.suppress_release=nil;return end
        if frame.visible then
            local pressed=action==e_input_action.press
            if ui.base then
                if pressed and event==e_input.back then
                    if ui.mode=='base'then close()else ui.mode='base';ui.attachment=nil;ui.focus=1 end
                elseif pressed and (event==e_input.down or event==e_input.right)then ui.focus=ui.focus%math.max(1,#ui.controls)+1
                elseif pressed and (event==e_input.up or event==e_input.left)then ui.focus=(ui.focus-2)%math.max(1,#ui.controls)+1
                elseif pressed and event==e_input.action_a then local b=ui.controls[ui.focus];if b and b.enabled then b.action()end
                elseif event==e_input.pointer_1 then
                    local x,y=cursor();local i,b=pointed(x,y)
                    if pressed then ui.pressed=i;ui.focus=i or ui.focus
                    elseif i and i==ui.pressed and b.enabled then b.action()end
                    if not pressed then ui.pressed=nil end
                end
                g_is_pointer_pressed=false;return
            elseif ui.route then
                if pressed and event==e_input.back then ui.route=nil;return end
                if pressed and (event==e_input.pointer_1 or event==e_input.action_a)then
                    local x,y=cursor();local wx,wz=unproject(x,y);local r=ui.route
                    if wx and mm_launch(r.tile,r.index,r.slot,wx,wz)then ui.route=nil end
                    ui.suppress_release=event
                    g_is_pointer_pressed=false;return
                end
            elseif frame.hover and (event==e_input.pointer_1 or event==e_input.action_a)then
                local b=frame.hover;local id=g_drag and g_drag.vehicle_id or 0
                if not pressed and id>0 then
                    if mm_issue_landing(update_get_map_vehicle_by_id(id),b)then
                        if type(g_drag.clear)=='function'then g_drag:clear()end;g_is_pointer_pressed=false;return
                    end
                elseif pressed then mm_open_airfield(b.tile,b.index);g_is_pointer_pressed=false;return end
            end
        end
        return original_event(event,action)
    end
end
