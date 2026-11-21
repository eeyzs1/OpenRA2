#include "game_regression.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>

int GameRegression::nativePlaythroughRegression(const char* outputDir) {
    ScopedUserFile progress("userdata/campaign_progress.ini");
    if(!progress.ready()){std::fprintf(stderr,"NATIVE_PLAYTHROUGH: cannot preserve campaign progress\n");return 1;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_PLAYTHROUGH %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    // This checkpoint is written by the real source-opening/paid-production/
    // crate journey before its independent counter-fixtures begin.
    const int preparation=nativeCrateRegression(outputDir);
    check(preparation==0,"the original opening, paid army, bridge repair and four real crate pickups still work");
    if(preparation)return failures;
    const auto checkpoint=(std::filesystem::path(outputDir)/"native-crates-collected.sav").string();
    if(!loadGameFile(checkpoint.c_str())){check(false,"the actual journey checkpoint loads");return failures;}
    const auto source=[&](const std::string& key){for(EID id=0;id<(EID)world.ents.size();++id)
        if(world.valid(id) && world.ents[id].nativeIdentity.object==key)return id;return INVALID_EID;};
    const auto fired=[&](const char* name){const int definition=world.nativeTriggerIndex(name);uint64_t count=0;
        for(const auto& t:world.nativeEvents.triggers)if(t.definition==definition)count+=t.firings;return count;};
    const auto visible=[&](const World::Ent& e){const int width=e.isBuilding?bldDef(e).w:1,height=e.isBuilding?bldDef(e).h:1;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(world.map.fogAt(0,(int)e.x+x,(int)e.y+y)==FOG_VISIBLE)return true;return false;};
    const auto attack=[&](std::vector<EID> actors,EID target){if(actors.empty() || !world.valid(target))return;
        World::Cmd c;c.type=World::Cmd::Attack;c.ids=std::move(actors);c.a=target;world.applyCmd(0,c);};
    const auto move=[&](std::vector<EID> actors,Vec2i point,bool aggressive=false){if(actors.empty() || !world.map.inBounds(point.x,point.y))return;
        World::Cmd c;c.type=World::Cmd::Move;c.ids=std::move(actors);c.x=point.x+.5f;c.y=point.y+.5f;c.attackMove=aggressive;world.applyCmd(0,c);};
    const auto heroKey=std::string("infantry:0");EID hero=source(heroKey);std::vector<std::string> ships;
    for(const auto& e:world.ents)if(e.alive && e.nativeIdentity.definition && e.nativeIdentity.definition->id=="DRED" && e.player==2)ships.push_back(e.nativeIdentity.object);
    check(world.valid(hero) && ships.size()==4 && world.nativeMissionOutcome(0)==MatchOutcome::Running
        && unitDef(world.ents[hero]).isInfantry() && unitDef(world.ents[hero]).pathDomain()==2,
        "the actual original Tanya is an infantry swimmer and all four original DREDs remain for normal commands");
    if(!world.valid(hero) || ships.size()!=4)return failures;
    bool provenance=true;for(const auto& e:world.ents)if(e.alive && e.player==0 && !e.isBuilding)
        provenance &= e.nativeIdentity.definition && e.nativeIdentity.object.find("counter:")==std::string::npos
            && e.nativeIdentity.object.rfind("condition:",0)!=0;
    check(provenance,"the loaded main journey contains source/produced player units rather than counter-fixture actors");
    const uint64_t start=world.tick;uint64_t waterTicks=0;size_t remaining=ships.size();bool swimFrame=false,swimShots=false;
    for(int step=0;step<7000 && remaining && world.nativeMissionOutcome(0)==MatchOutcome::Running;++step) {
        hero=source(heroKey);if(!world.valid(hero))break;
        if(world.tick%15==0) {
            EID best=INVALID_EID;float distance=std::numeric_limits<float>::max();
            for(const auto& key:ships){const EID id=source(key);if(!world.valid(id) || !visible(world.ents[id]))continue;
                const float d=distf(world.ents[hero].x,world.ents[hero].y,world.ents[id].x,world.ents[id].y);
                if(d<distance){distance=d;best=id;}}
            if(world.valid(best)) {if(world.ents[hero].target!=best)attack({hero},best);}
            else if(world.ents[hero].state!=UState::Moving)move({hero},world.nativeWaypoint(36));
        }
        logic();hero=source(heroKey);
        if(world.valid(hero) && world.map.at((int)world.ents[hero].x,(int)world.ents[hero].y).terrain==Terrain::Water) {
            ++waterTicks;
            if(!swimFrame){const auto saved=(std::filesystem::path(outputDir)/"normal-tanya-swimming.sav").string();
                swimFrame=saveGameFile(saved.c_str());std::printf("NATIVE_PLAYTHROUGH SWIM tick=%llu hp=%d pos=%.3f,%.3f saved=%d\n",(unsigned long long)world.tick,world.ents[hero].hp,world.ents[hero].x,world.ents[hero].y,swimFrame);}
            bool openWater=!world.map.hasDeck((int)world.ents[hero].x,(int)world.ents[hero].y);
            for(EID id=0;id<(EID)world.ents.size();++id)if(id!=hero && world.valid(id) && !world.ents[id].isBuilding)
                openWater &= distf(world.ents[hero].x,world.ents[hero].y,world.ents[id].x,world.ents[id].y)>3.5f;
            if(!swimShots && waterTicks>=20 && openWater && std::abs(world.ents[hero].x-world.ents[hero].px)+std::abs(world.ents[hero].y-world.ents[hero].py)>1e-5f) {
                swimShots=true;sel={hero};selBuilding=-1;clearPlacement();interpAlpha=1;camZoom=1;sim=SimInput{};sim.active=true;sim.pos={-100,-100};syncFogKnowledge();bakeTerrain();
                std::printf("NATIVE_PLAYTHROUGH SWIM_VIEW tick=%llu pos=%.3f,%.3f layer=%d moving=%d\n",(unsigned long long)world.tick,world.ents[hero].x,world.ents[hero].y,world.ents[hero].navLayer,world.ents[hero].walkAnim);
                const auto saved=(std::filesystem::path(outputDir)/"normal-tanya-swimming-view.sav").string();
                check(saveGameFile(saved.c_str()),"the actual moving-in-water checkpoint saves without pose or visibility overrides");
                for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                    SetWindowSize(size.x,size.y);render();const auto p=unitScreenPos(world.ents[hero]);camX+=p.x-(SCREEN_W-sidebarW)/2;camY+=p.y-SCREEN_H/2;
                    const auto filename=(std::filesystem::path(outputDir)/("normal-tanya-swimming-"+std::to_string(size.x)+".png")).string();
                    const auto hash=world.checksum();shotAtDisplaySize=true;shotFile=filename;render();Image frame=LoadImage(filename.c_str());
                    check(frame.data && frame.width==size.x && frame.height==size.y && world.checksum()==hash,
                        "the normal-command swim framebuffer has the requested dimensions and leaves simulation unchanged");UnloadImage(frame);
                }
            }
        }
        const size_t now=(size_t)std::count_if(ships.begin(),ships.end(),[&](const auto& key){return world.valid(source(key));});
        if(now!=remaining){remaining=now;std::printf("NATIVE_PLAYTHROUGH SHIPS tick=%llu remaining=%zu hero_hp=%d\n",(unsigned long long)world.tick,remaining,world.valid(hero)?world.ents[hero].hp:0);std::fflush(stdout);}
    }
    // Elimination is polled at the next general event pass, after the lethal
    // object update that removed the last ship.
    if(!remaining && fired("07B92F3C")==0 && world.nativeMissionOutcome(0)==MatchOutcome::Running)logic();
    check(!remaining && waterTicks>0 && swimFrame && swimShots && world.valid(source(heroKey)) && fired("07B92F3C")==1,
        "the original Tanya traverses actual water and sinks all four original ships, naturally completing Objective 1");
    if(remaining || !world.valid(source(heroKey)) || world.nativeMissionOutcome(0)!=MatchOutcome::Running) {
        std::printf("NATIVE_PLAYTHROUGH STOP stage=ships tick=%llu remaining=%zu outcome=%d hero=%d\n",(unsigned long long)world.tick,remaining,(int)world.nativeMissionOutcome(0),source(heroKey));return failures+1;
    }
    const auto shipsSave=(std::filesystem::path(outputDir)/"normal-four-ships-sunk.sav").string();
    check(saveGameFile(shipsSave.c_str()),"the real four-ship completion checkpoint saves without helper actors");
    uint64_t lastBuildings=0;int idle=0;EID objective=INVALID_EID;
    for(int step=0;step<18000 && world.nativeMissionOutcome(0)==MatchOutcome::Running;++step) {
        hero=source(heroKey);if(!world.valid(hero))break;
        if(world.tick%15==0) {
            const auto& tanya=world.ents[hero];EID threat=-1;float near=7;
            std::vector<EID> army;
            for(EID id=0;id<(EID)world.ents.size();++id){const auto& e=world.ents[id];if(!e.alive || e.isBuilding || id==hero)continue;
                if(e.player==0 && !unitDef(e).canHarvet() && world.effWeapon(e).damage>0)army.push_back(id);
                if(!world.isEnemy(0,e.player) || !visible(e) || unitDef(e).isNaval() || unitDef(e).isAir() || world.attackRejection(hero,id)!=AttackRejectReason::None)continue;
                const float d=distf(tanya.x,tanya.y,e.x,e.y);if(d<near){near=d;threat=id;}}
            if(world.valid(threat)) {
                if(tanya.target!=threat)attack({hero},threat);
                std::vector<EID> ready;for(EID id:army)if(world.ents[id].target!=threat)ready.push_back(id);attack(std::move(ready),threat);
            } else {
                if(!world.valid(objective) || !visible(world.ents[objective])) {
                    objective=-1;float best=std::numeric_limits<float>::max();
                    for(EID id=0;id<(EID)world.ents.size();++id){const auto& e=world.ents[id];
                        if(!e.alive || !e.isBuilding || e.player!=1 || !visible(e) || !e.nativeIdentity.definition
                            || e.nativeIdentity.definition->immune || !e.nativeIdentity.definition->significant || world.attackRejection(hero,id)!=AttackRejectReason::None)continue;
                        const float d=distf(tanya.x,tanya.y,e.x,e.y);const float score=d-(bldDef(e).weapon.damage>0?5.f:0.f);
                        if(score<best){best=score;objective=id;}}
                }
                if(world.valid(objective)) {
                    if(tanya.target!=objective)attack({hero},objective);
                    std::vector<EID> escort;for(EID id:army)if(distf(world.ents[id].x,world.ents[id].y,tanya.x,tanya.y)>5 && world.ents[id].state!=UState::Moving)escort.push_back(id);
                    move(std::move(escort),{(int)tanya.x,(int)tanya.y},true);
                } else if(tanya.state!=UState::Moving)move({hero},world.nativeWaypoint(48));
            }
        }
        logic();uint64_t buildings=0;for(const auto& e:world.ents)buildings+=e.alive && e.player==1 && e.isBuilding && e.nativeIdentity.definition && e.nativeIdentity.definition->significant;
        if(buildings!=lastBuildings || world.tick%900==0){lastBuildings=buildings;
            std::printf("NATIVE_PLAYTHROUGH BASE tick=%llu remaining=%llu hero_hp=%d objective=%d outcome=%d\n",(unsigned long long)world.tick,(unsigned long long)buildings,world.valid(source(heroKey))?world.ents[source(heroKey)].hp:0,objective,(int)world.nativeMissionOutcome(0));std::fflush(stdout);}
        if(!buildings)++idle;else idle=0;if(idle>400 && world.nativeMissionOutcome(0)==MatchOutcome::Running)break;
    }
    hero=source(heroKey);
    const bool won=world.valid(hero) && world.nativeMissionOutcome(0)==MatchOutcome::Victory && gameOver && victory
        && fired("07B92F3C")==1 && fired("07506C5C")==1 && fired("075F22EC")==1;
    check(won,"normal original-Tanya/paid-army commands clear the original Soviet base and reach the author's victory action");
    check(won && campaignMission>=0 && missionTable()[campaignMission].nativeScene=="assets/maps/native/all01t/scene.ini"
        && campaignObjDone.size()==4 && std::all_of(campaignObjDone.begin(),campaignObjDone.end(),[](bool done){return done;})
        && campaignProgress(missionTable()[campaignMission].lineId)>=1,
        "the installed campaign entry completes its four HUD objectives and records progress after this actual command-driven victory");
    const auto final=(std::filesystem::path(outputDir)/(won?"normal-authored-victory.sav":"normal-playthrough-blocked.sav")).string();
    check(saveGameFile(final.c_str()),"the actual normal-command playthrough result is reviewable as an application save");
    std::printf("NATIVE_PLAYTHROUGH RESULT won=%d begin=%llu end=%llu water_ticks=%llu hero_hp=%d failures=%d\n",won,(unsigned long long)start,(unsigned long long)world.tick,(unsigned long long)waterTicks,world.valid(hero)?world.ents[hero].hp:0,failures);
    return failures;
}
