#include "game/world.h"
#include <cmath>
#include <cstring>

bool World::validateNativeCrateState(bool lifetimes) const {
    if(nativeCrateBonuses.size()>65536 || (!nativeCrateRules.available && !nativeCrateBonuses.empty()))return false;
    for(const auto& crate:crates){if(crate.kind>=NATIVE_CRATE_BASE && !nativeCrateType(crate.kind))return false;
        if(lifetimes && (crate.remainingTicks<0 || crate.remainingTicks>216000 || (crate.remainingTicks && crate.kind!=NATIVE_CRATE_BASE+19)
            || (crate.kind==NATIVE_CRATE_BASE+19 && !crate.remainingTicks)))return false;}
    for(const auto& [key,bonus]:nativeCrateBonuses){if(key.empty() || key.size()>128)return false;
        const float values[]={bonus.armor,bonus.speed,bonus.firepower};const int kinds[]={9,10,11};
        for(int i=0;i<3;++i)if(!std::isfinite(values[i]) || (values[i]!=1 && values[i]!=nativeCrateRules.data[kinds[i]]))return false;}
    return true;
}
bool World::writeNativeCrateState(FILE* file) const {
    if(!validateNativeCrateState())return false;const uint8_t available=nativeCrateRules.available;
    if(fwrite(&available,1,1,file)!=1)return false;if(!available)return true;
    const uint32_t count=(uint32_t)nativeCrateBonuses.size();if(fwrite(&count,4,1,file)!=1)return false;
    for(const auto& [key,bonus]:nativeCrateBonuses){const uint32_t length=(uint32_t)key.size();const float values[]={bonus.armor,bonus.firepower,bonus.speed};
        if(fwrite(&length,4,1,file)!=1 || fwrite(key.data(),1,length,file)!=length || fwrite(values,sizeof(float),3,file)!=3)return false;}
    return true;
}
bool World::readNativeCrateState(FILE* file){
    nativeCrateBonuses.clear();uint8_t available=0;
    if(fread(&available,1,1,file)!=1 || available>1 || (available!=0)!=nativeCrateRules.available)return false;
    if(!available)return validateNativeCrateState();uint32_t count=0;
    if(fread(&count,4,1,file)!=1 || count>65536)return false;
    for(uint32_t i=0;i<count;++i){uint32_t length=0;float values[3]{};
        if(fread(&length,4,1,file)!=1 || !length || length>128)return false;std::string key(length,'\0');
        if(fread(key.data(),1,length,file)!=length || fread(values,sizeof(float),3,file)!=3
            || !nativeCrateBonuses.emplace(key,NativeCrateBonus{values[0],values[1],values[2]}).second)return false;}
    return validateNativeCrateState(false);
}
bool World::writeSourceCrateLifetimes(FILE* file) const {
    if(!validateNativeCrateState())return false;const uint32_t version=1,count=(uint32_t)crates.size(),bytes=8+count*4;
    if(fwrite(&version,4,1,file)!=1 || fwrite(&count,4,1,file)!=1)return false;
    for(const auto& c:crates)if(fwrite(&c.remainingTicks,4,1,file)!=1)return false;
    return fwrite(&bytes,4,1,file)==1;
}
bool World::readSourceCrateLifetimes(FILE* file){
    uint32_t version=0,count=0,bytes=0;
    if(fread(&version,4,1,file)!=1 || version!=1 || fread(&count,4,1,file)!=1 || count!=crates.size() || count>1024)return false;
    for(auto& c:crates)if(fread(&c.remainingTicks,4,1,file)!=1)return false;
    return fread(&bytes,4,1,file)==1 && bytes==8+count*4 && validateNativeCrateState();
}
uint32_t World::nativeCrateChecksum() const {
    uint32_t h=2166136261u;const auto number=[&](uint32_t v){for(int i=0;i<4;++i){h=(h^(uint8_t)v)*16777619u;v>>=8;}};
    number((uint32_t)nativeCrateBonuses.size());
    for(const auto& [key,bonus]:nativeCrateBonuses){number((uint32_t)key.size());for(unsigned char c:key)h=(h^c)*16777619u;
        for(float value:{bonus.armor,bonus.firepower,bonus.speed}){uint32_t bits=0;memcpy(&bits,&value,4);number(bits);}}
    return h;
}
