#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

int GameRegression::nativeTeamAttackRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_ATTACK SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_ATTACK %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();world=World{};localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const std::string& file){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(file.c_str()) && world.startNativeEvents();};
    if(!load(scene)){check(false,"original scene and target profiles load");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    const auto resume=[&](int ticks) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= world.checksum()==restored.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}
        return exact;
    };
    const auto teamIndex=[&](int definition){for(int i=0;i<(int)world.nativeEvents.teams.size();++i)
        if(world.nativeEvents.teams[i].definition==definition)return i;return -1;};
    const int definition=world.nativeTeamIndex("097531FC"),otherDefinition=world.nativeTeamIndex("09752C8C");
    const int tanyaDefinition=world.nativeTeamIndex("094F68EC");
    const int assaultDefinition=world.nativeTeamIndex("0BF26C8C");
    const auto& coefficients=world.nativeTypes[world.nativeUnitIndex("E2")]->targeting.coefficients;
    check(definition>=0 && otherDefinition>=0 && tanyaDefinition>=0 && coefficients==std::array<float,5>{200,-200,200,-200,-10},
        "original infantry quarry lines, eight-member rosters and retail target coefficients are loaded");
    if(definition<0 || otherDefinition<0 || tanyaDefinition<0 || assaultDefinition<0)return failures+1;
    {
        const auto old="review_out/native-campaign/legacy-native38-team.sav";
        if(FileExists(old)) {
            bool exact=loadGameFile(old);
            for(const auto& team:world.nativeEvents.teams)exact &= !team.advancePending;
            check(exact && resume(15),"an actual pre-upgrade v10/World v38 native save restores and continues with no pending attack advance");
        } else std::printf("NATIVE_ATTACK NOTE: optional original v38 save is not installed\n");
    }
    reset();
    const int house=world.nativeEventDefinition->teams[definition].house;
    const int start=world.nativeTriggerIndex("07A707CC");
    for(auto& trigger:world.nativeEvents.triggers)if(trigger.definition==start){trigger.enabled=true;trigger.timerStart=world.tick;}
    for(int i=0;i<10;++i)world.update();bool empty=true;
    for(int def:{definition,otherDefinition}) {
        const int index=teamIndex(def);empty &= index>=0;
        if(index>=0)empty &= world.nativeEvents.teams[index].members.empty() && !world.nativeEvents.teams[index].assembled;
    }
    int originalConscripts=0;for(const auto& actor:world.ents)if(actor.alive && actor.player==1
        && actor.nativeIdentity.definition && actor.nativeIdentity.definition->id=="E2")++originalConscripts;
    check(empty && originalConscripts==22,"the original BadGuy3 eight-place teams remain empty; none of BadGuy1's twenty-two placed conscripts is silently reassigned");
    reset();world.update();const int stranded=world.createNativeTeam(assaultDefinition,false);world.update();world.update();
    bool isolated=stranded>=0 && !world.map.hasBridgeDecks();
    if(stranded>=0) {
        const auto& team=world.nativeEvents.teams[stranded];
        const EID target=team.focus;
        isolated &= team.members.size()==2 && !team.advancePending && world.valid(target);
        for(EID id:team.members)isolated &= world.valid(id) && world.ents[id].x<80;
        isolated &= resume(2) && team.step==0 && !team.succeeded && team.focus==target;
    }
    check(isolated,"with authored bridges still destroyed, the real empty-team origin recruits the existing western pair and retains its reachable building quarry without fabricating Success");

    // Positive pursuit fixture: execute the actual source demolition first,
    // then explicitly restore the two spans before creating this test team.
    // No trigger, alliance, actor position or source script is rewritten.
    reset();world.update();bool restoredSpans=!world.map.hasBridgeDecks();
    for(int group=0;group<(int)world.nativeBridgeGroups.size();++group)restoredSpans &= world.restoreBridge(group);
    check(restoredSpans && world.map.hasBridgeDecks(),"the separately marked pursuit fixture restores source bridge topology after the real demolition action");
    const int armyHouse=world.nativeEventDefinition->teams[assaultDefinition].house;
    const int assault=world.createNativeTeam(assaultDefinition,false);
    const auto originalDamage=world.combatDamage[armyHouse];std::set<EID> members;bool attack=false,fired=false,saved=false;World shot;EID shotActor=-1;
    int attackAt=-1,damageAt=-1,firstTeamDamage=0;
    for(int count=0;count<6000 && damageAt<0;++count) {
        world.update();
        if(assault>=0) {
            const auto& team=world.nativeEvents.teams[assault];
            members.insert(team.members.begin(),team.members.end());
            if(team.step==0 && team.lineStarted && team.blockedScript==-1 && world.valid(team.focus)) {
                attack=true;if(attackAt<0)attackAt=(int)world.tick;
                if(!saved){saved=resume(5);}
                if(!shot.nativeEventDefinition && !team.members.empty() && world.valid(team.members.front())) {
                    shot=world;shot.map.bldOccRef=&shot.bldOcc;shotActor=team.members.front();
                }
            }
        }
        EID muzzleTarget=INVALID_EID;bool teamHit=false;
        for(const auto& event:world.presentationEvents)if(event.tick==world.tick) {
            if(event.kind==PresentationKind::Muzzle) {
                muzzleTarget=INVALID_EID;
                if(members.contains(event.entity) && world.valid(event.entity)) {
                    fired=true;shotActor=event.entity;muzzleTarget=world.ents[event.entity].target;
                }
            } else if(event.kind==PresentationKind::Impact && event.entity==muzzleTarget && event.damage>0) {
                teamHit=true;firstTeamDamage=event.damage;
            }
        }
        if(attack && fired && teamHit && world.combatDamage[armyHouse]>originalDamage) {
            damageAt=(int)world.tick;shot=world;shot.map.bldOccRef=&shot.bldOcc;
        }
        if(assault>=0 && !world.nativeEvents.teams[assault].alive)break;
    }
    bool moved=false;for(EID id:members)if(id<(EID)initial.ents.size())
        moved |= distf(initial.ents[id].x,initial.ents[id].y,world.ents[id].x,world.ents[id].y)>.05f;
    check(attack && saved && moved && fired && damageAt>=0 && firstTeamDamage>0 && members.size()==2,
        "with the source spans explicitly restored, the original BadGuy1 roster recruits two existing actors, physically pursues a source building and hits it through its unchanged Attack(2) script");
    std::printf("NATIVE_ATTACK repaired-topology fixture: attack=%d damage=%d members=%zu teamHit=%d damageTotal=%lld\n",attackAt,damageAt,members.size(),firstTeamDamage,(long long)world.combatDamage[armyHouse]);
    if(damageAt<0 && assault>=0){const auto& team=world.nativeEvents.teams[assault];
        std::printf("NATIVE_ATTACK DIAG team focus=%d blocked=%d alive=%d step=%d\n",team.focus,team.blockedScript,team.alive,team.step);
        for(EID id:team.members)if(world.valid(id))std::printf("NATIVE_ATTACK DIAG actor=%d object=%s pos=%.2f,%.2f state=%d target=%d path=%d/%zu\n",
            id,world.ents[id].nativeIdentity.object.c_str(),world.ents[id].x,world.ents[id].y,(int)world.ents[id].state,world.ents[id].target,world.ents[id].pathIdx,world.ents[id].path.size());
        if(world.valid(team.focus))std::printf("NATIVE_ATTACK DIAG target=%s pos=%.2f,%.2f\n",world.ents[team.focus].nativeIdentity.definition->id.c_str(),world.ents[team.focus].x,world.ents[team.focus].y);}

    // Explicit mechanism copies keep the actual roster and source profiles.
    // Only their script is changed; the isolated arena is not an original mission.
    const auto fixture=[&](int quarry,bool human=false) {
        const std::string id=human?"09680dbc":"07f232bc";std::string source=initial.nativeScenarioSource;
        const std::string header="[NativeEvent.record."+id+"]";const auto first=source.find(header);
        if(first==std::string::npos)return -1;auto end=source.find("\n[",first+header.size());if(end==std::string::npos)end=source.size();
        source.replace(first,end-first,header+"\nname = COUNTER isolated Attack\n0 = 0,"+std::to_string(quarry)+"\n1 = 5,0\n");
        const auto file=(std::filesystem::path(outputDir)/("counter-"+std::to_string(quarry)+(human?"-human":"")+".ini")).string();
        {std::ofstream output(file,std::ios::binary);output<<source;}if(!load(file))return -1;
        for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        Vec2i point{-1,-1};
        for(int y=6;y<world.map.h-7 && point.x<0;++y)for(int x=6;x<world.map.w-7 && point.x<0;++x) {
            bool clear=true;
            for(int dy=-5;dy<=5 && clear;++dy)for(int dx=-5;dx<=5 && clear;++dx)
                clear=world.map.surfacePassable(x+dx,y+dy,0,0,true,true)
                    && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height;
            if(clear)point={x,y};
        }
        if(point.x<0)return -1;
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        const int def=human?tanyaDefinition:definition,owner=world.nativeEventDefinition->teams[def].house;
        const int count=human?1:8,type=world.nativeUnitIndex(human?"TANY":"E2");
        for(int i=0;i<count;++i) {
            const EID actor=world.spawnNativeActor(type,owner,point.x+(i%3)*.4f+.5f,point.y+(i/3)*.4f+.5f,256,0,"counter:member:"+std::to_string(i),"","Guard");
            if(!world.valid(actor))return -1;world.ents[actor].invuln=100000;
        }
        const int index=world.createNativeTeam(def,false);
        if(index>=0 && world.nativeAI.active)for(int i=1;i<count;++i)world.recruitNativeAITeam(index);
        world.update();return index;
    };
    const auto spawn=[&](const char* type,float x,float y,int owner) {
        int definition=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==type){definition=i;break;}
        return world.spawnNativeActor(definition,owner,x,y,256,0,
            "counter:target:"+std::to_string(world.tick)+":"+std::to_string(world.ents.size()),"","Sleep");
    };
    int index=fixture(4);
    if(index>=0 && !world.nativeEvents.teams[index].members.empty()) {
        auto& team=world.nativeEvents.teams[index];team.advancePending=false;team.lineStarted=false;
        const auto lead=team.members.back();const float x=world.ents[lead].x,y=world.ents[lead].y;
        const EID first=spawn("E1",x+3,y,0),second=spawn("E1",x+4,y+1,0);
        bool fired=false,continued=false;std::set<EID> targets;
        for(int i=0;i<300 && (world.valid(first) || world.valid(second));++i) {
            world.update();if(world.valid(team.focus))targets.insert(team.focus);
            for(const auto& event:world.presentationEvents)if(event.kind==PresentationKind::Muzzle
                && std::find(team.members.begin(),team.members.end(),event.entity)!=team.members.end())fired=true;
            if(!continued && world.combatDamage[house]>0)continued=resume(1);
        }
        check(fired && continued && !world.valid(first) && !world.valid(second) && world.combatDamage[house]>0,
            "the marked source-roster combat arena actually fires, inflicts damage, destroys two successive infantry targets and resumes exactly");
    } else check(false,"the actual-damage mechanism arena loads");
    index=fixture(4);
    if(index>=0 && !world.nativeEvents.teams[index].members.empty()) {
        // The first empty scan has already completed; restart only this marked fixture.
        auto& team=world.nativeEvents.teams[index];team.advancePending=false;team.lineStarted=false;
        const auto lead=team.members.back();const float x=world.ents[lead].x,y=world.ents[lead].y;
        const EID near=spawn("E1",x+3,y,0),weak=spawn("E1",x+3.04f,y,0);world.ents[weak].hp=10;
        world.ents[near].invuln=world.ents[weak].invuln=100000;
        const auto checksum=world.checksum();
        check(world.nativeTeamAttackTarget(index,4)==weak && checksum==world.checksum(),
            "the immutable source rating can prefer a slightly farther wounded enemy, and the query does not mutate simulation");
        world.update();const bool selected=team.focus==weak && !team.advancePending;
        world.ents[near].hp=1;
        check(selected && team.focus==weak && resume(2),"a selected mission target stays fixed while another enemy becomes more attractive, including save/load");
        world.kill(weak,false);const EID replacement=spawn("CMIN",x+4,y+1,0);world.update();
        check(team.focus==near && team.step==0 && team.focus!=replacement && resume(2),
            "destroying a quarry target reacquires infantry on the same script line instead of adopting an ID-reused vehicle or advancing");
        const EID building=spawn("GAPOWR",x-3,y-3,0);
        check(world.valid(building) && world.valid(replacement) && world.nativeTeamAttackTarget(index,2)==building && world.nativeTeamAttackTarget(index,3)==replacement
            && world.nativeTeamAttackTarget(index,5)==replacement && world.nativeTeamAttackTarget(index,9)==building,
            "building, storage, vehicle and positive-power categories use the authored role flags");
        const EID factory=spawn("GACNST",x-3,y+2,0),defense=spawn("GAPILL",x+3,y-3,0);
        check(world.valid(factory) && world.valid(defense) && world.nativeTeamAttackTarget(index,6)==factory && world.nativeTeamAttackTarget(index,7)==defense,
            "factory and armed-building categories do not collapse to ordinary building selection");
        const EID illegal=spawn("GAPOWR",x-1,y-3,0);
        if(!world.valid(illegal)){check(false,"the LegalTarget mechanism building is physically instantiated");return failures;}
        const auto original=world.ents[illegal].nativeIdentity.definition;
        const bool preferred=world.nativeTeamAttackTarget(index,9)==illegal;
        auto excluded=std::make_shared<NativeActorDefinition>(*original);excluded->targeting.legal=false;
        world.ents[illegal].nativeIdentity.definition=excluded;
        check(preferred && world.nativeTeamAttackTarget(index,9)!=illegal,
            "the explicit LegalTarget=no counterexample excludes an otherwise preferred power target");
        world.ents[illegal].nativeIdentity.definition=original;
        const auto before=team.focus;team.advancePending=true;FILE* bad=tmpfile();
        check(bad && !world.saveGame(bad),"an impossible pending-advance state with a live target is rejected");if(bad)fclose(bad);
        team.advancePending=false;check(team.focus==before,"malformed-state verification preserves the actual target");
    } else check(false,"the source-based eight-member isolated fixture loads");
    index=fixture(4);
    if(index>=0 && !world.nativeEvents.teams[index].members.empty()) {
        auto& team=world.nativeEvents.teams[index];team.advancePending=false;team.lineStarted=false;
        const auto leader=team.members.back();const auto origin=world.ents[leader];Vec2i point{-1,-1};
        for(int radius=6;radius<=16 && point.x<0;++radius)for(int dy=-radius;dy<=radius && point.x<0;++dy)for(int dx=-radius;dx<=radius && point.x<0;++dx) {
            if(std::max(abs(dx),abs(dy))!=radius)continue;const int x=(int)origin.x+dx,y=(int)origin.y+dy;
            if(!world.map.surfacePassable(x,y,0,0,true,true))continue;
            bool distant=true;for(EID id:team.members)distant &= distf(world.ents[id].x,world.ents[id].y,x+.5f,y+.5f)>unitDef(world.ents[id]).weapon.range+2;
            std::vector<Vec2i> path;if(distant && world.map.findPath((int)origin.x,(int)origin.y,x,y,path,20000,0))point={x,y};
        }
        const EID target=point.x>=0?spawn("E1",point.x+.5f,point.y+.5f,0):INVALID_EID;
        if(world.valid(target)) {
            world.ents[target].invuln=100000;world.update();bool selected=team.focus==target;
            int minX=world.map.w,minY=world.map.h,maxX=0,maxY=0;
            for(EID id:team.members){const auto& e=world.ents[id];minX=std::min(minX,(int)e.x);minY=std::min(minY,(int)e.y);maxX=std::max(maxX,(int)e.x);maxY=std::max(maxY,(int)e.y);}
            std::vector<std::pair<int,bool>> barrier;
            for(int y=minY-1;y<=maxY+1;++y)for(int x=minX-1;x<=maxX+1;++x)if(x<minX || x>maxX || y<minY || y>maxY) {
                const int cell=world.cellIdx(x,y);barrier.emplace_back(cell,world.map.cells[cell].blocked);world.map.cells[cell].blocked=true;
            }
            world.orderStop(team.members);team.waitUntil=0;world.update();
            check(selected && team.alive && team.step==0 && team.focus==target && team.blockedScript==0 && resume(2),
                "a target selected before a route is sealed is retained across public attack rejection and a snapshot, without falsely completing the quarry");
            for(auto [cell,blocked]:barrier)world.map.cells[cell].blocked=blocked;
            for(int i=0;i<2*LOGIC_FPS && team.blockedScript==0;++i)world.update();
            check(team.alive && team.step==0 && team.focus==target && team.blockedScript==-1 && resume(1),
                "restoring the route lets the same real mission target resume through the shared path/order system");
        } else check(false,"the route-rejection arena has a reachable, out-of-range infantry target");
    } else check(false,"the route-rejection arena loads");
    index=fixture(4);
    if(index>=0) {
        auto& team=world.nativeEvents.teams[index];
        check(team.advancePending && team.step==0 && resume(1) && team.step==1,
            "a real empty-category scan advances on the following turn, and its pending transition survives a snapshot");
        world.update();world.update();check(!team.alive,"Guard(0) after the empty scan finishes the finite mechanism script");
    } else check(false,"empty-category fixture loads");
    index=fixture(10);
    check(index>=0 && world.nativeEvents.teams[index].blockedScript==0 && !world.nativeEvents.teams[index].lineStarted && resume(2),
        "uncalibrated RA2-only occupied-building quarry remains explicitly blocked instead of being reported as an empty completed search");
    index=fixture(4,true);
    if(index>=0) {
        auto& team=world.nativeEvents.teams[index];const auto leader=team.members.back();
        const EID target=spawn("E1",world.ents[leader].x+3,world.ents[leader].y,house);
        std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),0);
        check(world.nativeTeamAttackTarget(index,4)==INVALID_EID,"a player-owned campaign scan cannot discover unseen ground enemies");
        world.map.fog[0][world.cellIdx((int)world.ents[target].x,(int)world.ents[target].y)]=1;
        check(world.nativeTeamAttackTarget(index,4)==target,"previous discovery is sufficient without requiring present visibility");
        world.ents[target].camouflaged=true;
        check(world.nativeTeamAttackTarget(index,4)==INVALID_EID,"an undetected disguise is excluded from the automatic team scan");
    } else check(false,"player discovery fixture loads");

    for(auto replacement:{std::pair<const char*,const char*>{"TargetDistance = -10","TargetDistance = nan"},
        {"TargetLegal = 1","TargetLegal = 2"},{"TargetProfile = 1","TargetProfile = 0"}}) {
        auto source=initial.nativeScenarioSource;const auto at=source.find(replacement.first);bool rejected=at!=std::string::npos;
        if(rejected){source.replace(at,strlen(replacement.first),replacement.second);World invalid=initial;invalid.map.bldOccRef=&invalid.bldOcc;rejected=!invalid.restoreNativeDefinitions(source);}
        check(rejected,"malformed or missing source targeting metadata is rejected strictly");
    }
    {
        auto source=initial.nativeScenarioSource;const auto scene=source.find("[Scene]");
        const auto at=source.find("Version =",scene),end=source.find('\n',at);bool legacy=scene!=std::string::npos && at!=std::string::npos && end!=std::string::npos;
        if(legacy) {
            source.replace(at,end-at,"Version = 6");const auto file=(std::filesystem::path(outputDir)/"counter-legacy-scene6.ini").string();
            {std::ofstream output(file,std::ios::binary);output<<source;}legacy=load(file);
            const int team=legacy?world.createNativeTeam(assaultDefinition,false):-1;world.update();world.update();
            legacy &= team>=0;if(team>=0)legacy &= world.nativeEvents.teams[team].blockedScript==0 && !world.nativeEvents.teams[team].lineStarted;
        }
        check(legacy && resume(1),"older scene data without target profiles keeps category attack explicitly blocked and remains resumable");
    }

    if(shot.nativeEventDefinition && shot.valid(shotActor)) {
        world=shot;world.map.bldOccRef=&world.bldOcc;camZoom=.95f;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;
        sideMode=0;targetingSW=SWType::COUNT;sel={shotActor};selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);fogKnowledge={};syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen((int)world.ents[shotActor].x,(int)world.ents[shotActor].y,x,y);
            camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/("repaired-bridge-conscript-pursuit-"+std::to_string(size.x)+".png")).string();
            const auto checksum=world.checksum();render();check(checksum==world.checksum(),"the real source pursuit frame renders without changing simulation");
        }
    }
    std::printf("NATIVE_ATTACK failures=%d\n",failures);return failures;
}
