#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

int GameRegression::nativeTeamOrdersRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_ORDERS SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_ORDERS %s: %s\n",ok?"PASS":"FAIL",text);failures+=!ok;};
    g_script.shutdown();world=World{};ais.clear();localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const std::string& file){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(file.c_str()) && world.startNativeEvents();};
    if(!load(scene)){check(false,"the original scene and event graph load");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    const auto resume=[&](int ticks) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= world.checksum()==restored.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}
        return exact;
    };
    const int tanyaDefinition=world.nativeTeamIndex("094F68EC"),giDefinition=world.nativeTeamIndex("0A7475CC");
    EID tanya=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id)
        && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="TANY")tanya=id;
    check(tanyaDefinition>=0 && giDefinition>=0 && world.valid(tanya) && world.nativeEventDefinition->strayDistance==2,
        "the source Tanya/GI rosters, scripts and map-local StrayDistance are loaded");
    if(tanyaDefinition<0 || giDefinition<0 || !world.valid(tanya))return failures+1;
    const auto teamIndex=[&](int definition){for(int i=0;i<(int)world.nativeEvents.teams.size();++i)
        if(world.nativeEvents.teams[i].definition==definition)return i;return -1;};
    const auto travelTicks=[&](const World::Ent& actor,float distance){
        float step=1.f/std::max(1,unitDef(actor).speed);
        if(actor.nativeIdentity.definition && actor.nativeIdentity.definition->walkSpeedLeptons>=0){const auto& d=*actor.nativeIdentity.definition;
            step=(int)(d.walkSpeedLeptons*d.walkVeteranSpeed[std::clamp(actor.vetRank,0,2)])*(15.f/(256.f*LOGIC_FPS));}
        return step>0?(int)std::ceil(distance/step):18000;
    };
    const auto pathLength=[&](const World::Ent& actor){float length=0,x=actor.x,y=actor.y;
        for(int i=actor.pathIdx;i<(int)actor.path.size();++i){const float nx=actor.path[i].x+.5f,ny=actor.path[i].y+.5f;length+=distf(x,y,nx,ny);x=nx;y=ny;}return length;};
    {
        const auto legacy="review_out/native-campaign/legacy-native37-team.sav";
        if(FileExists(legacy)) {
            const bool loaded=loadGameFile(legacy);bool members=loaded && world.nativeEvents.active && world.nativeEvents.teams.size()==4;
            for(const auto& team:world.nativeEvents.teams)members &= team.memberHouse==world.nativeEventDefinition->teams[team.definition].house && team.focusLayer==-1;
            check(members && resume(120),"an actual saved v10/World v37 native four-ship attack restores with default member ownership and continues exactly");
        } else std::printf("NATIVE_ORDERS NOTE: optional pre-v38 native snapshot is not installed\n");
    }
    reset();const float originalX=world.ents[tanya].x,originalY=world.ents[tanya].y;
    world.update();check(world.nativeEvents.inputLocked,"the original opening actually locks player input");
    const int unlock=world.nativeTriggerIndex("094D93DC");int movingAt=-1,guardAt=-1,unlockedAt=-1;bool movementSave=false,guardSave=false;
    World movementShot,guardShot;
    uint64_t openingLimit=1900;
    while(world.tick<openingLimit && (unlockedAt<0 || guardAt<0)) {
        world.update();const int index=teamIndex(tanyaDefinition);
        if(index>=0) {
            const auto& team=world.nativeEvents.teams[index];
            if(movingAt<0 && team.step==0 && team.lineStarted && world.ents[tanya].state==UState::Moving) {
                movingAt=(int)world.tick;movementShot=world;movementShot.map.bldOccRef=&movementShot.bldOcc;movementSave=resume(5);
                openingLimit=std::max(openingLimit,world.tick+travelTicks(world.ents[tanya],pathLength(world.ents[tanya]))+2*LOGIC_FPS);
            }
            if(guardAt<0 && team.step==1 && team.lineStarted) {
                guardAt=(int)world.tick;guardShot=world;guardShot.map.bldOccRef=&guardShot.bldOcc;
                guardSave=team.waitUntil==world.tick+30*LOGIC_FPS && resume(5);
            }
        }
        if(unlockedAt<0)for(const auto& trigger:world.nativeEvents.triggers)if(trigger.definition==unlock && trigger.firings)unlockedAt=(int)world.tick;
    }
    check(movingAt>=0 && guardAt>movingAt && movementSave && guardSave
        && distf(originalX,originalY,world.ents[tanya].x,world.ents[tanya].y)>2,
        "the untouched opening physically moves the original Tanya to waypoint 36, then starts Guard(30); both stages resume exactly");
    check(unlockedAt==55*LOGIC_FPS && !world.nativeEvents.inputLocked,"the genuine chained timers unlock control at project tick 1650");
    std::printf("NATIVE_ORDERS opening: moving=%d guard=%d unlocked=%d position=%.3f,%.3f\n",movingAt,guardAt,unlockedAt,world.ents[tanya].x,world.ents[tanya].y);
    const int tanyaTeam=teamIndex(tanyaDefinition);
    if(tanyaTeam>=0 && world.valid(tanya)) {
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={tanya};command.x=std::nanf("");command.y=3;
        const auto before=world.checksum();world.applyCmd(0,command);
        check(before==world.checksum() && world.nativeEvents.teams[tanyaTeam].alive,"an invalid player destination cannot sever the authored team or change its simulation orders");
        Vec2i target{-1,-1};const auto& actor=world.ents[tanya];const auto& type=unitDef(actor);
        for(int dy=-7;dy<=7 && target.x<0;++dy)for(int dx=-7;dx<=7 && target.x<0;++dx) {
            if(dx*dx+dy*dy<16 || dx*dx+dy*dy>49)continue;const int x=(int)actor.x+dx,y=(int)actor.y+dy;
            std::vector<Vec2i> route;const int layer=world.map.preferredLayer(x,y,type.pathDomain());
            if(world.map.inBounds(x,y) && world.map.surfacePassable(x,y,type.pathDomain(),layer,true,type.fitsUnderBridge(),world.nativeGateAccess(actor))
                && !world.cellHardBlockedForMove(x,y,tanya,layer)
                && world.map.findPath((int)actor.x,(int)actor.y,x,y,route,20000,type.pathDomain(),nullptr,actor.navLayer,layer,type.fitsUnderBridge(),world.nativeGateAccess(actor)))target={x,y};
        }
        const int tag=actor.nativeEventTag;const float x=actor.x,y=actor.y;
        const int playerMotionTicks=std::max(60,travelTicks(actor,3.f));
        command.x=target.x+.5f;command.y=target.y+.5f;world.applyCmd(0,command);
        const bool released=target.x>=0 && !world.nativeEvents.teams[tanyaTeam].alive && world.ents[tanya].nativeEventTag==tag;
        const auto application=(std::filesystem::path(outputDir)/"tanya-player-control.sav").string();const auto hash=world.checksum();
        const bool saved=saveGameFile(application.c_str());if(saved)world.players[0].money+=19;
        check(saved && loadGameFile(application.c_str()) && world.checksum()==hash,"application v10 restores the moving player-controlled actor and finished script team");
        for(int i=0;i<playerMotionTicks;++i)world.update();
        check(released && world.valid(tanya) && distf(x,y,world.ents[tanya].x,world.ents[tanya].y)>2,
            "a successful player move releases Tanya from scripted guard and keeps her original death trigger");
        world.kill(tanya,false);const int failure=world.nativeTriggerIndex("0B1C7E9C");bool armed=false;
        for(const auto& trigger:world.nativeEvents.triggers)if(trigger.definition==failure && trigger.enabled)armed=true;
        check(armed,"Tanya's authored failure chain still arms after player control and save/load");
    }
    reset();
    {
        const int house=world.nativeEventDefinition->teams[giDefinition].house;std::set<EID> originalGis;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==house
            && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="E1")originalGis.insert(id);
        // Isolate the actual authored Enable GI's / ButtNugget action chain.
        // The TaskForce asks for 14, while only 11 GIs are placed. Its unfilled
        // team must not block the independent All Change House trigger.
        const int enable=world.nativeTriggerIndex("07A707CC"),handover=world.nativeTriggerIndex("07BBD33C");
        for(auto& trigger:world.nativeEvents.triggers)if(trigger.definition==enable){trigger.enabled=true;trigger.timerStart=world.tick;}
        bool fired=false;for(int i=0;i<10 && !fired;++i){world.update();for(const auto& trigger:world.nativeEvents.triggers)if(trigger.definition==handover && trigger.firings)fired=true;}
        const int index=teamIndex(giDefinition);bool exact=fired && index>=0 && originalGis.size()==11;
        if(index>=0) {
            const auto& team=world.nativeEvents.teams[index];const auto& def=world.nativeEventDefinition->teams[team.definition];
            exact &= !team.assembled && team.memberHouse==house && def.house==house
                && world.nativeEventDefinition->taskForces[def.taskForce].units.front().first==14;
        }
        std::set<EID> transferred;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
            && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="E1")transferred.insert(id);
        exact &= transferred==originalGis;
        check(exact && resume(3),"the original All Change House chain hands over eleven existing GIs while preserving the authored fourteen-place roster and its unfilled team");
    }
    // Direct Change House script counterexample uses the one-member Tanya
    // roster and a marked script copy, without filling the original GI roster.
    reset();
    {
        auto source=world.nativeScenarioSource;const auto section=source.find("[NativeEvent.record.09680dbc]");
        const auto first=source.find("\n0 = ",section),firstEnd=source.find('\n',first+1);
        const auto second=source.find("\n1 = ",section),secondEnd=source.find('\n',second+1);
        bool fixture=section!=std::string::npos && first!=std::string::npos && second!=std::string::npos;
        if(fixture) {
            source.replace(second+1,secondEnd-second-1,"1 = 3,36");source.replace(first+1,firstEnd-first-1,"0 = 20,14");
            const auto file=(std::filesystem::path(outputDir)/"change-team-house.ini").string();{std::ofstream output(file,std::ios::binary);output<<source;}
            fixture=load(file);for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
            for(auto& alliance:world.nativeAllies)alliance=(uint8_t)((1u<<world.numPlayers)-1);world.rebuildNativeGates();
            const int index=fixture?world.createNativeTeam(tanyaDefinition,false):-1;world.update();
            fixture &= index>=0 && world.ents[tanya].player==world.nativeHouseNumber(14);
            if(index>=0)fixture &= world.nativeEvents.teams[index].memberHouse==world.nativeHouseNumber(14)
                && world.nativeEventDefinition->teams[tanyaDefinition].house==0 && world.nativeEvents.teams[index].members.size()==1;
            fixture &= resume(5);
            if(index>=0)fixture &= world.nativeEvents.teams[index].step==1 && world.nativeEvents.teams[index].alive
                && world.nativeEvents.teams[index].members.size()==1;
        }
        check(fixture,"Change House script transfers its existing member, keeps the original team house, and continues the next movement line after save/load");
    }
    reset();
    {
        auto source=world.nativeScenarioSource;const auto section=source.find("[NativeEvent.record.09680dbc]");
        const auto step=source.find("\n1 = ",section),end=source.find('\n',step+1);
        bool fixture=section!=std::string::npos && step!=std::string::npos && end!=std::string::npos;
        if(fixture) {
            source.replace(step+1,end-step-1,"1 = 250,0");const auto file=(std::filesystem::path(outputDir)/"unsupported-later-line.ini").string();
            {std::ofstream output(file,std::ios::binary);output<<source;}
            fixture=load(file);for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
            for(auto& alliance:world.nativeAllies)alliance=(uint8_t)((1u<<world.numPlayers)-1);world.rebuildNativeGates();
            const int index=fixture?world.createNativeTeam(tanyaDefinition,false):-1;
            for(int i=0;i<900 && index>=0 && world.nativeEvents.teams[index].blockedScript!=250;++i)world.update();
            fixture &= index>=0 && world.nativeEvents.teams[index].alive && world.nativeEvents.teams[index].step==1
                && world.nativeEvents.teams[index].blockedScript==250 && !world.nativeEvents.teams[index].lineStarted && resume(20);
        }
        check(fixture,"an unsupported later instruction does not prevent team creation or its real movement prefix, and remains an explicit saved script barrier");
    }
    // Explicit movement/timer counterexample: isolate team orders from other
    // triggers and combat, and temporarily block the original Tanya's exits.
    reset();for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
    for(auto& alliance:world.nativeAllies)alliance=(uint8_t)((1u<<world.numPlayers)-1);
    world.rebuildNativeGates();world.ents[tanya].invuln=100000;
    std::vector<std::pair<int,bool>> barrier;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy) {
        const int x=(int)world.ents[tanya].x+dx,y=(int)world.ents[tanya].y+dy;if(!world.map.inBounds(x,y))continue;
        const int cell=world.cellIdx(x,y);barrier.push_back({cell,world.map.cells[cell].blocked});world.map.cells[cell].blocked=true;
    }
    const int blockedTeam=world.createNativeTeam(tanyaDefinition,false);world.update();
    if(blockedTeam>=0) {
        const int destination=world.nativeEvents.teams[blockedTeam].focusCell;
        const float x=world.ents[tanya].x,y=world.ents[tanya].y;
        bool held=resume(90);for(int i=0;i<30;++i)world.update();
        const auto& blocked=world.nativeEvents.teams[blockedTeam];
        check(held && blocked.step==0 && blocked.lineStarted && blocked.focusCell==destination && blocked.blockedScript==3
            && world.ents[tanya].x==x && world.ents[tanya].y==y,"a disconnected route keeps the fixed waypoint pending across save/load instead of counting an adjusted local destination as arrival");
        for(auto [cell,value]:barrier)world.map.cells[cell].blocked=value;
        for(int i=0;i<900 && world.nativeEvents.teams[blockedTeam].step==0;++i)world.update();
        world.update();
        bool reached=world.nativeEvents.teams[blockedTeam].step==1 && world.nativeEvents.teams[blockedTeam].lineStarted;
        check(reached,"opening the actual blocked route allows the same captured waypoint to be reached and the guard line to start");
        if(reached) {
            const auto saved=world.nativeEvents;auto& team=world.nativeEvents.teams[blockedTeam];
            team.memberHouse=world.numPlayers;const bool badHouse=!world.validateNativeEventState();world.nativeEvents=saved;
            world.nativeEvents.teams[blockedTeam].focusLayer=2;const bool badLayer=!world.validateNativeEventState();world.nativeEvents=saved;
            world.nativeEvents.teams[blockedTeam].waitUntil=world.tick+(uint64_t)30*LOGIC_FPS+1;
            const bool badTimer=!world.validateNativeEventState();world.nativeEvents=saved;
            check(badHouse && badLayer && badTimer,"out-of-range member houses, movement surfaces and guard deadlines are rejected before saving");
            const auto end=world.nativeEvents.teams[blockedTeam].waitUntil;check(resume(30),"the active nonzero guard deadline and regroup center resume exactly");
            while(world.tick<end)world.update();
            const bool waited=world.nativeEvents.teams[blockedTeam].alive && world.nativeEvents.teams[blockedTeam].step==1;
            world.update();check(waited && !world.nativeEvents.teams[blockedTeam].alive,"Guard(30) waits its full converted duration and finishes on the following team turn");
        }
    } else check(false,"the source Tanya team can be created in the isolated movement fixture");
    // Group counterexample: use a marked source copy with one nearby waypoint,
    // a Move/Guard script and the roster set to the eleven placed GIs.
    // The unchanged original fourteen-place roster was checked above.
    reset();
    {
        const auto& definition=world.nativeEventDefinition->teams[giDefinition];std::vector<EID> members;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==definition.house
            && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="E1")members.push_back(id);
        Vec2i point{-1,-1};
        if(!members.empty()) {
            const auto& lead=world.ents[members.back()];const auto& type=unitDef(lead);
            for(int dy=-6;dy<=6 && point.x<0;++dy)for(int dx=-6;dx<=6 && point.x<0;++dx) {
                if(dx*dx+dy*dy<9 || dx*dx+dy*dy>36)continue;const int x=(int)lead.x+dx,y=(int)lead.y+dy;
                if(!world.map.surfacePassable(x,y,type.pathDomain(),0) || world.groundUnitBlocksCell(x,y))continue;
                bool routes=true;for(EID id:members){std::vector<Vec2i> path;const auto& actor=world.ents[id];routes &= world.map.findPath((int)actor.x,(int)actor.y,x,y,path);}
                if(routes)point={x,y};
            }
        }
        auto source=world.nativeScenarioSource;const auto section=source.find("[NativeEvent.record.0a6dadbc]");
        const auto step=source.find("\n0 = ",section),last=source.find('\n',step+1);
        bool fixture=point.x>=0 && section!=std::string::npos && step!=std::string::npos && last!=std::string::npos;
        if(fixture) {
            source.replace(step+1,last-step-1,"0 = 3,4094\n1 = 5,0");
            const auto force=source.find("[NativeEvent.record.08a645e0]"),row=source.find("\n0 = ",force),end=source.find('\n',row+1);
            fixture=force!=std::string::npos && row!=std::string::npos && end!=std::string::npos;
            if(fixture)source.replace(row+1,end-row-1,"0 = "+std::to_string(members.size())+",E1");
            const auto list=source.find("[NativeEvent.waypoints]");source.insert(source.find('\n',list)+1,"4094 = "+std::to_string(point.y*1000+point.x)+"\n");
            const auto file=(std::filesystem::path(outputDir)/"group-waypoint.ini").string();{std::ofstream output(file,std::ios::binary);output<<source;}
            fixture=load(file);for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
            for(auto& alliance:world.nativeAllies)alliance=(uint8_t)((1u<<world.numPlayers)-1);world.rebuildNativeGates();
            const int index=fixture?world.createNativeTeam(giDefinition,false):-1;
            int moveBudget=900;
            if(index>=0){world.update();for(EID id:members)if(world.valid(id)){
                const auto& actor=world.ents[id];std::vector<Vec2i> route;float length=pathLength(actor);
                if(world.map.findPath((int)actor.x,(int)actor.y,point.x,point.y,route)){
                    float x=actor.x,y=actor.y,distance=0;for(auto cell:route){distance+=distf(x,y,cell.x+.5f,cell.y+.5f);x=cell.x+.5f;y=cell.y+.5f;}length=std::max(length,distance);}
                moveBudget=std::max(moveBudget,travelTicks(actor,length)+(int)members.size()*LOGIC_FPS);
            }}
            for(int i=0;i<moveBudget && index>=0 && world.nativeEvents.teams[index].alive;++i)world.update();
            fixture &= index>=0 && !world.nativeEvents.teams[index].alive;
            if(index>=0)std::printf("NATIVE_ORDERS group: available=%zu members=%zu step=%d alive=%d block=%d destination=%d,%d\n",members.size(),world.nativeEvents.teams[index].members.size(),world.nativeEvents.teams[index].step,world.nativeEvents.teams[index].alive,world.nativeEvents.teams[index].blockedScript,point.x,point.y);
            for(EID id:members)fixture &= world.valid(id) && distf(world.ents[id].x,world.ents[id].y,point.x+.5f,point.y+.5f)<=world.nativeEventDefinition->strayDistance;
            if(!fixture)for(EID id:members)if(world.valid(id)){const auto& e=world.ents[id];std::printf("NATIVE_ORDERS GROUP_BLOCKED budget=%d id=%d pos=%.3f,%.3f state=%d path=%d/%zu goal=%.1f,%.1f block=%d\n",moveBudget,id,e.x,e.y,(int)e.state,e.pathIdx,e.path.size(),e.goalX,e.goalY,e.blockTick);}
        }
        check(fixture,"the isolated eleven-GI movement group converges within the source Stray radius before advancing");
    }
    for(auto* snapshot:{&movementShot,&guardShot})if(snapshot->nativeEventDefinition) {
        world=*snapshot;world.map.bldOccRef=&world.bldOcc;camZoom=.85f;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;
        sideMode=0;targetingSW=SWType::COUNT;sel={tanya};selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);fogKnowledge={};syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen((int)world.ents[tanya].x,(int)world.ents[tanya].y,x,y);
            camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/(std::string(snapshot==&movementShot?"tanya-moving-":"tanya-guard-")+std::to_string(size.x)+".png")).string();
            const auto hash=world.checksum();render();check(hash==world.checksum(),"the actual source movement/guard frame renders without mutating simulation");
        }
    }
    std::printf("NATIVE_ORDERS failures=%d\n",failures);return failures;
}
