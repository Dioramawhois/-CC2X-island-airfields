// SPDX-License-Identifier: GPL-3.0-or-later
// Real independent Lua module and public SDK Host over synthetic engine values.
// This fixture integrates velocity on flat ground; it is not game/PhysX/network acceptance.
#include "runtime/host.h"
#include "order_context.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
extern "C" void cc2x_native_hangar_spawn_result(std::uint32_t,int,std::uint32_t,std::uint32_t) {
    throw std::runtime_error("Unexpected carrier spawn in airfield fixture");
}
namespace fs=std::filesystem;
using cc2::runtime::Host;
namespace {
struct Waypoint {int type=CC2X_WAYPOINT_MOVE,param=0;};
struct Vehicle {
    std::uint32_t id=0; bool wing=false,ai=false,exists=true;
    int team=1,dock=0,flight=2,attached=0,dockVehicle=0,dockTile=0,dockAirfield=-1,mode=0,taxi=0;
    double speed=0,power=1,target[3]{};cc2x_body body{};std::vector<Waypoint> waypoints;
};
struct Airfield {int runway=-1,state=0,mode=0;std::vector<std::uint32_t> hangars{0,0,0,0},landing,takeoff;};
struct Tile {std::uint32_t id=7;int team=1;bool loaded=true;};
std::vector<Vehicle> vehicles;Airfield af;Tile tile;int island,server;
std::wstring project;std::vector<std::string> logs;int publications=0,bodyWrites=0;
std::uint64_t coreEpoch=19;
std::uint64_t CoreGeneration(){return coreEpoch;}
void Require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void CheckOwnedMapKnowledge(const fs::path& root) {
    for(const char* screen:{"screen_holomap.lua.patch","screen_vehicle_control.lua.patch"}) {
        std::ifstream input(root/"lua"/screen);std::string patch((std::istreambuf_iterator<char>(input)),{}),code;
        for(const char* name:{"mm_airfield_count","mm_is_marked_airfield"}) {
            auto start=patch.find(std::string("+function ")+name+"(");
            auto end=patch.find("\n+end",start);Require(start!=std::string::npos&&end!=std::string::npos,"Missing owned screen helper");
            for(std::size_t i=start;i<end+5;++i)if(patch[i]!='+'||!(i==start||patch[i-1]=='\n'))code+=patch[i];
            code+='\n';
        }
        std::ifstream fixture(root/"native/tests/owned_map_knowledge.lua");
        std::string checks((std::istreambuf_iterator<char>(fixture)),{});
        Require(!checks.empty(),"Missing owned map knowledge checks");code+=checks;
        lua_State* L=luaL_newstate();Require(L!=nullptr,"Map fixture VM unavailable");luaL_openlibs(L);
        int rc=luaL_loadbuffer(L,code.data(),code.size(),screen);if(rc==LUA_OK)rc=lua_pcall(L,0,0,0);
        std::string error=rc==LUA_OK?"":lua_tostring(L,-1);lua_close(L);
        if(rc!=LUA_OK)throw std::runtime_error(std::string(screen)+": "+error);
    }
}
void Log(const char*,int,const char* format,...){char text[4096];va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);logs.emplace_back(text);}
void TickRegister(void(*)(void*,void*),void*){}
void SessionRegister(void(*)(void*,int,void*),void*){}
int ModCount(){return 1;}
int ModPath(int index,wchar_t* out,std::uint32_t capacity){if(index||project.size()>=capacity)return 0;wcscpy_s(out,capacity,project.c_str());return 1;}
int WaypointsRegister(void(*)(void*,void*),void*){return 1;}
int AirfieldRegister(void(*)(void*,void*),void*){return 1;}
int HangarRegister(int(*)(void*,void*,void*),void*){return 1;}
int ControllerRegister(int(*)(int,void*),void*){return 1;}
int SpawnRegister(int(*)(void*,int,void*),void*){return 1;}
Vehicle* V(const void* object){for(auto& v:vehicles)if(&v==object&&v.exists)return &v;return nullptr;}
std::vector<std::uint32_t>* List(const void* object,int field){if(object!=&af)return nullptr;
    switch(field){case CC2X_AIRFIELD_HANGARS:return &af.hangars;case CC2X_AIRFIELD_DOCK_QUEUE:return &af.landing;case CC2X_AIRFIELD_UNDOCK_QUEUE:return &af.takeoff;default:return nullptr;}}
