-- SPDX-License-Identifier: GPL-3.0-or-later
-- TEST ONLY. Appended only in the private copy. Use the same custom-game
-- settings callbacks and script event as the stock New Game GUI.
do
    local original_update=update
    local frames,requested=0,false
    function update(...)
        original_update(...)
        frames=frames+1
        if requested or frames<120 then return end
        requested=true
        update_set_new_game_island_count(64)
        update_set_new_game_team_count_human(1)
        update_set_new_game_team_count_ai(1)
        update_set_new_game_island_count_per_team(10)
        update_set_new_game_carrier_count_per_team(1)
        update_set_new_game_is_tutorial(false)
        if cc2x and cc2x.log then cc2x.log('AIRFIELDS_LIVE private GUI custom-game event') end
        update_ui_event('new_game_custom')
    end
end
