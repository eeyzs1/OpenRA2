#include "game/game.h"
#include <cstdio>

namespace {
int vehicleTransportChecks() {
    int failures=0;
    auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("VEHICLE_TRANSPORT %s %s\n",ok?"PASS":"FAIL",name);};
    auto fixture=[]() {
        World world;world.init(32,32,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;for(auto& player:world.players){player.money=0;player.defeated=false;}
        return world;
    };
    auto advance=[](World& world,int ticks){for(int i=0;i<ticks;++i)world.update();};
    auto roundTrip=[&](World& world,World& loaded) {
        FILE* f=tmpfile();bool ok=f && world.saveGame(f);
        if(f){rewind(f);ok &= loaded.loadGame(f);fclose(f);}
        return ok && world.checksum()==loaded.checksum();
    };
    check(unitDef(UnitType::AmphTransport).cargoCap==12 && unitDef(UnitType::AmphTransport).cargoSizeLimit==6
        && unitDef(UnitType::MCV).transportSize()==6 && unitDef(UnitType::Apocalypse).transportSize()==6
        && unitDef(UnitType::Grizzly).transportSize()==3 && unitDef(UnitType::GI).transportSize()==1,
        "weighted original-rule capacities include ordinary and heavy vehicles");
    {
        auto w=fixture();w.map.bldOccRef=&w.bldOcc;EID boat=w.spawnUnit(0,UnitType::AmphTransport,15.5f,15.5f);std::vector<EID> tanks;
        for(int i=0;i<5;++i)tanks.push_back(w.spawnUnit(0,UnitType::Grizzly,11.5f+i,12.5f));
        check(w.orderBoard(tanks,boat).accepted==5,"five approaching tanks may compete for four actual slots");
        advance(w,900);int alive=0;for(EID id:tanks)alive+=w.valid(id);
        check(w.ents[boat].cargo.size()==4 && World::cargoLoad(w.ents[boat].cargo)==12 && alive==1,
            "capacity race loads exactly four tanks without losing fifth");
        check(w.orderUnload({boat}).released==4 && w.ents[boat].cargo.empty(),"all four tanks unload into free land cells");
        bool legal=true;std::vector<Vec2i> cells;
        for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.utype==UnitType::Grizzly) {
            Vec2i cell{(int)e.x,(int)e.y};legal &= w.map.passable(cell.x,cell.y) && !w.bldBlocked(cell.x,cell.y);
            for(auto other:cells)legal &= other!=cell;cells.push_back(cell);
        }
        check(legal && cells.size()==5,"unloaded tanks neither overlap nor cross buildings");
    }
    {
        auto w=fixture();w.map.bldOccRef=&w.bldOcc;EID boat=w.spawnUnit(0,UnitType::AmphTransport,15.5f,15.5f),tank=w.spawnUnit(0,UnitType::Grizzly,14.5f,15.5f);
        w.ents[tank].hp=123;w.ents[tank].kills=2;w.ents[tank].veterancyValue=500;
        w.ents[tank].crateDmgBoost=10000;w.ents[tank].crateArmorBoost=9000;w.ents[tank].crateSpeedBoost=8000;
        w.orderBoard({tank},boat);advance(w,1);bool loaded=w.ents[boat].cargo.size()==1;
        World replay;check(loaded && roundTrip(w,replay),"damaged boosted vehicle survives loaded save and checksum");
        if(loaded) {
            auto saved=w.ents[boat].cargo.front();auto a=w.orderUnload({boat}),b=replay.orderUnload({boat});
            bool preserved=a.released==1 && b.released==1 && w.checksum()==replay.checksum();
            for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.utype==UnitType::Grizzly)
                preserved &= e.hp==saved.hp && e.kills==saved.kills && e.veterancyValue==saved.veterancyValue
                    && e.crateDmgBoost==saved.crateDmgBoost && e.crateArmorBoost==saved.crateArmorBoost && e.crateSpeedBoost==saved.crateSpeedBoost;
            for(int i=0;i<300;++i){w.update();replay.update();preserved &= w.checksum()==replay.checksum();}
            check(preserved,"vehicle health experience buffs and deterministic continuation preserved");
        }
    }
    {
        auto w=fixture();w.map.bldOccRef=&w.bldOcc;EID boat=w.spawnUnit(0,UnitType::AmphTransport,15.5f,15.5f),ifv=w.spawnUnit(0,UnitType::IFV,14.5f,15.5f);
        EID engineer=w.spawnUnit(0,UnitType::Engineer,13.5f,15.5f);w.ents[engineer].hp=71;
        w.orderBoard({engineer},ifv);advance(w,1);check(w.ents[ifv].cargo.size()==1,"infantry vehicle actually receives engineer before transport");
        w.orderBoard({ifv},boat);advance(w,1);World replay;
        bool nested=w.ents[boat].cargo.size()==1 && w.ents[boat].cargo.front().cargo.size()==1;
        check(nested && roundTrip(w,replay),"loaded infantry vehicle and engineer survive nested cargo save");
        check(World::cargoUnitCount(w.ents[boat].cargo,UnitType::Engineer)==1,"nested passenger counts once in unit inventory");
        if(nested) {
            auto hash=w.checksum();auto& passenger=w.ents[boat].cargo.front().cargo.front();++passenger.ammo;
            check(hash!=w.checksum(),"nested passenger state participates in simulation checksum");--passenger.ammo;
            passenger.ammo=-1;FILE* bad=tmpfile();bool rejected=bad && !w.saveGame(bad);if(bad)fclose(bad);passenger.ammo=0;
            check(rejected,"malformed nested passenger state is rejected before save");
        }

        bool restored=w.orderUnload({boat}).released==1 && replay.orderUnload({boat}).released==1 && w.checksum()==replay.checksum();
        EID restoredIfv=INVALID_EID;
        for(size_t i=0;i<w.ents.size();++i)if(w.ents[i].alive && !w.ents[i].isBuilding && w.ents[i].utype==UnitType::IFV)restoredIfv=(EID)i;
        restored &= w.valid(restoredIfv) && w.ents[restoredIfv].cargo.size()==1 && w.ents[restoredIfv].cargo.front().hp==71;
        check(restored,"unloaded infantry vehicle retains its engineer and repair mode");
        if(w.valid(restoredIfv)) {
            auto a=w.orderUnload({restoredIfv}),b=replay.orderUnload({restoredIfv});
            check(a.released==1 && b.released==1 && w.checksum()==replay.checksum(),"nested passenger can unload normally after ferry trip");
        }
    }
    {
        auto w=fixture();w.map.bldOccRef=&w.bldOcc;EID boat=w.spawnUnit(0,UnitType::AmphTransport,15.5f,15.5f),miner=w.spawnUnit(0,UnitType::WarMiner,14.5f,15.5f);
        w.ents[miner].autoHarvest=false;w.ents[miner].oreLoad=20;w.ents[miner].gemLoad=4;
        w.orderBoard({miner},boat);advance(w,1);World replay;
        check(w.ents[boat].cargo.size()==1 && roundTrip(w,replay),"ore-carrying miner can board and save without losing cargo");
        auto result=w.orderUnload({boat});bool ok=result.released==1;
        for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.utype==UnitType::WarMiner)
            ok &= e.oreLoad==20 && e.gemLoad==4 && !e.autoHarvest;
        check(ok,"unloaded miner retains ore gems and manual harvest mode");
    }
    {
        auto w=fixture();w.map.bldOccRef=&w.bldOcc;EID boat=w.spawnUnit(0,UnitType::AmphTransport,15.5f,15.5f);
        EID tank=w.spawnUnit(0,UnitType::Grizzly,14.5f,15.5f),ifv=w.spawnUnit(0,UnitType::IFV,13.5f,15.5f);
        check(w.entryRejection(tank,ifv,EntryKind::Board)==EntryRejectReason::Incompatible,"infantry vehicle still rejects another tank");
        EID air=w.spawnUnit(0,UnitType::Intruder,12.5f,15.5f),navy=w.spawnUnit(0,UnitType::Destroyer,11.5f,15.5f);
        check(w.entryRejection(air,boat,EntryKind::Board)==EntryRejectReason::Incompatible
            && w.entryRejection(navy,boat,EntryKind::Board)==EntryRejectReason::Incompatible,"ferry rejects aircraft and warships");
        EID other=w.spawnUnit(0,UnitType::AmphTransport,10.5f,15.5f);
        check(w.entryRejection(other,boat,EntryKind::Board)==EntryRejectReason::Incompatible,"ferry cannot recursively load another ferry");
        w.ents[tank].mindBy=ifv;
        check(w.entryRejection(tank,boat,EntryKind::Board)==EntryRejectReason::Incompatible,"active entity-linked control is not erased by boarding");
        w.ents[tank].mindBy=INVALID_EID;w.orderBoard({tank},boat);advance(w,1);
        for(auto& cell:w.map.cells)cell.terrain=Terrain::Water;
        check(w.orderUnload({boat}).released==0 && w.ents[boat].cargo.size()==1,"loaded vehicle stays aboard when no reachable landing exists");
        for(auto& cell:w.map.cells)cell.terrain=Terrain::Clear;
        check(w.orderUnload({boat}).released==1,"vehicle unload recovers after landing becomes available");
    }
    {
        auto w=fixture();w.map.bldOccRef=&w.bldOcc;
        EID boat=w.spawnUnit(0,UnitType::AmphTransport,15.5f,15.5f);
        EID a=w.spawnUnit(0,UnitType::MCV,14.5f,15.5f),b=w.spawnUnit(0,UnitType::Apocalypse,15.5f,14.5f);
        w.orderBoard({a,b},boat);advance(w,2);
        check(w.ents[boat].cargo.size()==2 && World::cargoLoad(w.ents[boat].cargo)==12,"two size-six heavy vehicles fill ferry exactly");
        EID extra=w.spawnUnit(0,UnitType::GI,14.5f,14.5f);
        check(w.entryRejection(extra,boat,EntryKind::Board)==EntryRejectReason::Full,"weighted full ferry rejects an additional infantry passenger");
        check(w.orderUnload({boat}).released==2,"construction vehicle and heavy tank unload normally");
    }
    return failures;
}
}

