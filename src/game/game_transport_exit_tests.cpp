#include "game_regression.h"
#include "game/native_ground.h"
#include "game/native_scene_contract.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
World fixture() {
    World w;w.init(64,64,20261123,2,0,{Faction::Soviet,Faction::Soviet},0,nullptr,false,false,
        "assets/rules/profiles/ra2.ini",{Country::Russia,Country::Iraq});
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& p:w.players){p.money=0;p.defeated=false;}
    w.spawnBuilding(0,BldType::ConYard,48,3,true);w.spawnBuilding(1,BldType::ConYard,48,48,true);return w;
}
void feedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void cmd(World& w,World::Cmd::Type type,EID id,EID target=INVALID_EID,float x=0,float y=0) {
    World::Cmd c{};c.type=type;c.ids={id};c.a=target;c.x=x;c.y=y;w.bindCommand(c);w.applyCmd(0,c);
}
bool save(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"w+b");bool exact=f && w.saveGame(f);
    if(exact){std::rewind(f);exact=copy.loadGame(f) && w.checksum()==copy.checksum();}
    if(f)std::fclose(f);return exact;
}
EID object(const World& w,const std::string& name) {
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].nativeIdentity.object==name)return id;
    return INVALID_EID;
}
bool legacy(const std::filesystem::path& folder,const std::string& label) {
    World w;FILE* f=std::fopen((folder/(label+"-ordered.sav")).string().c_str(),"rb");bool exact=f && w.loadGame(f);if(f)std::fclose(f);
    std::ifstream trace(folder/(label+".trace")),final(folder/(label+".final"));uint64_t value=0;
    exact &= w.nativeRulesBaseVersion==57 && bool(trace>>value) && value==w.checksum();
    for(int i=0;i<600 && exact;++i){w.update();exact=bool(trace>>value) && w.checksum()==value;feedback(w);}
    return exact && bool(final>>value) && value==w.checksum();
}
bool sourceMatrices() {
    bool exact=true;int queryCount=0,releaseCount=0;
    for(const std::string kind:{"query","release"}) {
        std::ifstream input("tests/fixtures/ra2-transport-exit-"+kind+".csv");std::string line;exact &= bool(input);std::getline(input,line);
        while(std::getline(input,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);std::string tag;int v[14]{};row>>tag;for(int& n:v)row>>n;
            if(!row || tag!=kind){exact=false;continue;}
            if(kind=="query") {
                const auto actual=NativeTransportMath::selectExit(static_cast<uint16_t>(v[0]),static_cast<uint8_t>(v[1]),v[2]!=0);int direction=-1;
                for(int d=0;d<8;++d)if(v[4]==10+NativeTransportMath::directions[d][0] && v[5]==10+NativeTransportMath::directions[d][1])direction=d;
                exact &= actual.direction==direction && actual.facing==v[3];++queryCount;
            } else {
                const auto actual=NativeTransportMath::selectRelease(static_cast<uint16_t>(v[0]),static_cast<uint8_t>(v[1]),static_cast<uint8_t>(v[2]));int direction=-1;
                if(v[8]==0)for(int d=0;d<8;++d)if(v[9]==(10+NativeTransportMath::directions[d][0])*256+128 && v[10]==(10+NativeTransportMath::directions[d][1])*256+128)direction=d;
                exact &= actual.direction==direction;
                if(direction>=0)exact &= v[11]==10+NativeTransportMath::directions[direction][0]*(actual.moveFar?2:1) && v[12]==10+NativeTransportMath::directions[direction][1]*(actual.moveFar?2:1);
                ++releaseCount;
            }
        }
    }
    exact &= queryCount==131072 && releaseCount==160;
    std::printf("TRANSPORT_EXIT_MATRIX %s queries=%d releases=%d\n",exact?"PASS":"FAIL",queryCount,releaseCount);return exact;
}
}

