#include "game/world.h"
#include <algorithm>
#include <limits>

bool World::usesNativeAirNavigation(const Ent& e) const {
    return usesNativeJumpjet(e) && e.nativeIdentity.definition->jumpjet.version>=2;
}
NativeAirProbeCell World::nativeAirProbeCell(int x,int y) const {
    if(map.w<=0 || map.h<=0)return {};
    x=std::clamp(x,0,map.w-1);y=std::clamp(y,0,map.h-1);NativeAirProbeCell result;
    result.floor=map.surfaceHeight(x,y,0)*104;
    if(map.hasDeck(x,y))result.bridgeHeight=map.surfaceHeight(x,y,1)*104-result.floor;
    const EID building=bldAt(x,y);
    if(valid(building)){
        const auto& d=ents[building].nativeIdentity.definition;
        if(!d || d->kind!=3){result.occupied=true;if(d && d->kind==0 && !d->wall)result.buildingHeight=d->airObstacleHeight;}
    }
    if(!result.occupied){const auto overlay=map.at(x,y).overlay;
        result.occupied=unitAtCell(x,y,0)!=INVALID_EID || overlay==Overlay::Tree1 || overlay==Overlay::Tree2 || overlay==Overlay::Rock1 || overlay==Overlay::Rock2;}
    return result;
}
int World::nativeAirLandingLayer(int x,int y,EID actor) const {
    if(!map.inBounds(x,y))return -1;
    const int layer=map.hasDeck(x,y)?1:0;
    return map.surfacePassable(x,y,0,layer) && !bldBlocked(x,y,layer)
        && !groundUnitBlocksCell(x,y,actor,layer)?layer:-1;
}
bool World::findNativeAirLanding(Ent& e,EID actor) {
    if(map.w<=0 || map.h<=0 || !valid(actor))return false;
    if(tick<e.nativeAirLandingRetry)return false;
    const int cx=std::clamp((int)e.x,0,map.w-1),cy=std::clamp((int)e.y,0,map.h-1);
    for(int radius=0;radius<std::max(map.w,map.h);++radius) {
        int best=std::numeric_limits<int>::max(),bx=-1,by=-1,layer=-1;
        const auto consider=[&](int x,int y){
                const int candidate=nativeAirLandingLayer(x,y,actor);if(candidate<0)return;
                const int distance=(x-cx)*(x-cx)+(y-cy)*(y-cy);
                if(distance<best || (distance==best && (y<by || (y==by && x<bx)))){best=distance;bx=x;by=y;layer=candidate;}
        };
        // Visit each square perimeter once, preserving distance/y/x ties.
        // Re-scanning the enclosed square at every radius is cubic in size.
        if(!radius)consider(cx,cy);
        else {
            for(int x=cx-radius;x<=cx+radius;++x){consider(x,cy-radius);consider(x,cy+radius);}
            for(int y=cy-radius+1;y<cy+radius;++y){consider(cx-radius,y);consider(cx+radius,y);}
        }
        if(layer>=0){e.nativeAirLandingRetry=0;e.goalX=bx+.5f;e.goalY=by+.5f;e.goalLayer=(uint8_t)layer;
            nativeJumpjetToward(e,e.goalX,e.goalY);return true;}
    }
    e.nativeAirLandingRetry=tick+30;e.goalX=e.x;e.goalY=e.y;return false;
}
