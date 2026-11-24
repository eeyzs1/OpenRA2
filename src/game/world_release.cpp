#include "game/world.h"
#include "game/native_scene_contract.h"
#include <algorithm>

// A release is placement into a neighboring cell, so it has no diagonal
// walking-corner test. Surface, height, building and occupancy admission still
// use the project's existing map/actor model, not a reconstructed original map.
bool World::nativeTransportCell(const Ent& source,const UnitDef& ud,int x,int y,ReleaseCell& result) const {
    const int bx=(int)source.x,by=(int)source.y;
    const int distance=std::max(std::abs(x-bx),std::abs(y-by));
    if(distance<1 || distance>2 || !map.inBounds(x,y))return false;
    int layer=source.navLayer,px=bx,py=by;
    const int dx=(x-bx)/distance,dy=(y-by)/distance;
    for(int i=1;i<=distance;++i) {
        const int nx=bx+dx*i,ny=by+dy*i;
        layer=map.hasBridgeDecks()?map.stepLayer(px,py,layer,nx,ny,ud.pathDomain(),false,ud.fitsUnderBridge()):
            (map.climbOk(px,py,nx,ny,ud.pathDomain()) && map.passable(nx,ny)?0:-1);
        if(layer<0)return false;px=nx;py=ny;
    }
    if(bldBlocked(x,y,layer))return false;
    if(!ud.isInfantry()) {if(groundUnitBlocksCell(x,y,INVALID_EID,layer))return false;}
    else {
        if(countInfantryAtCell(x,y,INVALID_EID,layer)>=3)return false;
        for(EID id=unitAtCell(x,y,layer);id!=INVALID_EID;id=ents[id].occNext)
            if(ents[id].navLayer==layer && !unitDef(ents[id]).isInfantry())return false;
    }
    result={x,y,layer};return true;
}

NativeTransportMath::Exit World::nativeTransportExit(const Ent& source) const {
    uint8_t available=0;ReleaseCell ignored;
    for(int d=0;d<8;++d) {
        const auto& step=NativeTransportMath::directions[d];
        if(nativeTransportCell(source,unitDef(UnitType::Conscript),(int)source.x+step[0],(int)source.y+step[1],ignored))available|=uint8_t(1u<<d);
    }
    return NativeTransportMath::selectExit(nativeHullHeading(source,tick),available,source.nativeIdentity.definition->isTrain);
}

bool World::nativeTransportReleasePlan(const Ent& source,const UnitDef& passenger,TransportReleasePlan& plan) const {
    uint8_t nearCells=0,farCells=0;std::array<ReleaseCell,8> near{},far{};
    for(int d=0;d<8;++d) {
        const auto& step=NativeTransportMath::directions[d];const int x=(int)source.x,y=(int)source.y;
        // Original Mission_Unload rejects a near Cell with its bridge flag.
        if(!map.hasDeck(x+step[0],y+step[1]) && nativeTransportCell(source,passenger,x+step[0],y+step[1],near[d]))nearCells|=uint8_t(1u<<d);
        if(nativeTransportCell(source,passenger,x+step[0]*2,y+step[1]*2,far[d]))farCells|=uint8_t(1u<<d);
    }
    const auto choice=NativeTransportMath::selectRelease(nativeHullHeading(source,tick),nearCells,farCells);
    if(choice.direction<0)return false;
    plan={near[choice.direction],choice.moveFar?far[choice.direction]:near[choice.direction]};return true;
}

