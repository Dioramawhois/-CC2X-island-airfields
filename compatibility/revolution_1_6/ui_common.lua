-- SPDX-License-Identifier: GPL-3.0-or-later
-- Island Airfields screen helpers; gameplay remains in the independent module.
-- [my_mod] Best-effort airbase anchor for an island: command centre if present, else island centre.
-- (Lua has no runway position, so the command centre is used as the anchor of the marker.)
function mm_island_pos(island)
    if island:get_command_center_count() > 0 then
        local p = island:get_command_center_position(0)
        return p:x(), p:y()
    end

    local p = island:get_position_xz()
    return p:x(), p:y()
end

-- Bridge to the separate Island Airfields Lua module through the shared SDK.
-- Without it, keep ordinary waypoint behavior and show the runtime notice.
MM_LANDING_RADIUS = 3000.0

function mm_plugin()
    local modules = cc2x and cc2x.modules
    if type(modules) == "table" and type(modules.read) == "function" and
       modules.read("island_airfields", "active", 0) == 1 then
        return {
            count = function(id) return modules.read("island_airfields", "count", id) end,
            planes = function() return modules.read("island_airfields", "planes", 0) == 1 end,
        }
    end
    return cc2x and cc2x.island_airfields
end

-- Orders travel through the vanilla multiplayer waypoint channel. The host alone
-- accepts the landing and changes the aircraft, including fixed-wing policy.
function mm_remote_host()
    if type(update_get_is_multiplayer) ~= "function" then return false end
    local multiplayer, host = update_get_is_multiplayer()
    return multiplayer == true and host == false
end

-- [my_mod] Without CC2X the mod can only show markers. Say what is missing, on the map itself.
function mm_draw_cc2x_notice(screen_w, screen_h)
    if mm_plugin() ~= nil or mm_remote_host() then return end
    local line = "Island Airfields requires CC2X SDK 0.8: copy dinput8.dll beside carrier_command.exe"
    if cc2x ~= nil then line = "Island Airfields module inactive: check the SDK version and cc2x.log" end
    update_ui_rectangle(0, screen_h - 13, screen_w, 13, color8(0, 0, 0, 220))
    update_ui_text(4, screen_h - 11, line, screen_w - 8, 0, color8(255, 190, 0, 255), 0)
end

-- airfield count (>= 0), -1 = plugin active but not known yet, nil = plugin not active
function mm_airfield_count(island)
    local p = mm_plugin()
    if p == nil and not mm_remote_host() then return nil end
    local n = p and p.count(island:get_id()) or -1
    if n ~= nil and n >= 0 then return n end

    -- The supported game's generator requests one runway at difficulty 2-4,
    -- none at difficulty 1. Stock map metadata exposes that level before loading
    -- the island. Our own infrastructure needs no reconnaissance flight.
    -- See docs/OWNED_ISLAND_KNOWLEDGE.md for the checked engine rule.
    if island:get_team_control() == update_get_screen_team_id() and
       type(island.get_difficulty_level) == "function" then
        local level = island:get_difficulty_level()
        if type(level) == "number" and level >= 1 and level <= 4 then
            return level >= 2 and 1 or 0
        end
    end
    return -1
end

-- true when the plugin answers for this island (landing orders will be honoured)
function mm_is_native(island)
    return mm_plugin() ~= nil or mm_remote_host()
end

-- can this aircraft land on an island airfield? (fixed-wing only with experimental_planes=1)
function mm_can_land(vehicle)
    local p = mm_plugin()
    if p == nil and not mm_remote_host() then return false end
    local def = vehicle:get_definition_index()
    if def == e_game_object_type.chassis_air_wing_light or def == e_game_object_type.chassis_air_wing_heavy then
        return mm_remote_host() or p.planes()
    end
    return get_is_vehicle_air(def)
end

-- Should the airfield marker be drawn on this (own) island?
-- Hide a confirmed absence of runways; keep unqueried owned islands as candidates.
function mm_is_marked_airfield(island)
    local n = mm_airfield_count(island)

    if n == nil or n < 0 then return true end
    return n > 0
end

-- Marker colour shows island ownership, including islands not loaded yet.
-- A coloured marker is not confirmation of runway data or remote host support.

-- [my_mod] Self-test hook for native/tools (plugin test channel). Does nothing unless a tool hands
-- out a command; then it gives the same orders the map does.
function mm_self_test_tick()
    local p = mm_plugin()
    if p == nil or type(p.test_poll) ~= "function" then return end
    local cmd, vehicle_id, tile_id, x, y = p.test_poll()
    if cmd == nil then return end

    local vehicle = update_get_map_vehicle_by_id(vehicle_id)
    if vehicle:get() == false then return end

    vehicle:clear_waypoints()
    vehicle:clear_attack_target()
    local waypoint_id = vehicle:add_waypoint(x, y)

    if cmd == 2 then
        vehicle:set_waypoint_type_barge_load_tile(waypoint_id, tile_id)
        g_mm_landing_orders[vehicle_id] = tile_id
    elseif cmd == 3 then
        vehicle:set_target_vehicle(waypoint_id, tile_id)    -- tile_id carries the carrier id here
    end
