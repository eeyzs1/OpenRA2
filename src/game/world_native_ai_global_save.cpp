#include "game/world.h"
#include "core/binary_state_io.h"
#include <set>

namespace {
bool transfer(BinaryStateIO& io,NativeAIState& state,const NativeGlobalAIContract& contract,int houses,int teams) {
    const uint32_t expected=contract.stateVersion;
    uint32_t magic=0x4c474941,version=expected,count=(uint32_t)state.houses.size();io.value(magic);io.value(version);io.value(count);
    if(!io.ok || magic!=0x4c474941 || version!=expected || count!=state.houses.size())return false;
    for(auto& house:state.houses){uint8_t configured=house.sourceConfigured;io.value(configured);if(configured>1)return false;
        house.sourceConfigured=configured!=0;io.value(house.sourceDifficulty);io.value(house.sourceEnemy);}
    count=(uint32_t)state.memberTeams.size();io.value(count);if(!io.ok || count>65536)return false;
    if(io.reading){state.memberTeams.clear();for(uint32_t i=0;i<count;++i){std::string key;int team=-1;io.text(key);io.value(team);
        if(!io.ok || !state.memberTeams.emplace(key,team).second)return false;}}
    else for(const auto& [key,index]:state.memberTeams){auto text=key;auto team=index;io.text(text);io.value(team);}
    if(!contract.houseControl)return io.ok;
    count=(uint32_t)state.centers.size();io.value(count);if(!io.ok || count!=(uint32_t)houses)return false;
    if(io.reading)state.centers.resize(count);
    for(auto& house:state.centers){uint8_t initialized=house.initialized;io.value(initialized);if(initialized>1)return false;
        house.initialized=initialized!=0;io.value(house.center.x);io.value(house.center.y);}
    io.value(state.nextFoot);count=(uint32_t)state.feet.size();io.value(count);if(!io.ok || count>65536)return false;
    if(io.reading){state.feet.clear();for(uint32_t i=0;i<count;++i){std::string key;NativeTeamFootState foot;uint8_t leader=0;
        io.text(key);io.value(foot.joined);io.value(leader);if(!io.ok || leader>1)return false;foot.leader=leader!=0;
        if(!state.feet.emplace(key,foot).second)return false;}}
    else for(const auto& [key,foot]:state.feet){auto text=key;auto joined=foot.joined;uint8_t leader=foot.leader;io.text(text);io.value(joined);io.value(leader);}
    count=(uint32_t)state.underStrength.size();io.value(count);if(!io.ok || count!=(uint32_t)teams || count>4096)return false;
    if(io.reading)state.underStrength.resize(count);
    for(auto& under:state.underStrength){io.value(under);if(under>1)return false;}
    return io.ok;
}
}

bool World::validateNativeAIGlobalState() const {
    if(!nativeAIRules.hasOriginalTeamControl()) {
        if(!nativeAI.centers.empty() || !nativeAI.feet.empty() || nativeAI.nextFoot || !nativeAI.underStrength.empty())return false;
    } else {
        if(!nativeAIRules.isGlobal() || nativeSceneVersion!=nativeAIRules.globalContract->sceneVersion || nativeAI.centers.size()!=(size_t)numPlayers
            || nativeAI.feet.size()!=nativeAI.memberTeams.size() || nativeAI.underStrength.size()!=nativeEvents.teams.size()
            || nativeAI.nextFoot==UINT64_MAX)return false;
        // A real original Foot query can select the default cell just beyond
        // the project canvas. Bound the source query domain like staging.
        const int padding=nativeAIRules.globalContract->queryPadding;
        for(const auto& house:nativeAI.centers)if(!house.initialized || house.center.x<0 || house.center.y<0
            || house.center.x>=(map.w+padding)*256 || house.center.y>=(map.h+padding)*256)return false;
        std::set<uint64_t> joined;
        for(const auto& [key,foot]:nativeAI.feet)if(!nativeAI.memberTeams.contains(key) || !foot.joined || foot.joined>nativeAI.nextFoot || !joined.insert(foot.joined).second)return false;
        for(auto under:nativeAI.underStrength)if(under>1)return false;
    }
    if(!nativeAIRules.isGlobal()){if(!nativeAI.memberTeams.empty())return false;
        for(const auto& h:nativeAI.houses)if(h.sourceConfigured || h.sourceDifficulty!=1 || h.sourceEnemy!=-1)return false;return true;}
    if(!nativeEventDefinition || !nativeEventDefinition->aiOnly || nativeSceneVersion<38 || !nativeAI.jobs.empty() || nativeAI.memberTeams.size()>65536)return false;
    for(int house=0;house<(int)nativeAI.houses.size();++house){const auto& h=nativeAI.houses[house];
        if(h.sourceDifficulty<0 || h.sourceDifficulty>2 || h.sourceEnemy < -1 || h.sourceEnemy>=numPlayers || h.sourceEnemy==house
            || (!h.sourceConfigured && (h.enabled || h.production)))return false;}
    for(const auto& [key,team]:nativeAI.memberTeams)if(key.empty() || key.size()>128 || team<0 || team>=(int)nativeEvents.teams.size() || !nativeEvents.teams[team].alive)return false;
    return true;
}
bool World::writeNativeAIGlobalState(FILE* file) const {
    if(!nativeAIRules.isGlobal() || !validateNativeAIGlobalState())return false;
    auto state=nativeAI;BinaryStateIO io{file};return transfer(io,state,*nativeAIRules.globalContract,numPlayers,(int)nativeEvents.teams.size());
}
bool World::readNativeAIGlobalState(FILE* file) {
    if(!nativeAIRules.isGlobal())return false;BinaryStateIO io{file,true};
    if(!transfer(io,nativeAI,*nativeAIRules.globalContract,numPlayers,(int)nativeEvents.teams.size()) || !validateNativeAIGlobalState())return false;
    rebuildNativeHouseInventory();return true;
}
