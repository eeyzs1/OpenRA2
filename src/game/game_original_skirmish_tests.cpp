#include "game_regression.h"
#include "game/skirmish_map.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include "core/content.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

int GameRegression::originalSkirmishRegression(const char* outputDir,const char* inspectWorld) {
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("ORIGINAL_SKIRMISH %s %s\n",ok?"PASS":"FAIL",message);
        std::fflush(stdout);failures+=!ok;return ok;};
    if(inspectWorld){FILE* in=std::fopen(inspectWorld,"rb");bool loaded=in && world.loadGame(in);if(in)std::fclose(in);
        if(!loaded)loaded=loadGameFile(inspectWorld);
        if(!check(loaded,"diagnostic reads the captured natural World"))return failures;
        std::printf("ORIGINAL_DIAG scene=%d production=%d oreValue=%d gemValue=%d\n",world.nativeSceneVersion,
            world.nativeProductionRules.version,world.nativeProductionRules.oreValue,world.nativeProductionRules.gemValue);
        const auto file=(std::filesystem::path(outputDir)/"actors.csv").string();FILE* out=std::fopen(file.c_str(),"w");if(!out)return 1;
        std::fprintf(out,"id,player,type,building,x,y,hp,state,target,goalX,goalY,path,attackMove,attackX,attackY,air,frozen,altitude,jumpPhase,jumpSpeed,jumpDesired,jumpGoalX,jumpGoalY,jumpGround,jumpHeight\n");
        for(EID id=0;id<(EID)world.ents.size();++id){const auto& e=world.ents[id];if(!e.alive || e.player<0)continue;
            const auto& j=e.nativeJumpjet;
            const char* name=e.nativeIdentity.definition?e.nativeIdentity.definition->id.c_str():e.isBuilding?bldDef(e).name:unitDef(e).name;
            std::fprintf(out,"%d,%d,%s,%d,%.3f,%.3f,%d,%d,%d,%.3f,%.3f,%d,%d,%.3f,%.3f,%d,%d,%.3f,%d,%.9g,%.9g,%.3f,%.3f,%d,%d\n",id,e.player,name,e.isBuilding,e.x,e.y,e.hp,(int)e.state,e.target,e.goalX,e.goalY,std::max(0,(int)e.path.size()-e.pathIdx),e.attackMove.active,e.attackMove.x,e.attackMove.y,!e.isBuilding && unitDef(e).isAir(),e.immobilized(),e.nativeAltitude,(int)j.phase,j.speed,j.desiredSpeed,j.goalX/256.,j.goalY/256.,j.ground,j.goalHeight);}
        std::fclose(out);
        for(int p=0;p<world.numPlayers;++p){const auto& player=world.players[p];const auto& pr=player.unitProd[1];
            std::printf("ORIGINAL_DIAG player=%d money=%lld power=%d/%d factory=%d lab=%d buildings=%d readyBld=%d activeUnit=%d unitType=%d progress=%d paid=%lld readyUnit=%d queue=%zu\n",p,(long long)player.money,player.powerMade,player.powerUsed,world.countBlds(p,BldType::WarFactory),world.countBlds(p,BldType::BattleLab),world.countBlds(p,BldType::ConYard),player.bldProd.ready,pr.active,pr.typeIdx,pr.progress,(long long)pr.paid,pr.ready,player.unitQueue[1].size());}
        const auto brains=(std::filesystem::path(outputDir)/"brains.csv").string();if(FILE* f=std::fopen(brains.c_str(),"w")){
            std::fprintf(f,"player,rallyX,rallyY,rallyWait,attackTimer,wave,knownOwner,knownType,knownX,knownY\n");
            for(const auto& ai:ais)for(const auto& b:ai.knownBuildings)std::fprintf(f,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",ai.player,ai.rallyPoint.x,ai.rallyPoint.y,ai.rallyCheckTimer,ai.attackTimer,ai.attackWave,b.owner,(int)b.type,b.x,b.y);
            std::fclose(f);}
        if(const char* requested=std::getenv("OPENRA2_INSPECT_CONTINUE_TICKS")) {
            const int ticks=std::atoi(requested);if(ticks<1 || ticks>180000)return 1;
            World stepped=world;stepped.map.bldOccRef=&stepped.bldOcc;auto bots=ais;const auto original=world.checksum();
            const auto trace=(std::filesystem::path(outputDir)/"continuation.csv").string();FILE* f=std::fopen(trace.c_str(),"w");if(!f)return 1;
            std::fprintf(f,"tick,id,player,type,x,y,hp,state,target,goalX,goalY,attackMove,jumpPhase,speed,desiredSpeed,facing,desiredFacing,dx,dy,height,ground\n");
            for(int frame=0;frame<=ticks;++frame){
                if(frame%(std::max(1,ticks/20))==0)for(EID id=0;id<(EID)stepped.ents.size();++id){const auto& e=stepped.ents[id];
                    if(!e.alive || e.player<0 || e.isBuilding || (e.player!=0 && !unitDef(e).isAir() && !e.attackMove.active))continue;
                    const auto& j=e.nativeJumpjet;const char* name=e.nativeIdentity.definition?e.nativeIdentity.definition->id.c_str():unitDef(e).name;
                    std::fprintf(f,"%llu,%d,%d,%s,%.6f,%.6f,%d,%d,%d,%.6f,%.6f,%d,%d,%.9g,%.9g,%u,%u,%d,%d,%.3f,%d\n",
                        (unsigned long long)stepped.tick,id,e.player,name,e.x,e.y,e.hp,(int)e.state,e.target,e.goalX,e.goalY,e.attackMove.active,
                        (int)j.phase,j.speed,j.desiredSpeed,j.motion.facing,j.motion.desiredFacing,j.motion.dx,j.motion.dy,e.nativeAltitude,j.ground);}
                if(frame==ticks)break;
                stepped.update();for(auto& bot:bots)bot.update(stepped);
                stepped.presentationEvents.clear();stepped.evaQueue.clear();
            }
            std::fclose(f);
            std::printf("ORIGINAL_DIAG continuationTick=%llu outcome0=%d originalUnchanged=%d\n",(unsigned long long)stepped.tick,
                (int)stepped.matchOutcome(0),world.checksum()==original);
            check(world.checksum()==original,"continuation diagnostic advances only the captured copy");
        }
        return failures;
    }
    selectRulesChoice(RulesChoice::RA2);
    for(const auto& choice:OriginalSkirmishMaps::choices) {
        OriginalSkirmishMaps::Data data;
        if(!check(OriginalSkirmishMaps::load(choice.id,data,choice.players),"original map, starts and scenery metadata load without a generated substitute"))continue;
        std::string source;
        if(!check(composeRules(rulesInfo[(int)RulesChoice::RA2].source,data,source),"ordinary source rules compose with the identified original map"))continue;
        const World::RulesSnapshot snapshot{source,true,true};
        for(const char* base:{"37","38","43","44","45"}) {
            const auto declaration="RulesBaseVersion = "+std::to_string(nativeTravelSceneVersion);
            auto invalid=source;const auto position=invalid.find(declaration);
            if(position!=std::string::npos)invalid.replace(position,declaration.size(),std::string("RulesBaseVersion = ")+base);
            World rejected;const World::RulesSnapshot mismatch{invalid,true,true};
            check(position!=std::string::npos && !rejected.init(data.terrain.w,data.terrain.h,922,0,choice.players,
                std::vector<Faction>(choice.players,Faction::Allies),0,data.terrainPath.c_str(),false,true,nullptr,{},&mismatch)
                && rejected.ents.empty(),"an original-map base version cannot reinterpret the travel contract or select an experimental AI cohort");
        }
        std::vector<Country> countries{Country::America,Country::Russia,Country::France,Country::Iraq};countries.resize(choice.players);
        std::vector<Faction> factions;for(auto country:countries)factions.push_back(countryFaction(country));
        {
            World unsupported;const World::RulesSnapshot plain{rulesInfo[(int)RulesChoice::RA2].source,true,true};
            check(!unsupported.init(data.terrain.w,data.terrain.h,922,0,choice.players,factions,0,
                data.terrainPath.c_str(),false,true,nullptr,countries,&plain) && unsupported.ents.empty(),
                "a plain rules profile cannot manufacture an unbound native-map opening");
        }
        World sample;
        const bool opened=sample.init(data.terrain.w,data.terrain.h,922,0,choice.players,factions,0,
            data.terrainPath.c_str(),false,true,nullptr,countries,&snapshot);
        if(!check(opened,"an ordinary native-map opening installs original scenery and creates the normal starting armies"))continue;
        int props=0,units=0;bool positions=true;EID destructible=INVALID_EID;
        for(EID id=0;id<(EID)sample.ents.size();++id){const auto& e=sample.ents[id];if(!e.alive)continue;
            if(e.player<0){++props;positions &= e.nativeIdentity.definition && !e.nativeIdentity.definition->rulesOnly;
                if(e.nativeIdentity.definition && !e.nativeIdentity.definition->immune)destructible=id;}
            else {++units;positions &= !e.isBuilding && sample.map.passable((int)e.x,(int)e.y) && !sample.bldBlocked((int)e.x,(int)e.y)
                && e.x==e.px && e.y==e.py && e.nativeIdentity.definition && e.nativeIdentity.definition->rulesOnly;}}
        check(props==data.actors && units==choice.players*5 && positions,"original neutral props and normal armies have valid occupancy and initialized movement positions");
        check(sample.usesRulesProfile() && sample.nativeSceneVersion==nativeSkirmishMapSceneVersion
            && !sample.nativeEvents.active && !sample.nativeAIRules.isGlobal(),"the original-map opening retains ordinary AI and outcome policy");
        {
            World probe=sample;probe.map.bldOccRef=&probe.bldOcc;
            for(EID id=0;id<(EID)probe.ents.size();++id)if(probe.valid(id) && probe.ents[id].player>=0)probe.kill(id,false);
            const auto start=data.starts[1];const EID scout=probe.spawnUnit(1,UnitType::Kirov,start.x+.5f,start.y+.5f);
            probe.players[1].defeated=false;probe.shortGame=false;
            for(int tick=0;tick<120;++tick)probe.update();
            Vec2i destination=data.starts[0];
            for(int y=0;y<probe.map.h;++y)for(int x=0;x<probe.map.w;++x)if(probe.map.inBounds(x,y))probe.map.fog[1][probe.cellIdx(x,y)]=FOG_VISIBLE;
            probe.map.fog[1][probe.cellIdx(destination.x,destination.y)]=FOG_UNSEEN;
            for(auto& p:probe.players)p.money=0;SkirmishAI bot;bot.reset(1);
            for(int tick=0;tick<30;++tick)bot.update(probe);
            const auto& e=probe.ents[scout];
            std::printf("ORIGINAL_SCOUT_TRACE map=%s id=%d state=%d attackMove=%d goal=%.2f,%.2f air=%d phase=%d weapon=%d defeated=%d\n",choice.id,scout,(int)e.state,e.attackMove.active,e.goalX,e.goalY,unitDef(e).isAir(),(int)e.nativeJumpjet.phase,probe.effWeapon(e).damage,probe.players[1].defeated);
            check(probe.valid(scout) && probe.ents[scout].attackMove.active
                && probe.map.inBounds((int)probe.ents[scout].attackMove.x,(int)probe.ents[scout].attackMove.y),
                "air reconnaissance chooses a real original-map cell rather than the absent canvas");
        }
        for(int player=0;player<sample.numPlayers;++player){EID mcv=INVALID_EID;
            for(EID id=0;id<(EID)sample.ents.size();++id){const auto& e=sample.ents[id];
                if(e.alive && !e.isBuilding && e.player==player && e.utype==UnitType::MCV){mcv=id;break;}}
            World::Cmd cmd;cmd.type=World::Cmd::Deploy;cmd.ids={mcv};sample.applyCmd(player,cmd);
            check(sample.countBlds(player,BldType::ConYard)==1,"every original start actually deploys its source MCV");
        }
        if(std::string_view(choice.id)=="mp15s4"){bool disabled=true;int found=0;
            for(const auto& d:sample.nativeTypes)if(d->id=="GAYARD" || d->id=="NAYARD"){++found;disabled &= d->production.techLevel==-1;}
            check(found==2 && disabled,"Snow Valley retains its original map-local prohibition on naval yards");}
        // Destruction is a persistence mechanism fixture, separate from natural matches below.
        if(destructible!=INVALID_EID)sample.kill(destructible,false);
        FILE* save=tmpfile();World resumed;bool exact=save && sample.saveGame(save);
        if(exact){rewind(save);exact=resumed.loadGame(save) && resumed.checksum()==sample.checksum();}if(save)fclose(save);
        for(int tick=0;tick<300 && exact;++tick){sample.update();resumed.update();exact=sample.checksum()==resumed.checksum();
            sample.presentationEvents.clear();resumed.presentationEvents.clear();sample.evaQueue.clear();resumed.evaQueue.clear();}
        check(exact && (destructible==INVALID_EID || !resumed.valid(destructible)),"World saves continue exactly and never recreate a destroyed original prop");
    }
    SetTargetFPS(0);const int language=g_lang;
    const auto click=[&](float x,float y){sim={};sim.active=true;sim.pos=menuCanvasFromUi(x,y);sim.pressedL=true;render();sim.pressedL=false;render();};
    const auto picture=[&](const std::string& name){sim={};sim.active=true;sim.pos={-100,-100};const auto hash=world.checksum();
        shotFile=(std::filesystem::path(outputDir)/(name+".png")).string();render();return hash==world.checksum() && terrainTex.id;};
    for(int map=0;map<2;++map)for(auto edition:{RulesChoice::RA2,RulesChoice::YR}){
        selectRulesChoice(edition);clearOriginalMap();cfgReferenceMap=false;cfgAI=map?3:1;cfgMapSize=96;cfgCountry=(int)Country::America;
        aiCountry[0]=(int)Country::Russia;aiCountry[1]=(int)Country::France;aiCountry[2]=(int)Country::Iraq;
        cfgGameMode=map && edition==RulesChoice::YR?1:0;cfgMapType=0;cfgCrates=false;cfgMoney=10000;previewSeed=922+map;phase=Phase::Setup;previewDirty=true;
        sim={};sim.active=true;sim.pos={-100,-100};render();click(320,430);click(320,388+20.f*map);
        const std::string id=OriginalSkirmishMaps::choices[map].id;
        if(!check(cfgOriginalMap==id && !cfgReferenceMap && cfgMapType==0 && referenceMapReady && previewMap.native.assetId==id,
            "the real setup dropdown selects original terrain and legal player count"))continue;
        const auto prefix=id+"-"+std::to_string((int)edition);bool previews=true;
        for(int lang:{0,1}){g_lang=lang;sim.pos={-100,-100};shotFile=(std::filesystem::path(outputDir)/(prefix+"-setup-"+std::to_string(lang)+".png")).string();
            const auto before=world.checksum();render();previews &= previewTex.id && before==world.checksum();}g_lang=language;
        check(previews,"both languages show the actual original map preview without advancing simulation");
        const auto side=menuShellSide(),mon=menuShellMonitor();click(side.x+side.width*.5f,mon.y+mon.height+51.f);
        if(!check(phase==Phase::InGame && world.map.native.assetId==id && matchSetup && matchSetup->originalMap==id && world.numPlayers==(map?4:2),
            "normal Start opens the selected original map under either source edition"))continue;
        check(picture(prefix+"-opening"),"the normal first frame has original ground and does not alter simulation");
        const auto initial=world.checksum();const auto source=world.nativeScenarioSource;
        for(int tick=0;tick<30;++tick)world.update();const auto continued=world.checksum();
        const auto file=(std::filesystem::path(outputDir)/(prefix+"-app12.sav")).string();bool app=saveGameFile(file.c_str());
        std::ifstream input(file,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});input.close();
        clearOriginalMap();cfgMapSize=64;cfgAI=1;app &= bytes.starts_with("RA2GAM12") && loadGameFile(file.c_str());
        check(app && world.checksum()==continued && cfgOriginalMap==id && world.nativeScenarioSource==source,"App12 restores the map, rules, scenery and ordinary match configuration exactly");
        restartCurrentMatch();check(world.tick==0 && world.checksum()==initial && cfgOriginalMap==id,"Restart reproduces the original opening from its embedded map/rules snapshot");
        if(!bytes.empty()){bytes.replace(0,8,"RA2GAM11");const auto bad=(std::filesystem::path(outputDir)/(prefix+"-wrong-envelope.sav")).string();
            std::ofstream out(bad,std::ios::binary);out.write(bytes.data(),bytes.size());out.close();const auto before=world.checksum();
            check(!loadGameFile(bad.c_str()) && world.checksum()==before && cfgOriginalMap==id,"a map save with an incompatible envelope leaves the active game unchanged");}
        if(edition==RulesChoice::RA2){const auto terrain=contentResolve(("assets/maps/native/"+id+"/terrain.nmap").c_str());
            ScopedUserFile guard(std::filesystem::u8path(terrain));const auto before=world.checksum();bool refused=guard.ready();
            if(refused){std::filesystem::remove(std::filesystem::u8path(terrain));newGame(922+map);
                refused=phase==Phase::Setup && world.checksum()==before && !loadGameFile(file.c_str()) && world.checksum()==before;}
            check(refused && guard.restore(),"a missing original map refuses Start and load while retaining the current game");}
    }
    const auto held=world.checksum();phase=Phase::NetLobby;lobbyState=0;sim={};sim.active=true;sim.pos={-100,-100};render();
    check(cfgOriginalMap.empty() && !cfgReferenceMap && cfgMapSize==96 && world.checksum()==held,"entering the current LAN lobby normalizes local-only map choice without changing the current world");
    selectOriginalMap("mp15s4");selectRulesChoice(RulesChoice::Fusion);
    check(cfgOriginalMap.empty() && cfgMapSize==96,"choosing the extended fusion rules leaves the original source-map cohort");
    sim={};g_lang=language;
    return failures;
}

