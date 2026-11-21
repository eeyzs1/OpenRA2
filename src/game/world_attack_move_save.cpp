#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

bool World::validateAttackMoveState() const {
    for(const auto& e:ents) {
        for(const auto& p:e.wps)if(p.attackMode< -1 || p.attackMode>1)return false;
        const auto& a=e.attackMove;
        if(!a.active && !(a==Ent::AttackMoveOrder{}))return false;
        if(hasAttackMoveState(e) && (!e.alive || e.isBuilding))return false;
        if(!a.active)continue;
        if(a.owner<0 || a.owner>=numPlayers || a.owner!=e.player || !std::isfinite(a.x) || !std::isfinite(a.y)
            || a.x<0 || a.y<0 || a.x>=map.w || a.y>=map.h || a.layer>1
            || (unitDef(e).isAir() && a.layer!=0) || (a.retryAt>tick && a.retryAt-tick>LOGIC_FPS))return false;
    }
    return true;
}

bool World::writeAttackMoveState(FILE* file) const {
    if(!file || !validateAttackMoveState())return false;
    bool ok=true;uint64_t bytes=0;
    const auto item=[&](const auto& value){bytes+=sizeof(value);if(ok && fwrite(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t count=0;for(const auto& e:ents)if(hasAttackMoveState(e))++count;item(count);
    for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];if(!hasAttackMoveState(e))continue;
        item(id);const uint8_t active=e.attackMove.active;item(active);
        if(active){const auto& a=e.attackMove;item(a.owner);item(a.x);item(a.y);item(a.layer);item(a.retryAt);}
        const uint32_t modes=(uint32_t)e.wps.size();item(modes);for(const auto& p:e.wps)item(p.attackMode);
    }
    if(bytes>std::numeric_limits<uint32_t>::max())return false;
    const uint32_t size=(uint32_t)bytes;
    return ok && fwrite(&size,sizeof(size),1,file)==1;
}

bool World::readAttackMoveState(FILE* file) {
    if(!file)return false;
    for(auto& e:ents){e.attackMove={};for(auto& p:e.wps)p.attackMode=-1;}
    bool ok=true;uint64_t bytes=0;
    const auto item=[&](auto& value){bytes+=sizeof(value);if(ok && fread(&value,sizeof(value),1,file)!=1)ok=false;};
    uint32_t count=0;item(count);if(!ok || count>ents.size())return false;
    EID previous=-1;
    for(uint32_t i=0;i<count;++i){EID id=-1;uint8_t active=0;item(id);item(active);
        if(!ok || id<=previous || !valid(id) || active>1)return false;previous=id;auto& e=ents[id];
        if(active){auto& a=e.attackMove;a.active=true;item(a.owner);item(a.x);item(a.y);item(a.layer);item(a.retryAt);}
        uint32_t modes=0;item(modes);if(!ok || modes!=e.wps.size())return false;
        for(auto& p:e.wps)item(p.attackMode);if(!ok || !hasAttackMoveState(e))return false;
    }
    uint32_t size=0;if(fread(&size,sizeof(size),1,file)!=1 || size!=bytes)return false;
    return validateAttackMoveState();
}

uint32_t World::attackMoveChecksum() const {
    uint32_t h=2166136261u;const auto byte=[&](uint8_t value){h=(h^value)*16777619u;};
    const auto number=[&](uint64_t value,int size=4){for(int i=0;i<size;++i){byte((uint8_t)value);value>>=8;}};
    const auto scalar=[&](float value){uint32_t bits=0;memcpy(&bits,&value,4);number(bits);};
    uint32_t count=0;for(const auto& e:ents)if(hasAttackMoveState(e))++count;number(count);
    for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];if(!hasAttackMoveState(e))continue;
        number((uint32_t)id);byte(e.attackMove.active);
        if(e.attackMove.active){const auto& a=e.attackMove;number((uint32_t)a.owner);scalar(a.x);scalar(a.y);byte(a.layer);number(a.retryAt,8);}
        number(e.wps.size());for(const auto& p:e.wps)byte((uint8_t)p.attackMode);
    }
    return h;
}
