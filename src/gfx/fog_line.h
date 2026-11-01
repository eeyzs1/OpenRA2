#pragma once
#include "game/map.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

// Parametric intervals of a world-space line inside currently visible cells.
// Split at exact cell boundaries: endpoints may both be hidden while the middle is visible.
inline std::vector<std::pair<float,float>> visibleLineIntervals(const Map& map,int player,float x,float y,float x2,float y2) {
    std::vector<float> cuts{0,1};const float dx=x2-x,dy=y2-y;
    if(std::abs(dx)>0.000001f) for(int grid=0;grid<=map.w;++grid) {
        const float t=(grid-x)/dx;if(t>0 && t<1) cuts.push_back(t);
    }
    if(std::abs(dy)>0.000001f) for(int grid=0;grid<=map.h;++grid) {
        const float t=(grid-y)/dy;if(t>0 && t<1) cuts.push_back(t);
    }
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    std::vector<std::pair<float,float>> visible;
    for(size_t i=1;i<cuts.size();++i) {
        const float mid=(cuts[i-1]+cuts[i])*0.5f;
        const float tx=x+dx*mid,ty=y+dy*mid;
        if(tx<0 || ty<0 || tx>=map.w || ty>=map.h) continue;
        if(map.fogAt(player,(int)std::floor(tx),(int)std::floor(ty))!=FOG_VISIBLE) continue;
        if(!visible.empty() && visible.back().second==cuts[i-1]) visible.back().second=cuts[i];
        else visible.emplace_back(cuts[i-1],cuts[i]);
    }
    return visible;
}
