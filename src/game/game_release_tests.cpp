#include "game_regression.h"
#include "game/lang.h"
#include <cstdio>

int GameRegression::releaseRegression() {
    int failures=0;localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("RELEASE %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;world.presentationEvents.clear();
    };
    reset();EID transport=world.spawnUnit(0,UnitType::IFV,20.5f,20.5f);
    world.ents[transport].cargo.push_back({UnitType::GI,67,2,400,1});
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) world.spawnBuilding(0,BldType::Wall,20+dx,20+dy,true);
    World::Cmd cmd{};cmd.type=World::Cmd::Unload;cmd.ids={transport};world.applyCmd(0,cmd);
    check(world.ents[transport].cargo.size()==1,"transport cannot unload through a complete wall ring");
    reset();EID bunker=world.spawnBuilding(0,BldType::BattleBunker,20,20,true);
    world.ents[bunker].garrison.push_back({UnitType::GI,67,2,400,1});
    const auto& footprint=bldDef(BldType::BattleBunker);
    for(int y=19;y<=20+footprint.h;++y) for(int x=19;x<=20+footprint.w;++x)
        if(x==19 || y==19 || x==20+footprint.w || y==20+footprint.h) world.spawnBuilding(0,BldType::Wall,x,y,true);
    cmd.type=World::Cmd::Ungarrison;cmd.ids={bunker};world.applyCmd(0,cmd);
    check(world.ents[bunker].garrison.size()==1,"garrison cannot evacuate across a complete wall perimeter");
    auto issue=[&](const std::vector<EID>& ids,ReleaseKind kind) {
        World::Cmd order{};order.type=kind==ReleaseKind::Unload?World::Cmd::Unload:World::Cmd::Ungarrison;order.ids=ids;world.applyCmd(0,order);
    };
    world.presentationEvents.clear();auto hash=world.checksum();issue({bunker},ReleaseKind::Ungarrison);
    bool blocked=world.presentationEvents.back().release.remaining==1 && world.presentationEvents.back().release.released==0;
    consumePresentationEvents();
    check(blocked && hash==world.checksum() && moveFeedbackRejected && msg==TextFormat(TR(S::MsgReleaseBlocked),1),"blocked evacuation preserves world and explains retained occupant");
    reset();transport=world.spawnUnit(0,UnitType::IFV,20.5f,20.5f);
    issue({transport},ReleaseKind::Unload);consumePresentationEvents();
    check(moveFeedbackRejected && msg==TR(S::MsgReleaseEmpty),"empty transport reports no passengers");
    EID carrier=world.spawnUnit(0,UnitType::AircraftCarrier,30.5f,30.5f);
    issue({carrier},ReleaseKind::Unload);consumePresentationEvents();
    check(world.ents[carrier].cargo.size()==3 && msg==TR(S::MsgReleaseUnsupported),"carrier cannot unload its aircraft storage as passengers");
    reset();transport=world.spawnUnit(0,UnitType::AmphTransport,20.5f,20.5f);
    world.ents[transport].cargo={{UnitType::GI,61,1,200,1},{UnitType::GI,62,2,300,1},{UnitType::GI,63,3,400,1}};
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    world.map.at(20,20).terrain=Terrain::Clear;world.map.at(21,20).terrain=Terrain::Clear;
    issue({transport},ReleaseKind::Unload);
    auto result=world.presentationEvents.back().release;
    hash=world.checksum();consumePresentationEvents();
    check(result.released==1 && result.remaining==2 && moveFeedbackPartial && hash==world.checksum()
        && world.ents[transport].cargo.size()==2 && world.ents[transport].cargo[0].hp==61 && world.ents[transport].cargo[1].hp==62,
        "partial unload preserves exact remaining passenger order and reports actual counts");
    FILE* file=tmpfile();World restored;bool replay=file && world.saveGame(file);
    if(replay) { rewind(file);replay=restored.loadGame(file); }
    if(file) fclose(file);
    world.map.at(20,21).terrain=Terrain::Clear;world.map.at(19,20).terrain=Terrain::Clear;
    if(replay) { restored.map.at(20,21).terrain=Terrain::Clear;restored.map.at(19,20).terrain=Terrain::Clear; }
    World::Cmd resume{};resume.type=World::Cmd::Unload;resume.ids={transport};world.applyCmd(0,resume);
    if(replay) restored.applyCmd(0,resume);
    for(int frame=0;frame<300;++frame) {
        world.update();if(replay) { restored.update();replay=world.checksum()==restored.checksum(); }
    }
    int passengers=0;bool stats=true;
    for(const auto& unit:world.ents) if(unit.alive && !unit.isBuilding && unit.utype==UnitType::GI) {
        ++passengers;stats &= unit.hp>=61 && unit.hp<=63 && unit.vetRank==1 && unit.kills==unit.hp-60 && unit.veterancyValue==(unit.hp-59)*100;
    }
    check(replay && passengers==3 && stats && world.ents[transport].cargo.empty(),"unblocked retry releases each passenger once with stats and 300 tick save-resume equality");
    reset();bunker=world.spawnBuilding(0,BldType::CivHouse,20,20,true);world.ents[bunker].garrison={{UnitType::GI,67,2,400,1},{UnitType::GI,68,3,500,1}};
    const auto& house=bldDef(BldType::CivHouse);int ex=20+house.w,ey=20;
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    for(int y=20;y<20+house.h;++y) for(int x=20;x<20+house.w;++x) world.map.at(x,y).terrain=Terrain::Clear;
    world.map.at(ex,ey).terrain=Terrain::Clear;
    issue({bunker},ReleaseKind::Ungarrison);result=world.presentationEvents.back().release;
    check(result.released==1 && result.remaining==1 && world.ents[bunker].player==0 && world.ents[bunker].garrison[0].hp==67,
        "partially evacuated civil house retains owner and unplaced occupant");
    world.map.at(ex,ey+1).terrain=Terrain::Clear;issue({bunker},ReleaseKind::Ungarrison);
    check(world.ents[bunker].garrison.empty() && world.ents[bunker].player==-1,"civil house returns neutral only after final occupant leaves");
    reset();bunker=world.spawnBuilding(0,BldType::BioReactor,20,20,true);world.ents[bunker].garrison={{UnitType::GI,67,2,400,1}};world.update();
    int oldPower=world.players[0].powerMade;issue({bunker},ReleaseKind::Ungarrison);
    check(world.ents[bunker].garrison.empty() && world.players[0].powerMade==oldPower-g_gameRules.bioReactorPowerPerOccupant,"bio-reactor output drops only for released occupants");
    reset();transport=world.spawnUnit(0,UnitType::IFV,20.5f,20.5f);world.ents[transport].cargo={{UnitType::GI,67,2,400,1}};
    issue({transport,transport},ReleaseKind::Unload);result=world.presentationEvents.back().release;
    check(result.released==1 && result.rejected==0,"duplicate source ID does not repeat release or invent empty-target failure");
    EID tank=world.spawnUnit(0,UnitType::Grizzly,30.5f,30.5f);world.ents[transport].cargo={{UnitType::GI,67,2,400,1}};
    issue({transport,tank},ReleaseKind::Unload);result=world.presentationEvents.back().release;consumePresentationEvents();
    check(result.released==1 && result.rejected==1 && moveFeedbackPartial,"mixed release selection reports supported and unsupported targets");
    reset();bunker=world.spawnBuilding(0,BldType::BattleBunker,20,20,true);
    sel.clear();selBuilding=bunker;sideMode=0;placing=false;paused=false;
    const int oldKey=keyBind[KA_Unload];keyBind[KA_Unload]=KEY_U;sim.active=true;sim.pos={-100,-100};sim.keysDown.clear();sim.keysPressed.clear();sim.keysPressed.insert(KEY_U);
    handleInput();keyBind[KA_Unload]=oldKey;sim.keysPressed.clear();sim.active=false;consumePresentationEvents();
    check(msg==TR(S::MsgReleaseEmpty),"unload hotkey on empty selected garrison produces authoritative feedback");
    reset();bunker=world.spawnBuilding(0,BldType::CivHouse,20,20,true);world.ents[bunker].garrison={{UnitType::GI,67,2,400,1},{UnitType::GI,68,3,500,1}};
    world.ents.shrink_to_fit();const size_t oldSize=world.ents.size();world.damage(bunker,bldDef(BldType::CivHouse).hp/2,1);
    check(world.valid(bunker) && world.ents[bunker].garrison.empty() && world.ents[bunker].player==-1 && world.ents.size()==oldSize+2,
        "damage-forced evacuation survives entity vector growth and leaves neutral damaged house");
    reset();EID prime=world.spawnUnit(0,UnitType::YuriPrime,20.5f,20.5f);bunker=world.spawnBuilding(1,BldType::BattleBunker,23,20,true);
    world.ents[bunker].garrison={{UnitType::Conscript,67,2,400,1}};World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={prime};attack.a=bunker;world.applyCmd(0,attack);
    world.ents.shrink_to_fit();for(int frame=0;frame<120 && world.ents[bunker].player!=0;++frame) world.update();
    check(world.valid(prime) && world.ents[bunker].player==0 && world.ents[bunker].garrison.empty(),"mind-control capture safely reacquires actor after evacuation growth");
    reset();transport=world.spawnUnit(0,UnitType::AmphTransport,20.5f,20.5f);
    for(int i=0;i<12;++i) world.ents[transport].cargo.push_back({UnitType::GI,67,2,400,1});
    issue({transport},ReleaseKind::Unload);result=world.presentationEvents.back().release;
    check(result.released==12 && result.remaining==0 && world.ents[transport].cargo.empty(),"open local paths release a full twelve-passenger transport");
    reset();transport=world.spawnUnit(0,UnitType::AmphTransport,20.5f,20.5f);world.ents[transport].cargo={{UnitType::GI,67,2,400,1},{UnitType::GI,68,3,500,1}};
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    world.map.at(22,20).terrain=Terrain::Clear;issue({transport},ReleaseKind::Unload);
    check(world.ents[transport].cargo.size()==2,"transport cannot jump a water gap to distant land");
    world.map.at(21,20).terrain=Terrain::Clear;issue({transport},ReleaseKind::Unload);
    check(world.ents[transport].cargo.empty(),"connected shore permits disembarkation past earlier infantry");
    reset();transport=world.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);world.ents[transport].cargo={{UnitType::GI,67,2,400,1}};
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    hash=world.checksum();issue({transport},ReleaseKind::Unload);consumePresentationEvents();
    check(world.ents[transport].cargo.size()==1 && hash==world.checksum() && feedbackLift==AIR_ALT && moveFeedbackRejected,
        "air transport without landing area retains cargo and places feedback at aircraft height");
    reset();transport=world.spawnUnit(1,UnitType::IFV,20.5f,20.5f);world.ents[transport].cargo={{UnitType::GI,67,2,400,1}};
    std::string previous=msg;cmd.type=World::Cmd::Unload;cmd.ids={transport};world.applyCmd(1,cmd);hash=world.checksum();consumePresentationEvents();world.applyCmd(0,cmd);
    check(previous==msg && hash==world.checksum() && world.presentationEvents.empty(),"release results filter other players and foreign selections");
    return failures;
}
