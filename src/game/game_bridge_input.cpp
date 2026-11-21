#include "game/game.h"
#include <cmath>

int Game::observedDeckHeight(int x,int y) const {
    if(!world.map.inBounds(x,y))return -1;
    const auto fog=world.map.fogAt(localPlayer,x,y);
    if(fog==FOG_VISIBLE)return world.map.hasDeck(x,y)?world.map.surfaceHeight(x,y,1):-1;
    if(fog!=FOG_SEEN || fogKnowledge.epoch!=world.presentationEpoch || fogKnowledge.player!=localPlayer)return -1;
    for(const auto& [key,memory]:fogKnowledge.buildings) {
        if(memory.nativeType<0 || memory.nativeType>=(int)world.nativeTypes.size())continue;
        const auto& spec=*world.nativeTypes[memory.nativeType];if(spec.kind!=3)continue;
        const bool alongY=spec.id=="BRIDGE2" || spec.id=="BRIDGEB2";
        if(alongY?y==memory.y && std::abs(x-memory.x)<=1:x==memory.x && std::abs(y-memory.y)<=1)return memory.height+4;
    }
    return -1;
}

void Game::surfaceAt(float x,float y,int& tileX,int& tileY,int* layer) const {
    screenToTile(x,y,tileX,tileY);if(layer)*layer=0;
    // Trace the visible/remembered isometric surfaces from front to back.
    // Generated terrain is elevated too. The original map permits height 14
    // plus the four-level bridge deck; the same projection applies to both.
    for(int height=18;height>=0;--height) {
        int tx,ty;screenToTile(x,y+height*(TILE_H/2.f),tx,ty);
        if(!world.map.inBounds(tx,ty) || world.map.fogAt(localPlayer,tx,ty)==FOG_UNSEEN)continue;
        if(observedDeckHeight(tx,ty)==height){tileX=tx;tileY=ty;if(layer)*layer=1;return;}
        if(observedCell(tx,ty).height==height){tileX=tx;tileY=ty;return;}
    }
}

bool Game::observedSurfacePassable(int x,int y,int domain,int layer,bool underBridge) const {
    if(!world.map.inBounds(x,y))return false;
    if(layer==1 && domain!=1)return observedDeckHeight(x,y)>=0;
    if(!underBridge && observedDeckHeight(x,y)>=0)return false;
    const auto& cell=observedCell(x,y);if(!cell.present || cell.blocked)return false;
    if(domain==1)return cell.terrain==Terrain::Water;
    return cell.passable() || (domain==2 && cell.terrain==Terrain::Water);
}
