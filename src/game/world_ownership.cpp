#include "game/world.h"
#include <algorithm>

bool World::hasAttackMoveState(const Ent& actor) {
    return actor.attackMove.active || std::any_of(actor.wps.begin(),actor.wps.end(),[](const auto& point){return point.attackMode>=0;});
}

void World::changeActorOwner(EID id,int owner) {
    // Retirement can finalize an allocated slot before its lifecycle ends.
    if(id<0 || id>=(EID)ents.size() || owner< -1 || owner>=numPlayers)return;
    auto& actor=ents[id];if(actor.player==owner)return;
    if(hasAttackMoveState(actor)){actor.attackMove={};actor.wps.clear();}
    actor.player=owner;
    actor.cancelChronoReturn();
    actor.pendingDockRefinery={};
    if(nativeGroundEnabled() && actor.isBuilding && actor.btype==BldType::OreRefinery) {
        const auto receiver=entityRef(id);
        for(auto& miner:ents)if(miner.alive && miner.player!=owner) {
            if(miner.pendingDockRefinery==receiver)miner.pendingDockRefinery={};
            if(nativeTravelEnabled() && miner.nativeDock.receiver==receiver)miner.cancelChronoReturn();
        }
    }
    if(!actor.isBuilding || !actor.nativeIdentity.definition || !actor.nativeIdentity.definition->gate
        || map.gateCells.size()!=map.cells.size())return;
    const auto& definition=bldDef(actor);
    for(int dy=0;dy<definition.h;++dy)for(int dx=0;dx<definition.w;++dx) {
        const int x=(int)actor.x+dx,y=(int)actor.y+dy;if(!map.inBounds(x,y))continue;
        auto& cell=map.gateCells[cellIdx(x,y)];if(cell.owner==id)cell.house=owner;
    }
}

void World::synchronizeBuildingOwners() {
    // Whole-house/script actions finish their batch here. Factory-job
    // cancellation keeps the existing action/production scheduling policy.
    rebuildNativeGates();recomputePower();
}
