#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

int GameRegression::nativeWeaponRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_GUN SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_GUN %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();world=World{};localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const std::string& file){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(file.c_str()) && world.startNativeEvents();};
    const auto ordinary=unitDef(UnitType::Conscript).weapon;
    if(!load(scene)){check(false,"original scene and four-slot weapon profiles load");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto resume=[&](int ticks) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= world.checksum()==restored.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}
        return exact;
    };
    {
        const auto old="review_out/native-campaign/legacy-native39-missile.sav";
        if(FileExists(old)) {
            bool loaded=loadGameFile(old);for(const auto& p:world.projs)loaded &= p.w.nativeSlot==-1;
            check(loaded && resume(30),"an actual pre-upgrade v10/World v39 missile snapshot restores legacy weapon references and continues exactly");
        } else std::printf("NATIVE_GUN NOTE: optional actual v39 snapshot is not installed\n");
    }
    Vec2i origin{-1,-1};int serial=0;
    const auto arena=[&](bool keepLoaded=false) {
        if(!keepLoaded){world=initial;world.map.bldOccRef=&world.bldOcc;}
        for(auto& trigger:world.nativeEvents.triggers)trigger.enabled=false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        world.projs.clear();world.timedBombs.clear();world.effects.clear();world.update();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        origin={-1,-1};
        for(int y=6;y<world.map.h-6 && origin.x<0;++y)for(int x=6;x<world.map.w-6 && origin.x<0;++x) {
            bool clear=true;
            for(int dy=-5;dy<=5 && clear;++dy)for(int dx=-5;dx<=5 && clear;++dx)
                clear=world.map.surfacePassable(x+dx,y+dy,0,0,true,true) && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height;
            if(clear)origin={x,y};
        }
        return origin.x>=0;
    };
    const auto spawn=[&](const char* name,int player,float x,float y) {
        int type=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name){type=i;break;}
        const float offset=type>=0 && world.nativeTypes[type]->isBuilding?0:.5f;
        return world.spawnNativeActor(type,player,origin.x+x+offset,origin.y+y+offset,256,64,"counter:gun:"+std::to_string(serial++),"","Guard");
    };
    const auto trace=[&](const char* name,int rank,bool deployed,int damage,int cooldown,int delivered) {
        if(!arena()){check(false,"the native gun arena has an open level surface");return;}
        const EID actor=spawn(name,0,0,0),target=spawn("E2",1,2,0);
        if(!world.valid(actor) || !world.valid(target)){check(false,"the gun trace actors physically spawn");return;}
        world.ents[actor].vetRank=rank;world.ents[actor].deployed=deployed;
        world.ents[target].hp=100000;world.ents[target].tpSick=2000;
        const auto weapon=world.effWeapon(world.ents[actor],&world.ents[target]);
        bool exact=weapon.damage==damage && weapon.cooldown==cooldown && world.orderAttack({actor},target,false).accepted==1;
        std::vector<uint64_t> times;int last=world.ents[target].hp;bool continued=false;
        const auto end=world.tick+3*cooldown+2;
        while(world.tick<end && world.valid(actor) && world.valid(target)) {
            world.update();const int hp=world.ents[target].hp;
            if(hp!=last){exact &= last-hp==delivered;times.push_back(world.tick);last=hp;
                if(!continued){continued=resume(1);last=world.ents[target].hp;}}
        }
        exact &= times.size()==4 && continued;
        for(size_t i=1;i<times.size();++i)exact &= times[i]-times[i-1]==(uint64_t)cooldown;
        std::printf("NATIVE_GUN TRACE %s rank=%d deployed=%d damage=%d cooldown=%d shots=%zu\n",name,rank,deployed,weapon.damage,weapon.cooldown,times.size());
        check(exact,"the source rifle/pistol actually deals the expected damage at each source-clock interval and resumes without a timing shift");
    };
    trace("E2",0,false,10,50,8);trace("E1",0,false,15,40,12);trace("E1",0,true,15,30,15);
    trace("E1",1,false,16,24,12);trace("E1",2,false,27,24,21);trace("E1",2,true,27,6,27);
    trace("E2",2,false,22,30,17);trace("TANY",0,false,125,20,125);trace("TANY",2,false,137,12,137);
    check(unitDef(UnitType::Conscript).weapon.damage==ordinary.damage && unitDef(UnitType::Conscript).weapon.cooldown==ordinary.cooldown,
        "loading source weapons does not rewrite the ordinary skirmish Conscript rules");
    if(arena()) {
        const EID gi=spawn("E1",0,0,0),target=spawn("E2",1,2,0);
        world.ents[target].hp=100000;world.ents[target].tpSick=2000;
        World::Cmd command{};command.type=World::Cmd::Deploy;command.ids={gi};world.applyCmd(0,command);
        const auto weapon=world.effWeapon(world.ents[gi],&world.ents[target]);
        check(world.ents[gi].deployed && weapon.range==5 && weapon.damage==15 && weapon.cooldown==30 && weapon.nativeSlot==1 && resume(1),
            "the real Deploy command selects the map-local Para weapon and persists that deployed state");
        world.ents[gi].vetRank=2;const auto elite=world.effWeapon(world.ents[gi],&world.ents[target]);
        check(elite.range==7 && elite.nativeSlot==3 && elite.damage==27 && elite.cooldown==6 && resume(1),
            "elite deployed GI selects the separate original ParaE slot rather than a core deployed override");
        world.ents[target].vetRank=1;world.ents[target].hp=1000;
        world.damage(target,15,0,gi);check(world.ents[target].hp==990,"source STRONGER applies the original 1.5 armor divisor once");
    } else check(false,"the deployment/armor arena loads");
    if(arena()) {
        const EID tanya=spawn("TANY",0,0,0),building=spawn("GACNST",1,2,0);
        world.ents[building].hp=8000;const auto weapon=world.effWeapon(world.ents[tanya],&world.ents[building]);
        bool correct=weapon.nativeSlot==1 && weapon.damage==2500 && weapon.range==1.5f && weapon.cooldown==100
            && world.orderAttack({tanya},building,false).accepted==1 && world.ents[tanya].state==UState::Chasing && resume(2);
        while(world.tick<200 && world.ents[building].hp==8000)world.update();
        const auto hit=world.tick;correct &= world.ents[building].hp==5500 && world.timedBombs.empty();
        correct &= resume(1);while(world.tick<hit+99)world.update();correct &= world.ents[building].hp==5500;
        world.update();correct &= world.ents[building].hp==3000 && world.timedBombs.empty();
        check(correct,"Tanya reaches a large foundation edge and uses source Sapper damage/range/ROF instead of a delayed 6000-damage bomb");
    } else check(false,"the sabotage arena loads");
    if(arena()) {
        Vec2i water{-1,-1};for(int y=2;y<world.map.h-2 && water.x<0;++y)for(int x=2;x<world.map.w-2 && water.x<0;++x)
            if(world.map.surfacePassable(x,y,1,0,true,true) && world.map.surfacePassable(x-1,y,2,0,true,true))water={x,y};
        bool correct=water.x>=0;
        if(correct) {
            origin=water;const EID tanya=spawn("TANY",0,-1,0),ship=spawn("DRED",1,0,0);
            correct=world.valid(tanya) && world.valid(ship) && world.effWeapon(world.ents[tanya],&world.ents[ship]).nativeSlot==1
                && world.orderAttack({tanya},ship,false).accepted==1;
            world.update();correct &= !world.valid(ship) && world.timedBombs.empty() && resume(1);
        }
        check(correct,"the same source Sapper slot destroys an actual naval Dreadnought in water through the public attack order");
    }
    if(arena()) {
        const EID tank=spawn("MTNK",0,0,0),target=spawn("E2",1,3,0);world.ents[tank].vetRank=2;
        world.ents[target].hp=100000;world.ents[target].tpSick=2000;
        bool correct=world.orderAttack({tank},target,false).accepted==1;
        world.update();const auto first=world.tick;const auto initialShots=world.projs.size();const auto pending=world.ents[tank].burstRemaining;
        correct &= initialShots==1 && pending==1 && world.projs.front().w.nativeSlot==2 && world.projs.front().w.damage==60
            && world.ents[tank].burstDelay>=6 && world.ents[tank].burstDelay<=10 && resume(1);
        while(world.tick<first+12)world.update();
        correct &= world.projs.size()==2 && world.ents[tank].burstRemaining==0 && resume(4);
        for(const auto& p:world.projs)correct &= p.w.nativeSlot==2 && p.w.nativeType==world.nativeUnitIndex("MTNK");
        check(correct,"original elite 105mmE burst count, converted burst gap and each flying shell's slot survive a mid-volley snapshot");
    } else check(false,"the elite shell arena loads");
    // Remove only the launcher schema in a marked source copy: conventional
    // projectile references must not depend on an installed missile spawner.
    {
        std::istringstream input(initial.nativeScenarioSource);std::string source,line;bool skip=false;
        while(std::getline(input,line)) {
            if(!line.empty() && line.front()=='[')skip=line=="[NativeRockets]" || line.starts_with("[NativeRocket.");
            if(!skip)source+=line+'\n';
        }
        const auto file=(std::filesystem::path(outputDir)/"counter-no-launcher.ini").string();{std::ofstream output(file,std::ios::binary);output<<source;}
        bool correct=load(file) && arena(true);
        if(correct) {
            const EID sam=spawn("NASAM",0,0,0);world.spawnBuilding(0,BldType::PowerPlant,origin.x-3,origin.y-3,true);
            const EID aircraft=world.spawnUnit(1,UnitType::Kirov,origin.x+3.5f,origin.y+.5f);world.ents[aircraft].tpSick=2000;
            world.update();correct &= world.nativeRocketDefinitions.empty() && !world.projs.empty();
            if(!world.projs.empty()) {
                auto& projectile=world.projs.front();correct &= projectile.w.nativeSlot==0 && projectile.w.nativeType==world.ents[sam].nativeIdentity.type && resume(1);
                if(!world.projs.empty()) {
                    const int slot=world.projs.front().w.nativeSlot;world.projs.front().w.nativeSlot=4;FILE* bad=tmpfile();
                    check(bad && !world.saveGame(bad),"an invalid conventional projectile slot is rejected by snapshot validation");if(bad)fclose(bad);
                    world.projs.front().w.nativeSlot=slot;
                }
            }
        }
        check(correct,"a live source SAM projectile saves/restores its weapon slot in a scene with no rocket launcher definitions");
    }
    {
        auto source=initial.nativeScenarioSource;const auto at=source.find("Gun0ROF = ");bool rejected=at!=std::string::npos;
        if(rejected){const auto end=source.find('\n',at);source.replace(at,end-at,"Gun0ROF = 0");World invalid=initial;invalid.map.bldOccRef=&invalid.bldOcc;rejected=!invalid.restoreNativeDefinitions(source);}
        check(rejected,"a zero-rate source gun is rejected instead of enabling a zero-time fire loop");
    }
    std::printf("NATIVE_GUN failures=%d\n",failures);return failures;
}
