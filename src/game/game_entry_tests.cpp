#include "game_regression.h"
#include "game/lang.h"
#include <cstdio>

int GameRegression::entryRegression() {
    int failures=0;localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;camZoom=1;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("ENTRY %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;world.presentationEvents.clear();
    };
    reset();EID actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);
    EID target=world.spawnUnit(1,UnitType::IFV,25.5f,10.5f);
    World::Cmd cmd{};cmd.type=World::Cmd::Move;cmd.ids={actor};cmd.x=5;cmd.y=20;world.applyCmd(0,cmd);
    cmd.a=1;cmd.y=25;world.applyCmd(0,cmd);
    auto before=world.checksum();cmd.type=World::Cmd::Board;cmd.a=target;world.applyCmd(0,cmd);
    check(before==world.checksum() && world.ents[actor].wps.size()==1,"enemy transport rejection preserves previous route and queue");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    world.ents[target].cargo.push_back({UnitType::Engineer,100,0,0,0});
    before=world.checksum();cmd.ids={actor};cmd.a=target;world.applyCmd(0,cmd);
    check(before==world.checksum(),"already full transport rejects before changing orders");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnBuilding(0,BldType::BattleBunker,30,10,true);
    for(int y=0;y<64;++y) for(int x=12;x<=20;++x) world.map.at(x,y).terrain=Terrain::Water;
    before=world.checksum();cmd.type=World::Cmd::Garrison;cmd.ids={actor};cmd.a=target;world.applyCmd(0,cmd);
    check(before==world.checksum(),"unreachable garrison rejects before committing a boarding state");
    auto issue=[&](const std::vector<EID>& ids,EID destination,EntryKind kind) {
        World::Cmd order{};order.type=kind==EntryKind::Board?World::Cmd::Board:World::Cmd::Garrison;
        order.ids=ids;order.a=destination;world.applyCmd(0,order);
    };
    auto rejects=[&](EID unit,EID destination,EntryKind kind,EntryRejectReason reason,S text) {
        world.presentationEvents.clear();auto hash=world.checksum();issue({unit},destination,kind);
        bool result=!world.presentationEvents.empty() && world.presentationEvents.back().kind==PresentationKind::EntryOrder
            && world.presentationEvents.back().entry.reason==reason && world.presentationEvents.back().entry.rejected==1;
        consumePresentationEvents();return result && hash==world.checksum() && msg==TR(text) && moveFeedbackRejected;
    };
    check(rejects(actor,target,EntryKind::Garrison,EntryRejectReason::Unreachable,S::MsgEntryUnreachable),"unreachable entrance explains rejection without changing simulation");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::AircraftCarrier,25.5f,10.5f);
    check(rejects(actor,target,EntryKind::Board,EntryRejectReason::Unsupported,S::MsgEntryUnsupported),"carrier aircraft storage cannot receive infantry passengers");
    check(rejects(actor,99999,EntryKind::Board,EntryRejectReason::InvalidTarget,S::MsgEntryInvalid) && moveFeedbackTimer==0,"invalid entry ID has no fabricated target marker");
    target=world.spawnBuilding(0,BldType::CivHouse,25,10,true);
    EID engineer=world.spawnUnit(0,UnitType::Engineer,6.5f,10.5f);
    check(rejects(engineer,target,EntryKind::Garrison,EntryRejectReason::Incompatible,S::MsgEntryIncompatible),"civil house rejects incompatible infantry");
    world.orderRadDeploy({actor});
    check(rejects(actor,target,EntryKind::Garrison,EntryRejectReason::Deployed,S::MsgEntryDeployed),"deployed infantry must undeploy before entering");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    EID tank=world.spawnUnit(0,UnitType::Grizzly,7.5f,10.5f);
    world.ents[actor].hp=67;world.ents[actor].vetRank=1;world.ents[actor].veterancyValue=400;world.ents[actor].kills=2;
    issue({actor,tank},target,EntryKind::Board);
    bool partial=world.presentationEvents.back().entry.accepted==1 && world.presentationEvents.back().entry.rejected==1;
    before=world.checksum();consumePresentationEvents();
    check(partial && moveFeedbackPartial && before==world.checksum()
        && msg==TextFormat(TR(S::MsgEntryPartial),1,TR(S::MsgEntryIncompatible)),"mixed entry reports actual partial acceptance without mutating simulation");
    world.ents[actor].feedbackEntryKind=EntryKind::Garrison;world.ents[actor].feedbackEntryTarget=INVALID_EID;
    check(before==world.checksum(),"entry feedback kind and provenance are excluded from simulation hash");
    FILE* file=tmpfile();World restored;bool replay=file && world.saveGame(file);
    if(replay) { rewind(file);replay=restored.loadGame(file); }
    if(file) fclose(file);
    for(int frame=0;frame<700;++frame) {
        world.update();if(replay) { restored.update();replay=world.checksum()==restored.checksum(); }
    }
    const auto& cargo=world.ents[target].cargo;
    check(replay && !world.valid(actor) && cargo.size()==1 && cargo[0].hp==67 && cargo[0].vetRank==1 && cargo[0].kills==2 && cargo[0].veterancyValue==400,
        "successful boarding preserves passenger stats through saved approach and 700 tick replay");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);EID second=world.spawnUnit(0,UnitType::GI,6.5f,10.5f);
    target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);issue({actor,second},target,EntryKind::Board);world.presentationEvents.clear();
    bool budget=true;for(int frame=0;frame<600;++frame) { world.update();budget &= world.repathsThisTick<=8; }
    int reports=0;for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::EntryOrder && event.entry.reason==EntryRejectReason::Full) ++reports;
    check(budget && reports==1 && world.ents[target].cargo.size()==1 && world.valid(actor)!=world.valid(second),"capacity race boards exactly one passenger and reports one full-target failure");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    issue({actor},target,EntryKind::Board);world.presentationEvents.clear();world.ents[target].cargo.push_back({UnitType::Engineer,100,0,0,0});world.update();consumePresentationEvents();
    check(world.valid(actor) && world.ents[actor].target==INVALID_EID && world.ents[actor].path.empty() && msg==TR(S::MsgEntryFull),"target filled during approach releases passenger with explicit reason");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnBuilding(0,BldType::BattleBunker,30,10,true);
    issue({actor},target,EntryKind::Garrison);world.presentationEvents.clear();world.ents[target].player=1;world.update();consumePresentationEvents();
    check(world.valid(actor) && world.ents[actor].target==INVALID_EID && msg==TR(S::MsgEntryOwner),"garrison ownership change ends the original entry");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnBuilding(0,BldType::BattleBunker,30,10,true);
    issue({actor},target,EntryKind::Garrison);world.presentationEvents.clear();
    for(int y=0;y<64;++y) for(int x=12;x<=20;++x) world.map.at(x,y).terrain=Terrain::Water;
    for(int frame=0;frame<150;++frame) { world.update();budget &= world.repathsThisTick<=8; }
    reports=0;for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::EntryOrder && event.entry.reason==EntryRejectReason::Unreachable) ++reports;
    auto paths=world.repathRequests;for(int frame=0;frame<300;++frame) world.update();
    check(budget && reports==1 && paths==world.repathRequests && world.ents[actor].state==UState::Idle && world.ents[actor].path.empty(),"sealed entry ends once with bounded repaths and no repeated requests");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    for(int i=0;i<3;++i) { EID blocker=world.spawnUnit(0,UnitType::GI,24.5f,10.5f);world.orderRadDeploy({blocker}); }
    issue({actor},target,EntryKind::Board);for(int frame=0;frame<600 && world.valid(actor);++frame) world.update();
    check(!world.valid(actor) && world.ents[target].cargo.size()==1,"occupied near entrance uses another reachable approach");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy)
        for(int i=0;i<3;++i) { EID blocker=world.spawnUnit(0,UnitType::GI,25.5f+dx,10.5f+dy);world.orderRadDeploy({blocker}); }
    check(rejects(actor,target,EntryKind::Board,EntryRejectReason::Unreachable,S::MsgEntryUnreachable),"all entry cells permanently occupied reject instead of repeating a blocked goal");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    issue({actor},target,EntryKind::Board);world.presentationEvents.clear();
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy)
        for(int i=0;i<3;++i) { EID blocker=world.spawnUnit(0,UnitType::GI,25.5f+dx,10.5f+dy);world.orderRadDeploy({blocker}); }
    for(int frame=0;frame<150;++frame) world.update();
    reports=0;for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::EntryOrder && event.entry.reason==EntryRejectReason::Unreachable) ++reports;
    paths=world.repathRequests;for(int frame=0;frame<300;++frame) world.update();
    check(reports==1 && paths==world.repathRequests && world.ents[actor].state==UState::Idle,"new permanent entry blockade terminates once without repeated repaths");
    reset();actor=world.spawnUnit(0,UnitType::GI,21.5f,10.5f);target=world.spawnUnit(0,UnitType::AmphTransport,25.5f,10.5f);
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    for(int y=0;y<64;++y) for(int x=0;x<24;++x) world.map.at(x,y).terrain=Terrain::Clear;
    check(rejects(actor,target,EntryKind::Board,EntryRejectReason::Unreachable,S::MsgEntryUnreachable),"offshore transport without an in-range shore rejects instead of looping");
    world.ents[target].x=24.5f;world.unitOccSync(target);issue({actor},target,EntryKind::Board);
    for(int frame=0;frame<200 && world.valid(actor);++frame) world.update();
    check(!world.valid(actor) && world.ents[target].cargo.size()==1,"reachable shoreline boards infantry without walking into water");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    issue({actor},target,EntryKind::Board);world.presentationEvents.clear();world.kill(target);EID replacement=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);world.update();consumePresentationEvents();
    check(replacement==target && world.valid(actor) && world.ents[actor].target==INVALID_EID && msg==TR(S::MsgEntryInvalid) && moveFeedbackTimer==0,
        "retired entry target reports without binding marker to replacement");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    issue({actor},target,EntryKind::Board);for(int frame=0;frame<10;++frame) world.update();
    world.ents[target].x=35.5f;world.unitOccSync(target);
    budget=true;for(int frame=0;frame<700 && world.valid(actor);++frame) { world.update();budget &= world.repathsThisTick<=8; }
    check(budget && !world.valid(actor) && world.ents[target].cargo.size()==1,"moving transport is followed with budgeted entry replans");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnBuilding(0,BldType::BioReactor,25,10,true);
    world.ents[actor].hp=67;world.ents[actor].vetRank=1;world.ents[actor].veterancyValue=400;world.ents[actor].kills=2;
    const int basePower=world.players[0].powerMade;
    issue({actor},target,EntryKind::Garrison);for(int frame=0;frame<700 && world.valid(actor);++frame) world.update();
    bool garrisoned=!world.valid(actor) && world.ents[target].garrison.size()==1 && world.ents[target].garrison[0].hp==67
        && world.players[0].powerMade==basePower+g_gameRules.bioReactorPowerPerOccupant;
    world.orderUngarrison({target});bool restoredStats=false;
    for(const auto& unit:world.ents) restoredStats |= unit.alive && !unit.isBuilding && unit.utype==UnitType::GI && unit.hp==67 && unit.vetRank==1 && unit.kills==2 && unit.veterancyValue==400;
    check(garrisoned && restoredStats && world.ents[target].garrison.empty(),"bio-reactor entry and evacuation preserve stats and update occupant power");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnBuilding(0,BldType::TankBunker,25,10,true);
    camX=-500;camY=0;sel={actor};sideMode=0;placing=false;dragging=false;
    auto anchor=bldScreenPos(world.ents[target]);
    updateHoverCursor((int)anchor.x,(int)anchor.y-20);before=world.checksum();issueSmartOrder((int)anchor.x,(int)anchor.y-20);consumePresentationEvents();
    check(cursorKind==CursorKind::NoMove && before==world.checksum() && msg==TR(S::MsgEntryIncompatible),"invalid smart garrison matches cursor and gives an actual rejection");
    tank=world.spawnUnit(0,UnitType::Grizzly,7.5f,10.5f);sel={tank};
    updateHoverCursor((int)anchor.x,(int)anchor.y-20);issueSmartOrder((int)anchor.x,(int)anchor.y-20);
    bool tankEntry=cursorKind==CursorKind::Enter && world.ents[tank].state==UState::Boarding;
    for(int frame=0;frame<700 && world.valid(tank);++frame) world.update();
    check(tankEntry && !world.valid(tank) && world.ents[target].garrison.size()==1,"tank-bunker smart order accepts eligible vehicle and completes entry");
    reset();actor=world.spawnUnit(0,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(0,UnitType::IFV,25.5f,10.5f);
    world.ents[target].cargo.push_back({UnitType::Engineer,100,0,0,0});sel={actor};camX=-500;camY=0;
    auto rectangle=unitScreenRect(world.ents[target]);int mx=(int)(rectangle.x+rectangle.width/2),my=(int)(rectangle.y+rectangle.height/2);
    updateHoverCursor(mx,my);before=world.checksum();issueSmartOrder(mx,my);consumePresentationEvents();
    check(cursorKind==CursorKind::NoMove && before==world.checksum() && msg==TR(S::MsgEntryFull),"full transport smart order rejects consistently with hover cursor");
    world.ents[target].cargo.clear();tank=world.spawnUnit(0,UnitType::Grizzly,7.5f,10.5f);sel={actor,tank};
    updateHoverCursor(mx,my);issueSmartOrder(mx,my);consumePresentationEvents();
    check(cursorKind==CursorKind::Enter && moveFeedbackPartial && world.ents[actor].state==UState::Boarding,
        "mixed smart boarding reports skipped incompatible units");
    World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={actor};world.applyCmd(0,stop);world.presentationEvents.clear();world.kill(target);world.update();
    bool quiet=true;for(const auto& event:world.presentationEvents) quiet &= event.kind!=PresentationKind::EntryOrder;
    check(quiet,"stopped entry does not report a later target retirement");
    reset();actor=world.spawnUnit(1,UnitType::GI,5.5f,10.5f);target=world.spawnUnit(1,UnitType::IFV,25.5f,10.5f);
    std::string previous=msg;World::Cmd foreign{};foreign.type=World::Cmd::Board;foreign.ids={actor};foreign.a=target;world.applyCmd(1,foreign);
    before=world.checksum();consumePresentationEvents();world.applyCmd(0,foreign);
    check(msg==previous && before==world.checksum() && world.presentationEvents.empty(),"entry presentation filters other players and foreign selections");
    return failures;
}