void* Find(int list,std::uint32_t id){if(list==CC2X_WORLD_TILES)return id==tile.id?&tile:nullptr;
    if(list==CC2X_WORLD_VEHICLES)for(auto& v:vehicles)if(v.id==id&&v.exists)return &v;return nullptr;}
int Count(const void* object,int field){if(auto* list=List(object,field))return static_cast<int>(list->size());
    if(!object&&field==CC2X_WORLD_VEHICLES)return static_cast<int>(vehicles.size());
    if(!object&&field==CC2X_WORLD_TILES)return 1;
    if(object==&island&&field==CC2X_ISLAND_AIRFIELDS)return 1;
    if(auto* v=V(object)){if(field==CC2X_VEHICLE_WAYPOINTS)return static_cast<int>(v->waypoints.size());if(field==CC2X_VEHICLE_WHEELS)return 3;if(field==CC2X_VEHICLE_ATTACHMENTS)return 1;}
    return 0;}
void* Ref(const void* object,int field,int index){if(index<0)return nullptr;
    if(!object&&field==CC2X_WORLD_TILES)return index==0?&tile:nullptr;
    if(!object&&field==CC2X_WORLD_VEHICLES)return static_cast<std::size_t>(index)<vehicles.size()&&vehicles[index].exists?&vehicles[index]:nullptr;
    if(object==&tile&&field==CC2X_TILE_ISLAND)return tile.loaded?&island:nullptr;
    if(object==&island&&field==CC2X_ISLAND_AIRFIELDS)return index==0?&af:nullptr;
    if(auto* v=V(object))if(field==CC2X_VEHICLE_WAYPOINTS&&static_cast<std::size_t>(index)<v->waypoints.size())return &v->waypoints[index];
    return nullptr;}
std::int64_t GetI(const void* object,int field,int index){if(auto* list=List(object,field))return index>=0&&static_cast<std::size_t>(index)<list->size()?(*list)[index]:0;
    if(auto* v=V(object)){switch(field){
    case CC2X_VEHICLE_ID:return v->id;case CC2X_VEHICLE_TEAM:return v->team;case CC2X_VEHICLE_TEAM_IS_AI:return v->ai;
    case CC2X_VEHICLE_IS_AIR:return 1;case CC2X_VEHICLE_IS_WING:return v->wing;case CC2X_VEHICLE_IS_ROTOR:return !v->wing;
    case CC2X_VEHICLE_DOCK_STATE:return v->dock;case CC2X_VEHICLE_FLIGHT_STATE:return v->flight;
    case CC2X_VEHICLE_ATTACHED_TO:return v->attached;case CC2X_VEHICLE_DOCK_VEHICLE:return v->dockVehicle;
    case CC2X_VEHICLE_DOCK_TILE:return v->dockTile;case CC2X_VEHICLE_DOCK_AIRFIELD:return v->dockAirfield;
    case CC2X_VEHICLE_CONTROL_MODE:return v->mode;case CC2X_VEHICLE_CONTROL_TAXI:return v->taxi;case CC2X_VEHICLE_HP:return 100;default:return 0;}}
    for(auto& v:vehicles)for(auto& w:v.waypoints)if(&w==object)return field==CC2X_WAYPOINT_TYPE?w.type:field==CC2X_WAYPOINT_PARAM?w.param:0;
    if(object==&tile){if(field==CC2X_TILE_ID)return tile.id;if(field==CC2X_TILE_TEAM)return tile.team;if(field==CC2X_TILE_AIRFIELDS)return tile.loaded?1:-1;}
    if(object==&af){if(field==CC2X_AIRFIELD_TILE)return tile.id;if(field==CC2X_AIRFIELD_RUNWAY_VEHICLE)return af.runway;if(field==CC2X_AIRFIELD_STATE)return af.state;if(field==CC2X_AIRFIELD_MODE)return af.mode;}
    return 0;}
