#include "game/world.h"
#include <bit>
#include <cmath>
#include <type_traits>

namespace {
template<class M,class F>void missionFields(M& m,F&& item) {
    item(m.aircraft.id);item(m.aircraft.generation);item(m.type);item(m.phase);
    item(m.x);item(m.y);item(m.exitX);item(m.exitY);item(m.nextDrop);
    item(m.passes);
}
template<class E,class F>void actorFields(E& e,F&& item) {
    item(e.supportAir);item(e.supportFallHalf);item(e.supportFallSpeed);item(e.supportSince);
    item(e.nativeAltitude);item(e.previousNativeAltitude);
}
}
bool World::validateSupportDelivery() const {
    if(!supportDeliveryEnabled && !supportMissions.empty())return false;
    if(supportDeliveryEnabled && (!independentSupportEnabled || !nativeFlightEnabled
        || (usesRulesProfile() && nativeProductionRules.version<5)))return false;
    std::vector<uint8_t> seen(ents.size());
    for(const auto& m:supportMissions) {
        if(!valid(m.aircraft) || (unsigned)m.type>=(unsigned)SupportType::PsychicReveal || m.phase>2 || m.passes>5)return false;
        const auto& e=ents[m.aircraft.id];
        if(seen[m.aircraft.id]++ || e.supportAir!=1 || e.player<0 || e.player>=numPlayers || e.isBuilding
            || e.utype!=(m.type==SupportType::SpyPlane?UnitType::SpyPlane:UnitType::CargoPlane)
            || !std::isfinite(m.x) || !std::isfinite(m.y) || m.x<0 || m.y<0 || m.x>=map.w || m.y>=map.h
            || !std::isfinite(m.exitX) || !std::isfinite(m.exitY) || m.exitX<0 || m.exitY<0 || m.exitX>=map.w || m.exitY>=map.h
            || (m.nextDrop>tick && m.nextDrop-tick>10) || e.cargo.size()>64 || !fixedWingProfile(e).available)return false;
        if(m.type==SupportType::SpyPlane && !e.cargo.empty())return false;
        for(const auto& passenger:e.cargo)if(!(passenger.nativeIdentity.definition?passenger.nativeIdentity.definition->unit:unitDef(passenger.type)).isInfantry()
            || !passenger.cargo.empty())return false;
    }
    for(size_t i=0;i<ents.size();++i) {
        const auto& e=ents[i];if(!e.alive)continue;
        if(!e.supportAir) {
            if(e.supportFallHalf || e.supportFallSpeed || e.supportSince)return false;continue;
        }
        if(!supportDeliveryEnabled || e.supportAir>2 || e.supportFallHalf>1 || e.isBuilding || e.supportSince>tick
            || e.player<0 || e.player>=numPlayers || !std::isfinite(e.nativeAltitude) || !std::isfinite(e.previousNativeAltitude)
            || e.nativeAltitude<0 || e.nativeAltitude>2000000 || e.previousNativeAltitude<0 || e.previousNativeAltitude>2000000)return false;
        if(e.supportAir==1) {if(!seen[i] || e.supportFallHalf || e.supportFallSpeed)return false;}
        else if(!unitDef(e).isInfantry() || e.supportFallSpeed>0 || e.supportFallSpeed<supportDeliveryRules().parachuteFall
            || !e.cargo.empty() || e.nativeFlight.started || e.nativeJumpjet.motion.started)return false;
    }
    return true;
}
bool World::writeSupportDelivery(FILE* file) const {
    if(!file || !validateSupportDelivery())return false;bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fwrite(&value,sizeof(value),1,file)!=1)ok=false;};
    item(uint32_t{0x52564c44});item(uint32_t{1});item((uint8_t)supportDeliveryEnabled);item((uint32_t)supportMissions.size());
    for(const auto& mission:supportMissions)missionFields(mission,item);
    uint32_t count=0;for(const auto& e:ents)if(e.alive && e.supportAir)++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id)if(ents[id].alive && ents[id].supportAir){item(id);item(ents[id].generation);actorFields(ents[id],item);}
    return ok && fwrite(&bytes,4,1,file)==1;
}
bool World::readSupportDelivery(FILE* file) {
    if(!file)return false;supportMissions.clear();
    for(auto& e:ents){e.supportAir=e.supportFallHalf=0;e.supportFallSpeed=0;e.supportSince=0;}
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fread(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t header=0,version=0,count=0;uint8_t enabled=0;item(header);item(version);item(enabled);item(count);
    if(!ok || header!=0x52564c44 || version!=1 || enabled>1 || count>ents.size())return false;supportDeliveryEnabled=enabled!=0;
    supportMissions.resize(count);for(auto& mission:supportMissions)missionFields(mission,item);
    item(count);if(!ok || count>ents.size())return false;EID previous=-1;
    for(uint32_t i=0;i<count;++i){EntityRef ref;item(ref.id);item(ref.generation);
        if(!ok || ref.id<=previous || !valid(ref))return false;previous=ref.id;actorFields(ents[ref.id],item);
        if(!ok || !ents[ref.id].supportAir)return false;}
    uint32_t footer=0;if(!ok || fread(&footer,4,1,file)!=1 || footer!=bytes)return false;
    return validateSupportDelivery();
}
uint32_t World::supportDeliveryChecksum() const {
    uint32_t h=2166136261u;
    const auto item=[&](const auto& value){using T=std::decay_t<decltype(value)>;uint64_t bits=0;
        if constexpr(std::is_same_v<T,float>)bits=std::bit_cast<uint32_t>(value);else bits=(uint64_t)value;
        for(size_t i=0;i<sizeof(value);++i){h=(h^(uint8_t)bits)*16777619u;bits>>=8;}};
    item((uint8_t)supportDeliveryEnabled);item((uint32_t)supportMissions.size());
    for(const auto& mission:supportMissions)missionFields(mission,item);
    for(EID id=0;id<(EID)ents.size();++id)if(ents[id].alive && ents[id].supportAir){item(id);item(ents[id].generation);actorFields(ents[id],item);}
    return h;
}
