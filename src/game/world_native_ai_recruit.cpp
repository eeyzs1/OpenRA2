#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <limits>

void World::nativeAIJoined(int team,EID actor) {
    if(!nativeAI.active || !valid(actor) || ents[actor].nativeIdentity.object.empty())return;
    const auto& d=nativeEventDefinition->teams[nativeEvents.teams[team].definition];
    if(nativeAIRules.isGlobal()){
        nativeTeamFootJoined(team,actor);
        nativeAI.memberTeams[ents[actor].nativeIdentity.object]=team;
        const int x=(int)ents[actor].x,y=(int)ents[actor].y;
        for(EID factory=0;factory<(EID)ents.size();++factory)if(valid(factory) && ents[factory].isBuilding && ents[factory].player==ents[actor].player
            && nativeFactorySupports(ents[factory],ents[actor].nativeIdentity.type)) {
            const auto exits=factoryExitCells(factory,unitDef(ents[actor]));
            if(std::any_of(exits.begin(),exits.end(),[&](const auto& point){return point.x==x && point.y==y;})){
                nativeAIClearFactoryExit(actor,factory);break;}
        }
    }
    auto& p=nativeAI.recruit[ents[actor].nativeIdentity.object];p.automatic=d.recruitable;
    p.group=d.group==-1?nativeEventDefinition->taskForces[d.taskForce].group:d.group;
}

void World::recruitNativeAITeam(int index) {
    auto& team=nativeEvents.teams[index];const auto& d=nativeEventDefinition->teams[team.definition];
    if(team.reinforced || (team.assembled && !(nativeAIRules.isGlobal() && d.reinforce)))return;
    const int group=d.group==-1?nativeEventDefinition->taskForces[d.taskForce].group:d.group;
    Vec2i origin=nativeWaypoint(d.waypoint);
    float centerX=origin.x+.5f,centerY=origin.y+.5f;
    if(!map.inBounds(origin.x,origin.y)){
        float x=0,y=0;int count=0;for(EID id:team.members)if(valid(id)){x+=ents[id].x;y+=ents[id].y;++count;}
        // The source uses COORD_NONE while an unplaced team has no member,
        // not a made-up base center. The first recruit then gives it a center.
        centerX=count?x/count:0;centerY=count?y/count:0;
    }
    for(const auto& [wanted,name]:nativeEventDefinition->taskForces[d.taskForce].units){const int type=nativeUnitIndex(name);if(type<0)continue;
        const auto matches=[&](EID id){const auto& e=ents[id];return e.nativeIdentity.definition?e.nativeIdentity.type==type:
            !nativeAI.autocreate[team.definition] && nativeTypes[type]->unit.type!=UnitType::NativeVehicle && e.utype==nativeTypes[type]->unit.type;};
        int have=0;if(nativeAIRules.isGlobal())have=nativeAITeamCount(index,type);
        else for(EID id:team.members)if(valid(id) && matches(id))++have;if(have>=wanted)continue;
        EID best=INVALID_EID;int previous=-1;int64_t distance=std::numeric_limits<int64_t>::max();
        for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];
            if(!e.alive || e.isBuilding || e.player!=team.memberHouse || e.state!=UState::Idle || e.parasiting || e.immobilized()
                || !matches(id) || (unitDef(e).isAir() && unitDef(e).ammo>0 && !e.ammo))continue;
            auto permission=nativeAI.recruit.find(e.nativeIdentity.object);const NativeRecruitState defaults{};
            const auto& p=permission==nativeAI.recruit.end()?defaults:permission->second;
            if(nativeAI.autocreate[team.definition]?!p.automatic:!p.normal)continue;
            if(group!=-2 && p.group!=group && !d.recruiter)continue;
            int assigned=-1;for(int t=0;t<(int)nativeEvents.teams.size();++t)if(nativeEvents.teams[t].alive
                && std::find(nativeEvents.teams[t].members.begin(),nativeEvents.teams[t].members.end(),id)!=nativeEvents.teams[t].members.end()){assigned=t;break;}
            if(assigned==index || (assigned>=0 && nativeEventDefinition->teams[nativeEvents.teams[assigned].definition].priority>=d.priority))continue;
            const int64_t dx=std::llround(e.x*256)-std::llround(centerX*256),dy=std::llround(e.y*256)-std::llround(centerY*256);
            const int64_t rank=dx*dx+dy*dy+(p.group!=group?50*256:0);
            if(rank<distance){distance=rank;best=id;previous=assigned;}
        }
        if(best!=INVALID_EID){if(previous>=0)std::erase(nativeEvents.teams[previous].members,best);
            if(team.tag>=0)ents[best].nativeEventTag=team.tag;
            ents[best].nativeIdentity.sleeping=false;team.members.push_back(best);nativeAIJoined(index,best);}
    }
}
