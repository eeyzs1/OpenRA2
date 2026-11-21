#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

int GameRegression::nativeTeamRegression(const char* outputDir) {
    const auto path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_TEAM SKIP: export the original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("NATIVE_TEAM %s: %s\n",ok?"PASS":"FAIL",message);failures+=!ok;};
    g_script.shutdown();world=World{};ais.clear();localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const std::string& source){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(source.c_str()) && world.startNativeEvents();};
    if(!load(path)){check(false,"the complete original scene and event graph load");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    const int definition=world.nativeTeamIndex("0A67F6CC"),trigger=world.nativeTriggerIndex("07A9578C");
    EID statue=INVALID_EID;std::set<EID> originalShips;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition) {
        const auto& name=world.ents[id].nativeIdentity.definition->id;
        if(name=="DRED")originalShips.insert(id);if(name=="CANEWY04")statue=id;
    }
    check(definition>=0 && trigger>=0 && originalShips.size()==4 && world.valid(statue)
        && world.nativeEventDefinition->teams[definition].max==1 && world.nativeWaypointObject(world.cellIdx(62,71))==statue,
        "the source has four DREDs, four explicit team actions despite Max=1, and the statue foundation covers waypoint A");
    if(definition<0 || !world.valid(statue))return failures+1;
    const int initialHp=world.ents[statue].hp;const auto initialEntities=world.ents.size();
    world.update();
    std::vector<int> teams;std::set<EID> recruited;
    for(int i=0;i<(int)world.nativeEvents.teams.size();++i) {
        const auto& team=world.nativeEvents.teams[i];if(team.definition!=definition)continue;teams.push_back(i);
        recruited.insert(team.members.begin(),team.members.end());
    }
    bool authored=false;for(const auto& runtime:world.nativeEvents.triggers)if(runtime.definition==trigger)authored=runtime.firings==1 && runtime.blockedAction==-1;
    bool focused=teams.size()==4;
    for(int i:teams){const auto& team=world.nativeEvents.teams[i];focused &= team.assembled && team.lineStarted && team.focusFixed
        && team.focus==statue && team.members.size()==1 && team.blockedScript==-1;}
    bool onlyMissiles=true;for(EID id=(EID)initialEntities;id<(EID)world.ents.size();++id)if(world.valid(id))onlyMissiles &= world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->nativeRocket;
    check(authored && focused && recruited==originalShips && onlyMissiles,
        "the genuine Mission Start recruits four distinct original ships without cloning ships; new actors are their spawned missiles");
    if(teams.empty())return failures+1;
    World started=world;started.map.bldOccRef=&started.bldOcc;
    const auto startedReset=[&](){world=started;world.map.bldOccRef=&world.bldOcc;};
    FILE* snapshot=tmpfile();World replay;bool continued=snapshot && world.saveGame(snapshot);
    if(continued){rewind(snapshot);continued=replay.loadGame(snapshot);}if(snapshot)fclose(snapshot);
    const auto application=(std::filesystem::path(outputDir)/"dreadnought-attack.sav").string();const auto saved=world.checksum();
    const bool appSaved=saveGameFile(application.c_str());if(appSaved)world.players[0].money+=19;
    check(appSaved && loadGameFile(application.c_str()) && world.checksum()==saved,
        "application v10 restores the active DRED script, captured target, members and projectile state");
    continued &= replay.checksum()==world.checksum();
    bool missiles=false,damaged=false;int hitAt=-1,destroyedAt=-1;World attackShot;
    for(int i=0;i<120000 && continued && world.valid(statue);++i) {
        world.update();replay.update();continued &= world.checksum()==replay.checksum();
        for(const auto& projectile:world.nativeRockets)if(originalShips.contains(projectile.source) && projectile.phase==NativeRocketPhase::Flight) {
            if(!missiles){attackShot=world;attackShot.map.bldOccRef=&attackShot.bldOcc;}missiles=true;
        }
        if(world.valid(statue) && world.ents[statue].hp<initialHp && hitAt<0){hitAt=(int)world.tick;damaged=true;}
        if(!world.valid(statue))destroyedAt=(int)world.tick;
        if(i%3000==0){std::ofstream progress(std::filesystem::path(outputDir)/"attack-progress.txt");progress<<"tick="<<world.tick<<" hp="<<world.ents[statue].hp<<" missiles="<<world.nativeRockets.size()<<" exact="<<continued<<"\n";}
    }
    check(continued && missiles && damaged && destroyedAt>hitAt && hitAt>0,
        "the original recruited DREDs launch actual projectiles, damage and destroy the source statue with identical post-load simulation");
    std::printf("NATIVE_TEAM ATTACK: continued=%d tick=%llu replayTick=%llu missiles=%d hp=%d projectiles=%zu checksums=%u/%u\n",
        continued,(unsigned long long)world.tick,(unsigned long long)replay.tick,missiles,world.ents[statue].hp,world.projs.size(),world.checksum(),replay.checksum());
    world.update();replay.update();bool ended=world.checksum()==replay.checksum();
    for(int i:teams)ended &= !world.nativeEvents.teams[i].alive && world.nativeEvents.teams[i].members.empty();
    for(EID id:originalShips)ended &= world.valid(id) && world.ents[id].nativeIdentity.definition->id=="DRED";
    check(ended,"finishing the authored attack script retires its teams while keeping the original ships alive");
    if(missiles) {
        world=attackShot;world.map.bldOccRef=&world.bldOcc;camZoom=.75f;interpAlpha=1;
        showMenu=placing=dragging=targetingParadrop=false;sideMode=0;targetingSW=SWType::COUNT;
        sel.assign(originalShips.begin(),originalShips.end());selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);fogKnowledge={};syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(57,74,x,y);
            camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/("authored-dred-attack-"+std::to_string(size.x)+".png")).string();
            const auto state=world.checksum();render();check(world.checksum()==state,"the real source attack renders without changing simulation state");
        }
    }
    startedReset();
    const float sx=world.ents[statue].x,sy=world.ents[statue].y;const int owner=world.ents[statue].player;
    world.kill(statue,false);const auto replacement=world.spawnBuilding(owner,BldType::ConYard,(int)sx,(int)sy);
    bool detached=replacement==statue;for(int i:teams)detached &= world.nativeEvents.teams[i].focus==-1;
    world.update();for(int i:teams)detached &= !world.nativeEvents.teams[i].alive;
    check(detached,"destroying and reusing the target ID completes the old script instead of retargeting the replacement");
    startedReset();
    auto state=world.nativeEvents;
    world.nativeEvents.teams[teams.front()].focus=1000000;const bool badTarget=!world.validateNativeEventState();world.nativeEvents=state;
    world.nativeEvents.teams[teams.front()].step=1000000;const bool badCursor=!world.validateNativeEventState();world.nativeEvents=state;
    check(badTarget && badCursor,"invalid team target references and script cursors are rejected before saving");
    // Counterexamples use explicit source copies; the original map/scene stays intact.
    const auto fixture=[&](const char* name,const std::string& record,const std::string& key,const std::string& value) {
        auto source=initial.nativeScenarioSource;const auto section=source.find("[NativeEvent.record."+record+"]");
        if(section==std::string::npos)return std::string{};
        const auto begin=source.find('\n',section),end=source.find("\n[",begin+1);const auto limit=end==std::string::npos?source.size():end;
        const auto position=source.find("\n"+key+" = ",begin);
        if(position<limit){const auto last=source.find('\n',position+1);source.replace(position+1,last-position-1,key+" = "+value);}
        else source.insert(limit,"\n"+key+" = "+value+"\n");
        const auto out=(std::filesystem::path(outputDir)/name).string();std::ofstream file(out,std::ios::binary);file<<source;return out;
    };
    const auto tagged=fixture("independent-team-tags.ini","0a67f6cc","tag","0965B06C");
    bool independent=!tagged.empty() && load(tagged);
    if(independent) {
        world.update();std::set<int> tags;EID first=INVALID_EID;int firstTag=-1;std::vector<int> otherTags;
        for(const auto& team:world.nativeEvents.teams)if(team.definition==definition && team.members.size()==1) {
            tags.insert(team.tag);if(first<0){first=team.members.front();firstTag=team.tag;}else otherTags.push_back(team.tag);
        }
        independent=tags.size()==4 && firstTag>=(int)world.nativeEventDefinition->tags.size();
        const auto wp=world.nativeWaypoint(4);const auto entrant=world.spawnUnit(0,UnitType::GI,wp.x+.5f,wp.y+.5f);
        world.offerNativeTagEvent(firstTag,1,entrant);
        independent &= !world.nativeEvents.tags[firstTag].alive && world.ents[first].nativeEventTag==-1;
        for(int tag:otherTags)independent &= world.nativeEvents.tags[tag].alive;
        FILE* save=tmpfile();World restored;bool exact=save && world.saveGame(save);
        if(exact){rewind(save);exact=restored.loadGame(save);}if(save)fclose(save);
        independent &= exact && restored.checksum()==world.checksum();
    }
    check(independent,"four explicitly created teams have independent tags; firing one volatile tag leaves its siblings alive across save/load");
    const auto moving=fixture("moving-waypoint-target.ini","07a92b3c","0","1,4");
    bool sticky=!moving.empty() && load(moving);
    if(sticky) {
        const auto point=world.nativeWaypoint(4);const auto target=world.spawnUnit(0,UnitType::MCV,point.x+.5f,point.y+.5f);
        world.ents[target].invuln=100000;world.update();
        Vec2i destination{-1,-1};
        for(auto step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const Vec2i next{point.x+step.x,point.y+step.y};std::vector<Vec2i> route;
            if(world.map.surfacePassable(next.x,next.y,0,0) && world.unitAtCell(next.x,next.y)==INVALID_EID
                && world.map.findPath(point.x,point.y,next.x,next.y,route) && route.size()==1){destination=next;break;}
        }
        const auto movement=world.orderMove({target},destination.x+.5f,destination.y+.5f,false);
        for(int i=0;i<90;++i)world.update();
        sticky &= movement.accepted==1 && destination.x>=0 && ((int)world.ents[target].x!=point.x || (int)world.ents[target].y!=point.y);
        for(const auto& team:world.nativeEvents.teams)if(team.definition==definition)sticky &= team.focus==target && team.focusFixed;
        FILE* save=tmpfile();World restored;bool exact=save && world.saveGame(save);
        if(exact){rewind(save);exact=restored.loadGame(save);}if(save)fclose(save);
        sticky &= exact && restored.checksum()==world.checksum();
        std::printf("NATIVE_TEAM MOVING: position=%f,%f destination=%d,%d save=%d checksums=%u/%u\n",
            world.ents[target].x,world.ents[target].y,destination.x,destination.y,exact,world.checksum(),restored.checksum());
    }
    check(sticky,"an object selected on the first script turn remains the target after actual movement away from the waypoint and reload");
    bool cellTarget=load(moving);
    if(cellTarget) {
        world.update();
        for(const auto& team:world.nativeEvents.teams)if(team.definition==definition)cellTarget &= !team.focusFixed && team.focus==-1 && team.blockedScript==1;
        const auto point=world.nativeWaypoint(4);const auto target=world.spawnUnit(0,UnitType::MCV,point.x+.5f,point.y+.5f);world.update();
        for(const auto& team:world.nativeEvents.teams)if(team.definition==definition)cellTarget &= !team.focusFixed && team.focus==target;
        world.kill(target,false);const auto replacement=world.spawnUnit(0,UnitType::MCV,point.x+.5f,point.y+.5f);world.update();
        for(const auto& team:world.nativeEvents.teams)if(team.definition==definition)cellTarget &= team.alive && !team.focusFixed && team.focus==replacement;
    }
    check(cellTarget,"an initially empty waypoint retains a cell target, reports unsupported ground fire and reacquires later objects without finishing the script");
    reset();const auto extra=world.createNativeTeam(definition,false);world.update();
    bool noClonedShips=true;for(EID id=(EID)initialEntities;id<(EID)world.ents.size();++id)if(world.valid(id))noClonedShips &= world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->nativeRocket;
    check(extra>=0 && world.nativeEvents.teams.size()>=5 && noClonedShips,
        "an extra explicit Create Team creates an unfilled team without bypassing recruitment or cloning a ship");
    {
        World defense;defense.init(64,64,47,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)defense.ents.size();++id)if(defense.valid(id))defense.kill(id,false);
        defense.spawnBuilding(0,BldType::PowerPlant,20,20,true);
        const auto tower=defense.spawnBuilding(0,BldType::PatriotMissile,30,30,true);
        const auto& gun=bldDef(defense.ents[tower]).weapon;
        Projectile incoming;incoming.kind=ProjKind::Missile;incoming.player=1;incoming.x=incoming.ox=32;
        incoming.y=incoming.oy=incoming.ty=31;incoming.tx=50;incoming.hp=45;incoming.speed=10;incoming.w.damage=0;
        defense.projs.assign(3,incoming);defense.update();
        const bool first=defense.projs.size()==2 && defense.ents[tower].atkCd==gun.cooldown;
        FILE* save=tmpfile();World restored;bool exact=save && defense.saveGame(save);
        if(exact){rewind(save);exact=restored.loadGame(save);}if(save)fclose(save);
        for(int i=0;i<gun.cooldown-1;++i){defense.update();restored.update();exact &= defense.checksum()==restored.checksum();}
        const bool waited=defense.projs.size()==2;defense.update();restored.update();exact &= defense.checksum()==restored.checksum();
        check(first && waited && defense.projs.size()==1 && exact,
            "one powered defender uses actual weapon damage and one shared cooldown across incoming missiles, including save/load");
        defense.ents[tower].atkCd=0;defense.players[0].powerMade=0;defense.players[0].powerUsed=1000;defense.update();
        check(defense.projs.size()==1 && defense.ents[tower].atkCd==0,"an unpowered defender cannot intercept incoming missiles");
    }
    std::printf("NATIVE_TEAM failures=%d firstHit=%d destroyed=%d initialHp=%d\n",failures,hitAt,destroyedAt,initialHp);return failures;
}
