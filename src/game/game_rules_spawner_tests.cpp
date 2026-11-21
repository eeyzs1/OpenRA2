#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::rulesSpawnerRegression(const char* outputDir) {
    if(contentResolve("assets/rules/profiles/ra2.ini").empty() || contentResolve("assets/rules/profiles/yr.ini").empty()) {
        std::printf("RULES_SPAWNER SKIP: export the original profiles\n");return 77;
    }
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("RULES_SPAWNER %s: %s\n",ok?"PASS":"FAIL",message);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();
    const auto capture=[&](const World& sample,EID launcher,EID missile,const std::string& name) {
        world=sample;world.map.bldOccRef=&world.bldOcc;resetMatchInteraction();resetNotifications();ais.clear();
        // A presentation fixture shows both source models and the destination;
        // the simulated match and its ordinary sight remain untouched.
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=paused=showMenu=netGame=false;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;SetTargetFPS(0);bakeTerrain();
        const bool art=world.valid(missile) && nativeActorSprite(world.ents[launcher]).tex.id && nativeActorSprite(world.ents[missile]).tex.id;
        int x,y;tileToScreen(21,22,x,y);camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;clampCameraToMap();
        bool exact=art;shotAtDisplaySize=true;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto file=(std::filesystem::path(outputDir)/(name+"-"+std::to_string(size.x)+".png")).string();shotFile=file;render();
            Image picture=LoadImage(file.c_str());exact &= picture.data && picture.width==size.x && picture.height==size.y && world.checksum()==before;UnloadImage(picture);
        }
        shotAtDisplaySize=false;sim=SimInput{};check(exact,"independent source launcher/missile art renders at both display sizes without changing simulation");
    };
    const auto resume=[&](World& sample,int frames) {
        FILE* file=tmpfile();World restored;bool exact=file && sample.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= sample.checksum()==restored.checksum();
        for(int i=0;i<frames && exact;++i){sample.update();restored.update();exact &= sample.checksum()==restored.checksum();}
        return exact;
    };
    for(const char* edition:{"ra2","yr"})for(int index=0;index<(std::string(edition)=="yr"?3:2);++index) {
        const std::string path=std::string("assets/rules/profiles/")+edition+".ini";
        World sample;const bool yuri=index==2;
        const bool loaded=sample.init(64,64,135,1,1,{yuri?Faction::Yuri:Faction::Soviet,Faction::Allies},0,nullptr,false,false,path.c_str());
        check(loaded && sample.nativeRocketDefinitions.size()==(std::string(edition)=="yr"?3u:2u),"ordinary rules contain all edition-specific missile launchers");
        if(!loaded || index>=(int)sample.nativeRocketDefinitions.size())continue;
        for(EID id=0;id<(EID)sample.ents.size();++id)if(sample.valid(id))sample.kill(id,false);
        sample.ents.clear();sample.freeList.clear();sample.nativeLaunchers.clear();sample.nativeRockets.clear();
        std::fill(sample.bldOcc.begin(),sample.bldOcc.end(),-1);std::fill(sample.unitOccHead.begin(),sample.unitOccHead.end(),INVALID_EID);std::fill(sample.airOccHead.begin(),sample.airOccHead.end(),INVALID_EID);
        for(auto& cell:sample.map.cells)cell=Cell{};
        for(auto& player:sample.players){player.active=true;player.defeated=false;}
        sample.cratesEnabled=false;const auto definition=sample.nativeRocketDefinitions[index];
        const auto type=sample.nativeTypes[definition.launcher]->unit.type;
        if(unitDef(type).isNaval())for(int y=18;y<26;++y)for(int x=8;x<20;++x)sample.map.at(x,y).terrain=Terrain::Water;
        const EID launcher=sample.spawnUnit(0,type,14.5f,22.5f,definition.launcher);
        const EID target=sample.spawnBuilding(1,BldType::OreRefinery,26,22,true);
        for(auto& fog:sample.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        check(sample.valid(launcher) && sample.valid(target) && sample.nativeDockedSpawns(launcher)==definition.capacity,
            "ordinary source construction initializes the original missile magazine");
        if(!sample.valid(launcher) || !sample.valid(target))continue;
        const auto beforeActors=sample.ents.size(),generation=sample.ents[launcher].generation;const int beforeHp=sample.ents[target].hp;
        const auto rejection=sample.attackRejection(launcher,target);
        if(rejection!=AttackRejectReason::None)std::printf("RULES_SPAWNER REJECT launcher=%s reason=%d navalOnly=%d\n",sample.nativeTypes[definition.launcher]->id.c_str(),(int)rejection,sample.effWeapon(sample.ents[launcher]).navalOnly);
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={launcher};attack.a=target;sample.applyCmd(0,attack);sample.update();
        EID missile=INVALID_EID;for(const auto& rocket:sample.nativeRockets)if(rocket.source==launcher)missile=rocket.actor;
        if(!sample.valid(missile) || sample.ents[missile].hp!=50 || unitDef(sample.ents[missile]).armor!=Armor::Special2)
            std::printf("RULES_SPAWNER DIAG launcher=%s state=%d target=%d rockets=%zu missile=%d hp=%d armor=%d nodes=%d guns=%d\n",sample.nativeTypes[definition.launcher]->id.c_str(),(int)sample.ents[launcher].state,sample.ents[launcher].target,sample.nativeRockets.size(),missile,sample.valid(missile)?sample.ents[missile].hp:-1,sample.valid(missile)?(int)unitDef(sample.ents[missile]).armor:-1,sample.nativeDockedSpawns(launcher),sample.hasNativeGuns(sample.ents[launcher]));
        check(sample.valid(missile) && sample.ents[missile].nativeIdentity.definition->nativeRocket
            && sample.ents[missile].hp==50 && unitDef(sample.ents[missile]).armor==Armor::Special2
            && sample.ents.size()>beforeActors && sample.ents[launcher].generation==generation,
            "the normal attack command creates a real source missile while preserving launcher identity");
        if(!sample.valid(missile))continue;
        sample.orderStop({launcher});sample.ents[launcher].nativeIdentity.sleeping=true;
        const std::string name=std::string(edition)+"-"+sample.nativeTypes[definition.launcher]->id;
        capture(sample,launcher,missile,name+"-pause");
        World defended=sample;defended.map.bldOccRef=&defended.bldOcc;
        defended.spawnBuilding(1,BldType::PowerPlant,32,18,true);const EID patriot=defended.spawnBuilding(1,BldType::PatriotMissile,10,15,true);
        bool intercepted=false;
        for(int tick=0;tick<260 && defended.valid(missile);++tick) {
            defended.update();for(const auto& shot:defended.projs)intercepted |= shot.alive && shot.src==patriot && shot.target==missile;
        }
        check(intercepted && !defended.valid(missile) && defended.valid(target) && defended.ents[target].hp==beforeHp,
            "a powered source Patriot targets and destroys the real missile before it damages its destination");
        check(resume(sample,20),"ordinary-map pause/tilt state and missile identities survive a snapshot");
        for(int i=0;i<500 && sample.valid(missile);++i){sample.update();bool flying=false;for(const auto& rocket:sample.nativeRockets)if(rocket.actor==missile)flying=rocket.phase==NativeRocketPhase::Flight;if(flying)break;}
        check(sample.valid(missile) && resume(sample,40),"ordinary-map accelerating flight resumes deterministically");
        if(sample.valid(missile))failures+=nativeGroundImpactRegression(sample,missile,
            (std::filesystem::path(outputDir)/(name+"-ground")).string().c_str());
        for(int tick=0;tick<80 && sample.valid(missile);++tick)sample.update();
        if(sample.valid(missile))capture(sample,launcher,missile,name+"-flight");
        for(int i=0;i<5000 && sample.valid(missile);++i)sample.update();
        check(!sample.valid(missile) && (!sample.valid(target) || sample.ents[target].hp<beforeHp),"the original missile reaches its captured ground destination and applies its payload");
        if(yuri) {
            sample.ents[launcher].nativeIdentity.sleeping=false;sample.orderStop({launcher});const EID ship=sample.spawnUnit(1,UnitType::Destroyer,18.5f,22.5f);
            const int health=sample.ents[ship].hp;bool torpedo=false;
            attack.a=ship;sample.applyCmd(0,attack);
            for(int tick=0;tick<180 && sample.valid(ship) && sample.ents[ship].hp==health;++tick) {
                sample.update();for(const auto& shot:sample.projs)torpedo |= shot.alive && shot.src==launcher && shot.w.nativeSlot%2==0;
            }
            bool cruise=false;for(const auto& rocket:sample.nativeRockets)cruise |= sample.valid(rocket.actor) && rocket.source==launcher;
            check(torpedo && !cruise && (!sample.valid(ship) || sample.ents[ship].hp<health),"the source Boomer selects its original torpedo for ships and reserves the secondary spawner for ground attacks");
            check(resume(sample,20),"ordinary Boomer torpedo projectiles and their source weapon references survive a snapshot");
        }
        std::printf("RULES_SPAWNER CASE edition=%s launcher=%s damage=%d altitude=%d tick=%llu\n",edition,sample.nativeTypes[definition.launcher]->id.c_str(),definition.damage,definition.altitude,(unsigned long long)sample.tick);
    }
    std::printf("RULES_SPAWNER failures=%d\n",failures);return failures;
}
