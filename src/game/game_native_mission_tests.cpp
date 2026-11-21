#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

int GameRegression::nativeMissionRegression(const char* outputDir) {
    const auto path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_MISSION SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0,serial=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_MISSION %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();world=World{};localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;netGame=false;
    if(!world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        || !world.loadNativeScene(path.c_str()) || !world.startNativeEvents()){check(false,"the original mission graph starts");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;gameOver=victory=false;};
    const auto trigger=[&](const char* name)->const NativeTriggerState* {
        const int definition=world.nativeTriggerIndex(name);for(const auto& t:world.nativeEvents.triggers)if(t.definition==definition)return &t;return nullptr;
    };
    const auto resume=[&](int ticks) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= world.checksum()==restored.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}
        return exact;
    };
    for(const char* previous:{"review_out/native-campaign/legacy-native40-active.sav","review_out/native-campaign/legacy-native40-missile.sav"}) {
        if(!FileExists(previous))continue;
        const bool loaded=loadGameFile(previous);
        check(loaded && world.nativeEvents.outcome==NativeMissionOutcome::Running && world.nativeEvents.outcomeAt==0
            && world.nativeEvents.outcomeTrigger==-1 && resume(20),"an actual World40 snapshot defaults the new terminal fields and resumes its existing events/flight exactly");
    }
    reset();world.players[0].defeated=true;logic();
    check(!gameOver && world.nativeMissionOutcome(0)==MatchOutcome::Running,
        "a generic skirmish defeat flag cannot finish an active authored mission before its terminal action");

    // Weapon/mission integration fixture: the authored opening is untouched;
    // extra counter-Tanyas are positioned only to isolate actual C4 commands.
    reset();while(world.tick<1650)world.update();std::vector<EID> ships;int tanyaType=world.nativeUnitIndex("TANY");
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==2
        && world.ents[id].nativeIdentity.definition->id=="DRED")ships.push_back(id);
    bool sabotaged=ships.size()==4 && !world.nativeEvents.inputLocked && world.nativeMissionOutcome(0)==MatchOutcome::Running;
    for(EID ship:ships) {
        const int x=(int)world.ents[ship].x,y=(int)world.ents[ship].y;EID hero=-1;
        for(auto offset:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const int nx=x+offset.x,ny=y+offset.y;
            if(world.map.surfacePassable(nx,ny,2,0,true,true) && world.unitAtCell(nx,ny)<0) {
                hero=world.spawnNativeActor(tanyaType,0,nx+.5f,ny+.5f,256,64,"counter:c4:"+std::to_string(serial++),"","Guard");break;
            }
        }
        sabotaged &= world.valid(hero);if(!world.valid(hero))continue;
        const auto weapon=world.effWeapon(world.ents[hero],&world.ents[ship]);
        sabotaged &= weapon.nativeSlot==1 && weapon.damage==2500;
        World::Cmd cmd{};cmd.type=World::Cmd::Attack;cmd.ids={hero};cmd.a=ship;world.applyCmd(0,cmd);
        sabotaged &= world.ents[hero].target==ship;
    }
    for(int i=0;i<30 && trigger("07B92F3C")->firings==0;++i)world.update();
    for(EID ship:ships)sabotaged &= !world.valid(ship);
    bool airborne=false;for(const auto& r:world.nativeRockets)airborne |= world.valid(r.actor) && world.ents[r.actor].player==2;
    sabotaged &= trigger("07B92F3C")->firings==1 && airborne && world.nativeMissionOutcome(0)==MatchOutcome::Running;
    check(sabotaged && resume(5),"real player C4 commands sink all four authored DREDs and complete Objective 1 while their airborne missiles do not block the house event");

    // Explicit mechanism fixture: remove authored Soviet structures, retaining
    // their army and every other house. This is not a campaign playthrough.
    reset();std::vector<EID> buildings;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==1 && world.ents[id].isBuilding
        && world.ents[id].nativeIdentity.definition->kind==0 && world.ents[id].nativeIdentity.definition->significant)buildings.push_back(id);
    check(buildings.size()>1 && !world.nativeHouseEliminated(14,10) && !world.nativeHouseEliminated(14,11),"the original Soviet base initially keeps the building/all elimination events false");
    if(buildings.empty())return failures+1;
    const EID last=buildings.back();for(EID id:buildings)if(id!=last)world.kill(id,false);
    logic();check(trigger("07506C5C") && trigger("07506C5C")->firings==0 && !gameOver,
        "the original Enable Victory trigger waits while one counted Soviet structure remains");
    world.kill(last,false);logic();const auto enabled=trigger("075F22EC");
    const uint64_t deadline=enabled?enabled->timerStart+10*LOGIC_FPS:0;
    check(trigger("07506C5C")->firings==1 && enabled && enabled->enabled && !gameOver && !world.nativeHouseEliminated(14,9)
        && world.nativeEvents.outcome==NativeMissionOutcome::Running,
        "the real building-elimination trigger enables the author's ten-second victory timer while Soviet troops still exist");
    check(resume(100),"the enabled victory timer resumes without an earlier result or tick shift");
    while(world.tick+1<deadline && !gameOver)logic();
    check(!gameOver && world.tick+1==deadline,"the application remains in the mission through the last tick before authored victory");
    logic();
    check(gameOver && victory && world.nativeMissionOutcome(0)==MatchOutcome::Victory && world.nativeMissionOutcome(1)==MatchOutcome::Running
        && world.nativeEvents.outcomeAt==deadline && world.nativeEvents.outcomeTrigger==world.nativeTriggerIndex("075F22EC")
        && trigger("075F22EC")->firings==1 && world.validateNativeEventState(),
        "the original Winner action ends the local mission at its source deadline with saved trigger provenance");
    std::printf("NATIVE_MISSION VICTORY tick=%llu trigger=%d\n",(unsigned long long)world.tick,world.nativeEvents.outcomeTrigger);
    World win=world;win.map.bldOccRef=&win.bldOcc;
    const auto application=(std::filesystem::path(outputDir)/"authored-victory-fixture.sav").string();
    bool saved=saveGameFile(application.c_str());gameOver=victory=false;
    check(saved && loadGameFile(application.c_str()) && gameOver && victory && world.checksum()==win.checksum() && resume(5),
        "application v10 / World41 restores the authored terminal result and the visible victory state");
    {
        const auto state=world.nativeEvents;
        world.nativeEvents.outcome=(NativeMissionOutcome)3;const bool unknown=!world.validateNativeEventState();world.nativeEvents=state;
        world.nativeEvents.outcomeAt=world.tick+1;const bool future=!world.validateNativeEventState();world.nativeEvents=state;
        world.nativeEvents.outcomeTrigger=world.nativeTriggerIndex("07A9578C");const bool source=!world.validateNativeEventState();world.nativeEvents=state;
        world.nativeEvents.outcome=NativeMissionOutcome::Running;const bool stray=!world.validateNativeEventState();world.nativeEvents=state;
        check(unknown && future && source && stray,"unknown outcomes, future deadlines, unauthored results and stale running-state provenance are rejected");
        FILE* file=tmpfile();std::vector<uint8_t> bytes;bool rejected=file && world.saveGame(file);
        if(rejected){rewind(file);for(int c;(c=fgetc(file))!=EOF;)bytes.push_back((uint8_t)c);}if(file)fclose(file);
        uint8_t marker[13]{};marker[0]=(uint8_t)state.outcome;memcpy(marker+1,&state.outcomeAt,8);memcpy(marker+9,&state.outcomeTrigger,4);
        auto at=std::search(bytes.begin(),bytes.end(),std::begin(marker),std::end(marker));rejected &= at!=bytes.end();
        if(at!=bytes.end()) {
            *at=3;file=tmpfile();World malformed;rejected &= file && fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
            if(file){rewind(file);rejected &= !malformed.loadGame(file);fclose(file);}
        }
        check(rejected,"a malformed serialized terminal result is rejected by the World reader");
    }
    reset();EID tanya=-1;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition->id=="TANY"){tanya=id;break;}
    check(world.valid(tanya),"the original tagged Tanya is present");if(!world.valid(tanya))return failures+1;
    const uint64_t killed=world.tick;world.damage(tanya,10000,1);logic();
    check(trigger("08B9B62C")->firings==1 && !gameOver,"actual tagged Tanya death starts the original failure chain without ending the mission early");
    check(resume(40),"the pending Tanya-death chain survives a mid-delay snapshot");
    const uint64_t failureAt=killed+5*LOGIC_FPS;
    while(world.tick+1<failureAt && !gameOver)logic();
    check(!gameOver && world.tick+1==failureAt,"Tanya's three-second then two-second failure delays both remain effective");logic();
    check(gameOver && !victory && world.nativeMissionOutcome(0)==MatchOutcome::Defeat && world.nativeEvents.outcomeAt==failureAt
        && world.nativeEvents.outcomeTrigger==world.nativeTriggerIndex("08B9BC5C") && resume(5),
        "the original Loser action reaches a durable defeat at the authored five-second deadline");
    std::printf("NATIVE_MISSION DEFEAT tick=%llu trigger=%d\n",(unsigned long long)world.nativeEvents.outcomeAt,world.nativeEvents.outcomeTrigger);
    World lose=world;lose.map.bldOccRef=&lose.bldOcc;

    // Counterexample arena: category/count tests deliberately disable the
    // unrelated original triggers. No changes are made to the source graph.
    reset();for(auto& t:world.nativeEvents.triggers)t.enabled=false;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
    Vec2i origin{-1,-1};
    for(int y=6;y<world.map.h-6 && origin.x<0;++y)for(int x=6;x<world.map.w-6 && origin.x<0;++x) {
        bool clear=true;for(int dy=-4;dy<=4 && clear;++dy)for(int dx=-4;dx<=4 && clear;++dx)clear=world.map.surfacePassable(x+dx,y+dy,0,0,true,true);
        if(clear)origin={x,y};
    }
    const auto spawn=[&](const char* name,int player,int x=0,int y=0) {
        int type=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name){type=i;break;}
        return world.spawnNativeActor(type,player,origin.x+x+.5f,origin.y+y+.5f,256,64,"counter:mission:"+std::to_string(serial++),"","Guard");
    };
    check(origin.x>=0 && world.nativeHouseEliminated(16,9) && world.nativeHouseEliminated(16,10) && world.nativeHouseEliminated(16,11)
        && world.nativeHouseEliminated(1023,11),"empty or absent source houses satisfy their polled elimination events immediately");
    if(origin.x<0)return failures+1;
    const EID air=world.spawnUnit(3,UnitType::Nighthawk,origin.x+.5f,origin.y+.5f);
    check(world.nativeHouseEliminated(16,9) && world.nativeHouseEliminated(16,11),"AircraftTypes do not keep the source unit/all elimination conditions false");
    const EID soldier=spawn("E2",3,1,0);bool boarded=world.orderBoard({soldier},air).accepted==1;
    for(int i=0;i<240 && world.valid(soldier);++i)world.update();
    check(boarded && !world.valid(soldier) && !world.ents[air].cargo.empty() && !world.nativeHouseEliminated(16,9)
        && !world.nativeHouseEliminated(16,11) && resume(1),"a genuinely boarded infantry passenger still counts while its aircraft transport is excluded");
    world.kill(air,false);
    check(world.nativeHouseEliminated(16,9) && world.nativeHouseEliminated(16,11),"destroying the occupied transport removes the contained infantry from ownership counts");
    int insignificant=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(!world.nativeTypes[i]->isBuilding && !world.nativeTypes[i]->significant
        && !world.nativeTypes[i]->unit.isAir()){insignificant=i;break;}
    if(insignificant>=0) {
        const auto actor=spawn(world.nativeTypes[insignificant]->id.c_str(),3);
        check(world.valid(actor) && world.nativeHouseEliminated(16,9) && world.nativeHouseEliminated(16,11),"an actual source Insignificant object does not block elimination");world.kill(actor,false);
    }
    const EID yard=spawn("GACNST",3);check(world.valid(yard) && world.nativeHouseEliminated(16,9) && !world.nativeHouseEliminated(16,10)
        && !world.nativeHouseEliminated(16,11),"an undeployable construction yard remains a counted building rather than a vehicle");world.kill(yard,false);
    auto& p=world.players[3];p.unitQueue[0].push_back((int)UnitType::Conscript);
    check(world.nativeHouseEliminated(16,11),"unstarted queue entries do not create owned units");p.unitQueue[0].clear();
    p.unitProd[0].active=p.unitProd[0].isUnit=true;p.unitProd[0].typeIdx=(int)UnitType::Conscript;
    check(!world.nativeHouseEliminated(16,9) && world.nativeHouseEliminated(16,10),"started ground-unit production counts before physical release");p.unitProd[0]={};
    p.bldProd.active=p.bldProd.ready=true;p.bldProd.typeIdx=(int)BldType::PowerPlant;
    check(!world.nativeHouseEliminated(16,10) && !world.nativeHouseEliminated(16,11),"a completed sidebar building still belongs to its house before placement");p.bldProd={};
    const EID plant=spawn("GAPOWR",3);world.ents[plant].player=4;
    check(world.nativeHouseEliminated(16,10) && !world.nativeHouseEliminated(17,10),"ownership counts follow a surviving structure's current owner");

    for(const auto& scenario:{std::pair{&win,"victory"},std::pair{&lose,"defeat"}}) {
        world=*scenario.first;world.map.bldOccRef=&world.bldOcc;gameOver=true;victory=std::string(scenario.second)=="victory";
        phase=Phase::InGame;camZoom=.75f;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;sideMode=0;targetingSW=SWType::COUNT;
        sel.clear();selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(61,71,x,y);camX=x-(SCREEN_W-sidebarW)/(2.f*camZoom);camY=y-SCREEN_H/(2.f*camZoom);
            shotFile=(std::filesystem::path(outputDir)/("authored-"+std::string(scenario.second)+"-fixture-"+std::to_string(size.x)+".png")).string();
            const auto hash=world.checksum();render();check(hash==world.checksum(),"the source-driven victory/defeat overlay renders without changing the terminal World state");
        }
    }
    std::printf("NATIVE_MISSION failures=%d\n",failures);return failures;
}
