#include "game_regression.h"
#include "game/native_source_coordinates.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {
std::string text(const std::string& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
std::vector<std::vector<int>> rows(const char* name) {
    std::ifstream file(std::string("tests/fixtures/")+name);std::string line;std::getline(file,line);
    std::vector<std::vector<int>> result;
    while(std::getline(file,line))if(!line.empty()) {
        std::vector<int> row;std::stringstream input(line);std::string token;
        try{while(std::getline(input,token,','))row.push_back(std::stoi(token));}catch(...){return {};}
        result.push_back(std::move(row));
    }
    return result;
}
std::vector<char> save(const World& world) {
    std::vector<char> result;FILE* file=tmpfile();if(!file)return result;
    if(world.saveGame(file)){const auto size=ftell(file);if(size>0){result.resize(size);rewind(file);if(fread(result.data(),1,result.size(),file)!=result.size())result.clear();}}
    fclose(file);return result;
}
bool load(World& world,const std::vector<char>& data) {
    FILE* file=tmpfile();if(!file)return false;const bool wrote=fwrite(data.data(),1,data.size(),file)==data.size();rewind(file);
    const bool ok=wrote && world.loadGame(file);fclose(file);return ok;
}
bool initialize(World& world,const std::string& source) {
    World::RulesSnapshot rules{source};return world.init(64,64,20261118,1,1,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,nullptr,{Country::Russia,Country::America},&rules);
}
void flat(World& world) {
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
    world.map.native={};world.map.bridgeCells.clear();world.map.gateCells.clear();
    auto& native=world.map.native;native.assetId="source-coordinate-fixture";native.sourceHash=std::string(64,'0');native.theater="TEMPERATE";
    native.width=native.height=32;native.local={0,0,32,32};native.tiles.resize(world.map.cells.size());
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        auto& cell=world.map.at(x,y);cell=Cell{};cell.terrain=Terrain::Clear;
        cell.present=std::abs(x-y)<32 && x+y>=33 && x+y<=96;cell.blocked=!cell.present;
        auto& tile=native.tiles[world.cellIdx(x,y)];tile.overlay=255;tile.terrainType=0;tile.ramp=0;
    }
    world.rebuildUnitOcc();world.rebuildAirOcc();world.cratesEnabled=false;
    for(auto& player:world.players){player.active=true;player.defeated=false;player.money=100000;}
}
std::string trim(std::string value) {
    while(!value.empty() && std::isspace((unsigned char)value.back()))value.pop_back();
    size_t begin=0;while(begin<value.size() && std::isspace((unsigned char)value[begin]))++begin;
    return value.substr(begin);
}
std::string dimensions(const std::string& source,int type,int width,int height) {
    const std::string section="[Type."+std::to_string(type)+"]";std::stringstream input(source);std::string line,result;
    bool active=false;int replaced=0;
    while(std::getline(input,line)) {
        const auto stripped=trim(line);if(!stripped.empty() && stripped.front()=='[')active=stripped==section;
        if(active) {
            const auto equal=line.find('=');const auto key=trim(line.substr(0,equal));
            if(equal!=std::string::npos && (key=="Width" || key=="Height")) {
                line=key+" = "+std::to_string(key=="Width"?width:height);++replaced;
            }
        }
        result+=line+'\n';
    }
    return replaced==2?result:std::string{};
}
}

