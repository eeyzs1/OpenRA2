#include "game_regression.h"
#include <cstdio>

int GameRegression::refineryRecoveryRegression() {
    int failures=0;
    for(int scenario=0;scenario<5;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Yuri,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        for(auto& player:w.players) { player.money=0;player.defeated=false; }
        w.cratesEnabled=false;
        EID near=w.spawnBuilding(0,BldType::OreRefinery,20,20,true),alternative=w.spawnBuilding(0,BldType::OreRefinery,35,20,true);
        // Respect loaded footprints (the current rules override the defaults).
        for(int y=20;y<=22;++y) for(int x=22;x<=24;++x) if(!w.bldBlocked(x,y)) w.map.at(x,y).overlay=Overlay::Rock1;
        std::vector<EID> miners;
        for(int index=0;index<(scenario==1?6:1);++index) {
            auto type=scenario==2?UnitType::ChronoMiner:scenario==1 && index<4?UnitType::Slave:UnitType::SlaveMiner;
            EID id=w.spawnUnit(0,type,18.5f,24.5f+index);w.ents[id].oreLoad=5;miners.push_back(id);
        }
        w.orderReturnToRefinery(miners,near);
        bool recovered=true;for(EID id:miners) recovered &= w.ents[id].dockRefinery==alternative
            && w.ents[id].occCell==(int)w.ents[id].y*w.map.w+(int)w.ents[id].x;
        if(!recovered) {
            std::printf("REFINERY_DIAGNOSTIC near=%d alternative=%d size=%dx%d\n",near,alternative,bldDef(BldType::OreRefinery).w,bldDef(BldType::OreRefinery).h);
            for(EID id:miners) std::printf("REFINERY_ROUTE unit=%d dock=%d path=%zu\n",id,w.ents[id].dockRefinery,w.ents[id].path.size());
            for(int y=20;y<=22;++y) std::printf("REFINERY_DOCK y=%d occupancy=%d\n",y,w.bldAt(24,y));
        }
        FILE* file=tmpfile();World replay;bool equal=file && w.saveGame(file);
        if(equal) { rewind(file);equal=replay.loadGame(file); }
        if(file) fclose(file);
        bool legal=true,budget=true,interrupted=false;
        for(int tick=0;tick<2000 && equal;++tick) {
            if(scenario>=3 && !interrupted && w.ents[miners[0]].state==UState::HarvestUnload) {
                if(scenario==3) { w.kill(alternative,false);replay.kill(alternative,false); }
                else { w.ents[alternative].player=1;replay.ents[alternative].player=1; }
                w.spawnBuilding(0,BldType::OreRefinery,45,20,true);replay.spawnBuilding(0,BldType::OreRefinery,45,20,true);
                interrupted=true;
            }
            const auto oldIncome=w.harvestedIncome[0];bool mayDeposit=true;
            if(scenario>=3) {
                const auto& miner=w.ents[miners[0]];
                mayDeposit=miner.state==UState::HarvestUnload && w.valid(miner.dockRefinery)
                    && w.ents[miner.dockRefinery].player==0;
            }
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            if(scenario>=3 && w.harvestedIncome[0]!=oldIncome) legal &= mayDeposit;
            for(EID id:miners) legal &= w.valid(id) && !w.bldBlocked((int)w.ents[id].x,(int)w.ents[id].y);
        }
        if(scenario>=3) recovered &= interrupted;
        const bool ok=recovered && equal && legal && budget && w.harvestedIncome[0]==miners.size()*5*oreUnitValue(false);
        failures+=!ok;
        std::printf("REFINERY_RECOVERY %s scenario=%d recovered=%d replay=%d legal=%d budget=%d income=%llu\n",ok?"PASS":"FAIL",scenario,recovered,equal,legal,budget,(unsigned long long)w.harvestedIncome[0]);
    }
    for(int scenario=0;scenario<4;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::China,Faction::Soviet},0,"",true);
        for(size_t i=0;i<w.ents.size();++i)if(w.ents[i].alive)w.kill((EID)i,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& player:w.players){player.money=0;player.defeated=false;}
        w.cratesEnabled=false;
        EID near=w.spawnBuilding(0,BldType::OreRefinery,20,20,true);
        EID alternative=w.spawnBuilding(0,BldType::OreRefinery,35,20,true);
        std::vector<EID> miners,nearGuards,farGuards;
        for(int index=0;index<(scenario==3?6:1);++index) {
            EID id=w.spawnUnit(0,UnitType::Harvester,17.5f+index,27.5f);
            w.ents[id].oreLoad=5;miners.push_back(id);
        }
        w.orderReturnToRefinery(miners,near);
        bool selected=true;for(EID id:miners)selected &= w.ents[id].dockRefinery==near;
        auto block=[&](int bx,std::vector<EID>& ids) {
            for(int y=20;y<=22;++y)for(int x=bx+2;x<=bx+4;++x)if(!w.bldBlocked(x,y)) {
                EID id=w.spawnUnit(0,UnitType::Type99,x+0.5f,y+0.5f);
                w.ents[id].guard=true;w.ents[id].tpSick=6000;ids.push_back(id);
            }
        };
        block(20,nearGuards);if(scenario==1)block(35,farGuards);
        World replay;FILE* file=tmpfile();bool equal=file && w.saveGame(file);
        if(equal){rewind(file);equal=replay.loadGame(file);}if(file)fclose(file);
        bool budget=true,legal=true,rerouted=false,interrupted=false,keptCargo=true;
        for(int tick=0;tick<3600 && equal;++tick) {
            if(scenario==1 && tick==600)for(EID id:farGuards){w.kill(id,false);replay.kill(id,false);}
            if(scenario==2 && !interrupted && w.ents[miners[0]].dockRefinery==alternative) {
                interrupted=true;w.kill(alternative,false);replay.kill(alternative,false);
                w.spawnBuilding(0,BldType::OreRefinery,45,20,true);replay.spawnBuilding(0,BldType::OreRefinery,45,20,true);
            }
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            for(EID id:miners) {
                const auto& miner=w.ents[id];
                legal &= w.valid(id) && !w.bldBlocked((int)miner.x,(int)miner.y)
                    && miner.occCell==(int)miner.y*w.map.w+(int)miner.x;
                rerouted |= miner.dockRefinery!=INVALID_EID && miner.dockRefinery!=near;
                if(scenario==1 && tick<600)keptCargo &= miner.oreLoad==5 && w.harvestedIncome[0]==0;
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        const bool ok=selected && equal && legal && budget && rerouted && keptCargo && (scenario!=2 || interrupted)
            && w.harvestedIncome[0]==miners.size()*5*oreUnitValue(false);
        failures+=!ok;
        std::printf("REFINERY_TRAFFIC %s scenario=%d initial=%d replay=%d legal=%d budget=%d rerouted=%d cargo=%d income=%llu\n",ok?"PASS":"FAIL",scenario,selected,equal,legal,budget,rerouted,keptCargo,(unsigned long long)w.harvestedIncome[0]);
    }
    return failures;
}
