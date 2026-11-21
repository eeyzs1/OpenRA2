#include "game/ai.h"
#include <algorithm>

namespace {
bool constructionVehicle(const World::Ent& unit,int player) {
    return unit.alive && !unit.isBuilding && unit.player==player && unit.utype==UnitType::MCV
        && unit.mindBy==INVALID_EID && !unit.permaControlled;
}

bool carriedConstructionVehicle(const std::vector<World::Ent::GarrisonedUnit>& cargo) {
    for(const auto& passenger:cargo)
        if((passenger.type==UnitType::MCV && !passenger.permaControlled)
            || carriedConstructionVehicle(passenger.cargo))return true;
    return false;
}
}

bool SkirmishAI::constructionVehiclePlanned(const World& world) const {
    for(const auto& unit:world.ents) {
        if(constructionVehicle(unit,player))return true;
        if(unit.alive && unit.player==player && carriedConstructionVehicle(unit.cargo))return true;
    }
    const auto mcv=[&](int selector) {
        if(!world.validProductionType(true,selector))return false;
        return world.nativeProductionType(selector)
            ?world.nativeTypes[selector-NATIVE_PRODUCTION_BASE]->unit.type==UnitType::MCV
            :selector==(int)UnitType::MCV;
    };
    const auto& house=world.players[player];
    for(int category=0;category<PROD_CAT_N;++category) {
        if(house.unitProd[category].active && mcv(house.unitProd[category].typeIdx))return true;
        for(int selector:house.unitQueue[category])if(mcv(selector))return true;
    }
    return false;
}

bool SkirmishAI::deployConstructionVehicle(World& world,EID id) {
    if(!world.valid(id) || !constructionVehicle(world.ents[id],player))return false;
    const auto& unit=world.ents[id];
    if(unit.state!=UState::Idle || unit.immobilized() || unit.deploying())return false;
    if(canDeployConstructionVehicleAt(world,id,(int)unit.x,(int)unit.y))return world.orderDeploy(id)!=INVALID_EID;
    // Retry the spatial search at most once per house cadence. The cadence is
    // derived from the saved world tick; no unsaved cooldown changes resumption.
    if((world.tick+(uint64_t)player*13)%90>=(uint64_t)thinkInterval() || world.repathsThisTick>=8)return false;
    const int sx=(int)unit.x,sy=(int)unit.y;
    if(!world.map.inBounds(sx,sy))return false;
    const auto& def=unitDef(unit);const int domain=def.pathDomain();
    const bool underBridge=def.fitsUnderBridge();const auto gates=world.nativeGateAccess(unit);
    const int area=(int)world.map.cells.size(),start=(int)world.movementSlot(sx,sy,unit.navLayer);
    if(start<0 || (size_t)start>=world.movementSlotCount())return false;
    std::vector<int> parent(world.movementSlotCount(),-1),search{start};parent[start]=start;
    constexpr Vec2i directions[]={{0,-1},{-1,0},{1,0},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
    int goal=-1;
    for(size_t head=0;head<search.size() && head<4096;++head) {
        const int slot=search[head],cell=slot%area,layer=slot/area,x=cell%world.map.w,y=cell/world.map.w;
        if(slot!=start && layer==0 && canDeployConstructionVehicleAt(world,id,x,y)){goal=slot;break;}
        for(auto delta:directions) {
            const int nx=x+delta.x,ny=y+delta.y;
            const int nextLayer=world.map.stepLayer(x,y,layer,nx,ny,domain,true,underBridge,gates);
            if(nextLayer<0 || world.cellHardBlockedForMove(nx,ny,id,nextLayer))continue;
            if(delta.x && delta.y) {
                const int horizontal=world.map.stepLayer(x,y,layer,nx,y,domain,true,underBridge,gates);
                const int vertical=world.map.stepLayer(x,y,layer,x,ny,domain,true,underBridge,gates);
                if(horizontal<0 || vertical<0 || world.cellHardBlockedForMove(nx,y,id,horizontal)
                    || world.cellHardBlockedForMove(x,ny,id,vertical))continue;
            }
            const int next=(int)world.movementSlot(nx,ny,nextLayer);
            if(parent[next]>=0 || search.size()>=4096)continue;
            parent[next]=slot;search.push_back(next);
        }
    }
    if(goal<0)return false;
    std::vector<Vec2i> route;
    for(int slot=goal;slot!=start;slot=parent[slot])route.push_back({(slot%area)%world.map.w,(slot%area)/world.map.w});
    std::reverse(route.begin(),route.end());
    const int gx=(goal%area)%world.map.w,gy=(goal%area)/world.map.w;
    ++world.repathsThisTick;++world.repathRequests;
    const auto moved=world.orderMove({id},gx+0.5f,gy+0.5f,false,false,false,0);
    if(!moved.accepted || world.ents[id].path.empty() || world.ents[id].path.back()!=Vec2i{gx,gy})return false;
    // Keep the route whose cells were checked above, rather than the ordinary
    // shortest route through parked allies. The regular order owns all intent.
    auto& actor=world.ents[id];
    if(distf(actor.x,actor.y,sx+0.5f,sy+0.5f)>0.001f)route.insert(route.begin(),{sx,sy});
    actor.path=std::move(route);actor.pathIdx=0;actor.blockTick=0;
    return true;
}

void SkirmishAI::recoverConstruction(World& world) {
    if(world.hasBld(player,BldType::ConYard))return;
    std::vector<EID> waiting;
    for(EID id=0;id<(EID)world.ents.size();++id) {
        const auto& unit=world.ents[id];
        if(!constructionVehicle(unit,player) || unit.state!=UState::Idle || unit.immobilized())continue;
        if(canDeployConstructionVehicleAt(world,id,(int)unit.x,(int)unit.y) && world.orderDeploy(id)!=INVALID_EID)return;
        waiting.push_back(id);
    }
    if(!waiting.empty())deployConstructionVehicle(world,waiting[(world.tick/90)%waiting.size()]);
}
