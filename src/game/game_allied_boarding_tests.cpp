#include "game_regression.h"
#include "game/actor_state_transfer.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

namespace {
World fixture(const char* profile="assets/rules/profiles/ra2.ini") {
    World w;
    w.init(64,64,20261123,1,2,{Faction::Soviet,Faction::Allies,Faction::Soviet},0,nullptr,false,false,
        profile,{Country::Russia,Country::America,Country::Russia});
    w.cratesEnabled=false;w.crates.clear();w.aiAlliance=true;w.sharedVision=false;
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& c:w.map.cells)c=Cell{};
    for(auto& f:w.map.fog)std::fill(f.begin(),f.end(),FOG_VISIBLE);
    for(auto& p:w.players){p.money=0;p.defeated=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);
    w.spawnBuilding(1,BldType::ConYard,2,48,true);
    w.spawnBuilding(2,BldType::ConYard,48,2,true);
    return w;
}
void feedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void command(World& w,int player,World::Cmd::Type kind,const std::vector<EID>& ids,EID target=INVALID_EID) {
    World::Cmd c{};c.type=kind;c.ids=ids;c.a=target;w.bindCommand(c);w.applyCmd(player,c);
}
bool save(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"w+b");bool ok=f && w.saveGame(f);
    if(ok){std::rewind(f);ok=copy.loadGame(f) && copy.checksum()==w.checksum();}
    if(f)std::fclose(f);return ok;
}
bool archived(const std::filesystem::path& directory,const std::string& name,bool middle=false) {
    World w;FILE* f=std::fopen((directory/(name+(middle?"-midway.sav":"-ordered.sav"))).string().c_str(),"rb");
    bool ok=f && w.loadGame(f);if(f)std::fclose(f);
    std::ifstream trace(directory/(name+(middle?"-midway.trace":".trace"))),final(directory/(name+".final")),commands(directory/(name+".commands"));
    int frame=0,player=-1;EID host=INVALID_EID;uint64_t value=0;
    ok &= w.nativeRulesBaseVersion==58 && bool(commands>>frame>>player>>host)
        && frame==300 && player==2 && bool(trace>>value) && w.checksum()==value;
    for(int at=middle?181:1;at<=600 && ok;++at) {
        if(at==240)command(w,1,World::Cmd::Unload,{host});
        if(at==300)command(w,2,World::Cmd::Unload,{host});
        w.update();ok=bool(trace>>value) && w.checksum()==value;feedback(w);
    }
    return ok && bool(final>>value) && w.checksum()==value;
}
struct Journey {const char* name;UnitType host,passenger;int input=0;bool mixed=false;};
constexpr Journey journeys[]{
    {"flak-command",UnitType::FlakTrack,UnitType::Conscript},
    {"ifv-classic",UnitType::IFV,UnitType::Engineer,1},
    {"nighthawk-modern",UnitType::Nighthawk,UnitType::TerrorDrone,2},
    {"amph-command",UnitType::AmphTransport,UnitType::Rhino},
    {"mixed-owner",UnitType::FlakTrack,UnitType::Conscript,0,true}
};
}

