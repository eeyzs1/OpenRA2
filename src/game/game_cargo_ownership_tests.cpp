#include "game_regression.h"
#include "game/native_scene_contract.h"
#include "game/actor_contents.h"
#include "world_save_sections.h"
#include <algorithm>
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
    w.cratesEnabled=false;w.crates.clear();w.aiAlliance=false;w.sharedVision=false;
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& c:w.map.cells)c=Cell{};
    for(auto& p:w.players){p.money=0;p.defeated=false;}
    w.spawnBuilding(0,BldType::ConYard,48,3,true);w.spawnBuilding(1,BldType::ConYard,48,48,true);
    return w;
}
void feedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void cmd(World& w,int player,World::Cmd::Type type,const std::vector<EID>& ids,EID target=INVALID_EID) {
    World::Cmd c{};c.type=type;c.ids=ids;c.a=target;w.bindCommand(c);w.applyCmd(player,c);
}
bool save(World& w,World& replay,const std::filesystem::path& p) {
    FILE* f=std::fopen(p.string().c_str(),"w+b");bool ok=f && w.saveGame(f);
    if(ok){std::rewind(f);ok=replay.loadGame(f) && w.checksum()==replay.checksum();}
    if(f)std::fclose(f);return ok;
}
EID objectActor(const World& w,const std::string& object) {
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].nativeIdentity.object==object)return id;
    return INVALID_EID;
}
void moveReleased(World& w,const std::string& object) {
    const auto id=objectActor(w,object);
    if(w.valid(id)) {
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={id};move.x=6.5f;move.y=25.5f;
        w.bindCommand(move);w.applyCmd(0,move);
    }
}
void release(World& w,EID host,const std::string& object) {
    cmd(w,1,World::Cmd::Unload,{host});moveReleased(w,object);
}
bool legacy(const std::filesystem::path& folder,const std::string& name,int scene=55) {
    World w;FILE* f=std::fopen((folder/(name+"-ordered.sav")).string().c_str(),"rb");bool exact=f && w.loadGame(f);if(f)std::fclose(f);
    std::ifstream trace(folder/(name+".trace")),final(folder/(name+".final")),commands(folder/(name+".commands"));
    EID host=INVALID_EID;std::string object;uint64_t value=0;
    bool moved=false;
    exact &= bool(commands>>host>>object) && w.nativeRulesBaseVersion==scene && bool(trace>>value) && w.checksum()==value;
    for(int frame=1;frame<=600 && exact;++frame) {
        if(frame==300)release(w,host,object);
        w.update();
        if(scene==57 && frame>=300 && !moved && w.valid(objectActor(w,object))){moveReleased(w,object);moved=true;}
        exact=exact && bool(trace>>value) && w.checksum()==value;feedback(w);
        if(scene>=56 && frame==210 && w.valid(host) && w.valid(w.ents[host].mindBy)) {
            World::Cmd move{};move.type=World::Cmd::Move;move.ids={w.ents[host].mindBy};move.x=40.5f;move.y=10.5f;
            w.bindCommand(move);w.applyCmd(1,move);
        }
    }
    return exact && bool(final>>value) && w.checksum()==value;
}
struct Case {const char* name;UnitType carrier,passenger;bool gunner=false,nested=false;};
constexpr Case cases[]{
    {"flak-conscript",UnitType::FlakTrack,UnitType::Conscript},
    {"flak-drone",UnitType::FlakTrack,UnitType::TerrorDrone},
    {"amph-tank",UnitType::AmphTransport,UnitType::Grizzly},
    {"amph-mcv",UnitType::AmphTransport,UnitType::MCV},
    {"ifv-gunner",UnitType::IFV,UnitType::Conscript,true},
    {"nested-ifv",UnitType::AmphTransport,UnitType::IFV,false,true}
};
}

