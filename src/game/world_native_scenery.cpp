#include "game/world.h"

void World::retireNativeScenery(const Ent& actor,bool scriptedCollapse) {
    if(nativeScenarioSource.empty() || !map.native.active())return;
    const auto& definition=actor.nativeIdentity.definition;
    const int x=(int)actor.x,y=(int)actor.y;
    if(definition && definition->kind>=2 && map.inBounds(x,y)) {
        auto& tile=map.native.tiles[cellIdx(x,y)];
        if(tile.overlay==definition->overlay) {
            tile.overlay=255;tile.overlayData=0;
            map.at(x,y).blocked=tile.terrainType==7 || tile.terrainType==15;
            if(definition->wall) {
                constexpr int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
                for(int direction=0;direction<4;++direction) {
                    const int nx=x+dx[direction],ny=y+dy[direction];if(!map.inBounds(nx,ny))continue;
                    auto& neighbor=map.native.tiles[cellIdx(nx,ny)];if(neighbor.overlay!=definition->overlay)continue;
                    unsigned connections=0;
                    for(int side=0;side<4;++side) {
                        const int ax=nx+dx[side],ay=ny+dy[side];
                        if(map.inBounds(ax,ay) && map.native.tiles[cellIdx(ax,ay)].overlay==definition->overlay)connections|=1u<<side;
                    }
                    neighbor.overlayData=(uint8_t)((neighbor.overlayData&0xf0)|connections);
                }
            }
        }
    }
    // Source scenery can overlap a building footprint. Restore any surviving
    // blocker after kill removed the retiring actor's own occupancy, with the
    // same last-ID preference as initial scene placement.
    const auto& footprint=bldDef(actor);
    for(int cy=y;cy<y+footprint.h;++cy)for(int cx=x;cx<x+footprint.w;++cx) {
        if(!map.inBounds(cx,cy) || bldOcc[cellIdx(cx,cy)]>0)continue;
        for(EID id=0;id<(EID)ents.size();++id) {
            const auto& other=ents[id];if(!other.alive || !other.isBuilding)continue;
            if(other.nativeIdentity.definition && !other.nativeIdentity.definition->blocksMovement)continue;
            const auto& d=bldDef(other);
            if(cx>=other.x && cy>=other.y && cx<other.x+d.w && cy<other.y+d.h)bldOcc[cellIdx(cx,cy)]=id+1;
        }
    }
    if(definition && definition->kind==3)rebuildNativeBridges(true,scriptedCollapse);
    if(definition && definition->gate)rebuildNativeGates();
}
