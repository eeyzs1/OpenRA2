#pragma once
#include "core/util.h"
#include <bit>
#include <vector>

struct NativeHouseBuildingPoint {int x=0,y=0,cost=0,health=0;bool limbo=false;};
inline Vec2i nativeHouseWeightedCenter(const std::vector<NativeHouseBuildingPoint>& buildings) {
    uint32_t x=0,y=0,total=0;
    for(const auto& point:buildings)if(point.health>0 && !point.limbo) {
        const int weight=point.cost/1000+1;if(weight<=0)continue;
        x+=(uint32_t)((int64_t)point.x*weight);y+=(uint32_t)((int64_t)point.y*weight);total+=(uint32_t)weight;
    }
    const int count=std::bit_cast<int32_t>(total);if(count<=0)return {};
    const int cx=std::bit_cast<int32_t>(x)/count,cy=std::bit_cast<int32_t>(y)/count;
    // Original code writes a CellStruct before querying nearby clearance.
    return {(int)(int16_t)(cx/256)*256+128,(int)(int16_t)(cy/256)*256+128};
}
