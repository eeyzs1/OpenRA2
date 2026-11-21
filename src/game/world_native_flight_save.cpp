#include "game/world.h"
#include <cmath>
#include <cstring>
#include <bit>
#include <type_traits>

namespace {
bool record(const World::Ent& e){return e.alive && e.nativeFlight.started;}
template<class F> void fields(NativeFlightState& s,F&& item) {
    item(s.throttle);item(s.frame);item(s.turnStart);item(s.lastTick);
    item(s.facing);item(s.startFacing);item(s.desiredFacing);item(s.turnFrames);item(s.dx);item(s.dy);item(s.half);
}
}
bool World::validateNativeFlightState() const {
    for(const auto& e:ents) {
        if(!e.alive)continue;const auto& s=e.nativeFlight;
        if(!s.started){if(!(s==NativeFlightState{}))return false;continue;}
        if(!nativeFlightEnabled || e.isBuilding || !fixedWingProfile(e).available
           || !unitDef(e).isAir() || !std::isfinite(s.throttle) || s.throttle<0 || s.throttle>1 || !s.frame
           || (s.frame>s.lastTick && s.frame-s.lastTick>1) || s.turnStart>s.frame || s.lastTick>tick || s.half>1
           || s.turnFrames!=(fixedWingProfile(e).rotation?
               std::abs((int16_t)(s.desiredFacing-s.startFacing))/fixedWingProfile(e).rotation:0)
           || s.dx < -256 || s.dx>256 || s.dy < -256 || s.dy>256 || s.facing!=NativeFlightMath::facing(s))return false;
    }
    return true;
}
bool World::writeNativeFlightState(FILE* file) const {
    if(!file || !validateNativeFlightState())return false;
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fwrite(&value,sizeof(value),1,file)!=1)ok=false;};
    item(uint32_t{1});item((uint8_t)nativeFlightEnabled);
    uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])){item(id);auto state=ents[id].nativeFlight;fields(state,item);}
    return ok && fwrite(&bytes,sizeof(bytes),1,file)==1;
}
bool World::readNativeFlightState(FILE* file) {
    if(!file)return false;for(auto& e:ents)e.nativeFlight={};
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fread(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t version=0,count=0;uint8_t enabled=0;item(version);item(enabled);item(count);
    if(!ok || version!=1 || enabled>1 || count>ents.size())return false;nativeFlightEnabled=enabled!=0;
    EID previous=-1;
    for(uint32_t i=0;i<count;++i){EID id=-1;item(id);if(!ok || id<=previous || !valid(id))return false;previous=id;
        auto& state=ents[id].nativeFlight;state.started=true;fields(state,item);}
    uint32_t footer=0;if(!ok || fread(&footer,sizeof(footer),1,file)!=1 || footer!=bytes)return false;
    return validateNativeFlightState();
}
uint32_t World::nativeFlightChecksum() const {
    uint32_t h=2166136261u;
    const auto item=[&](const auto& value){uint64_t bits;
        if constexpr(std::is_floating_point_v<std::decay_t<decltype(value)>>)bits=std::bit_cast<uint64_t>(value);
        else bits=(uint64_t)value;
        for(size_t i=0;i<sizeof(value);++i){h=(h^(uint8_t)bits)*16777619u;bits>>=8;}};
    item((uint8_t)nativeFlightEnabled);
    uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])){item(id);auto state=ents[id].nativeFlight;fields(state,item);}
    return h;
}
