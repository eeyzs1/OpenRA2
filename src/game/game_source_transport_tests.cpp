#include "game_regression.h"
#include "game/actor_state_transfer.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

namespace {
World fixture() {
    World w;
    w.init(64,64,20261123,2,0,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,
        "assets/rules/profiles/ra2.ini",{Country::Russia,Country::America});
    w.cratesEnabled=false;w.crates.clear();w.aiAlliance=false;w.sharedVision=false;
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    for(auto& player:w.players){player.money=0;player.defeated=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);
    return w;
}
void feedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void command(World& w,World::Cmd::Type type,const std::vector<EID>& ids,EID target=INVALID_EID) {
    World::Cmd cmd{};cmd.type=type;cmd.ids=ids;cmd.a=target;w.bindCommand(cmd);w.applyCmd(0,cmd);
}
bool save(World& w,World& copy,const std::filesystem::path& path) {
    FILE* file=std::fopen(path.string().c_str(),"w+b");bool exact=file && w.saveGame(file);
    if(exact){std::rewind(file);exact=copy.loadGame(file) && w.checksum()==copy.checksum();}
    if(file)std::fclose(file);return exact;
}
bool archived(const std::filesystem::path& path,const std::string& name,int scene=54) {
    World w;FILE* file=std::fopen((path/(name+"-ordered.sav")).string().c_str(),"rb");
    bool exact=file && w.loadGame(file);if(file)std::fclose(file);
    std::ifstream trace(path/(name+".trace")),final(path/(name+".final")),commands(path/(name+".commands"));
    int at=0,host=INVALID_EID;uint64_t value=0;
    exact &= bool(commands>>at>>host) && at==300 && w.nativeRulesBaseVersion==scene
        && bool(trace>>value) && w.checksum()==value;
    for(int frame=1;frame<=600 && exact;++frame) {
        if(frame==at)command(w,World::Cmd::Unload,{host});
        w.update();exact=exact && bool(trace>>value) && w.checksum()==value;feedback(w);
    }
    return exact && bool(final>>value) && w.checksum()==value;
}
struct Admission {const char* name;UnitType host,passenger,node;int nodes;bool allowed,foreign=false;};
using U=UnitType;
constexpr Admission admissions[]{
    {"flak-drone",U::FlakTrack,U::TerrorDrone,U::GI,0,true},
    {"flak-two-drones-plus-gi",U::FlakTrack,U::GI,U::TerrorDrone,2,true},
    {"flak-third-drone",U::FlakTrack,U::TerrorDrone,U::TerrorDrone,2,false},
    {"flak-drone-after-three-gi",U::FlakTrack,U::TerrorDrone,U::GI,3,true},
    {"flak-drone-after-four-gi",U::FlakTrack,U::TerrorDrone,U::GI,4,false},
    {"flak-tank-too-large",U::FlakTrack,U::Grizzly,U::GI,0,false},
    {"flak-five-gi",U::FlakTrack,U::GI,U::GI,4,true},
    {"flak-sixth-gi",U::FlakTrack,U::GI,U::GI,5,false},
    {"ifv-drone",U::IFV,U::TerrorDrone,U::GI,0,false},
    {"ifv-gi",U::IFV,U::GI,U::GI,0,true},
    {"nighthawk-drone",U::Nighthawk,U::TerrorDrone,U::GI,0,true},
    {"amph-drone",U::AmphTransport,U::TerrorDrone,U::GI,0,true},
    {"amph-fourth-tank",U::AmphTransport,U::Grizzly,U::Grizzly,3,true},
    {"amph-overfill-tank",U::AmphTransport,U::Grizzly,U::Grizzly,4,false},
    {"amph-second-mcv",U::AmphTransport,U::MCV,U::MCV,1,true},
    {"wrong-house",U::FlakTrack,U::TerrorDrone,U::GI,0,false,true},
    {"no-capacity",U::Rhino,U::GI,U::GI,0,false}
};
struct Journey {const char* name;UnitType host,passenger;int count,expected;bool mixed=false,smart=false;};
constexpr Journey journeys[]{
    {"flak-drone",U::FlakTrack,U::TerrorDrone,1,1,false,true},
    {"flak-race",U::FlakTrack,U::TerrorDrone,4,3,true},
    {"nighthawk-drone",U::Nighthawk,U::TerrorDrone,1,1,false,true},
    {"amph-drone",U::AmphTransport,U::TerrorDrone,1,1},
    {"flak-conscript",U::FlakTrack,U::Conscript,1,1},
    {"ifv-conscript",U::IFV,U::Conscript,1,1}
};
}

