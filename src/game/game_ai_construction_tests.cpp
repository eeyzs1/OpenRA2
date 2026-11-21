#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

int GameRegression::aiConstructionRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const bool hidden=IsWindowHidden();failures+=!hidden;
    std::printf("AI_CONSTRUCTION %s CLI regression window is hidden\n",hidden?"PASS":"FAIL");
    for(int choice=0;choice<3;++choice)for(int faction=0;faction<4;++faction) {
        if(choice && (faction==(int)Faction::China || (choice==1 && faction==(int)Faction::Yuri)))continue;
        for(int scenario=0;scenario<7;++scenario) {
            auto storage=std::make_unique<World>();auto& w=*storage;
            const auto side=(Faction)faction;
            const auto country=side==Faction::Soviet?Country::Russia:side==Faction::Yuri?Country::Yuri:side==Faction::China?Country::China:Country::America;
            const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
            bool setup=w.init(64,64,1717,2,0,{side,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,profile,{country,Country::Russia});
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
            for(auto& cell:w.map.cells)cell=Cell{};
            w.cratesEnabled=false;w.crates.clear();
            for(auto& p:w.players){p.money=0;p.defeated=false;}
            const auto yard=w.spawnBuilding(0,BldType::ConYard,4,4,true);
            const auto factory=w.spawnBuilding(0,BldType::WarFactory,10,10,true);
            w.spawnBuilding(1,BldType::ConYard,50,50,true);
            for(const auto item:{std::pair{BldType::Barracks,Vec2i{20,4}},
                {BldType::OreRefinery,{26,4}},{BldType::Radar,{32,4}},{BldType::BattleLab,{38,4}},
                {BldType::ServiceDepot,{20,12}},{BldType::Grinder,{27,12}}})
                w.spawnBuilding(0,item.first,item.second.x,item.second.y,true);
            const auto power=side==Faction::Soviet?BldType::TeslaReactor:side==Faction::Yuri?BldType::BioReactor:BldType::PowerPlant;
            for(int index=0;index<8;++index)w.spawnBuilding(0,power,4+index*5,25,true);
            const auto miner=w.spawnUnit(0,harvesterType(side),34.5f,20.5f);
            setup &= w.valid(yard) && w.valid(factory) && w.valid(miner);
            if(!setup){std::printf("AI_CONSTRUCTION SETUP_FAIL choice=%d faction=%d scenario=%d\n",choice,faction,scenario);return 1;}
            w.ents[miner].autoHarvest=false;w.players[0].money=20000;
            setup &= w.unitPrereqMet(0,unitDef(UnitType::MCV));
            EID mcv=INVALID_EID;
            if(scenario==0 || scenario==1 || scenario>=4) {
                mcv=w.spawnUnit(0,UnitType::MCV,9.5f,12.5f);
                setup &= w.valid(mcv) && !w.canDeployMcv(mcv);
                if(scenario==4)w.ents[mcv].tpSick=900;
                if(scenario==6)w.ents[mcv].permaControlled=true;
            }
            if(scenario!=1)w.kill(yard,false);
            if(scenario==2)setup &= w.startUnitProd(0,UnitType::MCV);
            if(scenario==5) {
                const auto carrier=w.spawnUnit(0,UnitType::AmphTransport,8.5f,12.5f);
                setup &= w.valid(carrier) && w.orderBoard({mcv},carrier).accepted==1;
                w.update();setup &= w.valid(carrier) && World::cargoUnitCount(w.ents[carrier].cargo,UnitType::MCV)==1;
            }
            SkirmishAI bot;bot.reset(0);bot.pcfg.expandBase=scenario==1;
            const int selector=w.productionSelector(0,true,(int)UnitType::MCV);
            const auto planned=[&]() {
                int count=w.countUnits(0,UnitType::MCV);
                for(const auto& unit:w.ents)if(unit.alive && unit.player==0)count+=World::cargoUnitCount(unit.cargo,UnitType::MCV);
                for(int cat=0;cat<PROD_CAT_N;++cat)count+=(w.players[0].unitProd[cat].active && w.players[0].unitProd[cat].typeIdx==selector)
                    +(int)std::count(w.players[0].unitQueue[cat].begin(),w.players[0].unitQueue[cat].end(),selector);
                return count;
            };
            for(int frame=0;frame<15;++frame){w.update();bot.update(w);}
            const bool bounded=planned()==(scenario==6?2:1);
            const bool ok=setup && bounded;failures+=!ok;
            std::printf("AI_CONSTRUCTION %s choice=%d faction=%d scenario=%d setup=%d planned=%d money=%lld\n",
                ok?"PASS":"FAIL",choice,faction,scenario,setup,planned(),(long long)w.players[0].money);
            std::fflush(stdout);
        }
    }
    for(int choice=0;choice<3;++choice)for(int scenario=0;scenario<3;++scenario) {
        const auto label=std::to_string(choice)+"-"+std::to_string(scenario);
        const auto check=[&](bool ok,const char* text) {
            failures+=!ok;std::printf("AI_CONSTRUCTION %s case=%s %s\n",ok?"PASS":"FAIL",label.c_str(),text);std::fflush(stdout);
        };
        World w;
        const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        bool setup=w.init(64,64,1717,2,0,{Faction::Soviet,Faction::Allies},0,"maps/skirmish-reference.txt",true,true,profile,{Country::Russia,Country::America});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.cratesEnabled=false;w.crates.clear();for(auto& p:w.players){p.money=0;p.defeated=false;}
        w.players[0].isAI=true;
        const auto factory=w.spawnBuilding(0,BldType::WarFactory,10,10,true);
        w.spawnBuilding(0,BldType::ServiceDepot,20,10,true);w.spawnBuilding(0,BldType::OreRefinery,4,20,true);
        for(int index=0;index<6;++index)w.spawnBuilding(0,BldType::TeslaReactor,12+index*5,30,true);
        w.spawnBuilding(1,BldType::ConYard,50,50,true);
        const auto miner=w.spawnUnit(0,UnitType::WarMiner,34.5f,20.5f);
        const auto mcv=w.spawnUnit(0,UnitType::MCV,9.5f,12.5f);
        setup &= w.valid(factory) && w.valid(miner) && w.valid(mcv) && !w.canDeployMcv(mcv);
        if(!setup){check(false,"recovery fixture has real blocked MCV and paid production facilities");continue;}
        w.ents[miner].autoHarvest=false;w.players[0].money=20000;
        if(scenario==2) {
            for(auto& cell:w.map.cells)cell.overlay=Overlay::Rock1;
            w.map.at(9,12)=Cell{};
        }
        SkirmishAI bot;bot.reset(0);
        const auto initial=w.checksum();
        bool query=w.canDeployMcvAt(mcv,9,12)==w.canDeployMcv(mcv)
            && !w.canDeployMcvAt(mcv,-1,12) && !w.canDeployMcvAt(mcv,w.map.w,12)
            && !w.canDeployMcvAt(factory,9,12) && w.checksum()==initial;
        check(query,"prospective foundation query is read only, bounded and shares current deployment checks");
        for(int frame=0;frame<90;++frame){w.update();bot.update(w);}
        const bool moving=w.valid(mcv) && w.ents[mcv].state==UState::Moving && !w.ents[mcv].path.empty();
        const bool waiting=w.valid(mcv) && w.ents[mcv].state==UState::Idle && w.ents[mcv].x==9.5f && w.ents[mcv].y==12.5f;
        check(scenario==2?waiting:moving,"blocked MCV receives a reachable relocation, while fully sealed terrain waits");
        if(scenario==1 && moving) {
            const auto goal=w.ents[mcv].path.back();
            const auto blocker=w.spawnUnit(0,UnitType::Conscript,goal.x-0.5f,goal.y-0.5f);
            if(w.valid(blocker))w.ents[blocker].guard=true;
            check(w.valid(blocker) && !w.canDeployMcvAt(mcv,goal.x,goal.y),"a real arriving-footprint blocker invalidates the first planned site");
        }
        const auto snapshot=[&](const std::string& suffix) {
            world=w;world.map.bldOccRef=&world.bldOcc;ais={bot};matchSetup.reset();campaignMission=-1;localPlayer=0;
            selectRulesChoice(choice==0?RulesChoice::Fusion:choice==1?RulesChoice::RA2:RulesChoice::YR);
            phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;clearPlacement();
            const auto appPath=(std::filesystem::path(outputDir)/(label+suffix+"-app11.sav")).string();
            const auto worldPath=(std::filesystem::path(outputDir)/(label+suffix+"-world56.sav")).string();
            FILE* file=std::fopen(worldPath.c_str(),"wb");const bool savedWorld=file && w.saveGame(file);if(file)std::fclose(file);
            return savedWorld && saveGameFile(appPath.c_str());
        };
        auto replay=std::make_unique<Game>();
        const auto appPath=(std::filesystem::path(outputDir)/(label+"-moving-app11.sav")).string();
        bool exact=snapshot("-moving") && replay->loadGameFile(appPath.c_str()) && replay->ais.size()==1
            && replay->world.checksum()==w.checksum() && replay->ais[0]==bot;
        check(exact,"actual App11 preserves the pending recovery route or sealed wait and full AI state");
        bool stayed=true;
        for(int frame=0;frame<600 && exact;++frame) {
            if(scenario==2 && w.tick==270) {
                stayed &= w.valid(mcv) && w.ents[mcv].x==9.5f && w.ents[mcv].y==12.5f && !w.hasBld(0,BldType::ConYard)
                    && w.countUnits(0,UnitType::MCV)==1 && w.unitQueuedCount(0,1)==0;
                for(int y=9;y<=17;++y)for(int x=6;x<=17;++x) {
                    w.map.at(x,y)=Cell{};replay->world.map.at(x,y)=Cell{};
                }
                std::vector<Vec2i> route;
                check(w.canDeployMcvAt(mcv,7,12) && w.map.findPath(9,12,7,12,route),
                    "reopened terrain actually contains a reachable complete source foundation beside the factory");
            }
            w.update();replay->world.update();bot.update(w);replay->ais[0].update(replay->world);
            exact &= w.checksum()==replay->world.checksum() && bot==replay->ais[0];
            w.presentationEvents.clear();w.evaQueue.clear();replay->world.presentationEvents.clear();replay->world.evaQueue.clear();
        }
        check(exact,"six hundred real World/AI ticks resume exactly through movement, obstruction and reopening");
        check(stayed && w.hasBld(0,BldType::ConYard) && w.countUnits(0,UnitType::MCV)==0 && w.players[0].money>=0,
            "the existing vehicle physically rebuilds one yard without duplicates or a grant");
        check(snapshot("-final"),"final recovered World56 and App11 are saved through the real application path");
    }
    return failures+aiFactoryLayoutRegression((std::filesystem::path(outputDir)/"factory-layout").string().c_str());
}
