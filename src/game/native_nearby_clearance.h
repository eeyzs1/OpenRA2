#pragma once
#include "core/util.h"
#include <cstdint>
#include <cstring>

inline uint32_t nativeNearbyFrame(uint64_t tick,uint32_t substeps) {
    return (uint32_t)(tick/substeps+(tick%substeps!=0));
}
inline int nativeNearbyPack(Vec2i cell) {
    const uint32_t packed=(uint16_t)cell.x | (uint32_t)(uint16_t)cell.y<<16;
    int32_t result;std::memcpy(&result,&packed,sizeof(result));return result;
}
inline Vec2i nativeNearbyUnpack(int packed) {
    return {(int16_t)(uint32_t)packed,(int16_t)((uint32_t)packed>>16)};
}
inline uint32_t nativeNearbyBridgeBits(bool present,bool knownBridge) {
    return present?0x1100:knownBridge?0x400:0;
}

// Original Cell.IsClearToMove. The zone and cost callbacks supply engine data;
// querying the common Nearby footprint uses zone=-1 and level=-1.
struct NativeNearbyClearCell {
    int level=0,land=0;
    uint8_t groundOccupancy=0,deckOccupancy=0;
    bool bridge=false,wall=false,crushable=false;
};
struct NativeNearbyClearQuery {
    int speed=0,wantedZone=-1,movement=0,wantedLevel=-1;
    bool ignoreInfantry=false,ignoreVehicles=false,alt=false;
};
template<class Zone,class Cost>
bool nativeNearbyClear(const NativeNearbyClearCell& cell,const NativeNearbyClearQuery& query,
                       Zone&& zone,Cost&& cost) {
    if(query.speed==4)return true;
    if(query.wantedZone!=-1 && query.wantedZone!=zone(query.movement,query.alt))return false;
    if(query.wantedLevel!=-1) {
        if(query.wantedLevel!=cell.level && !(cell.bridge && query.wantedLevel==cell.level+4))return false;
        if(query.wantedLevel==cell.level && cell.bridge && !query.alt)return false;
    }
    const bool deck=cell.bridge && (query.wantedLevel==-1 || query.wantedLevel==cell.level+4);
    uint8_t occupied=deck?cell.deckOccupancy:cell.groundOccupancy;
    if(query.ignoreInfantry)occupied&=0xe0;
    if(query.ignoreVehicles)occupied&=0x5f;
    if(occupied)return false;
    int land=cell.land;
    if(cell.wall) {
        const bool destroy=query.movement==2 || query.movement==3 || query.movement==8;
        const bool crush=(query.movement==1 || query.movement==4) && cell.crushable;
        if(!destroy && !crush)return false;
        land=0;
    }
    const float value=cost(land,query.speed);
    // An unordered x87 comparison is rejected like zero unless the bridge
    // deck supplies the surface. Native rules normally contain finite costs.
    return deck || (value==value && value!=0);
}

struct NativeNearbyBurrowCell {
    bool usable=false,knownTile=false,allowedTile=true;
    int ramp=0;
    uint32_t flags=0;
};
template<class ObjectType>
bool nativeNearbyBurrow(const NativeNearbyBurrowCell& cell,bool checkObjects,int objectCount,ObjectType&& objectType) {
    if(!cell.usable)return true;
    if((cell.knownTile && !cell.allowedTile) || cell.ramp || (cell.flags&0x500))return false;
    if(checkObjects) {
        for(int i=0;i<objectCount;++i)if(objectType(i)==6)return false;
        for(int i=0;i<objectCount;++i)if(objectType(i)==36)return false;
    }
    return true;
}
