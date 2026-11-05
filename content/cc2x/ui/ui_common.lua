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
local function landing_target(v)
    local tid=read('landing',v:get_id())
    if tid==nil then tid=g_mm_landing_orders[v:get_id()] end
    if integer(tid) and tid>0 and own(tid)then return tid,read('landing_stage',v:get_id()) or 1 end
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
local function map_link(x,y,tx,ty,c)
    if not finite(x) or not finite(y) or not finite(tx) or not finite(ty)then return end
    local dx,dy=tx-x,ty-y;local first,last=0,1
    local function edge(p,q)
        if p==0 then return q>=0 end
        local t=q/p
        if p<0 then first=math.max(first,t)else last=math.min(last,t)end
        return first<=last
    end
    if not edge(-dx,x) or not edge(dx,frame.w-1-x) or not edge(-dy,y) or not edge(dy,frame.h-1-y)then return end
    tx,ty=x+dx*last,y+dy*last;x,y=x+dx*first,y+dy*first
    if not frame.holomap and type(render_dashed_line)=='function'then render_dashed_line(x,y,tx,ty,c);return end
    -- Holomap has no vehicle-control render_dashed_line helper. Bound work to
    -- the visible segment and use the same three-pixel dash/spacing style.
    dx,dy=tx-x,ty-y;local length=math.sqrt(dx*dx+dy*dy);if length<1 then return end
    local step=math.max(6,length/512)
    for cursor=0,length,step do
        local finish=math.min(cursor+3,length)
        update_ui_line(x+dx*cursor/length,y+dy*cursor/length,x+dx*finish/length,y+dy*finish/length,c)
    end