int GameRegression::originalSkirmishNaturalRegression(const char* outputDir,int mapIndex) {
    if(mapIndex<0 || mapIndex>=2)return 1;g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("ORIGINAL_NATURAL %s %s\n",ok?"PASS":"FAIL",message);std::fflush(stdout);failures+=!ok;return ok;};
    const auto& choice=OriginalSkirmishMaps::choices[mapIndex];OriginalSkirmishMaps::Data data;selectRulesChoice(RulesChoice::RA2);std::string source;
    if(!check(OriginalSkirmishMaps::load(choice.id,data,choice.players) && OriginalSkirmishMaps::composeRules(rulesInfo[(int)RulesChoice::RA2].source,data,source),"original map and ordinary RA2 rules load"))return failures;
    std::vector<Country> countries{Country::America,Country::Russia,Country::France,Country::Iraq};countries.resize(choice.players);
    std::vector<Faction> factions;for(auto country:countries)factions.push_back(countryFaction(country));
    const uint64_t seed=922+mapIndex;World sample;World::RulesSnapshot snapshot{source,true,true};
    if(!check(sample.init(data.terrain.w,data.terrain.h,seed,0,choice.players,factions,0,data.terrainPath.c_str(),false,true,nullptr,countries,&snapshot),"natural original-map MCV opening has ordinary funds and no supplied base"))return failures;
    sample.cratesEnabled=false;std::vector<SkirmishAI> bots(choice.players);for(int p=0;p<choice.players;++p){bots[p].reset(p);bots[p].difficulty=AIDiff::Normal;}
    Game resumed;int replayTicks=0,checkpoints=0;bool exact=true;int survivors=choice.players;uint64_t resolved=0,firstDamage=0;
    constexpr int budget=180000; // fixed before the first natural run; 100 minutes at 30 Hz
    const auto telemetryPath=(std::filesystem::path(outputDir)/(std::string(choice.id)+"-telemetry.csv")).string();FILE* telemetry=std::fopen(telemetryPath.c_str(),"w");
    if(telemetry)std::fprintf(telemetry,"tick,player,money,income,produced,damage,buildings,units,defeated\n");
    for(int frame=0;frame<budget;++frame){sample.update();for(auto& bot:bots)bot.update(sample);
        if(replayTicks>0){resumed.world.update();for(auto& bot:resumed.ais)bot.update(resumed.world);
            exact &= sample.checksum()==resumed.world.checksum() && bots==resumed.ais;--replayTicks;}
        uint64_t damage=0;for(int p=0;p<choice.players;++p)damage+=sample.combatDamage[p];if(!firstDamage && damage)firstDamage=sample.tick;
        if(sample.tick%6000==0){for(int p=0;p<choice.players;++p){int b=0,u=0;for(const auto& e:sample.ents)if(e.alive && e.player==p){b+=e.isBuilding;u+=!e.isBuilding;}
            std::printf("ORIGINAL_NATURAL HEALTH map=%s tick=%llu player=%d money=%lld income=%llu produced=%llu damage=%llu buildings=%d units=%d defeated=%d\n",choice.id,(unsigned long long)sample.tick,p,(long long)sample.players[p].money,(unsigned long long)sample.harvestedIncome[p],(unsigned long long)sample.producedUnits[p],(unsigned long long)sample.combatDamage[p],b,u,sample.players[p].defeated);
            if(telemetry)std::fprintf(telemetry,"%llu,%d,%lld,%llu,%llu,%llu,%d,%d,%d\n",(unsigned long long)sample.tick,p,(long long)sample.players[p].money,(unsigned long long)sample.harvestedIncome[p],(unsigned long long)sample.producedUnits[p],(unsigned long long)sample.combatDamage[p],b,u,sample.players[p].defeated);}
            std::fflush(stdout);if(telemetry)std::fflush(telemetry);}
        if(sample.tick==300 || sample.tick==12000){Game saved;saved.world=sample;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.ais=bots;saved.phase=Phase::InGame;
            saved.selectRulesChoice(RulesChoice::RA2);saved.cfgAI=choice.players-1;saved.selectOriginalMap(choice.id);saved.cfgCountry=(int)countries[0];saved.cfgCrates=false;
            for(int p=1;p<choice.players;++p)saved.aiCountry[p-1]=(int)countries[p];saved.matchSetup=saved.currentMatchSetup(seed);
            const auto file=(std::filesystem::path(outputDir)/(std::string(choice.id)+"-tick-"+std::to_string(sample.tick)+"-app12.sav")).string();
            const bool ok=saved.saveGameFile(file.c_str()) && resumed.loadGameFile(file.c_str()) && sample.checksum()==resumed.world.checksum() && bots==resumed.ais;
            exact &= ok;if(ok){++checkpoints;replayTicks=180;}}
        sample.presentationEvents.clear();sample.evaQueue.clear();resumed.world.presentationEvents.clear();resumed.world.evaQueue.clear();
        survivors=0;for(const auto& p:sample.players)survivors+=!p.defeated;
        if(survivors<=1){resolved=sample.tick;break;}
    }
    if(telemetry)std::fclose(telemetry);
    bool economy=true;for(int p=0;p<choice.players;++p)economy &= sample.harvestedIncome[p]>0 && sample.producedUnits[p]>0;
    check(economy && firstDamage>0,"all original starts naturally develop, harvest, produce and reach combat");
    check(exact && checkpoints==2,"real App12 checkpoints retain ordinary AI decisions and exact resumed simulation");
    int victories=0;for(int p=0;p<choice.players;++p)victories+=sample.matchOutcome(p)==MatchOutcome::Victory;
    check(resolved && survivors==1 && victories==1,"the original map reaches ordinary elimination and an actual winner within its fixed window");
    const auto finalPath=(std::filesystem::path(outputDir)/(std::string(choice.id)+"-final-world60.sav")).string();
    FILE* final=std::fopen(finalPath.c_str(),"wb");check(final && sample.saveGame(final),"the unforced final map state is retained");if(final)std::fclose(final);
    {
        Game saved;saved.world=sample;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.ais=bots;saved.phase=Phase::InGame;
        saved.selectRulesChoice(RulesChoice::RA2);saved.cfgAI=choice.players-1;saved.selectOriginalMap(choice.id);saved.cfgCountry=(int)countries[0];saved.cfgCrates=false;
        for(int p=1;p<choice.players;++p)saved.aiCountry[p-1]=(int)countries[p];saved.matchSetup=saved.currentMatchSetup(seed);
        const auto file=(std::filesystem::path(outputDir)/(std::string(choice.id)+"-final-app12.sav")).string();
        const bool written=saved.saveGameFile(file.c_str());
        check(written,"the final application and AI brains remain available for inspection");
        if(written && resolved){const auto outcome=sample.matchOutcome(0);const bool loaded=loadGameFile(file.c_str());
            if(loaded)logic();
            check(loaded && gameOver && victory==(outcome==MatchOutcome::Victory),"the normal game loop settles the naturally completed match with its real local outcome");
            if(loaded){sim={};sim.active=true;sim.pos={-100,-100};const auto checksum=world.checksum();
                shotFile=(std::filesystem::path(outputDir)/(std::string(choice.id)+"-settlement.png")).string();render();
                check(world.checksum()==checksum,"the actual natural settlement frame leaves the simulation unchanged");sim={};}
        }
    }
    const auto result=(std::filesystem::path(outputDir)/(std::string(choice.id)+"-result.json")).string();if(FILE* f=std::fopen(result.c_str(),"w")){
        std::fprintf(f,"{\"map\":\"%s\",\"seed\":%llu,\"budget\":%d,\"tick\":%llu,\"resolved\":%llu,\"survivors\":%d,\"firstDamage\":%llu,\"checkpoints\":%d,\"exact\":%s,\"failures\":%d}\n",choice.id,(unsigned long long)seed,budget,(unsigned long long)sample.tick,(unsigned long long)resolved,survivors,(unsigned long long)firstDamage,checkpoints,exact?"true":"false",failures);std::fclose(f);}
    return failures;
}
