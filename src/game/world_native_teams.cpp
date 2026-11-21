#include "game/world.h"
#include <algorithm>
#include <set>

int World::nativeTeamBlocker(int index) const {
    if(!nativeEventDefinition || index<0 || index>=(int)nativeEventDefinition->teams.size() || nativeEvents.teams.size()>=4096)return 1;
    const auto& team=nativeEventDefinition->teams[index];
    if(team.loadable || team.prebuild || (team.reinforce && !nativeAIRules.isGlobal()))return 2;
    // Creating a team does not execute its whole script. Unsupported future
    // lines are reported on the live script cursor, after earlier lines run.
    for(const auto& [count,id]:nativeEventDefinition->taskForces[team.taskForce].units)if(nativeUnitIndex(id)<0)return 4;
    return -1;
}

bool World::nativeTeamPlacements(int index,int waypoint,std::vector<std::pair<int,Vec2i>>& placements,const std::vector<uint8_t>* reserved) const {
    placements.clear();if(nativeTeamBlocker(index)!=-1)return false;
    const auto& definition=nativeEventDefinition->teams[index];
    const auto origin=nativeWaypoint(waypoint<0?definition.waypoint:waypoint);std::set<size_t> chosen;
    if(!map.inBounds(origin.x,origin.y))return false;
        for(const auto& [count,id]:nativeEventDefinition->taskForces[definition.taskForce].units) {
            const int type=nativeUnitIndex(id);const auto& unit=nativeTypes[type]->unit;
            for(int member=0;member<count;++member) {
                bool found=false;
                for(int radius=0;radius<=6 && !found;++radius)for(int y=-radius;y<=radius && !found;++y)for(int x=-radius;x<=radius && !found;++x) {
                    if(std::max(abs(x),abs(y))!=radius)continue;const int tx=origin.x+x,ty=origin.y+y;
                    const int layer=map.preferredLayer(tx,ty,unit.pathDomain());
                    if(!map.inBounds(tx,ty))continue;const auto slot=movementSlot(tx,ty,layer);
                    if(chosen.contains(slot) || (reserved && slot<reserved->size() && (*reserved)[slot])
                        || !map.surfacePassable(tx,ty,unit.pathDomain(),layer,true,unit.fitsUnderBridge()) || unitAtCell(tx,ty,layer)!=INVALID_EID)continue;
                    placements.push_back({type,{tx,ty}});chosen.insert(slot);found=true;
                }
                if(!found)return false;
            }
        }
    return true;
}

int World::createNativeTeam(int index,bool reinforce,int waypoint,const std::vector<std::pair<int,Vec2i>>* prepared) {
    if(nativeTeamBlocker(index)!=-1)return -1;
    const auto& definition=nativeEventDefinition->teams[index];
    NativeTeamState instance;instance.definition=index;instance.reinforced=reinforce;instance.memberHouse=definition.house;
    std::vector<std::pair<int,Vec2i>> placements;
    if(reinforce){if(prepared)placements=*prepared;else if(!nativeTeamPlacements(index,waypoint,placements))return -1;}
    if(definition.tag>=0){instance.tag=nativeEventTagInstance(definition.tag,true);if(instance.tag<0)return -1;}
    const int team=(int)nativeEvents.teams.size();nativeEvents.teams.push_back(instance);
    nativeAITeamCreated(team);
    for(const auto& [type,point]:placements) {
        const std::string object="team:"+std::to_string(team)+":"+std::to_string(nativeEvents.teams[team].members.size());
        const std::string tag=definition.tag<0?"":nativeEventDefinition->tags[definition.tag].id;
        const auto id=spawnNativeActor(type,definition.house,point.x+.5f,point.y+.5f,256,0,object,tag,"Guard");
        if(!valid(id))return -1;ents[id].nativeEventTag=instance.tag;
        ents[id].vetRank=std::clamp(definition.veterancy-1,0,2);
        nativeEvents.teams[team].members.push_back(id);
        nativeAIJoined(team,id);
    }
    return team;
}

void World::updateNativeTeams() {
    if(!nativeEvents.active || !nativeEventDefinition)return;
    pruneNativeTeamFeet();
    if(nativeAIRules.isGlobal())for(EID actor=0;actor<(EID)ents.size();++actor)if(valid(actor) && !ents[actor].isBuilding) {
        const auto found=nativeAI.memberTeams.find(ents[actor].nativeIdentity.object);
        if(found==nativeAI.memberTeams.end() || found->second<0 || found->second>=(int)nativeEvents.teams.size())continue;
        auto& team=nativeEvents.teams[found->second];
        if(team.alive && team.memberHouse==ents[actor].player && std::find(team.members.begin(),team.members.end(),actor)==team.members.end()) {
            // Unloading allocates a new EID. Recover the existing membership
            // through its source key, never a retired or recycled slot.
            team.members.push_back(actor);if(team.tag>=0)ents[actor].nativeEventTag=team.tag;
        }
    }
    std::set<EID> assigned;
    for(const auto& team:nativeEvents.teams)if(team.alive)for(auto id:team.members)if(valid(id))assigned.insert(id);
    const int count=(int)nativeEvents.teams.size();
    for(int index=0;index<count;++index) {
        auto& team=nativeEvents.teams[index];if(!team.alive)continue;const auto& definition=nativeEventDefinition->teams[team.definition];
        std::erase_if(team.members,[&](EID id){return !valid(id) || ents[id].player!=team.memberHouse;});
        if(team.assembled && team.members.empty()){
            bool carried=false;if(nativeAIRules.isGlobal())for(const auto& [count,name]:nativeEventDefinition->taskForces[definition.taskForce].units)
                carried |= nativeAITeamCount(index,nativeUnitIndex(name))>0;
            if(!carried && !(nativeAIRules.isGlobal() && definition.reinforce)){finishNativeTeam(index);continue;}
        }
        if(nativeAI.active)recruitNativeAITeam(index);
        else if(!team.reinforced && !team.assembled)for(const auto& [count,id]:nativeEventDefinition->taskForces[definition.taskForce].units) {
            const int type=nativeUnitIndex(id);if(type<0)continue;int have=0;
            const auto matches=[&](EID actor){const auto& e=ents[actor];
                return e.nativeIdentity.definition?e.nativeIdentity.type==type:
                    nativeTypes[type]->unit.type!=UnitType::NativeVehicle && unitDef(e).type==nativeTypes[type]->unit.type;};
            for(EID actor:team.members)if(valid(actor) && matches(actor))++have;
            for(EID actor=0;actor<(EID)ents.size() && have<count;++actor) {
                auto& e=ents[actor];if(!e.alive || e.isBuilding || e.player!=definition.house || assigned.contains(actor)
                    || e.state!=UState::Idle || !matches(actor))continue;
                e.nativeEventTag=team.tag;e.nativeIdentity.sleeping=false;team.members.push_back(actor);assigned.insert(actor);++have;
            }
        }
        if(!team.assembled) {
            int wanted=0;for(const auto& [count,id]:nativeEventDefinition->taskForces[definition.taskForce].units)wanted+=count;
            team.assembled=!team.members.empty() && (team.reinforced || (int)team.members.size()==wanted);
        }
        updateNativeTeamStrength(index);
        if(team.assembled)updateNativeTeamScript(index);
    }
}
