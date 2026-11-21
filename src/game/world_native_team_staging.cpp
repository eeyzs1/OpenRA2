#include "game/world.h"
#include "game/native_team_geometry.h"
#include "game/native_nearby_clearance.h"
#include <limits>

namespace {
Vec2i baseAnchor(const World& world,int house) {
    // The prototype has no persistent House.BaseCenter or IsTeamLeader yet.
    // Use the first live building's source-cell center for this opt-in path.
    // This projection is intentionally not promoted to default rules.
    const World::Ent* first=nullptr;
    for(const auto& actor:world.ents)if(actor.alive && actor.isBuilding && actor.player==house
        && (!first || actor.combatActor<first->combatActor))first=&actor;
    // Entity slots are recycled. The saved combat identity preserves creation
    // order, including after loading; a lower EID can be a much newer plant.
    return first?Vec2i{(int)((first->x+.5f)*256),(int)((first->y+.5f)*256)}:Vec2i{-1,-1};
}
Vec2i nearby(const World& world,EID lead,Vec2i point) {
    const auto& actor=world.ents[lead];const auto& type=unitDef(actor);
    const auto clear=[&](int x,int y){
        if(!world.map.inBounds(x,y))return false;
        if(type.isAir())return true;
        const int layer=world.map.preferredLayer(x,y,type.pathDomain());
        return world.movementPassable(actor,x,y,layer,true) && !world.cellHardBlockedForMove(x,y,lead,layer);
    };
    if(clear(point.x,point.y))return point;
    Vec2i best{-1,-1};int64_t distance=std::numeric_limits<int64_t>::max();
    for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x) {
        const int64_t dx=x-point.x,dy=y-point.y,d=dx*dx+dy*dy;
        if(d<distance && clear(x,y)){distance=d;best={x,y};}
    }
    return best;
}
}

void World::updateNativeTeamStaging(int index) {
    auto& team=nativeEvents.teams[index];
    const auto& definition=nativeEventDefinition->teams[team.definition];
    const int action=nativeEventDefinition->scripts[definition.script].steps[team.step].x;
    const bool starting=!team.lineStarted;
    if(starting) {
        const EID lead=nativeTeamLeader(team);
        if(!valid(lead)){advanceNativeTeamScript(index);return;}
        auto own=baseAnchor(*this,team.memberHouse);
        const int house=nativeAI.houses[team.memberHouse].sourceEnemy;
        auto enemy=house>=0 && house<numPlayers?baseAnchor(*this,house):Vec2i{-1,-1};
        bool absent=enemy.x<0;
        if(nativeAIRules.hasOriginalTeamControl()) {
            own=nativeAI.centers[team.memberHouse].center;
            enemy=house>=0 && house<numPlayers?nativeAI.centers[house].center:Vec2i{};
            absent=house<0 || house>=numPlayers;
            if(action==53 && (absent || enemy==Vec2i{})){advanceNativeTeamScript(index);return;}
            if(action==53 && own==Vec2i{})own={(int)(ents[lead].x*256),(int)(ents[lead].y*256)};
        } else {
            if(own.x<0)own={(int)(ents[lead].x*256),(int)(ents[lead].y*256)};
            if(action==53 && absent){advanceNativeTeamScript(index);return;}
        }
        const int direction=absent?rng.range(0,255)*256:-1;
        const auto requested=NativeTeamGeometry::cell(own,enemy,nativeAIRules.safeDistance,action==53,direction);
        const int speed=nativeAIRules.hasOriginalNearbyGeometry()?nativeNearbySpeed(lead):0;
        if(speed<0){team.lineStarted=true;team.blockedScript=action;return;}
        const auto point=nativeAIRules.hasOriginalNearbyGeometry()?nativeNearbyPoint(requested,speed,3):nearby(*this,lead,requested);
        team.lineStarted=true;
        if(point.x<0){team.blockedScript=action;return;}
        team.focusCell=nativeAIRules.hasOriginalNearbyGeometry()?nativeNearbyPack(point):cellIdx(point.x,point.y);
        const auto& type=unitDef(ents[lead]);
        team.focusLayer=type.isAir()?0:map.preferredLayer(point.x,point.y,type.pathDomain());
    }
    if(team.focusCell<0)return;
    const auto point=nativeAIRules.hasOriginalNearbyGeometry()?nativeNearbyUnpack(team.focusCell):Vec2i{team.focusCell%map.w,team.focusCell/map.w};
    const float x=point.x+.5f,y=point.y+.5f,
        stray=nativeAIRules.hasOriginalTeamControl()?nativeEventDefinition->relaxedStrayDistance:nativeEventDefinition->strayDistance;
    bool arrived=true,blocked=false;const bool retry=tick>=team.waitUntil;
    for(EID id:team.members)if(valid(id)) {
        const auto& actor=ents[id];
        if((actor.airborne() || actor.navLayer==team.focusLayer) && distf(actor.x,actor.y,x,y)<=stray)continue;
        if(definition.aggressive && valid(actor.target) && (actor.state==UState::Attacking || actor.state==UState::Chasing))continue;
        arrived=false;
        if(actor.state==UState::Moving && actor.pathIdx<(int)actor.path.size() && actor.goalLayer==team.focusLayer
            && distf(actor.goalX,actor.goalY,x,y)<=stray)continue;
        if(retry) {
            const auto result=orderMove({id},x,y,false,false,false,team.focusLayer);
            blocked |= result.accepted==0 || ents[id].goalLayer!=team.focusLayer || distf(ents[id].goalX,ents[id].goalY,x,y)>stray;
        }
    }
    if(arrived){if(!starting)advanceNativeTeamScript(index);return;}
    if(retry){team.waitUntil=tick+LOGIC_FPS;team.blockedScript=blocked?action:-1;}
}
