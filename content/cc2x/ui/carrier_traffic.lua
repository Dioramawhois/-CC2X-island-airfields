-- SPDX-License-Identifier: GPL-3.0-or-later
-- Flight-deck indications belong to the displayed carrier, not the whole team.
-- Use the same parent distinctions as the installed stock propulsion screen.
function mm_carrier_flight_deck_state(carrier, vehicle)
    local states = g_flight_deck_states
    local carrier_id = carrier:get_id()
    if carrier_id <= 0 then return states.none end
    local dock = vehicle:get_dock_state()
    if dock == e_vehicle_dock_state.undocking or dock == e_vehicle_dock_state.undock_holding then
        if vehicle:get_attached_parent_id() ~= carrier_id then return states.none end
        return dock == e_vehicle_dock_state.undocking and states.launch or states.holding
    end
    if dock == e_vehicle_dock_state.docking or dock == e_vehicle_dock_state.docking_taxi or
       dock == e_vehicle_dock_state.dock_queue then
        if vehicle:get_dock_parent_id() ~= carrier_id then return states.none end
        if dock == e_vehicle_dock_state.docking then return states.landing end
        return dock == e_vehicle_dock_state.docking_taxi and states.taxi or states.queued
    end
    return states.none
end
