#pragma once
#include "game/map.h"
#include <algorithm>

inline Terrain terrainSubstrate(Terrain terrain) {
    return terrain==Terrain::Ore || terrain==Terrain::Gems?Terrain::Rough:terrain;
}
inline uint32_t terrainVisualHash(int x,int y,uint64_t seed) {
    uint32_t h=uint32_t(x)*73856093u ^ uint32_t(y)*19349663u ^ uint32_t(seed)*83492791u;
    h^=h>>13;h*=0x5bd1e995u;h^=h>>15;return h;
}
inline int resourceVisualVariant(int x,int y,int storedVariant) {
    return (terrainVisualHash(x,y,20260723+113)+storedVariant)&7;
}
constexpr int RESOURCE_VISUAL_LEVELS=8;
inline int resourceVisualLevel(int amount,int capacity) {
    const int64_t cap=std::max(1,capacity);
    return (int)std::clamp<int64_t>((int64_t(std::max(0,amount))*RESOURCE_VISUAL_LEVELS+cap-1)/cap,0,RESOURCE_VISUAL_LEVELS);
}