end
-- [my_mod] Nearest own island with an airfield within max_dist of a world point.
-- Returns island, anchor x, anchor y (or nil).
function mm_find_landing_island(world_x, world_y, max_dist)
    local screen_team = update_get_screen_team_id()
    local count = update_get_tile_count()
    local best, best_x, best_y, best_d = nil, 0, 0, max_dist

    for i = 0, count - 1 do
        local island = update_get_tile_by_index(i)

        if island:get() and island:get_team_control() == screen_team and mm_is_marked_airfield(island) then
            local px, py = mm_island_pos(island)
            local dx = px - world_x
            local dy = py - world_y
            local d = math.sqrt(dx * dx + dy * dy)

            if d < best_d then
                best_d = d
                best = island
                best_x = px
                best_y = py
            end
        end
    end

    return best, best_x, best_y
end

-- [my_mod] vehicle id -> island id of the last landing order given from this screen
g_mm_landing_orders = {}

-- [my_mod] Drop-in replacement for vehicle:add_waypoint(x, y) used by the map controls.
-- A flying aircraft with no other waypoints sent near an own airfield gets a landing order: the
-- waypoint is typed barge_load_tile with the island id, which the native patch turns into a real
-- landing (queue -> approach -> runway -> hangar, then repair/refuel/rearm). Anything else, or no
-- native patch, is an ordinary waypoint.
function mm_add_waypoint(vehicle, world_x, world_y)
    -- Revolution may pass a cached VProxy from a group selector. Use its real
    -- map object for waypoint writes; do not mutate or invalidate its cache.
    if type(vehicle) == "table" and vehicle.extended == true and vehicle.v ~= nil then
        vehicle = vehicle.v
    end
    if vehicle and vehicle:get()
        and mm_can_land(vehicle)
        and vehicle:get_dock_state() == e_vehicle_dock_state.undocked
        and vehicle:get_waypoint_count() == 0 then
        local island, ax, ay = mm_find_landing_island(world_x, world_y, MM_LANDING_RADIUS)

        if island ~= nil and mm_is_native(island) then
            local waypoint_id = vehicle:add_waypoint(ax, ay)
            vehicle:set_waypoint_type_barge_load_tile(waypoint_id, island:get_id())
            g_mm_landing_orders[vehicle:get_id()] = island:get_id()
            return waypoint_id
        end
    end

    return vehicle:add_waypoint(world_x, world_y)
end

-- [my_mod] Aircraft parked in an island hangar (docked but not attached to a carrier).
-- Dragging a waypoint from it makes the native patch launch it from the island runway.
function mm_is_parked_at_airfield(vehicle)
    return get_is_vehicle_air(vehicle:get_definition_index())
        and vehicle:get_dock_state() == e_vehicle_dock_state.docked
        and vehicle:get_attached_parent_id() == 0
end

-- [my_mod] Aircraft landing on an island: queued or on the approach, not for a carrier.
-- Until it is parked a new waypoint cancels the landing.
function mm_is_queued_at_airfield(vehicle)
    local state = vehicle:get_dock_state()
    return get_is_vehicle_air(vehicle:get_definition_index())
        and (state == e_vehicle_dock_state.dock_queue or state == e_vehicle_dock_state.docking)
        and vehicle:get_dock_queue_vehicle_id() == 0
        and vehicle:get_attached_parent_id() == 0
end

-- Called inside Revolution's normal map branch, after its notification and
-- background guards. Reuse its transform, including the HD holomap layout.
function mm_draw_airfields(screen_w, screen_h, holomap)
    local team = update_get_screen_team_id()
    local function project(x, y)
        if holomap then return get_holomap_from_world(x, y, screen_w, screen_h) end
        return get_screen_from_world(x, y, g_camera_pos_x, g_camera_pos_y,
                                    g_camera_size, screen_w, screen_h)
    end
    for i = 0, update_get_tile_count() - 1 do
        local island = update_get_tile_by_index(i)
        if island:get() and island:get_team_control() == team and mm_is_marked_airfield(island) then
            local x, y = mm_island_pos(island)
            local sx, sy = project(x, y)
            local base = update_get_team_color(team)
            update_ui_image(sx - 8, sy - 8, atlas_icons.map_icon_factory_chassis_air,
                            color8(base:r(), base:g(), base:b(), 200), 0)
        end
    end
    if holomap then return end
    for i = 0, update_get_map_vehicle_count() - 1 do
        local vehicle = update_get_map_vehicle_by_index(i)
        if vehicle:get() and vehicle:get_team() == team and get_is_vehicle_air(vehicle:get_definition_index()) then
            local pos = vehicle:get_position_xz()
            local sx, sy = project(pos:x(), pos:y())
            local base = update_get_team_color(team)
            local col = color8(base:r(), base:g(), base:b(), 200)
            if mm_is_parked_at_airfield(vehicle) then
                update_ui_circle(sx, sy, 7, 12, col)
            elseif mm_is_queued_at_airfield(vehicle) then
                local target = g_mm_landing_orders[vehicle:get_id()]
                local island = target and update_get_tile_by_id(target)
                if island == nil or not island:get() then
                    island = mm_find_landing_island(pos:x(), pos:y(), 1000000.0)
                end
                if island and island:get() then
                    local x, y = mm_island_pos(island)
                    local ix, iy = project(x, y)
                    render_dashed_line(sx, sy, ix, iy, col)
                end
            end
        end
    end
end