double GetF(const void* object,int field,int index){if(auto* v=V(object)){if(field==CC2X_VEHICLE_POS)return v->body.pos[index];
    if(field==CC2X_VEHICLE_CONTROL_TARGET)return v->target[index];if(field==CC2X_VEHICLE_CONTROL_SPEED)return v->speed;if(field==CC2X_VEHICLE_CONTROL_POWER)return v->power;
    if(field==CC2X_VEHICLE_WHEEL_CONTACT)return index>0&&v->body.pos[1]<=1.05?1:0;}
    if(object==&af){if(field==CC2X_AIRFIELD_RUNWAY_START)return index==0?75:index==2?-140:0;
    if(field==CC2X_AIRFIELD_RUNWAY_END)return index==0?-150:index==2?-140:0;
    if(field==CC2X_AIRFIELD_TRANSFORM)return index==0||index==4||index==8?1:0;}
    return 0;}
int SetI(void* object,int field,int index,std::int64_t value){if(auto* list=List(object,field)){if(index<0||static_cast<std::size_t>(index)>=list->size())return 0;(*list)[index]=static_cast<std::uint32_t>(value);return 1;}
    if(auto* v=V(object)){switch(field){case CC2X_VEHICLE_DOCK_STATE:v->dock=static_cast<int>(value);break;case CC2X_VEHICLE_FLIGHT_STATE:v->flight=static_cast<int>(value);break;
    case CC2X_VEHICLE_DOCK_VEHICLE:v->dockVehicle=static_cast<int>(value);break;case CC2X_VEHICLE_DOCK_TILE:v->dockTile=static_cast<int>(value);break;
    case CC2X_VEHICLE_DOCK_AIRFIELD:v->dockAirfield=static_cast<int>(value);break;case CC2X_VEHICLE_CONTROL_MODE:v->mode=static_cast<int>(value);break;
    case CC2X_VEHICLE_CONTROL_TAXI:v->taxi=static_cast<int>(value);break;case CC2X_VEHICLE_ATTACHMENT_FLYING:break;default:return 0;}return 1;}
    for(auto& v:vehicles)for(auto& w:v.waypoints)if(&w==object){if(field==CC2X_WAYPOINT_TYPE)w.type=static_cast<int>(value);else if(field==CC2X_WAYPOINT_PARAM)w.param=static_cast<int>(value);else return 0;return 1;}
    if(object==&af){if(field==CC2X_AIRFIELD_RUNWAY_VEHICLE)af.runway=static_cast<int>(value);else if(field==CC2X_AIRFIELD_STATE)af.state=static_cast<int>(value);else if(field==CC2X_AIRFIELD_MODE)af.mode=static_cast<int>(value);else return 0;return 1;}return 0;}
int SetF(void* object,int field,int index,double value){if(auto* v=V(object)){if(field==CC2X_VEHICLE_CONTROL_TARGET)v->target[index]=value;else if(field==CC2X_VEHICLE_CONTROL_SPEED)v->speed=value;else if(field==CC2X_VEHICLE_CONTROL_POWER)v->power=value;else return 0;return 1;}return 0;}
int Clear(void* object,int field){if(auto* v=V(object))if(field==CC2X_VEHICLE_WAYPOINTS){v->waypoints.clear();return 1;}
    if(auto* list=List(object,field)){list->clear();return 1;}return object==&af&&field==CC2X_AIRFIELD_TAXI_PATH;}
int Remove(void* object,int field,int index){auto* list=List(object,field);if(!list||index<0||static_cast<std::size_t>(index)>=list->size())return 0;list->erase(list->begin()+index);return 1;}
int Push(void* object,int field,std::int64_t value){auto* list=List(object,field);if(!list)return 0;list->push_back(static_cast<std::uint32_t>(value));return 1;}
int BodyGet(const void* object,cc2x_body* out){auto* v=V(object);if(!v||!out)return 0;*out=v->body;return 1;}
int BodySet(void* object,const cc2x_body* in,int mask){auto* v=V(object);if(!v||!in)return 0;
    if(mask&CC2X_BODY_POS)std::copy_n(in->pos,3,v->body.pos);if(mask&CC2X_BODY_ROT)std::copy_n(in->rot,4,v->body.rot);
    if(mask&CC2X_BODY_VEL)std::copy_n(in->vel,3,v->body.vel);if(mask&CC2X_BODY_SPIN)std::copy_n(in->spin,3,v->body.spin);++bodyWrites;return 1;}
