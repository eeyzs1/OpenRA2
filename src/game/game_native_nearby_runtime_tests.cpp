#include "game_regression.h"
#include "game/native_nearby_clearance.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

int nativeNearbyClearanceRegression();
namespace {
std::vector<char> bytes(const std::string& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
std::vector<char> save(const World& world) {
    std::vector<char> result;FILE* file=tmpfile();if(!file)return result;
    if(world.saveGame(file)){const auto n=ftell(file);if(n>0){result.resize(n);rewind(file);if(fread(result.data(),1,result.size(),file)!=result.size())result.clear();}}
    fclose(file);return result;
}
bool load(World& world,const std::vector<char>& data) {
    FILE* file=tmpfile();if(!file)return false;const bool wrote=fwrite(data.data(),1,data.size(),file)==data.size();rewind(file);
    const bool ok=wrote && world.loadGame(file);fclose(file);return ok;
}
bool initialize(World& world,const std::string& source) {
    World::RulesSnapshot snapshot{source};return world.init(64,64,20261118,1,1,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,nullptr,{Country::Russia,Country::America},&snapshot);
}
void flat(World& world,bool authored) {
    for(EID i=0;i<(EID)world.ents.size();++i)if(world.valid(i))world.kill(i,false);
    world.map.native={};world.map.bridgeCells.clear();world.map.gateCells.clear();
    if(authored) {
        auto& map=world.map.native;map.assetId="nearby-fixture";map.sourceHash=std::string(64,'0');map.theater="TEMPERATE";
        map.width=map.height=32;map.local={0,0,32,32};map.tiles.resize(world.map.cells.size());
    }
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        auto& cell=world.map.at(x,y);cell=Cell{};cell.terrain=Terrain::Clear;
        cell.present=!authored || (std::abs(x-y)<32 && x+y>=33 && x+y<=96);cell.blocked=!cell.present;
    }
    world.rebuildUnitOcc();world.rebuildAirOcc();world.cratesEnabled=false;
    for(auto& player:world.players){player.active=true;player.defeated=false;player.money=100000;}
}
int force(World& world,const char* type) {
    for(int index=0;index<(int)world.nativeEventDefinition->teams.size();++index) {
        const auto& team=world.nativeEventDefinition->teams[index];const auto& task=world.nativeEventDefinition->taskForces[team.taskForce];
        int count=0;bool exact=true;for(const auto& row:task.units){count+=row.first;exact &= row.second==type;}
        if(exact && count>0 && count<=20 && world.nativeTeamBlocker(index)==-1)return index;
    }
    return -1;
}
std::string stagingScript(std::string source,std::string id) {
    for(char& c:id)c=(char)std::tolower((unsigned char)c);
    const auto start=source.find("[NativeEvent.record."+id+"]");if(start==std::string::npos)return {};
    const auto end=source.find("\n[",start),line=source.find("\n0",start);
    if(line==std::string::npos || (end!=std::string::npos && line>end))return {};
    const auto finish=source.find('\n',line+1);if(finish==std::string::npos)return {};
    source.replace(line+1,finish-line-1,"0=54,0");return source;
}
std::string safeDistance(std::string source,int value) {
    const auto header=source.find("[NativeAI]");if(header==std::string::npos)return {};
    const auto end=source.find("\n[",header),start=source.find("\nAISafeDistance",header);
    if(start==std::string::npos || (end!=std::string::npos && start>end))return {};
    const auto finish=source.find('\n',start+1);if(finish==std::string::npos)return {};
    source.replace(start+1,finish-start-1,"AISafeDistance="+std::to_string(value));return source;
}
int populate(World& world,int definition,const char* type) {
    world.players[0].isAI=true;
    if(!world.configureNativeSkirmishAI(0,1,true,1))return -1;
    world.nativeAI.houses[0].production=false;const int team=world.createNativeTeam(definition,false);if(team<0)return -1;
    world.nativeEvents.teams[team].memberHouse=0;world.nativeAI.autocreate[definition]=1;
    const int native=world.nativeUnitIndex(type);if(native<0)return -1;
    int wanted=0;for(const auto& row:world.nativeEventDefinition->taskForces[world.nativeEventDefinition->teams[definition].taskForce].units)wanted+=row.first;
    for(int n=0;n<wanted;++n){const auto actor=world.spawnUnit(0,world.nativeTypes[native]->unit.type,27.5f+n%4,29.5f+n/4,native);
        if(!world.valid(actor))return -1;world.nativeEvents.teams[team].members.push_back(actor);world.nativeAIJoined(team,actor);}
    world.nativeEvents.teams[team].assembled=true;world.nativeAI.houses[0].enabled=false;
    return team;
}
}

