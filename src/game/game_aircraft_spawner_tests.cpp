#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <limits>

int GameRegression::aircraftSpawnerRegression(const char* outputDir) {
    if(contentResolve("assets/rules/profiles/ra2.ini").empty() || contentResolve("assets/rules/profiles/yr.ini").empty())return 77;
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    using Phase=World::AircraftPhase;
    const auto check=[&](bool ok,const char* message){std::printf("AIRCRAFT %s: %s\n",ok?"PASS":"FAIL",message);std::fflush(stdout);failures+=!ok;};
    const auto clone=[&](World& sample,int ticks) {
        FILE* f=tmpfile();World copy;bool exact=f && sample.saveGame(f);
        if(exact){rewind(f);exact=copy.loadGame(f);}if(f)fclose(f);
        exact &= sample.checksum()==copy.checksum();
        for(int i=0;i<ticks && exact;++i){sample.update();copy.update();exact=sample.checksum()==copy.checksum();}
        return exact;
    };
    const auto capture=[&](const World& sample,const std::string& name) {
        world=sample;world.map.bldOccRef=&world.bldOcc;resetMatchInteraction();resetNotifications();ais.clear();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        localPlayer=0;phase=::Phase::InGame;campaignMission=-1;gameOver=victory=paused=showMenu=netGame=false;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;SetTargetFPS(0);bakeTerrain();
        int x,y;tileToScreen(22,22,x,y);camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;clampCameraToMap();
        bool exact=true,unselectable=true;shotAtDisplaySize=true;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto file=(std::filesystem::path(outputDir)/(name+"-"+std::to_string(size.x)+".png")).string();shotFile=file;render();
            Image picture=LoadImage(file.c_str());exact &= picture.data && picture.width==size.x && picture.height==size.y && world.checksum()==before;UnloadImage(picture);
            for(EID id=0;id<(EID)world.ents.size();++id)if(world.managedAircraft(id)) {
                const auto rect=unitScreenRect(world.ents[id]);const EID picked=pickUnit((int)((rect.x+rect.width*.5f)*camZoom),(int)((rect.y+rect.height*.5f)*camZoom));
                unselectable &= !world.managedAircraft(picked);
            }
            doBoxSelect({0,0,(float)(SCREEN_W-sidebarW),(float)SCREEN_H},false);
            for(EID selected:sel)unselectable &= !world.managedAircraft(selected);
        }
        shotAtDisplaySize=false;sim=SimInput{};check(exact && unselectable,"both display sizes render the real sortie without advancing simulation or selecting managed friendly aircraft");
    };
    for(const char* edition:{"fusion","ra2","yr"}) {
        const bool source=std::string(edition)!="fusion";const std::string path=std::string("assets/rules/profiles/")+edition+".ini";
        World sample;bool loaded=sample.init(64,64,153,1,1,{Faction::Allies,Faction::Allies},0,nullptr,false,false,source?path.c_str():nullptr);
        check(loaded,"the ordinary edition starts with its aircraft controller enabled");if(!loaded)continue;
        for(EID id=0;id<(EID)sample.ents.size();++id)if(sample.valid(id))sample.kill(id,false);
        sample.ents.clear();sample.freeList.clear();sample.nativeLaunchers.clear();sample.nativeRockets.clear();
        std::fill(sample.bldOcc.begin(),sample.bldOcc.end(),-1);std::fill(sample.unitOccHead.begin(),sample.unitOccHead.end(),INVALID_EID);std::fill(sample.airOccHead.begin(),sample.airOccHead.end(),INVALID_EID);
        for(auto& cell:sample.map.cells)cell=Cell{};
        for(int y=15;y<30;++y)for(int x=8;x<21;++x)sample.map.at(x,y).terrain=Terrain::Water;
        for(auto& fog:sample.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        for(auto& p:sample.players){p.active=true;p.defeated=false;}sample.cratesEnabled=false;
        const EID carrier=sample.spawnUnit(0,UnitType::AircraftCarrier,14.5f,22.5f);
        const EID target=sample.spawnBuilding(1,BldType::OreRefinery,27,22,true);
        check(sample.valid(carrier) && sample.hasAircraftSpawner(carrier) && sample.aircraftCapacity(carrier)==3
            && sample.dockedAircraft(carrier,true)==3 && sample.validateAircraftState(),"a real carrier owns three fixed, ready slots with original aircraft records");
        if(!sample.valid(carrier) || !sample.hasAircraftSpawner(carrier))continue;
        if(source) {
            World staged=sample;const auto before=staged.checksum();auto malformed=sample.nativeScenarioSource;
            const auto start=malformed.find("[NativeAircraft.0]");const auto value=malformed.find("Regenerate = 600",start);
            bool strict=value!=std::string::npos;
            if(strict){malformed.insert(value+std::string("Regenerate = 600").size(),"x");strict=!staged.restoreNativeDefinitions(malformed) && staged.checksum()==before;}
            check(strict,"malformed source aircraft timing is refused without changing an existing world's immutable definitions");
            malformed=sample.nativeScenarioSource;
            const auto aircraftType=sample.nativeAircraftDefinitions.front().aircraft;
            const auto header=malformed.find("[Type."+std::to_string(aircraftType)+"]");const auto next=malformed.find('[',header+1);
            const auto rocket=malformed.find("NativeRocket = 0",header);bool exclusive=rocket!=std::string::npos && (next==std::string::npos || rocket<next);
            if(exclusive){malformed[rocket+std::string("NativeRocket = ").size()]='1';exclusive=!staged.restoreNativeDefinitions(malformed) && staged.checksum()==before;}
            check(exclusive,"source aircraft and missile roles cannot overlap or corrupt an active world's definitions");
        }
        for(int i=0;i<3;++i){auto& p=sample.ents[carrier].cargo[i];p.hp=17+i*7;p.kills=10+i;p.vetRank=1;p.veterancyValue=400+i;}
        const auto records=sample.ents[carrier].cargo;
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={carrier};attack.a=target;sample.applyCmd(0,attack);
        std::vector<EID> planes;std::vector<uint64_t> launches;bool preserved=true,gathered=true;int previous=0;
        for(int tick=0;tick<200 && planes.size()<3;++tick) {
            sample.update();
            for(const auto& node:sample.aircraftSpawners.front().nodes)if(sample.valid(node.aircraft)
                && std::find(planes.begin(),planes.end(),node.aircraft.id)==planes.end()) {
                const auto& e=sample.ents[node.aircraft.id];
                if(e.kills<10 || e.kills>12)preserved=false;
                else {const auto& old=records[e.kills-10];preserved &= e.hp==old.hp && e.vetRank==old.vetRank && e.veterancyValue==old.veterancyValue
                    && e.nativeIdentity.object==old.nativeIdentity.object && e.nativeIdentity.type==old.nativeIdentity.type && e.ammo==1;}
                planes.push_back(node.aircraft.id);launches.push_back(sample.tick);
            }
            if(planes.size()<3)for(const auto& shot:sample.projs)for(auto id:planes)gathered &= !shot.alive || shot.src!=id;
            if(planes.size()==1 && previous==0)check(clone(sample,10),"a partially launched gathering wave resumes with exact source identities and remaining launch work");
            previous=(int)planes.size();
        }
        const bool all=planes.size()==3;
        check(all && preserved && gathered && launches[1]-launches[0]==40 && launches[2]-launches[1]==40,
            "actual attack commands launch a gathered wave at the source cadence while retaining health, veterancy and identity");
        if(!all)continue;
        check(clone(sample,15),"a complete wave continues actual flight identically across a snapshot");
        World frozen=sample;frozen.map.bldOccRef=&frozen.bldOcc;
        for(auto id:planes){frozen.ents[id].tpSick=2000;frozen.ents[id].nativeIdentity.sleeping=false;}
        bool capacity=true;
        for(int tick=0;tick<245;++tick) {
            frozen.update();int count=frozen.dockedAircraft(carrier);
            for(auto id:planes)count+=frozen.valid(id);capacity &= count==3 && frozen.ents[carrier].cargo.empty();
        }
        check(capacity && clone(frozen,20),"airborne aircraft retain their slots for the whole legacy refill window, without extra stock or births");
        const auto frozenBefore=frozen.checksum();World::Cmd move{};move.type=World::Cmd::Move;move.ids={planes[0]};move.x=40;move.y=40;frozen.applyCmd(0,move);
        check(frozen.checksum()==frozenBefore,"a forged player command cannot take over a managed aircraft");
        World defended=frozen;defended.map.bldOccRef=&defended.bldOcc;
        defended.spawnBuilding(1,BldType::PowerPlant,12,8,true);const EID patriot=defended.spawnBuilding(1,BldType::PatriotMissile,21,20,true);
        bool intercepted=false,allDead=false;
        for(int tick=0;tick<300 && !allDead;++tick) {
            defended.update();for(const auto& shot:defended.projs)if(shot.alive && shot.src==patriot)
                intercepted |= std::find(planes.begin(),planes.end(),shot.target)!=planes.end();
            allDead=true;for(auto id:planes)allDead &= !defended.valid(id);
        }
        check(intercepted && allDead && defended.dockedAircraft(carrier)==0 && clone(defended,20),
            "a powered enemy Patriot destroys real spawned aircraft and starts persisted loss regeneration, with no immediate refill");
        World outsider=frozen;outsider.map.bldOccRef=&outsider.bldOcc;
        const EID foreign=outsider.spawnUnit(0,UnitType::Hornet,outsider.ents[carrier].x,outsider.ents[carrier].y);
        outsider.ents[foreign].airbase=carrier;outsider.ents[foreign].state=UState::Returning;outsider.update();
        check(!outsider.valid(foreign) && outsider.dockedAircraft(carrier)==0 && clone(outsider,20),
            "an unregistered aircraft cannot join a modern carrier simply by copying its entity ID");
        capture(sample,std::string(edition)+"-sortie");
        const float previousX=sample.ents[carrier].x,previousY=sample.ents[carrier].y;
        World::Cmd sail{};sail.type=World::Cmd::Move;sail.ids={carrier};sail.x=18.5f;sail.y=26.5f;sample.applyCmd(0,sail);
        const int targetHp=sample.ents[target].hp;bool hit=false,spent=false;
        for(int tick=0;tick<1200 && !spent;++tick) {
            sample.update();hit |= !sample.valid(target) || sample.ents[target].hp<targetHp;spent=true;
            for(auto id:planes)spent &= !sample.valid(id) || sample.ents[id].ammo==0;
        }
        World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={carrier};
        for(int tick=0;tick<600 && (sample.dockedAircraft(carrier)<3 || !hit);++tick) {
            sample.update();hit |= !sample.valid(target) || sample.ents[target].hp<targetHp;
        }
        bool retained=sample.dockedAircraft(carrier)==3 && sample.dockedAircraft(carrier,true)==0;
        for(const auto& p:sample.ents[carrier].cargo){if(p.kills<10 || p.kills>12){retained=false;continue;}const auto& old=records[p.kills-10];retained &= p.hp==old.hp && p.vetRank==old.vetRank && p.ammo==0 && p.nativeIdentity.object==old.nativeIdentity.object;}
        if(!hit || !spent || !retained) {
            std::printf("AIRCRAFT DIAG edition=%s hit=%d spent=%d retained=%d target_hp=%d/%d docked=%d ready=%d tick=%llu\n",edition,hit,spent,retained,sample.ents[target].hp,targetHp,sample.dockedAircraft(carrier),sample.dockedAircraft(carrier,true),(unsigned long long)sample.tick);
            for(const auto& n:sample.aircraftSpawners.front().nodes)std::printf("AIRCRAFT NODE phase=%d actor=%d cargo=%d remain=%d\n",(int)n.phase,n.aircraft.id,n.cargo,n.remaining);
        }
        check(hit && spent && retained && distf(previousX,previousY,sample.ents[carrier].x,sample.ents[carrier].y)>.05f && clone(sample,20),
            "real bombing damages the destination and damaged aircraft track a moving carrier into persisted reloading slots");
        sample.applyCmd(0,stop);
        bool stayed=true;
        for(int tick=0;tick<400;++tick) {
            int left=1800001;for(const auto& node:sample.aircraftSpawners.front().nodes)if(node.phase==Phase::Reloading)left=std::min(left,node.remaining);
            if(left<=1 || left==1800001)break;sample.update();
            for(const auto& p:sample.ents[carrier].cargo)stayed &= p.hp<unitDef(p).hp && p.ammo==0;
        }
        sample.update();bool rearmed=false;for(const auto& node:sample.aircraftSpawners.front().nodes)if(node.phase==Phase::Docked) {
            const auto& p=sample.ents[carrier].cargo[node.cargo];rearmed |= p.hp==unitDef(p).hp && p.ammo==unitDef(p).ammo;
        }
        check(stayed && rearmed && clone(sample,20),"health and ammunition recover only when source reloading completes, preserving aircraft identity and experience");
        if(source) {
            World exhausted=sample;exhausted.map.bldOccRef=&exhausted.bldOcc;exhausted.nativeProductionSerial=std::numeric_limits<uint64_t>::max();
            exhausted.applyCmd(0,attack);exhausted.update();bool launched=false;
            for(const auto& node:exhausted.aircraftSpawners.front().nodes)launched |= exhausted.valid(node.aircraft);
            check(launched && exhausted.nativeProductionSerial==std::numeric_limits<uint64_t>::max() && clone(exhausted,20),
                "existing source aircraft can relaunch at an exhausted creation serial without consuming a new logical identity");
        }
        World loss=frozen;loss.map.bldOccRef=&loss.bldOcc;const auto oldRef=loss.entityRef(planes[0]);const auto oldKey=loss.ents[planes[0]].nativeIdentity.object;
        loss.kill(planes[0],false);loss.applyCmd(0,stop);const int duration=loss.aircraftSpawners.front().regenerateTicks;
        for(int i=1;i<3;++i)loss.ents[planes[i]].tpSick=duration+100;
        bool waited=true;for(int tick=0;tick<duration-1;++tick){loss.update();waited &= loss.ents[carrier].cargo.empty();}
        check(waited && loss.aircraftSpawners.front().nodes[0].remaining==1 && clone(loss,0),"only destruction starts the original regeneration delay and a one-tick remainder survives a save");
        loss.update();bool regenerated=loss.dockedAircraft(carrier,true)==1 && loss.ents[carrier].cargo.size()==1;
        if(regenerated && source)regenerated &= loss.ents[carrier].cargo.front().nativeIdentity.object!=oldKey;
        loss.applyCmd(0,attack);loss.update();const auto newRef=loss.aircraftSpawners.front().nodes[0].aircraft;
        check(regenerated && loss.valid(newRef) && newRef.id==oldRef.id && newRef.generation>oldRef.generation && clone(loss,20),
            "one genuinely new aircraft replaces a destroyed slot, and entity-ID reuse cannot reconnect the old incarnation");
        World captured=frozen;captured.map.bldOccRef=&captured.bldOcc;captured.ents[carrier].player=1;captured.update();bool handed=true;
        for(const auto& node:captured.aircraftSpawners.front().nodes)if(captured.valid(node.aircraft))handed &= captured.ents[node.aircraft.id].player==1 && captured.ents[node.aircraft.id].state==UState::Returning;
        check(handed && clone(captured,20),"changing carrier ownership transfers and recalls its managed aircraft without leaving an enemy sortie active");
        World destroyed=frozen;destroyed.map.bldOccRef=&destroyed.bldOcc;destroyed.kill(carrier,false);
        const EID replacement=destroyed.spawnUnit(0,UnitType::MCV,24.5f,22.5f);
        for(auto id:planes)if(destroyed.valid(id))destroyed.ents[id].tpSick=0;
        check(replacement==carrier && clone(destroyed,20) && destroyed.valid(replacement)
            && !destroyed.valid(planes[0]) && !destroyed.valid(planes[1]) && !destroyed.valid(planes[2]),"destroyed or reused carriers cannot adopt orphaned aircraft after a snapshot");
        World bad=frozen;bad.map.bldOccRef=&bad.bldOcc;bad.aircraftSpawners.front().nodes[1].aircraft=bad.aircraftSpawners.front().nodes[0].aircraft;
        FILE* invalid=tmpfile();bool refused=invalid && !bad.saveGame(invalid);if(invalid)fclose(invalid);check(refused,"duplicate live aircraft assignments are refused at the save boundary");
        FILE* bytesFile=tmpfile();bool corrupted=false;
        if(bytesFile && frozen.saveGame(bytesFile)) {
            const auto size=ftell(bytesFile);std::vector<uint8_t> bytes((size_t)size);rewind(bytesFile);fread(bytes.data(),1,bytes.size(),bytesFile);
            const auto section=locateWorldSaveSection(bytes,WorldSaveSection::Aircraft);
            if(section && section->length>65) {
                bytes[section->offset+65]=255;FILE* altered=tmpfile();World rejected;
                if(altered){fwrite(bytes.data(),1,bytes.size(),altered);rewind(altered);corrupted=!rejected.loadGame(altered);fclose(altered);}
            }
        }
        if(bytesFile)fclose(bytesFile);check(corrupted,"malformed persisted aircraft phases are rejected rather than interpreted as live slots");
        std::printf("AIRCRAFT CASE edition=%s slots=3 launch_delay=40 regenerate=%d reload=%d\n",edition,duration,sample.aircraftSpawners.front().reloadTicks);
    }
    const auto old=contentResolve("review_out/project-review-20261115/legacy-world49-scene18.sav");
    if(!old.empty()) {
        Game legacy;bool loaded=legacy.loadGameFile(old.c_str());g_script.shutdown();
        check(loaded && legacy.world.usesRulesProfile() && !legacy.world.aircraftSpawnersEnabled
            && legacy.world.nativeAircraftDefinitions.empty() && clone(legacy.world,20),
            "an actual World49/Scene18 application save retains its embedded source rules and historical aircraft behavior");
    }
    std::printf("AIRCRAFT failures=%d\n",failures);return failures;
}