int Raycast(const double* from,const double* direction,double distance,int,cc2x_hit* hit){if(direction[1]>=0||from[1]<0||from[1]>distance)return 0;
    *hit={};hit->pos[0]=from[0];hit->pos[2]=from[2];hit->normal[1]=1;hit->distance=from[1];return 1;}
void Gear(void*,int){}void Brakes(void*,float,float){}
void Sync(void*){++publications;}
int Landing(void* object,void* vehicle){auto* v=V(vehicle);if(object!=&island||!v)return 0;v->dock=CC2X_DOCK_QUEUED;af.landing.push_back(v->id);return 1;}
void* TileAt(double,double){return &tile;}
void Transform(const void*,const double* local,double* world){std::copy_n(local,3,world);}
int Bay(const void*,int slot,double* pos,double* yaw){if(slot<0||slot>3)return 0;if(pos){pos[0]=slot&1?20:-20;pos[1]=0;pos[2]=slot&2?20:-20;}if(yaw)*yaw=slot&1?-1.5707963267948966:1.5707963267948966;return 1;}
void Reset(bool wing=false,int n=1){vehicles.assign(n,{});af=Airfield{};tile=Tile{};publications=bodyWrites=0;
    for(int i=0;i<n;++i){auto& v=vehicles[i];v.id=17u+i;v.wing=wing;v.body.pos[0]=wing?3000:500;v.body.pos[1]=wing?300:150;v.body.pos[2]=wing?-140:0;v.body.vel[0]=wing?-70:0;v.body.rot[3]=1;}}
void CheckErrors(Host& host){Require(host.Stats().errors==0,"Lua callback failed");for(auto& s:logs)Require(s.find("MODULE_LOAD_ERROR")==std::string::npos,"Lua module failed initialization");}
void Step(Host& host,int n=1){for(int k=0;k<n;++k){host.Tick(&server);if(tile.loaded)host.WorldEvent("airfield_tick",tile.id,0);
    for(auto& v:vehicles)if(v.exists&&v.dock!=CC2X_DOCK_DOCKED){for(int axis=0;axis<3;++axis)v.body.pos[axis]+=v.body.vel[axis]/30.0;if(v.body.pos[1]<1){v.body.pos[1]=1;v.body.vel[1]=0;}}CheckErrors(host);}}
