#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::aiAirTransportRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("AI_AIR_TRANSPORT %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto fresh=[&](World& w,const char* edition,Faction faction,bool island){
        const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        if(!w.init(64,64,1911,1,1,{faction,Faction::Soviet},0,nullptr,false,false,profile.c_str()))return false;
        w.ents.clear();w.freeList.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto& c=w.map.at(x,y);c.height=0;c.ore=c.oreMax=0;c.blocked=false;c.overlay=Overlay::None;
            c.terrain=!island || (x<27 && y<30) || (x>=39 && y>=39)?Terrain::Clear:Terrain::Water;}
        for(auto& p:w.players){p.isAI=false;p.money=100000;p.defeated=false;}
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);w.cratesEnabled=false;w.rebuildUnitOcc();w.rebuildAirOcc();
        w.spawnBuilding(0,BldType::ConYard,3,3,true);w.spawnBuilding(0,BldType::WarFactory,12,3,true);
        w.spawnBuilding(0,BldType::Barracks,4,10,true);w.spawnBuilding(0,BldType::OreRefinery,4,18,true);
        w.spawnBuilding(0,BldType::Radar,19,3,true);w.spawnBuilding(0,BldType::BattleLab,19,9,true);
        for(int i=0;i<4;++i)w.spawnBuilding(0,BldType::PowerPlant,10+i*4,22,true);
        for(int i=0;i<3;++i)w.spawnUnit(0,harvesterType(faction),13.5f+i,18.5f);
        const EID enemy=w.spawnBuilding(1,BldType::ConYard,51,51,true);if(w.valid(enemy))w.ents[enemy].hp=100000;
        return w.valid(enemy) && w.hasBld(0,BldType::WarFactory) && w.hasBld(0,BldType::BattleLab);
    };
    const auto botFor=[](){SkirmishAI bot;bot.reset(0);bot.pcfg.useSuperWeapon=false;bot.pcfg.useEngineers=false;bot.pcfg.useSpies=false;return bot;};
    struct ProductionCase {const char* edition;Faction faction;UnitType type;bool airport;AIDiff difficulty;int wave;};
    for(const auto c:{ProductionCase{"ra2",Faction::Allies,UnitType::Intruder,true,AIDiff::Normal,0},
                      {"ra2",Faction::Allies,UnitType::BlackEagle,true,AIDiff::Normal,0},
                      {"yr",Faction::Allies,UnitType::BlackEagle,true,AIDiff::Normal,0},
                      {"ra2",Faction::Soviet,UnitType::Kirov,false,AIDiff::Normal,0},
                      {"ra2",Faction::Soviet,UnitType::Kirov,false,AIDiff::Hard,2},
                      {"yr",Faction::Soviet,UnitType::Kirov,false,AIDiff::Normal,0},
                      {"yr",Faction::Yuri,UnitType::FloatingDisc,false,AIDiff::Normal,0}}) {
        World w;bool ok=fresh(w,c.edition,c.faction,false);if(c.airport)w.spawnBuilding(0,BldType::AirForceCmd,32,4,true);
        if(c.type==UnitType::BlackEagle)w.players[0].country=Country::Korea;
        auto bot=botFor();bot.difficulty=c.difficulty;bot.attackWave=c.wave;bool queued=false,produced=false;int producedAt=-1;
        const int selector=w.productionSelector(0,true,(int)c.type);const int category=selector>=0?w.productionCategory(true,selector):-1;
        const auto money=w.players[0].money;
        for(int i=0;i<6500 && ok && !produced;++i){w.update();bot.update(w);
            if(category>=0)queued |= w.players[0].unitProd[category].active && w.players[0].unitProd[category].typeIdx==selector;
            produced=w.countUnits(0,c.type)>0;if(produced)producedAt=i;
        }
        std::printf("AI_AIR_TRANSPORT PRODUCTION edition=%s faction=%d type=%s difficulty=%d wave=%d category=%d queued=%d produced=%d tick=%d money=%lld\n",c.edition,(int)c.faction,unitName(c.type),(int)c.difficulty,c.wave,category,queued,produced,producedAt,(long long)w.players[0].money);
        check(ok && queued && produced && w.producedUnits[0]>0 && w.players[0].money<money,"a developed source AI pays for its legal aircraft using the actual production category and factory");
        if(!c.airport)check(!w.hasBld(0,BldType::AirForceCmd),"source vehicle-factory aircraft production does not require an unavailable airfield");
        int maximum=0;for(int i=0;i<4500 && produced;++i){w.update();bot.update(w);
            const auto& item=w.players[0].unitProd[category];const int planned=w.countUnits(0,c.type)+(item.active && item.typeIdx==selector)
                +(int)std::count(w.players[0].unitQueue[category].begin(),w.players[0].unitQueue[category].end(),selector);maximum=std::max(maximum,planned);}
        check(produced && maximum<=2 && w.countUnits(0,c.type)==2,"extended AI production maintains two strike aircraft including pending work instead of endlessly queueing the same special unit");
    }
    struct EasyCase {const char* edition;Faction faction;UnitType type;bool airport,allowed;};
    for(const auto c:{EasyCase{"ra2",Faction::Allies,UnitType::Intruder,true,true},
                      {"ra2",Faction::Allies,UnitType::BlackEagle,true,false},
                      {"yr",Faction::Allies,UnitType::BlackEagle,true,false},
                      {"ra2",Faction::Soviet,UnitType::Kirov,false,false},
                      {"yr",Faction::Soviet,UnitType::Kirov,false,false},
                      {"yr",Faction::Yuri,UnitType::FloatingDisc,false,false},
                      {"ra2",Faction::Allies,UnitType::Nighthawk,false,false},
                      {"yr",Faction::Allies,UnitType::Nighthawk,false,false}}) {
        World w;bool ok=fresh(w,c.edition,c.faction,c.type==UnitType::Nighthawk);auto bot=botFor();bot.difficulty=AIDiff::Easy;
        if(c.airport)w.spawnBuilding(0,BldType::AirForceCmd,32,4,true);
        if(c.type==UnitType::BlackEagle)w.players[0].country=Country::Korea;
        const int selector=w.productionSelector(0,true,(int)c.type);const int category=selector>=0?w.productionCategory(true,selector):-1;bool queued=false;
        for(int frame=0;frame<6500 && ok;++frame){w.update();bot.update(w);
            if(category>=0)queued |= w.players[0].unitProd[category].active && w.players[0].unitProd[category].typeIdx==selector;}
        const bool built=w.countUnits(0,c.type)>0;
        std::printf("AI_AIR_TRANSPORT EASY edition=%s type=%s allowed=%d queued=%d built=%d total_produced=%llu\n",c.edition,unitName(c.type),c.allowed,queued,built,(unsigned long long)w.producedUnits[0]);
        check(ok && w.producedUnits[0]>0 && (c.allowed?(queued && built):(!queued && !built)),"the simple AI respects the original aircraft difficulty gate while continuing ordinary production");
    }
    for(const char* edition:{"ra2","yr"}) {
        World w;bool ok=fresh(w,edition,Faction::Soviet,false);const EID actor=w.spawnUnit(0,UnitType::Kirov,40.5f,20.5f);
        ok &= w.valid(actor);if(ok){w.orderMove({actor},45.5f,23.5f,false);for(int frame=0;frame<1800;++frame)w.update();
            const auto& e=w.ents[actor];std::printf("AI_AIR_TRANSPORT ARRIVAL edition=%s state=%d xy=%.6f,%.6f goal=%.6f,%.6f speed=%.6f desired=%.6f phase=%d\n",edition,(int)e.state,e.x,e.y,e.goalX,e.goalY,e.nativeJumpjet.speed,e.nativeJumpjet.desiredSpeed,(int)e.nativeJumpjet.phase);
            ok &= e.state==UState::Circling && (int)e.x==45 && (int)e.y==23;
            w.orderMove({actor},40.5f,16.5f,false);for(int frame=0;frame<1800;++frame)w.update();
            ok &= w.ents[actor].state==UState::Circling && (int)w.ents[actor].x==40 && (int)w.ents[actor].y==16;}
        check(ok,"a source Kirov finishes the arrival handoff and can finish a second oblique move instead of remaining permanently Moving at zero speed");
    }
    for(const char* edition:{"ra2","yr"}) {
        World w;bool ok=fresh(w,edition,Faction::Allies,true);auto bot=botFor();Game restored;bool checkpoint=false,attempted=false,equal=true;
        bool produced=false,boarded=false,crossed=false,pending=false,landed=false,returned=false,secondLoad=false;std::string passenger;EID aircraft=INVALID_EID;
        int landingTick=-1,maximum=0;bool privacy=false,privacyChecked=false,frozen=false;const auto money=w.players[0].money;
        for(int i=0;i<7500 && ok && !secondLoad;++i){
            w.update();bot.update(w);
            if(checkpoint){restored.world.update();restored.ais[0].update(restored.world);equal &= w.checksum()==restored.world.checksum() && bot==restored.ais[0];}
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && !w.ents[id].isBuilding && w.ents[id].player==0 && w.ents[id].utype==UnitType::Nighthawk){aircraft=id;produced=true;break;}
            if(!w.valid(aircraft))continue;const auto& air=w.ents[aircraft];
            maximum=std::max(maximum,w.countUnits(0,UnitType::Nighthawk));
            if(!air.cargo.empty()){boarded=true;if(passenger.empty())passenger=air.cargo.front().nativeIdentity.object;}
            crossed |= !air.cargo.empty() && air.x>=39 && air.y>=39;
            pending |= air.nativeAirUnload;
            for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.player==0 && !passenger.empty() && e.nativeIdentity.object==passenger && e.x>=39 && e.y>=39){landed=true;if(landingTick<0)landingTick=i;}
            returned |= landed && air.cargo.empty() && air.x<27 && air.y<30;
            secondLoad |= returned && !air.cargo.empty();
            if(!privacyChecked && !air.cargo.empty() && air.state==UState::Moving && air.x<27 && air.y<30){
                privacyChecked=true;World first=w,other=w;first.map.bldOccRef=&first.bldOcc;other.map.bldOccRef=&other.bldOcc;
                for(EID id=0;id<(EID)other.ents.size();++id)if(other.valid(id) && other.ents[id].isBuilding && other.ents[id].player==1)other.kill(id,false);
                other.spawnBuilding(1,BldType::ConYard,39,39,true);
                for(World* p:{&first,&other}){for(auto& fog:p->map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);p->orderStop({aircraft});}
                auto a=bot,b=bot;for(int frame=0;frame<30;++frame){a.update(first);b.update(other);}
                const auto& x=first.ents[aircraft];const auto& y=other.ents[aircraft];privacy=x.state==UState::Moving && y.state==UState::Moving
                    && x.goalX>=39 && x.goalY>=39 && x.goalX==y.goalX && x.goalY==y.goalY;
            }
            if(!attempted && air.nativeAirUnload){attempted=true;Game saved;saved.world=w;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.ais={bot};saved.phase=Phase::InGame;saved.cfgRules=std::string(edition)=="ra2"?RulesChoice::RA2:RulesChoice::YR;saved.localPlayer=0;
                const auto path=(std::filesystem::path(outputDir)/(std::string(edition)+"-unload-app11.sav")).string();
                checkpoint=saved.saveGameFile(path.c_str()) && restored.loadGameFile(path.c_str()) && restored.ais.size()==1;equal &= checkpoint && w.checksum()==restored.world.checksum() && bot==restored.ais[0];
                World held=w;held.map.bldOccRef=&held.bldOcc;auto heldBot=bot;held.ents[aircraft].tpSick=61;
                frozen=true;for(int frame=0;frame<60;++frame){held.update();heldBot.update(held);const auto& e=held.ents[aircraft];
                    frozen &= e.x==air.x && e.y==air.y && e.nativeAltitude==air.nativeAltitude && e.nativeAirUnload && e.cargo.size()==air.cargo.size();}
            }
        }
        std::printf("AI_AIR_TRANSPORT JOURNEY edition=%s produced=%d boarded=%d crossed=%d pending=%d landed=%d return=%d second=%d checkpoint=%d equal=%d landing_tick=%d actor=%d state=%d cargo=%zu pos=%.3f,%.3f\n",edition,produced,boarded,crossed,pending,landed,returned,secondLoad,checkpoint,equal,landingTick,aircraft,w.valid(aircraft)?(int)w.ents[aircraft].state:-1,w.valid(aircraft)?w.ents[aircraft].cargo.size():0,w.valid(aircraft)?w.ents[aircraft].x:0,w.valid(aircraft)?w.ents[aircraft].y:0);
        check(ok && produced && w.players[0].money<money,"the island AI produces its own source Nighthawk without a supplied transport or manual production command");
        check(boarded && crossed && pending && landed,"AI-produced infantry board, cross water and physically land before the same passenger is unloaded");
        check(returned && secondLoad,"the empty transport returns home and starts a second AI-controlled trip");
        check(checkpoint && equal,"a real App11 snapshot resumes the AI and pending landing/unload decisions identically");
        check(privacyChecked && privacy,"moving an unseen enemy base onto the nearest shore cannot change the AI's first landing order");
        check(frozen,"AI orders preserve the frozen transport's physical position and pending unload for sixty ticks");
        check(produced && maximum==1,"one transport completes repeated trips without duplicate live aircraft");
        bool respected=false;if(w.valid(aircraft)){World captured=w;captured.map.bldOccRef=&captured.bldOcc;auto oldBot=bot;captured.ents[aircraft].player=1;
            const auto before=captured.ents[aircraft];for(int frame=0;frame<30;++frame)oldBot.update(captured);const auto& after=captured.ents[aircraft];
            respected=before.x==after.x && before.y==after.y && before.state==after.state && before.goalX==after.goalX && before.goalY==after.goalY && before.cargo.size()==after.cargo.size();}
        check(respected,"the former owner's AI does not command a captured loaded transport");
        bool replaced=false;if(w.valid(aircraft)){World lost=w;lost.map.bldOccRef=&lost.bldOcc;auto replacementBot=bot;
            const auto oldKey=lost.ents[aircraft].nativeIdentity.object;lost.kill(aircraft,false);
            for(int frame=0;frame<6000 && !replaced;++frame){lost.update();replacementBot.update(lost);
                for(const auto& e:lost.ents)if(e.alive && !e.isBuilding && e.player==0 && e.utype==UnitType::Nighthawk)
                    replaced=e.nativeIdentity.object!=oldKey && lost.countUnits(0,UnitType::Nighthawk)==1;
            }
        }
        check(replaced,"after losing a loaded transport the AI pays for one replacement with a fresh source identity");
    }
    for(const char* edition:{"ra2","yr"})for(auto faction:{Faction::Allies,Faction::Soviet,Faction::Yuri}) {
        if(faction==Faction::Yuri && std::string(edition)=="ra2")continue;
        World w;const bool ok=fresh(w,edition,faction,false);
        const auto defense=faction==Faction::Allies?BldType::PatriotMissile:faction==Faction::Soviet?BldType::FlakCannon:BldType::GatlingCannon;
        if(faction==Faction::Yuri){w.spawnBuilding(0,defense,45,3,true);w.spawnBuilding(0,defense,50,3,true);}
        const int before=w.countBlds(0,defense);const auto money=w.players[0].money;
        World hidden=w;hidden.map.bldOccRef=&hidden.bldOcc;
        const EID unseen=hidden.spawnUnit(1,UnitType::Kirov,49.5f,39.5f);if(hidden.valid(unseen))hidden.ents[unseen].guard=false;
        const EID threat=w.spawnUnit(1,UnitType::Kirov,20.5f,20.5f);if(w.valid(threat))w.ents[threat].guard=false;
        auto bot=botFor(),control=botFor();
        for(int frame=0;frame<60;++frame){hidden.update();control.update(hidden);}
        const auto& hiddenOrder=hidden.players[0].defProd;
        const int hiddenType=hiddenOrder.active && hidden.nativeProductionType(hiddenOrder.typeIdx)?(int)hidden.nativeTypes[hiddenOrder.typeIdx-NATIVE_PRODUCTION_BASE]->building.type:-1;
        const bool privateDecision=hidden.valid(unseen) && hidden.map.fogAt(0,(int)hidden.ents[unseen].x,(int)hidden.ents[unseen].y)!=FOG_VISIBLE
            && hidden.countBlds(0,defense)==before && hiddenType!=(int)defense;
        for(int frame=0;frame<3000;++frame){w.update();bot.update(w);}
        check(ok && w.countBlds(0,defense)>before && w.players[0].money<money,
              "a visible source air raid causes the AI to pay for and place a legal anti-air building near its base");
        check(privateDecision,
              "an aircraft outside current vision cannot trigger the initial anti-air construction order");
        World held;const bool holding=fresh(held,edition,faction,false);const EID attacker=held.spawnUnit(1,UnitType::Kirov,20.5f,20.5f);
        if(held.valid(attacker))held.ents[attacker].guard=false;
        const auto ground=faction==Faction::Allies?BldType::Pillbox:faction==Faction::Soviet?BldType::SentryGun:BldType::GatlingCannon;
        held.startBldProd(0,ground);for(int frame=0;frame<240 && held.players[0].defProd.active && held.players[0].defProd.paid==0;++frame)held.update();held.holdBldProd(0,ground,true);
        const auto order=held.players[0].defProd;auto keeper=botFor();
        for(int frame=0;frame<120;++frame){held.update();keeper.update(held);}
        const auto& after=held.players[0].defProd;
        check(holding && order.active && order.held && order.paid>0 && after.active && after.held && after.typeIdx==order.typeIdx && after.paid==order.paid,
              "air-defense construction preserves an already paid and held defense order");
    }
    return failures;
}
