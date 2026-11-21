#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
EID leader(const World& world,const NativeTeamState& team) {
    if(world.nativeAIRules.hasOriginalTeamControl())return world.nativeTeamLeader(team);
    for(auto it=team.members.rbegin();it!=team.members.rend();++it)if(world.valid(*it))return *it;
    return INVALID_EID;
}
Vec2i destination(const World& world,EID lead,Vec2i point) {
    if(!world.valid(lead) || !world.map.inBounds(point.x,point.y))return {-1,-1};
    const auto& actor=world.ents[lead];const auto& type=unitDef(actor);
    const auto clear=[&](int x,int y) {
        if(!world.map.inBounds(x,y))return false;
        if(type.isAir())return true;
        const int layer=world.map.preferredLayer(x,y,type.pathDomain());
        return world.movementPassable(actor,x,y,layer,true) && !world.cellHardBlockedForMove(x,y,lead,layer);
    };
    if(clear(point.x,point.y))return point;
    // Select once by clearance, independently of route reachability.
    // A disconnected nearest cell must not become a completed move.
    Vec2i best{-1,-1};int distance=std::numeric_limits<int>::max();
    for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x) {
        const int dx=x-point.x,dy=y-point.y,d=dx*dx+dy*dy;
        if(d<distance && clear(x,y)){distance=d;best={x,y};}
    }
    return best;
}
EID center(const World& world,const NativeTeamState& team) {
    float x=0,y=0;int count=0;
    for(EID id:team.members)if(world.valid(id)){x+=world.ents[id].x;y+=world.ents[id].y;++count;}
    if(!count)return INVALID_EID;x/=count;y/=count;
    EID best=INVALID_EID;float distance=std::numeric_limits<float>::max();
    for(auto it=team.members.rbegin();it!=team.members.rend();++it)if(world.valid(*it)) {
        const auto& e=world.ents[*it];const float d=distf(x,y,e.x,e.y);
        if(d<distance){distance=d;best=*it;}
    }
    return best;
}
bool goingNear(const World::Ent& actor,float x,float y,int layer,float stray) {
    return actor.state==UState::Moving && actor.pathIdx<(int)actor.path.size()
        && actor.goalLayer==layer && distf(actor.goalX,actor.goalY,x,y)<=stray;
}
}

void World::releaseNativePlayerUnits(const std::vector<int>& actors) {
    if(!nativeEvents.active || actors.empty())return;
    if(nativeAIRules.hasOriginalTeamControl())nativeTeamMemberCounts.clear();
    if(nativeAIRules.hasOriginalTeamControl())for(EID id:actors)if(valid(id)) {
        nativeAI.memberTeams.erase(ents[id].nativeIdentity.object);nativeAI.feet.erase(ents[id].nativeIdentity.object);
    }
    for(int index=0;index<(int)nativeEvents.teams.size();++index) {
        auto& team=nativeEvents.teams[index];if(!team.alive)continue;
        std::erase_if(team.members,[&](EID actor){return std::find(actors.begin(),actors.end(),actor)!=actors.end();});
        if(team.assembled && team.members.empty())finishNativeTeam(index);
    }
    // Death/object tags remain on released actors, including Tanya's failure tag.
}

void World::updateNativeTeamOrders(int index) {
    auto& team=nativeEvents.teams[index];const auto& definition=nativeEventDefinition->teams[team.definition];
    const auto line=nativeEventDefinition->scripts[definition.script].steps[team.step];
    const float stray=nativeEventDefinition->strayDistance;
    if(line.x==20) {
        if(line.y<0 || line.y>1023){team.blockedScript=20;return;}
        if(team.lineStarted){advanceNativeTeamScript(index);return;}
        const int owner=nativeHouseNumber(line.y);
        if(owner>=0) {
            for(EID id:team.members)if(valid(id) && ents[id].player!=owner) {
                orderStop({id});auto& actor=ents[id];changeActorOwner(id,owner);actor.nativeIdentity.sleeping=false;
                actor.feedbackAttackTarget=actor.feedbackEntryTarget=INVALID_EID;
            }
            team.memberHouse=owner;synchronizeBuildingOwners();
        }
        // The team keeps its original defining house; its members may change.
        team.lineStarted=true;return;
    }
    if(line.x==3) {
        const bool starting=!team.lineStarted;
        if(starting) {
            team.lineStarted=true;const EID lead=leader(*this,team);
            const auto point=destination(*this,lead,nativeWaypoint(line.y));
            if(point.x<0){team.blockedScript=3;return;}
            team.focusCell=cellIdx(point.x,point.y);
            team.focusLayer=unitDef(ents[lead]).isAir()?0:map.preferredLayer(point.x,point.y,unitDef(ents[lead]).pathDomain());
        }
        if(team.focusCell<0)return;
        const float x=team.focusCell%map.w+.5f,y=team.focusCell/map.w+.5f;
        bool arrived=true,blocked=false;const bool retry=tick>=team.waitUntil;
        for(EID id:team.members)if(valid(id)) {
            const auto& actor=ents[id];
            if((actor.airborne() || actor.navLayer==team.focusLayer) && distf(actor.x,actor.y,x,y)<=stray)continue;
            if(definition.aggressive && valid(actor.target) && (actor.state==UState::Attacking || actor.state==UState::Chasing))continue;
            arrived=false;
            if(goingNear(actor,x,y,team.focusLayer,stray))continue;
            if(retry) {
                // Single-member requests converge inside Stray rather than
                // spreading a large formation beyond the completion radius.
                const auto result=orderMove({id},x,y,false,false,false,team.focusLayer);
                blocked |= result.accepted==0 || ents[id].goalLayer!=team.focusLayer
                    || distf(ents[id].goalX,ents[id].goalY,x,y)>stray;
            }
        }
        if(arrived){if(!starting)advanceNativeTeamScript(index);return;}
        if(retry){team.waitUntil=tick+LOGIC_FPS;team.blockedScript=blocked?3:-1;}
        return;
    }
    if(line.x!=5)return;
    if(line.y<0 || line.y>86400){team.blockedScript=5;return;}
    if(line.y==0) {
        // Preserve the source miner's normal harvest behavior on Guard(0).
        if(team.lineStarted)advanceNativeTeamScript(index);else team.lineStarted=true;
        return;
    }
    const bool starting=!team.lineStarted;
    if(starting){team.lineStarted=true;team.waitUntil=tick+(uint64_t)line.y*LOGIC_FPS;}
    else if(tick>team.waitUntil){advanceNativeTeamScript(index);return;}
    const EID anchor=center(*this,team);if(!valid(anchor)){team.blockedScript=5;return;}
    team.focus=anchor;team.focusCell=cellIdx((int)ents[anchor].x,(int)ents[anchor].y);team.focusLayer=ents[anchor].navLayer;
    const float x=ents[anchor].x,y=ents[anchor].y;
    for(EID id:team.members)if(valid(id) && !ents[id].immobilized()) {
        auto& actor=ents[id];
        if((actor.airborne() || actor.navLayer==team.focusLayer) && distf(actor.x,actor.y,x,y)<=stray) {
            if(!actor.guard){orderStop({id});orderGuard({id});actor.guard=true;}
        } else if((starting || (tick+(uint64_t)index)%LOGIC_FPS==0) && !goingNear(actor,x,y,team.focusLayer,stray)) {
            orderMove({id},x,y,false,false,false,team.focusLayer);
        }
    }
    team.blockedScript=-1;
}
