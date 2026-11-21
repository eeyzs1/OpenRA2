#include "game/ai.h"
#include <algorithm>
#include <tuple>

namespace {
int assaultRank(BldType type) {
    switch(type) {
        case BldType::ConYard:return 0;
        case BldType::WarFactory:return 1;
        case BldType::OreRefinery:return 2;
        case BldType::Barracks:return 3;
        case BldType::AirForceCmd:return 4;
        case BldType::NavalYard:return 5;
        default:return 6;
    }
}

bool approach(const World& world,const std::vector<EID>& army,
    const std::vector<SkirmishAI::KnownBuilding>& objectives,Vec2i center,Vec2i& destination,
    SkirmishAI::KnownBuilding& chosen,const SkirmishAI::KnownBuilding* primary=nullptr) {
    EID anchor=INVALID_EID;float closest=1e30f;
    for(EID id:army) {
        const auto& unit=world.ents[id];
        if(!unit.alive || unit.guard || unit.deployed || unit.radDeployed || unit.immobilized() || unit.deploying())continue;
        const float d=distf(unit.x,unit.y,center.x+0.5f,center.y+0.5f);
        if(d<closest){closest=d;anchor=id;}
    }
    if(anchor==INVALID_EID)return false;
    const auto& unit=world.ents[anchor];const auto& def=unitDef(unit);int attempts=0;
    for(const auto& building:objectives) {
        // A second group pressures another observed installation of the same
        // opponent, rather than splitting a wave between unrelated houses.
        if(primary && (building.owner!=primary->owner
            || (building.x==primary->x && building.y==primary->y)))continue;
        if(attempts++>=8)break;
        std::vector<Vec2i> route;
        if(!world.map.findPathToRange((int)unit.x,(int)unit.y,building.x+0.5f,building.y+0.5f,1.5f,route,20000,
            def.pathDomain(),nullptr,unit.navLayer,-1,-1,def.fitsUnderBridge(),world.nativeGateAccess(unit)))continue;
        destination=route.empty()?Vec2i{(int)unit.x,(int)unit.y}:route.back();chosen=building;return true;
    }
    return false;
}
}

std::vector<SkirmishAI::KnownBuilding> SkirmishAI::assaultObjectives(const World& world,Vec2i center) const {
    std::vector<KnownBuilding> choices;
    for(const auto& building:knownBuildings)
        if(assaultRank(building.type)>=0 && world.map.inBounds(building.x,building.y)
            && world.isEnemy(player,building.owner) && !world.players[building.owner].defeated)choices.push_back(building);
    std::stable_sort(choices.begin(),choices.end(),[&](const auto& a,const auto& b) {
        const auto score=[&](const auto& q) {
            const int dx=q.x-center.x,dy=q.y-center.y;
            return std::tuple{assaultRank(q.type),dx*dx+dy*dy,q.y,q.x,q.owner};
        };
        return score(a)<score(b);
    });
    return choices;
}

bool SkirmishAI::issueAssault(World& world,const std::vector<EID>& army,
    const std::vector<KnownBuilding>& objectives,Vec2i center,int& accepted) {
    Vec2i destination;KnownBuilding chosen;
    if(!approach(world,army,objectives,center,destination,chosen))return false;
    const auto advance=[&](const std::vector<EID>& group,Vec2i point,const KnownBuilding& objective) {
        const EID preferred=world.bldAt(objective.x,objective.y);
        const bool visible=world.valid(preferred) && world.ents[preferred].player==objective.owner
            && world.ents[preferred].x==objective.x && world.ents[preferred].y==objective.y
            && world.ents[preferred].btype==objective.type && buildingVisible(world,world.ents[preferred]);
        int directed=0;
        std::vector<EID> ready;
        for(EID id:group) {
            const auto& unit=world.ents[id];
            if(unit.guard || unit.deployed || unit.radDeployed || unit.immobilized() || unit.deploying())continue;
            if((unit.state==UState::Attacking || unit.state==UState::Chasing) && world.valid(unit.target)) {
                const auto& current=world.ents[unit.target];
                if(current.isBuilding && buildingVisible(world,current)
                    && world.attackRejection(id,unit.target)==AttackRejectReason::None) {
                    // Keep a committed strategic attack. Reissuing it would
                    // discard pursuit progress and reset weapon bursts.
                    if(assaultRank(current.btype)<=assaultRank(objective.type))continue;
                    if(unit.state==UState::Attacking && visible
                        && world.attackRejection(id,preferred)==AttackRejectReason::None) {
                        const auto& target=world.ents[preferred];float x=target.x,y=target.y;
                        world.attackTargetPoint(unit,target,x,y);
                        const float local=std::max((float)unitDef(unit).sight,world.attackRange(unit,target));
                        // A nearby, observed strategic objective can override
                        // incidental fire on peripheral buildings. Memory alone
                        // still only grants an attack-move destination.
                        if(distf(unit.x,unit.y,x,y)<=local && world.orderAttack({id},preferred).accepted) {
                            ++directed;continue;
                        }
                    }
                }
            }
            if(unit.attackMove.active)continue;
            ready.push_back(id);
        }
        return directed+(ready.empty()?0:world.orderMove(ready,point.x+0.5f,point.y+0.5f,true).accepted);
    };
    if((difficulty==AIDiff::Hard || difficulty==AIDiff::Brutal) && army.size()>=12) {
        std::vector<EID> first,second;
        for(size_t i=0;i<army.size();++i)(i%2==0?first:second).push_back(army[i]);
        Vec2i flank;KnownBuilding other;
        if(!approach(world,second,objectives,center,flank,other,&chosen)){flank=destination;other=chosen;}
        accepted+=advance(first,destination,chosen);
        accepted+=advance(second,flank,other);
    } else accepted+=advance(army,destination,chosen);
    // A reachable objective remains in charge even when every member already
    // has a retained order. A local target must not erase that accepted work.
    return true;
}

void SkirmishAI::scoutGround(World& world,const std::vector<EID>& army) {
    // Keep the opening pair; a large force can cover several fog regions
    // while the rest remain available for contacts. Use only our own orders.
    const int limit=std::clamp((int)army.size()/6,2,8);
    std::vector<Vec2i> claimed;
    int scouts=0;
    for(EID id:army) {
        const auto& unit=world.ents[id];
        if(unit.attackMove.active || unit.state==UState::AttackMoving){++scouts;
            if(limit>2)claimed.push_back({(int)unit.attackMove.x,(int)unit.attackMove.y});}
    }
    for(EID id:army) {
        if(scouts>=limit)break;
        const auto& unit=world.ents[id];
        if(unit.state!=UState::Idle || unit.attackMove.active || unit.guard || unit.deployed || unit.radDeployed
            || unit.immobilized() || unit.deploying())continue;
        const auto goal=scoutDestination(world,unit,claimed);if(goal.x<0)continue;
        if(world.orderMove({id},goal.x+0.5f,goal.y+0.5f,true).accepted){++scouts;
            if(limit>2)claimed.push_back({(int)world.ents[id].attackMove.x,(int)world.ents[id].attackMove.y});}
    }
}
