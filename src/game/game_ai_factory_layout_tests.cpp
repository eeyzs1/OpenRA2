#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace {
struct LayoutFixture {
    std::unique_ptr<World> storage=std::make_unique<World>();
    SkirmishAI bot;
    EID factory=INVALID_EID,yard=INVALID_EID;
    UnitType tank;
    Vec2i door;
    bool ready=false;
    LayoutFixture(int choice,Faction side) {
        auto& w=*storage;
        const auto country=side==Faction::Soviet?Country::Russia:side==Faction::Yuri?Country::Yuri:side==Faction::China?Country::China:Country::America;
        const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        ready=w.init(64,64,1717,2,0,{side,Faction::Allies},0,"maps/skirmish-reference.txt",true,true,profile,{country,Country::America});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.cratesEnabled=false;w.crates.clear();
        yard=w.spawnBuilding(0,BldType::ConYard,4,18,true);
        factory=w.spawnBuilding(0,BldType::WarFactory,10,10,true);
        w.spawnBuilding(0,BldType::Barracks,22,4,true);
        w.spawnBuilding(0,BldType::OreRefinery,28,4,true);
        w.spawnBuilding(0,BldType::Radar,35,4,true);
        w.spawnBuilding(0,BldType::BattleLab,42,4,true);
        w.spawnBuilding(0,BldType::ServiceDepot,22,12,true);
        w.spawnBuilding(0,BldType::Grinder,28,12,true);
        const auto power=side==Faction::Allies?BldType::PowerPlant:side==Faction::Yuri?BldType::BioReactor:BldType::TeslaReactor;
        for(int i=0;i<8;++i)w.spawnBuilding(0,power,4+i*5,32,true);
        const auto miner=w.spawnUnit(0,harvesterType(side),40.5f,26.5f);
        w.spawnBuilding(1,BldType::ConYard,54,54,true);
        for(auto& player:w.players){player.money=0;player.defeated=false;}
        w.players[0].isAI=true;
        ready &= w.valid(yard) && w.valid(factory) && w.valid(miner);
        if(w.valid(miner))w.ents[miner].autoHarvest=false;
        if(w.valid(factory)){const auto& def=bldDef(w.ents[factory]);door={10+def.w/2,10+def.h};}
        tank=side==Faction::Allies?UnitType::Grizzly:side==Faction::Soviet?UnitType::Rhino:side==Faction::Yuri?UnitType::LasherTank:UnitType::Type99;
        bot.reset(0);bot.pcfg.useEngineers=bot.pcfg.useSpies=bot.pcfg.useSuperWeapon=false;
        bot.pcfg.expandBase=false;bot.pcfg.rallySize=1000;
    }
    void think(){for(int i=0;i<15;++i)bot.update(*storage);}
    bool readyTank() {
        auto& w=*storage;w.players[0].money=20000;
        if(!w.startUnitProd(0,tank))return false;
        auto& order=w.productionItem(0,true,w.productionSelector(0,true,(int)tank));
        for(int tick=0;tick<4000 && !order.ready;++tick)w.update();
        w.players[0].money=0;
        return order.active && order.ready && order.paid==order.totalCost && w.countUnits(0,tank)==0;
    }
};
}

