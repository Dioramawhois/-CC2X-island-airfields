-- SPDX-License-Identifier: GPL-3.0-or-later
-- TEST ONLY. A manifest has one lifecycle entry; other scripts are require-able
-- sources. Compose the unmodified player lifecycle with our private driver.
local player=require('bridge.main')
local driver=require('bridge.driver')
return {
    on_load=function() player.on_load();driver.on_load() end,
    on_update=function(dt) player.on_update(dt);driver.on_update(dt) end,
    on_unload=function() if player.on_unload then player.on_unload() end end,
}
