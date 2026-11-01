#include "game/game.h"
#include <array>
#include <cstdio>

int Game::aiAirDefenseRegression() {
    int failures=0;
    const std::array<UnitType,4> aa{UnitType::IFV,UnitType::FlakTrack,UnitType::Apocalypse,UnitType::GatlingTank};
    for(int faction=0;faction<4;++faction)for(int scenario=0;scenario<11;++scenario) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Yuri},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};if(scenario==3 || scenario==4)cell.terrain=Terrain::Water;}
        if(scenario==3) {
            for(int y=14;y<30;++y)for(int x=4;x<14;++x)w.map.at(x,y).terrain=Terrain::Clear;
            // Keep the water gap wider than every defender's weapon range.
            // The eight-cell Apocalypse missile can shoot across the old gap.
            for(int y=16;y<28;++y)for(int x=26;x<40;++x)w.map.at(x,y).terrain=Terrain::Clear;
        }
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        w.cratesEnabled=false;for(auto& player:w.players){player.money=0;player.defeated=false;}
        w.players[0].isAI=true;
        const int assetX=scenario==3?28:20;
        const EID asset=w.spawnBuilding(0,scenario==4?BldType::NavalYard:BldType::PowerPlant,assetX,20,true);
        const auto& def=bldDef(w.ents[asset].btype);
        const float tx=assetX+def.w/2.f,ty=20+def.h/2.f;
        // Aegis ownership also exercises a captured legal surface defender;
        // these fixtures supply existing units, not production prerequisites.
        const auto type=scenario==4?UnitType::Aegis:scenario==7?UnitType::Grizzly:scenario==10?UnitType::FlakTrooper:aa[faction];
        const auto& unitDefinition=unitDef(type);
        const float aaRange=unitDefinition.hasAirWeapon?unitDefinition.airWeapon.range:unitDefinition.weapon.range;
        const float sx=scenario==8?tx-2:scenario==2?tx-aaRange-4:12.5f,sy=scenario==8?ty:22.5f;
        const EID defender=w.spawnUnit(0,type,sx,sy);
        const EID enemy=w.spawnUnit(1,UnitType::FloatingDisc,tx,ty);
        w.ents[enemy].hp=200;w.ents[enemy].state=UState::Chasing;w.ents[enemy].target=asset;
        if(scenario==1)w.ents[defender].tpSick=120;
        if(scenario==5)std::fill(w.map.fog[0].begin(),w.map.fog[0].end(),FOG_SEEN);
        else w.map.reveal(0,(int)tx,(int)ty,4);
        if(scenario==8)w.ents[defender].guard=true;
        EID taskTarget=INVALID_EID;bool taskStarted=true;
        if(scenario==9) {
            taskTarget=w.spawnBuilding(0,BldType::ServiceDepot,6,20,true);w.ents[defender].hp-=50;
            taskStarted=w.orderService({defender},taskTarget).accepted==1;
            for(int index=0;index<8;++index)w.spawnUnit(0,UnitType::Grizzly,28.5f+index,28.5f);
        }
        if(scenario==10){taskTarget=w.spawnUnit(0,UnitType::AmphTransport,8.5f,22.5f);taskStarted=w.orderBoard({defender},taskTarget).accepted==1;}
        std::vector<Vec2i> initialPath;
        if(scenario==6){w.orderAttack({defender},enemy);w.ents[defender].blockTick=17;initialPath=w.ents[defender].path;}
        SkirmishAI bot;bot.reset(0);bot.rallyPoint={45,45};bot.pcfg.rallySize=1000;
        bot.pcfg.useEngineers=false;bot.pcfg.useSpies=false;bot.pcfg.useSuperWeapon=false;
        if(scenario==9){bot.pcfg.rallySize=0;bot.attackTimer=7;}
        if(scenario==2){w.orderMove({defender},5.5f,40.5f,false);bot.thinkTimer=14;}
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;SkirmishAI replayBot=bot;
        const bool actual=scenario<=4;
        bool equal=true,legal=true,budget=true,protectedBefore=true,saved=false,maintained=true;
        int finished=-1;uint64_t requestsBefore=w.repathRequests;Vec2f held{sx,sy};
        for(int frame=0;frame<(actual?3600:30);++frame) {
            if(scenario==3 && frame==300)for(int x=14;x<26;++x){w.map.at(x,22).terrain=Terrain::Clear;replay.map.at(x,22).terrain=Terrain::Clear;}
            if(actual){w.update();replay.update();}
            if((scenario==2 && frame<120) || scenario==8){w.repathsThisTick=8;replay.repathsThisTick=8;}
            bot.update(w);replayBot.update(replay);
            equal &= w.checksum()==replay.checksum() && bot==replayBot;budget &= w.repathsThisTick<=8;
            const auto& unit=w.ents[defender];
            legal &= w.passableFor((int)unit.x,(int)unit.y,unitDef(type).pathDomain())
                && !w.bldBlocked((int)unit.x,(int)unit.y) && unit.occCell==(int)unit.y*w.map.w+(int)unit.x;
            if(scenario==1 && frame<119)
                protectedBefore &= unit.x==sx && unit.y==sy && unit.target==INVALID_EID && w.ents[enemy].hp==200;
            if(scenario==2 && frame<120) {
                if(frame==0)held={unit.x,unit.y};
                protectedBefore &= unit.x==held.x && unit.y==held.y && unit.target==INVALID_EID && w.ents[enemy].hp==200;
            }
            if(scenario==3 && frame<300)protectedBefore &= w.ents[enemy].hp==200;
            if(scenario==6)maintained &= unit.path==initialPath && unit.blockTick==17 && unit.target==enemy;
            if(scenario==9 || scenario==10)maintained &= unit.target==taskTarget
                && unit.state==(scenario==9?UState::Servicing:UState::Boarding);
            if(frame==10) {
                FILE* file=tmpfile();equal &= file && w.saveGame(file);
                if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;
            }
            if(actual && !w.valid(enemy)){finished=frame;break;}
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        bool outcome=actual?finished>=0 && w.combatDamage[0]>0:w.ents[defender].target!=enemy;
        if(scenario==6)outcome=maintained && w.repathRequests==requestsBefore;
        if(scenario==9 || scenario==10)outcome=taskStarted && maintained;
        if(scenario==8)outcome=w.ents[defender].target==enemy && w.ents[defender].state==UState::Attacking
            && w.ents[defender].guard && w.repathRequests==requestsBefore;
        const bool ok=outcome && equal && legal && budget && protectedBefore && saved && w.players[0].money==0;
        failures+=!ok;
        std::printf("AI_AIR_DEFENSE %s faction=%d scenario=%d actual=%d finished=%d damage=%llu replay=%d legal=%d budget=%d protected=%d saved=%d requests=%llu\n",ok?"PASS":"FAIL",faction,scenario,actual,finished,(unsigned long long)w.combatDamage[0],equal,legal,budget,protectedBefore,saved,(unsigned long long)(w.repathRequests-requestsBefore));
    }
    return failures;
}
