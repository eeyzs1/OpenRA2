#include "game/world.h"
#include <algorithm>
#include <limits>

bool World::canPlaceNativeBuilding(int player,int selector,int x,int y) const {
    if(player<0 || player>=numPlayers || !nativeProductionType(selector))return false;
    const auto& d=*nativeTypes[selector-NATIVE_PRODUCTION_BASE];if(!d.isBuilding)return false;
    const auto& b=d.building;int height=-1;
    for(int dy=0;dy<b.h;++dy)for(int dx=0;dx<b.w;++dx) {
        const int cx=x+dx,cy=y+dy;if(!map.inBounds(cx,cy))return false;
        if(d.production.naval?map.at(cx,cy).terrain!=Terrain::Water:!map.passable(cx,cy))return false;
        if(map.hasDeck(cx,cy) || map.at(cx,cy).terrain==Terrain::Bridge || bldOcc[cellIdx(cx,cy)]>0 || groundUnitBlocksCell(cx,cy,INVALID_EID))return false;
        if(!map.native.tiles.empty() && map.native.tiles[cellIdx(cx,cy)].ramp)return false;
        if(height>=0 && height!=map.at(cx,cy).height)return false;height=map.at(cx,cy).height;
    }
    for(const auto& e:ents)if(e.alive && e.isBuilding && e.player==player && !e.selling && e.nativeIdentity.definition
        && e.nativeIdentity.definition->production.available && e.nativeIdentity.definition->production.baseNormal) {
        const auto& other=bldDef(e);
        const int dx=std::max({(int)e.x-x-b.w,x-(int)e.x-other.w,0}),dy=std::max({(int)e.y-y-b.h,y-(int)e.y-other.h,0});
        if(std::max(dx,dy)<=d.production.adjacent)return true;
    }
    return false;
}

bool World::placeNativeBuilding(int player,int selector,int x,int y) {
    if(!canPlaceNativeBuilding(player,selector,x,y) || nativeProductionSerial==std::numeric_limits<uint64_t>::max())return false;
    auto& item=productionItem(player,false,selector);if(!item.active || !item.ready || item.typeIdx!=selector)return false;
    const int type=selector-NATIVE_PRODUCTION_BASE;const auto& d=*nativeTypes[type];
    const std::string key="production:"+std::to_string(player)+":"+std::to_string(nativeProductionSerial++);
    const EID building=spawnNativeActor(type,player,(float)x,(float)y,256,0,key,"","");if(!valid(building))return false;
    ents[building].constructAnim=g_gameRules.buildingAnimationTicks;item={};recomputePower();
    recordNativeBuilt(player,type);
    emitSoundAt(Sfx::Place,x+d.building.w/2.f,y+d.building.h/2.f);
    if(!d.production.freeUnit.empty()) {
        const int unit=nativeUnitIndex(d.production.freeUnit);
        if(unit>=0)for(int step=0;step<6;++step){const int cx=x+d.building.w/2,cy=y+d.building.h+step;
            if(!map.surfacePassable(cx,cy,nativeTypes[unit]->unit.pathDomain(),0,true,nativeTypes[unit]->unit.fitsUnderBridge()) || unitAtCell(cx,cy)!=INVALID_EID)continue;
            const EID actor=spawnProductionActor(player,nativeTypes[unit]->unit.type,cx+.5f,cy+.5f,unit);
            if(valid(actor) && unitDef(ents[actor]).canHarvet()){ents[actor].autoHarvest=true;Vec2i ore;if(findNearestReachableOre(cx,cy,48,ore))orderHarvest({actor},ore.x,ore.y);}break;}
    }
    return true;
}
