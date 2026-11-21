#include "game/world.h"
#include <cmath>
#include <cstring>

namespace {
bool record(const World::Ent& e){return e.alive && e.nativeMovement.started;}
}

bool World::validateNativeMovementState() const {
    for(const auto& e:ents) {
        if(!e.alive)continue;const auto& s=e.nativeMovement;
        if(!s.started){if(!(s==NativeMovementState{}))return false;continue;}
        if(e.isBuilding || !nativeMovementRules.available || !e.nativeIdentity.definition
           || !e.nativeIdentity.definition->movement.available() || s.half>1 || s.lastTick>tick
           || !map.inBounds(s.nodeX,s.nodeY) || s.nodeLayer<0 || s.nodeLayer>1 || s.heldSpeed<0 || s.heldSpeed>100000000
           || !std::isfinite(s.throttle) || s.throttle<0 || s.throttle>100
           || !std::isfinite(s.targetThrottle) || s.targetThrottle<0 || s.targetThrottle>100
           || !std::isfinite(s.credit) || s.credit<0 || s.credit>100000000)return false;
    }
    return true;
}

bool World::writeNativeMovementState(FILE* file) const {
    if(!file || !validateNativeMovementState())return false;
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fwrite(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t count=0;for(const auto& e:ents)if(record(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];if(!record(e))continue;const auto& s=e.nativeMovement;
        item(id);item(s.throttle);item(s.targetThrottle);item(s.credit);item(s.lastTick);
        item(s.nodeX);item(s.nodeY);item(s.nodeLayer);item(s.heldSpeed);item(s.half);
    }
    return ok && fwrite(&bytes,sizeof(bytes),1,file)==1;
}

bool World::readNativeMovementState(FILE* file) {
    if(!file)return false;for(auto& e:ents)e.nativeMovement={};
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](auto& value){bytes+=(uint32_t)sizeof(value);if(ok && fread(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t count=0;item(count);if(!ok || count>ents.size())return false;EID previous=-1;
    for(uint32_t i=0;i<count;++i){EID id=-1;item(id);if(!ok || id<=previous || !valid(id))return false;previous=id;
        auto& s=ents[id].nativeMovement;s.started=true;
        item(s.throttle);item(s.targetThrottle);item(s.credit);item(s.lastTick);
        item(s.nodeX);item(s.nodeY);item(s.nodeLayer);item(s.heldSpeed);item(s.half);
    }
    uint32_t size=0;if(!ok || fread(&size,sizeof(size),1,file)!=1 || size!=bytes)return false;
    return validateNativeMovementState();
}

uint32_t World::nativeMovementChecksum() const {
    uint32_t h=2166136261u;const auto byte=[&](uint8_t v){h=(h^v)*16777619u;};
    const auto number=[&](uint64_t v,int size=4){for(int i=0;i<size;++i){byte((uint8_t)v);v>>=8;}};
    const auto scalar=[&](double v){uint64_t bits=0;memcpy(&bits,&v,8);number(bits,8);};
    uint32_t count=0;for(const auto& e:ents)if(record(e))++count;number(count);
    for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];if(!record(e))continue;const auto& s=e.nativeMovement;
        number((uint32_t)id);scalar(s.throttle);scalar(s.targetThrottle);scalar(s.credit);number(s.lastTick,8);
        number((uint32_t)s.nodeX);number((uint32_t)s.nodeY);number((uint32_t)s.nodeLayer);number((uint32_t)s.heldSpeed);byte(s.half);
    }
    return h;
}