int GameRegression::aiFactoryLayoutRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,int choice,int faction,const char* text) {
        failures+=!ok;std::printf("AI_FACTORY_LAYOUT %s choice=%d faction=%d %s\n",ok?"PASS":"FAIL",choice,faction,text);std::fflush(stdout);
    };
    for(int choice=0;choice<3;++choice)for(int faction=0;faction<4;++faction) {
        if(choice && (faction==(int)Faction::China || (choice==1 && faction==(int)Faction::Yuri)))continue;
        const auto side=(Faction)faction;
        {
            LayoutFixture f(choice,side);auto& w=*f.storage;
            w.kill(f.yard,false);
            const auto mcv=w.spawnUnit(0,UnitType::MCV,f.door.x+0.5f,f.door.y+1.5f);
            const bool setup=f.ready && w.valid(mcv) && w.canDeployMcv(mcv)
                && w.factoryExitCells(f.factory,unitDef(f.tank)).size()==1;
            check(setup,choice,faction,"human-legal MCV foundation would cover the only real factory outlet");
            if(!setup)continue;
            f.think();
            check(!w.factoryExitCells(f.factory,unitDef(f.tank)).empty(),choice,faction,"AI does not deploy a yard on an existing factory outlet");
            for(int tick=0;tick<600;++tick){w.update();f.bot.update(w);w.presentationEvents.clear();w.evaQueue.clear();}
            check(w.hasBld(0,BldType::ConYard) && w.countUnits(0,UnitType::MCV)==0
                && !w.factoryExitCells(f.factory,unitDef(f.tank)).empty(),choice,faction,"the same MCV physically relocates and rebuilds without sealing production");
        }
        {
            LayoutFixture f(choice,side);auto& w=*f.storage;
            const auto power=side==Faction::Allies?BldType::PowerPlant:side==Faction::Yuri?BldType::BioReactor:BldType::TeslaReactor;
            const int type=w.usesRulesProfile()?w.rulesTypeIndex(0,false,(int)power):-1;
            const auto& def=type>=0?w.nativeTypes[type]->building:bldDef(power);
            for(auto& cell:w.map.cells)cell.overlay=Overlay::Rock1;
            const int bx=f.door.x-1,by=f.door.y;
            for(int y=by;y<by+def.h;++y)for(int x=bx;x<bx+def.w;++x)w.map.at(x,y)=Cell{};
            bool setup=f.ready && w.canPlace(power,bx,by,0);
            w.players[0].money=w.buildingProductionCost(0,power);
            setup &= w.startBldProd(0,power);
            auto& order=w.players[0].bldProd;
            for(int tick=0;tick<4000 && !order.ready;++tick)w.update();
            setup &= order.active && order.ready && order.paid==order.totalCost && w.players[0].money==0;
            const auto paid=order.paid;const int initial=w.countBlds(0,power);
            check(setup,choice,faction,"the only initial legal paid-building site covers the factory door");
            if(!setup)continue;
            f.think();
            check(!w.factoryExitCells(f.factory,unitDef(f.tank)).empty() && order.active && order.ready && order.paid==paid
                && w.countBlds(0,power)==initial,choice,faction,"tight-space fallback retains the paid building rather than sealing the outlet");
            for(int y=20;y<29;++y)for(int x=9;x<19;++x)w.map.at(x,y)=Cell{};
            f.think();
            check(w.countBlds(0,power)==initial+1 && !order.active && w.players[0].money==0
                && !w.factoryExitCells(f.factory,unitDef(f.tank)).empty(),choice,faction,"opening a safe site places that same paid building without another charge");
        }
        for(int scenario=0;scenario<3;++scenario) {
            LayoutFixture f(choice,side);auto& w=*f.storage;
            EID blocker=INVALID_EID;
            if(scenario==0) {
                w.kill(f.yard,false);
                f.yard=w.spawnBuilding(0,BldType::ConYard,f.door.x-1,f.door.y,true);
            } else {
                blocker=w.spawnUnit(0,UnitType::MCV,f.door.x+0.5f,f.door.y+0.5f);
                if(w.valid(blocker))w.ents[blocker].guard=true;
            }
            const bool setup=f.ready && f.readyTank();
            const int selector=w.productionSelector(0,true,(int)f.tank);
            auto& paidOrder=w.productionItem(0,true,selector);const auto paid=paidOrder.paid;
            const int factoryCount=w.countBlds(0,BldType::WarFactory);
            const auto initialCash=w.buildingProductionCost(0,BldType::WarFactory);
            check(setup,choice,faction,scenario==0?"a real fully paid vehicle waits behind a building-sealed outlet":"a real fully paid vehicle waits behind a protected parked unit");
            if(!setup)continue;
            if(scenario==2)w.ents[f.factory].chrono=10000;
            f.think();
            check(paidOrder.active && paidOrder.ready && paidOrder.paid==paid && w.players[0].money==0
                && w.countBlds(0,BldType::WarFactory)==factoryCount,choice,faction,"no budget produces no grant and does not cancel the paid vehicle");
            w.players[0].money=initialCash;f.think();
            const bool backup=w.players[0].bldProd.active && w.players[0].bldProd.typeIdx==w.productionSelector(0,false,(int)BldType::WarFactory);
            check(scenario==0?backup:!backup,choice,faction,scenario==0?"a sealed factory starts one normally quoted backup":"unit congestion and temporary factory suspension do not trigger a backup");
            if(scenario!=0)continue;
            const auto label=std::to_string(choice)+"-"+std::to_string(faction);
            const auto snapshot=[&](const std::string& suffix) {
                world=w;world.map.bldOccRef=&world.bldOcc;ais={f.bot};matchSetup.reset();campaignMission=-1;localPlayer=0;
                selectRulesChoice(choice==0?RulesChoice::Fusion:choice==1?RulesChoice::RA2:RulesChoice::YR);
                phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;clearPlacement();
                const auto a=(std::filesystem::path(outputDir)/(label+suffix+"-app11.sav")).string();
                const auto b=(std::filesystem::path(outputDir)/(label+suffix+"-world56.sav")).string();
                FILE* file=std::fopen(b.c_str(),"wb");const bool saved=file && w.saveGame(file);if(file)std::fclose(file);
                return saved && saveGameFile(a.c_str());
            };
            auto replay=std::make_unique<Game>();
            const auto path=(std::filesystem::path(outputDir)/(label+"-funded-app11.sav")).string();
            bool exact=snapshot("-funded") && replay->loadGameFile(path.c_str()) && replay->ais.size()==1
                && replay->world.checksum()==w.checksum() && replay->ais[0]==f.bot;
            bool retained=true;int produced=-1;
            for(int tick=0;tick<3000 && exact;++tick) {
                w.update();replay->world.update();f.bot.update(w);replay->ais[0].update(replay->world);
                exact &= w.checksum()==replay->world.checksum() && f.bot==replay->ais[0];
                if(w.countUnits(0,f.tank)>0 && produced<0)produced=tick;
                if(produced<0)retained &= paidOrder.active && paidOrder.ready && paidOrder.typeIdx==selector && paidOrder.paid==paid;
                w.presentationEvents.clear();w.evaQueue.clear();replay->world.presentationEvents.clear();replay->world.evaQueue.clear();
            }
            check(exact && retained,choice,faction,"actual App11 resumes paid backup construction and the original order exactly for 3000 ticks");
            check(produced>=0 && w.countUnits(0,f.tank)==1 && w.countBlds(0,BldType::WarFactory)==factoryCount+1
                && w.players[0].money==0 && w.valid(f.yard) && !w.ents[f.yard].selling
                && w.factoryExitCells(f.factory,unitDef(f.tank)).empty(),choice,faction,"one paid backup releases the original vehicle without deleting the blocking yard or granting money");
            check(snapshot("-final"),choice,faction,"the repaired World56 and App11 are saved through the actual application path");
        }
    }
    return failures;
}