// Source footprint cells are virtual seeds. Only connected, empty cells in
// the existing local search box are exits; a second surface is a separate node.
std::vector<World::ReleaseCell> World::releaseCells(const Ent& source,const UnitDef& ud,int radius) const {
    if(radius<1 || radius>map.w+map.h)return {};
    const int bx=(int)source.x,by=(int)source.y;
    const int width=source.isBuilding?bldDef(source).w:1,height=source.isBuilding?bldDef(source).h:1;
    const int cx=source.isBuilding?(int)(source.x+width/2.f):bx,cy=source.isBuilding?(int)(source.y+height/2.f):by;
    const bool layered=map.hasBridgeDecks(),airSource=!source.isBuilding && unitDef(source).isAir();
    const bool landedAir=airSource && usesNativeAirNavigation(source) && !source.airborne();
    const int seedLayer=layered && (!airSource || landedAir)?source.navLayer:0,layers=layered?2:1,side=radius*2+1;
    std::vector<uint8_t> reached((size_t)side*side*layers,0);std::vector<ReleaseCell> queue;
    auto inBox=[&](int x,int y){return map.inBounds(x,y) && abs(x-cx)<=radius && abs(y-cy)<=radius;};
    auto inside=[&](int x,int y){return x>=bx && x<bx+width && y>=by && y<by+height;};
    auto index=[&](int x,int y,int layer){return (size_t)layer*side*side+(size_t)(y-cy+radius)*side+x-cx+radius;};
    auto occupied=[&](int x,int y,int layer) {
        if(!ud.isInfantry())return groundUnitBlocksCell(x,y,INVALID_EID,layer);
        if(countInfantryAtCell(x,y,INVALID_EID,layer)>=3)return true;
        for(EID id=unitAtCell(x,y,layer);id!=INVALID_EID;id=ents[id].occNext)
            if(ents[id].navLayer==layer && !unitDef(ents[id]).isInfantry())return true;
        return false;
    };
    auto open=[&](int x,int y,int layer) {
        if(!inBox(x,y))return false;
        if(inside(x,y) && layer==seedLayer)return true;
        return (layered?map.surfacePassable(x,y,ud.pathDomain(),layer,false,ud.fitsUnderBridge()):map.passable(x,y))
            && !bldBlocked(x,y,layer) && !occupied(x,y,layer);
    };
    for(int y=by;y<by+height;++y)for(int x=bx;x<bx+width;++x)if(inBox(x,y)) {
        reached[index(x,y,seedLayer)]=1;queue.push_back({x,y,seedLayer});
    }
    for(size_t n=0;n<queue.size();++n)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy) {
        const auto from=queue[n];const int x=from.x+dx,y=from.y+dy;
        const bool seed=inside(from.x,from.y) && from.layer==seedLayer;
        int layer=0;
        if(layered)layer=seed && airSource && !landedAir?map.preferredLayer(x,y,ud.pathDomain()):map.stepLayer(from.x,from.y,from.layer,x,y,ud.pathDomain(),false,ud.fitsUnderBridge());
        else if(!(seed && airSource && !landedAir) && !map.climbOk(from.x,from.y,x,y,ud.pathDomain()))continue;
        if(layer<0 || !open(x,y,layer) || reached[index(x,y,layer)])continue;
        if(dx && dy) {
            auto corner=[&](int sx,int sy) {
                if(!inBox(sx,sy))return false;
                if(inside(sx,sy))return true;
                // Shores may meet boats diagonally. Walls and vehicles still
                // close that corner, on the source surface rather than above it.
                if(seed && ((airSource && !landedAir) || (!airSource && map.at(from.x,from.y).terrain==Terrain::Water))) {
                    const int cornerLayer=airSource?map.preferredLayer(sx,sy,ud.pathDomain()):from.layer;
                    return !bldBlocked(sx,sy,cornerLayer) && !occupied(sx,sy,cornerLayer);
                }
                const int cornerLayer=layered?map.stepLayer(from.x,from.y,from.layer,sx,sy,ud.pathDomain(),false,ud.fitsUnderBridge()):0;
                if(cornerLayer<0 || !open(sx,sy,cornerLayer))return false;
                return !layered || map.stepLayer(sx,sy,cornerLayer,x,y,ud.pathDomain(),false,ud.fitsUnderBridge())==layer;
            };
            if(!corner(from.x+dx,from.y) || !corner(from.x,from.y+dy))continue;
        }
        reached[index(x,y,layer)]=1;queue.push_back({x,y,layer});
    }
    std::vector<ReleaseCell> cells;
    for(int r=1;r<=radius;++r)for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
        if(std::max(abs(dx),abs(dy))!=r)continue;
        const int x=cx+dx,y=cy+dy;if(!inBox(x,y) || inside(x,y))continue;
        for(int layer=0;layer<layers;++layer)if(reached[index(x,y,layer)] && !bldBlocked(x,y,layer) && unitAtCell(x,y,layer)==INVALID_EID)
            cells.push_back({x,y,layer});
    }
    return cells;
}