int GameRegression::transportExitRegression(const char* outputDir) {
    const std::filesystem::path out=outputDir;std::filesystem::create_directories(out);int failures=0;
    failures+=!sourceMatrices();
    World northBase;EID northHost=INVALID_EID;std::string northObject;
    std::ofstream positions(out/"positions.csv");positions<<"case,frame,hull,target,cargo,passenger_x,passenger_y,goal_x,goal_y\n";
    for(const auto& sample:{std::pair{"flak",UnitType::FlakTrack},std::pair{"ifv",UnitType::IFV},std::pair{"amph",UnitType::AmphTransport}}) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(0,sample.second,20.5f,20.5f),person=w.spawnUnit(0,UnitType::Conscript,17.5f,18.5f);
        bool setup=w.valid(host) && w.valid(person);if(!setup){++failures;continue;}
        w.ents[person].invuln=4000;const std::string name=w.ents[person].nativeIdentity.object;
        cmd(w,World::Cmd::Board,person,host);
        for(int i=0;i<240 && w.valid(person);++i){w.update();feedback(w);}
        setup &= !w.valid(person) && w.ents[host].cargo.size()==1;
        // Real movement establishes north; the fixture never sets the heading.
        cmd(w,World::Cmd::Move,host,INVALID_EID,20.5f,15.5f);
        for(int i=0;i<600 && (std::abs(w.ents[host].x-20.5f)>.01f || std::abs(w.ents[host].y-15.5f)>.01f);++i){w.update();feedback(w);}
        cmd(w,World::Cmd::Stop,host);feedback(w);
        setup &= std::abs(w.ents[host].x-20.5f)<.01f && std::abs(w.ents[host].y-15.5f)<.01f && w.nativeHullHeading(w.ents[host],w.tick)==0;
        if(std::string(sample.first)=="flak" && setup){northBase=w;northBase.map.bldOccRef=&northBase.bldOcc;northHost=host;northObject=name;}
        cmd(w,World::Cmd::Unload,host);World replay,middle;const std::string label=sample.first;
        bool exact=save(w,replay,out/(label+"-ordered.sav")),mid=true,active=false;
        std::ofstream trace(out/(label+".trace")),midTrace;trace<<w.checksum()<<'\n';
        int first=-1;bool heading=false,placement=false,departure=false;
        for(int frame=1;frame<=600;++frame) {
            w.update();if(exact){replay.update();exact=w.checksum()==replay.checksum();}
            if(active && mid){middle.update();mid=w.checksum()==middle.checksum();}
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            const auto restored=object(w,name);
            if(first<0 && w.valid(restored)) {
                first=frame;const auto& p=w.ents[restored];heading=w.nativeHullHeading(w.ents[host],w.tick)==8192;
                placement=(int)p.x==19 && (int)p.y==16;departure=std::abs(p.goalX-18.5f)<.01f && std::abs(p.goalY-17.5f)<.01f;
                positions<<label<<','<<frame<<','<<w.nativeHullHeading(w.ents[host],w.tick)<<','<<w.ents[host].nativeGround.hull.target<<','<<w.ents[host].cargo.size()<<','<<p.x<<','<<p.y<<','<<p.goalX<<','<<p.goalY<<'\n';
            }
            if(frame==1 || frame==3)positions<<label<<','<<frame<<','<<w.nativeHullHeading(w.ents[host],w.tick)<<','<<w.ents[host].nativeGround.hull.target<<','<<w.ents[host].cargo.size()<<",-1,-1,-1,-1\n";
            if(frame==3){mid=save(w,middle,out/(label+"-midway.sav"));active=true;midTrace.open(out/(label+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(replay);feedback(middle);
        }
        const bool ok=setup && first>5 && heading && placement && departure && exact && mid;failures+=!ok;
        std::ofstream(out/(label+".final"))<<w.checksum()<<'\n';
        std::printf("TRANSPORT_EXIT %s case=%s scene=%d setup=%d first=%d heading=%d placement=%d departure=%d replay=%d midway=%d\n",
            ok?"PASS":"FAIL",sample.first,w.nativeRulesBaseVersion,setup,first,heading,placement,departure,exact,mid);
        if(const char* old=std::getenv("OPENRA2_LEGACY_TRANSPORT_EXIT57_INPUT_DIR")) {
            const bool preserved=legacy(old,label);failures+=!preserved;
            std::printf("TRANSPORT_EXIT_LEGACY57 %s case=%s exact=%d\n",preserved?"PASS":"FAIL",sample.first,preserved);
        }
    }
    for(const std::string label:{"turn-stop","turn-move","far-blocked","rear-retry"}) {
        if(!northBase.valid(northHost)){++failures;continue;}
        World w=northBase;w.map.bldOccRef=&w.bldOcc;const EID host=northHost;
        if(label=="far-blocked")for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
            if(std::max(std::abs(dx),std::abs(dy))==2)w.map.at(20+dx,15+dy).blocked=true;
        cmd(w,World::Cmd::Unload,host);World replay,middle;
        bool exact=save(w,replay,out/(label+"-ordered.sav")),mid=true,active=false,retained=true;
        std::ofstream trace(out/(label+".trace")),midTrace;trace<<w.checksum()<<'\n';
        int first=-1;bool position=false,destination=false;uint16_t held=0;
        const auto actions=[&](World& state,int frame) {
            if(frame==3 && label=="turn-stop")cmd(state,World::Cmd::Stop,host);
            if(frame==3 && label=="turn-move")cmd(state,World::Cmd::Move,host,INVALID_EID,26.5f,15.5f);
            if(frame==3 && label=="rear-retry") {
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy)state.map.at(20+dx,15+dy).blocked=!(dx==-1 && dy==1);
                state.map.at(18,17).blocked=true;
            }
            if(frame==120 && label=="rear-retry")state.map.at(18,17).blocked=false;
        };
        for(int frame=1;frame<=600;++frame) {
            actions(w,frame);if(exact)actions(replay,frame);if(active && mid)actions(middle,frame);
            if(frame==3 && label=="turn-stop")held=w.nativeHullHeading(w.ents[host],w.tick);
            w.update();if(exact){replay.update();exact=w.checksum()==replay.checksum();}
            if(active && mid){middle.update();mid=w.checksum()==middle.checksum();}
            if(label=="rear-retry" && frame<120)retained &= w.ents[host].cargo.size()==1;
            const EID person=object(w,northObject);
            if(first<0 && w.valid(person)) {
                first=frame;const auto& p=w.ents[person];
                position=(int)p.x==19 && (int)p.y==(label=="far-blocked"?15:16);
                destination=std::abs(p.goalX-(label=="far-blocked"?19.5f:18.5f))<.01f && std::abs(p.goalY-(label=="far-blocked"?15.5f:17.5f))<.01f;
            }
            if(label=="turn-stop" && frame>=3)retained &= w.nativeHullHeading(w.ents[host],w.tick)==held;
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            if(frame==3){mid=save(w,middle,out/(label+"-midway.sav"));active=true;midTrace.open(out/(label+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(replay);feedback(middle);
        }
        const bool interrupt=label.starts_with("turn-");
        bool behavior=interrupt?(first<0 && w.ents[host].cargo.size()==1 && !w.ents[host].nativeTransportUnload.active()):
            (first>5 && position && destination && w.ents[host].cargo.empty());
        if(label=="turn-move")behavior &= std::abs(w.ents[host].x-26.5f)<.01f && std::abs(w.ents[host].y-15.5f)<.01f;
        if(label=="rear-retry")behavior &= first>=120 && first<=153;
        const bool ok=behavior && retained && exact && mid;failures+=!ok;
        std::ofstream(out/(label+".final"))<<w.checksum()<<'\n';
        std::printf("TRANSPORT_EXIT_FLOW %s case=%s first=%d retained=%d position=%d destination=%d replay=%d midway=%d\n",ok?"PASS":"FAIL",label.c_str(),first,retained,position,destination,exact,mid);
    }
    if(northBase.valid(northHost)) {
        world=northBase;world.map.bldOccRef=&world.bldOcc;cmd(world,World::Cmd::Unload,northHost);world.update();
        const auto file=out/"turning-app11.sav";Game resumed;const auto before=world.checksum();
        bool exact=saveGameFile(file.string().c_str()) && resumed.loadGameFile(file.string().c_str()) && resumed.world.checksum()==before;
        std::ifstream input(file,std::ios::binary);const std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),{}};
        for(const bool oldHeader:{false,true}) {
            auto bytes=original;const std::string key=oldHeader?"[Scene]\nVersion = "+std::to_string(nativeCurrentRulesSceneVersion):"\nIsTrain = 0";
            const std::string replacement=oldHeader?"[Scene]\nVersion = "+std::to_string(nativeTransportUnloadSceneVersion):"\nIsTrain = 2";
            const auto found=std::search(bytes.begin(),bytes.end(),key.begin(),key.end());bool rejected=found!=bytes.end();
            rejected &= replacement.size()==key.size();
            if(rejected)std::copy(replacement.begin(),replacement.end(),found);
            const auto bad=out/(oldHeader?"turning-old-scene.sav":"turning-invalid-train.sav");
            std::ofstream output(bad,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());output.close();
            rejected &= output && !resumed.loadGameFile(bad.string().c_str()) && resumed.world.checksum()==before;exact &= rejected;
            std::printf("TRANSPORT_EXIT_REJECT %s case=%s atomic=%d\n",rejected?"PASS":"FAIL",oldHeader?"old-scene":"train-flag",rejected);
        }
        failures+=!exact;std::printf("TRANSPORT_EXIT_CONTRACT %s world76_app11=%d\n",exact?"PASS":"FAIL",exact);
    } else ++failures;
    return failures;
}
