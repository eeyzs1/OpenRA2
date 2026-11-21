#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::aswSpawnerRegression(const char* outputDir) {
    if(contentResolve("assets/rules/profiles/ra2.ini").empty() || contentResolve("assets/rules/profiles/yr.ini").empty())return 77;
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    using P=World::AircraftPhase;
    const auto check=[&](bool ok,const char* text){std::printf("ASW %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto snapshot=[&](World& sample,int ticks=0){
        FILE* f=tmpfile();World copy;bool ok=f && sample.saveGame(f);
        if(ok){rewind(f);ok=copy.loadGame(f);}if(f)fclose(f);
        ok &= copy.checksum()==sample.checksum();
        for(int i=0;i<ticks && ok;++i){sample.update();copy.update();ok=sample.checksum()==copy.checksum();}return ok;
    };
    const auto capture=[&](const World& sample,const std::string& name){
        world=sample;world.map.bldOccRef=&world.bldOcc;resetMatchInteraction();resetNotifications();ais.clear();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        localPlayer=0;phase=::Phase::InGame;campaignMission=-1;gameOver=victory=paused=showMenu=netGame=false;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;SetTargetFPS(0);bakeTerrain();
        int x,y;tileToScreen(19,22,x,y);camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;clampCameraToMap();
        bool ok=true;shotAtDisplaySize=true;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){
            SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto filename=(std::filesystem::path(outputDir)/(name+"-"+std::to_string(size.x)+".png")).string();shotFile=filename;render();
            Image img=LoadImage(filename.c_str());ok &= img.data && img.width==size.x && img.height==size.y && world.checksum()==before;UnloadImage(img);
            for(EID id=0;id<(EID)world.ents.size();++id)if(world.managedAircraft(id)){
                const auto r=unitScreenRect(world.ents[id]);
                ok &= !world.managedAircraft(pickUnit((int)((r.x+r.width*.5f)*camZoom),(int)((r.y+r.height*.5f)*camZoom)));
            }
            doBoxSelect({0,0,(float)(SCREEN_W-sidebarW),(float)SCREEN_H},false);
            for(auto selected:sel)ok &= !world.managedAircraft(selected);
        }
        shotAtDisplaySize=false;sim=SimInput{};check(ok,"both display sizes render real assets and keep friendly ASW outside player selection");
    };
    for(const char* edition:{"fusion","ra2","yr"}){
        std::printf("ASW CASE %s\n",edition);const bool source=std::string(edition)!="fusion";
        const auto path=std::string("assets/rules/profiles/")+edition+".ini";World w;
        const bool loaded=w.init(64,64,153,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,source?path.c_str():nullptr);
        check(loaded,"the edition initializes with destroyer/ASW support");if(!loaded)continue;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        w.ents.clear();w.freeList.clear();w.nativeLaunchers.clear();w.nativeRockets.clear();
        w.effects.clear();w.projs.clear();w.crates.clear();
        std::fill(w.bldOcc.begin(),w.bldOcc.end(),-1);std::fill(w.unitOccHead.begin(),w.unitOccHead.end(),INVALID_EID);std::fill(w.airOccHead.begin(),w.airOccHead.end(),INVALID_EID);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Water;}
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        for(auto& player:w.players){player.active=true;player.defeated=false;}w.cratesEnabled=false;
        const EID owner=w.spawnUnit(0,UnitType::Destroyer,14.5f,22.5f),sub=w.spawnUnit(1,UnitType::Typhoon,19.5f,22.5f);
        check(w.valid(owner) && w.valid(sub),"the ordinary source destroyer and submarine spawn");if(!w.valid(owner)||!w.valid(sub))continue;
        w.ents[sub].tpSick=5000;w.ents[sub].guard=false;w.ents[owner].guard=false;
        check(w.hasAircraftSpawner(owner) && w.aircraftCapacity(owner)==1 && w.dockedAircraft(owner,true)==1
            && w.ents[owner].cargo[0].type==UnitType::ASW && w.ents[owner].cargo[0].hp==30 && w.ents[owner].cargo[0].ammo==1
            && w.aircraftSpawners.front().regenerateTicks==800 && w.aircraftSpawners.front().reloadTicks==300,
            "the single 30-HP Osprey slot has original 400/150-frame loss and reload timers");
        if(source){const auto& d=*w.ents[owner].nativeIdentity.definition;
            check(d.guns.slots[1].id=="ASWLauncher" && d.guns.slots[1].weapon.range==-2 && !d.guns.slots[1].weapon.antiGround,
                "the original virtual secondary retains its negative sentinel and AG=no source flags");
            World staged=w;staged.map.bldOccRef=&staged.bldOcc;const auto before=staged.checksum();auto malformed=w.nativeScenarioSource;
            const auto section=malformed.find("[NativeAircraft.1]"),slot=malformed.find("WeaponSlot = 1",section);
            bool refused=slot!=std::string::npos;
            if(refused){malformed[slot+13]='0';refused=!staged.restoreNativeDefinitions(malformed) && staged.checksum()==before;}
            check(refused,"an ASW controller cannot be assigned the surface primary slot or partially replace active definitions");
        }
        check(snapshot(w,1),"a docked source aircraft and its definition survive continued save/load");
        const World initial=w;capture(w,std::string(edition)+"-ready");
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={owner};attack.a=sub;w.applyCmd(0,attack);w.update();
        const auto planeRef=w.aircraftSpawners.front().nodes.front().aircraft;const EID plane=planeRef.id;
        check(w.valid(planeRef) && w.managedAircraft(plane) && w.ents[plane].utype==UnitType::ASW && w.ents[plane].hp==30
            && w.dockedAircraft(owner)==0 && w.ents[sub].hp==600,
            "the ordinary Attack command launches an actual aircraft without direct hull damage");if(!w.valid(planeRef))continue;
        if(source)check(unitDef(w.ents[plane]).weapon.damage==50 && unitDef(w.ents[plane]).weapon.range==3
            && w.ents[plane].nativeIdentity.definition->navalTargeting==2,
            "the source aircraft uses the original 50-damage, range-3 depth charge");
        check(snapshot(w,1),"airborne ASW state and generations survive continued save/load");
        const World flight=w;capture(w,std::string(edition)+"-sortie");
        World continuous=flight;continuous.map.bldOccRef=&continuous.bldOcc;int rearmedAt=-1,relaunchedAt=-1;
        for(int i=0;i<1000 && relaunchedAt<0;++i){
            const auto old=continuous.aircraftSpawners.front().nodes.front();continuous.update();
            const auto& node=continuous.aircraftSpawners.front().nodes.front();
            if(old.phase==P::Reloading && old.remaining==1 && rearmedAt<0)rearmedAt=i;
            if(continuous.valid(node.aircraft) && node.aircraft!=planeRef)relaunchedAt=i;
        }
        std::printf("ASW CADENCE edition=%s rearmed=%d launched=%d extra_wait=%d\n",edition,rearmedAt,relaunchedAt,relaunchedAt-rearmedAt);
        check(rearmedAt>=0 && relaunchedAt>=rearmedAt && relaunchedAt<=rearmedAt+1
            && (!source || continuous.nativeProductionSerial==flight.nativeProductionSerial) && snapshot(continuous,3),
            "a continuously ordered destroyer relaunches its existing ASW as soon as reload completes, without an empty-slot cooldown penalty");
        World::Cmd forged{};forged.type=World::Cmd::Move;forged.ids={plane};forged.x=40;forged.y=40;
        const auto before=w.checksum();w.applyCmd(0,forged);check(w.checksum()==before,"forged player orders cannot take over a managed ASW aircraft");
        check(w.attackRejection(plane,owner)==AttackRejectReason::InvalidTarget
            && w.effWeapon(w.ents[plane],&w.ents[owner]).damage==0,"ASW does not attack surface ships");
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={owner};move.x=16.5f;move.y=23.5f;w.applyCmd(0,move);
        bool cannon=false,fired=false,damage=false,returned=false;
        for(int i=0;i<600 && !returned;++i){
            w.update();for(const auto& shot:w.projs)if(shot.alive){cannon |= shot.src==owner;fired |= shot.src==plane;}
            damage |= w.ents[sub].hp==550;
            returned=w.aircraftSpawners.front().nodes.front().phase==P::Reloading;
        }
        World resolved=w;resolved.map.bldOccRef=&resolved.bldOcc;
        for(int i=0;i<80 && !damage;++i){resolved.update();damage |= resolved.ents[sub].hp==550;}
        if(!(fired && damage && !cannon && returned))std::printf("ASW DIAGNOSTIC fired=%d damage=%d cannon=%d returned=%d hp=%d owner_x=%.2f\n",fired,damage,cannon,returned,resolved.ents[sub].hp,w.ents[owner].x);
        check(fired && damage && !cannon && returned && w.ents[owner].x>14.5f,
            "ASW deals one real 50-damage depth charge, then returns to its moving destroyer without cannon fire");
        if(returned){
            World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={owner};w.applyCmd(0,stop);
            w.kill(sub,false); // target combat is verified in the projectile-resolution copy above
            auto& node=w.aircraftSpawners.front().nodes.front();const int remaining=node.remaining;
            check(remaining==300 && w.ents[owner].cargo[node.cargo].ammo==0 && snapshot(w),"landing stores spent ammunition for the full original reload delay");
            for(int i=1;i<remaining;++i)w.update();
            check(node.phase==P::Reloading && node.remaining==1 && w.dockedAircraft(owner,true)==0 && snapshot(w),"reload work is not completed early and the final tick is persisted");
            w.update();check(node.phase==P::Docked && w.dockedAircraft(owner,true)==1 && w.ents[owner].cargo[node.cargo].ammo==1 && snapshot(w),"only completed service restores launch readiness");
        }
        World loss=flight;loss.map.bldOccRef=&loss.bldOcc;const auto oldIdentity=loss.ents[plane].nativeIdentity.object;
        loss.kill(plane,false);loss.kill(sub,false);loss.orderStop({owner});
        for(int i=0;i<799;++i)loss.update();
        check(loss.aircraftSpawners.front().nodes.front().phase==P::Dead && loss.aircraftSpawners.front().nodes.front().remaining==1
            && loss.dockedAircraft(owner)==0 && snapshot(loss),"a destroyed ASW slot waits all 800 simulation ticks before replacement");
        loss.update();check(loss.dockedAircraft(owner,true)==1 && (!source || loss.ents[owner].cargo[0].nativeIdentity.object!=oldIdentity),"regeneration creates a fresh logical aircraft only after the complete delay");
        if(source){World exhausted=initial;exhausted.map.bldOccRef=&exhausted.bldOcc;exhausted.nativeProductionSerial=std::numeric_limits<uint64_t>::max();
            exhausted.applyCmd(0,attack);exhausted.update();check(exhausted.valid(exhausted.aircraftSpawners.front().nodes.front().aircraft) && snapshot(exhausted),"launching an existing ASW identity does not consume a new production serial");}
        World captured=flight;captured.map.bldOccRef=&captured.bldOcc;captured.ents[owner].player=1;captured.update();
        const auto& capturedNode=captured.aircraftSpawners.front().nodes.front();
        const bool recalled=captured.valid(planeRef)?(captured.ents[plane].player==1 && captured.ents[plane].state==UState::Returning)
            :(capturedNode.phase==P::Reloading && capturedNode.cargo>=0 && captured.ents[owner].cargo[capturedNode.cargo].nativeIdentity.object==flight.ents[plane].nativeIdentity.object);
        check(captured.aircraftSpawners.front().player==1 && recalled && snapshot(captured),"ownership changes transfer and recall the same logical ASW aircraft, including an immediate landing");
        World destroyed=flight;destroyed.map.bldOccRef=&destroyed.bldOcc;destroyed.kill(owner,false);
        const EID replacement=destroyed.spawnUnit(0,UnitType::Destroyer,14.5f,22.5f);destroyed.update();
        check(destroyed.valid(replacement) && !destroyed.valid(planeRef) && destroyed.dockedAircraft(replacement,true)==1 && snapshot(destroyed),"a reused destroyer slot cannot adopt its predecessor's orphaned ASW aircraft");
        World surface=initial;surface.map.bldOccRef=&surface.bldOcc;
        const EID ship=surface.spawnUnit(1,UnitType::Destroyer,18.5f,22.5f);surface.ents[ship].tpSick=5000;surface.ents[ship].guard=false;
        attack.a=ship;surface.applyCmd(0,attack);bool surfaceCannon=false,aswLaunched=false;
        for(int i=0;i<100;++i){surface.update();for(const auto& shot:surface.projs)surfaceCannon |= shot.alive && shot.src==owner;
            aswLaunched |= surface.dockedAircraft(owner)!=1;}
        check(surfaceCannon && !aswLaunched && snapshot(surface),"surface targets use the primary cannon while the ASW slot stays docked");
        World land=initial;land.map.bldOccRef=&land.bldOcc;land.map.at(18,22).terrain=Terrain::Clear;
        const EID tank=land.spawnUnit(1,UnitType::Grizzly,18.5f,22.5f);land.ents[tank].tpSick=5000;
        attack.a=tank;land.applyCmd(0,attack);bool landCannon=false;
        for(int i=0;i<100;++i){land.update();for(const auto& shot:land.projs)landCannon |= shot.alive && shot.src==owner;}
        check(landCannon && land.dockedAircraft(owner)==1 && snapshot(land),"land targets also use the cannon while ASW remains unavailable for land attacks");
        World changed=flight;changed.map.bldOccRef=&changed.bldOcc;
        const EID newTarget=changed.spawnUnit(1,UnitType::Destroyer,18.5f,22.5f);changed.ents[newTarget].tpSick=5000;
        attack.a=newTarget;changed.applyCmd(0,attack);changed.update();const auto changedPhase=changed.aircraftSpawners.front().nodes.front().phase;
        check((changedPhase==P::Returning || changedPhase==P::Reloading) && snapshot(changed),"switching the parent to a surface target recalls ASW instead of assigning an illegal attack");
        World lost=flight;lost.map.bldOccRef=&lost.bldOcc;lost.kill(sub,false);lost.update();const auto lostPhase=lost.aircraftSpawners.front().nodes.front().phase;
        check((lostPhase==P::Returning || lostPhase==P::Reloading) && snapshot(lost),"destroying the submarine recalls ASW and preserves the return through a snapshot");
        World aa=flight;aa.map.bldOccRef=&aa.bldOcc;
        aa.ents[plane].tpSick=1000; // keep the real aircraft aloft through normal AA acquisition; retain its natural HP/armor
        const auto setLand=[&](int x,int y,int width,int height){for(int cy=y;cy<y+height;++cy)for(int cx=x;cx<x+width;++cx)aa.map.at(cx,cy).terrain=Terrain::Clear;};
        setLand(12,8,4,4);setLand(18,18,3,3);
        const int power=source?aa.rulesTypeIndex(0,false,(int)BldType::PowerPlant):-1;
        const int patriotType=source?aa.rulesTypeIndex(0,false,(int)BldType::PatriotMissile):-1;
        aa.spawnBuilding(1,BldType::PowerPlant,12,8,true,power);const EID patriot=aa.spawnBuilding(1,BldType::PatriotMissile,18,18,true,patriotType);
        bool intercepted=false;for(int i=0;i<240 && aa.valid(planeRef);++i){aa.update();for(const auto& shot:aa.projs)intercepted |= shot.alive && shot.src==patriot && shot.target==plane;}
        check(intercepted && !aa.valid(planeRef) && aa.dockedAircraft(owner)==0 && snapshot(aa),"a powered enemy Patriot shoots down the real 30-HP ASW and starts persisted regeneration");
        World bad=flight;bad.map.bldOccRef=&bad.bldOcc;bad.aswEnabled=false;FILE* invalid=tmpfile();bool refused=invalid && !bad.saveGame(invalid);if(invalid)fclose(invalid);
        check(refused,"an ASW world cannot be saved with the historical behavior flag");
        FILE* bytesFile=tmpfile();bool malformedFlag=false;
        if(bytesFile && flight.saveGame(bytesFile)){
            const auto length=ftell(bytesFile);std::vector<uint8_t> bytes((size_t)length);rewind(bytesFile);fread(bytes.data(),1,bytes.size(),bytesFile);
            if(const auto range=locateWorldSaveSection(bytes,WorldSaveSection::ASW)){
                bytes[range->offset+4]=2;FILE* altered=tmpfile();World rejected;
                if(altered){fwrite(bytes.data(),1,bytes.size(),altered);rewind(altered);malformedFlag=!rejected.loadGame(altered);fclose(altered);}
            }
        }
        if(bytesFile)fclose(bytesFile);check(malformedFlag,"malformed persisted ASW behavior flags are rejected at the read boundary");
    }
    const auto old=contentResolve("review_out/project-review-20261115/legacy-world50-scene19.sav");
    if(!old.empty()){Game legacy;const bool loaded=legacy.loadGameFile(old.c_str());g_script.shutdown();
        check(loaded && legacy.world.aircraftSpawnersEnabled && !legacy.world.aswEnabled && legacy.world.nativeAircraftDefinitions.size()==1 && snapshot(legacy.world,20),
            "an actual World50/Scene19 application save keeps its carrier controller and historical destroyer behavior");}
    std::printf("ASW failures=%d\n",failures);return failures;
}
