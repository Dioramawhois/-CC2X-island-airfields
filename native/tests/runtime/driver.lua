-- SPDX-License-Identifier: GPL-3.0-or-later
-- TEST ONLY. Run in the private game/profile prepared by the SDK sandbox.
-- Uses the actual public SDK; waypoint requests go through stock screen Lua.
local cc2=require('cc2')
local W,F=cc2.world,cc2.world.fields
local stage,started,generation,sequence='init',0,0,0
local actors,actor_index,team,carrier,candidates,target,chosen={},1,nil,nil,{},nil,nil
local last_report,last_probe,teleported,landed=0,0,false,0
local function log(text) print('AIRFIELDS_LIVE '..text) end
local function transition(name)
    stage,started=name,cc2.session.tick();log('stage='..name..' actor='..tostring(actors[actor_index]))
end
local function fail(reason) stage='failed';log('FAIL '..reason);cc2.messages.publish('finished',0,-1) end
local function request(v,kind,tile_id,x,z)
    sequence=sequence+1
    cc2.messages.publish('vehicle',0,v.id);cc2.messages.publish('kind',0,kind)
    cc2.messages.publish('tile',0,tile_id or 0);cc2.messages.publish('x',0,x);cc2.messages.publish('z',0,z)
    cc2.messages.publish('sequence',0,sequence)
    log(('request=%d vehicle=%d type=%d tile=%d'):format(sequence,v.id,kind,tile_id or 0))
end
local function choose_target()
    if chosen then target=chosen;return true end
    target=table.remove(candidates,1)
    return target~=nil
end
local function configure()
    local vehicles=assert(W.vehicles())
    for _,v in ipairs(vehicles) do if v.definition==0 and not v.team_is_ai then carrier=v;team=v.team;break end end
    if not carrier then return false end
    local helicopter,plane
    for _,v in ipairs(vehicles) do if v.is_air and v.team==team and not v.team_is_ai and v.attached_to==carrier.id then
        if v.is_wing and not plane then plane=v.id elseif v.is_rotor and not helicopter then helicopter=v.id end
    end end
    if not helicopter or not plane then return false end
    actors={helicopter,plane};actor_index=1
    local tiles=assert(W.tiles())
    for _,t in ipairs(tiles) do if t.team==team and (not t.airfields_known or t.airfields>0) then candidates[#candidates+1]=t end end
    table.sort(candidates,function(a,b)
        return (a.x-carrier.position.x)^2+(a.z-carrier.position.z)^2 < (b.x-carrier.position.x)^2+(b.z-carrier.position.z)^2
    end)
    log(('configured team=%d carrier=%d helicopter=%d plane=%d candidate_islands=%d'):format(team,carrier.id,helicopter,plane,#candidates))
    return choose_target()
end
return {
    on_load=function()
        assert(cc2.orders and cc2.orders.available(),'Guarded incoming order API required')
        log('test-only driver loaded; focus the vehicle control screen to deliver ordinary game orders')
    end,
    on_update=function()
        local epoch=cc2.session.generation();local tick=cc2.session.tick()
        if epoch==0 then return end
        if generation~=epoch then
            generation=epoch;actors={};actor_index=1;carrier=nil;team=nil;candidates={};target=nil;chosen=nil
            teleported=false;last_report=0;last_probe=0;transition('init')
        end
        if stage=='done' or stage=='failed' then return end
        if tick-last_report>=150 then
            last_report=tick;log('heartbeat stage='..stage..' tick='..tick)
        end
        for _,message in ipairs(cc2.messages.poll() or {}) do
            if message.topic=='ui_result' then
                log('ui_result seq='..message.subject..' value='..message.value)
                if message.subject==sequence then cc2.messages.publish('sequence',0,0) end
                if message.value<0 then fail('stock UI rejected order');return end
            end
        end
        if tick-started>30*300 then fail('timeout in '..stage);return end
        if stage=='init' then
            if tick-last_probe<30 then return end
            last_probe=tick;candidates={}
            if not configure() then return end
            local v=assert(W.vehicle(actors[actor_index]))
            request(v,0,0,carrier.position.x+2000,carrier.position.z+1000);transition('launch');return
        end
        local v=W.vehicle(actors[actor_index])
        if not v or v.hp<=0 then fail('aircraft missing/dead');return end
        local selector=assert(W.find(F.CC2X_WORLD_VEHICLES,v.id))
        if stage=='launch' then
            if v.attached_to~=0 or v.dock_state~=F.CC2X_DOCK_UNDOCKED or not v.has_body or v.position.y<50 then return end
            -- The explicit test position change occurs only after a normal carrier
            -- launch, while airborne. No dock/attachment/ownership field is forged.
            assert(selector:body_set({pos={target.x+400,1000,target.z}},F.CC2X_BODY_POS))
            teleported=true;last_probe=tick;transition('load_island');return
        elseif stage=='load_island' then
            if tick-last_probe<15 then return end
            last_probe=tick
            local t=assert(W.tile(target.id))
            if not t.island_loaded then return end
            if t.airfields==0 or not t.airfield_details or #t.airfield_details==0 then
                log('island has no airfield id='..t.id)
                if not choose_target() then fail('no owned airfield found');return end
                assert(selector:body_set({pos={target.x+400,1000,target.z}},F.CC2X_BODY_POS));return
            end
            chosen=target
            request(v,4,target.id,target.x,target.z);transition('landing');return
        elseif stage=='landing' then
            if v.dock_state==F.CC2X_DOCK_DOCKED and v.attached_to==0 then
                landed=tick;log(('LANDED vehicle=%d tile=%d hp=%.0f fuel=%.1f'):format(v.id,target.id,v.hp,v.fuel));transition('parked')
            end
        elseif stage=='parked' then
            if tick-landed<120 then return end
            if v.dock_state~=F.CC2X_DOCK_DOCKED or v.attached_to~=0 then fail('parking did not remain stable');return end
            request(v,0,0,target.x+3500,target.z);transition('takeoff')
        elseif stage=='takeoff' then
            if v.dock_state~=F.CC2X_DOCK_UNDOCKED or not v.has_body or v.position.y<40 then return end
            log(('TOOK_OFF vehicle=%d hp=%.0f'):format(v.id,v.hp))
            actor_index=actor_index+1
            if actor_index>#actors then
                stage='done';cc2.messages.publish('finished',0,1);log('PASS helicopter and plane actual landing, stable hangar and launch');return
            end
            local next_vehicle=assert(W.vehicle(actors[actor_index]))
            request(next_vehicle,0,0,carrier.position.x+2000,carrier.position.z+1000);transition('launch')
        end
    end,
}