void DispatchOrder(Host& host,std::size_t index) {
    cc2x_waypoint_order record{sizeof(record),vehicles[index].id,23,CC2X_ORDER_BROADCAST_PENDING,coreEpoch};cc2x_order_scope previous;
    cc2x_orders_enter(&record,CoreGeneration,&previous);host.WorldEvent("waypoints",vehicles[index].id);cc2x_orders_leave(&previous);
}
void Order(Host& host,int type=CC2X_WAYPOINT_BARGE_LOAD_TILE,int param=7){vehicles[0].waypoints={{type,param}};int before=publications;DispatchOrder(host,0);
    Require(publications==before,"Incoming order was published twice");Sync(&vehicles[0]);}
}
int main(int argc,char** argv){try{
    Require(argc==2,"Expected project path");project=fs::absolute(argv[1]).wstring();
    CheckOwnedMapKnowledge(fs::path(project));
    wchar_t temp[MAX_PATH];GetTempPathW(MAX_PATH,temp);auto storage=fs::path(temp)/(L"cc2x-airfields-fixture-"+std::to_wstring(GetCurrentProcessId()));fs::create_directories(storage);
    const auto config=storage.string();cc2x_api api{};api.api_version=8;api.size=sizeof(api);api.sdk_version="synthetic-sdk";api.game_build="synthetic-engine";api.config_dir=config.c_str();api.log=Log;
    api.on_server_tick=TickRegister;api.on_session=SessionRegister;api.active_mod_count=ModCount;api.active_mod_path=ModPath;
    api.find=Find;api.ref=Ref;api.count=Count;api.geti=GetI;api.getf=GetF;api.seti=SetI;api.setf=SetF;api.list_clear=Clear;api.list_remove=Remove;api.list_push=Push;
    api.body_get=BodyGet;api.body_set=BodySet;api.raycast=Raycast;api.vehicle_gear=Gear;api.vehicle_brakes=Brakes;api.waypoints_sync=Sync;api.request_landing=Landing;api.tile_at=TileAt;api.airfield_to_world=Transform;api.airfield_bay=Bay;
    api.on_waypoints=WaypointsRegister;api.on_airfield_tick=AirfieldRegister;api.on_hangar_choice=HangarRegister;api.on_island_controller=ControllerRegister;api.on_island_spawn=SpawnRegister;
    cc2x_orders_profile(1);Reset();Host host;Require(host.Attach(&api,false),"SDK host attach failed");
    Require(host.WorldEvent("island_controller",1,0,0,0,true)==1,"Human island rule missing");
    Require(host.WorldEvent("island_spawn",7,0,0,1,true)==0,"Human garrison was not excluded");
    Require(host.WorldEvent("island_spawn",7,1,0,1,true)==1,"AI garrison changed");
    host.Session(&server,true);Step(host);Require(host.Stats().modules==1,"Lua module did not load");
    vehicles[0].waypoints={{CC2X_WAYPOINT_BARGE_LOAD_TILE,7}};host.WorldEvent("waypoints",vehicles[0].id);
    Require(vehicles[0].dock==0&&vehicles[0].waypoints[0].type==CC2X_WAYPOINT_MOVE,"Unattributed command was accepted");
    // Deferred order replaced before the island loads.
    tile.loaded=false;Order(host);Require(vehicles[0].dock==0,"Unloaded island landed immediately");
    Order(host,CC2X_WAYPOINT_MOVE,0);tile.loaded=true;Step(host,16);Require(vehicles[0].dock==0,"Canceled deferred order survived");
    tile.team=2;Order(host);Require(vehicles[0].dock==0,"Foreign island accepted");tile.team=1;
    // Entire helicopter landing / launch through the public module.
    Order(host);for(int i=0;i<6000&&vehicles[0].dock!=CC2X_DOCK_DOCKED;++i)Step(host);
    Require(vehicles[0].dock==CC2X_DOCK_DOCKED,"Helicopter did not land");Require(bodyWrites>0,"No flight control output");
    Order(host,CC2X_WAYPOINT_MOVE,0);for(int i=0;i<3000&&vehicles[0].dock!=0;++i)Step(host);Require(vehicles[0].dock==0,"Helicopter did not take off");
    // New sessions reuse IDs but must not retain flights, runway locks or intent.
    host.Session(&server,false);Reset(true,2);host.Session(&server,true);Step(host);
    Order(host);vehicles[1].waypoints={{CC2X_WAYPOINT_BARGE_LOAD_TILE,7}};DispatchOrder(host,1);Sync(&vehicles[1]);
    for(int i=0;i<24000&&(vehicles[0].dock!=4||vehicles[1].dock!=4);++i)Step(host);
    Require(vehicles[0].dock==4&&vehicles[1].dock==4,"Two planes did not share the runway successfully");
    Order(host,CC2X_WAYPOINT_MOVE,0);for(int i=0;i<6000&&vehicles[0].dock!=0;++i)Step(host);Require(vehicles[0].dock==0,"Plane did not take off");
    host.Session(&server,false);Reset();host.Session(&server,true);Step(host);Order(host);Step(host,10);Order(host,CC2X_WAYPOINT_MOVE,0);Step(host,50);
    Require(vehicles[0].dock==0,"New order did not cancel active landing");
    Order(host);Step(host,10);tile.team=2;int before_capture=bodyWrites;Step(host,5);
    Require(bodyWrites==before_capture&&vehicles[0].dock==0,"Captured island retained flight control");
    CheckErrors(host);host.Shutdown();
    Require(fs::weakly_canonical(storage).parent_path()==fs::weakly_canonical(fs::path(temp)),"Unsafe fixture cleanup target");fs::remove_all(storage);
    std::cout<<"PASS actual Lua gameplay + public SDK: own island map knowledge before streaming, early rules, deferred cancellation, ownership, helicopter/plane landing and launch, shared runway, reused sessions and active cancellation (synthetic flat-ground integration)\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<"\n";for(auto& s:logs)std::cerr<<s<<"\n";return 1;}}