int GameRegression::cargoOwnershipRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);const std::filesystem::path output=outputDir;int failures=0;
    std::ofstream observed(output/"ownership.csv");observed<<"case,frame,carrier_player,cargo_projected_player,restored_player\n";
    const auto applicationChecks=[&](const World& live) {
        world=live;world.map.bldOccRef=&world.bldOcc;
        const auto file=(output/"ownership-app11.sav").string();
        Game resumed;const auto before=world.checksum();
        if(!saveGameFile(file.c_str()) || !resumed.loadGameFile(file.c_str()) || resumed.world.checksum()!=before)return false;
        std::ifstream input(file,std::ios::binary);
        const std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),{}};
        const auto nested=embeddedWorldSaveOffset(original);
        if(!nested || !worldSaveHeaderMatches(std::span(original).subspan(*nested),"RA2WLD75"))return false;
        const auto owners=locateWorldSaveSection(std::span(original).subspan(*nested),WorldSaveSection::CargoOwners);
        if(!owners || owners->length<12)return false;
        const auto reject=[&](const char* name,const std::vector<uint8_t>& bytes) {
            std::ofstream out(output/(std::string("ownership-bad-")+name+".sav"),std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());out.close();
            const bool rejected=out && !resumed.loadGameFile((output/(std::string("ownership-bad-")+name+".sav")).string().c_str());
            const bool unchanged=resumed.world.checksum()==before;
            std::printf("CARGO_OWNER_REJECT %s case=%s rejected=%d unchanged=%d\n",rejected && unchanged?"PASS":"FAIL",name,rejected,unchanged);
            return rejected && unchanged;
        };
        bool ok=true;auto bytes=original;int value=live.numPlayers;
        std::memcpy(bytes.data()+*nested+owners->offset+8,&value,4);ok &= reject("owner",bytes);
        bytes=original;value=-3;std::memcpy(bytes.data()+*nested+owners->offset+8,&value,4);ok &= reject("negative-owner",bytes);
        bytes=original;value=63;std::memcpy(bytes.data()+*nested+8,&value,4);ok &= reject("base",bytes);
        bytes=original;value=1000001;std::memcpy(bytes.data()+*nested+owners->offset+4,&value,4);ok &= reject("count",bytes);
        bytes=original;bytes.resize(bytes.size()-1);ok &= reject("truncated",bytes);
        bytes=original;bytes.push_back(0);ok &= reject("trailing",bytes);
        bytes=original;std::memcpy(bytes.data()+*nested,"RA2WLD64",8);ok &= reject("old-world",bytes);
        bytes=original;const std::string scene="[Scene]\nVersion = "+std::to_string(nativeCurrentRulesSceneVersion);
        const auto found=std::search(bytes.begin(),bytes.end(),scene.begin(),scene.end());
        if(found==bytes.end())ok=false;
        else {
            const std::string oldScene="[Scene]\nVersion = "+std::to_string(nativeSizedTransportSceneVersion);
            if(oldScene.size()!=scene.size())ok=false;
            else {std::copy(oldScene.begin(),oldScene.end(),found);ok &= reject("old-scene",bytes);}
        }
        std::printf("CARGO_OWNER_CONTRACT %s app=1 atomic=%d cases=8\n",ok?"PASS":"FAIL",ok);
        return ok;
    };
    for(const auto& c:cases) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(0,c.carrier,10.5f,10.5f),passenger=w.spawnUnit(0,c.passenger,8.5f,10.5f);
        bool staged=w.valid(host) && w.valid(passenger) && w.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion;
        if(staged && c.gunner)staged=w.hasNativeIFV(w.ents[host]);
        if(!staged){++failures;std::printf("CARGO_OWNER FAIL case=%s setup=0\n",c.name);continue;}
        w.ents[host].guard=false;w.ents[host].hp=unitDef(w.ents[host]).hp;
        w.ents[passenger].hp=73;w.ents[passenger].kills=3;w.ents[passenger].vetRank=1;w.ents[passenger].veterancyValue=1600;
        w.ents[passenger].invuln=4000; // Isolate ownership from hostile fire after Unload.
        const auto original=w.ents[passenger].nativeIdentity.object;const auto combat=w.ents[passenger].combatActor;
        if(c.nested) {
            const EID crew=w.spawnUnit(0,UnitType::Conscript,7.5f,10.5f);cmd(w,0,World::Cmd::Board,{crew},passenger);
            for(int i=0;i<120 && w.valid(crew);++i){w.update();feedback(w);}
            staged &= !w.valid(crew) && w.ents[passenger].cargo.size()==1;
        }
        cmd(w,0,World::Cmd::Board,{passenger},host);
        int preparation=0;
        for(;preparation<600 && w.valid(passenger);++preparation){w.update();feedback(w);}
        std::printf("CARGO_OWNER_PREP case=%s ticks=%d loaded=%zu\n",c.name,preparation,w.ents[host].cargo.size());
        staged &= !w.valid(passenger) && w.ents[host].cargo.size()==1;
        const EID psychic=w.spawnUnit(1,UnitType::Yuri,15.5f,10.5f);
        staged &= w.valid(psychic);
        if(!staged){++failures;std::printf("CARGO_OWNER FAIL case=%s setup=0 loaded=%zu\n",c.name,w.ents[host].cargo.size());continue;}
        w.ents[psychic].invuln=4000;cmd(w,1,World::Cmd::Attack,{psychic},host);
        const std::string name=c.name;World replay,middle;
        bool exact=save(w,replay,output/(name+"-ordered.sav")),mid=true,active=false;
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<w.checksum()<<'\n';
        std::ofstream(output/(name+".commands"))<<host<<' '<<original<<'\n';
        bool controlled=false,ownership=true,nestedOwner=true,inventory=true,stats=false,checkedRelease=false;const int expected=0;
        for(int frame=1;frame<=600;++frame) {
            if(frame==300) {
                release(w,host,original);if(exact)release(replay,host,original);if(active && mid)release(middle,host,original);
            }
            w.update();if(exact){replay.update();exact=w.checksum()==replay.checksum();}
            if(active && mid){middle.update();mid=w.checksum()==middle.checksum();}
            if(frame>=300 && !checkedRelease) {
                const auto id=objectActor(w,original);
                if(w.valid(id)) {
                    checkedRelease=true;
                    const auto& e=w.ents[id];stats=e.hp==73 && e.kills==3 && e.vetRank==1 && e.combatActor==combat;
                    ownership &= e.player==expected;
                    observed<<name<<','<<frame<<','<<w.ents[host].player<<",-1,"<<e.player<<'\n';
                    moveReleased(w,original);if(exact)moveReleased(replay,original);if(active && mid)moveReleased(middle,original);
                    exact &= w.checksum()==replay.checksum();if(active)mid &= w.checksum()==middle.checksum();
                }
            }
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            controlled |= w.valid(host) && w.ents[host].player==1 && w.ents[host].mindBy==psychic;
            if(frame==180) {
                mid=save(w,middle,output/(name+"-midway.sav"));active=true;
                midTrace.open(output/(name+"-midway.trace"));midTrace<<w.checksum()<<'\n';
            }
            if(frame==200 && w.valid(host) && !w.ents[host].cargo.empty()) {
                const auto projected=w.passengerActor(w.ents[host],w.ents[host].cargo.front());
                ownership=projected.player==expected;
                if(c.nested && !w.ents[host].cargo.front().cargo.empty())nestedOwner=w.passengerActor(projected,w.ents[host].cargo.front().cargo.front()).player==0;
                observed<<name<<','<<frame<<','<<w.ents[host].player<<','<<projected.player<<",-1\n";
                for(int player=0;player<2;++player) {
                    int count=0;visitLivePlayerContents(w,player,[&](const auto& actor){count+=actor.nativeIdentity.object==original;});
                    inventory &= count==(player==expected);
                }
                if(name=="flak-conscript") {
                    failures+=!applicationChecks(w);
                    World dead=w;dead.map.bldOccRef=&dead.bldOcc;dead.shortGame=false;
                    for(EID id=0;id<(EID)dead.ents.size();++id)if(dead.valid(id) && dead.ents[id].isBuilding && dead.ents[id].player==0)dead.kill(id,false);
                    const bool aliveHouse=!dead.players[0].defeated;
                    dead.kill(host,false);int count=0;
                    visitLivePlayerContents(dead,0,[&](const auto& actor){count+=actor.nativeIdentity.object==original;});
                    World restored;const bool persisted=save(dead,restored,output/"historical-cargo.sav");
                    const auto before=restored.checksum();
                    if(!restored.ents[host].cargo.empty())restored.ents[host].cargo.front().player=1;
                    const bool historical=aliveHouse && dead.players[0].defeated && count==0 && persisted && restored.checksum()!=before;
                    failures+=!historical;std::printf("CARGO_OWNER_HISTORY %s survives_with_cargo=%d dead_excluded=%d saved=%d hashed=%d\n",
                        historical?"PASS":"FAIL",aliveHouse,count==0,persisted,restored.checksum()!=before);
                    World mixed=w;mixed.map.bldOccRef=&mixed.bldOcc;
                    const EID extra=mixed.spawnUnit(1,UnitType::Conscript,8.5f,12.5f);
                    const std::string extraObject=mixed.ents[extra].nativeIdentity.object;
                    cmd(mixed,1,World::Cmd::Board,{extra},host);
                    for(int i=0;i<180 && mixed.valid(extra);++i){mixed.update();feedback(mixed);}
                    const bool loaded=!mixed.valid(extra) && mixed.ents[host].cargo.size()==2;
                    bool owners=true;
                    for(int player=0;player<2;++player) {
                        int oldCount=0,newCount=0;
                        visitLivePlayerContents(mixed,player,[&](const auto& actor) {
                            oldCount+=actor.nativeIdentity.object==original;newCount+=actor.nativeIdentity.object==extraObject;
                        });
                        owners &= oldCount==(player==0) && newCount==(player==1);
                    }
                    World copy;const bool saved=save(mixed,copy,output/"mixed-owners.sav");
                    cmd(mixed,1,World::Cmd::Unload,{host});if(saved)cmd(copy,1,World::Cmd::Unload,{host});
                    bool oldReleased=false,newReleased=false;
                    bool exact=saved && mixed.checksum()==copy.checksum();std::ofstream mixedTrace(output/"mixed-owners.trace");mixedTrace<<mixed.checksum()<<'\n';
                    for(int i=0;i<600 && exact;++i){mixed.update();copy.update();exact=mixed.checksum()==copy.checksum();mixedTrace<<mixed.checksum()<<'\n';feedback(mixed);feedback(copy);
                        const auto oldId=objectActor(mixed,original),newId=objectActor(mixed,extraObject);
                        oldReleased |= mixed.valid(oldId) && mixed.ents[oldId].player==0;
                        newReleased |= mixed.valid(newId) && mixed.ents[newId].player==1;}
                    const bool released=oldReleased && newReleased;
                    const bool ok=loaded && owners && saved && released && exact;failures+=!ok;
                    std::printf("CARGO_OWNER_MIXED %s loaded=%d inventory=%d save=%d released=%d exact=%d\n",ok?"PASS":"FAIL",loaded,owners,saved,released,exact);
                }
                if(name=="amph-mcv") {
                    World carried=w;carried.map.bldOccRef=&carried.bldOcc;carried.shortGame=true;
                    for(EID id=0;id<(EID)carried.ents.size();++id)if(carried.valid(id) && carried.ents[id].isBuilding && carried.ents[id].player==0)carried.kill(id,false);
                    const bool survives=!carried.players[0].defeated;failures+=!survives;
                    std::printf("CARGO_OWNER_MCV %s short_game=1 survives_with_cargo=%d\n",survives?"PASS":"FAIL",survives);
                }
            }
            if(frame==210) {
                // Keep a second psychic acquisition from obscuring the unload result.
                const auto leave=[&](World& state) {
                    if(!state.valid(host))return;const EID psychic=state.ents[host].mindBy;if(!state.valid(psychic))return;
                    World::Cmd move{};move.type=World::Cmd::Move;move.ids={psychic};move.x=40.5f;move.y=10.5f;
                    state.bindCommand(move);state.applyCmd(1,move);
                };
                leave(w);if(exact)leave(replay);if(active && mid)leave(middle);
            }
            feedback(w);feedback(replay);feedback(middle);
        }
        const auto restored=objectActor(w,original);bool released=w.valid(restored);stats &= released;
        if(released){const auto& e=w.ents[restored];ownership &= e.player==expected;stats &= e.combatActor==combat;
            if(c.nested)nestedOwner &= e.player==0 && e.cargo.size()==1 && w.passengerActor(e,e.cargo.front()).player==0;
            observed<<name<<",600,"<<w.ents[host].player<<",-1,"<<e.player<<'\n';}
        const bool ok=staged && controlled && ownership && nestedOwner && inventory && released && stats && exact && mid;failures+=!ok;
        std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
        std::printf("CARGO_OWNER %s case=%s scene=%d setup=%d controlled=%d ownership=%d nested=%d inventory=%d released=%d stats=%d replay=%d midway=%d\n",
            ok?"PASS":"FAIL",c.name,w.nativeRulesBaseVersion,staged,controlled,ownership,nestedOwner,inventory,released,stats,exact,mid);
        if(const char* old=std::getenv("OPENRA2_LEGACY_CARGO55_INPUT_DIR");old && name!="amph-mcv") {
            const bool preserved=legacy(old,name);failures+=!preserved;
            std::printf("CARGO_OWNER_LEGACY55 %s case=%s exact=%d\n",preserved?"PASS":"FAIL",c.name,preserved);
        }
        if(const char* old=std::getenv("OPENRA2_LEGACY_CARGO56_INPUT_DIR")) {
            const bool preserved=legacy(old,name,56);failures+=!preserved;
            std::printf("CARGO_OWNER_LEGACY56 %s case=%s exact=%d\n",preserved?"PASS":"FAIL",c.name,preserved);
        }
        if(const char* old=std::getenv("OPENRA2_LEGACY_CARGO57_INPUT_DIR")) {
            const bool preserved=legacy(old,name,57);failures+=!preserved;
            std::printf("CARGO_OWNER_LEGACY57 %s case=%s exact=%d\n",preserved?"PASS":"FAIL",c.name,preserved);
        }
    }
    for(const bool cubic:{false,true}) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(0,UnitType::FlakTrack,10.5f,10.5f),inf=w.spawnUnit(0,UnitType::Conscript,9.5f,10.5f);
        cmd(w,0,World::Cmd::Board,{inf},host);
        for(int i=0;i<160 && w.valid(inf);++i){w.update();feedback(w);}
        const bool loaded=!w.valid(inf) && w.ents[host].cargo.size()==1;
        const EID tank=w.spawnUnit(0,UnitType::Grizzly,21.5f,20.5f);auto& e=w.ents[tank];
        // A valid serialized curve fixture checks coexistence of both travel tags.
        e.path={{22,20},{22,21},{22,22}};e.pathIdx=0;e.goalX=22.5f;e.goalY=22.5f;e.state=UState::Moving;
        e.driveArc={{22,20},{0,1},1,0,0,true,cubic};
        const std::string name=cubic?"owners-cubic74":"owners-quadratic68";World replay;
        bool exact=loaded && save(w,replay,output/(name+".sav"));
        std::ifstream input(output/(name+".sav"),std::ios::binary);char magic[8]{};int base=0;input.read(magic,8);input.read(reinterpret_cast<char*>(&base),4);
        exact &= input && (!std::memcmp(magic,"RA2WLD75",8) || !std::memcmp(magic,"RA2WLD77",8)) && base==(cubic?74:68);
        std::ofstream trace(output/(name+".trace"));trace<<w.checksum()<<'\n';
        for(int frame=0;frame<600 && exact;++frame){w.update();replay.update();exact=w.checksum()==replay.checksum();trace<<w.checksum()<<'\n';feedback(w);feedback(replay);}
        failures+=!exact;std::printf("CARGO_OWNER_TRAVEL %s base=%d exact=%d\n",exact?"PASS":"FAIL",base,exact);
    }
    for(const auto& sample:{std::pair{"OPENRA2_LEGACY_CARGO56_INPUT_DIR",56},std::pair{"OPENRA2_LEGACY_CARGO57_INPUT_DIR",57}})if(const char* directory=std::getenv(sample.first)) {
        const std::filesystem::path folder=directory;
        for(const std::string name:{"mixed-owners","owners-quadratic68","owners-cubic74"}) {
            World w;FILE* f=std::fopen((folder/(name+".sav")).string().c_str(),"rb");bool exact=f && w.loadGame(f);if(f)std::fclose(f);
            if(name=="mixed-owners") {
                EID host=INVALID_EID;std::string object;std::ifstream commands(folder/"flak-conscript.commands");
                exact &= bool(commands>>host>>object);if(exact)cmd(w,1,World::Cmd::Unload,{host});
            }
            std::ifstream trace(folder/(name+".trace"));uint64_t value=0;
            exact &= w.nativeRulesBaseVersion==sample.second && bool(trace>>value) && w.checksum()==value;
            for(int i=0;i<600 && exact;++i){w.update();exact=bool(trace>>value) && w.checksum()==value;feedback(w);}
            failures+=!exact;std::printf("CARGO_OWNER_LEGACY%d_STORED %s case=%s exact=%d\n",sample.second,exact?"PASS":"FAIL",name.c_str(),exact);
        }
    }
    return failures;
}