int GameRegression::alliedBoardingRegression(const char* outputDir) {
    g_script.shutdown();SetTargetFPS(0);selectRulesChoice(RulesChoice::RA2);
    const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);
    int failures=0;std::ofstream observations(output/"journeys.csv");
    observations<<"name,frame,cargo,foreign,local,live_foreign\n";
    for(const auto& c:journeys) {
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;
        world=fixture();world.map.bldOccRef=&world.bldOcc;localPlayer=1;
        phase=Phase::InGame;showMenu=paused=gameOver=victory=netGame=false;
        cfgClassicOrders=c.input!=2;camX=-500;camY=0;camZoom=1;camEdgeLock=10000;
        const EID host=world.spawnUnit(2,c.host,10.5f,10.5f);
        const EID passenger=world.spawnUnit(1,c.passenger,6.5f,10.5f);
        std::map<std::string,std::pair<int,World::Ent::GarrisonedUnit>> originals;
        const auto record=[&](EID id) {
            if(!world.valid(id))return;
            auto& e=world.ents[id];e.hp=73;e.kills=4;e.vetRank=1;e.veterancyValue=1600;
            e.crateDmgBoost=4000;e.crateArmorBoost=3500;e.crateSpeedBoost=3000;
            originals[e.nativeIdentity.object]={e.player,ActorStateTransfer::capture(e)};
        };
        record(passenger);
        if(c.mixed) {
            const EID own=world.spawnUnit(2,UnitType::Conscript,14.5f,10.5f);record(own);
            command(world,2,World::Cmd::Board,{own},host);
        }
        bool setup=world.valid(host) && world.valid(passenger) && world.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion
            && world.isAllied(1,2) && world.isAllied(2,1) && world.isEnemy(0,1)
            && originals.size()==(c.mixed?2u:1u);
        if(!setup){++failures;std::printf("ALLIED_BOARD FAIL journey=%s setup=0\n",c.name);continue;}
        bool cursor=true,picked=true,network=true;
        if(c.input) {
            sel={passenger};syncFogKnowledge();const auto box=unitScreenRect(world.ents[host]);
            const int x=(int)(box.x+box.width*.5f),y=(int)(box.y+box.height*.5f);
            picked=pickCommandUnit(x,y)==host;updateHoverCursor(x,y);cursor=cursorKind==CursorKind::Enter;
            const auto before=world.checksum();pendingCmds.clear();netGame=c.input==1;
            sim={};sim.active=true;sim.pos={(float)x,(float)y};
            sim.pressedL=sim.downL=c.input==1;sim.pressedR=sim.downR=c.input==2;handleInput();
            sim.pressedL=sim.downL=sim.pressedR=sim.downR=false;
            sim.releasedL=c.input==1;sim.releasedR=c.input==2;handleInput();sim={};
            if(c.input==1) {
                network=pendingCmds.size()==1 && pendingCmds.front().type==World::Cmd::Board
                    && pendingCmds.front().ids==std::vector<EID>{passenger}
                    && pendingCmds.front().a==host && pendingCmds.front().identitiesBound
                    && pendingCmds.front().generations==std::vector<uint64_t>{world.ents[passenger].generation}
                    && pendingCmds.front().targetGeneration==world.ents[host].generation && world.checksum()==before;
                if(network)world.applyCmd(1,pendingCmds.front());
            }
            netGame=false;pendingCmds.clear();
        } else command(world,1,World::Cmd::Board,{passenger},host);
        setup &= picked;
        const bool admitted=world.valid(passenger) && world.ents[passenger].state==UState::Boarding && world.ents[passenger].target==host;
        const std::string name=c.name;World resumed,middle;
        bool exact=save(world,resumed,output/(name+"-ordered.sav")),midway=true,active=false;
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<world.checksum()<<'\n';
        std::ofstream(output/(name+".commands"))<<300<<' '<<2<<' '<<host<<'\n';
        bool loaded=false,owners=true,stats=true,authority=true;
        for(int at=1;at<=600;++at) {
            const auto controls=[&](World& w) {
                if(at==240){const auto before=w.checksum();command(w,1,World::Cmd::Unload,{host});authority &= before==w.checksum();}
                if(at==300)command(w,2,World::Cmd::Unload,{host});
            };
            controls(world);world.update();
            if(exact){controls(resumed);resumed.update();exact=world.checksum()==resumed.checksum();}
            if(active && midway){controls(middle);middle.update();midway=world.checksum()==middle.checksum();}
            trace<<world.checksum()<<'\n';if(active)midTrace<<world.checksum()<<'\n';
            if(at==200) {
                loaded=world.valid(host) && world.ents[host].cargo.size()==originals.size() && !world.valid(passenger);
                for(const auto& p:world.ents[host].cargo) {
                    const auto it=originals.find(p.nativeIdentity.object);owners &= it!=originals.end();
                    if(it!=originals.end()) {
                        owners &= p.player==it->second.first;
                        const auto& old=it->second.second;stats &= p.hp==old.hp && p.kills==old.kills && p.vetRank==old.vetRank
                            && p.veterancyValue==old.veterancyValue && p.combatActor==old.combatActor;
                    }
                }
            }
            if(at==180){midway=save(world,middle,output/(name+"-midway.sav"));active=true;midTrace.open(output/(name+"-midway.trace"));midTrace<<world.checksum()<<'\n';}
            if(c.mixed && at==300){World pending;stats &= save(world,pending,output/"mixed-owner-unloading.sav");}
            if(at==180 || at==200 || at==300 || at==600) {
                int foreign=0,local=0,live=0;
                for(const auto& p:world.ents[host].cargo){foreign+=World::passengerOwner(p,2)==1;local+=World::passengerOwner(p,2)==2;}
                for(const auto& e:world.ents)if(e.alive && e.player==1 && originals.count(e.nativeIdentity.object))++live;
                observations<<name<<','<<at<<','<<world.ents[host].cargo.size()<<','<<foreign<<','<<local<<','<<live<<'\n';
            }
            feedback(world);feedback(resumed);feedback(middle);
        }
        for(const auto& [object,old]:originals) {
            int count=0;for(const auto& e:world.ents)if(e.alive && e.nativeIdentity.object==object) {
                ++count;owners &= e.player==old.first;const auto& p=old.second;
                stats &= e.hp==p.hp && e.kills==p.kills && e.vetRank==p.vetRank && e.veterancyValue==p.veterancyValue
                    && e.combatActor==p.combatActor && e.crateDmgBoost>0 && e.crateDmgBoost<p.crateDmgBoost;
            }
            owners &= count==1;
        }
        const bool cleared=world.valid(host) && world.ents[host].cargo.empty();
        const bool ok=setup && cursor && network && admitted && loaded && owners && stats && authority && cleared && exact && midway;
        failures+=!ok;std::ofstream(output/(name+".final"))<<world.checksum()<<'\n';
        std::printf("ALLIED_BOARD %s journey=%s scene=%d setup=%d cursor=%d admitted=%d loaded=%d owners=%d stats=%d authority=%d clear=%d replay=%d midway=%d network=%d\n",
            ok?"PASS":"FAIL",c.name,world.nativeRulesBaseVersion,setup,cursor,admitted,loaded,owners,stats,authority,cleared,exact,midway,network);
        if(const char* previous=std::getenv("OPENRA2_LEGACY_ALLIED_BOARD58_INPUT_DIR")) {
            const bool ordered=archived(previous,name),middle=archived(previous,name,true),preserved=ordered && middle;failures+=!preserved;
            std::printf("ALLIED_BOARD_LEGACY58 %s journey=%s ordered=%d midway=%d exact=%d\n",preserved?"PASS":"FAIL",c.name,ordered,middle,preserved);
        }
    }
    const auto check=[&](bool ok,const char* kind,const char* name) {
        failures+=!ok;std::printf("ALLIED_BOARD_%s %s case=%s\n",kind,ok?"PASS":"FAIL",name);return ok;
    };
    for(int scenario=0;scenario<5;++scenario) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(2,scenario==3?UnitType::IFV:UnitType::FlakTrack,10.5f,10.5f);
        const EID unit=w.spawnUnit(1,scenario==2?UnitType::Rhino:scenario==3?UnitType::TerrorDrone:scenario==4?UnitType::GI:UnitType::Conscript,6.5f,10.5f);
        if(scenario<2)w.changeActorOwner(host,scenario==0?0:-1);
        if(scenario==4){command(w,1,World::Cmd::Deploy,{unit});for(int i=0;i<60;++i)w.update();}
        const auto reason=scenario<2?EntryRejectReason::WrongOwner:scenario==4?EntryRejectReason::Deployed:EntryRejectReason::Incompatible;
        const auto before=w.checksum();command(w,1,World::Cmd::Board,{unit},host);
        const bool setup=w.valid(host) && w.valid(unit) && (scenario!=4 || w.ents[unit].deployed);
        check(setup && w.entryRejection(unit,host,EntryKind::Board)==reason && w.checksum()==before,"GUARD",
            scenario==0?"enemy":scenario==1?"neutral":scenario==2?"oversize":scenario==3?"ifv-size":"deployed");
    }
    {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(2,UnitType::FlakTrack,10.5f,10.5f),unit=w.spawnUnit(2,UnitType::Conscript,6.5f,10.5f);
        const auto before=w.checksum();command(w,1,World::Cmd::Board,{unit},host);
        check(w.valid(unit) && w.checksum()==before && w.ents[unit].state!=UState::Boarding,"GUARD","foreign-selection");
    }
    {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(2,UnitType::FlakTrack,10.5f,10.5f);
        std::vector<EID> own;
        for(const auto& p:std::array<Vec2i,5>{{{9,10},{10,9},{11,10},{10,11},{9,9}}})
            own.push_back(w.spawnUnit(2,UnitType::Conscript,p.x+.5f,p.y+.5f));
        command(w,2,World::Cmd::Board,own,host);
        for(int at=0;at<180 && w.ents[host].cargo.size()<5;++at)w.update();
        const EID extra=w.spawnUnit(1,UnitType::Conscript,5.5f,10.5f);
        const auto before=w.checksum();command(w,1,World::Cmd::Board,{extra},host);
        check(w.ents[host].cargo.size()==5 && w.entryRejection(extra,host,EntryKind::Board)==EntryRejectReason::Full
            && w.checksum()==before,"GUARD","actual-full-carrier");
    }
    std::ofstream flows(output/"flows.csv");flows<<"name,admitted,cargo200,full_reports,ended,owners,replay,midway\n";
    for(int scenario=0;scenario<3;++scenario) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const char* name=scenario==0?"relation-lost":scenario==1?"carrier-changed":"capacity-race";
        const EID host=w.spawnUnit(2,scenario==2?UnitType::IFV:UnitType::FlakTrack,scenario==2?10.5f:15.5f,10.5f);
        std::vector<EID> actors{w.spawnUnit(1,UnitType::Conscript,scenario==2?6.5f:2.5f,10.5f)};
        if(scenario==2)actors.push_back(w.spawnUnit(1,UnitType::Conscript,4.5f,10.5f));
        std::vector<std::string> objects;for(EID id:actors)objects.push_back(w.ents[id].nativeIdentity.object);
        command(w,1,World::Cmd::Board,actors,host);
        bool admitted=true;for(EID id:actors)admitted &= w.ents[id].state==UState::Boarding && w.ents[id].target==host;
        World resumed,middle;bool exact=save(w,resumed,output/(std::string(name)+"-ordered.sav")),midway=true,active=false;
        std::ofstream trace(output/(std::string(name)+".trace")),midTrace;trace<<w.checksum()<<'\n';
        std::ofstream commands(output/(std::string(name)+".commands"));
        if(scenario==0)commands<<"45 AIAlliance 0\n";
        if(scenario==1)commands<<"45 Owner "<<host<<" 0\n";
        commands<<"300 Unload 2 "<<host<<'\n';
        int cargo200=-1,full=0;
        for(int at=1;at<=600;++at) {
            const auto controls=[&](World& q) {
                if(at==45 && scenario==0)q.aiAlliance=false;
                if(at==45 && scenario==1)q.changeActorOwner(host,0);
                if(at==300)command(q,2,World::Cmd::Unload,{host});
            };
            controls(w);w.update();
            if(exact){controls(resumed);resumed.update();exact=w.checksum()==resumed.checksum();}
            if(active && midway){controls(middle);middle.update();midway=w.checksum()==middle.checksum();}
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            for(const auto& event:w.presentationEvents)if(event.kind==PresentationKind::EntryOrder && event.entry.reason==EntryRejectReason::Full)++full;
            if(at==200)cargo200=(int)w.ents[host].cargo.size();
            if(at==30){midway=save(w,middle,output/(std::string(name)+"-midway.sav"));active=true;midTrace.open(output/(std::string(name)+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(resumed);feedback(middle);
        }
        bool ended=w.ents[host].cargo.empty(),owners=w.ents[host].player==(scenario==1?0:2);
        for(const auto& object:objects) {
            int count=0;for(const auto& e:w.ents)if(e.alive && e.nativeIdentity.object==object) {
                ++count;owners &= e.player==1;ended &= e.state!=UState::Boarding;
            }
            owners &= count==1;
        }
        const bool ok=admitted && cargo200==(scenario==2?1:0) && full==(scenario==2?1:0) && ended && owners && exact && midway;
        check(ok,"FLOW",name);flows<<name<<','<<admitted<<','<<cargo200<<','<<full<<','<<ended<<','<<owners<<','<<exact<<','<<midway<<'\n';
        std::ofstream(output/(std::string(name)+".final"))<<w.checksum()<<'\n';
    }
    {
        FILE* f=std::fopen((output/"mixed-owner-unloading.sav").string().c_str(),"rb");
        World pending;bool ok=f && pending.loadGame(f);if(f)std::fclose(f);
        world=std::move(pending);world.map.bldOccRef=&world.bldOcc;
        const auto before=world.checksum();const auto app=output/"mixed-app11.sav";
        auto copy=std::make_unique<Game>();ok &= saveGameFile(app.string().c_str()) && copy->loadGameFile(app.string().c_str())
            && copy->world.checksum()==before && copy->world.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion;
        int owners=0;std::map<std::string,int> expected;
        for(const auto& e:world.ents)if(e.alive && !e.isBuilding)for(const auto& p:e.cargo) {
            const int player=World::passengerOwner(p,e.player);if(player>=0)owners|=1<<player;
            expected[p.nativeIdentity.object]=player;
        }
        ok &= owners==6 && expected.size()==2;
        std::ifstream input(app,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),{}};
        const auto current="Version = "+std::to_string(nativeCurrentRulesSceneVersion);int rejected=0;
        for(int version:{maximumNativeSceneVersion+1,nativeTransportUnloadSceneVersion}) {
            auto broken=bytes;const auto at=broken.find(current);bool atomic=at!=std::string::npos;
            if(atomic)broken.replace(at,current.size(),"Version = "+std::to_string(version));
            const auto bad=output/("mixed-app-rejected-"+std::to_string(version)+".sav");
            {std::ofstream out(bad,std::ios::binary);out.write(broken.data(),broken.size());atomic &= bool(out);}
            atomic &= !loadGameFile(bad.string().c_str()) && world.checksum()==before;rejected+=atomic;ok &= atomic;
        }
        std::ofstream trace(output/"mixed-app.trace");trace<<world.checksum()<<'\n';
        for(int at=0;at<600 && ok;++at) {
            world.update();copy->world.update();ok=world.checksum()==copy->world.checksum();trace<<world.checksum()<<'\n';
            feedback(world);feedback(copy->world);
        }
        for(const auto& [object,owner]:expected) {
            int count=0;for(const auto& e:world.ents)if(e.alive && e.nativeIdentity.object==object){++count;ok &= e.player==owner;}
            ok &= count==1;
        }
        std::ofstream(output/"mixed-app.final")<<world.checksum()<<'\n';
        check(ok && rejected==2,"APP","world76-independent-owners");
    }
    for(auto rules:{RulesChoice::YR,RulesChoice::Fusion}) {
        selectRulesChoice(rules);World w=fixture(rules==RulesChoice::YR?"assets/rules/profiles/yr.ini":nullptr);
        w.map.bldOccRef=&w.bldOcc;
        const EID host=w.spawnUnit(2,UnitType::AmphTransport,10.5f,10.5f),unit=w.spawnUnit(1,UnitType::GI,6.5f,10.5f);
        const auto before=w.checksum();command(w,1,World::Cmd::Board,{unit},host);
        check(w.valid(host) && w.valid(unit) && unitDef(w.ents[host]).cargoCap>0 && w.isAllied(1,2)
            && w.entryRejection(unit,host,EntryKind::Board)==EntryRejectReason::WrongOwner && w.checksum()==before,
            "GUARD",rules==RulesChoice::YR?"yr-policy":"fusion-policy");
    }
    selectRulesChoice(RulesChoice::RA2);
    return failures;
}
