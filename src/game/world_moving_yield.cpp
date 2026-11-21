#include "game/world.h"
#include <algorithm>

bool World::yieldMovingConflict(EID requester,EID blocker,bool admissionBlocked) {
    if(!valid(requester) || !valid(blocker) || requester==blocker || !isAllied(ents[requester].player,ents[blocker].player))return false;
    auto moving=[&](const Ent& unit) {
        return unit.alive && !unit.isBuilding && !unitDef(unit).isAir() && !unit.parasiting
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
        const int domain=unitDef(unit).pathDomain();
        if(!admissionBlocked && !narrowMovementCell(unit.occCell%map.w,unit.occCell/map.w,domain,unit.navLayer,unitDef(unit).fitsUnderBridge()))return false;
        const auto next=unit.path.begin()+unit.pathIdx;
        const bool waiting=std::adjacent_find(next,unit.path.end())!=unit.path.end();
        // Preserve a retreat already travelling toward a bay. A dwell located
        // on the occupied next step cannot begin and may be replanned.
        if(waiting && (next+1==unit.path.end() || *next!=*(next+1)))return false;
    }
    if(ents[requester].path.back()==ents[blocker].path.back())return false;
    const int area=(int)map.cells.size(),layers=map.hasBridgeDecks()?2:1;
    auto cellX=[&](int cell){return cell%area%map.w;};
    auto cellY=[&](int cell){return cell%area/map.w;};
    auto occupiedCell=[&](EID id){return ents[id].occCell+ents[id].navLayer*area;};
    auto nextCell=[&](EID id){const auto next=ents[id].path[ents[id].pathIdx];return (int)movementSlot(next.x,next.y,movementLayerAt(ents[id],next.x,next.y));};
    if(nextCell(requester)!=occupiedCell(blocker) || nextCell(blocker)!=occupiedCell(requester))return false;
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
        const int domain=unitDef(unit).pathDomain(),start=occupiedCell(yielding);
        const auto goal=unit.path.back(),passingGoal=ents[passing].path.back();
        const int destination=(int)movementSlot(goal.x,goal.y,movementLayerAt(unit,goal.x,goal.y));
        std::vector<uint8_t> retreatCost(movementSlotCount(),0),resumeCost(movementSlotCount(),0),avoid(movementSlotCount(),0),landing(movementSlotCount(),1);
        int passingX=(int)ents[passing].x,passingY=(int)ents[passing].y,passingLayer=ents[passing].navLayer;
        for(size_t index=ents[passing].pathIdx;index<ents[passing].path.size();++index) {
            const auto point=ents[passing].path[index];
            if(map.hasBridgeDecks())passingLayer=movementStep(ents[passing],passingX,passingY,passingLayer,point.x,point.y);
            if(passingLayer<0)break;
            avoid[movementSlot(point.x,point.y,passingLayer)]=1;passingX=point.x;passingY=point.y;
        }
        for(size_t id=0;id<ents.size();++id) {
            const auto& other=ents[id];if(!other.alive || other.isBuilding || (EID)id==yielding)continue;
            if(moving(other) && isAllied(unit.player,other.player)) {
                const auto end=other.path.back();landing[movementSlot(end.x,end.y,movementLayerAt(other,end.x,end.y))]=0;
            }
            if(other.occCell<0 || other.occCell>=area)continue;
            const int cell=other.occCell+other.navLayer*area;
            landing[cell]=0;
            const bool hard=cellHardBlockedForMove(cellX(cell),cellY(cell),yielding,other.navLayer);
            if(hard)retreatCost[cell]=255;
            resumeCost[cell]=std::max(resumeCost[cell],uint8_t{16});
            if(hard && (!moving(other) || !isAllied(unit.player,other.player)))resumeCost[cell]=255;
        }
        // The other unit will eventually park here. Reject a retreat whose
        // return would need to pass through that final destination again.
        resumeCost[movementSlot(passingGoal.x,passingGoal.y,movementLayerAt(ents[passing],passingGoal.x,passingGoal.y))]=255;
        auto open=[&](int x,int y,int layer,const std::vector<uint8_t>& cost) {
            return movementPassable(unit,x,y,layer) && !bldBlocked(x,y,layer) && cost[movementSlot(x,y,layer)]!=255;
        };
        auto step=[&](int from,int tx,int ty,const std::vector<uint8_t>& cost) {
            const int fx=cellX(from),fy=cellY(from),fl=from/area;
            const int layer=map.hasBridgeDecks()?movementStep(unit,fx,fy,fl,tx,ty):map.climbOk(fx,fy,tx,ty,domain)?0:-1;
            if(layer<0 || !open(tx,ty,layer,cost))return -1;
            if(tx!=fx && ty!=fy) {
                const int a=map.hasBridgeDecks()?movementStep(unit,fx,fy,fl,tx,fy):map.climbOk(fx,fy,tx,fy,domain)?0:-1;
                const int b=map.hasBridgeDecks()?movementStep(unit,fx,fy,fl,fx,ty):map.climbOk(fx,fy,fx,ty,domain)?0:-1;
                if(a<0 || b<0 || !open(tx,fy,a,cost) || !open(fx,ty,b,cost))return -1;
            }
            return (int)movementSlot(tx,ty,layer);
        };
        if(!open(goal.x,goal.y,destination/area,resumeCost))continue;
        std::vector<int> onward(movementSlotCount(),-1),future{destination};onward[destination]=destination;
        // Reverse directed movement edges also respect asymmetric height cases.
        for(size_t head=0;head<future.size() && head<20000;++head) {
            const int cell=future[head],x=cellX(cell),y=cellY(cell);
            for(auto d:directions)for(int layer=0;layer<layers;++layer) {
                const int nx=x+d.x,ny=y+d.y;
                if(!open(nx,ny,layer,resumeCost))continue;
                const int previous=(int)movementSlot(nx,ny,layer);
                if(onward[previous]>=0 || step(previous,x,y,resumeCost)!=cell)continue;
                onward[previous]=cell;future.push_back(previous);
            }
        }
        std::vector<int> parent(movementSlotCount(),-1),search{start};parent[start]=start;
        int bay=-1;
        for(size_t head=0;head<search.size() && head<4096;++head) {
            const int cell=search[head],x=cellX(cell),y=cellY(cell);
            if(landing[cell] && !avoid[cell] && onward[cell]>=0 && !narrowMovementCell(x,y,domain,cell/area,unitDef(unit).fitsUnderBridge())){bay=cell;break;}
            for(auto d:directions) {
                const int next=step(cell,x+d.x,y+d.y,retreatCost);if(next<0 || parent[next]>=0)continue;
                parent[next]=cell;search.push_back(next);
            }
        }
        if(bay<0){++repathFailures;continue;}
        std::vector<Vec2i> path;
        for(int cell=bay;cell!=start;cell=parent[cell])path.push_back({cellX(cell),cellY(cell)});
        std::reverse(path.begin(),path.end());
        // Re-centre partial interpolation using the same legal movement path.
        if(distf(unit.x,unit.y,cellX(start)+0.5f,cellY(start)+0.5f)>0.001f)path.insert(path.begin(),{cellX(start),cellY(start)});
        path.insert(path.end(),30,Vec2i{cellX(bay),cellY(bay)});
        for(int cell=bay;cell!=destination && path.size()<=4096;) {
            cell=onward[cell];path.push_back({cellX(cell),cellY(cell)});
        }
        if(path.size()>4096 || path.back()!=goal){++repathFailures;continue;}
        unit.path=std::move(path);unit.pathIdx=0;unit.blockTick=0;
        return true;
    }
    return false;
}
