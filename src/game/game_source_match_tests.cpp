#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <unordered_map>

int GameRegression::sourceMatchRegression(const char* outputDir,int ticks,int editionFilter,int terrainFilter) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("SOURCE_MATCH %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    for(int edition=0;edition<2;++edition)for(int terrain:{0,1}) {
        if((editionFilter>=0 && editionFilter!=edition) || (terrainFilter>=0 && terrainFilter!=terrain))continue;
        const std::string name=std::string(edition?"yr":"ra2")+"-"+std::to_string(terrain);
        const auto directory=std::filesystem::path(outputDir)/name;std::filesystem::create_directories(directory);
        World w;const auto profile=std::string("assets/rules/profiles/")+(edition?"yr":"ra2")+".ini";
        const uint64_t seed=terrain?941:922;
        const bool started=w.init(64,64,seed,0,2,{Faction::Allies,edition?Faction::Yuri:Faction::Soviet},terrain,nullptr,false,false,profile.c_str(),
                                  {Country::America,edition?Country::Yuri:Country::Russia});
        int buildings=0;for(const auto& e:w.ents)buildings+=e.alive && e.isBuilding && e.player>=0;
        check(started && buildings==0 && w.players.size()==2 && w.players[0].money==10000 && w.players[1].money==10000,
              "two source AIs start on the generated map with ordinary funds, construction vehicles and no supplied base");
        if(!started)continue;
        for(int loser=0;loser<2;++loser){World terminal=w;terminal.map.bldOccRef=&terminal.bldOcc;
            for(EID id=0;id<(EID)terminal.ents.size();++id)if(terminal.valid(id) && terminal.ents[id].player==loser)terminal.kill(id,false);
            for(int frame=0;frame<120;++frame)terminal.update();
            check(terminal.players[loser].defeated && terminal.matchOutcome(loser)==MatchOutcome::Defeat && terminal.matchOutcome(1-loser)==MatchOutcome::Victory,
                  "source rules preserve ordinary elimination and settlement for either player index");}
        std::vector<SkirmishAI> bots(2);for(int p=0;p<2;++p){bots[p].reset(p);bots[p].difficulty=AIDiff::Normal;}
        Game resumed;int resumeLeft=0,snapshots=0;bool exact=true,typed=true;std::vector<std::string> checkpointFiles;
        std::array<std::vector<uint8_t>,2> home;
        for(int p=0;p<2;++p){home[p].assign(w.map.cells.size(),0);Vec2i start{-1,-1};
            for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.player==p && e.utype==UnitType::MCV){start={(int)e.x,(int)e.y};break;}
            if(start.x<0)continue;std::vector<Vec2i> queue{start};home[p][w.cellIdx(start.x,start.y)]=1;
            constexpr Vec2i directions[]={{1,0},{-1,0},{0,1},{0,-1}};
            for(size_t head=0;head<queue.size();++head)for(auto d:directions){const auto from=queue[head];const Vec2i to{from.x+d.x,from.y+d.y};
                if(!w.map.passable(to.x,to.y) || !w.map.climbOk(from.x,from.y,to.x,to.y,0) || home[p][w.cellIdx(to.x,to.y)])continue;
                home[p][w.cellIdx(to.x,to.y)]=1;queue.push_back(to);}
        }
        int firstAir=-1,firstBoard=-1,firstForeign=-1;std::unordered_map<std::string,int> airPassengers;
        std::vector<double> timings;timings.reserve(ticks);uint64_t resolved=0;
        const auto saveWorld=[&](const char* suffix){const auto path=(directory/suffix).string();FILE* f=std::fopen(path.c_str(),"wb");bool ok=f && w.saveGame(f);if(f)std::fclose(f);return ok;};
        check(saveWorld("start-world56.sav"),"the unmodified ordinary opening is captured before either AI issues an order");
        const auto telemetryPath=(directory/"telemetry.csv").string();FILE* telemetry=std::fopen(telemetryPath.c_str(),"w");
        if(telemetry)std::fprintf(telemetry,"tick,player,money,income,produced,damage,buildings,units,aircraft,cargo,waves\n");
        for(int frame=0;frame<ticks;++frame){
            const auto begin=std::chrono::steady_clock::now();w.update();for(auto& bot:bots)bot.update(w);
            timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
            if(resumeLeft>0){resumed.world.update();for(auto& bot:resumed.ais)bot.update(resumed.world);
                exact &= w.checksum()==resumed.world.checksum() && bots==resumed.ais;--resumeLeft;}
            for(EID id=0;id<(EID)w.ents.size();++id){const auto& e=w.ents[id];if(!e.alive)continue;
                typed &= e.nativeIdentity.definition && e.nativeIdentity.definition->rulesOnly;
                if(!e.isBuilding && unitDef(e).isAir() && firstAir<0)firstAir=frame;
                if(!e.isBuilding && e.utype==UnitType::Nighthawk && !e.cargo.empty()){
                    if(firstBoard<0)firstBoard=frame;for(const auto& passenger:e.cargo)airPassengers[passenger.nativeIdentity.object]=e.player;}
                if(firstForeign<0 && !e.isBuilding && w.map.inBounds((int)e.x,(int)e.y)){
                    const auto passenger=airPassengers.find(e.nativeIdentity.object);
                    if(passenger!=airPassengers.end() && !home[passenger->second][w.cellIdx((int)e.x,(int)e.y)])firstForeign=frame;}
            }
            if(w.tick%6000==0){for(int p=0;p<2;++p){int b=0,u=0,a=0,c=0;for(const auto& e:w.ents)if(e.alive && e.player==p){b+=e.isBuilding;u+=!e.isBuilding;a+=!e.isBuilding && unitDef(e).isAir();c+=(int)e.cargo.size();}
                std::printf("SOURCE_MATCH HEALTH name=%s tick=%llu player=%d money=%lld income=%lld produced=%llu damage=%llu buildings=%d units=%d air=%d cargo=%d waves=%d\n",name.c_str(),(unsigned long long)w.tick,p,(long long)w.players[p].money,(long long)w.harvestedIncome[p],(unsigned long long)w.producedUnits[p],(unsigned long long)w.combatDamage[p],b,u,a,c,bots[p].attackWave);
                if(telemetry)std::fprintf(telemetry,"%llu,%d,%lld,%lld,%llu,%llu,%d,%d,%d,%d,%d\n",(unsigned long long)w.tick,p,(long long)w.players[p].money,(long long)w.harvestedIncome[p],(unsigned long long)w.producedUnits[p],(unsigned long long)w.combatDamage[p],b,u,a,c,bots[p].attackWave);
            }std::fflush(stdout);}
            if(w.tick==300 || w.tick%9000==0){Game saved;saved.world=w;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.ais=bots;saved.phase=Phase::InGame;
                saved.cfgRules=edition?RulesChoice::YR:RulesChoice::RA2;saved.cfgAI=1;saved.cfgMapSize=64;saved.cfgMapType=terrain;saved.cfgMoney=10000;saved.localPlayer=0;
                saved.cfgCountry=(int)w.players[0].country;saved.cfgColor=w.players[0].colorId;
                saved.cfgCrates=w.cratesEnabled;saved.cfgAlliance=w.aiAlliance;saved.cfgSharedVision=w.sharedVision;
                saved.cfgShortGame=w.shortGame;saved.cfgSuperweapons=w.superweaponsEnabled;saved.cfgMcvRepacks=w.mcvRepacks;
                saved.matchSetup=saved.currentMatchSetup(seed);
                const auto path=(directory/("tick-"+std::to_string(w.tick)+"-app11.sav")).string();
                const bool written=saved.saveGameFile(path.c_str()),loaded=written && resumed.loadGameFile(path.c_str());
                const auto resaved=(directory/("tick-"+std::to_string(w.tick)+"-resaved-app11.sav")).string();
                const bool restored=loaded && resumed.world.checksum()==w.checksum() && resumed.ais==bots;
                const bool rewritten=restored && resumed.saveGameFile(resaved.c_str()),reloaded=rewritten && resumed.loadGameFile(resaved.c_str());
                const bool ok=reloaded && resumed.ais.size()==2;
                if(!ok)std::printf("SOURCE_MATCH CHECKPOINT_FAIL name=%s tick=%llu write=%d load=%d exact=%d resave=%d reload=%d\n",
                    name.c_str(),(unsigned long long)w.tick,written,loaded,restored,rewritten,reloaded);
                exact &= ok && resumed.world.checksum()==w.checksum() && resumed.ais==bots;
                if(ok){++snapshots;resumeLeft=180;checkpointFiles.push_back(std::filesystem::path(path).filename().string());
                    checkpointFiles.push_back(std::filesystem::path(resaved).filename().string());}}
            w.presentationEvents.clear();w.evaQueue.clear();resumed.world.presentationEvents.clear();resumed.world.evaQueue.clear();
            if(w.matchOutcome(0)!=MatchOutcome::Running || w.matchOutcome(1)!=MatchOutcome::Running){resolved=w.tick;break;}
        }
        if(telemetry)std::fclose(telemetry);
        World motion=w;motion.map.bldOccRef=&motion.bldOcc;
        const auto motionPath=(directory/"motion-240.csv").string();if(FILE* f=std::fopen(motionPath.c_str(),"w")){
            std::fprintf(f,"frame,id,type,x,y,state,goal_x,goal_y,path_left,throttle,target_throttle,held_speed,credit,jump_phase,jump_speed,jump_desired,jump_goal_x,jump_goal_y\n");
            for(int frame=0;frame<=240;++frame){if(frame%30==0)for(EID id=0;id<(EID)motion.ents.size();++id){const auto& e=motion.ents[id];
                if(!e.alive || e.isBuilding || (e.utype!=UnitType::Kirov && e.utype!=UnitType::Typhoon && e.utype!=UnitType::Dreadnought && e.utype!=UnitType::Destroyer))continue;
                std::fprintf(f,"%d,%d,%s,%.6f,%.6f,%d,%.6f,%.6f,%d,%.9g,%.9g,%d,%.9g,%d,%.9g,%.9g,%.6f,%.6f\n",frame,id,e.nativeIdentity.definition?e.nativeIdentity.definition->id.c_str():"UNTYPED",e.x,e.y,(int)e.state,e.goalX,e.goalY,std::max(0,(int)e.path.size()-e.pathIdx),e.nativeMovement.throttle,e.nativeMovement.targetThrottle,e.nativeMovement.heldSpeed,e.nativeMovement.credit,(int)e.nativeJumpjet.phase,e.nativeJumpjet.speed,e.nativeJumpjet.desiredSpeed,e.nativeJumpjet.goalX/256.,e.nativeJumpjet.goalY/256.);}
                if(frame<240){motion.update();motion.presentationEvents.clear();motion.evaQueue.clear();}}
            std::fclose(f);}
        check(saveWorld("final-world56.sav"),"the final observed source match state is saved without forcing a winner");
        check(typed && w.harvestedIncome[0]>0 && w.harvestedIncome[1]>0 && w.producedUnits[0]>0 && w.producedUnits[1]>0,
              "both source AIs naturally harvest and train through typed source actors");
        check(snapshots>0 && exact,"actual App11 checkpoints preserve both AI brains through load, resave and continued simulation decisions");
        check(resolved>0,"the ordinary source match reaches an actual engine victory or defeat within the observation budget");
        std::sort(timings.begin(),timings.end());const double p95=timings.empty()?0:timings[(timings.size()-1)*95/100];
        std::printf("SOURCE_MATCH RESULT name=%s tick=%llu resolved=%llu outcome0=%d outcome1=%d air=%d board=%d foreign=%d snapshots=%d exact=%d typed=%d p95_ms=%.3f\n",name.c_str(),(unsigned long long)w.tick,(unsigned long long)resolved,(int)w.matchOutcome(0),(int)w.matchOutcome(1),firstAir,firstBoard,firstForeign,snapshots,exact,typed,p95);
        const auto resultPath=(directory/"result.json").string();if(FILE* f=std::fopen(resultPath.c_str(),"w")){
            std::fprintf(f,"{\"name\":\"%s\",\"seed\":%llu,\"ticks\":%llu,\"resolved\":%llu,\"outcome0\":%d,\"outcome1\":%d,\"first_air\":%d,\"first_board\":%d,\"first_foreign_passenger\":%d,\"snapshots\":%d,\"exact\":%s,\"typed\":%s,\"p95_tick_ms\":%.6f,\"checkpoints\":[",name.c_str(),(unsigned long long)seed,(unsigned long long)w.tick,(unsigned long long)resolved,(int)w.matchOutcome(0),(int)w.matchOutcome(1),firstAir,firstBoard,firstForeign,snapshots,exact?"true":"false",typed?"true":"false",p95);
            for(size_t i=0;i<checkpointFiles.size();++i)std::fprintf(f,"%s\"%s\"",i?",":"",checkpointFiles[i].c_str());std::fprintf(f,"]}\n");std::fclose(f);}
        const auto actorsPath=(directory/"actors.csv").string();if(FILE* f=std::fopen(actorsPath.c_str(),"w")){
            std::fprintf(f,"id,owner,type,building,x,y,hp,state,target,goal_x,goal_y,path_left,cargo,air_pending,layer,altitude,damage,sight,goal_fog,guard,frozen\n");
            for(EID id=0;id<(EID)w.ents.size();++id){const auto& e=w.ents[id];if(!e.alive)continue;
                std::fprintf(f,"%d,%d,%s,%d,%.4f,%.4f,%d,%d,%d,%.4f,%.4f,%d,%zu,%d,%d,%.4f,%d,%d,%d,%d,%d\n",id,e.player,e.nativeIdentity.definition?e.nativeIdentity.definition->id.c_str():"UNTYPED",e.isBuilding,e.x,e.y,e.hp,(int)e.state,e.target,e.goalX,e.goalY,std::max(0,(int)e.path.size()-e.pathIdx),e.cargo.size(),e.nativeAirUnload,(int)e.navLayer,e.nativeAltitude,e.isBuilding?bldDef(e).weapon.damage:unitDef(e).weapon.damage,e.isBuilding?bldDef(e).sight:unitDef(e).sight,e.player>=0?w.map.fogAt(e.player,(int)e.goalX,(int)e.goalY):FOG_UNSEEN,e.guard,e.immobilized());}
            std::fclose(f);}
        const auto brainsPath=(directory/"brains.csv").string();if(FILE* f=std::fopen(brainsPath.c_str(),"w")){
            std::fprintf(f,"player,x,y,type,center_fog,anchor_fog\n");for(const auto& bot:bots)for(const auto& b:bot.knownBuildings)
                std::fprintf(f,"%d,%d,%d,%d,%d,%d\n",bot.player,b.x,b.y,(int)b.type,w.map.fogAt(bot.player,b.x+bldDef(b.type).w/2,b.y+bldDef(b.type).h/2),w.map.fogAt(bot.player,b.x,b.y));std::fclose(f);}
    }
    return failures;
}
