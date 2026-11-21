#include "game/world.h"
#include "game/native_nearby_search.h"
#include "game/native_nearby_clearance.h"
#include <cctype>

int World::nativeNearbySpeed(EID actor) const {
    if(!valid(actor) || !ents[actor].nativeIdentity.definition)return -1;
    auto name=ents[actor].nativeIdentity.definition->id;
    for(char& c:name)c=(char)std::tolower((unsigned char)c);
    const auto found=nativeAIRules.nearbySpeeds.find(name);
    return found==nativeAIRules.nearbySpeeds.end()?-1:found->second;
}

Vec2i World::nativeNearbyPoint(Vec2i requested,int speed,int footprint) const {
    if(!nativeAIRules.hasOriginalNearbyGeometry() || speed<0 || speed>8 || footprint<1 || footprint>8 || map.cells.empty())return {};
    const bool authored=map.native.active();
    const NativeNearbyMapBounds bounds=authored
        ?NativeNearbyMapBounds{map.native.width,map.native.height,map.native.local[0],map.native.local[1],map.native.local[2],map.native.local[3]}
        :NativeNearbyMapBounds{map.w,map.h,0,0,map.w,map.h};
    const auto cellAt=[&](Vec2i point) {
        NativeNearbyCellState result;
        if(!map.inBounds(point.x,point.y))return result;
        const auto index=cellIdx(point.x,point.y);const auto& cell=map.cells[index];
        result.present=true;result.level=cell.height;
        // The original bridge updater sets both bits from bridge presence.
        result.flags=nativeNearbyBridgeBits(map.hasDeck(point.x,point.y),false);
        if(authored && map.native.tiles.size()==map.cells.size()) {
            const auto& tile=map.native.tiles[index];result.ramp=tile.ramp;result.overlay=tile.overlay==255?-1:tile.overlay;
        } else if(cell.overlay!=Overlay::None)result.overlay=0;
        return result;
    };
    const auto usable=[&](Vec2i point) {
        return authored?nativeNearbyUsable(bounds,point,cellAt(point)):map.inBounds(point.x,point.y);
    };
    const auto clear=[&](Vec2i point,const NativeNearbyFootprint& spec) {
        // Source Winged bypasses occupancy, costs and zone checks entirely.
        if(spec.speed==4)return true;
        const auto properties=cellAt(point);NativeNearbyClearCell cell;
        cell.level=properties.level;cell.bridge=(properties.flags&0x100)!=0;
        // Original static InvalidCell is constructed with land0. Scene40
        // retains its earlier land3 padding interface when reading old saves.
        cell.land=nativeAIRules.hasOriginalSourceCoordinates()?0:3;
        if(properties.present) {
            const auto& terrain=map.at(point.x,point.y);
            if(authored && map.native.tiles.size()==map.cells.size()) {
                constexpr uint8_t land[16]={0,8,8,8,8,10,9,3,3,2,6,1,1,0,7,3};
                cell.land=land[map.native.tiles[cellIdx(point.x,point.y)].terrainType];
            } else {
                cell.land=terrain.terrain==Terrain::Water?2:terrain.terrain==Terrain::Rough?7:terrain.terrain==Terrain::Bridge?1:0;
                if(terrain.blocked || terrain.overlay==Overlay::Rock1 || terrain.overlay==Overlay::Rock2)cell.groundOccupancy|=0x80;
            }
            if(terrain.ore>0 && (terrain.terrain==Terrain::Ore || terrain.terrain==Terrain::Gems))cell.land=5;
            if(bldBlocked(point.x,point.y,0))cell.groundOccupancy|=0x40;
            // The source Nearby callers never ignore either occupancy group;
            // their zero/nonzero admission is supplied by the shared spatial index.
            if(unitAtCell(point.x,point.y,0)!=INVALID_EID)cell.groundOccupancy|=0x20;
            if(unitAtCell(point.x,point.y,1)!=INVALID_EID)cell.deckOccupancy|=0x20;
            const auto wall=nativeAIRules.nearbyWalls.find(properties.overlay);
            if(wall!=nativeAIRules.nearbyWalls.end()){cell.wall=true;cell.crushable=wall->second;}
        }
        NativeNearbyClearQuery query;query.speed=spec.speed;query.wantedZone=spec.zone;query.movement=spec.movement;query.alt=spec.alt;
        const auto cost=[&](int land,int type){return nativeMovementRules.available?(float)nativeMovementRules.land[land][type]:0.f;};
        return nativeNearbyClear(cell,query,[](int,bool){return -1;},cost);
    };
    NativeNearbyFootprint space;space.width=space.height=footprint;space.speed=speed;
    const auto place=[&](Vec2i point){return nativeNearbyCanPlace(point,space,cellAt,clear);};
    const auto project=[&](Vec2i point){return nativeNearbyProjection({point.x*256+128,point.y*256+128},cellAt);};
    NativeNearbyQuery query;query.origin=requested;query.mapWidth=bounds.width;query.mapHeight=bounds.height;
    static_assert(LOGIC_FPS>=15 && LOGIC_FPS%15==0);
    query.frame=nativeNearbyFrame(tick,LOGIC_FPS/15);
    // Original House uses 1x1; scripts53/54 use 3x3. Both use normal movement,
    // no zone/level constraint, no extra predicate and allow bridge surfaces.
    return nativeNearbySearch(query,cellAt,usable,place,[](Vec2i){return false;},project);
}
