#include "game/world.h"
#include <algorithm>

bool World::yieldMovingConflict(EID requester,EID blocker,bool admissionBlocked) {
    if(!valid(requester) || !valid(blocker) || requester==blocker || !isAllied(ents[requester].player,ents[blocker].player))return false;
    auto moving=[&](const Ent& unit) {
        return unit.alive && !unit.isBuilding && !unitDef(unit.utype).isAir() && !unit.parasiting
            && !unit.deployed && !unit.radDeployed && !unit.guard && !unit.immobilized()
            && !(valid(unit.parasite) && ents[unit.parasite].utype==UnitType::Squid)
            && unit.occCell>=0 && unit.pathIdx<(int)unit.path.size()
            && (unit.state==UState::Moving || unit.state==UState::AttackMoving || unit.state==UState::HarvestGo
                || unit.state==UState::HarvestReturn || unit.state==UState::Chasing || unit.state==UState::Boarding || unit.state==UState::Servicing);
    };
    if(!moving(ents[requester]) || !moving(ents[blocker]))return false;
    // Open areas already have local detours. A retreat through those parking
    // fields needlessly disturbs completed traffic. Admission may explicitly
    // request an apron swap: its wait would otherwise prevent local detours.
    for(EID id:{requester,blocker}) {
        const auto& unit=ents[id];
        const int domain=unitDef(unit.utype).pathDomain();
        if(!admissionBlocked && !narrowMovementCell(unit.occCell%map.w,unit.occCell/map.w,domain))return false;
        const auto next=unit.path.begin()+unit.pathIdx;
        const bool waiting=std::adjacent_find(next,unit.path.end())!=unit.path.end();
        // Preserve a retreat already travelling toward a bay. A dwell located
        // on the occupied next step cannot begin and may be replanned.
        if(waiting && (next+1==unit.path.end() || *next!=*(next+1)))return false;
    }
    if(ents[requester].path.back()==ents[blocker].path.back())return false;
    auto nextCell=[&](EID id){const auto next=ents[id].path[ents[id].pathIdx];return cellIdx(next.x,next.y);};
    if(nextCell(requester)!=ents[blocker].occCell || nextCell(blocker)!=ents[requester].occCell)return false;
    // A previous dwell prefix may itself point into the peer's occupied cell.
    // Reciprocal next steps prove it cannot finish; replace it, preserving the
    // real destination at path.back(), rather than deferring forever.
    constexpr Vec2i directions[]={{0,-1},{-1,0},{1,0},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
    for(EID yielding:{std::max(requester,blocker),std::min(requester,blocker)}) {
        auto& unit=ents[yielding];const EID passing=yielding==requester?blocker:requester;
        // Combat approach may stop as soon as a target enters range. Do not
        // make it responsible for completing a temporary retreat and return.
        if(unit.state==UState::Chasing || unit.state==UState::Boarding || unit.state==UState::Servicing)continue;
        if(repathsThisTick>=8)return false;
        ++repathsThisTick;++repathRequests;
        const int domain=unitDef(unit.utype).pathDomain(),start=unit.occCell;
        const auto goal=unit.path.back(),passingGoal=ents[passing].path.back();
        const int destination=cellIdx(goal.x,goal.y);
        std::vector<uint8_t> retreatCost(map.cells.size(),0),resumeCost(map.cells.size(),0),avoid(map.cells.size(),0),landing(map.cells.size(),1);
        for(size_t index=ents[passing].pathIdx;index<ents[passing].path.size();++index) {
            const auto point=ents[passing].path[index];avoid[cellIdx(point.x,point.y)]=1;
        }
        for(size_t id=0;id<ents.size();++id) {
            const auto& other=ents[id];if(!other.alive || other.isBuilding || (EID)id==yielding)continue;
            if(moving(other) && isAllied(unit.player,other.player)) {
                const auto end=other.path.back();landing[cellIdx(end.x,end.y)]=0;
            }
            const int cell=other.occCell;if(cell<0 || cell>=(int)retreatCost.size())continue;
            landing[cell]=0;
            const bool hard=cellHardBlockedForMove(cell%map.w,cell/map.w,yielding);
            if(hard)retreatCost[cell]=255;
            resumeCost[cell]=std::max(resumeCost[cell],uint8_t{16});
            if(hard && (!moving(other) || !isAllied(unit.player,other.player)))resumeCost[cell]=255;
        }
        // The other unit will eventually park here. Reject a retreat whose
        // return would need to pass through that final destination again.
        resumeCost[cellIdx(passingGoal.x,passingGoal.y)]=255;
        auto open=[&](int x,int y,const std::vector<uint8_t>& cost) {
            return passableFor(x,y,domain) && !bldBlocked(x,y) && cost[cellIdx(x,y)]!=255;
        };
        auto step=[&](int fx,int fy,int tx,int ty,const std::vector<uint8_t>& cost) {
            if(!open(tx,ty,cost) || !map.climbOk(fx,fy,tx,ty,domain))return false;
            if(tx!=fx && ty!=fy)return open(tx,fy,cost) && open(fx,ty,cost)
                && map.climbOk(fx,fy,tx,fy,domain) && map.climbOk(fx,fy,fx,ty,domain);
            return true;
        };
        if(!open(goal.x,goal.y,resumeCost))continue;
        std::vector<int> onward(map.cells.size(),-1),future{destination};onward[destination]=destination;
        // Reverse directed movement edges also respect asymmetric height cases.
        for(size_t head=0;head<future.size() && head<20000;++head) {
            const int cell=future[head],x=cell%map.w,y=cell/map.w;
            for(auto d:directions) {
                const int nx=x+d.x,ny=y+d.y;
                if(!open(nx,ny,resumeCost) || !step(nx,ny,x,y,resumeCost))continue;
                const int previous=cellIdx(nx,ny);if(onward[previous]>=0)continue;
                onward[previous]=cell;future.push_back(previous);
            }
        }
        std::vector<int> parent(map.cells.size(),-1),search{start};parent[start]=start;
        int bay=-1;
        for(size_t head=0;head<search.size() && head<4096;++head) {
            const int cell=search[head],x=cell%map.w,y=cell/map.w;
            if(landing[cell] && !avoid[cell] && onward[cell]>=0 && !narrowMovementCell(x,y,domain)){bay=cell;break;}
            for(auto d:directions) {
                int nx=x+d.x,ny=y+d.y;if(!step(x,y,nx,ny,retreatCost))continue;
                const int next=cellIdx(nx,ny);if(parent[next]>=0)continue;
                parent[next]=cell;search.push_back(next);
            }
        }
        if(bay<0){++repathFailures;continue;}
        std::vector<Vec2i> path;
        for(int cell=bay;cell!=start;cell=parent[cell])path.push_back({cell%map.w,cell/map.w});
        std::reverse(path.begin(),path.end());
        // Re-centre partial interpolation using the same legal movement path.
        if(distf(unit.x,unit.y,start%map.w+0.5f,start/map.w+0.5f)>0.001f)path.insert(path.begin(),{start%map.w,start/map.w});
        path.insert(path.end(),30,Vec2i{bay%map.w,bay/map.w});
        for(int cell=bay;cell!=destination && path.size()<=4096;) {
            cell=onward[cell];path.push_back({cell%map.w,cell/map.w});
        }
        if(path.size()>4096 || path.back()!=goal){++repathFailures;continue;}
        unit.path=std::move(path);unit.pathIdx=0;unit.blockTick=0;
        return true;
    }
    return false;
}
