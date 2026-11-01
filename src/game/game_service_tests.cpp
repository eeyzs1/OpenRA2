#include "game/game.h"
#include "game/lang.h"
#include <cstdio>
#ifdef RA2_TESTING
bool writeLegacyV21(const World&,FILE*);
#endif

int Game::serviceRegression() {
    int failures=0;localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;camZoom=1;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("SERVICE %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;
    };
    auto issue=[&](const std::vector<EID>& units,EID target) {
        World::Cmd cmd;cmd.type=World::Cmd::Service;cmd.ids=units;cmd.a=target;world.applyCmd(0,cmd);
    };
    auto powered=[&]() {
        for(size_t id=0;id<world.ents.size();++id)
            if(world.ents[id].alive && world.ents[id].isBuilding && world.ents[id].btype==BldType::PowerPlant) world.kill((EID)id,false);
        world.spawnBuilding(0,BldType::PowerPlant,50,50,true);
    };
    reset();EID tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,20.5f);
    EID depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);world.ents[tank].hp=100;
    world.orderMove({tank},8,20,false);auto oldPath=world.ents[tank].path;auto oldState=world.ents[tank].state;
    for(int y=0;y<64;++y) world.map.at(20,y).terrain=Terrain::Water;
    world.orderService({tank},depot);
    check(world.ents[tank].target==INVALID_EID && world.ents[tank].state==oldState && world.ents[tank].path==oldPath,
        "unreachable repair order preserves the previous move");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,20.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::TechOutpost,23,20,true);world.ents[tank].hp=100;world.players[0].money=1000;
    world.players[0].powerMade=10000;world.orderService({tank},depot);
    for(int frame=0;frame<400;++frame) world.update();
    check(world.ents[tank].hp==unitDef(UnitType::Grizzly).hp && world.players[0].money<1000,
        "owned tech outpost accepts and completes paid vehicle repair");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,20.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,23,20,true);world.ents[tank].hp=100;powered();
    EID infantry=world.spawnUnit(0,UnitType::GI,10.5f,10.5f);
    world.presentationEvents.clear();issue({tank,tank,infantry},depot);
    auto result=world.presentationEvents.back().service;auto hash=world.checksum();consumePresentationEvents();
    check(result.accepted==1 && result.rejected==1 && result.reason==ServiceRejectReason::Incompatible
        && moveFeedbackPartial && hash==world.checksum(),"mixed command deduplicates IDs and reports actual partial acceptance without changing simulation on consumption");
    auto original=world.ents[tank];EID shop=world.spawnBuilding(0,BldType::MachineShop,35,20,true);
    auto rejected=world.orderService({tank},shop);
    check(rejected.reason==ServiceRejectReason::Unsupported && world.ents[tank].state==original.state
        && world.ents[tank].target==original.target && world.ents[tank].path==original.path,
        "passive machine shop rejects docking and preserves the existing service order");
    world.ents[depot].player=1;rejected=world.orderService({tank},depot);
    check(rejected.reason==ServiceRejectReason::WrongOwner,"enemy facilities reject repair commands");
    world.update();
    check(world.ents[tank].state==UState::Idle && world.ents[tank].target==INVALID_EID && world.ents[tank].path.empty(),
        "facility ownership change ends service and clears its route");
    world.ents[depot].player=0;powered();world.ents[tank].deployed=true;
    rejected=world.orderService({tank},depot);check(rejected.reason==ServiceRejectReason::Deployed,"deployed vehicle cannot start service");
    world.ents[tank].deployed=false;world.ents[tank].hp=unitDef(UnitType::Grizzly).hp;
    rejected=world.orderService({tank},depot);check(rejected.reason==ServiceRejectReason::FullHealth,"healthy vehicle without parasite reports no repair needed");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,29.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);world.ents[tank].hp=100;powered();
    world.players[0].money=0;issue({tank},depot);world.presentationEvents.clear();
    for(int frame=0;frame<30;++frame) world.update();
    bool paused=world.ents[tank].hp==100 && world.ents[tank].state==UState::Servicing;
    int notices=0;for(const auto& event:world.presentationEvents) notices+=event.kind==PresentationKind::ServiceOrder && event.serviceStatus==ServiceStatus::WaitingMoney;
    check(paused && notices==1,"empty treasury pauses docking with one waiting notice instead of repeated spam");
    world.players[0].money=1000;world.players[0].powerMade=0;world.players[0].powerUsed=25;world.presentationEvents.clear();
    for(int frame=0;frame<30;++frame) world.update();
    notices=0;for(const auto& event:world.presentationEvents) notices+=event.kind==PresentationKind::ServiceOrder && event.serviceStatus==ServiceStatus::WaitingPower;
    check(world.ents[tank].hp==100 && world.players[0].money==1000 && notices==1,"low power pauses HP gain and charges with one waiting notice");
    powered();world.update();
    FILE* file=tmpfile();World restored;bool replay=file && world.saveGame(file);
    if(replay) { rewind(file);replay=restored.loadGame(file); }
    if(file) fclose(file);
    for(int frame=0;frame<400 && replay;++frame) { world.update();restored.update();replay=world.checksum()==restored.checksum(); }
    consumePresentationEvents();
    check(replay && world.ents[tank].hp==unitDef(UnitType::Grizzly).hp && world.ents[tank].state==UState::Idle
        && world.ents[tank].target==INVALID_EID && world.players[0].money==1000-6*((unitDef(UnitType::Grizzly).hp-100+11)/12)
        && msg==TR(S::MsgServiceDone),
        "power and funds recovery completes repair at the retained rate and matches 400 saved-resume ticks");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,29.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);powered();
    EID drone=world.spawnUnit(1,UnitType::TerrorDrone,29.5f,20.5f);
    world.ents[drone].parasiting=true;world.ents[drone].parasiteHost=tank;world.ents[tank].parasite=drone;
    world.unitOccSync(drone);world.players[0].money=0;issue({tank},depot);world.update();
    check(!world.valid(drone) && world.ents[tank].parasite==INVALID_EID && world.ents[tank].state==UState::Idle
        && world.players[0].money==0,"powered docking clears a parasite without charging or consuming the host");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);world.ents[tank].hp=100;powered();issue({tank},depot);
    world.kill(depot,false);EID replacement=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);
    check(replacement==depot && world.ents[tank].state==UState::Idle && world.ents[tank].target==INVALID_EID
        && world.ents[tank].path.empty(),"destroyed facility ends service before the entity slot can be reused");
    powered();issue({tank},depot);world.orderStop({tank});world.presentationEvents.clear();world.update();
    check(world.ents[tank].state==UState::Idle && world.ents[tank].target==INVALID_EID && world.presentationEvents.empty(),
        "stop cancels service without a stale failure or completion event");
    issue({tank},depot);for(int y=0;y<64;++y) world.map.at(20,y).terrain=Terrain::Water;
    for(int frame=0;frame<90;++frame) world.update();
    auto requests=world.repathRequests;
    for(int frame=0;frame<300;++frame) world.update();
    check(world.ents[tank].state==UState::Idle && world.ents[tank].target==INVALID_EID && world.repathRequests==requests,
        "route closure terminates unreachable service instead of requesting paths forever");
    reset();infantry=world.spawnUnit(0,UnitType::GI,10.5f,20.5f);tank=world.spawnUnit(0,UnitType::Grizzly,12.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,25,20,true);world.ents[tank].hp=100;powered();
    camX=-500;camY=120;sideMode=0;placing=false;dragging=false;sel={infantry};
    auto anchor=bldScreenPos(world.ents[depot]);updateHoverCursor((int)anchor.x,(int)anchor.y-20);
    hash=world.checksum();issueSmartOrder((int)anchor.x,(int)anchor.y-20);consumePresentationEvents();
    std::printf("SERVICE_UI invalid cursor=%d expected=%d hash=%d msg=%s anchor=%.1f,%.1f pick=%d\n",(int)cursorKind,(int)CursorKind::NoMove,world.checksum()==hash,msg.c_str(),anchor.x,anchor.y,pickBuilding((int)anchor.x,(int)anchor.y-20));
    check(cursorKind==CursorKind::NoMove && world.checksum()==hash && msg==TR(S::MsgServiceIncompatible),
        "invalid right-click service agrees with hover cursor and preserves simulation");
    sel={tank,infantry};updateHoverCursor((int)anchor.x,(int)anchor.y-20);
    issueSmartOrder((int)anchor.x,(int)anchor.y-20);consumePresentationEvents();
    std::printf("SERVICE_UI mixed cursor=%d expected=%d partial=%d state=%d msg=%s\n",(int)cursorKind,(int)CursorKind::Repair,moveFeedbackPartial,(int)world.ents[tank].state,msg.c_str());
    check(cursorKind==CursorKind::Repair && moveFeedbackPartial && world.ents[tank].state==UState::Servicing,
        "mixed right-click service agrees with hover cursor and actual accepted units");
    for(int y=0;y<64;++y) world.map.at(20,y).terrain=Terrain::Water;
    for(int frame=0;frame<40;++frame) world.update();consumePresentationEvents();
    check(moveFeedbackRejected && msg==TR(S::MsgServiceUnreachable),"later route closure produces an authoritative failure notice");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);world.ents[tank].hp=100;powered();
    issue({tank},depot);world.presentationEvents.clear();world.kill(depot,false);world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);
    consumePresentationEvents();check(moveFeedbackRejected && msg==TR(S::MsgServiceInvalid) && moveFeedbackTimer==0,
        "destroyed service target reports once without marking a replacement facility");
    reset();depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);powered();
    std::vector<EID> group;
    for(int index=0;index<12;++index) {
        EID unit=world.spawnUnit(0,UnitType::Grizzly,8.5f+2*(index%4),18.5f+2*(index/4));
        world.ents[unit].hp=unitDef(UnitType::Grizzly).hp-24;group.push_back(unit);
    }
    world.players[0].money=1000;world.orderService(group,depot);bool budget=true,legal=true;
    for(int frame=0;frame<2400;++frame) {
        world.update();budget &= world.repathsThisTick<=8;
        for(size_t a=0;a<group.size();++a) for(size_t b=0;b<a;++b)
            legal &= (int)world.ents[group[a]].x!=(int)world.ents[group[b]].x || (int)world.ents[group[a]].y!=(int)world.ents[group[b]].y;
    }
    int finished=0;for(EID unit:group) finished+=world.ents[unit].hp==unitDef(UnitType::Grizzly).hp && world.ents[unit].state!=UState::Servicing;
    check(finished==12 && legal && budget && world.players[0].money==856,
        "twelve vehicles share a depot, finish paid repairs and yield without overlap or exceeding the replan budget");
    std::printf("SERVICE_GROUP repaired=%d legal=%d budget=%d money=%lld\n",finished,legal,budget,(long long)world.players[0].money);
