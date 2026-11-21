#include "game/world.h"
#include "core/binary_state_io.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace {
bool transfer(BinaryStateIO& io,NativeAIState& s){io.flag(s.active);if(!io.ok || !s.active)return io.ok;
    if(!io.vector(s.houses,8))return false;for(auto& h:s.houses){io.flag(h.enabled);io.flag(h.production);io.value(h.ratio);io.value(h.nextPass);io.value(h.nextChoice);for(auto& type:h.wanted)io.value(type);}
    if(!io.vector(s.triggers,4096))return false;for(auto& t:s.triggers){io.value(t.weight);io.value(t.runs);io.value(t.successes);}
    if(!io.vector(s.autocreate,4096))return false;for(auto& v:s.autocreate)io.value(v);
    if(!io.vector(s.teamBorn,4096))return false;for(auto& v:s.teamBorn)io.value(v);
    uint32_t count=(uint32_t)s.recruit.size();io.value(count);if(!io.ok || count>65536)return false;
    if(io.reading){s.recruit.clear();for(uint32_t i=0;i<count;++i){std::string key;NativeRecruitState p;io.text(key);io.flag(p.normal);io.flag(p.automatic);io.value(p.group);
        if(!io.ok || !s.recruit.emplace(key,p).second)return false;}}
    else for(const auto& entry:s.recruit){auto key=entry.first;auto p=entry.second;io.text(key);io.flag(p.normal);io.flag(p.automatic);io.value(p.group);}
    if(!io.vector(s.jobs,65536))return false;for(auto& j:s.jobs){io.value(j.actor);io.value(j.house);io.value(j.type);io.value(j.progress);io.value(j.work);io.value(j.paid);io.text(j.object);}
    return io.ok;
}
}

bool World::validateNativeAIState() const {
    if(!nativeAI.active)return nativeAI.houses.empty() && nativeAI.triggers.empty() && nativeAI.autocreate.empty()
        && nativeAI.teamBorn.empty() && nativeAI.recruit.empty() && nativeAI.jobs.empty() && (!nativeAIRules.available || !nativeEvents.active);
    if(!nativeAIRules.available || !nativeEvents.active || !nativeEventDefinition || nativeAI.houses.size()!=(size_t)numPlayers
        || nativeAI.triggers.size()!=nativeAIRules.triggers.size() || nativeAI.autocreate.size()!=nativeEventDefinition->teams.size()
        || nativeAI.teamBorn.size()!=nativeEvents.teams.size() || nativeAI.recruit.size()>65536 || nativeAI.jobs.size()>ents.size())return false;
    const uint64_t maxDelay=2ull*(*std::max_element(nativeAIRules.delays.begin(),nativeAIRules.delays.end())+175ull*numPlayers);
    for(int house=0;house<numPlayers;++house){const auto& h=nativeAI.houses[house];
        if(h.ratio<0 || h.ratio>100 || (h.nextPass>tick && h.nextPass-tick>maxDelay) || (h.nextChoice>tick && h.nextChoice-tick>LOGIC_FPS))return false;
        for(int c=0;c<4;++c)if(h.wanted[c]!=-1 && (h.wanted[c]<0 || h.wanted[c]>=(int)nativeTypes.size()
            || nativeTypes[h.wanted[c]]->isBuilding || !nativeTypes[h.wanted[c]]->production.available || nativeTypes[h.wanted[c]]->production.category!=c))return false;}
    for(const auto& t:nativeAI.triggers)if(!std::isfinite(t.weight) || t.weight<0 || t.weight>1000000 || t.successes>t.runs || t.runs>1000000000000ull)return false;
    for(auto a:nativeAI.autocreate)if(a>1)return false;for(auto born:nativeAI.teamBorn)if(born>tick)return false;
    for(const auto& [key,p]:nativeAI.recruit)if(key.empty() || key.size()>128 || p.group < -2 || p.group>1000000)return false;
    std::set<EID> seen;
    for(const auto& j:nativeAI.jobs){if(!seen.insert(j.actor).second || !valid(j.actor) || !ents[j.actor].isBuilding
        || ents[j.actor].nativeIdentity.object!=j.object || j.house<0 || j.house>=numPlayers
        || j.type<0 || j.type>=(int)nativeTypes.size() || !nativeFactorySupports(ents[j.actor],j.type) || nativeTypes[j.type]->isBuilding
        || j.progress<0 || j.progress>54 || j.work<0 || j.work>510 || j.paid<0 || j.paid>nativeTypes[j.type]->unit.cost
        || (j.progress==54 && j.paid!=nativeTypes[j.type]->unit.cost))return false;}
    return true;
}

bool World::writeNativeAIState(FILE* file) const {
    if(!validateNativeAIState())return false;const uint8_t available=nativeAIRules.available;
    if(fwrite(&available,1,1,file)!=1)return false;if(!available)return true;
    auto state=nativeAI;BinaryStateIO io{file};return transfer(io,state);
}
bool World::readNativeAIState(FILE* file) {
    uint8_t available=0;if(fread(&available,1,1,file)!=1 || available>1 || (available!=0)!=nativeAIRules.available)return false;
    nativeAI={};BinaryStateIO io{file,true};return (!available || transfer(io,nativeAI)) && validateNativeAIState();
}

uint32_t World::nativeAIChecksum() const {
    uint32_t h=2166136261u;const auto number=[&](uint64_t v){for(int i=0;i<8;++i){h=(h^(uint8_t)v)*16777619u;v>>=8;}};
    const auto text=[&](const std::string& s){number(s.size());for(unsigned char c:s)h=(h^c)*16777619u;};
    number(nativeAI.active);
    if(nativeAIRules.hasOriginalTeamControl()) {
        number(0xa162);number(nativeAI.centers.size());
        for(const auto& house:nativeAI.centers){number(house.initialized);number((uint32_t)house.center.x);number((uint32_t)house.center.y);}
        number(nativeAI.nextFoot);number(nativeAI.feet.size());
        for(const auto& [key,foot]:nativeAI.feet){text(key);number(foot.joined);number(foot.leader);}
        number(nativeAI.underStrength.size());for(auto under:nativeAI.underStrength)number(under);
    }
    if(!nativeAI.active)return h;
    for(const auto& p:nativeAI.houses){number(p.enabled);number(p.production);number(p.ratio);number(p.nextPass);number(p.nextChoice);for(auto v:p.wanted)number((uint32_t)v);}
    for(const auto& t:nativeAI.triggers){uint64_t bits=0;memcpy(&bits,&t.weight,8);number(bits);number(t.runs);number(t.successes);}
    for(auto a:nativeAI.autocreate)number(a);for(auto t:nativeAI.teamBorn)number(t);
    number(nativeAI.recruit.size());for(const auto& [key,p]:nativeAI.recruit){text(key);number(p.normal);number(p.automatic);number((uint32_t)p.group);}
    number(nativeAI.jobs.size());for(const auto& j:nativeAI.jobs){number(j.actor);number(j.house);number(j.type);number(j.progress);number(j.work);number(j.paid);text(j.object);}
    if(nativeAIRules.isGlobal()){number(0xa161);for(const auto& house:nativeAI.houses){number(house.sourceConfigured);number(house.sourceDifficulty);number((uint32_t)house.sourceEnemy);}
        number(nativeAI.memberTeams.size());for(const auto& [key,team]:nativeAI.memberTeams){text(key);number(team);}}
    return h;
}
