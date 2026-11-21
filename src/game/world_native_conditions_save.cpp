#include "game/world.h"
#include "core/binary_state_io.h"
#include <algorithm>
#include <set>

namespace {
bool present(const auto& entries,int index){return index==-1 || std::any_of(entries.begin(),entries.end(),[&](const auto& row){return row.first==index;});}
std::string readConditionKey(BinaryStateStream& stream) {
    std::string key;stream.readText(key,128);
    if(key.empty())stream.ok=false;
    return key;
}
}

bool World::validateNativeConditionState() const {
    const auto& s=nativeConditions;
    const bool enabled=nativeEventDefinition && nativeEventDefinition->extendedConditions && nativeEvents.active;
    if(s.active!=enabled)return false;
    if(!s.active)return s.lastBuilt.empty() && s.discoveredHumans.empty() && s.unloading.empty()
        && std::all_of(s.locals.begin(),s.locals.end(),[](auto v){return v==0;});
    if(s.lastBuilt.size()!=(size_t)numPlayers || s.discoveredHumans.size()>65536 || s.unloading.size()>4096)return false;
    for(const auto& built:s.lastBuilt){if(!present(nativeEventDefinition->buildingTypes,built[0]))return false;
        for(int category=0;category<3;++category)if(!present(nativeEventDefinition->mobileTypes[category],built[category+1]))return false;}
    for(auto v:s.locals)if(v>1)return false;
    std::set<std::string> keys;
    const auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->void{if(!p.nativeIdentity.object.empty())keys.insert(p.nativeIdentity.object);for(const auto& c:p.cargo)self(self,c);};
    for(const auto& e:ents)if(e.alive){if(!e.nativeIdentity.object.empty())keys.insert(e.nativeIdentity.object);
        for(const auto& p:e.cargo)passenger(passenger,p);for(const auto& p:e.garrison)passenger(passenger,p);}
    uint8_t humans=0;for(int p=0;p<numPlayers;++p)if(players[p].active && !players[p].isAI)humans|=(uint8_t)(1u<<p);
    for(const auto& [key,mask]:s.discoveredHumans)if(key.empty() || key.size()>128 || !keys.contains(key) || !mask || (mask&~humans))return false;
    size_t pending=0;
    for(const auto& [index,cargo]:s.unloading){if(index<0 || index>=(int)nativeEvents.teams.size() || cargo.size()>16384 || (pending+=cargo.size())>65536)return false;
        const auto& team=nativeEvents.teams[index];if(!team.alive || !team.assembled || !team.lineStarted)return false;
        const auto& script=nativeEventDefinition->scripts[nativeEventDefinition->teams[team.definition].script];
        if(team.step<0 || team.step>=(int)script.steps.size() || script.steps[team.step].x!=8)return false;
        std::set<std::string> unique;for(const auto& key:cargo)if(key.empty() || key.size()>128 || !unique.insert(key).second)return false;}
    return true;
}

bool World::writeNativeConditionState(FILE* file) const {
    if(!validateNativeConditionState())return false;
    BinaryStateStream io{file};
    const uint8_t available=nativeEventDefinition && nativeEventDefinition->extendedConditions;io.write(available);if(!available)return io.ok;
    const auto& s=nativeConditions;const uint8_t active=s.active;io.write(active);if(!active)return io.ok;
    for(const auto& row:s.lastBuilt)for(int value:row)io.write(value);
    for(auto value:s.locals)io.write(value);
    io.write((uint32_t)s.discoveredHumans.size());for(const auto& [name,mask]:s.discoveredHumans){io.writeText(name);io.write(mask);}
    io.write((uint32_t)s.unloading.size());for(const auto& [index,cargo]:s.unloading){io.write(index);io.write((uint32_t)cargo.size());for(const auto& name:cargo)io.writeText(name);}
    return io.ok;
}

bool World::readNativeConditionState(FILE* file) {
    nativeConditions={};BinaryStateStream io{file};uint8_t available=0,active=0;io.read(available);
    if(!io.ok || available>1 || (available!=0)!=(nativeEventDefinition && nativeEventDefinition->extendedConditions))return false;
    if(!available)return validateNativeConditionState();io.read(active);if(!io.ok || active>1)return false;
    auto& s=nativeConditions;s.active=active!=0;if(!active)return validateNativeConditionState();
    s.lastBuilt.resize(numPlayers);for(auto& row:s.lastBuilt)for(int& value:row)io.read(value);
    for(auto& value:s.locals)io.read(value);
    uint32_t count=0;io.read(count);if(!io.ok || count>65536)return false;
    for(uint32_t i=0;i<count;++i){const auto key=readConditionKey(io);uint8_t mask=0;io.read(mask);if(!io.ok || !s.discoveredHumans.emplace(key,mask).second)return false;}
    io.read(count);if(!io.ok || count>4096)return false;
    uint32_t pending=0;
    for(uint32_t i=0;i<count;++i){int index=-1;uint32_t size=0;io.read(index);io.read(size);if(!io.ok || size>16384 || size>65536-pending || s.unloading.contains(index))return false;pending+=size;
        auto& cargo=s.unloading[index];for(uint32_t j=0;j<size;++j){cargo.push_back(readConditionKey(io));if(!io.ok)return false;}}
    return io.ok && validateNativeConditionState();
}

uint32_t World::nativeConditionChecksum() const {
    uint32_t h=2166136261u;const auto byte=[&](uint8_t v){h=(h^v)*16777619u;};
    const auto number=[&](uint32_t v){for(int i=0;i<4;++i){byte((uint8_t)v);v>>=8;}};
    const auto key=[&](const std::string& v){number((uint32_t)v.size());for(unsigned char c:v)byte(c);};
    const auto& s=nativeConditions;byte((uint8_t)s.active);if(!s.active)return h;
    for(const auto& row:s.lastBuilt)for(int v:row)number((uint32_t)v);for(auto v:s.locals)byte(v);
    number((uint32_t)s.discoveredHumans.size());for(const auto& [name,mask]:s.discoveredHumans){key(name);byte(mask);}
    number((uint32_t)s.unloading.size());for(const auto& [index,cargo]:s.unloading){number(index);number((uint32_t)cargo.size());for(const auto& name:cargo)key(name);}
    return h;
}
