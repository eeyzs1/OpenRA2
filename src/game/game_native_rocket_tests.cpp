#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

int GameRegression::nativeRocketRegression(const char* outputDir) {
    const auto path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_ROCKET SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("NATIVE_ROCKET %s: %s\n",ok?"PASS":"FAIL",message);failures+=!ok;};
    g_script.shutdown();world=World{};ais.clear();localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const bool loaded=world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true) && world.loadNativeScene(path.c_str());
    check(loaded && world.nativeRocketDefinitions.size()==2,"the complete original scene loads a native missile-spawner definition");if(!loaded || world.nativeRocketDefinitions.size()!=2)return failures+1;
    const auto definition=world.nativeRocketDefinitions.front();EID ship=-1,statue=-1;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition) {
        if(world.ents[id].nativeIdentity.type==definition.launcher && ship<0)ship=id;
        if(world.ents[id].nativeIdentity.definition->id=="CANEWY04")statue=id;
    }
    check(world.valid(ship) && world.valid(statue) && definition.capacity==2 && definition.pause==20 && definition.tilt==60
        && definition.altitude==1550 && definition.regenerate==80 && definition.damage==300 && definition.eliteDamage==600
        && std::abs(definition.normal.verses[9]-.02f)<1e-6f && world.nativeLaunchers.size()==7 && world.nativeDockedSpawns(ship)==2,
        "original slots, phase durations, map altitude, payloads and the 2 percent statue multiplier are retained");
    if(!world.valid(ship) || !world.valid(statue))return failures+1;
    World original=world;original.map.bldOccRef=&original.bldOcc;
    const auto reset=[&](){world=original;world.map.bldOccRef=&world.bldOcc;};
    const auto find=[&](EID id)->const NativeRocketState*{for(const auto& r:world.nativeRockets)if(r.actor==id)return &r;return nullptr;};
    const auto resume=[&](int frames) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= restored.checksum()==world.checksum();
        for(int i=0;i<frames && exact;++i){world.update();restored.update();exact &= restored.checksum()==world.checksum();}
        return exact;
    };
    {
        const auto& owner=world.ents[ship];Vec2i nearby{-1,-1};
        for(int dx=1;dx<=3 && nearby.x<0;++dx)if(world.map.surfacePassable((int)owner.x+dx,(int)owner.y,1,0)
            && world.unitAtCell((int)owner.x+dx,(int)owner.y)<0)nearby={(int)owner.x+dx,(int)owner.y};
        const EID close=nearby.x<0?-1:world.spawnUnit(0,UnitType::Destroyer,nearby.x+.5f,nearby.y+.5f);
        check(world.valid(close) && !world.launchNativeRocket(ship,close) && world.nativeRockets.empty(),"the original minimum launch range rejects a genuinely nearby naval target");
    }
    reset();
    {
        WeaponDef impact;impact.damage=definition.damage;impact.cellSpread=definition.normal.spread;
        impact.percentAtMax=definition.normal.percentAtMax;impact.nativeVerses=definition.normal.verses.data();impact.antiAir=false;
        const int before=world.ents[statue].hp;world.applyCellSpreadDamage(impact,2,62.5f,70.5f,ship);
        // One central hit of 6, four edge hits of 3 and four corner hits of 1.
        check(before-world.ents[statue].hp==22,"the original 300-damage map warhead causes 22 damage across the statue's 3x3 foundation");
    }
    reset();
    // Explicit phase fixture: power sabotage disables the existing defenses.
    // The independent authored opening below keeps the original power state.
    for(auto& player:world.players)player.powerSabotage=100000;
    const auto attack=world.orderAttack({ship},statue,false);world.update();EID missile=-1;
    for(const auto& r:world.nativeRockets)if(r.source==ship)missile=r.actor;
    check(attack.accepted==1 && world.valid(missile) && find(missile) && find(missile)->phase==NativeRocketPhase::Pause
        && world.ents[missile].hp==75 && unitDef(world.ents[missile]).armor==Armor::Special2 && world.ents[missile].airborne()
        && !world.ents[missile].nativeIdentity.definition->selectable,
        "firing creates a real 75-HP special_2 aircraft with the map's strength override in the pause stage");
    if(!world.valid(missile))return failures+1;
    const uint64_t born=world.tick;const float initialX=world.ents[missile].x,initialY=world.ents[missile].y;
    const int x=(int)world.ents[ship].x,y=(int)world.ents[ship].y;Vec2i destination{-1,-1};
    for(auto step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
        const Vec2i next{x+step.x,y+step.y};if(world.map.surfacePassable(next.x,next.y,1,0) && world.unitAtCell(next.x,next.y)<0){destination=next;break;}
    }
    World::Cmd move{};move.type=World::Cmd::Move;move.ids={ship};move.x=destination.x+.5f;move.y=destination.y+.5f;world.applyCmd(2,move);
    for(int i=0;i<12;++i)world.update();
    check(destination.x>=0 && world.valid(missile) && distf(initialX,initialY,world.ents[missile].x,world.ents[missile].y)>.2f
        && find(missile)->phase==NativeRocketPhase::Pause,"a paused missile follows an actually moving launcher");
    check(resume(5),"pause state, linked slot and launcher movement resume with identical simulation");
    world.orderAttack({ship},statue,false);
    while(world.tick<born+definition.pause*LOGIC_FPS/15-1)world.update();
    check(world.valid(missile) && find(missile)->phase==NativeRocketPhase::Tilt && find(missile)->phaseTicks==0,
        "the source pause ends at its converted 15-Hz deadline");
    check(resume(30),"the tilting aircraft and its captured destination survive save/load");
    World tiltShot=world;tiltShot.map.bldOccRef=&tiltShot.bldOcc;
    while(world.tick<born+(definition.pause+definition.tilt)*LOGIC_FPS/15-1)world.update();
    check(world.valid(missile) && find(missile)->phase==NativeRocketPhase::Flight && find(missile)->slot==-1
        && std::abs(find(missile)->pitch-definition.pitchFinal*3.14159265358979323846f/2)<1e-4f,
        "tilt completes at the source deadline and releases the spent launch slot for regeneration");
    check(resume(100),"an accelerating, turning missile and regenerating launcher resume identically");
    World flightShot=world;flightShot.map.bldOccRef=&flightShot.bldOcc;
    check(world.valid(missile) && find(missile)->altitude>definition.flh[2] && find(missile)->speed>0 && world.nativeDockedSpawns(ship)==0
        && nativeActorFrame(world.ents[ship])==(world.ents[ship].dir&7)+8,"flight gains real altitude while the original DREDWO empty-launcher body is selected");
    {
        World::Cmd forbidden{};forbidden.type=World::Cmd::Move;forbidden.ids={missile};forbidden.x=10.5f;forbidden.y=10.5f;
        const auto before=world.checksum();world.applyCmd(world.ents[missile].player,forbidden);
        check(before==world.checksum(),"player movement commands cannot take control of a spawned missile");
    }
    const auto application=(std::filesystem::path(outputDir)/"native-missile-flight.sav").string();const auto before=world.checksum();
    const bool appSaved=saveGameFile(application.c_str());if(appSaved)world.players[0].money+=23;
    check(appSaved && loadGameFile(application.c_str()) && world.checksum()==before,"application v10 restores source actors, flight stages and captured warheads");
    world.kill(ship,false);const auto replacement=world.spawnUnit(2,UnitType::GI,x+.5f,y+.5f);
    bool detached=world.valid(missile) && find(missile) && find(missile)->source==-1 && replacement==ship;
    for(const auto& r:world.nativeRockets)detached &= r.source!=ship;
    check(detached && resume(50),"launcher destruction retires attached missiles, preserves airborne ones and prevents source-ID reuse from taking their credit");
    reset();world.startNativeEvents();bool antiAir=false,intercepted=false;int missileHits=0;
    const auto observeOpening=[&]() {
        std::set<EID> airborne;for(const auto& r:world.nativeRockets)airborne.insert(r.actor);
        const auto sequence=world.presentationSequence;world.update();
        for(const auto& event:world.presentationEvents)if(event.sequence>=sequence && event.kind==PresentationKind::Impact
            && airborne.contains(event.entity) && event.visualTarget==VisualTarget::Aircraft) {
            ++missileHits;intercepted |= event.lethal;
        }
        for(const auto& p:world.projs)if(p.alive && p.w.nativeType>=0 && world.valid(p.target)
            && world.ents[p.target].nativeIdentity.definition && world.ents[p.target].nativeIdentity.definition->nativeRocket)antiAir=true;
    };
    for(int i=0;i<120 && !antiAir;++i) {
        observeOpening();
    }
    check(antiAir && resume(60),"the untouched authored opening fires real anti-air projectiles at DMISL aircraft and restores their exact source warheads");
    for(int i=0;i<600 && !intercepted;++i)observeOpening();
    std::printf("NATIVE_ROCKET opening: tick=%llu missileHits=%d lethal=%d\n",(unsigned long long)world.tick,missileHits,intercepted);
    check(intercepted,"original powered defenses actually destroy spawned missile entities");
    {
        auto saved=world.nativeRockets;if(!saved.empty())world.nativeRockets.front().altitude=std::nanf("");
        const bool rejected=saved.empty() || !world.validateNativeRocketState();world.nativeRockets=saved;
        check(rejected,"non-finite flight state is rejected before persistence");
    }
    {
        const auto saved=world.nativeLaunchers;
        world.nativeLaunchers.front().nextLaunch=world.tick+(uint64_t)definition.launchDelay*LOGIC_FPS/15+1;
        const bool deadline=!world.validateNativeRocketState();world.nativeLaunchers=saved;
        world.nativeLaunchers.pop_back();const bool missing=!world.validateNativeRocketState();world.nativeLaunchers=saved;
        check(deadline && missing,"out-of-range launch deadlines and missing live-launcher records are rejected");
    }
    for(auto* snapshot:{&tiltShot,&flightShot}) {
        world=*snapshot;world.map.bldOccRef=&world.bldOcc;camZoom=.8f;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;
        sideMode=0;targetingSW=SWType::COUNT;sel={ship};selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);fogKnowledge={};syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int sx,sy;tileToScreen(57,74,sx,sy);camX=sx-(SCREEN_W-sidebarW)/2.f;camY=sy-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/(std::string(snapshot==&tiltShot?"native-tilt-":"native-flight-")+std::to_string(size.x)+".png")).string();
            const auto hash=world.checksum();render();check(hash==world.checksum(),"native tilted/airborne VXL and the launcher render without changing simulation state");
        }
    }
    std::printf("NATIVE_ROCKET failures=%d\n",failures);return failures;
}
