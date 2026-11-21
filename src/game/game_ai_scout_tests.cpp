#include "game_regression.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

int GameRegression::aiScoutingRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* text) {
        std::printf("AI_SCOUT %s %s\n",ok?"PASS":"FAIL",text);failures+=!ok;
    };
    auto setup=[&]() {
        world.init(64,64,20261113,1,2,{Faction::Soviet,Faction::Soviet,Faction::Soviet});
        for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& player:world.players){player.money=0;player.defeated=false;}
        world.cratesEnabled=false;world.aiAlliance=true;
        world.spawnBuilding(1,BldType::NukeSilo,4,4,true);
        world.spawnBuilding(2,BldType::ConYard,44,44,true);
        world.spawnBuilding(2,BldType::BattleLab,49,44,true);
        world.spawnBuilding(2,BldType::WarFactory,44,50,true);
        world.spawnBuilding(0,BldType::PowerPlant,16,46,true);
        localPlayer=0;campaignMission=-1;gameSpeed=1;ais.clear();
    };
    auto think=[](SkirmishAI& ai,World& w){for(int frame=0;frame<15;++frame)ai.update(w);};
    setup();SkirmishAI bot;bot.reset(1);
    world.map.reveal(1,32,32,64);world.players[1].swReady[(int)SWType::Nuke]=true;
    think(bot,world);
    const auto& launches=world.nukes;
    const EID target=launches.empty()?INVALID_EID:world.bldAt((int)launches.front().tx,(int)launches.front().ty);
    check(launches.size()==1 && world.valid(target) && world.ents[target].player==0
        && bot.knownBuildings.size()==1 && bot.knownBuildings.front().owner==0,
        "a visible allied base never outweighs the real enemy as a nuclear target");

    setup();bot.reset(1);world.map.reveal(1,5,5,8);
    world.players[1].swReady[(int)SWType::Nuke]=true;think(bot,world);
    check(world.nukes.empty() && bot.knownBuildings.empty() && world.players[1].swReady[(int)SWType::Nuke],
        "an undiscovered enemy base neither enters memory nor consumes a superweapon");
    world.map.reveal(1,17,47,4);think(bot,world);
    check(bot.knownBuildings.size()==1 && world.nukes.size()==1,"actual reconnaissance records an enemy installation");
    world.nukes.clear();world.players[1].swReady[(int)SWType::Nuke]=true;
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_SEEN);
    world.map.reveal(1,5,5,8);
    World changed=world;changed.map.bldOccRef=&changed.bldOcc;
    const EID old=changed.bldAt(16,46);changed.kill(old,false);
    changed.spawnBuilding(0,BldType::BattleLab,26,40,true);
    SkirmishAI copy=bot;think(bot,world);think(copy,changed);
    check(world.nukes.size()==1 && changed.nukes.size()==1
        && world.nukes.front().tx==changed.nukes.front().tx && world.nukes.front().ty==changed.nukes.front().ty
        && bot.knownBuildings==copy.knownBuildings,
        "hidden destruction and replacement cannot update remembered coordinates or nuclear targeting");

    ais={bot};const auto saved=ais;
    const auto file=std::filesystem::temp_directory_path()/
        ("openra2-scout-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sav");
    const auto utf8=file.u8string();const std::string path(utf8.begin(),utf8.end());
    const bool written=saveGameFile(path.c_str());ais[0].reset(1);
    const bool loaded=loadGameFile(path.c_str());
    check(written && loaded && ais==saved,"last observations and enemy contacts survive a real application save/load");
    World replay=world;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI resumed=ais[0];
    bool identical=true;
    for(int tick=0;tick<300;++tick) {
        world.update();replay.update();ais[0].update(world);resumed.update(replay);
        identical &= world.checksum()==replay.checksum() && ais[0]==resumed;
        world.presentationEvents.clear();replay.presentationEvents.clear();world.evaQueue.clear();replay.evaQueue.clear();
    }
    check(identical,"restored scouting decisions and simulation stay identical for 300 ticks");
    world.kill(world.bldAt(16,46),false);world.map.reveal(1,17,47,8);think(ais[0],world);
    check(ais[0].knownBuildings.empty(),"reconnaissance clears a remembered building after observing its empty footprint");
    std::error_code error;std::filesystem::remove(file,error);
    const auto infiltrationWorld=[](World& w) {
        w.init(48,48,47,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& p:w.players){p.money=0;p.active=true;p.defeated=false;}
        w.cratesEnabled=false;w.spawnBuilding(0,BldType::ConYard,2,2,true);
    };
    {
        World w;infiltrationWorld(w);SkirmishAI spyBot;spyBot.reset(0);
        const EID spy=w.spawnUnit(0,UnitType::Spy,6.5f,5.5f);
        const EID refinery=w.spawnBuilding(1,BldType::OreRefinery,34,34,true);
        spyBot.pcfg.useSpies=true;spyBot.pcfg.useEngineers=true;think(spyBot,w);
        check(spyBot.knownBuildings.empty() && w.ents[spy].target==INVALID_EID,
            "an undiscovered refinery cannot become a spy's infiltration target");
        w.orderStop({spy});w.map.reveal(0,35,35,5);think(spyBot,w);
        check(w.ents[spy].target==refinery && spyBot.knownBuildings.size()==1,
            "a refinery observed through the spy house's actual fog can be infiltrated");
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_SEEN);
        w.orderStop({spy});World changed=w;changed.map.bldOccRef=&changed.bldOcc;
        changed.kill(refinery,false);const EID hidden=changed.spawnBuilding(1,BldType::PowerPlant,40,38,true);
        SkirmishAI copy=spyBot;think(spyBot,w);think(copy,changed);
        check(w.ents[spy].target==INVALID_EID && changed.ents[spy].target==INVALID_EID
            && spyBot.knownBuildings==copy.knownBuildings
            && changed.map.fogAt(0,40,38)!=FOG_VISIBLE && changed.valid(hidden),
            "a spy approaches remembered coordinates without following hidden destruction or a hidden replacement");
        w.orderStop({spy});w.map.reveal(0,35,35,5);spyBot.pcfg.useEngineers=false;think(spyBot,w);
        check(w.ents[spy].target==refinery,"disabling engineers does not disable the independent spy personality option");
    }
    {
        World w;infiltrationWorld(w);SkirmishAI engineerBot;engineerBot.reset(0);
        engineerBot.pcfg.useEngineers=true;
        const EID engineer=w.spawnUnit(0,UnitType::Engineer,6.5f,5.5f);
        const EID oil=w.spawnBuilding(-1,BldType::OilDerrick,34,34,true);
        think(engineerBot,w);
        check(w.ents[engineer].target==INVALID_EID,"an engineer cannot discover neutral technology through the entity list");
        w.map.reveal(0,35,35,5);think(engineerBot,w);
        check(w.ents[engineer].target==oil && w.ents[engineer].engineerIntent==EngineerIntent::Capture,
            "a revealed neutral technology building remains a valid engineer capture target");
    }
    for(bool modern:{false,true}) {
        World w;const bool loaded=w.init(64,64,2311,2,0,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,"assets/rules/profiles/ra2.ini");
        check(loaded && w.nativeSceneVersion==nativeTravelSceneVersion,"source scene exposes its current ordinary RA2 travel contract");if(!loaded)continue;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& p:w.players){p.money=0;p.defeated=false;}w.cratesEnabled=false;w.rebuildUnitOcc();w.rebuildAirOcc();
        w.spawnBuilding(0,BldType::ConYard,2,2,true);const EID unseen=w.spawnBuilding(1,BldType::ConYard,50,50,true);
        const EID air=w.spawnUnit(0,UnitType::Kirov,14.7890625f,5.88671875f);w.ents[air].state=UState::Circling;
        w.ents[air].goalX=w.ents[air].x;w.ents[air].goalY=w.ents[air].y;
        if(!modern)w.nativeSceneVersion=35; // isolate the historical decision contract, without downgrading save data
        SkirmishAI probe;probe.reset(0);auto altered=w;altered.map.bldOccRef=&altered.bldOcc;
        altered.kill(unseen,false);altered.spawnBuilding(1,BldType::PowerPlant,44,48,true);auto other=probe;
        think(probe,w);think(other,altered);
        const auto& e=w.ents[air];const float distance=distf(e.x,e.y,e.goalX,e.goalY);
        check(modern?distance>=unitDef(e).sight-1:distance<1,"new source aerial scouting escapes its re-shrouded neighborhood; old decision contract stays intact");
        check(e.target==INVALID_EID && probe.knownBuildings.empty() && other.knownBuildings.empty()
            && e.goalX==altered.ents[air].goalX && e.goalY==altered.ents[air].goalY,
            "hidden destruction and replacement cannot change the reconnaissance destination or create an attack target");
        check(e.state==(modern?UState::AttackMoving:UState::Moving),"armed new-source scouting uses normal attack-move without a hidden target ID");
        if(modern) {
            // A stale contact at the current cell is retained, but does not pin the scout there.
            w.orderStop({air});probe.knownBuildings={{14,5,1,BldType::PowerPlant,0}};probe.attackTimer=probe.thinkTimer=0;
            think(probe,w);const auto& next=w.ents[air];
            check(distf(next.x,next.y,next.goalX,next.goalY)>=unitDef(next).sight-1 && !probe.knownBuildings.empty(),
                "already-reached stale memory remains private memory while the scout continues a real survey leg");
        }
    }
    {
        World w;
        if(!w.init(64,64,2716,2,0,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,"assets/rules/profiles/ra2.ini"))return failures+1;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        std::fill(w.map.fog[0].begin(),w.map.fog[0].end(),FOG_SEEN);
        // A cliff encloses a once-observed section. Ground troops are on the
        // surrounding plain, which still has an unrelated unexplored cell.
        for(int y=18;y<=25;++y)for(int x=18;x<=25;++x)w.map.at(x,y).height=5;
        w.map.fog[0][w.cellIdx(55,55)]=FOG_UNSEEN;
        w.spawnBuilding(0,BldType::ConYard,2,2,true);
        w.spawnBuilding(1,BldType::ConYard,45,45,true);
        w.spawnUnit(0,UnitType::Rhino,32.5f,21.5f);
        const EID air=w.spawnUnit(0,UnitType::Kirov,36.5f,20.5f);
        const EID hidden=w.spawnUnit(1,UnitType::GI,21.5f,21.5f);
        w.tick=3600;w.ents[air].state=UState::Circling;
        World different=w;different.map.bldOccRef=&different.bldOcc;
        different.kill(hidden,false);different.spawnUnit(1,UnitType::GI,47.5f,27.5f);
        SkirmishAI survey;survey.reset(0);auto other=survey;
        think(survey,w);think(other,different);
        const auto& scout=w.ents[air];
        check(scout.state==UState::AttackMoving && scout.goalX>=18 && scout.goalX<26 && scout.goalY>=18 && scout.goalY<26,
            "aerial scouts revisit stale ground that friendly surface forces cannot reach before pursuing a distant unobserved cell");
        check(scout.target==INVALID_EID && scout.goalX==different.ents[air].goalX && scout.goalY==different.ents[air].goalY,
            "ground reachability and fog alone determine the aerial survey destination");
    }
    return failures;
}
