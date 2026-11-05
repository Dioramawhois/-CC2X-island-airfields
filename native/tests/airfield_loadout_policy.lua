-- SPDX-License-Identifier: GPL-3.0-or-later
-- Pure owned policy fixture. No engine or third-party script execution.
local policy=assert(loadfile('content/bridge/policy.lua'))()
local F=setmetatable({},{__index=function(t,k)rawset(t,k,k);return k end})
local values={VEHICLE_ID=21,VEHICLE_TEAM=1,VEHICLE_TEAM_IS_AI=0,VEHICLE_IS_AIR=1,
 VEHICLE_DOCK_STATE='CC2X_DOCK_DOCKED',VEHICLE_ATTACHED_TO=0,VEHICLE_DOCK_TILE=10,VEHICLE_DOCK_AIRFIELD=0}
local field_values={AIRFIELD_HANGARS={21,0,0,0},AIRFIELD_DOCK_QUEUE={},AIRFIELD_UNDOCK_QUEUE={}}
local v,t,island,af={},{},{},{}
function v:geti(f)return values[f:gsub('^CC2X_','')] end
function v:count()return 0 end
function t:geti()return values.tile_team or 1 end
function t:ref()return island end
function island:ref(_,index)if index==0 then return af end end
function af:count(f)return #field_values[f:gsub('^CC2X_','')] end
function af:geti(f,i)return field_values[f:gsub('^CC2X_','')][i+1] end
local queue,replies,calls={}, {}, {}
local cc2={world={fields=F,find=function(f,id)
 if f=='CC2X_WORLD_VEHICLES' and id==21 then return v end
 if f=='CC2X_WORLD_TILES' and id==10 then return t end
end,tiles=function()return {}end},session={generation=function()return 1 end,tick=function()return 2 end},
 messages={publish=function(topic,subject,value)replies[topic..':'..subject]=value;return true end,
 poll=function()local result=queue;queue={};return result end},equipment={available=function()return true end,
 set_attachment=function(id,slot,definition,fill)
 calls[#calls+1]={id,slot,definition,fill};return slot<8 and definition~=99
 end}}
local p=policy.new(cc2,{planes=true})
local serial=0
local function request(slot,definition)
 serial=serial+1;local payload=serial*35184372088832+21*8192+slot*128+definition+1
 queue={{topic='loadout',subject=21,value=payload}};p.tick();return replies['loadout_reply:21'],payload
end
local reply,payload=request(7,30)
assert(reply==payload and #calls==1 and calls[1][4]==true,'free complete attachment was not requested')
reply,payload=request(0,-1);assert(reply==payload and calls[2][3]==-1,'clear attachment refused')
reply,payload=request(0,99);assert(reply==-payload,'backend incompatibility ignored')
local before=#calls
for _,change in ipairs({function()values.tile_team=2 end,function()values.VEHICLE_TEAM_IS_AI=1 end,
 function()values.VEHICLE_DOCK_STATE=0 end,function()values.VEHICLE_ATTACHED_TO=100 end,
 function()field_values.AIRFIELD_HANGARS={0,0,0,0}end,
 function()field_values.AIRFIELD_HANGARS={21,0,0,0,0}end,
 function()field_values.AIRFIELD_DOCK_QUEUE={21}end,function()field_values.AIRFIELD_UNDOCK_QUEUE={21}end})do
 values.tile_team=1;values.VEHICLE_TEAM_IS_AI=0;values.VEHICLE_DOCK_STATE='CC2X_DOCK_DOCKED';values.VEHICLE_ATTACHED_TO=0
 field_values={AIRFIELD_HANGARS={21,0,0,0},AIRFIELD_DOCK_QUEUE={},AIRFIELD_UNDOCK_QUEUE={}}
 change();reply,payload=request(0,30);assert(reply==-payload and #calls==before,'unsafe loadout reached SDK')
end
queue={{topic='loadout',subject=22,value=payload},{topic='loadout',subject=21,value=0/0}}
p.tick();assert(#calls==before)
assert(replies['equipment:0']==1)
print('PASS free loadout policy, complete refill, clear, compatibility refusal, four bays, ownership, queues and malformed requests')

-- Map discovery uses isolated placement data before an island controller exists.
local preview_calls=0
cc2.world.vehicles=function()return {{team=1,team_is_ai=false}}end
cc2.tiles={sites=function(tid,kind,definition)
 assert(tid==10 and kind==0 and definition==3);preview_calls=preview_calls+1
 return {{x=900,z=200,flags_low=1},{x=9999,z=9999,flags_low=0}}
end}
cc2.session.tick=function()return 17 end
values.tile_team=1
local q=policy.new(cc2,{planes=true})
queue={{topic='site_request',subject=10,value=0},{topic='site_request',subject=10,value=0}}
q.tick()
assert(preview_calls==1 and replies['site_x:10']==900 and replies['site_z:10']==200 and replies['layout_revision:0']==1)
values.tile_team=2;q=policy.new(cc2,{planes=true})
queue={{topic='site_request',subject=10,value=0}};q.tick();assert(preview_calls==1,'non-human-owned island was generated')
print('PASS distant site requests, human-team authorization, deduplication, active-layer filtering and actual placement coordinates')
