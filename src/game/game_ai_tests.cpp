#include "game_regression.h"
#include <cstdio>

int GameRegression::aiAttackRegression() {
    int failures=0;
    for(int scenario=0;scenario<4;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.terrain=Terrain::Clear; }
        w.cratesEnabled=false;
        for(auto& player:w.players) { player.defeated=false;player.money=0; }
        SkirmishAI bot;bot.reset(0);bot.rallyPoint={20,20};bot.attackTimer=7;
        bot.difficulty=scenario==2?AIDiff::Hard:AIDiff::Normal;
        bot.pcfg.rallySize=1;bot.pcfg.useEngineers=false;bot.pcfg.useSpies=false;bot.pcfg.useSuperWeapon=false;
        const int count=scenario==2?12:scenario==3?32:8;
        std::vector<EID> army;
        for(int index=0;index<count;++index)
            army.push_back(w.spawnUnit(0,scenario<2?UnitType::AttackDog:UnitType::Grizzly,18.5f+index%8,18.5f+index/8));
        const EID vehicle=w.spawnUnit(1,UnitType::MCV,24.5f,22.5f);
        EID infantry=INVALID_EID,hidden=INVALID_EID;
        if(scenario==1) infantry=w.spawnUnit(1,UnitType::GI,27.5f,22.5f);
        if(scenario==2) hidden=w.spawnBuilding(1,BldType::ConYard,42,42,true);
        w.map.reveal(0,24,22,8);
        if(scenario==3) bot.attackWave=1000;
        const int initial=bot.attackWave;
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        // Isolate AI decisions: no combat, production completion or forced damage.
        for(int frame=0;frame<15;++frame) { bot.update(w);replayBot.update(replay); }
        bool ok=true;
        if(scenario==0) {
            ok=bot.attackWave==initial;
            for(EID id:army) ok &= w.ents[id].target==INVALID_EID;
        } else {
            ok=bot.attackWave==initial+1;
            for(EID id:army) ok &= w.ents[id].target==(scenario==1?infantry:vehicle);
        }
        if(hidden!=INVALID_EID) ok &= w.map.fogAt(0,43,43)==FOG_UNSEEN;
        ok &= w.checksum()==replay.checksum() && bot==replayBot;
        failures+=!ok;
        std::printf("AI_ATTACK %s scenario=%d waves=%d/%d\n",ok?"PASS":"FAIL",scenario,bot.attackWave,initial);
    }
    for(int scenario=0;scenario<5;++scenario) {
        const int faction=scenario<4?scenario:3;
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.terrain=Terrain::Clear; }
        for(auto& player:w.players) { player.defeated=false;player.money=10000; }
        w.spawnBuilding(0,BldType::ConYard,4,4,true);w.spawnBuilding(0,BldType::WarFactory,8,4,true);
        w.spawnBuilding(0,BldType::Barracks,13,4,true);w.spawnBuilding(0,BldType::Radar,18,4,true);
        w.spawnBuilding(0,BldType::BattleLab,22,4,true);w.spawnBuilding(0,BldType::OreRefinery,4,10,true);
        for(int index=0;index<6;++index) w.spawnBuilding(0,BldType::PowerPlant,8+index*3,10,true);
        for(int index=0;index<3;++index) w.spawnUnit(0,harvesterType((Faction)faction),12.5f+index,15.5f);
        const UnitType tank=faction==0?UnitType::Grizzly:faction==1?UnitType::Rhino:faction==2?UnitType::Type99:UnitType::LasherTank;
        for(int index=0;index<7;++index) w.spawnUnit(0,scenario==4?UnitType::Magnetron:tank,18.5f+index,20.5f);
        if(scenario==4) w.spawnUnit(0,UnitType::GatlingTank,26.5f,20.5f);
        SkirmishAI bot;bot.reset(0);
        if(scenario==4) bot.attackWave=1;
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        for(int frame=0;frame<15;++frame) { bot.update(w);replayBot.update(replay); }
        const auto& production=w.players[0].unitProd[1];
        bool ok=production.active && production.typeIdx>=0 && production.typeIdx<(int)UnitType::COUNT
            && (scenario==4?production.typeIdx==(int)UnitType::LasherTank:unitDef((UnitType)production.typeIdx).hasAntiAir())
            && w.unitPrereqMet(0,unitDef((UnitType)production.typeIdx))
            && w.checksum()==replay.checksum() && bot==replayBot;
        failures+=!ok;
        std::printf("%s %s faction=%d type=%d\n",scenario==4?"AI_MIX":"AI_AA",ok?"PASS":"FAIL",faction,production.typeIdx);
    }
    // An isolated land army must not prevent air scouting/raids. These fixtures
    // prove decisions only; generated natural matches remain a separate gate.
    for(int faction=0;faction<4;++faction)for(int scenario=0;scenario<7;++scenario) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Water;}
        for(int y=3;y<20;++y)for(int x=3;x<20;++x)w.map.at(x,y).terrain=Terrain::Clear;
        for(int y=44;y<61;++y)for(int x=44;x<61;++x)w.map.at(x,y).terrain=Terrain::Clear;
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        w.map.reveal(0,10,10,8);
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        SkirmishAI bot;bot.reset(0);bot.rallyPoint={32,32};bot.pcfg.useEngineers=false;
        for(int index=0;index<8;++index)w.spawnUnit(0,UnitType::Grizzly,6.5f+index,8.5f);
        const UnitType type=scenario==5?UnitType::Hornet:faction==0?UnitType::Intruder:faction==1?UnitType::MiG:faction==2?UnitType::Kirov:UnitType::FloatingDisc;
        EID air=w.spawnUnit(0,type,10.5f,11.5f);
        EID enemy=w.spawnBuilding(1,BldType::ConYard,50,50,true);
        auto& aircraft=w.ents[air];aircraft.state=scenario==2?UState::Returning:scenario==3?UState::Landed:UState::Circling;
        aircraft.ammo=scenario==3?std::max(0,unitDef(type).ammo-1):unitDef(type).ammo;
        if(scenario==6)aircraft.state=UState::Landed;
        if(scenario>0)w.map.reveal(0,51,51,7);
        if(scenario==4){aircraft.state=UState::Chasing;aircraft.target=enemy;}
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        for(int frame=0;frame<15;++frame){bot.update(w);replayBot.update(replay);}
        bool ok=w.checksum()==replay.checksum() && bot==replayBot;
        if(scenario==0)ok &= aircraft.state==UState::Moving && aircraft.target==INVALID_EID
            && w.map.fogAt(0,(int)aircraft.goalX,(int)aircraft.goalY)==FOG_UNSEEN && bot.attackWave==0;
        if(scenario==1)ok &= aircraft.target==enemy && aircraft.state==UState::Chasing && bot.attackWave==1;
        if(scenario==2)ok &= aircraft.state==UState::Returning && aircraft.target==INVALID_EID;
        if(scenario==3)ok &= aircraft.state==UState::Landed && aircraft.target==INVALID_EID;
        if(scenario==4)ok &= aircraft.state==UState::Chasing && aircraft.target==enemy && bot.attackWave==0;
        if(scenario==5)ok &= aircraft.state==UState::Circling && aircraft.target==INVALID_EID && bot.attackWave==0;
        if(scenario==6)ok &= faction<2?(aircraft.state==UState::Chasing && aircraft.target==enemy && bot.attackWave==1)
            :(aircraft.state==UState::Landed && aircraft.target==INVALID_EID && bot.attackWave==0);
        failures+=!ok;
        std::printf("AI_AIR %s faction=%d scenario=%d state=%d target=%d waves=%d\n",ok?"PASS":"FAIL",faction,scenario,(int)aircraft.state,aircraft.target,bot.attackWave);
    }
    for(int faction=0;faction<4;++faction)for(int scenario=0;scenario<7;++scenario) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=scenario==2?Terrain::Clear:Terrain::Water;}
        if(scenario==2)for(int y=3;y<14;++y)for(int x=3;x<14;++x)w.map.at(x,y).terrain=Terrain::Water;
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),scenario==3?FOG_SEEN:FOG_UNSEEN);
        w.map.reveal(0,8,8,scenario==2?20:5);
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        SkirmishAI bot;bot.reset(0);bot.pcfg.useEngineers=false;bot.rallyPoint={8,8};
        const UnitType type=faction==0?UnitType::Destroyer:faction==1?UnitType::Typhoon:faction==2?UnitType::Aegis:UnitType::Boomer;
        EID id=w.spawnUnit(0,type,8.5f,8.5f);auto& ship=w.ents[id];
        EID enemy=INVALID_EID;
        if(scenario==1 || scenario==4) {
            enemy=w.spawnUnit(1,faction==2?UnitType::MiG:UnitType::Destroyer,45.5f,45.5f);
            w.map.reveal(0,45,45,7);
        }
        if(scenario==6) {
            enemy=w.spawnUnit(1,faction==2?UnitType::Grizzly:UnitType::MiG,45.5f,45.5f);
            w.map.reveal(0,45,45,7);
        }
        if(scenario==4){ship.state=UState::Chasing;ship.target=enemy;}
        if(scenario==5)ship.chrono=30;
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        for(int frame=0;frame<15;++frame){bot.update(w);replayBot.update(replay);}
        bool ok=w.checksum()==replay.checksum() && bot==replayBot;
        if(scenario==0 || scenario==6) {
            std::vector<Vec2i> path;
            ok &= ship.state==UState::Moving && ship.target==INVALID_EID && bot.attackWave==0
                && w.map.fogAt(0,(int)ship.goalX,(int)ship.goalY)==FOG_UNSEEN
                && w.map.findPath(8,8,(int)ship.goalX,(int)ship.goalY,path,20000,1);
            if(scenario==6)ok &= w.attackRejection(id,enemy)!=AttackRejectReason::None;
        }
        if(scenario==1)ok &= ship.state==UState::Chasing && ship.target==enemy && bot.attackWave==1;
        if(scenario==3) {
            std::vector<Vec2i> path;
            ok &= ship.state==UState::Moving && ship.target==INVALID_EID && bot.attackWave==0
                && w.map.fogAt(0,(int)ship.goalX,(int)ship.goalY)==FOG_SEEN
                && w.map.findPath(8,8,(int)ship.goalX,(int)ship.goalY,path,20000,1);
        }
        if(scenario==2 || scenario==5)ok &= ship.state==UState::Idle && ship.target==INVALID_EID && bot.attackWave==0;
        if(scenario==4)ok &= ship.state==UState::Chasing && ship.target==enemy && bot.attackWave==0;
        failures+=!ok;
        std::printf("AI_NAVAL %s faction=%d scenario=%d state=%d target=%d waves=%d\n",ok?"PASS":"FAIL",faction,scenario,(int)ship.state,ship.target,bot.attackWave);
    }
    for(int faction=0;faction<4;++faction)for(int pending=0;pending<2;++pending) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}
        for(int y=28;y<64;++y)for(int x=0;x<64;++x)w.map.at(x,y).terrain=Terrain::Water;
        for(auto& p:w.players){p.money=10000;p.defeated=false;}
        w.spawnBuilding(0,BldType::ConYard,4,4,true);w.spawnBuilding(0,BldType::WarFactory,8,4,true);
        w.spawnBuilding(0,BldType::Barracks,13,4,true);w.spawnBuilding(0,BldType::Radar,18,4,true);
        w.spawnBuilding(0,BldType::BattleLab,22,4,true);w.spawnBuilding(0,BldType::OreRefinery,4,10,true);
        w.spawnBuilding(0,BldType::AirForceCmd,20,18,true);w.spawnBuilding(0,BldType::NavalYard,20,30,true);
        for(int index=0;index<3;++index)w.spawnUnit(0,harvesterType((Faction)faction),12.5f+index,17.5f);
        for(int index=0;index<6;++index)w.spawnBuilding(0,BldType::PowerPlant,8+index*3,10,true);
        const UnitType escort=faction==0?UnitType::Destroyer:faction==1?UnitType::Typhoon:faction==2?UnitType::Aegis:UnitType::Boomer;
        const UnitType strike=faction==0?UnitType::AircraftCarrier:faction==1?UnitType::Dreadnought:faction==2?UnitType::Aegis:UnitType::Boomer;
        const UnitType air=faction==0?UnitType::Intruder:faction==1?UnitType::MiG:faction==2?UnitType::Kirov:UnitType::FloatingDisc;
        w.spawnUnit(0,escort,24.5f,35.5f);
        if(pending) {
            w.spawnUnit(0,strike,26.5f,35.5f);w.startUnitProd(0,strike);
            w.spawnUnit(0,air,20.5f,20.5f);w.startUnitProd(0,air);
        }
        SkirmishAI bot;bot.reset(0);
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        for(int frame=0;frame<15;++frame){bot.update(w);replayBot.update(replay);}
        const auto& p=w.players[0];bool ok=w.checksum()==replay.checksum() && bot==replayBot;
        if(!pending)ok &= p.unitProd[3].active && p.unitProd[3].typeIdx==(int)strike;
        else {
            ok &= p.unitProd[3].active && p.unitProd[3].typeIdx==(int)strike && p.unitQueue[2].empty();
            if(faction<2)ok &= p.unitQueue[3].size()==1 && p.unitQueue[3].front()==(int)escort;
            else ok &= p.unitQueue[3].size()==1 && p.unitQueue[3].front()==(int)strike;
        }
        failures+=!ok;
        std::printf("AI_FLEET_PRODUCTION %s faction=%d pending=%d navy=%d queue=%zu air_queue=%zu\n",ok?"PASS":"FAIL",faction,pending,p.unitProd[3].typeIdx,p.unitQueue[3].size(),p.unitQueue[2].size());
    }
    for(int faction=0;faction<2;++faction) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        w.cratesEnabled=false;w.spawnBuilding(0,BldType::ConYard,4,4,true);
        EID pad=w.spawnBuilding(0,BldType::AirForceCmd,8,4,true);
        w.spawnBuilding(1,BldType::ConYard,52,52,true);
        EID target=w.spawnUnit(1,UnitType::MCV,35.5f,35.5f);w.ents[target].hp=100000;
        EID jet=w.spawnUnit(0,faction==0?UnitType::Intruder:UnitType::MiG,10.5f,10.5f);w.ents[jet].airbase=pad;
        w.map.reveal(0,32,32,64);SkirmishAI bot;bot.reset(0);
        int returns=0,relaunches=0;bool returning=false;
        for(int tick=0;tick<3600;++tick) {
            w.update();if(!w.valid(jet))break;
            const bool wasOnPad=w.ents[jet].state==UState::Landed;
            if(w.ents[jet].state==UState::Returning && !returning)++returns;
            returning=w.ents[jet].state==UState::Returning;
            bot.update(w);
            const auto state=w.ents[jet].state;
            if(wasOnPad && (state==UState::Chasing || state==UState::Moving || state==UState::AttackMoving)
                && w.ents[jet].ammo>0)++relaunches;
            w.presentationEvents.clear();w.evaQueue.clear();
        }
        const bool ok=w.valid(jet) && returns>=2 && relaunches>=2 && w.combatDamage[0]>0;
        failures+=!ok;
        std::printf("AI_AIR_SORTIES %s faction=%d returns=%d relaunches=%d damage=%llu\n",ok?"PASS":"FAIL",faction,returns,relaunches,(unsigned long long)w.combatDamage[0]);
    }
    return failures+aiAssaultRegression();
}
