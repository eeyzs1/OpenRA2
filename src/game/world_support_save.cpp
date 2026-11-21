#include "game/world.h"

bool World::validateSupportState() const {
    if(independentSupportEnabled && nativeProductionRules.available && nativeProductionRules.version<4)return false;
    for(const auto& player:players) {
        if(independentSupportEnabled && (player.paradropCharge || player.paradropReady))return false;
        for(int i=0;i<SUPPORT_COUNT;++i) {
            const auto& charge=player.support[i];
            if(!independentSupportEnabled) {if(charge.ticks || charge.ready)return false;continue;}
            const auto& rule=supportRule((SupportType)i);
            if(charge.ticks<0 || charge.ticks>rule.rechargeTicks || (!rule.available && (charge.ticks || charge.ready)))return false;
            if(charge.ready!=(rule.available && charge.ticks==rule.rechargeTicks))return false;
        }
    }
    return true;
}

bool World::writeSupportState(FILE* file) const {
    if(!file || !validateSupportState())return false;
    const uint32_t header=0x53555050,version=1,footer=0x454e4453;
    const uint8_t veterancy=veterancyProgressEnabled,support=independentSupportEnabled;
    if(std::fwrite(&header,4,1,file)!=1 || std::fwrite(&version,4,1,file)!=1
        || std::fwrite(&veterancy,1,1,file)!=1 || std::fwrite(&support,1,1,file)!=1)return false;
    for(const auto& player:players)for(const auto& charge:player.support) {
        const uint8_t ready=charge.ready;
        if(std::fwrite(&charge.ticks,4,1,file)!=1 || std::fwrite(&ready,1,1,file)!=1)return false;
    }
    const uint32_t bytes=14+(uint32_t)players.size()*SUPPORT_COUNT*5;
    return std::fwrite(&footer,4,1,file)==1 && std::fwrite(&bytes,4,1,file)==1;
}

bool World::readSupportState(FILE* file) {
    uint32_t header=0,version=0,footer=0,bytes=0;uint8_t veterancy=0,support=0;
    if(!file || std::fread(&header,4,1,file)!=1 || std::fread(&version,4,1,file)!=1
        || std::fread(&veterancy,1,1,file)!=1 || std::fread(&support,1,1,file)!=1
        || header!=0x53555050 || version!=1 || veterancy>1 || support>1)return false;
    veterancyProgressEnabled=veterancy!=0;independentSupportEnabled=support!=0;
    for(auto& player:players)for(auto& charge:player.support) {
        uint8_t ready=0;
        if(std::fread(&charge.ticks,4,1,file)!=1 || std::fread(&ready,1,1,file)!=1 || ready>1)return false;
        charge.ready=ready!=0;
    }
    return std::fread(&footer,4,1,file)==1 && footer==0x454e4453 && std::fread(&bytes,4,1,file)==1
        && bytes==14+(uint32_t)players.size()*SUPPORT_COUNT*5 && validateSupportState();
}