int GameRegression::nativeNearbyRuntimeRegression(const char* outputDir) {
    const std::string directory=outputDir;
    for(const char* edition:{"ra2","yr"})if(!FileExists((directory+"/"+edition+"-profile.ini").c_str()))return 77;
    int failures=nativeNearbyClearanceRegression();
    const auto check=[&](bool ok,const char* name){std::printf("NEARBY_RUNTIME %s %s\n",ok?"PASS":"FAIL",name);std::fflush(stdout);failures+=!ok;};
    for(const char* edition:{"ra2","yr"}) {
        const auto input=bytes(std::string("tests/fixtures/native-source-coordinates-v62/")+edition+"-profile.ini");const std::string source(input.begin(),input.end());
        const auto oldInput=bytes(std::string("tests/fixtures/native-nearby-v62/")+edition+"-profile.ini");const std::string oldSource(oldInput.begin(),oldInput.end());
        const auto path=directory+"/nearby-runtime/"+edition;std::filesystem::create_directories(path);
        const auto dump=[&](const char* name,const World& w){const auto data=save(w);std::ofstream f(path+"/"+name,std::ios::binary);f.write(data.data(),data.size());return !data.empty();};
        World current,legacy;const bool installed=initialize(current,source) && initialize(legacy,oldSource);
        check(installed && current.nativeSceneVersion==40 && current.nativeAIRules.hasOriginalNearbyGeometry()
              && legacy.nativeSceneVersion==39 && !legacy.nativeAIRules.hasOriginalNearbyGeometry(),"actual Scene40 and frozen Scene39 install distinct query contracts");
        if(!installed)continue;
        flat(current,false);flat(legacy,false);
        current.spawnBuilding(0,BldType::ConYard,5,5,true);legacy.spawnBuilding(0,BldType::ConYard,5,5,true);
        check(current.nativeAI.centers[0].center==Vec2i{4*256+128,4*256+128}
              && legacy.nativeAI.centers[0].center==Vec2i{5*256+128,5*256+128},"real building lifecycle uses source occupancy clearance while Scene39 retains its center");
        check(dump("house-cleared.world",current),"the delegated Scene40 house center saves");
        current.map.at(13,13).terrain=Terrain::Water;
        check(current.nativeNearbyPoint({12,12},0,1)==Vec2i{12,12} && current.nativeNearbyPoint({12,12},0,3)==Vec2i{10,10},
              "a real water cell distinguishes original single-cell house and3x3 staging admission");
        current.map.at(20,20).terrain=Terrain::Water;
        check(current.nativeNearbyPoint({20,20},0)!=Vec2i{20,20} && current.nativeNearbyPoint({20,20},3)==Vec2i{20,20}
              && current.nativeNearbyPoint({20,20},5)==Vec2i{20,20},"actual source Foot, Float and Hover costs select different surfaces");
        const auto actor=current.spawnUnit(0,UnitType::Conscript,12.5f,12.5f);
        check(current.valid(actor) && current.nativeNearbySpeed(actor)==0 && current.nativeNearbyPoint({12,12},0)!=Vec2i{12,12}
              && current.nativeNearbyPoint({12,12},4)==Vec2i{12,12},"real unit occupancy blocks ground searches and source Winged bypasses it");
        current.tick=1;const auto first=current.nativeNearbyPoint({12,12},0);current.tick=2;
        check(current.nativeNearbyPoint({12,12},0)==first,"both World substeps share the same pre-incremented source query frame");
        check(dump("admission.world",current),"actual source cost and occupancy state saves");

        const int ground=force(current,"E2"),air=force(current,"ORCA");
        check(ground>=0 && air>=0,"original source graph supplies conscript and aircraft forces for isolated script checks");
        if(ground>=0) {
            const auto script=current.nativeEventDefinition->scripts[current.nativeEventDefinition->teams[ground].script].id;
            auto custom=stagingScript(source,script);
            // Declared custom fixture: source command54 with AISafeDistance0.
            // It isolates real orders/arrival and is not counted as natural AI selection.
            custom=safeDistance(custom,0);
            World stage;const bool started=!custom.empty() && initialize(stage,custom);check(started,"declared source-command54 fixture initializes");
            if(started) {
                flat(stage,true);stage.spawnBuilding(0,BldType::ConYard,20,20,true);stage.spawnBuilding(1,BldType::ConYard,41,41,true);
                const int team=populate(stage,ground,"E2");check(team>=0,"source team receives real physical conscripts");
                if(team>=0) {
                    stage.updateNativeTeamStaging(team);const auto focus=nativeNearbyUnpack(stage.nativeEvents.teams[team].focusCell);
                    check(focus==Vec2i{17,17} && stage.nativeEvents.teams[team].lineStarted,"original3x3 clearance selects the known native-map staging cell");
                    const auto saved=save(stage);World replay;
                    bool exact=!saved.empty() && load(replay,saved) && save(replay)==saved,arrived=false;
                    check(exact && dump("staging.world",stage),"packed original staging cell preserves exact World62 bytes");
                    for(int frame=0;frame<1800 && exact;++frame) {
                        const auto before=stage.nativeEvents.teams[team];const bool all=std::all_of(before.members.begin(),before.members.end(),[&](EID id){return stage.valid(id)
                            && distf(stage.ents[id].x,stage.ents[id].y,focus.x+.5f,focus.y+.5f)<=stage.nativeEventDefinition->relaxedStrayDistance;});
                        stage.update();replay.update();exact &= stage.checksum()==replay.checksum();
                        if(stage.nativeEvents.teams[team].step!=0){arrived=all;break;}
                    }
                    check(exact && arrived,"actual conscripts reach the source focus before the script advances with deterministic resume");
                    check(dump("staging-arrived.world",stage),"the actual physical-arrival state saves");
                }
            }
        }
        if(air>=0) {
            const auto script=current.nativeEventDefinition->scripts[current.nativeEventDefinition->teams[air].script].id;
            const auto custom=stagingScript(source,script);World sky;
            const bool started=!custom.empty() && initialize(sky,custom);check(started,"declared aircraft source-command54 fixture initializes");
            if(started) {
                flat(sky,true);sky.spawnBuilding(0,BldType::ConYard,20,20,true);sky.spawnBuilding(1,BldType::ConYard,41,41,true);
                const int team=populate(sky,air,"ORCA");check(team>=0,"source team receives actual source aircraft");
                if(team>=0) {
                    // Explicit synthetic House centers isolate the valid original
                    // CellStruct outside the project's smaller map canvas.
                    sky.nativeAI.centers[0].center={62*256+128,32*256+128};sky.nativeAI.centers[1].center={63*256+128,33*256+128};
                    sky.updateNativeTeamStaging(team);const int packed=sky.nativeEvents.teams[team].focusCell;
                    check(nativeNearbyUnpack(packed)==Vec2i{64,34},"source Winged clearance retains its original cell outside the project canvas");
                    const auto saved=save(sky);World replay;bool exact=!saved.empty() && load(replay,saved) && save(replay)==saved;
                    check(exact && dump("air-focus.world",sky),"the outside-canvas source focus saves and loads without scalar aliasing");
                    if(exact) {
                        char pattern[4];std::memcpy(pattern,&packed,4);std::vector<size_t> offsets;
                        for(size_t p=0;p+4<=saved.size();++p)if(!std::memcmp(saved.data()+p,pattern,4))offsets.push_back(p);
                        check(offsets.size()==1,"the actual packed focus field has a unique bounded mutation location");
                        if(offsets.size()==1) {
                            auto bad=saved;int value=nativeNearbyPack({-1,34});std::memcpy(bad.data()+offsets[0],&value,4);World rejected;
                            check(!load(rejected,bad),"a negative packed source focus coordinate is rejected on load");
                            bad=saved;value=nativeNearbyPack({1000,34});std::memcpy(bad.data()+offsets[0],&value,4);World rejectedFar;
                            check(!load(rejectedFar,bad),"a packed source focus beyond the query domain is rejected on load");
                        }
                        for(int frame=0;frame<120 && exact;++frame){sky.update();replay.update();exact &= sky.checksum()==replay.checksum();}
                        check(exact,"actual source aircraft state resumes with the outside-canvas focus unchanged in both simulations");
                    }
                }
            }
        }
        for(const char* kind:{"weighted","boarding"}) {
            const auto data=bytes(std::string("tests/fixtures/native-nearby-v62/")+edition+"-"+kind+".world");World old,shadow;
            bool exact=!data.empty() && load(old,data) && old.nativeSceneVersion==39 && !old.nativeAIRules.hasOriginalNearbyGeometry()
                && save(old)==data && load(shadow,data);
            for(int frame=0;frame<600 && exact;++frame){old.update();shadow.update();exact &= old.checksum()==shadow.checksum();}
            check(exact,"actual preceding Scene39 World62 fixtures retain original bytes and resume");
        }
        const auto app=std::string("tests/fixtures/native-nearby-v62/")+edition+"-tick0.app11";const auto resaved=path+"/legacy39.app11";
        check(loadGameFile(app.c_str()) && saveGameFile(resaved.c_str()) && bytes(app)==bytes(resaved),"actual Scene39 App11 opens and resaves with its own embedded query contract");
    }
    return failures;
}
