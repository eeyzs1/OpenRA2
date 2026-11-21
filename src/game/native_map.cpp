#include "game/native_map.h"
#include "game/map.h"
#include <algorithm>
#include <cstring>

namespace {
struct Reader {
    FILE* f;
    bool ok=true;
    unsigned byte() { const int n=ok?fgetc(f):EOF;if(n==EOF){ok=false;return 0;}return (unsigned)n; }
    unsigned u16() { unsigned lo=byte(),hi=byte();return lo|(hi<<8); }
    unsigned u32() { unsigned lo=u16(),hi=u16();return lo|(hi<<16); }
    std::string string(unsigned limit) {
        const unsigned n=u16();if(!ok || n>limit){ok=false;return {};}
        std::string value;value.reserve(n);for(unsigned i=0;i<n;++i)value.push_back((char)byte());return value;
    }
};
struct Writer {
    FILE* f;bool ok=true;
    void byte(unsigned n) { if(ok && fputc((int)(n&255),f)==EOF)ok=false; }
    void u16(unsigned n) { byte(n);byte(n>>8); }
    void u32(unsigned n) { u16(n);u16(n>>16); }
    void string(const std::string& s) { u16((unsigned)s.size());for(unsigned char c:s)byte(c); }
};
bool validId(const std::string& value) {
    return !value.empty() && value.size()<=48 && std::all_of(value.begin(),value.end(),[](char c){
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-';});
}
bool headerValid(const NativeMapInfo& native,int side) {
    return validId(native.assetId) && native.width>0 && native.height>0 && native.width<=256 && native.height<=256
        && native.width+native.height==side && side<=512
        && (native.theater=="URBAN" || native.theater=="TEMPERATE" || native.theater=="SNOW")
        && native.local[0]>=0 && native.local[1]>=0 && native.local[2]>0 && native.local[3]>0
        && native.local[0]+native.local[2]<=native.width && native.local[1]+native.local[3]<=native.height
        && native.sourceHash.size()==64 && std::all_of(native.sourceHash.begin(),native.sourceHash.end(),[](char c){
            return (c>='0' && c<='9') || (c>='a' && c<='f');});
}
NativeTileInfo readTile(Reader& r) {
    NativeTileInfo n;n.tile=(uint16_t)r.u16();n.auxiliary=(uint16_t)r.u16();
    n.subtile=(uint8_t)r.byte();n.auxiliary2=(uint8_t)r.byte();n.overlay=(uint8_t)r.byte();
    n.overlayData=(uint8_t)r.byte();n.terrainType=(uint8_t)r.byte();n.ramp=(uint8_t)r.byte();return n;
}
void writeTile(Writer& w,const NativeTileInfo& n) {
    w.u16(n.tile);w.u16(n.auxiliary);w.byte(n.subtile);w.byte(n.auxiliary2);w.byte(n.overlay);
    w.byte(n.overlayData);w.byte(n.terrainType);w.byte(n.ramp);
}
}

bool validateNativeMap(const Map& map) {
    const auto& native=map.native;
    if(!native.active()) return native.tiles.empty() && std::all_of(map.cells.begin(),map.cells.end(),[](const Cell& c){return c.present;});
    if(map.w!=map.h || !headerValid(native,map.w) || native.tiles.size()!=map.cells.size())return false;
    size_t count=0;
    for(int y=0;y<map.h;++y)for(int x=0;x<map.w;++x) {
        const size_t i=(size_t)y*map.w+x;const auto& c=map.cells[i];const auto& n=native.tiles[i];
        const int dx=x-y+native.width-1,dy=x+y-native.width-1;
        const bool expected=dx>=0 && dx<2*native.width-1 && dy>=0 && dy<2*native.height
            && (dx&1)==(dy&1);
        if(c.present!=expected || (c.present && (n.terrainType>15 || n.ramp>20)))return false;
        if(c.present)++count;
    }
    return count==(size_t)(2*native.width-1)*native.height;
}

bool loadNativeMap(const char* path,Map& destination,int players,std::vector<Vec2i>& spawns) {
    FILE* file=fopen(path,"rb");if(!file)return false;Reader r{file};
    std::string magic;for(int i=0;i<8;++i)magic.push_back((char)r.byte());
    Map loaded;loaded.w=loaded.h=(int)r.u16();auto& native=loaded.native;
    native.width=(int)r.u16();native.height=(int)r.u16();for(int& v:native.local)v=(int)r.u16();
    static const char hex[]="0123456789abcdef";
    for(int i=0;i<32;++i){unsigned b=r.byte();native.sourceHash+=hex[b>>4];native.sourceHash+=hex[b&15];}
    native.assetId=r.string(48);native.theater=r.string(16);
    const unsigned count=r.u32();
    bool ok=r.ok && magic=="RA2NMAP1" && players>0 && players<=MAX_PLAYERS && headerValid(native,loaded.w)
        && count==(unsigned)(2*native.width-1)*native.height;
    if(ok) {
        loaded.cells.assign((size_t)loaded.w*loaded.h,Cell{});native.tiles.resize(loaded.cells.size());
        for(auto& c:loaded.cells){c.present=false;c.blocked=true;}
        for(unsigned i=0;i<count && r.ok;++i) {
            const unsigned x=r.u16(),y=r.u16();NativeTileInfo tile;
            tile.tile=(uint16_t)r.u16();tile.auxiliary=(uint16_t)r.u16();tile.subtile=(uint8_t)r.byte();
            const unsigned height=r.byte();tile.auxiliary2=(uint8_t)r.byte();tile.overlay=(uint8_t)r.byte();
            tile.overlayData=(uint8_t)r.byte();tile.terrainType=(uint8_t)r.byte();tile.ramp=(uint8_t)r.byte();
            const unsigned terrain=r.byte(),blocked=r.byte();
            if(x>=(unsigned)loaded.w || y>=(unsigned)loaded.h || height>14 || terrain>(unsigned)Terrain::Bridge
                || blocked>1 || tile.terrainType>15 || tile.ramp>20){r.ok=false;break;}
            const size_t index=(size_t)y*loaded.w+x;auto& cell=loaded.cells[index];
            if(cell.present){r.ok=false;break;}
            cell.present=true;cell.blocked=blocked!=0;cell.height=(uint8_t)height;cell.terrain=(Terrain)terrain;
            if(cell.terrain==Terrain::Ore)cell.ore=cell.oreMax=300;
            if(cell.terrain==Terrain::Gems)cell.ore=cell.oreMax=150;
            native.tiles[index]=tile;
        }
        ok=r.ok && fgetc(file)==EOF && validateNativeMap(loaded);
    }
    fclose(file);if(!ok)return false;
    // Native starting actors/waypoints belong to the original scenario. A
    // terrain-only import supplies no fabricated multiplayer starting points.
    destination=std::move(loaded);spawns.clear();return true;
}

bool writeNativeMapState(FILE* file,const Map& map) {
    if(!file || !validateNativeMap(map))return false;
    Writer w{file};const auto& n=map.native;w.byte(n.active()?1:0);
    if(n.active()) {
        w.string(n.assetId);w.string(n.sourceHash);w.string(n.theater);w.u16(n.width);w.u16(n.height);
        for(int v:n.local)w.u16(v);w.u32((unsigned)n.tiles.size());for(const auto& tile:n.tiles)writeTile(w,tile);
    }
    return w.ok;
}

bool readNativeMapState(FILE* file,Map& map) {
    Reader r{file};const unsigned active=r.byte();if(!r.ok || active>1)return false;
    map.native={};if(!active)return validateNativeMap(map);
    auto& n=map.native;n.assetId=r.string(48);n.sourceHash=r.string(64);n.theater=r.string(16);
    n.width=(int)r.u16();n.height=(int)r.u16();for(int& v:n.local)v=(int)r.u16();
    const unsigned count=r.u32();if(!r.ok || count!=map.cells.size() || !headerValid(n,map.w))return false;
    n.tiles.resize(count);for(auto& tile:n.tiles)tile=readTile(r);
    return r.ok && validateNativeMap(map);
}
