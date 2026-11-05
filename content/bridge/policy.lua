-- SPDX-License-Identifier: GPL-3.0-or-later
-- Island Airfields gameplay policy, migrated from the project's native v46.
-- Only public cc2 Lua operations; keep IDs between callbacks, never selectors.
local M = {}
local pi, sin, cos, sqrt, abs, min, max = math.pi, math.sin, math.cos, math.sqrt, math.abs, math.min, math.max
local function clamp(x, lo, hi) return max(lo, min(x, hi)) end
local function wrap(a) return (a + pi) % (2 * pi) - pi end
local function length(x, z) return sqrt(x*x + z*z) end
local function quat(yaw, pitch, roll)
    local cy, sy, cp, sp = cos(yaw/2), sin(yaw/2), cos(-pitch/2), sin(-pitch/2)
    local cr, sr = cos(roll/2), sin(roll/2)
    local x, y, z, w = cy*sp, sy*cp, -sy*sp, cy*cp
    return {x*cr+y*sr, y*cr-x*sr, w*sr+z*cr, w*cr-z*sr}
end
local function sorted_keys(t)
    local keys = {}; for key in pairs(t) do keys[#keys+1] = key end; table.sort(keys); return keys
end
function M.new(cc2, settings)
    local W, F, P = cc2.world, cc2.world.fields, {}
    local generation, pending, flights, locks, plans, plan_order = 0, {}, {}, {}, {}, {}
    local now = 0
    local function field(name)
        local value=F['CC2X_'..name];assert(value, 'Missing SDK field: '..name);return value
    end
    local function gi(o, name, index) return o:geti(field(name), index or 0) end
    local function gf(o, name, index) return o:getf(field(name), index or 0) end
    local function si(o, name, value, index) return o:seti(field(name), index or 0, value) end
    local function sf(o, name, value, index) return o:setf(field(name), index or 0, value) end
    local function count(o, name)
        local value,reason=o:count(field(name))
        assert(value, name..': '..tostring(reason));return value
    end
    local function ref(o, name, index) return o:ref(field(name), index or 0) end
    local function vehicle(id) return W.find(field('WORLD_VEHICLES'), id) end
    local function tile(id) return W.find(field('WORLD_TILES'), id) end
    local function wing(v) return gi(v, 'VEHICLE_IS_WING') ~= 0 end
    local function human(v) return gi(v, 'VEHICLE_TEAM_IS_AI') == 0 end
    local function air(v) return gi(v, 'VEHICLE_IS_AIR') ~= 0 end
    local function id(v) return gi(v, 'VEHICLE_ID') end
    local function dock(v) return gi(v, 'VEHICLE_DOCK_STATE') end
    local function key(tid, index) return tid..':'..index end
    local function island(t) return t and ref(t, 'TILE_ISLAND') end
    local function airfield(tid, index)
        local i = island(tile(tid)); return i and ref(i, 'ISLAND_AIRFIELDS', index)
    end
    local function publish(topic, subject, value) return cc2.messages.publish(topic, subject, value) end
    local function sync_session()
        local current = cc2.session.generation()
        if current == 0 then return false end
        if current ~= generation then
            generation, pending, flights, locks, plans, plan_order = current, {}, {}, {}, {}, {}
            publish('active', 0, 1); publish('planes', 0, settings.planes and 1 or 0); publish('protocol', 0, 1)
        end
        now = cc2.session.tick()
        return true
    end
    local function list_find(af, name, value)
        for k = 0, count(af, name)-1 do if gi(af, name, k) == value then return k end end
        return -1
    end
    local function release(af, vid)
        for k = 0, count(af, 'AIRFIELD_HANGARS')-1 do
            if gi(af, 'AIRFIELD_HANGARS', k) == vid then si(af, 'AIRFIELD_HANGARS', 0, k) end
        end
    end
    local function slot(af, vid)
        local k = list_find(af, 'AIRFIELD_HANGARS', vid)
        if k < 0 then k = list_find(af, 'AIRFIELD_HANGARS', 0); if k >= 0 then si(af, 'AIRFIELD_HANGARS', vid, k) end end
        return k
    end
    local function cleanup(i)
        for a = 0, count(i, 'ISLAND_AIRFIELDS')-1 do
            local af = ref(i, 'ISLAND_AIRFIELDS', a)
            for k = 0, count(af, 'AIRFIELD_HANGARS')-1 do
                local vid = gi(af, 'AIRFIELD_HANGARS', k)
                local v = vid ~= 0 and vehicle(vid)
                if vid ~= 0 and (not v or dock(v) == field('DOCK_UNDOCKED')) then si(af, 'AIRFIELD_HANGARS', 0, k) end
            end
        end
    end
    local function move(wp)
        if wp and gi(wp, 'WAYPOINT_TYPE') == field('WAYPOINT_BARGE_LOAD_TILE') then
            si(wp, 'WAYPOINT_TYPE', field('WAYPOINT_MOVE')); si(wp, 'WAYPOINT_PARAM', 0)
        end
    end
    local function do_land(v, i, explicit_publish)
        cleanup(i)
        if not i:request_landing(v) then return false end
        assert(v:list_clear(field('VEHICLE_WAYPOINTS')))
        if explicit_publish then assert(v:waypoints_sync()) end
        return true
    end
    local function land(v, tid, explicit_publish)
        local vid = id(v); pending[vid] = nil
        if not air(v) or (wing(v) and not settings.planes) or dock(v) ~= field('DOCK_UNDOCKED') or
           gi(v, 'VEHICLE_ATTACHED_TO') ~= 0 or gi(v, 'VEHICLE_DOCK_VEHICLE') ~= 0 then return 0 end
        local t = tile(tid)
        if not t or gi(t, 'TILE_TEAM') ~= gi(v, 'VEHICLE_TEAM') then return 0 end
        local i = island(t)
        if not i then
            if #sorted_keys(pending) >= 32 then return 0 end
            pending[vid] = tid; return 2
        end
        if count(i, 'ISLAND_AIRFIELDS') == 0 or not do_land(v, i, explicit_publish) then return 0 end
        return 1
    end
    local function attachment_flags(v, flying)
        for k = 0, count(v, 'VEHICLE_ATTACHMENTS')-1 do si(v, 'VEHICLE_ATTACHMENT_FLYING', flying, k) end
    end
    local function flying(v) si(v, 'VEHICLE_FLIGHT_STATE', field('FLIGHT_FLYING')); attachment_flags(v, 1) end
    local function ground(v) si(v, 'VEHICLE_FLIGHT_STATE', field('FLIGHT_GROUND')); attachment_flags(v, 0) end
    local function undock(v, state)
        si(v, 'VEHICLE_DOCK_STATE', state); si(v, 'VEHICLE_DOCK_VEHICLE', 0)
        si(v, 'VEHICLE_DOCK_TILE', 0); si(v, 'VEHICLE_DOCK_AIRFIELD', -1)
    end
    local function lock_take(f)
        local k = key(f.tid, f.index)
        if locks[k] then return locks[k] == f.vid end
        if #sorted_keys(locks) >= 16 then return false end
        locks[k] = f.vid; return true
    end
    local function lock_free(f) local k = key(f.tid, f.index); if locks[k] == f.vid then locks[k] = nil end end
    local function finish(f) lock_free(f); flights[f.vid] = nil end
    local function phase(f, name)
        if f.phase ~= name then print(('vehicle %d: %s -> %s (tile %d)'):format(f.vid, f.phase, name, f.tid)) end
        f.phase = name
    end
    local function new_flight(v, tid, index, bay, takeoff, name)
        if not flights[id(v)] and #sorted_keys(flights) >= 32 then return nil end
        local f = {vid=id(v), team=gi(v,'VEHICLE_TEAM'), tid=tid, index=index, slot=bay, takeoff=takeoff, phase=name,
                   start=now, yaw=nil, speed=0, vz=0, roll=0, hv={0,0}, side=0, gear=false,
                   stand=1, lined=false, go_arounds=0, min_clear=1e9}
        flights[f.vid] = f; return f
    end
    local function body(v)
        local b = v:body(); if not b then return nil end
        return {w=b.pos, vel=b.vel, vs=sqrt(b.vel[1]^2+b.vel[2]^2+b.vel[3]^2)}
    end
    local function assist(v, velocity, yaw, pitch, roll)
        assert(v:body_set({vel=velocity, spin={0,0,0}, rot=quat(yaw,pitch,roll)},
                         field('BODY_VEL') | field('BODY_SPIN') | field('BODY_ROT')))
    end
    local function put_at(v, w, yaw)
        assert(v:body_set({pos=w, vel={0,0,0}, spin={0,0,0}, rot=quat(yaw,0,0)}, field('BODY_ALL')))
        for k = 1, 3 do sf(v, 'VEHICLE_CONTROL_TARGET', w[k], k-1) end
    end
    local function main_gear(v)
        for k = 1, min(count(v, 'VEHICLE_WHEELS')-1, 7) do if gf(v, 'VEHICLE_WHEEL_CONTACT', k) > .01 then return true end end
        return false
    end
    local function probe(x, y, z)
        local hit = W.raycast({x,y,z}, {0,-1,0}, 4000, field('RAY_STATIC'))
        return hit and max(hit.pos[2],0) or 0, hit ~= nil
    end
    local function ground_height(x, z) return probe(x,3000,z) end
    local function runway(af, reverse)
        local a, b = {}, {}
        for k = 0, 2 do a[k+1] = gf(af,'AIRFIELD_RUNWAY_START',k); b[k+1] = gf(af,'AIRFIELD_RUNWAY_END',k) end
        if reverse then a,b = b,a end
        local dx,dz = b[1]-a[1],b[3]-a[3]; local len = length(dx,dz)
        if len < 1 then return nil end
        return {thr=a, ax=dx/len, az=dz/len, len=len}
    end
    local function frame(r, w)
        local dx,dz = w[1]-r.thr[1],w[3]-r.thr[3]
        return -(dx*r.ax+dz*r.az), -dx*r.az+dz*r.ax
    end
    local function point(r,s,lat) return {r.thr[1]-s*r.ax-lat*r.az,r.thr[2],r.thr[3]-s*r.az+lat*r.ax} end
    local function plan(af, f)
        local k = key(f.tid,f.index)
        if plans[k] then return plans[k] end
        local result = {reverse=false, treverse=false, slope=.25, h0=20, cruise=250}
        local r = {runway(af,false),runway(af,true)}
        if not r[1] or not r[2] then return result end
        local ry = r[1].thr[2]; local g,hit = ground_height(r[1].thr[1],r[1].thr[3])
        if not hit or abs(g-ry) > 15 then return result end
        local ox,oz,top = gf(af,'AIRFIELD_TRANSFORM',9),gf(af,'AIRFIELD_TRANSFORM',11),0
        for dx = -3500,3500,125 do for dz = -3500,3500,125 do
            if dx*dx+dz*dz <= 3500^2 then top = max(top,ground_height(ox+dx,oz+dz)-ry) end
        end end
        local req,out = {0,0},{-1e9,-1e9}
        for rev = 1,2 do for d = 50,4000,25 do for lat = -80,80,80 do
            local p = point(r[rev],d,lat)
            req[rev] = max(req[rev],(ground_height(p[1],p[3])-ry+min(40,8+.15*d))/(d+60))
            p = point(r[rev],-r[rev].len-d,lat)
            out[rev] = max(out[rev],ground_height(p[1],p[3])-ry+40-.2*(d+r[rev].len*.5))
        end end end
        result.reverse, result.treverse = req[2]<req[1], out[2]<out[1]
        result.slope = clamp(min(req[1],req[2]),.12,.40); result.h0 = result.slope*60; result.cruise=max(250,top+100)
        if #plan_order >= 16 then plans[table.remove(plan_order,1)] = nil end
        plans[k] = result; plan_order[#plan_order+1] = k; return result
    end
    local function pad(af, f) return af:to_world({(f.slot&1)~=0 and 12 or -12,0,-70-((f.slot&2)~=0 and 25 or 0)}) end
    local function distance(af,v)
        return length(gf(v,'VEHICLE_POS',0)-gf(af,'AIRFIELD_TRANSFORM',9),gf(v,'VEHICLE_POS',2)-gf(af,'AIRFIELD_TRANSFORM',11))
    end
    local function park(f,af,v,stand)
        local spot,yaw = assert(af:bay(f.slot&3)); local floor,hit = probe(spot[1],spot[2]+5,spot[3])
        if not hit or abs(floor-spot[2])>4 then floor=spot[2] end
        spot[2]=max(spot[2],floor)+clamp(stand,.3,4)+(wing(v) and 1.5 or 1)
        put_at(v,spot,yaw); v:brakes(1,1); sf(v,'VEHICLE_CONTROL_SPEED',0);sf(v,'VEHICLE_CONTROL_POWER',0)
        si(v,'VEHICLE_CONTROL_TAXI',1);si(v,'VEHICLE_DOCK_STATE',field('DOCK_DOCKED'));si(v,'VEHICLE_FLIGHT_STATE',field('FLIGHT_PARKED'));attachment_flags(v,0)
        publish('dock',f.vid,field('DOCK_DOCKED'));print(('vehicle %d: LANDED (%s) tile %d hangar %d'):format(f.vid,wing(v) and 'plane' or 'helicopter',f.tid,f.slot))
    end
    local function hand_back(f,af,v)
        undock(v,field('DOCK_UNDOCKED'));flying(v);si(v,'VEHICLE_CONTROL_MODE',0);si(v,'VEHICLE_CONTROL_TAXI',0);sf(v,'VEHICLE_CONTROL_POWER',1)
        if wing(v) then v:gear(false) end
        release(af,f.vid)
    end
    local function steer(f,a,t,rate) return clamp(wrap(math.atan(t[1]-a.w[1],t[3]-a.w[3])-f.yaw)*.8,-rate,rate) end
    local function plane_fly(f,v,a,dt,rate,speed,alt,path,sink,pitch)
        f.yaw=wrap(f.yaw+rate*dt);f.speed=f.speed+clamp(speed-f.speed,-5*dt,4*dt)
        local vz=clamp(path+(alt-a.w[2])*.4,-sink,12);f.vz=f.vz+clamp(vz-f.vz,-8*dt,8*dt)
        local hs=sqrt(max(f.speed^2-f.vz^2,100));local roll=-math.atan(rate*f.speed/9.81)
        f.roll=f.roll+clamp(roll-f.roll,-.8*dt,.8*dt)
        assist(v,{sin(f.yaw)*hs,f.vz,cos(f.yaw)*hs},f.yaw,math.atan(f.vz,hs)+pitch,f.roll)
    end
    local function floor_height(a,t,ahead,margin)
        return max((ground_height(a.w[1],a.w[3])),(ground_height(a.w[1]+a.vel[1]*ahead,a.w[3]+a.vel[3]*ahead)),(ground_height(t[1],t[3])))+margin
    end
    local function target(v,t,alt,speed)
        sf(v,'VEHICLE_CONTROL_TARGET',t[1],0);sf(v,'VEHICLE_CONTROL_TARGET',alt,1);sf(v,'VEHICLE_CONTROL_TARGET',t[3],2)
        sf(v,'VEHICLE_CONTROL_SPEED',speed);sf(v,'VEHICLE_CONTROL_POWER',1);si(v,'VEHICLE_CONTROL_MODE',3);si(v,'VEHICLE_CONTROL_TAXI',0)
    end
    local function plane_land(f,af,v,a,dt)
        local pl=plan(af,f);local r=runway(af,pl.reverse);if not r then return end
        local s,lat=frame(r,a.w);local ry=r.thr[2];local h=a.w[2]-ry;local yl=math.atan(r.ax,r.az)
        local fin=(pl.cruise-pl.h0)/pl.slope+600;local width=2*75/.14
        local rate,speed,alt,sink,path=0,75,ry+pl.cruise,15,0;local t=point(r,s,0)
        if f.phase=='hold' then
            if lock_take(f) then phase(f,'inbound');return end
            local c=point(r,fin*.5,f.side*(width+1500));local ang=math.atan(a.w[1]-c[1],a.w[3]-c[3])+.6
            t={c[1]+sin(ang)*700,c[2],c[3]+cos(ang)*700};rate=steer(f,a,t,.14)
        elseif f.phase=='inbound' then
            if not lock_take(f) then f.side=lat>=0 and 1 or -1;phase(f,'hold');return end
            if s>.6*fin and abs(lat)<.3*s and abs(wrap(yl-f.yaw))<.8 then phase(f,'final');return end
            if f.side==0 then f.side=lat>=0 and 1 or -1 end
            if s>-400 and s<fin-300 and abs(lat-f.side*width)<350 and abs(wrap(yl+pi-f.yaw))<.6 then phase(f,'downwind');return end
            -- Join the outer approach circuit first. The old near-field
            -- target beyond the threshold made oblique arrivals head toward
            -- the island before flying back out to downwind/base/final.
            t=point(r,fin+600,f.side*width);rate=steer(f,a,t,.14)
            if length(t[1]-a.w[1],t[3]-a.w[3])<300 then phase(f,'downwind') end
        elseif f.phase=='downwind' then
            t=point(r,min(s+800,fin+600),f.side*width);rate=steer(f,a,t,.14);if s>=fin then phase(f,'base') end
        elseif f.phase=='base' then
            rate=-f.side*.14;speed=70;if abs(wrap(yl-f.yaw))<.3 then phase(f,'final') end
        elseif f.phase=='final' then
            local glide=clamp(pl.slope*(s+60),0,pl.cruise)
            if s<-.85*r.len or (s<300 and abs(lat)>60) or (s<800 and h>glide+80) or (s<1500 and abs(wrap(yl-f.yaw))>1.2) then
                f.go_arounds=f.go_arounds+1
                if f.go_arounds>=3 then hand_back(f,af,v);finish(f);return end
                lock_free(f);f.side=lat>=0 and 1 or -1;phase(f,'inbound');return
            end
            t=point(r,s-max(250,2.5*abs(lat)),0);rate=steer(f,a,t,.14);alt=ry+glide;speed=s<2000 and 62 or 75;sink=max(25,pl.slope*62+6)
            if glide>0 and glide<pl.cruise then path=-pl.slope*f.speed end
            if s<2500 and not f.gear then v:gear(true);f.gear=true end
            if s<150 and (main_gear(v) or h<2.5 or (s<0 and h<8)) then
                si(v,'VEHICLE_DOCK_STATE',field('DOCK_TAXI'));ground(v);v:gear(true);f.speed=length(a.vel[1],a.vel[3]);phase(f,'rollout');return
            end
        elseif f.phase=='rollout' then
            local dec=clamp(f.speed^2/(2*max(r.len+s-30,15)),4,20)
            f.speed=max(f.speed-dec*dt,0);f.yaw=wrap(f.yaw+clamp(wrap(yl-f.yaw),-.3*dt,.3*dt))
            local side_v=clamp(-lat*.5,-3,3);local vy=main_gear(v) and min(a.vel[2],0) or -3
            assist(v,{sin(f.yaw)*f.speed-r.az*side_v,vy,cos(f.yaw)*f.speed+r.ax*side_v},f.yaw,0,0);target(v,t,ry,0)
            if f.speed<2 then park(f,af,v,h);finish(f) end
            return
        end
        if not (f.phase=='final' and s<600) then
            alt=max(alt,floor_height(a,t,5,40));f.min_clear=min(f.min_clear,a.w[2]-ground_height(a.w[1],a.w[3]))
        end
        plane_fly(f,v,a,dt,rate,speed,alt,path,sink,0);target(v,t,alt,80)
    end
    local function plane_takeoff(f,af,v,a,dt)
        local pl=plan(af,f);local r=runway(af,pl.treverse);if not r then return end
        local s,lat=frame(r,a.w);local ry,yl=r.thr[2],math.atan(r.ax,r.az);local h=a.w[2]-ry
        if f.phase=='wait' then
            if not lock_take(f) then return end
            local t=point(r,-25,0);t[2]=ry+clamp(f.stand,.3,4);put_at(v,t,yl)
            si(v,'VEHICLE_DOCK_STATE',field('DOCK_UNDOCKING'));ground(v);v:gear(true);v:brakes(0,1)
            f.yaw,f.speed,f.vz,f.roll=yl,0,0,0;phase(f,'roll');return
        end
        local t=point(r,-(r.len+3000),0)
        if f.phase=='roll' then
            f.speed=f.speed+9*dt;f.yaw=yl;local side_v=clamp(-lat*.5,-3,3)
            assist(v,{sin(yl)*f.speed-r.az*side_v,clamp(a.vel[2],-5,1),cos(yl)*f.speed+r.ax*side_v},yl,0,0);target(v,t,ry+200,80)
            if f.speed>=45 then flying(v);f.vz=0;phase(f,'climb') end
            return
        end
        local alt=max(ry+250,floor_height(a,t,6,40));plane_fly(f,v,a,dt,steer(f,a,t,.14),75,alt,0,15,0);target(v,t,alt,80)
        if h>20 and not f.gear then v:gear(false);f.gear=true end
        if h>120 and s<-(r.len+300) then hand_back(f,af,v);finish(f) end
    end
    local function heli_land(f,af,v,a,dt)
        local p=assert(pad(af,f));local dx,dz=p[1]-a.w[1],p[3]-a.w[3];local d=length(dx,dz);local h=a.w[2]-p[2]
        local nv,pitch,alt={},0,0
        if f.phase=='heli_inbound' then
            alt=clamp(15+.25*d,15,100);if d>60 then alt=max(alt,floor_height(a,p,4,30)-p[2]) end
            local want=min(45,sqrt(4*max(d-1,0)));local excess=h-alt
            if excess>20 then want=min(want,max(6,45*(1-(excess-20)/120))) end
            local ux,uz=d>.5 and dx/d or 0,d>.5 and dz/d or 0;local ex,ez=ux*want-f.hv[1],uz*want-f.hv[2];local e=length(ex,ez);local lim=4*dt
            if e>lim then ex,ez=ex*lim/e,ez*lim/e end
            f.hv[1],f.hv[2]=f.hv[1]+ex,f.hv[2]+ez;f.speed=length(f.hv[1],f.hv[2]);nv={f.hv[1],clamp((alt-h)*.5,-10,5),f.hv[2]}
            if f.speed>5 then f.yaw=wrap(f.yaw+clamp(wrap(math.atan(f.hv[1],f.hv[2])-f.yaw),-.7*dt,.7*dt)) end
            pitch=-.12*f.speed/45;if d<3 and f.speed<1.5 then f.descent=now;phase(f,'heli_descend') end
        else
            local k=d>.1 and min(.5*d,2)/d or 0;nv={dx*k,h>12 and -4 or h>5 and -2 or -1,dz*k}
            if main_gear(v) or (h<3 and a.vel[2]>-.25 and now-f.descent>45) then park(f,af,v,h);finish(f);return end
        end
        assist(v,nv,f.yaw,pitch,0);target(v,p,p[2]+alt,max(f.speed,10))
    end
    local function heli_takeoff(f,af,v,a)
        local p=assert(pad(af,f))
        if not f.lined then
            local _,yaw=af:bay(f.slot&3);put_at(v,{p[1],p[2]+clamp(f.stand,.3,4),p[3]},yaw)
            si(v,'VEHICLE_DOCK_STATE',field('DOCK_UNDOCKING'));flying(v);f.yaw=yaw;f.lined=true;return
        end
        local h=a.w[2]-p[2];assist(v,{(p[1]-a.w[1])*.3,4,(p[3]-a.w[3])*.3},f.yaw,0,0);target(v,p,p[2]+50,10)
        if h>40 then hand_back(f,af,v);finish(f) end
    end
    local function start_landing(af,v,tid,index,bay)
        local f=new_flight(v,tid,index,bay,false,wing(v) and 'inbound' or 'heli_inbound');if not f then return nil end
        undock(v,field('DOCK_DOCKING'));flying(v);return f
    end
    local function ours(v) return v and human(v) and air(v) and (not wing(v) or (settings.planes and settings.plane_assist)) end
    local function take_from_queue(af,tid,index)
        local k=0
        while k<count(af,'AIRFIELD_DOCK_QUEUE') do
            local vid=gi(af,'AIRFIELD_DOCK_QUEUE',k);local v=vehicle(vid)
            local mine=ours(v) and gi(v,'VEHICLE_TEAM')==gi(tile(tid),'TILE_TEAM')
            if mine then sf(v,'VEHICLE_CONTROL_POWER',1) end
            local bay=mine and distance(af,v)<(wing(v) and 6000 or 4000) and slot(af,vid) or -1
            if bay>=0 and start_landing(af,v,tid,index,bay) then af:list_remove(field('AIRFIELD_DOCK_QUEUE'),k) else k=k+1 end
        end
    end
    local function take_from_runway(af,tid,index)
        if gi(af,'AIRFIELD_STATE')==9 then si(af,'AIRFIELD_RUNWAY_VEHICLE',-1);si(af,'AIRFIELD_STATE',0) end
        local vid=gi(af,'AIRFIELD_RUNWAY_VEHICLE');if gi(af,'AIRFIELD_STATE')~=3 or vid<0 then return end
        local v=vehicle(vid);if not ours(v) or gi(v,'VEHICLE_TEAM')~=gi(tile(tid),'TILE_TEAM') then return end
        local bay=slot(af,vid);if bay>=0 and start_landing(af,v,tid,index,bay) then si(af,'AIRFIELD_RUNWAY_VEHICLE',-1);si(af,'AIRFIELD_STATE',0) end
    end
    local function adopt(af,tid,index)
        for _, record in ipairs(W.vehicles() or {}) do
            local v=vehicle(record.id)
            if ours(v) and gi(v,'VEHICLE_TEAM')==gi(tile(tid),'TILE_TEAM') and gi(v,'VEHICLE_ATTACHED_TO')==0 and gi(v,'VEHICLE_DOCK_VEHICLE')==0 and
               (dock(v)==field('DOCK_DOCKING') or dock(v)==field('DOCK_TAXI')) and not flights[id(v)] and distance(af,v)<=6000 then
                local old=dock(v);local bay=slot(af,id(v))
                if bay>=0 then local f=start_landing(af,v,tid,index,bay);if f and old==field('DOCK_TAXI') then park(f,af,v,1);finish(f) end end
            end
        end
    end
    local function find_parked(v)
        local t=W.tile_at(gf(v,'VEHICLE_POS',0),gf(v,'VEHICLE_POS',2));local i=island(t)
        if not i then return nil end
        for index=0,count(i,'ISLAND_AIRFIELDS')-1 do
            local af=ref(i,'ISLAND_AIRFIELDS',index)
            if list_find(af,'AIRFIELD_HANGARS',id(v))>=0 then return af,gi(t,'TILE_ID'),index end
        end
        if count(i,'ISLAND_AIRFIELDS')>0 then return ref(i,'ISLAND_AIRFIELDS',0),gi(t,'TILE_ID'),0 end
    end
    local function takeoff(v,wp)
        if flights[id(v)] then return end
        local af,tid,index=find_parked(v);if not af then return end
        local bay=slot(af,id(v));if bay<0 then return end
        if wing(v) and not settings.plane_assist then
            if gi(af,'AIRFIELD_RUNWAY_VEHICLE')==id(v) or list_find(af,'AIRFIELD_UNDOCK_QUEUE',id(v))>=0 or list_find(af,'AIRFIELD_DOCK_QUEUE',id(v))>=0 then return end
            if not af:list_push(field('AIRFIELD_UNDOCK_QUEUE'),id(v)) then return end
            undock(v,field('DOCK_PENDING_UNDOCK'));move(wp);return
        end
        local f=new_flight(v,tid,index,bay,true,wing(v) and 'wait' or 'heli_lift');if not f then return end
        local spot=assert(af:bay(bay&3));f.stand=gf(v,'VEHICLE_POS',1)-spot[2]
        undock(v,field('DOCK_PENDING_UNDOCK'));sf(v,'VEHICLE_CONTROL_POWER',1);move(wp)
    end
    local function cancel(v)
        local f=flights[id(v)]
        if f and not f.takeoff and f.phase~='rollout' then
            local af=airfield(f.tid,f.index);if af then hand_back(f,af,v) end;finish(f);return
        end
        if dock(v)~=field('DOCK_DOCKING') and dock(v)~=field('DOCK_QUEUED') and dock(v)~=field('DOCK_TAXI') then return end
        if gi(v,'VEHICLE_DOCK_VEHICLE')~=0 or gi(v,'VEHICLE_ATTACHED_TO')~=0 then return end
        for _, t in ipairs(W.tiles() or {}) do local i=island(tile(t.id));if i then for index=0,count(i,'ISLAND_AIRFIELDS')-1 do
            local af=ref(i,'ISLAND_AIRFIELDS',index);local found=false
            if gi(af,'AIRFIELD_RUNWAY_VEHICLE')==id(v) then si(af,'AIRFIELD_RUNWAY_VEHICLE',-1);si(af,'AIRFIELD_STATE',0);si(af,'AIRFIELD_MODE',0);af:list_clear(field('AIRFIELD_TAXI_PATH'));found=true end
            local k=list_find(af,'AIRFIELD_DOCK_QUEUE',id(v));if k>=0 then af:list_remove(field('AIRFIELD_DOCK_QUEUE'),k);found=true end
            if found then release(af,id(v));undock(v,field('DOCK_UNDOCKED'));si(v,'VEHICLE_FLIGHT_STATE',field('FLIGHT_PARKED'));si(v,'VEHICLE_CONTROL_MODE',0);si(v,'VEHICLE_CONTROL_TAXI',0);return end
        end end end
    end
    function P.waypoints(vid)
        if not sync_session() then return end
        local v=vehicle(vid);if not v or not air(v) or not human(v) then return end
        pending[vid]=nil
        local wp=ref(v,'VEHICLE_WAYPOINTS',0);if not wp then return end
        local origin=cc2.orders.current()
        if not origin or origin.vehicle_id~=vid or not origin.broadcast_pending then
            move(wp);publish('request_result',vid,0);return
        end
        if dock(v)==field('DOCK_DOCKED') then if gi(v,'VEHICLE_ATTACHED_TO')==0 then takeoff(v,wp) end;return end
        if gi(wp,'WAYPOINT_TYPE')~=field('WAYPOINT_BARGE_LOAD_TILE') then cancel(v);return end
        local tid=gi(wp,'WAYPOINT_PARAM');local result=land(v,tid,false);publish('request_result',vid,result)
        print(('vehicle %d: landing request tile %d peer %d result %d'):format(vid,tid,origin.peer_id,result))
        if result~=1 then move(wp) end
    end
    function P.hangar(vid,tid,index)
        if not sync_session() then return -1 end
        local v,af=vehicle(vid),airfield(tid,index)
        return v and human(v) and af and gi(v,'VEHICLE_TEAM')==gi(tile(tid),'TILE_TEAM') and slot(af,vid) or -1
    end
    function P.airfield(tid,index)
        if not sync_session() then return end
        local af=airfield(tid,index);if not af then return end
        take_from_runway(af,tid,index);take_from_queue(af,tid,index);if now%32==5 then adopt(af,tid,index) end
        for _,vid in ipairs(sorted_keys(flights)) do local f=flights[vid];if f.tid==tid and f.index==index then
            local v=vehicle(vid)
            if not v then release(af,vid);finish(f)
            elseif not human(v) or gi(v,'VEHICLE_TEAM')~=f.team or gi(tile(tid),'TILE_TEAM')~=f.team then
                if human(v) and gi(v,'VEHICLE_TEAM')==f.team then hand_back(f,af,v) else release(af,vid) end
                finish(f)
            elseif not f.takeoff and dock(v)~=field('DOCK_DOCKING') and dock(v)~=field('DOCK_TAXI') then finish(f)
            elseif f.last_tick~=now then
                local a=body(v)
                if a then
                    local dt=f.last_tick and clamp((now-f.last_tick)/30,0,.1) or 1/30;f.last_tick=now
                    if not f.yaw then local vh=length(a.vel[1],a.vel[3]);f.yaw=vh>5 and math.atan(a.vel[1],a.vel[3]) or 0;f.speed=f.takeoff and 0 or vh;f.vz=a.vel[2];f.hv={a.vel[1],a.vel[3]} end
                    if wing(v) then if f.takeoff then plane_takeoff(f,af,v,a,dt) else plane_land(f,af,v,a,dt) end
                    else if f.takeoff then heli_takeoff(f,af,v,a) else heli_land(f,af,v,a,dt) end end
                end
            end
        end end
    end
    function P.tick()
        if not sync_session() then return end
        if now%8==0 then for _,vid in ipairs(sorted_keys(pending)) do
            local v,t=vehicle(vid),tile(pending[vid])
            if not v or not human(v) or not air(v) or dock(v)~=field('DOCK_UNDOCKED') or not t or gi(t,'TILE_TEAM')~=gi(v,'VEHICLE_TEAM') then pending[vid]=nil
            else local i=island(t);if i then
                if count(i,'ISLAND_AIRFIELDS')==0 then pending[vid]=nil;publish('request_result',vid,0)
                elseif do_land(v,i,true) then pending[vid]=nil;publish('request_result',vid,1) end
            end end
        end end
        if now%64==1 then for _,t in ipairs(W.tiles() or {}) do if t.airfields_known then publish('count',t.id,t.airfields) end end end
    end
    function P.unload()
        for _,vid in ipairs(sorted_keys(flights)) do local f=flights[vid];local v,af=vehicle(vid),airfield(f.tid,f.index);if v and af then hand_back(f,af,v) end end
        pending,flights,locks,plans,plan_order={},{},{},{},{};publish('active',0,0)
    end
    return P
end
return M
