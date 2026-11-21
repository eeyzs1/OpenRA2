#include "game/world.h"
#include <algorithm>
#include <set>

void World::updateNativeTeamTransport(int index) {
    auto& team=nativeEvents.teams[index];const auto& definition=nativeEventDefinition->teams[team.definition];
    const auto line=nativeEventDefinition->scripts[definition.script].steps[team.step];
    if(!nativeConditions.active || line.y<0 || line.y>3 || definition.transportsReturn){team.blockedScript=8;return;}
    if(!team.lineStarted){team.lineStarted=true;nativeConditions.unloading[index]={};}
    auto& keys=nativeConditions.unloading[index];
    const auto members=team.members;
    bool carrying=false;
    for(EID id:members)if(valid(id) && !ents[id].isBuilding && unitDef(ents[id]).cargoCap>0) {
        for(const auto& p:ents[id].cargo)if(!p.nativeIdentity.object.empty() && std::find(keys.begin(),keys.end(),p.nativeIdentity.object)==keys.end())keys.push_back(p.nativeIdentity.object);
        if(!ents[id].cargo.empty()) {
            if(ents[id].immobilized()){carrying=true;continue;}
            orderUnload({id});
            if(valid(id) && !ents[id].cargo.empty())carrying=true;
        }
    }
    // Join the actual released entities by source key, never by a retired
    // passenger's numeric slot and never by making replacement infantry.
    for(const auto& key:keys)for(EID id=0;id<(EID)ents.size();++id)if(valid(id) && ents[id].nativeIdentity.object==key
        && ents[id].player==team.memberHouse && std::find(team.members.begin(),team.members.end(),id)==team.members.end()) {
        bool assigned=false;for(int other=0;other<(int)nativeEvents.teams.size();++other)if(other!=index && nativeEvents.teams[other].alive
            && std::find(nativeEvents.teams[other].members.begin(),nativeEvents.teams[other].members.end(),id)!=nativeEvents.teams[other].members.end()){assigned=true;break;}
        if(!assigned){team.members.push_back(id);if(team.tag>=0)ents[id].nativeEventTag=team.tag;nativeAIJoined(index,id);}break;
    }
    team.blockedScript=carrying?8:-1;if(carrying)return;
    std::vector<EID> detached;
    std::erase_if(team.members,[&](EID id){if(!valid(id))return true;
        const bool transport=unitDef(ents[id]).cargoCap>0;
        const bool remove=transport?(line.y==2 || line.y==3):(line.y==1 || line.y==3);
        if(remove)detached.push_back(id);return remove;});
    orderStop(detached);
    advanceNativeTeamScript(index);
}
