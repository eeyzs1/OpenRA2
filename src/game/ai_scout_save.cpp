#include "game/ai.h"
#include <cstdio>
#include <limits>

bool SkirmishAI::saveKnowledge(FILE* file) const {
    bool ok=true;
    auto write=[&](const auto& value){if(ok && fwrite(&value,sizeof(value),1,file)!=1)ok=false;};
    write(scoutMapW);write(scoutMapH);
    uint32_t count=(uint32_t)scoutObserved.size();write(count);
    for(uint64_t stamp:scoutObserved)write(stamp);
    count=(uint32_t)knownBuildings.size();write(count);
    for(const auto& building:knownBuildings) {
        write(building.x);write(building.y);write(building.owner);
        const auto type=(uint8_t)building.type;write(type);write(building.observedTick);
    }
    return ok;
}

bool SkirmishAI::loadKnowledge(FILE* file) {
    bool ok=true;
    auto read=[&](auto& value){if(ok && fread(&value,sizeof(value),1,file)!=1)ok=false;};
    read(scoutMapW);read(scoutMapH);
    uint32_t count=0;read(count);
    if(!ok || scoutMapW<0 || scoutMapW>512 || scoutMapH<0 || scoutMapH>512
        || (scoutMapW==0)!=(scoutMapH==0) || count!=(uint32_t)(scoutMapW*scoutMapH))return false;
    scoutObserved.resize(count);
    for(auto& stamp:scoutObserved)read(stamp);
    read(count);if(!ok || count>65536)return false;
    knownBuildings.resize(count);
    for(auto& building:knownBuildings) {
        read(building.x);read(building.y);read(building.owner);
        uint8_t type=0;read(type);building.type=(BldType)type;read(building.observedTick);
    }
    return ok;
}

bool SkirmishAI::knowledgeValid(const World& world) const {
    if(scoutMapW==0 && scoutMapH==0)return scoutObserved.empty() && knownBuildings.empty();
    if(scoutMapW!=world.map.w || scoutMapH!=world.map.h || scoutObserved.size()!=world.map.cells.size()
        || knownBuildings.size()>65536)return false;
    const uint64_t latest=world.tick==std::numeric_limits<uint64_t>::max()?world.tick:world.tick+1;
    for(uint64_t stamp:scoutObserved)if(stamp>latest)return false;
    int previous=-1;
    for(const auto& building:knownBuildings) {
        if((unsigned)building.type>=(unsigned)BldType::COUNT || building.owner<0 || building.owner>=world.numPlayers
            || building.observedTick>world.tick || !world.map.inBounds(building.x,building.y))return false;
        const auto& def=bldDef(building.type);
        if(!world.map.inBounds(building.x+def.w-1,building.y+def.h-1))return false;
        const int cell=building.y*world.map.w+building.x;
        if(cell<=previous)return false;
        previous=cell;
    }
    return true;
}
