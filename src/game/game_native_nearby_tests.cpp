#include "game_regression.h"
#include "game/native_nearby_search.h"
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace {
using Row=std::vector<int64_t>;
std::vector<Row> read(const char* name) {
    std::ifstream input(std::string("tests/fixtures/native-nearby-")+name+"-ra2.csv");
    std::vector<Row> rows;std::string line;
    while(std::getline(input,line))if(!line.empty() && line[0]!='#') {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream stream(line);Row row;int64_t value;
        while(stream>>value)row.push_back(value);rows.push_back(std::move(row));
    }
    return rows;
}
int signed32(int64_t value) {
    const uint32_t bits=(uint32_t)value;int32_t result;std::memcpy(&result,&bits,sizeof(result));return result;
}
struct Trace {
    uint64_t hash=14695981039346656037ull;uint32_t count=0;
    void value(uint32_t v){for(int i=0;i<4;++i){hash^=(v>>(i*8))&255;hash*=1099511628211ull;}}
    void add(std::initializer_list<int64_t> row){value((uint32_t)row.size());for(auto v:row)value((uint32_t)v);++count;}
    bool matches(const Row& row,size_t offset) const {
        if(offset+3>row.size())return false;auto finished=*this;finished.value(count);
        return row[offset]==count && finished.hash==((uint64_t)row[offset+1]<<32 | (uint32_t)row[offset+2]);
    }
};
struct Tile {NativeNearbyCellState state;bool blocked=true,burrow=false;};
bool inside(Vec2i p){return p.x>=0 && p.y>=0 && p.x<64 && p.y<64;}
}

