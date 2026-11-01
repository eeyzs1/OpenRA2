#include "game/game.h"
#include <algorithm>

bool Game::buildingVisible(int x,int y,BldType type) const {
    const auto& d=bldDef(type);
    for(int ty=y;ty<y+d.h;++ty) for(int tx=x;tx<x+d.w;++tx)
        if(world.map.inBounds(tx,ty) && world.map.fogAt(localPlayer,tx,ty)==FOG_VISIBLE) return true;
    return false;
}
bool Game::buildingExplored(const KnownBuilding& b) const {
    const auto& d=bldDef(b.type);
    for(int ty=b.y;ty<b.y+d.h;++ty) for(int tx=b.x;tx<b.x+d.w;++tx)
        if(world.map.inBounds(tx,ty) && world.map.fogAt(localPlayer,tx,ty)==FOG_SEEN) return true;
    return false;
}
void Game::syncFogKnowledge() {
    auto& k=fogKnowledge;
    if(k.epoch!=world.presentationEpoch || k.player!=localPlayer || k.w!=world.map.w || k.h!=world.map.h) {
        k=FogKnowledge{};k.epoch=world.presentationEpoch;k.player=localPlayer;k.w=world.map.w;k.h=world.map.h;
        k.cells.resize((size_t)k.w*k.h);k.known.resize(k.cells.size());minimapTimer=0;
    }
    for(int y=0;y<k.h;++y) for(int x=0;x<k.w;++x)
        if(world.map.fogAt(localPlayer,x,y)==FOG_VISIBLE) {
            const int index=y*k.w+x;k.cells[index]=world.map.at(x,y);k.known[index]=1;
        }
    // Re-observation removes a silhouette even if its former entity ID was reused.
    for(auto i=k.buildings.begin();i!=k.buildings.end();) {
        const auto& b=i->second;
        if(buildingVisible(b.x,b.y,b.type)) i=k.buildings.erase(i);else ++i;
    }
    for(const auto& e:world.ents) {
        if(!e.alive || !e.isBuilding) continue;
        const int x=(int)e.x,y=(int)e.y,key=y*k.w+x;
        if(e.player==localPlayer) { k.buildings.erase(key);continue; }
        if(!buildingVisible(x,y,e.btype)) continue;
        KnownBuilding b;b.x=x;b.y=y;b.player=e.player;b.type=e.btype;
        b.height=world.map.at(x+bldDef(e.btype).w-1,y+bldDef(e.btype).h-1).height;b.constructAnim=e.constructAnim;b.selling=e.selling;
        b.damaged=e.hp*2<=bldDef(e.btype).hp;
        if(e.player>=0) { b.color=world.players[e.player].colorId;b.country=world.players[e.player].country; }
        else if(e.btype==BldType::OilDerrick || e.btype==BldType::Hospital || e.btype==BldType::TechAirport) b.color=-2;
        k.buildings[key]=b;
    }
}
const Cell& Game::observedCell(int x,int y) const {
    if(world.map.fogAt(localPlayer,x,y)==FOG_VISIBLE) return world.map.at(x,y);
    const int index=y*world.map.w+x;
    if(fogKnowledge.epoch==world.presentationEpoch && fogKnowledge.player==localPlayer
        && index>=0 && index<(int)fogKnowledge.known.size() && fogKnowledge.known[index]) return fogKnowledge.cells[index];
    static const Cell unknown{};return unknown;
}

bool Game::writeFogKnowledge(FILE* file) const {
    bool ok=true;auto write=[&](const auto& v) { if(ok && fwrite(&v,sizeof(v),1,file)!=1) ok=false; };
    const auto& k=fogKnowledge;write(k.player);write(k.w);write(k.h);
    for(size_t i=0;i<k.cells.size();++i) {
        write(k.known[i]);const auto& c=k.cells[i];write(c.terrain);write(c.overlay);write(c.variant);write(c.height);write(c.ore);write(c.oreMax);
    }
    const uint32_t count=(uint32_t)k.buildings.size();write(count);
    for(const auto& [key,b]:k.buildings) {
        write(b.x);write(b.y);write(b.player);write(b.color);write(b.height);write(b.constructAnim);write(b.type);write(b.country);
        const uint8_t selling=b.selling,damaged=b.damaged;write(selling);write(damaged);
    }
    return ok;
}
bool Game::readFogKnowledge(FILE* file,FogKnowledge& k) {
    bool ok=true;auto read=[&](auto& v) { if(ok && fread(&v,sizeof(v),1,file)!=1) ok=false; };
    read(k.player);read(k.w);read(k.h);
    if(!ok || k.w<1 || k.h<1 || k.w>512 || k.h>512) return false;
    k.cells.resize((size_t)k.w*k.h);k.known.resize(k.cells.size());
    for(size_t i=0;i<k.cells.size();++i) {
        read(k.known[i]);auto& c=k.cells[i];read(c.terrain);read(c.overlay);read(c.variant);read(c.height);read(c.ore);read(c.oreMax);
        if(k.known[i]>1 || (int)c.terrain>(int)Terrain::Bridge || (int)c.overlay>(int)Overlay::Rock2
            || c.ore<0 || c.oreMax<c.ore) return false;
    }
    uint32_t count=0;read(count);if(!ok || count>k.cells.size()) return false;
    for(uint32_t i=0;i<count;++i) {
        KnownBuilding b;uint8_t selling=0,damaged=0;
        read(b.x);read(b.y);read(b.player);read(b.color);read(b.height);read(b.constructAnim);read(b.type);read(b.country);read(selling);read(damaged);
        if(!ok || b.x<0 || b.y<0 || b.x>=k.w || b.y>=k.h || b.player < -1 || b.player>=MAX_PLAYERS
            || b.color < -2 || b.color>=8 || b.height<0 || b.height>255 || b.constructAnim<0 || b.constructAnim>1000000
            || (unsigned)b.type>=(unsigned)BldType::COUNT || (unsigned)b.country>=(unsigned)Country::COUNT || selling>1 || damaged>1) return false;
        b.selling=selling!=0;b.damaged=damaged!=0;
        if(!k.buildings.emplace(b.y*k.w+b.x,b).second) return false;
    }
    return ok;
}
bool Game::validateFogKnowledge(const FogKnowledge& k,const World& state,int player) {
    if(k.player!=player || k.w!=state.map.w || k.h!=state.map.h) return false;
    for(const auto& [key,b]:k.buildings) {
        const auto& d=bldDef(b.type);
        if(b.player>=state.numPlayers || b.player==player || b.x+d.w>k.w || b.y+d.h>k.h
            || (b.player>=0 && b.color<0)) return false;
    }
    return true;
}
