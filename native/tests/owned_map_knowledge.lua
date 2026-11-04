-- SPDX-License-Identifier: GPL-3.0-or-later
-- Actual owned screen helpers, evaluated without running stock game scripts.
local known, level, own, active, remote = nil, 1, true, true, false
local island = {
    get_id=function() return 7 end,
    get_team_control=function() return own and 1 or 2 end,
    get_difficulty_level=function() return level end,
}
function update_get_screen_team_id() return 1 end
function mm_plugin() if active then return {count=function() return known end} end end
function mm_remote_host() return remote end
for difficulty=1,4 do
    level=difficulty;known=nil
    local expected=difficulty==1 and 0 or 1
    assert(mm_airfield_count(island)==expected,'Own island requires a reconnaissance flight')
    assert(mm_is_marked_airfield(island)==(expected>0),'Wrong initial marker')
    known=expected -- approaching/streaming confirms the same generation metadata
    assert(mm_airfield_count(island)==expected,'Marker changes on approach')
end
known=nil;active=false;remote=true;level=3
assert(mm_airfield_count(island)==1,'Remote own island metadata unavailable')
own=false
assert(mm_airfield_count(island)==-1,'Foreign island knowledge was inferred')
own=true;known=0;active=true
assert(mm_airfield_count(island)==0,'Actual absence must override generation rules')
known=nil;island.get_difficulty_level=nil
assert(mm_airfield_count(island)==-1,'Unavailable metadata must remain unknown')