int GameRegression::nativeSourceCoordinateRegression(const char* outputDir) {
    const std::string directory=outputDir;
    for(const char* edition:{"ra2","yr"})if(!FileExists((directory+"/"+edition+"-profile.ini").c_str()))return 77;
    int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("SOURCE_COORD %s %s\n",ok?"PASS":"FAIL",message);std::fflush(stdout);failures+=!ok;};
    const auto coordinateRows=rows("native-source-coordinates-ra2.csv"),houseRows=rows("native-source-house-ra2.csv"),nearbyRows=rows("native-source-nearby-ra2.csv");
    bool exact=coordinateRows.size()==368;
    for(const auto& row:coordinateRows) {
        if(row.size()!=9){exact=false;continue;}
        NativeSourceCoordinates point{row[3],row[4],row[5]};if(row[0])point=nativeSourceBuildingCoordinates(point,row[1],row[2]);
        exact &= point.x==row[6] && point.y==row[7] && point.z==row[8];
    }
    check(exact,"original raw object and foundation-relative building coordinates samples=368");
    check(houseRows.size()==336 && nearbyRows.size()==336,"complete original House and query fixtures include the real map-edge cases samples=672");
    for(const char* edition:{"ra2","yr"}) {
        const auto source=text(directory+"/"+edition+"-profile.ini");World current;
        bool installed=initialize(current,source);
        check(installed && current.nativeSceneVersion==41 && current.nativeAIRules.hasOriginalSourceCoordinates()
              && current.nativeAIRules.hasOriginalNearbyGeometry(),"Scene41 installs both original coordinate and query interfaces");
        if(!installed)continue;
        const auto path=directory+"/source-coordinates/"+edition;std::filesystem::create_directories(path);
        const auto dump=[&](const char* name,const World& world){const auto data=save(world);std::ofstream file(path+"/"+name,std::ios::binary);
            file.write(data.data(),data.size());return !data.empty();};
        int yard=-1;for(int i=0;i<(int)current.nativeTypes.size();++i)if(current.nativeTypes[i]->id=="GACNST")yard=i;
        check(yard>=0,"source catalog contains the actual construction-yard definition for declared foundation fixtures");
        bool all=yard>=0,savedCenter=false,savedEdge=false;int processed=0;
        if(yard>=0)for(int foundation=0;foundation<21;++foundation) {
            const auto first=std::find_if(houseRows.begin(),houseRows.end(),[&](const auto& row){return row.size()==8 && row[0]==foundation;});
            if(first==houseRows.end()){all=false;continue;}
            // Declared custom immutable foundation dimensions isolate original
            // House/GetCoords/query math. They do not count as natural AI picks.
            const auto custom=dimensions(source,yard,(*first)[1],(*first)[2]);World world;
            if(custom.empty() || !initialize(world,custom)){all=false;continue;}flat(world);
            for(const auto& row:houseRows)if(row.size()==8 && row[0]==foundation) {
                world.tick=(uint64_t)row[5]*2;
                const auto actor=world.spawnNativeActor(yard,0,(float)row[3],(float)row[4],256,0,"", "", "");
                const bool live=world.valid(actor);all &= live;
                if(live) {
                    const auto point=world.nativeAI.centers[0].center;all &= point==Vec2i{row[6],row[7]};++processed;
                    if(!savedCenter && row[1]==4 && row[2]==4 && row[3]==20 && row[4]==20 && row[5]==0) {
                        const auto data=save(world);World replay;bool same=!data.empty() && load(replay,data) && save(replay)==data;
                        check(same && dump("house-current.world",world),"actual corrected foundation center preserves exact World62 bytes");
                        for(int frame=0;frame<120 && same;++frame){world.update();replay.update();same &= world.checksum()==replay.checksum();}
                        check(same,"the corrected house state resumes with actual World updates");
                        check(load(world,data) && save(world)==data,"the isolated foundation case restores its checkpoint before further oracle inputs");savedCenter=true;
                    }
                    if(!savedEdge && point.x>=world.map.w*256) {
                        const auto data=save(world);World replay;bool same=!data.empty() && load(replay,data) && save(replay)==data;
                        check(same && point==Vec2i{16512,8576} && dump("house-edge.world",world),"real original Foot query center outside the canvas saves and reloads");
                        char pattern[8];std::memcpy(pattern,&point.x,4);std::memcpy(pattern+4,&point.y,4);std::vector<size_t> offsets;
                        for(size_t at=0;at+8<=data.size();++at)if(!std::memcmp(data.data()+at,pattern,8))offsets.push_back(at);
                        check(offsets.size()==1,"the actual source house center has one bounded mutation location");
                        if(offsets.size()==1) {
                            auto bad=data;int value=-1;std::memcpy(bad.data()+offsets[0],&value,4);World rejected;
                            check(!load(rejected,bad),"negative source house center remains invalid");
                            bad=data;value=(world.map.w+32)*256;std::memcpy(bad.data()+offsets[0],&value,4);World tooFar;
                            check(!load(tooFar,bad),"source house center outside the query domain remains invalid");
                        }
                        for(int frame=0;frame<120 && same;++frame){world.update();replay.update();same &= world.checksum()==replay.checksum();}
                        check(same,"the original outside-canvas house center resumes deterministically");
                        check(load(world,data) && save(world)==data,"the isolated edge case restores its checkpoint before further oracle inputs");savedEdge=true;
                    }
                    world.kill(actor,false);
                }
            }
        }
        check(all && processed==336,"actual source building lifecycle matches the complete original House/GetCoords/clearance pipeline samples=336");
        check(savedCenter && savedEdge,"both ordinary and source-edge house checkpoint paths executed");
        flat(current);all=nearbyRows.size()==336;
        for(const auto& row:nearbyRows) {
            if(row.size()!=7){all=false;continue;}current.tick=(uint64_t)row[2]*2;
            all &= current.nativeNearbyPoint({row[0],row[1]},row[3],row[4])==Vec2i{row[5],row[6]};
        }
        check(all,"actual default-cell provider matches complete original Nearby and clearance samples=336");
        check(dump("query-current.world",current),"actual Scene41 query context saves");
        for(const char* stage:{"house-cleared","staging","air-focus"}) {
            const auto raw=text(std::string("tests/fixtures/native-source-coordinates-v62/")+edition+"-"+stage+".world");
            const std::vector<char> data(raw.begin(),raw.end());World old,replay;
            bool same=!data.empty() && load(old,data) && load(replay,data) && old.nativeSceneVersion==40
                && !old.nativeAIRules.hasOriginalSourceCoordinates() && save(old)==data;
            for(int frame=0;frame<120 && same;++frame){old.update();replay.update();same &= old.checksum()==replay.checksum();}
            check(same,"actual Scene40 World62 retains its earlier coordinates and cell interface");
        }
        const auto app=std::string("tests/fixtures/native-source-coordinates-v62/")+edition+"-tick0.app11";
        const auto output=path+"/legacy40.app11";
        check(loadGameFile(app.c_str()) && world.nativeSceneVersion==40 && !world.nativeAIRules.hasOriginalSourceCoordinates()
              && saveGameFile(output.c_str()) && text(app)==text(output),"actual Scene40 App11 preserves its own coordinate contract exactly");
    }
    return failures;
}
