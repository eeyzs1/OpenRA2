#include "game_regression.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

int GameRegression::aiScoutingRegression(const char* outputDir) {
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
        check(loaded && w.nativeSceneVersion==nativeCurrentRulesSceneVersion,"source scene exposes its current ordinary RA2 contract");if(!loaded)continue;
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
    struct BomberActors {EID air=INVALID_EID,decoy=INVALID_EID,hidden=INVALID_EID;};
    const auto bomberWorld=[&](World& w,float decoyX=45.5f,bool revealAll=false,const World::RulesSnapshot* snapshot=nullptr) {
        if(!w.init(64,64,2715,2,0,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,
            snapshot?nullptr:"assets/rules/profiles/ra2.ini",{},snapshot))return BomberActors{};
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        w.cratesEnabled=false;w.spawnBuilding(0,BldType::ConYard,2,2,true);
        const EID hidden=w.spawnBuilding(1,BldType::ConYard,50,50,true);
        const EID air=w.spawnUnit(0,UnitType::Kirov,10.5f,20.5f);
        const EID decoy=w.spawnUnit(1,UnitType::Conscript,decoyX,20.5f);
        w.ents[air].state=UState::Circling;
        w.map.reveal(0,10,20,8);w.map.reveal(0,(int)decoyX,20,3);
        if(revealAll)std::fill(w.map.fog[0].begin(),w.map.fog[0].end(),FOG_VISIBLE);
        return BomberActors{air,decoy,hidden};
    };
    for(bool alreadyPursuing:{false,true}) {
        World w;const auto [air,decoy,hidden]=bomberWorld(w);
        if(!w.valid(air) || !w.valid(decoy))return failures+1;
        bool staged=w.valid(air) && w.valid(decoy) && unitDef(w.ents[air]).sight<35;
        if(alreadyPursuing)staged &= w.orderAttack({air},decoy).accepted==1 && w.ents[air].state==UState::Chasing;
        World altered=w;altered.map.bldOccRef=&altered.bldOcc;
        altered.kill(hidden,false);altered.spawnBuilding(1,BldType::ConYard,53,45,true);
        SkirmishAI survey;survey.reset(0);auto other=survey;
        think(survey,w);think(other,altered);
        const auto& scout=w.ents[air];
        std::printf("AI_SCOUT_DISTANT_DECOY existing=%d staged=%d state=%d target=%d goal=%.3f,%.3f memory=%zu\n",
            alreadyPursuing,staged,(int)scout.state,scout.target,scout.goalX,scout.goalY,survey.knownBuildings.size());
        check(staged && scout.target==INVALID_EID && scout.state==UState::AttackMoving && scout.attackMove.active
            && distf(scout.x,scout.y,scout.goalX,scout.goalY)>=unitDef(scout).sight-1,
            "a remote mobile contact cannot capture or retain an ordinary bomber's reconnaissance assignment");
        check(survey.knownBuildings.empty() && other.knownBuildings.empty()
            && scout.goalX==altered.ents[air].goalX && scout.goalY==altered.ents[air].goalY,
            "distant-decoy recovery uses visible contacts and fog without learning hidden infrastructure");
        World resumed;FILE* file=std::tmpfile();bool exact=file && w.saveGame(file);
        if(exact){std::rewind(file);exact=resumed.loadGame(file);}if(file)std::fclose(file);
        exact &= resumed.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion;
        auto restored=survey;
        for(int elapsed=0;elapsed<300 && exact;++elapsed) {
            w.update();resumed.update();survey.update(w);restored.update(resumed);
            exact=w.checksum()==resumed.checksum() && survey==restored;
        }
        check(exact,"a saved current bomber survey reconstructs its ordinary rule contract and resumes AI decisions tick for tick");
    }
    for(int situation=0;situation<4;++situation) {
        World w;
        const auto [air,decoy,hidden]=bomberWorld(w,situation==0?16.5f:situation==1?12.5f:45.5f,situation==3);
        if(!w.valid(air) || !w.valid(decoy))return failures+1;
        const EID target=situation==2?hidden:decoy;
        if(situation==2)w.map.reveal(0,52,52,6);
        if(situation==3)w.kill(hidden,false);
        const bool admitted=situation==3 || w.orderAttack({air},target).accepted==1;
        if(situation==1)for(int elapsed=0;elapsed<600 && w.valid(target) && w.ents[air].atkCd==0;++elapsed)w.update();
        const bool firing=w.ents[air].state==UState::Attacking && w.ents[air].atkCd>0;
        const int cooldown=w.ents[air].atkCd;
        SkirmishAI survey;survey.reset(0);think(survey,w);
        const auto& bomber=w.ents[air];
        const bool preserved=admitted && bomber.target==target
            && (bomber.state==UState::Chasing || bomber.state==UState::Attacking)
            && (situation!=1 || (firing && bomber.state==UState::Attacking && bomber.atkCd==cooldown));
        std::printf("AI_SCOUT_DECOY_GUARD case=%d admitted=%d target=%d actual=%d state=%d firing=%d memory=%zu\n",
            situation,admitted,target,bomber.target,(int)bomber.state,firing,survey.knownBuildings.size());
        check(preserved,situation==0?"a bomber preserves a legal nearby mobile pursuit"
            :situation==1?"a bomber's actual firing and cooldown survive reconnaissance scheduling"
            :situation==2?"a bomber preserves pursuit of an observed strategic building"
            :"a fully observed map still admits a distant mobile survivor as a bomber target");
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_CRUSH_INPUT_DIR")) {
        const auto path=std::filesystem::path(directory)/"grizzly-gi.sav";
        World archived;FILE* file=std::fopen(path.string().c_str(),"rb");const bool loaded=file && archived.loadGame(file);if(file)std::fclose(file);
        check(loaded && archived.nativeRulesBaseVersion==nativeTravelSceneVersion,
            "the actual pre-change save reconstructs its Scene46 ordinary decision contract");
        if(loaded)for(bool alreadyPursuing:{false,true}) {
            const World::RulesSnapshot snapshot{archived.nativeScenarioSource,true,true};
            World w;const auto [air,decoy,hidden]=bomberWorld(w,45.5f,false,&snapshot);
            if(!w.valid(air) || !w.valid(decoy))return failures+1;
            if(alreadyPursuing)w.orderAttack({air},decoy);
            SkirmishAI survey;survey.reset(0);think(survey,w);
            check(w.ents[air].state==UState::Chasing && w.ents[air].target==decoy && survey.knownBuildings.empty(),
                "an actual Scene46 source snapshot retains its historical remote-mobile pursuit policy");
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
    {
        const char* requested=std::getenv("OPENRA2_GAP_AUDIT_DIR");
        const std::filesystem::path output=requested?requested:outputDir;
        std::filesystem::create_directories(output);
        for(int scenario=0;scenario<9;++scenario) {
            World w;
            bool staged=w.init(64,64,20261123,1,2,{Faction::Allies,Faction::Soviet,Faction::Soviet},
                0,nullptr,false,false,"assets/rules/profiles/ra2.ini");
            if(scenario==6 && staged) {
                auto source=w.nativeScenarioSource;const auto current="Version = "+std::to_string(nativeCurrentRulesSceneVersion);
                const auto at=source.find(current);
                staged=at!=std::string::npos;
                if(staged) {
                    source.replace(at,current.size(),"Version = 50");
                    const auto production=source.find("[NativeProduction]");
                    const auto declared="Version = "+std::to_string(w.nativeProductionRules.version);
                    const auto revision=source.find(declared,production);
                    if(revision!=std::string::npos)source.replace(revision,declared.size(),"Version = 9");
                    for(const char* key:{"BuildingRepairFrames","BuildingRepairStep","BuildingRepairCost","BuildingSaleHuman","BuildingSaleOther","SaleCrewed","Engineer"}) {
                        const std::string prefix=std::string("\n")+key+" = ";
                        for(size_t field=source.find(prefix);field!=std::string::npos;field=source.find(prefix)) {
                            const auto end=source.find('\n',field+1);source.erase(field,end==std::string::npos?std::string::npos:end-field);
                        }
                    }
                    // Scene64's carrier is appended; removing it preserves all
                    // original actor indexes in this synthetic Scene50 source.
                    const int last=(int)w.nativeTypes.size()-1;
                    for(const auto& section:{std::string("NativeSaleSurvivors"),"Type."+std::to_string(last),"NativeProduction.Type."+std::to_string(last)}) {
                        const auto begin=source.find("["+section+"]");
                        if(begin!=std::string::npos){const auto end=source.find("\n[",begin);source.erase(begin,end==std::string::npos?std::string::npos:end+1-begin);}
                    }
                    const std::string typeCount="Types = "+std::to_string(last+1);
                    if(const auto count=source.find(typeCount);count!=std::string::npos)source.replace(count,typeCount.size(),"Types = "+std::to_string(last));
                    const auto unload=source.find("[NativeTransportUnload]");
                    if(unload!=std::string::npos) {
                        const auto end=source.find("\n[",unload);
                        source.erase(unload,end==std::string::npos?std::string::npos:end+1-unload);
                    }
                    for(size_t field=source.find("\nIsTrain = ");field!=std::string::npos;field=source.find("\nIsTrain = ")) {
                        const auto end=source.find('\n',field+1);source.erase(field,end==std::string::npos?std::string::npos:end-field);
                    }
                    const World::RulesSnapshot historical{source,true,true};
                    staged=w.init(64,64,20261123,1,2,{Faction::Allies,Faction::Soviet,Faction::Soviet},
                        0,nullptr,false,false,nullptr,{},&historical);
                }
            }
            if(!staged){check(false,"gap fixture initializes a valid source scene");continue;}
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
            for(auto& cell:w.map.cells)cell=Cell{};
            for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
            for(auto& player:w.players){player.money=0;player.defeated=false;}
            w.cratesEnabled=false;w.crates.clear();w.aiAlliance=scenario==4 || scenario==5;
            w.sharedVision=scenario==4;
            const EID gap=w.spawnBuilding(0,BldType::GapGenerator,20,20,true);
            if(scenario==7)w.spawnBuilding(0,BldType::GapGenerator,22,20,true);
            if(scenario!=3)w.spawnBuilding(0,BldType::PowerPlant,38,38,true);
            const bool satellite=scenario==2 || scenario==3 || scenario==8;
            const bool hasObserver=scenario!=2 && scenario!=3;
            if(satellite) {
                w.spawnBuilding(1,BldType::PowerPlant,2,2,true);
                w.spawnBuilding(1,BldType::SpySat,4,2,true);
            }
            const int scoutOwner=scenario==4 || scenario==5?2:1;
            EID scout=INVALID_EID;bool admitted=!hasObserver;
            if(hasObserver) {
                scout=w.spawnUnit(scoutOwner,scenario==1?UnitType::Kirov:UnitType::Engineer,10.5f,20.5f);
                staged &= w.valid(scout) && w.valid(gap);
                if(w.valid(scout)) {
                    w.ents[scout].atkCd=1000000;
                    World::Cmd move{};move.type=World::Cmd::Move;move.ids={scout};move.x=18.5f;move.y=20.5f;
                    w.bindCommand(move);w.applyCmd(scoutOwner,move);
                    admitted=w.ents[scout].state==UState::Moving || w.ents[scout].state==UState::AttackMoving;
                }
            }
            const std::string prefix="gap-"+std::to_string(scenario);
            FILE* ordered=std::fopen((output/(prefix+"-ordered.sav")).string().c_str(),"w+b");
            World replay;bool exact=ordered && w.saveGame(ordered);
            if(exact){std::rewind(ordered);exact=replay.loadGame(ordered) && replay.checksum()==w.checksum();}
            if(ordered)std::fclose(ordered);
            std::ofstream trace(output/(prefix+".trace"));trace<<w.checksum()<<'\n';
            bool seen=false,observerSeen=false,moved=false;int firstSeen=0;
            for(int tick=1;tick<=600;++tick) {
                w.update();if(exact){replay.update();exact=w.checksum()==replay.checksum();}
                if(w.map.fogAt(1,20,20)==FOG_VISIBLE){seen=true;if(!firstSeen)firstSeen=tick;}
                observerSeen |= w.map.fogAt(scoutOwner,20,20)==FOG_VISIBLE;
                if(w.valid(scout))moved |= w.ents[scout].x!=10.5f || w.ents[scout].y!=20.5f;
                trace<<w.checksum()<<'\n';
                w.presentationEvents.clear();w.evaQueue.clear();replay.presentationEvents.clear();replay.evaQueue.clear();
            }
            std::ofstream(output/(prefix+".final"))<<w.checksum()<<'\n';
            World clear=w;clear.map.bldOccRef=&clear.bldOcc;
            for(EID id=0;id<(EID)clear.ents.size();++id)if(clear.valid(id) && clear.ents[id].isBuilding
                && clear.ents[id].btype==BldType::GapGenerator)clear.kill(id,false);
            clear.update();
            const bool noGap=clear.map.fogAt(scoutOwner,20,20)==FOG_VISIBLE;
            SkirmishAI survey;survey.reset(1);think(survey,w);
            const bool remembered=std::any_of(survey.knownBuildings.begin(),survey.knownBuildings.end(),[](const auto& b) {
                return b.owner==0 && b.x==20 && b.y==20;
            });
            const bool expected=scenario!=2 && scenario!=5 && scenario!=6;
            const bool ok=staged && admitted && exact && seen==expected && remembered==expected
                && (!hasObserver || (moved && noGap)) && (scenario!=5 || observerSeen)
                && (scenario!=8 || firstSeen>1);
            failures+=!ok;
            std::printf("AI_SCOUT_GAP %s case=%d scene=%d staged=%d admitted=%d moved=%d seen=%d observer=%d clear=%d memory=%d first=%d exact=%d\n",
                ok?"PASS":"FAIL",scenario,w.nativeRulesBaseVersion,staged,admitted,moved,seen,observerSeen,noGap,remembered,firstSeen,exact);
            if(scenario==0 && w.valid(scout)) {
                World::Cmd leave{};leave.type=World::Cmd::Move;leave.ids={scout};leave.x=6.5f;leave.y=20.5f;
                w.bindCommand(leave);w.applyCmd(1,leave);
                FILE* midway=std::fopen((output/"gap-depart-midway.sav").string().c_str(),"w+b");
                World departure;bool resumed=midway && w.saveGame(midway);
                if(resumed){std::rewind(midway);resumed=departure.loadGame(midway) && departure.checksum()==w.checksum();}
                if(midway)std::fclose(midway);
                std::ofstream route(output/"gap-depart.trace");route<<w.checksum()<<'\n';
                for(int tick=0;tick<600;++tick) {
                    w.update();if(resumed){departure.update();resumed=w.checksum()==departure.checksum();}
                    route<<w.checksum()<<'\n';
                    w.presentationEvents.clear();w.evaQueue.clear();departure.presentationEvents.clear();departure.evaQueue.clear();
                }
                std::ofstream(output/"gap-depart.final")<<w.checksum()<<'\n';
                const bool departed=w.valid(scout) && w.ents[scout].x<10 && w.map.fogAt(1,20,20)==FOG_UNSEEN;
                const bool restored=departed && resumed;failures+=!restored;
                std::printf("AI_SCOUT_GAP_RETURN %s departed=%d shrouded=%d replay=%d\n",restored?"PASS":"FAIL",
                    departed,w.map.fogAt(1,20,20)==FOG_UNSEEN,resumed);
            }
        }
        if(const char* historical=std::getenv("OPENRA2_LEGACY_GAP51_INPUT_DIR")) {
            const auto directory=std::filesystem::path(historical);
            for(const char* prefix:{"gap-0","gap-1","gap-2","gap-3","gap-4","gap-5","gap-7","gap-8","gap-depart"}) {
                World archived;const std::string name=prefix;
                const auto ordered=directory/(name+(name=="gap-depart"?"-midway.sav":"-ordered.sav"));
                FILE* input=std::fopen(ordered.string().c_str(),"rb");bool exact=input && archived.loadGame(input);if(input)std::fclose(input);
                std::ifstream route(directory/(name+".trace")),final(directory/(name+".final"));uint64_t expected=0;
                exact &= archived.nativeRulesBaseVersion==51 && bool(route>>expected) && archived.checksum()==expected;
                for(int tick=1;tick<=600 && exact;++tick) {
                    archived.update();exact=bool(route>>expected) && archived.checksum()==expected;
                    archived.presentationEvents.clear();archived.evaQueue.clear();
                }
                exact &= bool(final>>expected) && archived.checksum()==expected;
                failures+=!exact;std::printf("AI_GAP_LEGACY51 %s name=%s exact=%d\n",exact?"PASS":"FAIL",prefix,exact);
            }
        }
        if(const char* historical=std::getenv("OPENRA2_LEGACY_GAP_INPUT_DIR")) {
            const auto directory=std::filesystem::path(historical);World archived;
            FILE* input=std::fopen((directory/"gap-6-ordered.sav").string().c_str(),"rb");
            bool exact=input && archived.loadGame(input);if(input)std::fclose(input);
            std::ifstream trace(directory/"gap-6.trace"),final(directory/"gap-6.final");uint64_t expected=0;
            exact &= archived.nativeRulesBaseVersion==50 && bool(trace>>expected) && archived.checksum()==expected;
            for(int tick=1;tick<=600 && exact;++tick){archived.update();exact=bool(trace>>expected) && archived.checksum()==expected;}
            exact &= bool(final>>expected) && archived.checksum()==expected;
            check(exact,"the actual Scene50 gap save retains all601 recorded checksums");
        }
    }
    {
        const char* requested=std::getenv("OPENRA2_GAP_AUDIT_DIR");
        const std::filesystem::path output=requested?requested:outputDir;
        std::vector<uint8_t> originalMask(64*64,0);bool fixture=true;int originalCount=0;
        FILE* reference=std::fopen("tests/fixtures/ra2-gap-radius10.csv","r");
        if(reference) {
            char header[64];fixture=std::fgets(header,sizeof(header),reference)!=nullptr;int dx=0,dy=0;
            while(std::fscanf(reference,"%d,%d",&dx,&dy)==2) {
                if(std::abs(dx)>12 || std::abs(dy)>12){fixture=false;continue;}
                auto& cell=originalMask[(32+dy)*64+32+dx];fixture &= cell==0;cell=1;++originalCount;
            }
            fixture &= std::feof(reference)!=0;std::fclose(reference);
        } else fixture=false;
        fixture &= originalCount==373;
        World w;bool setup=w.init(64,64,20261123,2,0,{Faction::Allies,Faction::Allies},0,nullptr,
            false,false,"assets/rules/profiles/ra2.ini");
        if(!setup){check(false,"gap area fixture installs source rules");return failures;}
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& player:w.players){player.money=0;player.defeated=false;}
        w.cratesEnabled=false;w.crates.clear();w.aiAlliance=false;w.sharedVision=false;
        const EID gap=w.spawnBuilding(0,BldType::GapGenerator,32,32,true);
        w.spawnBuilding(0,BldType::PowerPlant,54,54,true);
        w.spawnBuilding(1,BldType::PowerPlant,2,2,true);
        w.spawnBuilding(1,BldType::SpySat,5,2,true);w.update();
        setup &= w.valid(gap) && w.isEnemy(1,0) && !w.players[0].lowPower() && !w.players[1].lowPower()
            && w.hasBld(1,BldType::SpySat);
        int covered=0,mismatches=0;
        std::ofstream observed(output/"gap-area-observed.csv");observed<<"dx,dy,shrouded\n";
        for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
            const bool shrouded=w.map.fogAt(1,x,y)==FOG_UNSEEN;
            covered+=shrouded;mismatches+=shrouded!=(originalMask[y*64+x]!=0);
            observed<<x-32<<','<<y-32<<','<<shrouded<<'\n';
        }
        const auto target=w.entityRef(gap);
        FILE* ordered=std::fopen((output/"gap-area-ordered.sav").string().c_str(),"w+b");
        World resumed;bool exact=ordered && w.saveGame(ordered);
        if(exact){std::rewind(ordered);exact=resumed.loadGame(ordered) && resumed.checksum()==w.checksum();}
        if(ordered)std::fclose(ordered);
        constexpr int sellAt=120;
        std::ofstream(output/"gap-area.commands")<<sellAt<<'\n';
        std::ofstream trace(output/"gap-area.trace");trace<<w.checksum()<<'\n';bool admitted=false;
        const auto sell=[](World& world,EID id) {
            World::Cmd command{};command.type=World::Cmd::SellBuilding;command.ids={id};
            world.bindCommand(command);world.applyCmd(0,command);
            return std::any_of(world.presentationEvents.begin(),world.presentationEvents.end(),[](const auto& event) {
                return event.kind==PresentationKind::BuildingOrder && event.building.action==BuildingAction::Sell && event.building.accepted;
            });
        };
        for(int frame=1;frame<=600;++frame) {
            if(frame==sellAt){admitted=sell(w,target.id);if(exact)exact=sell(resumed,target.id);}
            w.update();if(exact){resumed.update();exact=w.checksum()==resumed.checksum();}
            trace<<w.checksum()<<'\n';
            w.presentationEvents.clear();w.evaQueue.clear();resumed.presentationEvents.clear();resumed.evaQueue.clear();
        }
        std::ofstream(output/"gap-area.final")<<w.checksum()<<'\n';
        bool clear=!w.valid(target);for(auto fog:w.map.fog[1])clear &= fog==FOG_VISIBLE;
        const bool ok=fixture && setup && covered==373 && mismatches==0 && admitted && clear && exact;failures+=!ok;
        std::printf("AI_GAP_AREA %s scene=%d fixture=%d setup=%d covered=%d expected=%d mismatches=%d sell=%d clear=%d replay=%d\n",
            ok?"PASS":"FAIL",w.nativeRulesBaseVersion,fixture,setup,covered,originalCount,mismatches,admitted,clear,exact);
        if(const char* legacy=std::getenv("OPENRA2_LEGACY_GAP_AREA_INPUT_DIR")) {
            const auto directory=std::filesystem::path(legacy);World archived;
            FILE* input=std::fopen((directory/"gap-area-ordered.sav").string().c_str(),"rb");
            bool same=input && archived.loadGame(input);if(input)std::fclose(input);
            const EID archivedGap=archived.bldAt(32,32);
            std::ifstream route(directory/"gap-area.trace"),final(directory/"gap-area.final"),commands(directory/"gap-area.commands");
            uint64_t expected=0;int when=0;
            same &= archived.nativeRulesBaseVersion==51 && archived.valid(archivedGap) && bool(commands>>when)
                && when>0 && when<=600 && bool(route>>expected) && expected==archived.checksum();
            for(int frame=1;frame<=600 && same;++frame) {
                if(frame==when)same=sell(archived,archivedGap);
                archived.update();same=same && bool(route>>expected) && archived.checksum()==expected;
                archived.presentationEvents.clear();archived.evaQueue.clear();
            }
            same &= bool(final>>expected) && archived.checksum()==expected;
            check(same,"the actual Scene51 area and Sell schedule retain all601 checksums");
        }
    }
    // Distinguish revisit priority from hidden enemy targeting. Both islands
    // are remembered, stale and unreachable by our ground force. Only the
    // saved observation ages distinguish the farther island from the nearer.
    for(int scenario=0;scenario<4;++scenario) {
        const bool legacy=scenario>=2,unseen=scenario%2!=0;
        const char* archive=std::getenv("OPENRA2_LEGACY_BOARD59_INPUT_DIR");if(legacy && !archive)continue;
        World w;bool setup=false;
        if(legacy) {
            FILE* f=std::fopen((std::filesystem::path(archive)/"foot-n-ordered.sav").string().c_str(),"rb");
            setup=f && w.loadGame(f);if(f)std::fclose(f);setup &= w.nativeRulesBaseVersion==59;
        } else setup=w.init(64,64,20261124,1,2,{Faction::Soviet,Faction::Soviet,Faction::Soviet},0,nullptr,false,false,
            "assets/rules/profiles/ra2.ini");
        if(!setup){check(false,"isolated revisit fixture installs the intended source contract");continue;}
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Water;}
        const auto land=[&](int x0,int y0,int x1,int y1){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)w.map.at(x,y).terrain=Terrain::Clear;};
        land(2,48,11,57);land(24,22,27,25);land(46,8,49,11);land(52,50,60,58);
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        for(auto& p:w.players){p.money=0;p.active=true;p.defeated=false;}w.players[1].defeated=true;
        w.aiAlliance=false;w.cratesEnabled=false;w.crates.clear();w.effects.clear();w.presentationEvents.clear();w.evaQueue.clear();
        w.spawnBuilding(2,BldType::ConYard,3,49,true);const EID hiddenBase=w.spawnBuilding(0,BldType::ConYard,54,52,true);
        w.spawnBuilding(2,BldType::TeslaReactor,3,54,true);w.spawnUnit(2,UnitType::Rhino,6.5f,52.5f);
        const EID bomber=w.spawnUnit(2,UnitType::Kirov,15.5f,24.5f);w.tick=4000;
        SkirmishAI survey;survey.reset(2);survey.scoutMapW=survey.scoutMapH=64;survey.scoutObserved.assign(w.map.cells.size(),w.tick+1);
        survey.pcfg.useEngineers=survey.pcfg.useSpies=survey.pcfg.useSuperWeapon=false;
        for(int y=50;y<=58;++y)for(int x=52;x<=60;++x)w.map.fog[2][w.cellIdx(x,y)]=FOG_SEEN;
        for(int y=22;y<=25;++y)for(int x=24;x<=27;++x){w.map.fog[2][w.cellIdx(x,y)]=FOG_SEEN;survey.scoutObserved[w.cellIdx(x,y)]=2100;}
        for(int y=8;y<=11;++y)for(int x=46;x<=49;++x){w.map.fog[2][w.cellIdx(x,y)]=unseen?FOG_UNSEEN:FOG_SEEN;survey.scoutObserved[w.cellIdx(x,y)]=unseen?0:17;}
        setup &= w.valid(bomber) && w.nativeTravelEnabled() && survey.knowledgeValid(w);
        const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);
        const std::string name=std::string(legacy?"island-revisit59":"island-revisit60")+(unseen?"-unseen":"-seen");
        auto saved=std::make_unique<Game>(),initial=std::make_unique<Game>(),active=std::make_unique<Game>();
        const auto snapshot=[&](const char* phase,Game& replay) {
            saved->world=w;saved->world.map.bldOccRef=&saved->world.bldOcc;saved->ais={survey};saved->phase=Phase::InGame;
            saved->cfgRules=RulesChoice::RA2;saved->cfgMapSize=64;saved->cfgAI=2;saved->cfgCountry=(int)Country::Russia;
            const auto path=(output/(name+phase+"-app11.sav")).string();
            return saved->saveGameFile(path.c_str()) && replay.loadGameFile(path.c_str()) && replay.ais.size()==1
                && replay.world.checksum()==w.checksum() && replay.ais[0]==survey;
        };
        bool exact=snapshot("-ordered",*initial);think(survey,w);if(exact){think(initial->ais[0],initial->world);exact=w.checksum()==initial->world.checksum() && survey==initial->ais[0];}
        const auto& scout=w.ents[bomber];const bool near=scout.goalX>=24 && scout.goalX<28 && scout.goalY>=22 && scout.goalY<26;
        const bool far=scout.goalX>=46 && scout.goalX<50 && scout.goalY>=8 && scout.goalY<12;
        const bool priority=legacy?near:far;const float goalX=scout.goalX,goalY=scout.goalY;bool midway=snapshot("-active",*active);
        std::printf("AI_ISLAND_REVISIT_CONTEXT scene=%d state=%d target=%d known=%zu base=%d fog=%d\n",
            w.nativeRulesBaseVersion,(int)scout.state,scout.target,survey.knownBuildings.size(),hiddenBase,(int)w.map.fogAt(2,54,52));
        std::ofstream trace(output/(name+".trace"));trace<<w.checksum()<<'\n';
        for(int at=0;at<600;++at) {
            w.update();survey.update(w);
            if(exact){initial->world.update();initial->ais[0].update(initial->world);exact=w.checksum()==initial->world.checksum() && survey==initial->ais[0];}
            if(midway){active->world.update();active->ais[0].update(active->world);midway=w.checksum()==active->world.checksum() && survey==active->ais[0];}
            trace<<w.checksum()<<'\n';
            w.presentationEvents.clear();w.evaQueue.clear();initial->world.presentationEvents.clear();initial->world.evaQueue.clear();active->world.presentationEvents.clear();active->world.evaQueue.clear();
        }
        std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
        const bool ok=setup && priority && exact && midway;failures+=!ok;
        std::printf("AI_ISLAND_REVISIT %s scene=%d unseen=%d setup=%d near=%d far=%d priority=%d goal=%.1f,%.1f replay=%d active=%d\n",
            ok?"PASS":"FAIL",w.nativeRulesBaseVersion,unseen,setup,near,far,priority,goalX,goalY,exact,midway);
    }
    return failures;
}
