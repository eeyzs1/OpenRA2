#include "game/world.h"

namespace {
bool record(const World::Ent& e){return e.alive && (e.nativeAirUnload || e.nativeAirLandingRetry);}
}
bool World::validateNativeAirNavigation() const {
    for(const auto& e:ents)if(record(e)) {
        if(!usesNativeAirNavigation(e))return false;
        if(e.nativeIdentity.definition->jumpjet.balloon || e.utype==UnitType::SiegeChopper || (e.nativeAirUnload && unitDef(e).cargoCap<=0)
            || (e.nativeAirLandingRetry>tick && e.nativeAirLandingRetry-tick>30))return false;
    }
    return true;
}
bool World::writeNativeAirNavigation(FILE* file) const {
    if(!file || !validateNativeAirNavigation())return false;
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fwrite(&value,sizeof(value),1,file)!=1)ok=false;};
    item(uint32_t{1});uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])){item(id);item((uint8_t)ents[id].nativeAirUnload);item(ents[id].nativeAirLandingRetry);}
    return ok && fwrite(&bytes,sizeof(bytes),1,file)==1;
}
bool World::readNativeAirNavigation(FILE* file) {
    if(!file)return false;for(auto& e:ents){e.nativeAirUnload=false;e.nativeAirLandingRetry=0;}
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fread(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t version=0,count=0;item(version);item(count);if(!ok || version!=1 || count>ents.size())return false;
    EID previous=-1;
    for(uint32_t i=0;i<count;++i){EID id=-1;uint8_t pending=0;item(id);item(pending);
        if(!ok || !valid(id) || id<=previous || pending>1)return false;previous=id;
        auto& e=ents[id];e.nativeAirUnload=pending!=0;item(e.nativeAirLandingRetry);if(!record(e))return false;}
    uint32_t footer=0;if(!ok || fread(&footer,sizeof(footer),1,file)!=1 || footer!=bytes)return false;
    return validateNativeAirNavigation();
}
uint32_t World::nativeAirNavigationChecksum() const {
    uint32_t hash=2166136261u;
    const auto item=[&](uint64_t value,int bytes){while(bytes--){hash=(hash^(uint8_t)value)*16777619u;value>>=8;}};
    uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count,4);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])){item(id,4);item(ents[id].nativeAirUnload,1);item(ents[id].nativeAirLandingRetry,8);}
    return hash;
}