int GameRegression::nativeNearbyRegression() {
    int failures=0;
    const auto check=[&](bool ok,const char* label,size_t samples){std::printf("NEARBY_KERNEL %s %s samples=%zu\n",ok?"PASS":"FAIL",label,samples);failures+=!ok;};
    {
        const auto rows=read("bounds");bool exact=rows.size()==792;
        for(const auto& r:rows) {
            if(r.size()!=12){exact=false;continue;}
            NativeNearbyMapBounds map{(int)r[0],(int)r[1],(int)r[2],(int)r[3],(int)r[4],(int)r[5]};
            NativeNearbyCellState cell;cell.level=(int)r[8];cell.ramp=(int)r[9];
            exact &= nativeNearbyUsable(map,{(int)r[6],(int)r[7]},cell,r[10]!=0)==(r[11]!=0);
        }
        check(exact,"original diamond, local rectangle, level and ramp bounds",rows.size());
    }
    {
        const auto rows=read("projection");bool exact=rows.size()==256;
        for(const auto& r:rows) {
            if(r.size()!=21){exact=false;continue;}
            const auto cellAt=[&](Vec2i p){NativeNearbyCellState cell;const int k=p.x-(int)r[3];
                if(inside(p) && k==p.y-(int)r[4] && k>=0 && k<7){cell.level=(int)r[7+k];cell.flags=(uint32_t)r[14+k];cell.present=true;}return cell;};
            exact &= nativeNearbyProjection({(int)r[0],(int)r[1]},cellAt)==Vec2i{(int)r[5],(int)r[6]};
        }
        check(exact,"original diagonal height and flag projection",rows.size());
    }
    {
        const auto rows=read("footprint");bool exact=rows.size()==320;
        for(const auto& r:rows) {
            if(r.size()<16){exact=false;continue;}size_t i=12;
            std::map<std::pair<int,int>,int> overlays;std::set<std::pair<int,int>> blocked;
            const size_t overlayCount=(size_t)r[11];
            if(i+overlayCount*3+1>r.size()){exact=false;continue;}
            for(size_t n=0;n<overlayCount;++n,i+=3)overlays[{(int)r[i],(int)r[i+1]}]=(int)r[i+2];
            const size_t blockedCount=(size_t)r[i++];if(i+blockedCount*2+3!=r.size()){exact=false;continue;}
            for(size_t n=0;n<blockedCount;++n,i+=2)blocked.insert({(int)r[i],(int)r[i+1]});
            NativeNearbyFootprint spec{(int)r[2],(int)r[3],signed32(r[4]),signed32(r[5]),signed32(r[6]),signed32(r[7]),r[8]!=0,r[9]!=0};Trace calls;
            const auto cellAt=[&](Vec2i p){NativeNearbyCellState cell;const auto it=overlays.find({p.x,p.y});if(inside(p) && it!=overlays.end())cell.overlay=it->second;return cell;};
            const auto clear=[&](Vec2i p,const NativeNearbyFootprint& s){const bool allowed=inside(p) && !blocked.contains({p.x,p.y});
                calls.add({p.x,p.y,s.speed,0,0,s.zone,s.movement,s.owner,s.alt,allowed});return allowed;};
            const bool result=nativeNearbyCanPlace({(int)r[0],(int)r[1]},spec,cellAt,clear);
            exact &= result==(r[10]!=0) && calls.matches(r,i);
        }
        check(exact,"original footprint, overlay rejection and all forwarded clearance arguments",rows.size());
    }
    {
        const auto rows=read("flat");bool exact=rows.size()==202;
        for(const auto& r:rows) {
            if(r.size()<16){exact=false;continue;}size_t i=7;
            std::set<std::pair<int,int>> blocked,allowed;const size_t blockedCount=(size_t)r[6];
            if(i+blockedCount*2+1>r.size()){exact=false;continue;}
            for(size_t n=0;n<blockedCount;++n,i+=2)blocked.insert({(int)r[i],(int)r[i+1]});
            const size_t allowedCount=(size_t)r[i++];if(i+allowedCount*2+8!=r.size()){exact=false;continue;}
            for(size_t n=0;n<allowedCount;++n,i+=2)allowed.insert({(int)r[i],(int)r[i+1]});
            NativeNearbyQuery q;q.origin={(int)r[0],(int)r[1]};q.frame=(uint32_t)r[2];q.closeTo={(int)r[3],(int)r[4]};q.mapWidth=q.mapHeight=64;Trace bounds,eligibility;
            const auto cellAt=[](Vec2i){return NativeNearbyCellState{};};
            const auto usable=[&](Vec2i p){const bool result=inside(p);bounds.add({p.x,p.y,result});return result;};
            const auto canPlace=[&](Vec2i p){const bool result=r[5]?allowed.contains({p.x,p.y}):!blocked.contains({p.x,p.y});eligibility.add({p.x,p.y,result});return result;};
            const auto project=[&](Vec2i p){return nativeNearbyProjection({p.x*256+128,p.y*256+128},cellAt);};
            const auto result=nativeNearbySearch(q,cellAt,usable,canPlace,[](Vec2i){return true;},project);
            exact &= result==Vec2i{(int)r[i],(int)r[i+1]} && bounds.matches(r,i+2) && eligibility.matches(r,i+5);
        }
        check(exact,"original traversal, frame choice and approximate-distance ties",rows.size());
    }
    {
        std::array<std::vector<Tile>,4> grids;std::array<NativeNearbyMapBounds,4> maps;
        for(auto& grid:grids)grid.resize(4096);
        const auto data=read("grids");bool exact=data.size()==8064;
        for(const auto& r:data) {
            if(r.size()!=15 || r[0]<0 || r[0]>=4 || !inside({(int)r[7],(int)r[8]})){exact=false;continue;}
            const int pattern=(int)r[0];maps[pattern]={(int)r[1],(int)r[2],(int)r[3],(int)r[4],(int)r[5],(int)r[6]};
            auto& tile=grids[pattern][(size_t)r[8]*64+(size_t)r[7]];
            tile.state={(int)r[9],(int)r[10],(uint32_t)r[11],(int)r[12],true};tile.blocked=r[13]!=0;tile.burrow=r[14]!=0;
        }
        check(exact,"actual original composite input cells",data.size());
        const auto rows=read("composite");exact &= rows.size()==256;
        int printed=0;
        for(const auto& r:rows) {
            if(r.size()!=34 || r[0]<0 || r[0]>=4){exact=false;continue;}
            const int pattern=(int)r[0];const auto& map=maps[pattern];std::array<Trace,5> trace;
            const auto tileAt=[&](Vec2i p){return inside(p)?grids[pattern][(size_t)p.y*64+p.x]:Tile{};};
            const auto cellAt=[&](Vec2i p){return tileAt(p).state;};
            NativeNearbyFootprint fp{(int)r[13],(int)r[14],signed32(r[4]),signed32(r[5]),signed32(r[6]),-1,r[7]!=0,r[8]!=0};
            NativeNearbyQuery q;q.origin={(int)r[1],(int)r[2]};q.closeTo={(int)r[15],(int)r[16]};q.frame=(uint32_t)r[3];
            q.mapWidth=map.width;q.mapHeight=map.height;q.alt=fp.alt;q.checkHeight=r[9]!=0;q.requireBurrow=r[10]!=0;q.allowBridge=r[11]!=0;q.option14=r[12]!=0;
            const auto usable=[&](Vec2i p){trace[0].add({p.x,p.y});return nativeNearbyUsable(map,p,cellAt(p));};
            const auto clear=[&](Vec2i p,const NativeNearbyFootprint& s){const auto tile=tileAt(p);const bool result=tile.state.present && !tile.blocked;
                trace[2].add({p.x,p.y,s.speed,0,0,s.zone,s.movement,s.owner,s.alt,result});return result;};
            const auto place=[&](Vec2i p){trace[1].add({p.x,p.y,fp.width,fp.height,fp.speed,fp.zone,fp.movement,fp.owner,fp.alt,fp.avoidOverlay});return nativeNearbyCanPlace(p,fp,cellAt,clear);};
            const auto burrow=[&](Vec2i p){const auto tile=tileAt(p);const bool result=tile.state.present && tile.burrow;trace[3].add({p.x,p.y,result});return result;};
            const auto project=[&](Vec2i p){trace[4].add({p.x*256+128,p.y*256+128,0});return nativeNearbyProjection({p.x*256+128,p.y*256+128},cellAt);};
            const auto result=nativeNearbySearch(q,cellAt,usable,place,burrow,project);bool same=result==Vec2i{(int)r[17],(int)r[18]};
            for(size_t channel=0;channel<5;++channel)same &= trace[channel].matches(r,19+channel*3);
            if(!same && printed++<4)std::printf("NEARBY_KERNEL CASE pattern=%d frame=%u actual=%d,%d expected=%lld,%lld\n",pattern,q.frame,result.x,result.y,(long long)r[17],(long long)r[18]);
            exact &= same;
        }
        check(exact,"all source flag combinations and complete composed call traces",rows.size());
    }
    return failures;
}
