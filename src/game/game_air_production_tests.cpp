#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::airProductionRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("AIR_PRODUCTION %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto resume=[&](World& w,const std::string& name,int ticks){
        const auto path=(std::filesystem::path(outputDir)/name).string();FILE* f=std::fopen(path.c_str(),"wb");bool ok=f && w.saveGame(f);if(f)std::fclose(f);
        World restored;f=std::fopen(path.c_str(),"rb");if(f){ok &= restored.loadGame(f);std::fclose(f);}else ok=false;
        ok &= w.checksum()==restored.checksum();
        for(int i=0;i<ticks && ok;++i){w.update();restored.update();ok=w.checksum()==restored.checksum();}return ok;
    };
    for(const char* edition:{"fusion","ra2","yr"}) {
        const bool source=std::string(edition)!="fusion";
        const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        World w;bool ok=w.init(64,64,1911,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,source?profile.c_str():nullptr);
        std::printf("AIR_PRODUCTION rules=%s\n",edition);
        check(ok,"the selected rules initialize for paid aircraft production");if(!ok)continue;
        w.ents.clear();w.freeList.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(auto& c:w.map.cells){c.height=0;c.terrain=Terrain::Clear;c.ore=c.oreMax=0;c.blocked=false;c.overlay=Overlay::None;}
        for(auto& p:w.players){p.isAI=false;p.money=100000;p.defeated=false;}w.cratesEnabled=false;w.rebuildUnitOcc();w.rebuildAirOcc();
        w.spawnBuilding(0,BldType::ConYard,2,2,true);w.spawnBuilding(0,BldType::Barracks,10,2,true);
        w.spawnBuilding(0,BldType::OreRefinery,10,8,true);w.spawnBuilding(0,BldType::WarFactory,20,4,true);
        for(int i=0;i<3;++i)w.spawnBuilding(0,BldType::PowerPlant,2+i*4,16,true);
        const EID base=w.spawnBuilding(0,BldType::AirForceCmd,30,4,true);
        const auto aircraft=[&](){std::vector<EID> ids;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && !w.ents[id].isBuilding && w.ents[id].player==0 && w.ents[id].utype==UnitType::Intruder)ids.push_back(id);return ids;};
        const auto start=[&](UnitType type){World::Cmd cmd{};cmd.type=World::Cmd::StartUnitProd;cmd.a=(int)type;w.applyCmd(0,cmd);};
        const auto paid=[&](UnitType type){const auto before=w.producedUnits[0];start(type);for(int i=0;i<16000 && before==w.producedUnits[0];++i)w.update();return w.producedUnits[0]==before+1;};
        const auto initialMoney=w.players[0].money;bool births=true;
        for(int i=0;i<4;++i){ok &= paid(UnitType::Intruder);const auto ids=aircraft();if(ids.size()!=(size_t)i+1){ok=false;break;}
            const auto& plane=w.ents[ids.back()];births &= plane.x==plane.px && plane.y==plane.py && plane.state==UState::Landed && plane.airbase==base;
            // Deliberately interleave ordinary entities: entity IDs are not dock numbers.
            if(i==0)for(int j=0;j<3;++j)w.spawnUnit(0,UnitType::GI,45.5f+j,45.5f);
        }
        auto ids=aircraft();bool distinct=ids.size()==4;for(size_t i=0;i<ids.size();++i)for(size_t j=0;j<i;++j)distinct &= distf(w.ents[ids[i]].x,w.ents[ids[i]].y,w.ents[ids[j]].x,w.ents[ids[j]].y)>.5f;
        check(ok && births,"paid aircraft are born on their actual pad with matching interpolation coordinates");
        check(ok && distinct,"four paid aircraft occupy four distinct pads despite interleaved entity allocation");
        const int code=w.nativeProductionCode("ORCA");const int cost=code>=0?w.nativeTypes[code-NATIVE_PRODUCTION_BASE]->unit.cost:unitDef(UnitType::Intruder).cost;
        check(ok && cost>0 && initialMoney-w.players[0].money==4*cost,"four aircraft deduct exactly four rules prices through ordinary production commands");
        bool away=false;if(!ids.empty()){w.orderMove({ids.front()},24.5f,32.5f,false);for(int i=0;i<90;++i)w.update();away=w.ents[ids.front()].airborne();}
        check(away,"a normal Move command sends one paid aircraft away while its airfield retains the dock claim");
        start(UnitType::Intruder);const int category=code>=0?w.productionCategory(true,code):2;
        for(int i=0;i<16000 && w.players[0].unitProd[category].active && !w.players[0].unitProd[category].ready;++i)w.update();
        const auto& pending=w.players[0].unitProd[category];
        check(aircraft().size()==4 && pending.active && pending.ready && pending.paid==cost && initialMoney-w.players[0].money==5*cost,
              "a full airfield retains its fully paid fifth aircraft instead of stacking it on an occupied pad");
        check(resume(w,std::string(edition)+"-full-world56.sav",80),"a blocked paid aircraft and existing dock positions resume identically after saving");
        const EID second=w.spawnBuilding(0,BldType::AirForceCmd,36,4,true);w.update();ids=aircraft();
        bool overflow=ids.size()==5;int atSecond=0;for(EID id:ids)atSecond+=w.ents[id].airbase==second;
        check(overflow && atSecond==1 && !w.players[0].unitProd[category].active && initialMoney-w.players[0].money==5*cost,
              "adding a second airfield releases the already paid aircraft exactly once without charging again");
        check(resume(w,std::string(edition)+"-two-bases-world56.sav",120),"two paid airfields and their aircraft preserve deterministic continuation");
        if(!ids.empty())w.kill(ids.front(),false);ok=paid(UnitType::Intruder);ids=aircraft();
        distinct=ids.size()==5;int atFirst=0;
        for(size_t i=0;i<ids.size();++i){atFirst+=w.ents[ids[i]].airbase==base;for(size_t j=0;j<i;++j)distinct &= distf(w.ents[ids[i]].x,w.ents[ids[i]].y,w.ents[ids[j]].x,w.ents[ids[j]].y)>.5f;}
        check(ok && distinct && atFirst==4 && initialMoney-w.players[0].money==6*cost,"destroying an aircraft releases one pad and entity-slot reuse cannot displace or overlap the surviving grounded aircraft");
        EID departing=INVALID_EID;for(EID id:ids)if(w.ents[id].airbase==second)departing=id;
        const EID replacementBase=w.spawnBuilding(0,BldType::AirForceCmd,42,4,true);
        const EID enemy=w.spawnBuilding(1,BldType::PowerPlant,52,44,true);if(w.valid(enemy))w.ents[enemy].hp=100000;
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        bool fought=false,spentAmmo=false,diverted=false,snapshot=false,flightSaved=true;
        if(w.valid(departing) && w.valid(enemy)) {
            w.orderAttack({departing},enemy);
            for(int i=0;i<6000 && w.valid(departing);++i){w.update();const auto& plane=w.ents[departing];
                if(w.valid(second) && plane.airborne())w.kill(second,false);
                fought |= w.ents[enemy].hp<100000;
                spentAmmo |= plane.ammo<unitDef(plane).ammo;
                if(!snapshot && plane.state==UState::Returning){snapshot=true;flightSaved=resume(w,std::string(edition)+"-return-world56.sav",40);}
                if(fought && plane.state==UState::Landed){diverted=plane.airbase==replacementBase;break;}
            }
        }
        check(fought && spentAmmo && diverted,"after an actual attack and home-base destruction the returning aircraft skips a full airfield and lands at a free one");
        check(snapshot && flightSaved,"an attacking aircraft's return and alternate-base choice resume identically after saving");
        std::vector<EID> sortie;for(EID id:aircraft())if(w.ents[id].airbase==base && sortie.size()<2)sortie.push_back(id);
        bool spent[2]={},returned=false;if(sortie.size()==2){w.orderAttack(sortie,enemy);
            for(int i=0;i<6000;++i){w.update();bool landed=true;
                for(int j=0;j<2;++j){if(!w.valid(sortie[j])){landed=false;continue;}const auto& plane=w.ents[sortie[j]];
                    spent[j] |= plane.ammo<unitDef(plane).ammo;landed &= spent[j] && plane.state==UState::Landed && plane.airbase==base;}
                if(landed){returned=distf(w.ents[sortie[0]].x,w.ents[sortie[0]].y,w.ents[sortie[1]].x,w.ents[sortie[1]].y)>.5f;break;}
            }
        }
        check(returned,"two aircraft completing a real attack return to distinct pads while the other grounded aircraft keep their pads");
        if(!source)continue;
        // The vehicle-factory path must still produce and fly the source transport.
        const auto before=w.producedUnits[0];ok=paid(UnitType::Nighthawk);EID transport=INVALID_EID;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].utype==UnitType::Nighthawk && !w.ents[id].isBuilding)transport=id;
        check(ok && w.producedUnits[0]==before+1 && w.valid(transport) && w.ents[transport].nativeIdentity.definition->id=="SHAD",
              "ordinary paid vehicle production creates the original Nighthawk transport");
        if(w.valid(transport)){w.orderMove({transport},25.5f,25.5f,false);bool flown=false;for(int i=0;i<1000;++i){w.update();flown |= w.ents[transport].airborne();}
            check(flown && !w.ents[transport].airborne() && distf(w.ents[transport].x,w.ents[transport].y,25.5f,25.5f)<.1f,
                  "a paid Nighthawk takes off from its factory exit and lands at the commanded destination");
            ok=paid(UnitType::GI);EID passenger=INVALID_EID;
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && !w.ents[id].isBuilding && w.ents[id].utype==UnitType::GI
                && w.ents[id].nativeIdentity.object.rfind("production:",0)==0)passenger=id;
            std::string key;if(w.valid(passenger)){key=w.ents[passenger].nativeIdentity.object;w.ents[passenger].hp=61;ok &= w.orderBoard({passenger},transport).accepted==1;}else ok=false;
            for(int i=0;i<1600 && w.valid(passenger);++i)w.update();
            ok &= w.ents[transport].cargo.size()==1 && !w.valid(passenger);
            w.orderMove({transport},38.5f,28.5f,false);for(int i=0;i<150;++i)w.update();
            const auto release=w.orderUnload({transport});ok &= release.pending==1;
            check(ok && resume(w,std::string(edition)+"-paid-transport-world56.sav",50),"paid infantry board the paid transport and preserve their pending airborne unload in a save");
            for(int i=0;i<1600 && !w.ents[transport].cargo.empty();++i)w.update();bool delivered=false;
            for(const auto& e:w.ents)if(e.alive && e.nativeIdentity.object==key)delivered=e.hp==61 && distf(e.x,e.y,w.ents[transport].x,w.ents[transport].y)<3;
            check(ok && delivered && w.ents[transport].cargo.empty() && !w.ents[transport].airborne(),"the paid factory-to-barracks-to-airlift journey unloads the same damaged source infantry only after landing");
        }
    }
    return failures;
}