int GameRegression::sourceTransportRegression(const char* outputDir) {
    const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);int failures=0;
    std::ofstream matrix(output/"admission.csv");matrix<<"name,allowed,expected,reason,setup,preserved\n";
    for(const auto& c:admissions) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(0,c.host,10.5f,10.5f);bool staged=w.valid(host) && w.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion;
        if(!staged){++failures;std::printf("SOURCE_TRANSPORT FAIL matrix=%s setup=0\n",c.name);continue;}
        for(int i=0;i<c.nodes;++i) {
            const EID node=w.spawnUnit(0,c.node,5.5f+i,16.5f);staged &= w.valid(node);
            if(w.valid(node)){const auto saved=ActorStateTransfer::capture(w.ents[node]);w.kill(node,false);w.ents[host].cargo.push_back(saved);}
        }
        if(c.foreign)w.changeActorOwner(host,1);
        const EID actor=w.spawnUnit(0,c.passenger,6.5f,10.5f);staged &= w.valid(actor);
        if(!w.valid(actor)){++failures;continue;}
        const auto before=w.checksum();const auto reason=w.entryRejection(actor,host,EntryKind::Board);
        const bool allowed=reason==EntryRejectReason::None;
        command(w,World::Cmd::Board,{actor},host);
        const bool preserved=allowed?w.ents[actor].state==UState::Boarding:w.checksum()==before;
        World resumed;const bool encoded=save(w,resumed,output/(std::string("matrix-")+c.name+".sav"));
        const bool ok=staged && preserved && encoded && allowed==c.allowed;failures+=!ok;
        matrix<<c.name<<','<<allowed<<','<<c.allowed<<','<<(int)reason<<','<<staged<<','<<preserved<<'\n';
        std::printf("SOURCE_TRANSPORT %s matrix=%s scene=%d allowed=%d expected=%d setup=%d preserved=%d save=%d\n",
            ok?"PASS":"FAIL",c.name,w.nativeRulesBaseVersion,allowed,c.allowed,staged,preserved,encoded);
    }
    std::ofstream observations(output/"journeys.csv");observations<<"name,frame,cargo,load,airborne,identities\n";
    for(const auto& c:journeys) {
        world=fixture();world.map.bldOccRef=&world.bldOcc;localPlayer=0;phase=Phase::InGame;gameOver=victory=false;netGame=false;
        const EID host=world.spawnUnit(0,c.host,10.5f,10.5f);std::vector<EID> ids;
        std::map<std::string,World::Ent::GarrisonedUnit> originals;
        for(int i=0;i<c.count;++i) {
            const auto type=c.mixed && i==3?UnitType::Conscript:c.passenger;
            const EID id=world.spawnUnit(0,type,6.5f-i%2,10.5f+i/2);ids.push_back(id);
            if(world.valid(id)) {
                auto& e=world.ents[id];e.hp=73+i;e.kills=4;e.veterancyValue=1600;e.vetRank=1;
                e.crateDmgBoost=4000;e.crateArmorBoost=3500;e.crateSpeedBoost=3000;
                originals[e.nativeIdentity.object]=ActorStateTransfer::capture(e);
            }
        }
        bool staged=world.valid(host) && originals.size()==ids.size() && world.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion;
        for(EID id:ids)staged &= world.valid(id);
        if(!staged){++failures;std::printf("SOURCE_TRANSPORT FAIL journey=%s setup=0\n",c.name);continue;}
        World::Cmd move{};move.type=World::Cmd::Move;move.ids=ids;move.x=6.5f;move.y=20.5f;world.bindCommand(move);world.applyCmd(0,move);
        bool cursor=true;
        if(c.smart) {
            camX=-500;camY=0;camZoom=1;sel=ids;placing=false;dragging=false;showMenu=false;
            syncFogKnowledge();const auto rectangle=unitScreenRect(world.ents[host]);
            const int x=(int)(rectangle.x+rectangle.width*.5f),y=(int)(rectangle.y+rectangle.height*.5f);
            updateHoverCursor(x,y);cursor=cursorKind==CursorKind::Enter;issueSmartOrder(x,y);
        } else command(world,World::Cmd::Board,ids,host);
        int admitted=0;for(EID id:ids)admitted+=world.ents[id].state==UState::Boarding;
        const std::string name=c.name;World resumed,middle;
        bool exact=save(world,resumed,output/(name+"-ordered.sav")),midway=true,active=false;
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<world.checksum()<<'\n';
        std::ofstream(output/(name+".commands"))<<300<<' '<<host<<'\n';
        bool loaded=false,stats=true,identity=true;int fullReports=0;
        for(int frame=1;frame<=600;++frame) {
            if(frame==300){command(world,World::Cmd::Unload,{host});if(exact)command(resumed,World::Cmd::Unload,{host});if(active && midway)command(middle,World::Cmd::Unload,{host});}
            world.update();if(exact){resumed.update();exact=world.checksum()==resumed.checksum();}
            if(active && midway){middle.update();midway=world.checksum()==middle.checksum();}
            trace<<world.checksum()<<'\n';if(active)midTrace<<world.checksum()<<'\n';
            for(const auto& event:world.presentationEvents)if(event.kind==PresentationKind::EntryOrder && event.entry.reason==EntryRejectReason::Full)++fullReports;
            // Source cell contact adds the final approach. Check the complete
            // load immediately before the existing dispatch, not at old range.
            if(frame==299) {
                loaded=(int)world.ents[host].cargo.size()==c.expected;
                if(c.mixed)loaded &= World::cargoLoad(world.ents[host].cargo)==5;
                for(const auto& p:world.ents[host].cargo) {
                    const auto it=originals.find(p.nativeIdentity.object);stats &= it!=originals.end();
                    if(it!=originals.end())stats &= p.hp==it->second.hp && p.kills==4 && p.vetRank==1 && p.veterancyValue==1600
                        && p.combatActor==it->second.combatActor && p.crateDmgBoost>0 && p.crateDmgBoost<4000;
                }
            }
            if(frame==180) {
                midway=save(world,middle,output/(name+"-midway.sav"));active=true;
                midTrace.open(output/(name+"-midway.trace"));midTrace<<world.checksum()<<'\n';
            }
            if(frame==180 || frame==200 || frame==299 || frame==300 || frame==600) {
                int matches=0;for(const auto& [object,_]:originals)for(const auto& e:world.ents)matches+=e.alive && e.nativeIdentity.object==object;
                observations<<name<<','<<frame<<','<<world.ents[host].cargo.size()<<','<<World::cargoLoad(world.ents[host].cargo)<<','<<world.ents[host].airborne()<<','<<matches<<'\n';
            }
            feedback(world);feedback(resumed);feedback(middle);
        }
        for(const auto& [object,original]:originals) {
            int count=0;for(const auto& e:world.ents)if(e.alive && e.nativeIdentity.object==object) {
                ++count;identity &= e.hp==original.hp && e.kills==original.kills && e.vetRank==original.vetRank
                    && e.veterancyValue==original.veterancyValue && e.combatActor==original.combatActor
                    && e.crateDmgBoost>0 && e.crateDmgBoost<original.crateDmgBoost;
            }
            identity &= count==1;
        }
        const bool cleared=world.valid(host) && world.ents[host].cargo.empty();
        const bool behavior=cursor && admitted==c.count && loaded && (!c.mixed || fullReports==1);
        const bool ok=staged && behavior && stats && identity && cleared && exact && midway;failures+=!ok;
        std::ofstream(output/(name+".final"))<<world.checksum()<<'\n';
        std::printf("SOURCE_TRANSPORT %s journey=%s scene=%d setup=%d behavior=%d cursor=%d admitted=%d/%d loaded=%d stats=%d identities=%d clear=%d replay=%d midway=%d fullReports=%d\n",
            ok?"PASS":"FAIL",c.name,world.nativeRulesBaseVersion,staged,behavior,cursor,admitted,c.count,loaded,stats,identity,cleared,exact,midway,fullReports);
        if(const char* old=std::getenv("OPENRA2_LEGACY_TRANSPORT54_INPUT_DIR")) {
            const bool preserved=archived(old,name);failures+=!preserved;
            std::printf("SOURCE_TRANSPORT_LEGACY54 %s journey=%s exact=%d\n",preserved?"PASS":"FAIL",c.name,preserved);
        }
        if(const char* old=std::getenv("OPENRA2_LEGACY_TRANSPORT56_INPUT_DIR")) {
            const bool preserved=archived(old,name,56);failures+=!preserved;
            std::printf("SOURCE_TRANSPORT_LEGACY56 %s journey=%s exact=%d\n",preserved?"PASS":"FAIL",c.name,preserved);
        }
        if(const char* old=std::getenv("OPENRA2_LEGACY_TRANSPORT57_INPUT_DIR")) {
            const bool preserved=archived(old,name,57);failures+=!preserved;
            std::printf("SOURCE_TRANSPORT_LEGACY57 %s journey=%s exact=%d\n",preserved?"PASS":"FAIL",c.name,preserved);
        }
    }
    return failures;
}
