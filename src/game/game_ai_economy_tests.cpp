#include "game/game.h"
#include <cstdio>

int Game::aiEconomyRegression() {
    int failures=0;
    for(int faction=0;faction<4;++faction)for(int scenario=0;scenario<7;++scenario) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        w.cratesEnabled=false;
        w.spawnBuilding(0,BldType::ConYard,4,4,true);
        EID factory=w.spawnBuilding(0,BldType::WarFactory,10,4,true);
        w.spawnBuilding(0,BldType::Barracks,16,4,true);w.spawnBuilding(0,BldType::Radar,20,4,true);
        w.spawnBuilding(0,BldType::BattleLab,24,4,true);w.spawnBuilding(0,BldType::OreRefinery,4,12,true);
        for(int index=0;index<6;++index)w.spawnBuilding(0,BldType::PowerPlant,12+index*3,12,true);
        const auto miner=harvesterType((Faction)faction);
        // Use a parked generic collector: a deployed SlaveMiner can create
        // surviving slaves, which would correctly mean this is not zero income.
        EID last=w.spawnUnit(0,UnitType::Harvester,10.5f,17.5f);w.ents[last].autoHarvest=false;
        const auto tank=faction==0?UnitType::Grizzly:faction==1?UnitType::Rhino:faction==2?UnitType::Type99:UnitType::LasherTank;
        const auto sw=faction==0?BldType::WeatherDevice:faction==3?BldType::PsychicDominator:BldType::NukeSilo;
        auto& p=w.players[0];const int category=unitDef(miner).prodCat();
        p.money=scenario==4?100:w.unitProductionCost(0,miner)+400;
        bool setup=w.startUnitProd(0,scenario==1?miner:tank);
        if(scenario==1)w.update();
        w.holdUnitProd(0,scenario==1?miner:tank,true);
        if(scenario!=1)setup &= w.startUnitProd(0,miner);
        const auto swReason=w.productionStartRejection(0,false,(int)sw);
        setup &= w.startBldProd(0,sw);
        // Pay through the real production system, then lose the final collector.
        // The unfinished superweapon holds the only funds available to recover.
        for(int tick=0;tick<2000 && p.money>0;++tick)w.update();
        if(!setup || p.money>=10 || !p.defProd.active || p.defProd.typeIdx!=(int)sw || p.defProd.ready)
            std::printf("AI_ECONOMY_SETUP faction=%d scenario=%d initial=%d reason=%d money=%lld sw=%d active=%d type=%d ready=%d unit_active=%d unit_type=%d held=%d\n",faction,scenario,setup,(int)swReason,(long long)p.money,(int)sw,p.defProd.active,p.defProd.typeIdx,p.defProd.ready,p.unitProd[category].active,p.unitProd[category].typeIdx,p.unitProd[category].held);
        setup &= p.money<10 && p.defProd.active && p.defProd.typeIdx==(int)sw && !p.defProd.ready;
        if(scenario!=2) {
            // Refineries may already have replenished Yuri slaves while the
            // investment was being paid. Remove every collector for this loss.
            for(size_t id=0;id<w.ents.size();++id) {
                const auto& unit=w.ents[id];
                if(unit.alive && !unit.isBuilding && unit.player==0 && unitDef(unit.utype).canHarvet())w.kill((EID)id,false);
            }
        }
        if(scenario==3)w.skirmishMode=SkirmishMode::Megawealth;
        if(scenario==5)w.kill(factory,false);
        if(scenario==6){EID slave=w.spawnUnit(0,UnitType::Slave,10.5f,17.5f);w.ents[slave].autoHarvest=false;}
        for(int y=18;y<24;++y)for(int x=7;x<15;++x){auto& cell=w.map.at(x,y);cell.terrain=Terrain::Ore;cell.ore=cell.oreMax=300;}
        const auto original=p.unitProd[category];const auto investment=p.defProd;
        const Credits capital=p.money+p.defProd.paid+original.paid;
        SkirmishAI bot;bot.reset(0);
        for(int frame=0;frame<15;++frame)bot.update(w);
        const bool control=scenario==2 || scenario==3 || scenario==5 || scenario==6;
        const bool freeWorkers=faction==3 && !control;
        bool priority=(control || freeWorkers)?(p.defProd.active && p.defProd.paid==investment.paid && p.unitProd[category].typeIdx==original.typeIdx)
            :(p.unitProd[category].active && p.unitProd[category].typeIdx==(int)miner && !p.unitProd[category].held
                && p.unitQueue[category].empty() && !p.defProd.active && !p.bldProd.active);
        if(scenario==1)priority &= p.unitProd[category].progress==original.progress && p.unitProd[category].paid==original.paid;
        Credits remaining=p.money+p.bldProd.paid+p.defProd.paid;
        for(const auto& order:p.unitProd)remaining+=order.paid;
        bool conserved=remaining==capital;
        World replay;FILE* file=tmpfile();bool equal=file && w.saveGame(file);
        if(equal){rewind(file);equal=replay.loadGame(file);}if(file)fclose(file);
        SkirmishAI replayBot=bot;
        if(!control)for(int tick=0;tick<(scenario==4 && !freeWorkers?1200:6000) && equal && !w.harvestedIncome[0];++tick) {
            w.update();replay.update();bot.update(w);replayBot.update(replay);
            equal &= w.checksum()==replay.checksum() && bot==replayBot;
            if(scenario==4 && !freeWorkers)conserved &= p.money+p.unitProd[category].paid==capital;
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        const bool recovered=control || (freeWorkers?(w.harvestedIncome[0]>0 && w.countUnits(0,UnitType::Slave)>0):scenario==4?(w.harvestedIncome[0]==0 && w.countUnits(0,miner)==0)
            :(w.harvestedIncome[0]>0 && w.countUnits(0,miner)>0));
        const bool ok=setup && priority && conserved && equal && recovered;
        failures+=!ok;
        std::printf("AI_ECONOMY %s faction=%d scenario=%d setup=%d priority=%d conserved=%d replay=%d recovered=%d income=%llu capital=%lld money=%lld\n",ok?"PASS":"FAIL",faction,scenario,setup,priority,conserved,equal,recovered,(unsigned long long)w.harvestedIncome[0],(long long)capital,(long long)p.money);
    }
    return failures;
}
