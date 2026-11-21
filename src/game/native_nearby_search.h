#pragma once
#include "core/util.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

// Original RA2 geometry. Cell clearance remains an engine interface. These
// functions accept immutable cell data; they do not approximate that predicate.
struct NativeNearbyCellState {
    int level=0,ramp=0;
    uint32_t flags=0;
    int overlay=-1;
    bool present=false;
};
struct NativeNearbyMapBounds {
    int width=0,height=0,localX=0,localY=0,localWidth=0,localHeight=0;
};
struct NativeNearbyFootprint {
    int width=1,height=1,speed=0,zone=-1,movement=0,owner=-1;
    bool alt=false,avoidOverlay=false;
};
struct NativeNearbyQuery {
    Vec2i origin{},closeTo{};
    int mapWidth=0,mapHeight=0;
    uint32_t frame=0;
    bool alt=false,checkHeight=false,requireBurrow=false,allowBridge=true,option14=false;
};
inline Vec2i nativeNearbyCell(int x,int y) {
    return {(int16_t)x,(int16_t)y};
}
inline bool nativeNearbyUsable(const NativeNearbyMapBounds& map,Vec2i point,
                               const NativeNearbyCellState& cell,bool includeHeight=true) {
    int level=includeHeight?cell.level:0;
    if(includeHeight && cell.ramp && point.x+point.y<map.width+2*map.localY+level+4)++level;
    return point.x+point.y>map.width+2*map.localY+level
        && point.x+point.y<=map.width+2*(map.localY+map.localHeight)+2+level
        && point.x-point.y<2*(map.localX+map.localWidth)-map.width
        && point.y-point.x<map.width-2*map.localX;
}
template<class CellAt,class Clear>
bool nativeNearbyCanPlace(Vec2i origin,const NativeNearbyFootprint& footprint,CellAt&& cellAt,Clear&& clear) {
    for(int x=0;x<footprint.width;++x)for(int y=0;y<footprint.height;++y) {
        const auto point=nativeNearbyCell(origin.x+x,origin.y+y);
        if(footprint.avoidOverlay && cellAt(point).overlay!=-1)return false;
        if(!clear(point,footprint))return false;
    }
    return true;
}
template<class CellAt>
Vec2i nativeNearbyProjection(Vec2i leptons,CellAt&& cellAt) {
    const auto origin=nativeNearbyCell(leptons.x/256,leptons.y/256);
    const auto base=cellAt(origin);
    int x=origin.x*256+128+1536,y=origin.y*256+128+1536;
    for(;;) {
        x-=8;y-=8;
        const auto point=nativeNearbyCell(x/256,y/256);
        const auto cell=cellAt(point);
        const int delta=cell.level-base.level+((base.flags&0x1000) && (cell.flags&0x100)?4:0);
        const auto projected=nativeNearbyCell((x-delta*128)/256,(y-delta*128)/256);
        if((projected.x<=origin.x && projected.y<=origin.y) || point==origin)return point;
    }
}
inline uint32_t nativeNearbyDistanceKey(Vec2i point,Vec2i closeTo) {
    // Original sqrt discards ten low significand bits of its positive float
    // input before consulting a strictly increasing table. Comparing this
    // key preserves its ties without copying the table or using std::sqrt.
    const uint32_t dx=(uint32_t)((int16_t)point.x-(int16_t)closeTo.x);
    const uint32_t dy=(uint32_t)((int16_t)point.y-(int16_t)closeTo.y);
    const uint32_t wrapped=dx*dx+dy*dy;
    int32_t squared;std::memcpy(&squared,&wrapped,sizeof(squared));
    const float magnitude=(float)(squared<0?-(double)squared:(double)squared);
    static_assert(sizeof(float)==sizeof(uint32_t));
    uint32_t bits;std::memcpy(&bits,&magnitude,sizeof(bits));return bits>>10;
}
template<class CellAt,class Usable,class CanPlace,class CanBurrow,class Project>
Vec2i nativeNearbySearch(const NativeNearbyQuery& query,CellAt&& cellAt,Usable&& usable,
                        CanPlace&& canPlace,CanBurrow&& canBurrow,Project&& project) {
    std::array<Vec2i,24> candidates{},stable{},other{};
    int count=0;bool found=false;
    const auto origin=nativeNearbyCell(query.origin.x,query.origin.y);
    const auto base=cellAt(origin);
    const int level=base.level+(query.alt && (base.flags&0x100)?4:0);
    const auto consider=[&](int x,int y) {
        const auto point=nativeNearbyCell(x,y);
        if(!usable(point) || !canPlace(point))return;
        const auto cell=cellAt(point);
        const int delta=level-cell.level-(cell.flags&0x100?4:0);
        if(query.checkHeight && (delta<=-2 || delta>=2))return;
        if(query.requireBurrow && !canBurrow(point))return;
        if(!query.allowBridge && (cell.flags&0x100))return;
        candidates[count++]=point;
        if(query.alt || project(point)==point)found=true;
    };
    const int limit=std::min(32,query.mapWidth+query.mapHeight);
    for(int radius=0;radius<limit && count<24 && !found;++radius) {
        for(int offset=-radius;offset<=radius && count<24;++offset) {
            if(!query.option14)consider(origin.x+offset,origin.y-radius);
            if(count<24 && (!query.option14 || offset>-radius))consider(origin.x+offset,origin.y+radius);
        }
        for(int offset=-radius+1;offset<radius && count<24;++offset) {
            if(!query.option14)consider(origin.x-radius,origin.y+offset);
            if(count<24)consider(origin.x+radius,origin.y+offset);
        }
    }
    int stableCount=0,otherCount=0;
    for(int i=0;i<count;++i) {
        const auto point=candidates[i];
        if(project(point)==point)stable[stableCount++]=point;else other[otherCount++]=point;
    }
    const auto& selected=stableCount?stable:other;
    const int selectedCount=stableCount?stableCount:otherCount;
    if(!selectedCount)return {};
    if(query.closeTo==Vec2i{})return selected[query.frame%(uint32_t)selectedCount];
    auto result=selected[0];auto distance=nativeNearbyDistanceKey(result,query.closeTo);
    for(int i=1;i<selectedCount;++i) {
        const auto candidate=nativeNearbyDistanceKey(selected[i],query.closeTo);
        if(candidate<distance){distance=candidate;result=selected[i];}
    }
    return result;
}
