#include "game/world.h"
#include <bit>
#include <cmath>
#include <type_traits>

namespace {
bool record(const World::Ent& e){return e.alive && e.nativeJumpjet.motion.started;}
template<class F> void fields(NativeJumpjetState& s,F&& item) {
    auto& m=s.motion;item(m.frame);item(m.turnStart);item(m.lastTick);item(m.facing);item(m.startFacing);item(m.desiredFacing);
    item(m.turnFrames);item(m.dx);item(m.dy);item(m.half);item(s.speed);item(s.desiredSpeed);item(s.wobble);
    item(s.goalX);item(s.goalY);item(s.goalHeight);item(s.dz);item(s.ground);
}
}
bool World::validateNativeJumpjetState() const {
    for(const auto& e:ents) {
        if(!e.alive)continue;const auto& s=e.nativeJumpjet;const auto& m=s.motion;
        if(!m.started){if(!(s==NativeJumpjetState{}))return false;continue;}
        if(!usesNativeJumpjet(e) || !unitDef(e).isAir())return false;
        const auto& p=e.nativeIdentity.definition->jumpjet;
        if(!std::isfinite(s.speed) || s.speed<0 || s.speed>p.speed || !std::isfinite(s.desiredSpeed) || s.desiredSpeed<0 || s.desiredSpeed>p.speed
           || !std::isfinite(s.wobble) || s.wobble<0 || s.wobble>1000000000 || m.throttle!=0
           || m.turnStart>m.frame || m.lastTick>tick || (m.frame>m.lastTick && m.frame-m.lastTick>1)
           || m.half>1 || m.turnFrames<0 || m.turnFrames>32768 || (uint8_t)s.phase>(uint8_t)NativeJumpjetPhase::Descent
           || std::abs((double)m.dx)>p.speed+256 || std::abs((double)m.dy)>p.speed+256 || std::abs((double)s.dz)>p.height+p.deviation+100000
           || s.goalHeight<0 || s.goalHeight>p.height || s.ground<0 || s.ground>100000
           || s.goalX< -4096 || s.goalY< -4096 || s.goalX>(map.w+16)*256 || s.goalY>(map.h+16)*256
           || !std::isfinite(e.nativeAltitude) || !std::isfinite(e.previousNativeAltitude) || e.nativeAltitude<0 || e.nativeAltitude>2000000
           || e.previousNativeAltitude<0 || e.previousNativeAltitude>2000000)return false;
    }
    return true;
}
bool World::writeNativeJumpjetState(FILE* file) const {
    if(!file || !validateNativeJumpjetState())return false;
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& v){bytes+=(uint32_t)sizeof(v);if(ok && fwrite(&v,sizeof(v),1,file)!=1)ok=false;};
    item(uint32_t{1});item((uint8_t)nativeJumpjetEnabled);uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])){auto s=ents[id].nativeJumpjet;item(id);fields(s,item);
        item((uint8_t)s.phase);item((uint8_t)s.moving);item((uint8_t)s.requested);item(ents[id].nativeAltitude);item(ents[id].previousNativeAltitude);}
    return ok && fwrite(&bytes,sizeof(bytes),1,file)==1;
}
bool World::readNativeJumpjetState(FILE* file) {
    if(!file)return false;for(auto& e:ents)e.nativeJumpjet={};
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](auto& v){bytes+=(uint32_t)sizeof(v);if(ok && fread(&v,sizeof(v),1,file)!=1)ok=false;};
    uint32_t version=0,count=0;uint8_t enabled=0;item(version);item(enabled);item(count);
    if(!ok || version!=1 || enabled>1 || count>ents.size())return false;nativeJumpjetEnabled=enabled!=0;EID previous=-1;
    for(uint32_t i=0;i<count;++i){EID id=-1;item(id);if(!ok || id<=previous || !valid(id))return false;previous=id;
        auto& s=ents[id].nativeJumpjet;s.motion.started=true;fields(s,item);uint8_t phase=0,moving=0,requested=0;item(phase);item(moving);item(requested);
        if(!ok || phase>4 || moving>1 || requested>1)return false;s.phase=(NativeJumpjetPhase)phase;s.moving=moving!=0;s.requested=requested!=0;
        item(ents[id].nativeAltitude);item(ents[id].previousNativeAltitude);}
    uint32_t footer=0;if(!ok || fread(&footer,sizeof(footer),1,file)!=1 || footer!=bytes)return false;
    return validateNativeJumpjetState();
}
uint32_t World::nativeJumpjetChecksum() const {
    uint32_t h=2166136261u;
    const auto item=[&](const auto& value){using T=std::decay_t<decltype(value)>;uint64_t bits=0;
        if constexpr(std::is_same_v<T,double>)bits=std::bit_cast<uint64_t>(value);
        else if constexpr(std::is_same_v<T,float>)bits=std::bit_cast<uint32_t>(value);
        else bits=(uint64_t)value;
        for(size_t i=0;i<sizeof(value);++i){h=(h^(uint8_t)bits)*16777619u;bits>>=8;}};
    item((uint8_t)nativeJumpjetEnabled);uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])){auto s=ents[id].nativeJumpjet;item(id);fields(s,item);
        item((uint8_t)s.phase);item((uint8_t)s.moving);item((uint8_t)s.requested);item(ents[id].nativeAltitude);item(ents[id].previousNativeAltitude);}
    return h;
}
