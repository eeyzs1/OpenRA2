#include "game_regression.h"
#include "game/actor_state_transfer.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace {
World fixture(const char* profile="assets/rules/profiles/ra2.ini") {
    World w;
    w.init(64,64,20261124,1,2,{Faction::Soviet,Faction::Allies,Faction::Soviet},0,nullptr,false,false,
        profile,{Country::Russia,Country::America,Country::Russia});
    w.cratesEnabled=false;w.crates.clear();w.aiAlliance=true;w.sharedVision=false;
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& c:w.map.cells)c=Cell{};
    for(auto& f:w.map.fog)std::fill(f.begin(),f.end(),FOG_VISIBLE);
    for(auto& p:w.players){p.money=0;p.defeated=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);w.spawnBuilding(2,BldType::ConYard,48,2,true);
    return w;
}
void feedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void board(World& w,int player,EID unit,EID carrier) {
    World::Cmd c{};c.type=World::Cmd::Board;c.ids={unit};c.a=carrier;w.bindCommand(c);w.applyCmd(player,c);
}
void command(World& w,World::Cmd::Type type,EID unit,float x=0,float y=0) {
    World::Cmd c{};c.type=type;c.ids={unit};c.x=x;c.y=y;w.bindCommand(c);w.applyCmd(0,c);
}
bool save(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"w+b");bool ok=f && w.saveGame(f);
    if(ok){std::rewind(f);ok=copy.loadGame(f) && w.checksum()==copy.checksum();}
    if(f)std::fclose(f);return ok;
}
bool archived(const std::filesystem::path& directory,const std::string& name) {
    World w;FILE* f=std::fopen((directory/(name+"-ordered.sav")).string().c_str(),"rb");
    bool ok=f && w.loadGame(f);if(f)std::fclose(f);
    std::ifstream trace(directory/(name+".trace")),final(directory/(name+".final"));uint64_t value=0;
    ok &= w.nativeRulesBaseVersion==59 && bool(trace>>value) && w.checksum()==value;
    for(int at=1;at<=600 && ok;++at){w.update();ok=bool(trace>>value) && w.checksum()==value;feedback(w);}
    return ok && bool(final>>value) && w.checksum()==value;
}
struct Journey {const char* name;UnitType host,passenger;int dx,dy,player=0,owner=0;};
constexpr Journey journeys[]{
    {"foot-n",UnitType::FlakTrack,UnitType::Conscript,0,-1},
    {"foot-ne",UnitType::FlakTrack,UnitType::Conscript,1,-1},
    {"foot-e",UnitType::FlakTrack,UnitType::Conscript,1,0},
    {"foot-se",UnitType::FlakTrack,UnitType::Conscript,1,1},
    {"foot-s",UnitType::FlakTrack,UnitType::Conscript,0,1},
    {"foot-sw",UnitType::FlakTrack,UnitType::Conscript,-1,1},
    {"foot-w",UnitType::FlakTrack,UnitType::Conscript,-1,0},
    {"foot-nw",UnitType::FlakTrack,UnitType::Conscript,-1,-1},
    {"ifv-engineer",UnitType::IFV,UnitType::Engineer,-1,0},
    {"amph-tank",UnitType::AmphTransport,UnitType::Rhino,-1,0},
    {"nighthawk-drone",UnitType::Nighthawk,UnitType::TerrorDrone,-1,0},
    {"allied-foot",UnitType::FlakTrack,UnitType::Conscript,-1,0,1,2}
};
}

