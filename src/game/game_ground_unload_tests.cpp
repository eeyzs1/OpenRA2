#include "game_regression.h"
#include "game/native_scene_contract.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
World fixture() {
    World w;w.init(64,64,20261123,2,0,{Faction::Soviet,Faction::Soviet},0,nullptr,false,false,
        "assets/rules/profiles/ra2.ini",{Country::Russia,Country::Iraq});
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& p:w.players){p.money=0;p.defeated=false;}
    w.spawnBuilding(0,BldType::ConYard,48,3,true);w.spawnBuilding(1,BldType::ConYard,48,48,true);
    return w;
}
void feedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void cmd(World& w,World::Cmd::Type type,const std::vector<EID>& ids,EID target=INVALID_EID) {
    World::Cmd c{};c.type=type;c.ids=ids;c.a=target;w.bindCommand(c);w.applyCmd(0,c);
}
void move(World& w,EID id,float x,float y) {
    World::Cmd c{};c.type=World::Cmd::Move;c.ids={id};c.x=x;c.y=y;w.bindCommand(c);w.applyCmd(0,c);
}
EID board(World& w) {
    const EID host=w.spawnUnit(0,UnitType::FlakTrack,20.5f,20.5f);std::vector<EID> people;
    for(int i=0;i<3;++i)people.push_back(w.spawnUnit(0,UnitType::Conscript,17.5f,18.5f+i));
    if(!w.valid(host) || std::any_of(people.begin(),people.end(),[&](EID id){return !w.valid(id);}))return INVALID_EID;
    for(EID id:people)w.ents[id].invuln=4000;
    cmd(w,World::Cmd::Board,people,host);
    // Complete the cell-contact approach before measuring unload scheduling.
    int preparation=0;
    for(;preparation<600 && w.ents[host].cargo.size()<3;++preparation){w.update();feedback(w);}
    std::printf("GROUND_UNLOAD_PREP ticks=%d cargo=%zu expected=3\n",preparation,w.ents[host].cargo.size());
    cmd(w,World::Cmd::Stop,{host});feedback(w);
    return w.ents[host].cargo.size()==3?host:INVALID_EID;
}
void block(World& w,bool closed) {
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy)w.map.at(20+dx,20+dy).blocked=closed;
}
bool save(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"w+b");bool exact=f && w.saveGame(f);
    if(exact){std::rewind(f);exact=copy.loadGame(f) && copy.checksum()==w.checksum();}
    if(f)std::fclose(f);return exact;
}
bool legacy(const std::filesystem::path& folder,const std::string& name,int scene=56) {
    World w;FILE* f=std::fopen((folder/(name+"-ordered.sav")).string().c_str(),"rb");bool exact=f && w.loadGame(f);if(f)std::fclose(f);
    std::ifstream trace(folder/(name+".trace")),final(folder/(name+".final"));uint64_t value=0;
    exact &= w.nativeRulesBaseVersion==scene && bool(trace>>value) && value==w.checksum();
    EID host=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].nativeTransportUnload.active()){host=id;break;}
    for(int frame=1;frame<=600 && exact;++frame){
        if(scene==57) {
            if(name=="repeat")cmd(w,World::Cmd::Unload,{host});
            if(name=="blocked" && frame==4)block(w,true);
            if(name=="blocked" && frame==90)block(w,false);
            if(name.starts_with("interrupt-") && frame==6) {
                if(name=="interrupt-move")move(w,host,26.5f,20.5f);
                else if(name=="interrupt-stop")cmd(w,World::Cmd::Stop,{host});
                else if(name=="interrupt-guard")cmd(w,World::Cmd::Guard,{host});
                else {EID target=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].isBuilding && w.ents[id].player==1){target=id;break;}cmd(w,World::Cmd::Attack,{host},target);}
            }
        }
        w.update();exact=bool(trace>>value) && value==w.checksum();feedback(w);
    }
    return exact && bool(final>>value) && value==w.checksum();
}
struct Case {const char* name;UnitType host,passenger;int count;};
constexpr Case cases[]{{"flak3",UnitType::FlakTrack,UnitType::Conscript,3},
    {"ifv1",UnitType::IFV,UnitType::Conscript,1},{"amph3",UnitType::AmphTransport,UnitType::Grizzly,3}};
}

