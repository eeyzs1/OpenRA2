#include "game/world.h"
#include "game/native_team_leader.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace {
std::string lower(std::string value){for(char& c:value)c=(char)std::tolower((unsigned char)c);return value;}
std::map<std::string_view,int> trackedOwners(const World& world) {
    std::map<std::string_view,int> result;
    const auto add=[&](const std::string& key,int owner){const auto it=world.nativeAI.memberTeams.find(key);if(it!=world.nativeAI.memberTeams.end())result.emplace(it->first,owner);};
    const auto collect=[&](auto&& self,const World::Ent::GarrisonedUnit& passenger,int owner)->void{add(passenger.nativeIdentity.object,owner);for(const auto& child:passenger.cargo)self(self,child,owner);};
    for(const auto& actor:world.ents)if(actor.alive) {
        add(actor.nativeIdentity.object,actor.player);for(const auto& p:actor.cargo)collect(collect,p,actor.player);for(const auto& p:actor.garrison)collect(collect,p,actor.player);
    }
    return result;
}
}

EID World::nativeTeamLeader(const NativeTeamState& team) const {
    if(!nativeAIRules.hasOriginalTeamControl()) {
        for(auto it=team.members.rbegin();it!=team.members.rend();++it)if(valid(*it))return *it;
        return INVALID_EID;
    }
    std::vector<std::pair<uint64_t,NativeTeamLeaderCandidate>> members;
    for(EID id:team.members)if(valid(id)) {
        const auto& actor=ents[id];const auto& key=actor.nativeIdentity.object;
        const auto foot=nativeAI.feet.find(key);if(foot==nativeAI.feet.end() || !actor.nativeIdentity.definition)continue;
        const auto name=lower(actor.nativeIdentity.definition->id);const auto rating=nativeAIRules.leadership.find(name);
        const bool aircraft=std::any_of(nativeEventDefinition->mobileTypes[2].begin(),nativeEventDefinition->mobileTypes[2].end(),[&](const auto& entry){return lower(entry.second)==name;});
        members.push_back({foot->second.joined,{id,rating==nativeAIRules.leadership.end()?5:rating->second,actor.hp,true,actor.parasiting,foot->second.leader,aircraft}});
    }
    std::sort(members.begin(),members.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    std::vector<NativeTeamLeaderCandidate> ordered;for(const auto& member:members)ordered.push_back(member.second);
    return nativeSelectTeamLeader(ordered);
}

void World::nativeTeamFootJoined(int team,EID actor) {
    if(!nativeAIRules.hasOriginalTeamControl() || !valid(actor))return;
    const auto& key=ents[actor].nativeIdentity.object;
    const auto old=nativeAI.memberTeams.find(key);
    if(old!=nativeAI.memberTeams.end() && old->second==team && nativeAI.feet.contains(key))return;
    const auto live=trackedOwners(*this);
    bool first=true;
    for(const auto& [other,owner]:nativeAI.memberTeams)if(other!=key && owner==team) {
        const auto found=live.find(other);
        if(found!=live.end() && found->second==nativeEvents.teams[team].memberHouse){first=false;break;}
    }
    if(nativeAI.nextFoot==UINT64_MAX)return;
    nativeTeamMemberCounts.clear();
    nativeAI.feet[key]={++nativeAI.nextFoot,first};
}

void World::pruneNativeTeamFeet() {
    if(!nativeAIRules.hasOriginalTeamControl())return;
    const auto live=trackedOwners(*this);
    nativeTeamMemberCounts.assign(nativeEvents.teams.size(),0);
    std::erase_if(nativeAI.memberTeams,[&](const auto& entry){if(live.contains(entry.first))return false;nativeAI.feet.erase(entry.first);return true;});
    for(const auto& [key,team]:nativeAI.memberTeams)if(team>=0 && team<(int)nativeEvents.teams.size()) {
        const auto owner=live.find(key);
        if(owner!=live.end() && owner->second==nativeEvents.teams[team].memberHouse)++nativeTeamMemberCounts[team];
    }
}

void World::updateNativeTeamStrength(int index) {
    if(!nativeAIRules.hasOriginalTeamControl() || index<0 || index>=(int)nativeAI.underStrength.size())return;
    const auto& team=nativeEvents.teams[index];const auto& definition=nativeEventDefinition->teams[team.definition];
    if(nativeTeamMemberCounts.size()!=nativeEvents.teams.size())pruneNativeTeamFeet();
    int wanted=0;const int have=nativeTeamMemberCounts[index];
    for(const auto& [count,name]:nativeEventDefinition->taskForces[definition.taskForce].units)wanted+=count;
    const bool under=definition.guardSlower && wanted>2?have<=wanted/3:have<wanted;
    if(nativeAI.underStrength[index] && !under)for(const auto& [key,owner]:nativeAI.memberTeams)if(owner==index)nativeAI.feet[key].leader=true;
    nativeAI.underStrength[index]=under;
}
