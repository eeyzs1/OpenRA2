#include "game_regression.h"
#include "game/native_nearby_clearance.h"
#include "game/native_nearby_search.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
using Row=std::vector<int64_t>;
std::vector<Row> rows(const char* name) {
    std::ifstream file(std::string("tests/fixtures/native-nearby-")+name+"-ra2.csv");
    std::vector<Row> result;std::string line;
    while(std::getline(file,line))if(!line.empty() && line[0]!='#') {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream stream(line);Row row;int64_t value;
        while(stream>>value)row.push_back(value);result.push_back(std::move(row));
    }
    return result;
}
int signed32(int64_t value){const uint32_t bits=(uint32_t)value;int32_t result;std::memcpy(&result,&bits,4);return result;}
}

int nativeNearbyClearanceRegression() {
    int failures=0;
    const auto check=[&](bool ok,const char* name,size_t count){std::printf("NEARBY_RUNTIME %s %s samples=%zu\n",ok?"PASS":"FAIL",name,count);failures+=!ok;};
    std::array<std::array<uint32_t,108>,8> costs{};
    const auto tables=rows("costs");bool tablesValid=tables.size()==864;
    for(const auto& r:tables) {
        if(r.size()!=4 || r[0]<0 || r[0]>=8 || r[1]<0 || r[1]>=12 || r[2]<0 || r[2]>=9){tablesValid=false;continue;}
        costs[(size_t)r[0]][(size_t)r[1]*9+(size_t)r[2]]=(uint32_t)r[3];
    }
    check(tablesValid,"original nine-column float cost inputs",tables.size());
    {
        const auto cases=rows("clearance");bool exact=tablesValid && cases.size()==2785;
        for(const auto& r:cases) {
            if(r.size()<20 || r[15]<0 || r[15]>=8 || r.size()!=20+(size_t)r[19]){exact=false;continue;}
            NativeNearbyClearCell cell;cell.level=(int)r[0];cell.bridge=r[1]!=0;cell.groundOccupancy=(uint8_t)r[2];cell.deckOccupancy=(uint8_t)r[3];
            cell.land=(int)r[4];cell.wall=r[5]==0 && r[6]!=0;cell.crushable=r[7]!=0;
            NativeNearbyClearQuery q;q.speed=(int)r[8];q.ignoreInfantry=r[9]!=0;q.ignoreVehicles=r[10]!=0;q.wantedZone=signed32(r[11]);
            q.movement=(int)r[12];q.wantedLevel=signed32(r[13]);q.alt=r[14]!=0;
            int zones=0;bool zoneArguments=true;std::vector<int> reads;
            const auto zone=[&](int movement,bool alt){++zones;zoneArguments &= movement==q.movement && alt==q.alt;return signed32(r[16]);};
            const auto cost=[&](int land,int speed){const int index=land*9+speed;reads.push_back(index);const uint32_t bits=costs[(size_t)r[15]][index];float value;std::memcpy(&value,&bits,4);return value;};
            const bool result=nativeNearbyClear(cell,q,zone,cost);
            bool same=result==(r[17]!=0) && zones==r[18] && zoneArguments && reads.size()==(size_t)r[19];
            for(size_t i=0;i<reads.size() && 20+i<r.size();++i)same &= reads[i]==r[20+i];exact &= same;
        }
        check(exact,"actual occupancy, level, wall, zone and cost control flow",cases.size());
    }
    {
        const auto cases=rows("burrow");bool exact=cases.size()==2304;
        const NativeNearbyMapBounds bounds{32,32,0,0,32,32};
        for(const auto& r:cases) {
            if(r.size()<10 || 8+(size_t)r[7]+2>r.size()){exact=false;continue;}
            const size_t end=8+(size_t)r[7],expectedCount=(size_t)r[end+1];
            if(end+2+expectedCount!=r.size()){exact=false;continue;}
            NativeNearbyCellState state;state.ramp=(int)r[4];state.flags=(uint32_t)r[5];
            NativeNearbyBurrowCell cell;cell.usable=nativeNearbyUsable(bounds,{(int)r[0],(int)r[1]},state);cell.knownTile=r[2]==0;
            cell.allowedTile=r[3]!=0;cell.ramp=state.ramp;cell.flags=state.flags;std::vector<int> queried;
            const bool result=nativeNearbyBurrow(cell,r[6]!=0,(int)r[7],[&](int index){const int type=(int)r[8+index];queried.push_back(type);return type;});
            bool same=result==(r[end]!=0) && queried.size()==expectedCount;
            for(size_t i=0;i<queried.size() && end+2+i<r.size();++i)same &= queried[i]==r[end+2+i];exact &= same;
        }
        check(exact,"original extra tile predicate and repeated object traversal",cases.size());
    }
    {
        const auto cases=rows("bridge-flags");bool exact=cases.size()==576;
        for(const auto& r:cases)exact &= r.size()==4 && nativeNearbyBridgeBits(r[0]!=0,true)==((uint32_t)r[3]&0x1500);
        check(exact,"source bridge and projection-bit binding",cases.size());
    }
    {
        const auto cases=rows("frame");bool exact=cases.size()==9 && nativeNearbyFrame(0,2)==0;
        for(const auto& r:cases) {
            if(r.size()!=2){exact=false;continue;}
            const uint64_t step=(uint64_t)r[1];exact &= step==(uint64_t)r[0]+1
                && nativeNearbyFrame(step*2-1,2)==step && nativeNearbyFrame(step*2,2)==step;
        }
        check(exact,"pre-work source frame shared by two World substeps",cases.size());
    }
    return failures;
}