int GameRegression::groundUnloadRegression(const char* outputDir) {
    const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);int failures=0;
    std::ofstream events(output/"releases.csv");events<<"case,frame,cargo,released\n";
    for(const auto& sample:cases) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(0,sample.host,20.5f,20.5f);std::vector<EID> people;
        for(int i=0;i<sample.count;++i)people.push_back(w.spawnUnit(0,sample.passenger,17.5f,18.5f+i));
        bool setup=w.valid(host) && std::all_of(people.begin(),people.end(),[&](EID id){return w.valid(id);});
        if(!setup){++failures;std::printf("GROUND_UNLOAD FAIL case=%s setup=0\n",sample.name);continue;}
        for(EID id:people)w.ents[id].invuln=4000;
        cmd(w,World::Cmd::Board,people,host);
        int preparation=0;
        for(;preparation<600 && w.ents[host].cargo.size()<(size_t)sample.count;++preparation){w.update();feedback(w);}
        std::printf("GROUND_UNLOAD_PREP case=%s ticks=%d cargo=%zu expected=%d\n",sample.name,preparation,w.ents[host].cargo.size(),sample.count);
        setup &= w.ents[host].cargo.size()==(size_t)sample.count;
        cmd(w,World::Cmd::Stop,{host});feedback(w);
        cmd(w,World::Cmd::Unload,{host});
        const int afterOrder=(int)w.ents[host].cargo.size();
        World replay,middle;const std::string name=sample.name;
        bool exact=save(w,replay,output/(name+"-ordered.sav")),mid=true,active=false;
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<w.checksum()<<'\n';
        std::vector<int> released;bool oneAtATime=afterOrder==sample.count;int previous=afterOrder;
        events<<name<<",0,"<<afterOrder<<','<<sample.count-afterOrder<<'\n';
        for(int frame=1;frame<=600;++frame) {
            w.update();if(exact){replay.update();exact=w.checksum()==replay.checksum();}
            if(active && mid){middle.update();mid=w.checksum()==middle.checksum();}
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            const int count=(int)w.ents[host].cargo.size();
            if(count!=previous){oneAtATime &= previous-count==1;released.push_back(frame);events<<name<<','<<frame<<','<<count<<','<<sample.count-count<<'\n';previous=count;}
            if(frame==12){mid=save(w,middle,output/(name+"-midway.sav"));active=true;midTrace.open(output/(name+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(replay);feedback(middle);
        }
        bool cadence=released.size()==(size_t)sample.count && released.front()==5;
        for(size_t i=1;i<released.size();++i)cadence &= released[i]-released[i-1]>=28 && released[i]-released[i-1]<=32;
        const bool clear=w.ents[host].cargo.empty();const bool ok=setup && oneAtATime && cadence && clear && exact && mid;failures+=!ok;
        std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
        std::printf("GROUND_UNLOAD %s case=%s scene=%d setup=%d after_order=%d one_at_a_time=%d releases=%zu first=%d cadence=%d clear=%d replay=%d midway=%d\n",
            ok?"PASS":"FAIL",sample.name,w.nativeRulesBaseVersion,setup,afterOrder,oneAtATime,released.size(),released.empty()?-1:released.front(),cadence,clear,exact,mid);
        if(const char* old=std::getenv("OPENRA2_LEGACY_UNLOAD56_INPUT_DIR")){
            const bool preserved=legacy(old,name);failures+=!preserved;
            std::printf("GROUND_UNLOAD_LEGACY56 %s case=%s exact=%d\n",preserved?"PASS":"FAIL",sample.name,preserved);
        }
    }
    for(const std::string name:{"repeat","moving","sleeping","interrupt-move","interrupt-stop","interrupt-guard","interrupt-attack","blocked","no-exit"}) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;const EID host=board(w);
        if(!w.valid(host)){++failures;std::printf("GROUND_UNLOAD_FLOW FAIL case=%s setup=0\n",name.c_str());continue;}
        if(name=="moving")move(w,host,25.5f,20.5f);
        if(name=="no-exit")block(w,true);
        if(name=="sleeping")w.ents[host].nativeIdentity.sleeping=true;
        cmd(w,World::Cmd::Unload,{host});
        World replay,middle;bool exact=save(w,replay,output/(name+"-ordered.sav")),mid=true,active=false;
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<w.checksum()<<'\n';
        bool retained=true,stationary=true,interrupted=false;int previous=3,first=-1;
        const auto actions=[&](World& state,int frame) {
            if(name=="repeat")cmd(state,World::Cmd::Unload,{host});
            if(name=="blocked" && frame==4)block(state,true);
            if(name=="blocked" && frame==90)block(state,false);
            if(name.starts_with("interrupt-") && frame==6) {
                if(name=="interrupt-move")move(state,host,26.5f,20.5f);
                else if(name=="interrupt-stop")cmd(state,World::Cmd::Stop,{host});
                else if(name=="interrupt-guard")cmd(state,World::Cmd::Guard,{host});
                else {
                    EID target=INVALID_EID;for(EID id=0;id<(EID)state.ents.size();++id)
                        if(state.valid(id) && state.ents[id].isBuilding && state.ents[id].player==1){target=id;break;}
                    cmd(state,World::Cmd::Attack,{host},target);
                }
            }
        };
        for(int frame=1;frame<=600;++frame) {
            actions(w,frame);
            // Repetition is compared against an unrepeated independent run.
            if(exact && name!="repeat")actions(replay,frame);
            if(active && mid)actions(middle,frame);
            if(name.starts_with("interrupt-") && frame==6)interrupted=!w.ents[host].nativeTransportUnload.active();
            w.update();if(exact){replay.update();exact=w.checksum()==replay.checksum();}
            if(active && mid){middle.update();mid=w.checksum()==middle.checksum();}
            const int count=(int)w.ents[host].cargo.size();
            if(count<previous){if(first<0)first=frame;stationary &= name!="moving" || (std::abs(w.ents[host].x-25.5f)<.01f && std::abs(w.ents[host].y-20.5f)<.01f);}
            previous=count;
            if(name=="blocked" && frame<90)retained &= count==3;
            if(name.starts_with("interrupt-") && frame>=6)retained &= count==2;
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            if(frame==12){mid=save(w,middle,output/(name+"-midway.sav"));active=true;midTrace.open(output/(name+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(replay);feedback(middle);
        }
        const bool interrupt=name.starts_with("interrupt-");
        bool behavior=retained && stationary && !w.ents[host].nativeTransportUnload.active();
        behavior &= interrupt?(interrupted && w.ents[host].cargo.size()==2):name=="no-exit"?w.ents[host].cargo.size()==3:w.ents[host].cargo.empty();
        if(name=="blocked")behavior &= first>=90 && first<=121;
        if(name=="moving")behavior &= first>5;
        if(name=="sleeping")behavior &= first==5 && !w.ents[host].nativeIdentity.sleeping;
        const bool ok=behavior && exact && mid;failures+=!ok;
        std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
        std::printf("GROUND_UNLOAD_FLOW %s case=%s first=%d cargo=%zu retained=%d stationary=%d interrupted=%d replay=%d midway=%d\n",
            ok?"PASS":"FAIL",name.c_str(),first,w.ents[host].cargo.size(),retained,stationary,interrupted,exact,mid);
    }
    for(const bool owners:{false,true})for(const int base:{64,68,74}) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;const EID host=board(w);bool setup=w.valid(host);
        if(!setup){++failures;continue;}
        if(!owners)for(auto& p:w.ents[host].cargo)p.player=-2;
        if(base!=64) {
            const EID tank=w.spawnUnit(0,UnitType::Grizzly,30.5f,30.5f);auto& e=w.ents[tank];
            e.path={{31,30},{31,31},{31,32}};e.pathIdx=0;e.goalX=31.5f;e.goalY=32.5f;e.state=UState::Moving;
            e.driveArc={{31,30},{0,1},1,0,0,true,base==74};
        }
        cmd(w,World::Cmd::Unload,{host});World replay;
        const std::string name="layout-"+std::to_string(base)+(owners?"-owners":"-inherit");
        bool exact=save(w,replay,output/(name+".sav"));
        std::ifstream file(output/(name+".sav"),std::ios::binary);const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
        int actual=0;if(bytes.size()>13)std::memcpy(&actual,bytes.data()+8,4);
        const bool layout=bytes.size()>13 && worldSaveHeaderMatches(bytes,"RA2WLD76") && actual==base && (bytes[12]&1)==owners
            && bool(locateWorldSaveSection(bytes,WorldSaveSection::TransportUnload))
            && bool(locateWorldSaveSection(bytes,WorldSaveSection::CargoOwners))==owners
            && bool(locateWorldSaveSection(bytes,WorldSaveSection::NativeTravel));
        std::ofstream trace(output/(name+".trace"));trace<<w.checksum()<<'\n';
        for(int i=0;i<600 && exact;++i){w.update();replay.update();exact=w.checksum()==replay.checksum();trace<<w.checksum()<<'\n';feedback(w);feedback(replay);}
        const bool ok=setup && layout && exact;failures+=!ok;
        std::printf("GROUND_UNLOAD_LAYOUT %s base=%d owners=%d layout=%d exact=%d\n",ok?"PASS":"FAIL",base,owners,layout,exact);
    }
    {
        World live=fixture();live.map.bldOccRef=&live.bldOcc;const EID host=board(live);
        bool ok=live.valid(host);if(ok)cmd(live,World::Cmd::Unload,{host});
        world=live;world.map.bldOccRef=&world.bldOcc;
        const auto path=output/"unload-app11.sav";Game resumed;
        ok &= saveGameFile(path.string().c_str()) && resumed.loadGameFile(path.string().c_str());
        std::ifstream input(path,std::ios::binary);const std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),{}};
        const auto at=embeddedWorldSaveOffset(original);const auto section=at?locateWorldSaveSection(std::span(original).subspan(*at),WorldSaveSection::TransportUnload):std::nullopt;
        ok &= at && section && resumed.world.checksum()==live.checksum();
        int rejections=0;
        if(ok) {
            const auto before=resumed.world.checksum();
            const auto reject=[&](const char* name,const std::vector<uint8_t>& bytes) {
                const auto bad=output/(std::string("unload-bad-")+name+".sav");
                std::ofstream stream(bad,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());stream.close();
                const bool pass=stream && !resumed.loadGameFile(bad.string().c_str()) && resumed.world.checksum()==before;
                ++rejections;std::printf("GROUND_UNLOAD_REJECT %s case=%s atomic=%d\n",pass?"PASS":"FAIL",name,pass);return pass;
            };
            const size_t header=*at,record=header+section->offset+8;
            auto bytes=original;bytes[header+12]=2;ok &= reject("owners-flag",bytes);
            bytes=original;int value=75;std::memcpy(bytes.data()+header+8,&value,4);ok &= reject("base",bytes);
            bytes=original;value=0;std::memcpy(bytes.data()+header+section->offset+4,&value,4);ok &= reject("zero-count",bytes);
            bytes=original;value=65537;std::memcpy(bytes.data()+header+section->offset+4,&value,4);ok &= reject("count",bytes);
            bytes=original;value=65536;std::memcpy(bytes.data()+record,&value,4);ok &= reject("entity",bytes);
            bytes=original;bytes[record+4]^=1;ok &= reject("generation",bytes);
            bytes=original;bytes[record+12]=0;ok &= reject("phase",bytes);
            bytes=original;value=-1;std::memcpy(bytes.data()+record+13,&value,4);ok &= reject("negative-timer",bytes);
            bytes=original;value=33;std::memcpy(bytes.data()+record+13,&value,4);ok &= reject("timer-bound",bytes);
            bytes=original;bytes[header+section->offset]='X';ok &= reject("tag",bytes);
            bytes=original;bytes[header+section->footer]^=1;ok &= reject("length",bytes);
            bytes=original;bytes.pop_back();ok &= reject("truncated",bytes);
            bytes=original;bytes.push_back(0);ok &= reject("trailing",bytes);
            bytes=original;std::memcpy(bytes.data()+header,"RA2WLD75",8);ok &= reject("old-world",bytes);
            bytes=original;const std::string scene="[Scene]\nVersion = "+std::to_string(nativeCurrentRulesSceneVersion);auto found=std::search(bytes.begin(),bytes.end(),scene.begin(),scene.end());
            const std::string oldScene="[Scene]\nVersion = "+std::to_string(nativeCargoOwnerSceneVersion);
            if(found==bytes.end() || oldScene.size()!=scene.size())ok=false;
            else {std::copy(oldScene.begin(),oldScene.end(),found);ok &= reject("old-scene",bytes);}
        }
        failures+=!ok;std::printf("GROUND_UNLOAD_CONTRACT %s app=1 cases=%d atomic=%d\n",ok?"PASS":"FAIL",rejections,ok);
        // Retirement clears delayed work before its EID is reused.
        if(live.valid(host))live.kill(host,false);
        const auto replacement=live.spawnUnit(0,UnitType::FlakTrack,20.5f,20.5f);World replay;
        const bool retired=live.valid(replacement) && !live.ents[replacement].nativeTransportUnload.active()
            && save(live,replay,output/"retired-carrier.sav");
        failures+=!retired;std::printf("GROUND_UNLOAD_RETIRE %s reused=%d saved=%d\n",retired?"PASS":"FAIL",replacement==host,retired);
    }
    if(const char* old=std::getenv("OPENRA2_LEGACY_UNLOAD57_INPUT_DIR")) {
        const std::filesystem::path folder=old;
        for(const std::string name:{"flak3","ifv1","amph3","repeat","moving","sleeping","interrupt-move","interrupt-stop","interrupt-guard","interrupt-attack","blocked","no-exit"}) {
            const bool preserved=legacy(folder,name,57);failures+=!preserved;
            std::printf("GROUND_UNLOAD_LEGACY57 %s case=%s exact=%d\n",preserved?"PASS":"FAIL",name.c_str(),preserved);
        }
        for(const int base:{64,68,74})for(const bool owner:{false,true}) {
            const std::string name="layout-"+std::to_string(base)+(owner?"-owners":"-inherit");World old;
            FILE* file=std::fopen((folder/(name+".sav")).string().c_str(),"rb");bool exact=file && old.loadGame(file);if(file)std::fclose(file);
            std::ifstream trace(folder/(name+".trace"));uint64_t value=0;exact &= old.nativeRulesBaseVersion==57 && bool(trace>>value) && value==old.checksum();
            for(int i=0;i<600 && exact;++i){old.update();exact=bool(trace>>value) && value==old.checksum();feedback(old);}
            failures+=!exact;std::printf("GROUND_UNLOAD_LEGACY57_LAYOUT %s base=%d owners=%d exact=%d\n",exact?"PASS":"FAIL",base,owner,exact);
        }
    }
    return failures;
}