int Game::aiTransportRegression() {
    int failures=vehicleTransportChecks();
    const auto& initiate=unitDef(UnitType::Initiate).weapon;
    constexpr float flameVerses[]={1,0.8f,0.8f,0.5f,0.25f,0.25f,0.75f,0.5f,0.25f,1,1};
    static_assert(std::size(flameVerses)==(int)Armor::COUNT);
    bool flame=initiate.warhead==WeaponDef::Warhead::SAFlame && unitDef(UnitType::Yuri).weapon.warhead==WeaponDef::Warhead::Psychic;
    GameRules defaults;
    for(int armor=0;armor<(int)Armor::COUNT;++armor)flame &= weaponVsArmor(initiate,(Armor)armor,false,armor==(int)Armor::Concrete)==flameVerses[armor]
        && defaults.warheadVerses[(int)WeaponDef::Warhead::SAFlame][armor]==flameVerses[armor];
    failures+=!flame;std::printf("INITIATE_FLAME %s distinct damaging warhead and complete armor matrix\n",flame?"PASS":"FAIL");
    for(int faction=0;faction<4;++faction)for(int scenario=0;scenario<9;++scenario) {
        World w;w.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=scenario==3?Terrain::Clear:Terrain::Water;}
        for(int y=3;y<27;++y)for(int x=3;x<21;++x)w.map.at(x,y).terrain=Terrain::Clear;
        for(int y=39;y<61;++y)for(int x=39;x<61;++x)w.map.at(x,y).terrain=Terrain::Clear;
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        w.cratesEnabled=false;
        w.spawnBuilding(0,BldType::ConYard,4,4,true);w.spawnBuilding(0,BldType::Barracks,4,10,true);
        w.spawnBuilding(0,BldType::WarFactory,10,4,true);w.spawnBuilding(0,BldType::OreRefinery,4,18,true);
        w.spawnBuilding(0,BldType::NavalYard,22,15,true);
        EID miner=w.spawnUnit(0,UnitType::Harvester,8.5f,22.5f);w.ents[miner].autoHarvest=false;
        EID enemy=w.spawnBuilding(1,BldType::ConYard,49,49,true);w.ents[enemy].hp=10000;
        const UnitType tank=faction==0?UnitType::Grizzly:faction==1?UnitType::Rhino:faction==2?UnitType::Type99:UnitType::LasherTank;
        const UnitType troop=scenario==7?tank:faction==0?UnitType::GI:faction==1?UnitType::Conscript:faction==2?UnitType::PLA:UnitType::Initiate;
        for(int index=0;index<(scenario==1?1:6);++index)w.spawnUnit(0,troop,11.5f+index,14.5f);
        if(scenario==8)for(int i=0;i<3;++i)w.spawnUnit(0,tank,11.5f+i,16.5f);
        EID boat=INVALID_EID;
        if(scenario!=2)boat=w.spawnUnit(0,UnitType::AmphTransport,14.5f,20.5f);
        else {
            w.spawnUnit(0,faction==0?UnitType::Destroyer:faction==1?UnitType::Typhoon:faction==2?UnitType::Aegis:UnitType::Boomer,25.5f,19.5f);
            w.players[0].money=2000;
        }
        SkirmishAI bot;bot.reset(0);bot.pcfg.useEngineers=false;bot.pcfg.useSuperWeapon=false;
        if(scenario==6) {
            std::vector<EID> passengers;
            for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive && w.ents[id].player==0 && !w.ents[id].isBuilding && w.ents[id].utype==troop)passengers.push_back((EID)id);
            w.orderBoard(passengers,boat);for(int tick=0;tick<600;++tick)w.update();
            World alternative=w;alternative.map.bldOccRef=&alternative.bldOcc;
            alternative.kill(enemy,false);alternative.spawnBuilding(1,BldType::ConYard,54,54,true);
            SkirmishAI other=bot;
            for(int frame=0;frame<15;++frame){bot.update(w);other.update(alternative);}
            const auto& a=w.ents[boat];const auto& b=alternative.ents[boat];
            const bool ok=!a.cargo.empty() && a.state==UState::Moving && b.state==UState::Moving
                && !a.path.empty() && !b.path.empty()
                && a.path.back()==b.path.back() && a.path.back().x>=39 && a.path.back().y>=39;
            failures+=!ok;std::printf("AI_TRANSPORT_PRIVACY %s faction=%d unseen enemy position does not choose first shore\n",ok?"PASS":"FAIL",faction);continue;
        }
        if(scenario==2 || scenario==3) {
            for(int frame=0;frame<15;++frame)bot.update(w);
            bool ok=scenario==2?(w.players[0].unitProd[3].active && w.players[0].unitProd[3].typeIdx==(int)UnitType::AmphTransport)
                :(w.ents[boat].cargo.empty() && w.ents[boat].state==UState::Idle);
            if(scenario==3)for(const auto& unit:w.ents)if(unit.alive && unit.player==0)ok &= unit.state!=UState::Boarding;
            failures+=!ok;std::printf("AI_TRANSPORT_DECISION %s faction=%d scenario=%d\n",ok?"PASS":"FAIL",faction,scenario);continue;
        }
        if(scenario==4)w.ents[boat].tpSick=180;
        bool tankLanded=false;
        bool boarded=false,crossed=false,landed=false,returned=false,equal=true,checkpoint=false,legal=true,captured=false,frozen=true;
        World replay;SkirmishAI replayBot;
        for(int tick=0;tick<6000;++tick) {
            if(scenario==5 && landed && !captured && w.valid(boat) && w.ents[boat].cargo.empty()) {
                captured=true;w.ents[boat].player=1;
                if(checkpoint)replay.ents[boat].player=1;
            }
            w.update();bot.update(w);
            if(checkpoint){replay.update();replayBot.update(replay);equal &= w.checksum()==replay.checksum() && bot==replayBot;}
            if(w.valid(boat) && w.ents[boat].utype==UnitType::AmphTransport) {
                const auto& ferry=w.ents[boat];boarded |= !ferry.cargo.empty();
                crossed |= !ferry.cargo.empty() && (int)ferry.x>=39 && (int)ferry.y>=39;
                returned |= landed && ferry.cargo.empty() && (int)ferry.x<21 && (int)ferry.y<27;
                legal &= !w.bldBlocked((int)ferry.x,(int)ferry.y);
                if(scenario==4 && ferry.tpSick>0)frozen &= ferry.cargo.empty() && ferry.state==UState::Idle;
            }
            for(const auto& unit:w.ents)if(unit.alive && !unit.isBuilding && unit.player==0 && (unit.utype==troop || unit.utype==tank)) {
                landed |= crossed && unit.x>=39 && unit.y>=39;
                tankLanded |= crossed && unit.utype==tank && unit.x>=39 && unit.y>=39;
                legal &= !w.bldBlocked((int)unit.x,(int)unit.y) && w.map.passable((int)unit.x,(int)unit.y);
            }
            if(!checkpoint && boarded) {
                FILE* file=tmpfile();equal &= file && w.saveGame(file);
                if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}
                replayBot=bot;checkpoint=equal;
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
            if(scenario==5?(captured && w.ents[enemy].hp<10000):(returned && w.combatDamage[0]>0 && (scenario<7 || tankLanded)))break;
        }
        bool ok=boarded && crossed && landed && (scenario==5?(captured && w.ents[enemy].hp<10000):returned)
            && equal && checkpoint && legal && frozen && w.combatDamage[0]>0 && (scenario<7 || tankLanded);
        if(!ok)for(size_t id=0;id<w.ents.size();++id) {
            const auto& unit=w.ents[id];if(unit.alive && !unit.isBuilding && unit.player==0 && unit.utype==troop)
                std::printf("AI_TRANSPORT_TROOP faction=%d id=%zu pos=%.1f,%.1f state=%d target=%d path=%d/%zu rejection=%d weapon=%d\n",faction,id,unit.x,unit.y,(int)unit.state,unit.target,unit.pathIdx,unit.path.size(),(int)w.attackRejection((EID)id,enemy),unitDef(unit.utype).weapon.damage);
        }
        failures+=!ok;
        std::printf("AI_TRANSPORT %s faction=%d scenario=%d boarded=%d crossed=%d landed=%d returned=%d replay=%d legal=%d damage=%llu boat_state=%d cargo=%zu pos=%.1f,%.1f\n",ok?"PASS":"FAIL",faction,scenario,boarded,crossed,landed,returned,equal&&checkpoint,legal,(unsigned long long)w.combatDamage[0],w.valid(boat)?(int)w.ents[boat].state:-1,w.valid(boat)?w.ents[boat].cargo.size():0,w.ents[boat].x,w.ents[boat].y);
    }
    return failures;
}