end
function mm_draw_airfields(w,h,holomap)
    frame.visible,frame.holomap,frame.w,frame.h,frame.hover=true,holomap,w,h,nil
    local px,py=cursor();local best=49;local positions,arrivals={},{}
    frame.traffic_hint=nil
    for _,b in ipairs(mm_airfields())do
        local x,y=project(b.x,b.z);local dx,dy=x-px,y-py;local distance=dx*dx+dy*dy
        if distance<best then frame.hover,best=b,distance end
        airfield_marker(x,y,facility_colour(distance<49))
        positions[b.tile]={x=x,y=y}
    end
    for i=0,update_get_map_vehicle_count()-1 do
        local v=update_get_map_vehicle_by_index(i)
        if v:get() and v:get_team()==update_get_screen_team_id() and get_is_vehicle_air(v:get_definition_index())then
            local p=v:get_position_xz();local x,y=project(p:x(),p:y());local c=update_get_team_color(v:get_team())
            if mm_is_parked_at_airfield(v)then
                if not holomap then update_ui_circle(x,y,7,12,c)end
            else
                local tid,stage=landing_target(v)
                if tid then
                    arrivals[tid]=(arrivals[tid] or 0)+1
                    local destination=positions[tid]
                    if destination then map_link(x,y,destination.x,destination.y,c)end
                    local label=stage==2 and 'QUEUE' or (stage==3 and 'HOLD' or (stage==5 and 'TAXI' or 'LAND'))
                    if x>=0 and x<frame.w and y>=0 and y<frame.h then
                        update_ui_text(math.min(x+7,frame.w-34),math.max(0,y-10),label,34,0,c,0)
                    end
                    if (x-px)^2+(y-py)^2<64 then
                        local t=update_get_tile_by_id(tid)
                        local destination_name=type(t.get_name)=='function' and t:get_name() or ('Island '..tid)
                        frame.traffic_hint=label..' -> '..destination_name
                    end
                end
            end
        end
    end
    for tid,n in pairs(arrivals)do local p=positions[tid]
        if p and p.x>=0 and p.x<frame.w and p.y>=0 and p.y<frame.h then
            local x,y=math.max(0,math.min(frame.w-20,p.x+8)),math.max(0,p.y-20)
            update_ui_rectangle(x,y,20,9,colour(0,0,0))
            update_ui_text(x+1,y,tostring(n),18,0,facility_colour(false),0)
        end
    end
    frame.arrivals=arrivals
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
-- Keep labels inside their own controls, including translated chassis names.
-- Six pixels per character is conservative for the stock screen font.
local function lines(text,width,limit)
    local chars={}
    if utf8 and utf8.codes then
        for _,code in utf8.codes(tostring(text))do chars[#chars+1]=utf8.char(code)end
    else for char in tostring(text):gmatch('.')do chars[#chars+1]=char end end
    local count=math.max(1,math.floor(width/6));local output={}
    while #chars>0 and #output<limit do
        local n=math.min(count,#chars)
        if #chars>n and #output<limit-1 then
            for i=n,math.floor(n/2),-1 do if chars[i]==' 'then n=i;break end end
        end
        local row={};for _=1,n do row[#row+1]=table.remove(chars,1)end
        if #output==limit-1 and #chars>0 then
            while #row>math.max(0,count-3)do table.remove(row)end
            row[#row+1]=string.rep('.',math.min(3,count))
        end
        output[#output+1]=table.concat(row):gsub('%s+$','')
        while chars[1]==' 'do table.remove(chars,1)end
    end
    return output
end
local function text(x,y,value,w,c,max_lines)
    for i,row in ipairs(lines(value,w,max_lines or 1))do update_ui_text(x,y+(i-1)*9,row,w,0,c,0)end
end
local function outline(x,y,w,h,c)
    update_ui_rectangle(x,y,w,1,c);update_ui_rectangle(x,y+h-1,w,1,c)
    update_ui_rectangle(x,y,1,h,c);update_ui_rectangle(x+w-1,y,1,h,c)
end
local function button(x,y,w,h,label,enabled,action,selected,detail,icon)
    local n=#ui.controls+1
    ui.controls[n]={x=x,y=y,w=w,h=h,enabled=enabled,action=action}
    local px,py=cursor()
    if g_is_mouse_mode~=false and enabled and px>=x and px<x+w and py>=y and py<y+h then ui.focus=n end
    local focused=ui.focus==n and enabled
    local fg=enabled and colour(235,245,245) or colour(110,120,125)
    update_ui_rectangle(x,y,w,h,focused and colour(55,78,84) or colour(24,34,40))
    outline(x,y,w,h,selected and colour(95,220,210) or (focused and colour(235,245,245) or colour(57,73,80)))
    local tx=x+5
    if icon and frame.w>=192 then update_ui_image(x+5,y+math.floor((h-16)/2),icon,fg,0);tx=x+25 end
    local tw=x+w-tx-4
    if detail then
        if h>=30 and #lines(label,tw,2)>1 then
            text(tx,y+3,label,tw,fg,2)
            text(tx,y+h-10,detail,tw,colour(155,175,180))
        else
            text(tx,y+3,label,tw,fg)
            text(tx,y+12,detail,tw,colour(155,175,180),h>=30 and 2 or 1)
        end
    else text(tx,y+math.max(3,math.floor((h-18)/2)),label,tw,fg,h>=26 and 2 or 1)end
end
local function aircraft_data(v)
    if v and type(get_chassis_data_by_definition_index)=='function'then
        return get_chassis_data_by_definition_index(v:get_definition_index())
    end
    return v and name(v) or 'Empty bay',nil
end
local function current_attachment(v,slot)
    local a=v:get_attachment(slot)
    if a and a:get()then
        local definition=a:get_definition_index()
        local info=type(get_attachment_data_by_definition_index)=='function' and get_attachment_data_by_definition_index(definition)
        return definition,info and info.name or ('Attachment '..definition),info and info.icon16
    end
    return -1,'Empty',atlas_icons and atlas_icons.icon_attachment_16_none
end
local function back()
    ui.message=nil;ui.focus=1
    if ui.mode=='loadout' and ui.attachment~=nil then ui.attachment=nil
    elseif ui.mode~='base'then ui.mode='base';ui.attachment=nil
    else close()end
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
        if ui.route then
            update_ui_rectangle(0,0,frame.w,29,colour(8,14,18,245))
            text(6,4,'Launch: choose destination',frame.w-12,colour(255,215,100))
            text(6,15,'Back cancels',frame.w-12,colour(170,185,190))
        elseif frame.traffic_hint then
            update_ui_rectangle(0,frame.h-13,frame.w,13,colour(8,14,18,245))
            text(6,frame.h-11,frame.traffic_hint,frame.w-12,colour(235,245,245))
        end
        return
    end
    local b=base(ui.base.tile,ui.base.index);if not b then close();return end
    ui.controls={}
    local compact,narrow=frame.h<180,frame.w<220
    local white=colour(235,245,245);local muted=colour(155,175,180)
    local t=update_get_tile_by_id(b.tile);local label=type(t.get_name)=='function' and t:get_name() or tostring(b.tile)
    local v=parked(b,ui.slot)
    if ui.mode=='loadout' and not v then ui.mode='base';ui.attachment=nil end
    update_ui_rectangle(0,0,frame.w,frame.h,colour(8,14,18,255))
    update_ui_rectangle(0,0,frame.w,16,white)
    local heading='AIRFIELD: '..label
    if narrow and ui.mode=='land'then heading='LAND AIRCRAFT'
    elseif narrow and ui.mode=='loadout'then heading=ui.attachment~=nil and 'HARDPOINT '..(ui.attachment+1) or 'LOADOUT (FREE)'end
    text(6,4,heading,frame.w-32,colour(0,0,0))
    local footer=compact and 12 or 24
    local message=ui.message
    if ui.pending then message='Changing loadout...'
    elseif not b.loaded then message='Approach island to view hangar data'end
    if ui.mode=='base'then
        local arriving=frame.arrivals and frame.arrivals[b.tile] or 0
        local heading=narrow and 'HANGAR / 4 BAYS' or 'HANGAR / SELECT A BAY'
        if arriving>0 then heading=narrow and ('ARRIVALS: '..arriving) or ('4 BAYS / '..arriving..' ARRIVING')end
        text(8,20,heading,frame.w-16,muted)
        local gap=4;local card_w=math.floor((frame.w-20)/2)
        local card_h=compact and 19 or math.max(21,math.min(44,math.floor((frame.h-146)/2)))
        local grid_y=compact and 30 or 33
        for i=0,3 do local slot=i;local id=b.slots[i+1] or 0
            local a=id>0 and update_get_map_vehicle_by_id(id)
            local available=a and a:get()
            local chassis,icon,handle=aircraft_data(available and a or nil)
            if narrow and handle then chassis=handle end
            if narrow and id==0 then chassis='Empty'end
            if not b.loaded then chassis='No live data';icon=nil
            elseif id>0 and not available then chassis='Unavailable'end
            local detail=id==0 and 'EMPTY' or ('#'..id..(parked(b,i) and ' / PARKED' or ' / BUSY'))
            local x=8+(i%2)*(card_w+gap);local y=grid_y+math.floor(i/2)*(card_h+gap)
            -- Bay number is always visible independently of a long chassis name.
            local number_w=narrow and 12 or 16
            update_ui_rectangle(x,y,number_w,card_h,colour(45,61,67))
            text(x+4,y+math.floor((card_h-8)/2),i+1,12,white)
            button(x+number_w,y,card_w-number_w,card_h,chassis,b.loaded,function()ui.slot=slot;ui.message=nil end,
                ui.slot==i,not compact and detail or nil,not compact and not narrow and icon or nil)
        end
        local action_h=compact and 16 or 24
        local action_y=frame.h-footer-action_h*2-8
        local grid_end=grid_y+card_h*2+gap
        if not compact and action_y-grid_end>=29 then
            local selected=v and name(v)..' #'..v:get_id() or (b.loaded and 'Empty bay' or 'No live aircraft data')
            text(8,grid_end+7,'BAY '..(ui.slot+1)..' / '..selected,frame.w-16,white)
            local stats={}
            for _,item in ipairs({{'get_repair_factor','HP'},{'get_fuel_factor','FUEL'},{'get_ammo_factor','AMMO'}})do
                if v and type(v[item[1]])=='function'then
                    local value=v[item[1]](v);if finite(value)then stats[#stats+1]=item[2]..' '..math.floor(value*100+.5)..'%'end
                end
            end
            text(8,grid_end+19,table.concat(stats,'  '),frame.w-16,muted)
        end
        local half=math.floor((frame.w-20)/2)
        button(8,action_y,half,action_h,frame.w<220 and 'Loadout' or 'Loadout (free)',equipment(v),function()
            ui.mode='loadout';ui.attachment=nil;ui.hardpoint_page=0;ui.focus=1;ui.message=nil
        end)
        button(12+half,action_y,half,action_h,'Launch',v~=nil and not ui.pending,function()
            ui.route={tile=b.tile,index=b.index,slot=ui.slot};close()
        end)
        button(8,action_y+action_h+4,frame.w-16,action_h,'Land aircraft...',b.index==0,function()
            ui.mode='land';ui.land_page=0;ui.focus=1;ui.message=nil
        end)
        if not message then
            if not b.loaded then message='Approach island to view hangar data'
            elseif not v then message=(b.slots[ui.slot+1] or 0)==0 and 'Empty bay: land an aircraft here' or 'Aircraft is busy or unavailable'
            elseif mm_remote_host()then message='Remote loadout control unavailable'
            elseif read('equipment',0)~=1 then message='Loadout needs the matching SDK'
            else message=narrow and 'Choose an action' or 'Select bay, then loadout or launch'end
        end
    else
        local rows={};local page_field;local title
        if ui.mode=='land'then
            title='LAND / SELECT AIRCRAFT';page_field='land_page'
            for i=0,update_get_map_vehicle_count()-1 do
                local a=update_get_map_vehicle_by_index(i)
                if mm_can_land(a) and a:get_team()==update_get_screen_team_id() and a:get_dock_state()==e_vehicle_dock_state.undocked and a:get_attached_parent_id()==0 then
                    local chassis,icon=aircraft_data(a)
                    rows[#rows+1]={label=chassis,detail='#'..a:get_id()..' / AIRBORNE',icon=icon,enabled=true,action=function()
                        ui.message=mm_issue_landing(a,b) and 'Landing ordered' or 'Landing unavailable';ui.mode='base';ui.focus=1
                    end}
                end
            end
            if #rows==0 then message=message or 'No airborne aircraft available'end
        elseif ui.attachment~=nil then
            title='BAY '..(ui.slot+1)..' / HARDPOINT '..(ui.attachment+1);page_field='page'
            local current=current_attachment(v,ui.attachment)
            local options={{definition=-1,name='Clear attachment',icon=atlas_icons and atlas_icons.icon_attachment_16_none}}
            if type(update_get_attachment_option_count)=='function'then
                local kind=v:get_attachment_type(ui.attachment)
                for i=0,math.min(update_get_attachment_option_count(kind),255)-1 do
                    local definition=update_get_attachment_option(kind,i)
                    if definition>=0 and (type(update_get_attachment_option_hidden)~='function' or not update_get_attachment_option_hidden(definition))then
                        local info=type(get_attachment_data_by_definition_index)=='function' and get_attachment_data_by_definition_index(definition)
                        options[#options+1]={definition=definition,name=info and info.name or ('Attachment '..definition),icon=info and info.icon16}
                    end
                end
            end
            for _,option in ipairs(options)do
                local fitted=option.definition==current
                rows[#rows+1]={label=option.name,detail=fitted and 'FITTED' or (option.definition<0 and 'Leave hardpoint empty' or 'FREE / FULL AMMO'),icon=option.icon,
                    selected=fitted,enabled=equipment(v) and not fitted,action=function()
                        local ok=mm_set_loadout(v,ui.attachment,option.definition)
                        ui.message=ok and 'Changing loadout...' or 'Change refused';ui.attachment=nil;ui.focus=1
                    end}
            end
        else
            title='BAY '..(ui.slot+1)..' / LOADOUT';page_field='hardpoint_page'
            for i=0,math.min(v:get_attachment_count(),64)-1 do local slot=i
                local _,current,icon=current_attachment(v,i)
                rows[#rows+1]={label='Hardpoint '..(i+1),detail=current,icon=icon,enabled=equipment(v),action=function()
                    ui.attachment=slot;ui.page=0;ui.focus=1;ui.message=nil
                end}
            end
            if #rows==0 then message=message or 'No configurable hardpoints'end
        end
        if narrow and v then title='B'..(ui.slot+1)..' / '..name(v)..' #'..v:get_id()end
        text(8,21,title,frame.w-16,white)
        if not compact and v then text(8,35,name(v)..' #'..v:get_id(),frame.w-16,muted)end
        local row_y=compact and 32 or 49;local row_h=compact and 30 or 36
        local nav_h=compact and 18 or 22;local nav_y=frame.h-footer-nav_h-2
        local per_page=math.max(1,math.floor((nav_y-row_y+3)/(row_h+3)))
        local pages=math.max(1,math.ceil(#rows/per_page))
        ui[page_field]=math.max(0,math.min(ui[page_field] or 0,pages-1))
        local page=ui[page_field]
        for i=page*per_page+1,math.min(#rows,(page+1)*per_page)do
            local row=rows[i]
            button(8,row_y,frame.w-16,row_h,row.label,row.enabled,row.action,row.selected,row.detail,row.icon)
            row_y=row_y+row_h+3
        end
        button(8,nav_y,narrow and 36 or 60,nav_h,'Back',true,back)
        local next_x=frame.w-(narrow and 26 or 44);local prev_x=next_x-(narrow and 52 or 82)
        button(prev_x,nav_y,narrow and 18 or 30,nav_h,'<',page>0,function()ui[page_field]=page-1;ui.focus=1 end)
        text(prev_x+(narrow and 21 or 34),nav_y+5,(page+1)..'/'..pages,narrow and 30 or 45,muted)
        button(next_x,nav_y,narrow and 18 or 36,nav_h,'>',page+1<pages,function()ui[page_field]=page+1;ui.focus=1 end)
        message=message or (ui.mode=='land' and 'Select aircraft to order landing' or 'Select a hardpoint or attachment')
    end
    button(frame.w-21,1,20,14,'X',true,close)
    update_ui_rectangle(0,frame.h-footer-1,frame.w,1,colour(57,73,80))
    if message then text(8,frame.h-footer+2,message,frame.w-16,ui.pending and colour(255,210,100) or muted,compact and 1 or 2)end
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
local function navigate(event)
    local origin=ui.controls[ui.focus];if not origin then return end
    local dx=event==e_input.right and 1 or (event==e_input.left and -1 or 0)
    local dy=event==e_input.down and 1 or (event==e_input.up and -1 or 0)
    local x,y=origin.x+origin.w/2,origin.y+origin.h/2
    local best,score=nil,math.huge
    for i,b in ipairs(ui.controls)do if i~=ui.focus and b.enabled then
        local bx,by=b.x+b.w/2-x,b.y+b.h/2-y
        local along,cross=bx*dx+by*dy,math.abs(bx*dy-by*dx)
        local cost=along+cross*3
        if along>1 and cost<score then best,score=i,cost end
    end end
    if best then ui.focus=best end
end
if type(original_event)=='function'then
    function input_event(event,action)
        if ui.suppress_release==event and action==e_input_action.release then ui.suppress_release=nil;return end
        if frame.visible then
            local pressed=action==e_input_action.press
            if ui.base then
                if pressed and event==e_input.back then
                    back()
                elseif pressed and (event==e_input.down or event==e_input.right or event==e_input.up or event==e_input.left)then navigate(event)
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
