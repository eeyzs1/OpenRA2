#include "game_regression.h"
#include <array>
#include <cstdio>

int GameRegression::aiFactoryExitRegression() {
    int failures=0;
    const std::array<UnitType,4> tanks{UnitType::Grizzly,UnitType::Rhino,UnitType::Type99,UnitType::LasherTank};
    const std::array<UnitType,4> infantry{UnitType::GI,UnitType::Conscript,UnitType::PLA,UnitType::Initiate};
    const std::array<UnitType,4> ships{UnitType::Destroyer,UnitType::Typhoon,UnitType::Aegis,UnitType::Boomer};
    for(int faction=0;faction<4;++faction)for(int side=0;side<4;++side)for(int occupiedSouth=0;occupiedSouth<2;++occupiedSouth) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass)for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.cratesEnabled=false;for(auto& p:w.players){p.money=10000;p.defeated=false;}
        w.spawnBuilding(0,BldType::ConYard,20,18,true);w.spawnBuilding(0,BldType::WarFactory,10,10,true);
        w.spawnBuilding(0,BldType::Radar,10,15,true);w.spawnBuilding(0,BldType::BattleLab,15,15,true);
        for(int power=0;power<5;++power)w.spawnBuilding(0,BldType::PowerPlant,4+power*4,4,true);
        const auto& def=bldDef(BldType::NavalYard);
        for(int dy=0;dy<def.h;++dy)for(int dx=0;dx<def.w;++dx)w.map.at(24+dx,24+dy).terrain=Terrain::Water;
        const Vec2i ports[]={{24+def.w/2,24+def.h},{24+def.w,24+def.h/2},{24+def.w/2,23},{23,24+def.h/2}};
        const Vec2i dirs[]={{0,1},{1,0},{0,-1},{-1,0}};
        for(int i=0;i<4;++i)w.map.at(ports[side].x+dirs[side].x*i,ports[side].y+dirs[side].y*i).terrain=Terrain::Water;
        EID guard=INVALID_EID;
        if(occupiedSouth) {
            for(int i=0;i<4;++i)w.map.at(ports[0].x,ports[0].y+i).terrain=Terrain::Water;
            guard=w.spawnUnit(0,UnitType::AmphTransport,ports[0].x+0.5f,ports[0].y+0.5f);w.ents[guard].guard=true;
        }
        const bool legalSite=w.canPlace(BldType::NavalYard,24,24,0);
        EID yard=w.spawnBuilding(0,BldType::NavalYard,24,24,true);w.orderRally(yard,0,0,true);
        const bool started=w.startUnitProd(0,ships[faction]);const int quote=w.unitProductionCost(0,ships[faction]);
        for(int frame=0;frame<1200 && !w.producedUnits[0];++frame)w.update();
        bool protectedGuard=!occupiedSouth || (w.ents[guard].guard && w.ents[guard].x==ports[0].x+0.5f && w.ents[guard].y==ports[0].y+0.5f);
        bool retained=true;
        if(occupiedSouth && side==0) {
            retained=w.producedUnits[0]==0 && w.players[0].unitProd[3].ready && w.players[0].unitProd[3].paid==quote;
            w.orderMove({guard},ports[0].x+0.5f,ports[0].y+3.5f,false);
            for(int frame=0;frame<300 && !w.producedUnits[0];++frame)w.update();
        }
        bool legalShip=false;for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.player==0 && e.utype==ships[faction])
            legalShip=(int)e.x==ports[side].x && (int)e.y==ports[side].y && !w.bldBlocked((int)e.x,(int)e.y);
        bool ok=legalSite && started && w.producedUnits[0]==1 && legalShip && protectedGuard && retained && w.players[0].money==10000-quote;
        failures+=!ok;std::printf("NAVAL_SHORE_EXIT %s faction=%d side=%d south_guard=%d legal_site=%d started=%d produced=%llu legal_ship=%d protected=%d retained=%d ready=%d paid=%d\n",ok?"PASS":"FAIL",faction,side,occupiedSouth,legalSite,started,(unsigned long long)w.producedUnits[0],legalShip,protectedGuard,retained,w.players[0].unitProd[3].ready,w.players[0].unitProd[3].paid);
    }
    for(int faction=0;faction<4;++faction)for(int kind=0;kind<3;++kind)for(int scenario=0;scenario<7;++scenario) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass)for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.overlay=Overlay::Rock1;}
        w.cratesEnabled=false;for(auto& player:w.players){player.money=20000;player.defeated=false;}
        w.players[0].isAI=true;w.spawnUnit(1,UnitType::MCV,60.5f,60.5f);
        auto building=[&](BldType type,int x,int y) {
            const auto& def=bldDef(type);
            for(int dy=0;dy<def.h;++dy)for(int dx=0;dx<def.w;++dx)w.map.at(x+dx,y+dy)=Cell{};
            return w.spawnBuilding(0,type,x,y,true);
        };
        building(BldType::ConYard,4,4);building(BldType::Radar,10,4);building(BldType::BattleLab,15,4);
        // Keep a functioning economic baseline: this fixture exercises exit
        // clearing, not emergency liquidation after losing the last collector.
        building(BldType::OreRefinery,4,32);w.map.at(3,40)=Cell{};
        const EID collector=w.spawnUnit(0,UnitType::Harvester,3.5f,40.5f);w.ents[collector].autoHarvest=false;
        for(int power=0;power<6;++power)building(BldType::PowerPlant,4+power*4,12);
        if(kind!=1)building(BldType::Barracks,8,18);
        const auto factoryType=kind==0?BldType::WarFactory:kind==1?BldType::Barracks:BldType::NavalYard;
        building(factoryType,24,24);const auto& def=bldDef(factoryType);
        const int x=24+def.w/2,y=24+def.h;
        auto floor=[&](int tx,int ty){auto& cell=w.map.at(tx,ty);cell=Cell{};cell.terrain=kind==2?Terrain::Water:Terrain::Clear;};
        for(int dx=scenario==2?0:-6;dx<=6;++dx)floor(x+dx,y);
        if(scenario==0)for(int dy=0;dy<=3;++dy)for(int dx=-5;dx<=5;++dx)floor(x+dx,y+dy);
        if(kind==2) {
            for(int dy=0;dy<def.h;++dy)for(int dx=0;dx<def.w;++dx)w.map.at(24+dx,24+dy).terrain=Terrain::Water;
            // Constrain this fixture to one real launch port. Other water
            // cells below the hull are maneuvering room, blocked by walls at
            // the remaining hull edges rather than treated as hidden exits.
            for(int dx=0;dx<def.w;++dx)if(24+dx!=x)w.spawnBuilding(-1,BldType::Wall,24+dx,y,true);
            if(scenario==2) {
                for(int dx=1;dx<=6;++dx)w.map.at(x+dx,y)=Cell{};
                for(int dy=0;dy<=6;++dy)floor(x,y+dy);
            } else for(int dy=1;dy<=2;++dy)for(int dx=-6;dx<=6;++dx)floor(x+dx,y+dy);
        }
        const auto type=kind==0?tanks[faction]:kind==1?infantry[faction]:ships[faction];
        const bool protectedCase=scenario>=3;
        const auto blockingType=protectedCase?(kind==2?UnitType::AmphTransport:UnitType::MCV):type;
        const int owner=scenario==5?1:scenario==6?-1:0;
        EID blocker=w.spawnUnit(owner,blockingType,x+0.5f,y+0.5f);
        if(scenario==3)w.ents[blocker].guard=true;
        if(scenario==4)w.ents[blocker].tpSick=10000;
        std::vector<EID> protectedUnits;
        if(scenario==1) {
            EID door=w.spawnUnit(0,kind==2?UnitType::AmphTransport:UnitType::MCV,x-0.5f,y+(kind==2?1.5f:0.5f));
            w.ents[door].tpSick=10000;protectedUnits.push_back(door);
        }
        if(scenario==2)for(int dx=1;dx<=3;++dx)w.spawnUnit(0,type,kind==2?x+0.5f:x+dx+0.5f,kind==2?y+dx+0.5f:y+0.5f);
        if(scenario==0) {
            EID reserved=w.spawnUnit(0,kind==2?UnitType::AmphTransport:UnitType::MCV,x+5.5f,y+3.5f);
            w.ents[reserved].path={{x-2,y+2}};w.ents[reserved].state=UState::Moving;w.ents[reserved].tpSick=10000;
            protectedUnits.push_back(reserved);
        }
        bool prepared=w.startUnitProd(0,type);
        const int cat=unitDef(type).prodCat();
        for(int tick=0;tick<3000 && !w.players[0].unitProd[cat].ready;++tick)w.update();
        auto& order=w.players[0].unitProd[cat];
        prepared &= order.active && order.ready && order.paid==order.totalCost && w.producedUnits[0]==0;
        const auto quote=order.totalCost;w.players[0].money=0;
        SkirmishAI bot;bot.reset(0);bot.rallyPoint={x,y};bot.pcfg.rallySize=1000;
        bot.pcfg.useEngineers=false;bot.pcfg.useSpies=false;bot.pcfg.useSuperWeapon=false;
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        bool legal=true,smooth=true,equal=true,budget=true,protectedStill=true,paid=true,saved=false;
        int completed=-1;
        for(int tick=0;tick<600;++tick) {
            std::vector<Vec2f> before;before.reserve(w.ents.size());std::vector<bool> wasAlive;
            for(const auto& unit:w.ents){before.push_back({unit.x,unit.y});wasAlive.push_back(unit.alive);}
            w.update();bot.update(w);replay.update();replayBot.update(replay);
            equal &= w.checksum()==replay.checksum() && bot==replayBot;budget &= w.repathsThisTick<=8;
            for(size_t id=0;id<w.ents.size();++id) {
                const auto& unit=w.ents[id];if(!unit.alive || unit.isBuilding || unit.occCell<0)continue;
                if((EID)id==blocker && protectedCase)continue;
                if(unit.player!=0)continue;
                legal &= w.passableFor((int)unit.x,(int)unit.y,unitDef(unit.utype).pathDomain()) && !w.bldBlocked((int)unit.x,(int)unit.y)
                    && unit.occCell==(int)unit.y*w.map.w+(int)unit.x;
                if(id<before.size() && wasAlive[id]) {
                    const bool continuous=distf(before[id].x,before[id].y,unit.x,unit.y)<=1.f/std::max(1,unitDef(unit.utype).speed)+0.0001f;
                    if(smooth && !continuous)std::printf("AI_EXIT_SMOOTH faction=%d kind=%d scenario=%d frame=%d unit=%zu pos=%.2f,%.2f->%.2f,%.2f state=%d completed=%d\n",faction,kind,scenario,tick,id,before[id].x,before[id].y,unit.x,unit.y,(int)unit.state,completed);
                    smooth &= continuous;
                }
                if(unitDef(unit.utype).isInfantry())legal &= w.countInfantryAtCell((int)unit.x,(int)unit.y,(EID)id)<3;
                else legal &= !w.groundUnitBlocksCell((int)unit.x,(int)unit.y,(EID)id);
            }
            for(EID id:protectedUnits)protectedStill &= w.ents[id].x==before[id].x && w.ents[id].y==before[id].y;
            if(protectedCase)protectedStill &= w.ents[blocker].x==x+0.5f && w.ents[blocker].y==y+0.5f
                && (scenario!=3 || w.ents[blocker].guard);
            paid &= w.players[0].money==0 && (!order.active || order.typeIdx!=(int)type || order.ready || order.paid==0);
            if(order.active && order.ready && order.typeIdx==(int)type)paid &= order.paid==quote;
            if(tick==40) {
                FILE* file=tmpfile();equal &= file && w.saveGame(file);
                if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;
            }
            if(w.producedUnits[0]>0 && completed<0)completed=tick;
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        const bool outcome=protectedCase?completed<0 && order.active && order.ready && order.paid==quote:completed>=0 && w.producedUnits[0]==1;
        const bool ok=prepared && outcome && legal && smooth && equal && budget && protectedStill && paid && saved;
        failures+=!ok;
        std::printf("AI_FACTORY_EXIT %s faction=%d kind=%d scenario=%d prepared=%d completed=%d legal=%d smooth=%d replay=%d budget=%d protected=%d paid=%d saved=%d produced=%llu\n",ok?"PASS":"FAIL",faction,kind,scenario,prepared,completed,legal,smooth,equal,budget,protectedStill,paid,saved,(unsigned long long)w.producedUnits[0]);
    }
    return failures;
}