#ifdef RA2_TESTING
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,20.5f);
    depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);world.ents[tank].hp=100;powered();world.orderService({tank},depot);
    // v21 used an ordinary Moving + depot target. Generate it with a frozen writer.
    world.ents[tank].state=UState::Moving;file=tmpfile();bool migrated=file && writeLegacyV21(world,file);
    if(migrated) { rewind(file);migrated=restored.loadGame(file); }
    if(file) fclose(file);
    world.ents[tank].state=UState::Servicing;
    migrated &= restored.ents[tank].state==UState::Servicing && world.checksum()==restored.checksum();
    for(int frame=0;frame<600 && migrated;++frame) { world.update();restored.update();migrated=world.checksum()==restored.checksum(); }
    check(migrated,"frozen v21 moving-to-depot snapshot explicitly converts and matches 600 continued ticks");
    reset();infantry=world.spawnUnit(0,UnitType::GI,10.5f,20.5f);depot=world.spawnBuilding(0,BldType::ServiceDepot,30,20,true);
    world.ents[infantry].state=UState::Servicing;world.ents[infantry].target=depot;
    file=tmpfile();bool invalid=file && world.saveGame(file);
    if(invalid) { rewind(file);invalid=!restored.loadGame(file); }
    if(file) fclose(file);
    check(invalid,"current reader rejects a servicing state on an incompatible infantry entity");
#endif
    return failures;
}
