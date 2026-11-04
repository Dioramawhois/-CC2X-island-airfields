-- SPDX-License-Identifier: GPL-3.0-or-later
-- TEST ONLY. Appended to the private screen_vehicle_control.lua. Executes stock
-- vehicle waypoint setters on the game's UI thread, then waits for host state.
do
    local original_update,original_input=update,input_event
    local last_sequence,last_error,logged=0,nil,false
    local function deliver()
        if type(update_get_is_focus_local)~='function' or not update_get_is_focus_local() then return end
        if cc2x and cc2x.log and not logged then logged=true;cc2x.log('AIRFIELDS_LIVE focused control UI fixture running') end
        local modules=cc2x and cc2x.modules
        if not modules or not modules.read then return end
        local owner='island_airfields'
        local seq=modules.read(owner,'sequence',0)
        if not seq or seq==0 or seq==last_sequence then return end
        local vid=modules.read(owner,'vehicle',0)
        if not vid or type(update_get_map_vehicle_by_id)~='function' then return end
        local v=update_get_map_vehicle_by_id(vid)
        if not v or not v:get() then return end
        if type(v.clear_waypoints)~='function' or type(v.add_waypoint)~='function' then return end
        local x,z=modules.read(owner,'x',0),modules.read(owner,'z',0)
        local kind,tid=modules.read(owner,'kind',0),modules.read(owner,'tile',0)
        local success,err=pcall(function()
            v:clear_waypoints()
            local waypoint=v:add_waypoint(x,z)
            if kind==4 then v:set_waypoint_type_barge_load_tile(waypoint,tid) end
        end)
        last_sequence=seq
        modules.send(owner,'ui_result',seq,success and 1 or -1)
        if cc2x.log then cc2x.log('AIRFIELDS_LIVE UI_ORDER seq='..seq..' vehicle='..vid..' ok='..tostring(success)..(err and ' '..tostring(err) or '')) end
    end
    function update(...)
        local ok,reason=pcall(original_update,...)
        if not ok and reason~=last_error then last_error=reason;if cc2x and cc2x.log then cc2x.log('AIRFIELDS_LIVE UI_ERROR '..tostring(reason)) end end
        deliver()
    end
    function input_event(...)
        deliver()
        return original_input(...)
    end
end
