#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

int GameRegression::nativeScriptControlRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_CONTROL SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0,serial=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_CONTROL %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const std::string& file){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(file.c_str());};
    if(!load(scene)){check(false,"the actual source scene loads");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto resume=[&](int ticks) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= world.checksum()==restored.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}return exact;
    };
    const auto old="review_out/native-campaign/legacy-native41-control.sav";
    if(FileExists(old)) {
        const bool loaded=loadGameFile(old);bool unset=loaded;
        if(loaded)for(const auto& team:world.nativeEvents.teams)unset &= !team.succeeded;
        check(unset && resume(20),"an actual World41/Scene10 active save defaults Success to false and resumes identically");
    }
    const auto clear=[&]() {
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        world.projs.clear();world.effects.clear();world.timedBombs.clear();
    };
    world=initial;world.map.bldOccRef=&world.bldOcc;clear();Vec2i origin{-1,-1};
    for(int y=6;y<world.map.h-6 && origin.x<0;++y)for(int x=6;x<world.map.w-20 && origin.x<0;++x) {
        bool free=true;for(int dy=-2;dy<=2 && free;++dy)for(int dx=-2;dx<=17 && free;++dx)
            free=world.map.surfacePassable(x+dx,y+dy,0,0,true,true) && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height;
        if(free)origin={x,y};
    }
    check(origin.x>=0,"the explicit control/weapon arena has level original terrain");if(origin.x<0)return failures;
    const auto fixture=[&](const std::string& script,bool salvo=false) {
        auto source=initial.nativeScenarioSource;
        if(!script.empty()) {
            const std::string header="[NativeEvent.record.0a64854c]";const auto begin=source.find(header);
            if(begin==std::string::npos)return -1;auto end=source.find("\n[",begin+header.size());if(end==std::string::npos)end=source.size();
            source.replace(begin,end-begin,header+"\nname = COUNTER script control\n"+script);
        }
        if(salvo) {
            auto begin=source.find("[NativeEvent.waypoints]");if(begin==std::string::npos)return -1;
            begin=source.find("96 = ",begin);if(begin==std::string::npos)return -1;
            begin=source.find('=',begin)+2;source.replace(begin,source.find('\n',begin)-begin,std::to_string(origin.y*1000+origin.x+12));
        }
        const auto path=(std::filesystem::path(outputDir)/("counter-control-"+std::to_string(serial++)+".ini")).string();
        {std::ofstream out(path,std::ios::binary);out<<source;}if(!load(path))return -1;clear();
        if(!world.startNativeEvents())return -1;for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
        const int def=world.nativeTeamIndex(salvo?"095B322C":"0BF26C8C"),type=world.nativeUnitIndex(salvo?"V3":"E2");
        if(def<0 || type<0)return -1;
        for(int i=0;i<(salvo?1:2);++i)if(!world.valid(world.spawnNativeActor(type,1,origin.x+.5f+i*.35f,origin.y+.5f,256,64,
            "counter:control-member:"+std::to_string(i),"","Guard")))return -1;
        const int team=world.createNativeTeam(def,false);
        // Stage the first member of this explicit two-person mechanism arena;
        // the real next tick recruits the second and executes its first line.
        if(team>=0 && world.nativeAI.active && !salvo)world.recruitNativeAITeam(team);
        return team;
    };

    int team=fixture("0 = 49,123456\n5 = 6,1\n49 = 250,0\n50 = 49,0\n");
    if(team>=0) {
        const auto& source=world.nativeEventDefinition->scripts[world.nativeEventDefinition->teams[world.nativeEvents.teams[team].definition].script];
        check(source.steps.size()==3 && source.steps[0]==Vec2i{49,123456} && source.steps[1]==Vec2i{6,1},
            "script slots 0..49 are read in occupied order across gaps; slot50 is outside the original script table");
        world.update();const auto members=world.nativeEvents.teams[team].members;
        bool marked=world.nativeEvents.teams[team].succeeded && world.nativeEvents.teams[team].step==1 && members.size()==2;
        World unmarked=world;unmarked.map.bldOccRef=&unmarked.bldOcc;unmarked.nativeEvents.teams[team].succeeded=false;
        check(marked && world.checksum()!=unmarked.checksum() && resume(4),
            "Success ignores its unused parameter, is hashed and persists through backward jumps and exact resumed ticks");
        const auto path=(std::filesystem::path(outputDir)/"successful-loop.sav").string();const auto hash=world.checksum();
        check(saveGameFile(path.c_str()) && loadGameFile(path.c_str()) && world.checksum()==hash && world.nativeEvents.teams[team].succeeded,
            "application v10 / World43 restores a successful team while its original actor identities remain attached");
        world.finishNativeTeam(team);bool retained=world.nativeEvents.teams[team].succeeded && !world.nativeEvents.teams[team].alive;
        for(EID id:members)retained &= world.valid(id);
        check(retained && resume(2),"dissolving a successful team retains its lifetime result and leaves members alive with their existing orders");
    } else check(false,"the sparse control script loads");

    for(int target:{1,3,0,-1,4,100000000}) {
        team=fixture("0 = 6,"+std::to_string(target)+"\n1 = 49,0\n2 = 250,0\n");
        if(team<0){check(false,"a jump-boundary counter scene loads");continue;}
        world.update();const auto members=world.nativeEvents.teams[team].members;
        const int expected=target>0 && target<=3?target-1:3;
        bool exact=world.nativeEvents.teams[team].step==expected && world.nativeEvents.teams[team].alive && !world.nativeEvents.teams[team].succeeded;
        exact &= resume(1);
        const auto& state=world.nativeEvents.teams[team];
        if(target==1)exact &= state.alive && state.step==0 && !state.lineStarted;
        else if(target==3)exact &= state.alive && state.step==2 && state.blockedScript==250 && !state.succeeded;
        else {exact &= !state.alive && !state.succeeded;for(EID id:members)exact &= world.valid(id);}
        check(exact,"one-based forward/self/off-end jumps execute one line per tick, survive a snapshot and never fabricate Success or kill members");
    }
    team=fixture("0 = 6,1\n");
    if(team>=0) {
        world.update();const auto member=world.nativeEvents.teams[team].members.front();
        world.orderMove({member},origin.x+2.5f,origin.y+.5f,false);
        const auto actor=world.ents[member];const auto rng=world.rng.s;world.updateNativeTeamControl(team);
        check(world.ents[member].path==actor.path && world.ents[member].state==actor.state && world.ents[member].goalX==actor.goalX
            && world.ents[member].x==actor.x && world.rng.s==rng,"a cursor-only jump preserves an actual issued movement order and consumes no simulation RNG");
        world.nativeEvents.teams[team].succeeded=true;FILE* bad=tmpfile();
        check(bad && !world.saveGame(bad),"a forged successful state with no Success line is rejected");if(bad)fclose(bad);
        world.nativeEvents.teams[team].succeeded=false;world.finishNativeTeam(team);
        check(!world.nativeEvents.teams[team].succeeded && resume(2),"a team removed without reaching Success remains unsuccessful");
    } else check(false,"the cursor/order counter scene loads");
    team=fixture("0 = 49,0\n");
    if(team>=0) {
        world.update();const auto& state=world.nativeEvents.teams[team];
        const bool final=state.alive && state.succeeded && state.step==1;
        check(final && resume(1) && !state.alive && state.succeeded,"a final Success is recorded before next-turn script retirement and survives that transition");
    } else check(false,"the final-Success counter scene loads");

    // The exact authored V3 Salvo script and roster, in a marked arena whose
    // waypoint96 is relocated. Actual source missiles verify both passes.
    team=fixture("",true);
    if(team>=0) {
        const int type=world.nativeUnitIndex("E2");
        const auto spawnTarget=[&](const char* key){const auto id=world.spawnNativeActor(type,0,origin.x+12.5f,origin.y+.5f,256,0,key,"","Guard");
            if(world.valid(id)){world.ents[id].hp=10000;world.ents[id].tpSick=10000;}return id;};
        const auto first=spawnTarget("counter:first-salvo-target");world.update();
        for(int i=0;i<1800 && world.valid(first) && world.ents[first].hp==10000;++i)world.update();
        const bool firstHit=world.valid(first) && world.ents[first].hp<10000;
        const auto actors=world.nativeEvents.teams[team].members;world.kill(first,false);world.update();
        const bool waiting=world.nativeEvents.teams[team].step==1 && resume(0);world.update();
        const bool jumped=world.nativeEvents.teams[team].step==0 && !world.nativeEvents.teams[team].lineStarted;
        const auto second=spawnTarget("counter:second-salvo-target");world.update();
        const bool captured=world.nativeEvents.teams[team].focus==second && world.nativeEvents.teams[team].focusFixed;
        for(int i=0;i<1800 && world.valid(second) && world.ents[second].hp==10000;++i)world.update();
        const auto& script=world.nativeEventDefinition->scripts[world.nativeEventDefinition->teams[world.nativeEvents.teams[team].definition].script];
        check(firstHit && waiting && jumped && captured && script.steps==std::vector<Vec2i>{{1,96},{6,1}} && actors.size()==1
            && world.valid(second) && world.ents[second].hp<10000 && resume(30),
            "the unchanged original one-V3 salvo script hits through real missiles, jumps after target removal, captures the replacement at its waypoint and actually hits again");
        std::printf("NATIVE_CONTROL salvo tick=%llu firstHit=%d waiting=%d jumped=%d captured=%d secondHP=%d\n",
            (unsigned long long)world.tick,firstHit,waiting,jumped,captured,world.valid(second)?world.ents[second].hp:-1);
    } else check(false,"the source-script V3 salvo counter scene loads");
    std::printf("NATIVE_CONTROL failures=%d\n",failures);return failures;
}
