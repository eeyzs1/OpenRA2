#include "game/ai.h"
#include <algorithm>

void SkirmishAI::clearFactoryExits(World& w) {
    auto movable=[&](EID id) {
        if(!w.valid(id))return false;
        const auto& unit=w.ents[id];
        return !unit.isBuilding && unit.player==player && !unit.airborne() && !unit.parasiting
            && !unit.deployed && !unit.radDeployed && !unit.guard && !unit.immobilized()
            && !(w.valid(unit.parasite) && w.ents[unit.parasite].utype==UnitType::Squid)
            && unit.state!=UState::Boarding && unit.state!=UState::Servicing;
    };
    std::vector<uint8_t> mouths(w.map.cells.size(),0);
    for(size_t factoryId=0;factoryId<w.ents.size();++factoryId) {
        const auto& factory=w.ents[factoryId];
        if(!factory.alive || !factory.isBuilding || factory.player!=player || !isRallyBuilding(factory.btype))continue;
        for(auto exit:w.factoryExitCells((EID)factoryId,unitDef(UnitType::GI)))mouths[w.cellIdx(exit.x,exit.y)]=1;
    }
    auto freeLanding=[&](EID mover,int cell,EID released=INVALID_EID) {
        if(mouths[cell])return false;
        const bool infantry=unitDef(w.ents[mover]).isInfantry();
        int reserved=0;
        for(size_t id=0;id<w.ents.size();++id) {
            const auto& unit=w.ents[id];
            if((EID)id==mover || !unit.alive || unit.isBuilding || unit.airborne() || unit.parasiting || unit.magneticHeight>0)continue;
            const bool present=unit.occCell==cell;
            const bool incoming=(EID)id!=released && unit.pathIdx<(int)unit.path.size() && w.isAllied(player,unit.player)
                && w.cellIdx(unit.path.back().x,unit.path.back().y)==cell;
            if(present || incoming)reserved+=infantry && unitDef(unit).isInfantry()?1:3;
        }
        return reserved<(infantry?3:1);
    };
    auto step=[&](EID mover,int from,int to) {
        const int domain=unitDef(w.ents[mover]).pathDomain();
        const int x=from%w.map.w,y=from/w.map.w,nx=to%w.map.w,ny=to/w.map.w;
        if(std::abs(nx-x)>1 || std::abs(ny-y)>1)return false;
        auto open=[&](int tx,int ty){return w.passableFor(tx,ty,domain) && !w.bldBlocked(tx,ty);};
        if(!open(nx,ny) || !w.map.climbOk(x,y,nx,ny,domain))return false;
        return x==nx || y==ny || (open(nx,y) && open(x,ny)
            && w.map.climbOk(x,y,nx,y,domain) && w.map.climbOk(x,y,x,ny,domain)
            && !w.cellHardBlockedForMove(nx,y,mover) && !w.cellHardBlockedForMove(x,ny,mover));
    };
    auto issue=[&](EID mover,int destination,std::vector<Vec2i> route) {
        auto& unit=w.ents[mover];
        if(unit.state==UState::Moving && unit.pathIdx<(int)unit.path.size()
            && unit.path.back()==Vec2i{destination%w.map.w,destination/w.map.w}) {
            bool clear=true;int previous=unit.occCell;
            for(size_t index=unit.pathIdx;index<unit.path.size() && clear;++index) {
                const auto point=unit.path[index];const int cell=w.cellIdx(point.x,point.y);
                clear=step(mover,previous,cell) && !w.cellHardBlockedForMove(point.x,point.y,mover);previous=cell;
            }
            if(clear)return true;
        }
        if(w.repathsThisTick>=8)return false;
        ++w.repathsThisTick;++w.repathRequests;
        const bool automatic=unit.autoHarvest;const auto ore=unit.oreCell;
        const auto result=w.orderMove({mover},destination%w.map.w+0.5f,destination/w.map.w+0.5f,false);
        unit.autoHarvest=automatic;unit.oreCell=ore;
        if(result.accepted<=0 || unit.path.empty()
            || unit.path.back()!=Vec2i{destination%w.map.w,destination/w.map.w})return false;
        // The ordinary order establishes intent; follow the physically clear
        // route we checked instead of a shorter route through parked units.
        const int current=unit.occCell;
        if(distf(unit.x,unit.y,current%w.map.w+0.5f,current/w.map.w+0.5f)>0.001f)
            route.insert(route.begin(),{current%w.map.w,current/w.map.w});
        unit.path=std::move(route);unit.pathIdx=0;unit.blockTick=0;
        return true;
    };
    constexpr Vec2i directions[]={{0,1},{1,0},{-1,0},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
    for(size_t factoryId=0;factoryId<w.ents.size();++factoryId) {
        if(w.repathsThisTick>=8)return;
        const auto& factory=w.ents[factoryId];
        if(!World::productionFacilityOperational(factory) || factory.player!=player)continue;
        const UnitDef* waiting=nullptr;
        for(const auto& order:w.players[player].unitProd) {
            if(!order.active || !order.ready || !w.validProductionType(true,order.typeIdx))continue;
            const bool source=w.nativeProductionType(order.typeIdx);
            const auto& unit=source?w.nativeTypes[order.typeIdx-NATIVE_PRODUCTION_BASE]->unit:unitDef((UnitType)order.typeIdx);
            const bool supports=source?w.nativeFactorySupports(factory,order.typeIdx-NATIVE_PRODUCTION_BASE):isFactoryFor(factory.btype,unit);
            if(supports && !(unit.isAir() && unit.ammo>0)){waiting=&unit;break;}
        }
        if(!waiting)continue;
        for(auto exit:w.factoryExitCells((EID)factoryId,*waiting)) {
            const int x=exit.x,y=exit.y;
            const EID blocker=w.unitAtCell(x,y);
            if(!movable(blocker))continue;
            const int start=w.cellIdx(x,y);
            std::vector<int> parent(w.map.cells.size(),-1),search{start};parent[start]=start;
            int goal=-1,fallback=-1;
            for(size_t head=0;head<search.size() && head<4096;++head) {
                const int cell=search[head],cx=cell%w.map.w,cy=cell/w.map.w;
                if(cell!=start && freeLanding(blocker,cell)) {
                    if(std::max(std::abs(cx-x),std::abs(cy-y))>=2){goal=cell;break;}
                    if(fallback<0)fallback=cell;
                }
                for(auto delta:directions) {
                    const int nx=cx+delta.x,ny=cy+delta.y;
                    if(!w.map.inBounds(nx,ny) || std::max(std::abs(nx-x),std::abs(ny-y))>8)continue;
                    const int next=w.cellIdx(nx,ny);
                    if(parent[next]>=0 || !step(blocker,cell,next) || w.cellHardBlockedForMove(nx,ny,blocker))continue;
                    parent[next]=cell;search.push_back(next);
                }
            }
            if(goal<0)goal=fallback;
            if(goal>=0) {
                std::vector<Vec2i> route;
                for(int cell=goal;cell!=start;cell=parent[cell])route.push_back({cell%w.map.w,cell/w.map.w});
                std::reverse(route.begin(),route.end());
                if(issue(blocker,goal,std::move(route)))break;
            }
            if(w.repathsThisTick>=8)return;
            // A dense apron may need its tail cleared first. Move one reachable
            // ally into a genuinely free adjacent slot, then retry next cadence;
            // never order the head into a cell whose occupant has not left yet.
            std::vector<EID> actors{blocker};std::vector<uint8_t> seen(w.ents.size(),0);seen[blocker]=1;
            bool moved=false;
            for(size_t head=0;head<actors.size() && head<256 && !moved;++head) {
                const EID actor=actors[head];const int current=w.ents[actor].occCell;
                if(current<0)continue;
                for(int direction=0;direction<4 && !moved;++direction) {
                    const int nx=current%w.map.w+directions[direction].x,ny=current/w.map.w+directions[direction].y;
                    if(!w.map.inBounds(nx,ny) || std::max(std::abs(nx-x),std::abs(ny-y))>8)continue;
                    const int next=w.cellIdx(nx,ny);if(!step(actor,current,next))continue;
                    if(!w.cellHardBlockedForMove(nx,ny,actor) && freeLanding(actor,next,blocker)) {
                        auto& headUnit=w.ents[blocker];
                        if(actor!=blocker && headUnit.pathIdx<(int)headUnit.path.size()
                            && headUnit.path.back()==Vec2i{nx,ny}) {
                            // Its blocked approach may reserve the very slot the
                            // tail needs. Hold the head until that ally has left.
                            const bool automatic=headUnit.autoHarvest;const auto ore=headUnit.oreCell;
                            w.orderStop({blocker});headUnit.autoHarvest=automatic;headUnit.oreCell=ore;
                        }
                        moved=issue(actor,next,{{nx,ny}});continue;
                    }
                    for(EID other=w.unitAtCell(nx,ny);other!=INVALID_EID;other=w.ents[other].occNext) {
                        if(!seen[other] && movable(other)){seen[other]=1;actors.push_back(other);}
                    }
                }
            }
            if(moved)break;
        }
    }
}