int GameRegression::transportBoardingRegression(const char* outputDir) {
    g_script.shutdown();SetTargetFPS(0);selectRulesChoice(RulesChoice::RA2);
    const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);int failures=0;
    const auto check=[&](bool ok,const char* kind,const char* name) {
        failures+=!ok;std::printf("TRANSPORT_BOARD_%s %s case=%s\n",kind,ok?"PASS":"FAIL",name);
    };
    {
        World infantry=fixture(),vehicle=fixture();
        infantry.map.bldOccRef=&infantry.bldOcc;vehicle.map.bldOccRef=&vehicle.bldOcc;
        const EID foot=infantry.spawnUnit(0,UnitType::Conscript,10.5f,10.5f),flak=infantry.spawnUnit(0,UnitType::FlakTrack,10.5f,10.5f);
        const EID tank=vehicle.spawnUnit(0,UnitType::Rhino,10.5f,10.5f),amph=vehicle.spawnUnit(0,UnitType::AmphTransport,10.5f,10.5f);
        std::ifstream input("tests/fixtures/ra2-transport-boarding-positions.csv");std::string row;std::getline(input,row);
        std::ofstream observed(output/"contact-matrix.csv");observed<<"kind,x,y,target_x,target_y,expected,actual,readonly\n";
        int count=0,allowed=0;bool exact=bool(input) && infantry.valid(foot) && infantry.valid(flak) && vehicle.valid(tank) && vehicle.valid(amph);
        while(exact && std::getline(input,row)) {
            std::replace(row.begin(),row.end(),',',' ');std::istringstream values(row);
            int kind,x,y,tx,ty,cx,cy,hx,hy,expected;
            if(!(values>>kind>>x>>y>>tx>>ty>>cx>>cy>>hx>>hy>>expected) || (kind!=1 && kind!=15)){exact=false;break;}
            auto& w=kind==15?infantry:vehicle;const EID actor=kind==15?foot:tank,host=kind==15?flak:amph;
            w.ents[actor].x=x/256.f;w.ents[actor].y=y/256.f;w.ents[host].x=tx/256.f;w.ents[host].y=ty/256.f;
            w.unitOccSync(actor);w.unitOccSync(host);const auto before=w.checksum();
            const bool actual=w.entryWithinReach(w.ents[actor],w.ents[host]);
            const bool readonly=w.checksum()==before;
            exact &= actual==bool(expected) && readonly && w.entryRejection(actor,host,EntryKind::Board)==EntryRejectReason::None;
            observed<<kind<<','<<x<<','<<y<<','<<tx<<','<<ty<<','<<expected<<','<<actual<<','<<readonly<<'\n';
            ++count;allowed+=actual;
        }
        check(exact && count==200 && allowed==32,"MATRIX","200-original-positions");
    }
    std::ofstream positions(output/"positions.csv");
    positions<<"name,scene,first,retired_x,retired_y,host_x,host_y,cell_contact,retired_identity,owner,stats,replay,midway\n";
    for(const auto& c:journeys) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID actor=w.spawnUnit(c.player,c.passenger,20.5f+c.dx*6.f,20.5f+c.dy*6.f);
        const EID host=w.spawnUnit(c.owner,c.host,20.5f,20.5f);
        bool setup=w.valid(actor) && w.valid(host) && w.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion;
        if(!setup){++failures;std::printf("TRANSPORT_BOARD FAIL case=%s setup=0\n",c.name);continue;}
        const auto ref=w.entityRef(actor);auto& unit=w.ents[actor];
        unit.hp=73;unit.kills=4;unit.vetRank=1;unit.veterancyValue=1600;
        unit.crateDmgBoost=4000;unit.crateArmorBoost=3500;unit.crateSpeedBoost=3000;
        const auto before=ActorStateTransfer::capture(unit);
        setup &= w.entryRejection(actor,host,EntryKind::Board)==EntryRejectReason::None;
        board(w,c.player,actor,host);const bool admitted=w.valid(ref) && w.ents[actor].state==UState::Boarding && w.ents[actor].target==host;
        const std::string name=c.name;World resumed,middle;
        bool exact=save(w,resumed,output/(name+"-ordered.sav")),midway=true,active=false,retired=false,stats=true,owners=true;
        int first=-1;float x=0,y=0,hostX=0,hostY=0;
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<w.checksum()<<'\n';
        for(int at=1;at<=600;++at) {
            w.update();if(exact){resumed.update();exact=w.checksum()==resumed.checksum();}
            if(active && midway){middle.update();midway=w.checksum()==middle.checksum();}
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            if(first<0 && w.valid(host) && !w.ents[host].cargo.empty()) {
                first=at;retired=actor<(EID)w.ents.size() && !w.ents[actor].alive && w.ents[actor].generation==ref.generation;
                if(retired){x=w.ents[actor].x;y=w.ents[actor].y;}
                hostX=w.ents[host].x;hostY=w.ents[host].y;World loaded;
                exact &= save(w,loaded,output/(name+"-loaded.sav"));
            }
            if(at==30){midway=save(w,middle,output/(name+"-midway.sav"));active=true;midTrace.open(output/(name+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(resumed);feedback(middle);
        }
        const bool loaded=w.valid(host) && w.ents[host].cargo.size()==1 && !w.valid(ref) && first>0;
        if(loaded) {
            const auto& p=w.ents[host].cargo.front();owners=World::passengerOwner(p,c.owner)==c.player;
            stats=p.hp==before.hp && p.kills==before.kills && p.vetRank==before.vetRank && p.veterancyValue==before.veterancyValue
                && p.combatActor==before.combatActor && p.nativeIdentity.object==before.nativeIdentity.object
                && p.crateDmgBoost>0 && p.crateDmgBoost<before.crateDmgBoost;
        }
        const bool contact=retired && std::floor(x)==std::floor(hostX) && std::floor(y)==std::floor(hostY);
        const bool ok=setup && admitted && loaded && contact && retired && owners && stats && exact && midway;failures+=!ok;
        std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
        positions<<name<<','<<w.nativeRulesBaseVersion<<','<<first<<','<<x<<','<<y<<','<<hostX<<','<<hostY<<','<<contact<<','<<retired<<','<<owners<<','<<stats<<','<<exact<<','<<midway<<'\n';
        std::printf("TRANSPORT_BOARD %s case=%s scene=%d setup=%d admitted=%d loaded=%d cell=%d retired=%d owner=%d stats=%d first=%d replay=%d midway=%d\n",
            ok?"PASS":"FAIL",c.name,w.nativeRulesBaseVersion,setup,admitted,loaded,contact,retired,owners,stats,first,exact,midway);
        if(const char* old=std::getenv("OPENRA2_LEGACY_BOARD59_INPUT_DIR")) {
            const bool previous=archived(old,name);failures+=!previous;
            std::printf("TRANSPORT_BOARD_LEGACY59 %s case=%s exact=%d\n",previous?"PASS":"FAIL",c.name,previous);
        }
    }
    constexpr const char* flows[]{"moving-carrier","stop","move","retired-carrier","other-occupant","off-center","shore","capacity-race"};
    std::ofstream flowRows(output/"flows.csv");flowRows<<"name,admitted,guard,blocked,loaded,contact,replay,midway,outcome\n";
    for(int scenario=0;scenario<8;++scenario) {
        const std::string name=flows[scenario];World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID actor=w.spawnUnit(0,UnitType::Conscript,scenario==4?19.5f:scenario==6?22.5f:14.5f,20.5f);
        const EID host=w.spawnUnit(0,scenario==6?UnitType::AmphTransport:scenario==7?UnitType::IFV:UnitType::FlakTrack,
            scenario==5?20.01f:scenario==6?24.5f:20.5f,scenario==5?20.99f:20.5f);
        const EID second=scenario==7?w.spawnUnit(0,UnitType::Conscript,26.5f,20.5f):INVALID_EID;
        const EID blocker=scenario==4?w.spawnUnit(0,UnitType::Grizzly,20.5f,20.5f):INVALID_EID;
        if(scenario==4) {
            // This supplied overlap tests physical admission. Hold both
            // actors through the existing recovery timer so idle unstacking
            // cannot remove the blocker before the movement observation.
            command(w,World::Cmd::Guard,blocker);w.ents[blocker].tpSick=120;w.ents[host].tpSick=120;
        }
        const auto actorRef=w.entityRef(actor),hostRef=w.entityRef(host),secondRef=w.entityRef(second);
        if(scenario==6)for(int y=0;y<64;++y)for(int x=24;x<64;++x)w.map.at(x,y).terrain=Terrain::Water;
        const auto before=w.checksum();board(w,0,actor,host);if(scenario==7)board(w,0,second,host);
        const bool admitted=scenario==6?w.checksum()==before && w.ents[actor].state!=UState::Boarding:w.ents[actor].state==UState::Boarding;
        const bool strict=w.cellHardBlockedForMove((int)w.ents[host].x,(int)w.ents[host].y,actor,0);
        const bool physical=w.cellHardBlockedForMove((int)w.ents[host].x,(int)w.ents[host].y,actor,0,true);
        bool guard=strict && physical==(scenario==4 || scenario==6),blocked=scenario!=4,actions=true;
        bool contact=true,seen=false,exact=true,midway=true,active=false;World resumed,middle;
        exact=save(w,resumed,output/(name+"-ordered.sav"));
        std::ofstream trace(output/(name+".trace")),midTrace;trace<<w.checksum()<<'\n';
        for(int at=1;at<=600;++at) {
            const auto controls=[&](World& q) {
                if(at==20 && scenario==0)command(q,World::Cmd::Move,host,24.5f,25.5f);
                if(at==20 && scenario==1)command(q,World::Cmd::Stop,actor);
                if(at==20 && scenario==2)command(q,World::Cmd::Move,actor,10.5f,26.5f);
                if(at==20 && scenario==3) {
                    q.kill(host,false);const EID replacement=q.spawnUnit(0,UnitType::FlakTrack,20.5f,20.5f);
                    actions &= replacement==host && !q.valid(hostRef);
                }
                if(at==80 && scenario==4)q.kill(blocker,false);
                if(at==1 && scenario==6)command(q,World::Cmd::Move,host,23.5f,20.5f);
                if(at==80 && scenario==6)board(q,0,actor,host);
            };
            if(at==80 && scenario==4)blocked=w.valid(actorRef) && w.ents[host].cargo.empty()
                && (int)w.ents[actor].x!=20 && w.cellHardBlockedForMove(20,20,actor,0,true);
            controls(w);w.update();
            if(exact){controls(resumed);resumed.update();exact=w.checksum()==resumed.checksum();}
            if(active && midway){controls(middle);middle.update();midway=w.checksum()==middle.checksum();}
            trace<<w.checksum()<<'\n';if(active)midTrace<<w.checksum()<<'\n';
            for(const auto ref:{actorRef,secondRef})if(ref.generation && !w.valid(ref) && ref.id<(EID)w.ents.size()
                && w.ents[ref.id].generation==ref.generation) {
                if(!seen || scenario==7)contact &= (int)w.ents[ref.id].x==(int)w.ents[host].x && (int)w.ents[ref.id].y==(int)w.ents[host].y;
                seen=true;
            }
            if(at==30){midway=save(w,middle,output/(name+"-midway.sav"));active=true;midTrace.open(output/(name+"-midway.trace"));midTrace<<w.checksum()<<'\n';}
            feedback(w);feedback(resumed);feedback(middle);
        }
        const bool loaded=w.ents[host].cargo.size()==1;
        bool outcome=loaded && seen && !w.valid(actorRef);
        if(scenario==0)outcome &= (int)w.ents[host].x==24 && (int)w.ents[host].y==25;
        if(scenario==1 || scenario==2 || scenario==3)outcome=w.valid(actorRef) && w.ents[host].cargo.empty()
            && w.ents[actor].state!=UState::Boarding && w.ents[actor].target!=host
            && w.cellHardBlockedForMove(20,20,actor,0,true);
        if(scenario==2)outcome &= distf(w.ents[actor].x,w.ents[actor].y,10.5f,26.5f)<.1f;
        if(scenario==6)outcome &= w.map.at((int)w.ents[host].x,(int)w.ents[host].y).terrain!=Terrain::Water;
        if(scenario==7) {
            const EID remaining=w.valid(actorRef)?actor:second;
            outcome=loaded && seen && w.valid(actorRef)!=w.valid(secondRef) && w.ents[remaining].state!=UState::Boarding
                && w.cellHardBlockedForMove(20,20,remaining,0,true);
        }
        const bool ok=admitted && guard && blocked && actions && contact && exact && midway && outcome;
        check(ok,"FLOW",name.c_str());
        flowRows<<name<<','<<admitted<<','<<guard<<','<<blocked<<','<<loaded<<','<<contact<<','<<exact<<','<<midway<<','<<outcome<<'\n';
        std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
    }
    {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const EID actor=w.spawnUnit(0,UnitType::Conscript,20.5f,20.5f),host=w.spawnUnit(0,UnitType::FlakTrack,20.5f,20.5f);
        w.map.bridgeCells.assign(w.map.cells.size(),{});w.map.bridgeCells[20*w.map.w+20]={0,4,0};
        w.ents[host].navLayer=w.ents[host].previousNavLayer=w.ents[host].goalLayer=1;w.unitOccSync(host);
        const auto before=w.checksum();const bool reachable=w.entryWithinReach(w.ents[actor],w.ents[host]);
        check(!reachable && w.checksum()==before,"GUARD","different-bridge-height");
    }
    {
        World pending;FILE* f=std::fopen((output/"foot-e-midway.sav").string().c_str(),"rb");
        bool ok=f && pending.loadGame(f);if(f)std::fclose(f);
        world=std::move(pending);world.map.bldOccRef=&world.bldOcc;
        const auto before=world.checksum();const auto app=output/"approach-app11.sav";auto copy=std::make_unique<Game>();
        ok &= saveGameFile(app.string().c_str()) && copy->loadGameFile(app.string().c_str()) && copy->world.checksum()==before;
        std::ifstream input(app,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),{}};
        int rejected=0;
        for(int variant=0;variant<2;++variant) {
            auto broken=bytes;const auto current="Version = "+std::to_string(nativeCurrentRulesSceneVersion);
            const auto at=broken.find(current);bool atomic=at!=std::string::npos;
            if(variant==0 && atomic)broken.replace(at,current.size(),"Version = "+std::to_string(maximumNativeSceneVersion+1));
            if(variant==1)broken.resize(broken.size()/2);
            const auto bad=output/("approach-rejected-"+std::to_string(variant)+".sav");
            {std::ofstream out(bad,std::ios::binary);out.write(broken.data(),broken.size());atomic &= bool(out);}
            atomic &= !loadGameFile(bad.string().c_str()) && world.checksum()==before;rejected+=atomic;ok &= atomic;
        }
        std::ofstream trace(output/"approach-app.trace");trace<<world.checksum()<<'\n';
        for(int at=0;at<600 && ok;++at) {
            world.update();copy->world.update();ok=world.checksum()==copy->world.checksum();trace<<world.checksum()<<'\n';
            feedback(world);feedback(copy->world);
        }
        std::ofstream(output/"approach-app.final")<<world.checksum()<<'\n';
        check(ok && rejected==2,"APP","approach-and-atomic-rejection");
    }
    for(auto rules:{RulesChoice::YR,RulesChoice::Fusion}) {
        selectRulesChoice(rules);World w=fixture(rules==RulesChoice::YR?"assets/rules/profiles/yr.ini":nullptr);w.map.bldOccRef=&w.bldOcc;
        const EID actor=w.spawnUnit(0,UnitType::GI,19.5f,20.5f),host=w.spawnUnit(0,UnitType::IFV,20.5f,20.5f);
        const bool oldRange=w.entryWithinReach(w.ents[actor],w.ents[host]);board(w,0,actor,host);w.update();
        check(oldRange && !w.valid(actor) && w.ents[host].cargo.size()==1,"LEGACY",rules==RulesChoice::YR?"yr-range":"fusion-range");
    }
    selectRulesChoice(RulesChoice::RA2);
    return failures;
}
