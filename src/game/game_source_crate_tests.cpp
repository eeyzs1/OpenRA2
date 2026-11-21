#include "game_regression.h"
#include "game/script.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cmath>

int GameRegression::sourceCrateRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("SOURCE_CRATE %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto fresh=[&](World& w,const char* edition,SkirmishMode mode){
        w.skirmishMode=mode;const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        if(!w.init(64,64,2471,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile.c_str()))return false;
        w.ents.clear();w.freeList.clear();w.crates.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(auto& c:w.map.cells){c.height=0;c.ore=c.oreMax=0;c.blocked=false;c.overlay=Overlay::None;c.terrain=Terrain::Clear;}
        for(auto& p:w.players){p.isAI=false;p.money=0;p.defeated=false;}w.cratesEnabled=false;w.shortGame=false;
        w.rebuildUnitOcc();w.rebuildAirOcc();w.spawnBuilding(1,BldType::ConYard,38,38,true);
        return true;
    };
    const auto collect=[&](World& w,int original,int prototype,EID& collector,UnitType unit=UnitType::Engineer){
        collector=w.spawnUnit(0,unit,10.5f,10.5f);if(!w.valid(collector))return false;
        World::Crate c;c.x=11;c.y=10;c.kind=w.nativeCrateRules.available?NATIVE_CRATE_BASE+original:prototype;w.crates.push_back(c);
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={collector};move.x=11.5f;move.y=10.5f;w.applyCmd(0,move);
        for(int i=0;i<180 && !w.crates.empty();++i)w.update();
        return w.crates.empty() && w.valid(collector);
    };
    for(const char* edition:{"ra2","yr"}) {
        World normal;const bool loaded=fresh(normal,edition,SkirmishMode::Battle);
        std::printf("SOURCE_CRATE CASE edition=%s\n",edition);
        check(loaded && normal.nativeCrateRules.available && normal.nativeCrateRules.data[0]==2000,
              "source openings import the original crate rules and Money data");
        bool flags=loaded;for(const char* id:{"ZEP","DEST","SHAD","DTRUCK"}){
            const int type=normal.nativeProductionCode(id)-NATIVE_PRODUCTION_BASE;
            flags &= type>=0 && !normal.nativeTypes[type]->production.crateGoodie;
        }
        check(flags,"missing CrateGoodie defaults to the original false value for actual represented vehicles");
        for(auto mode:{SkirmishMode::Battle,SkirmishMode::MeatGrinder,SkirmishMode::NavalWar}) {
            World base;if(!fresh(base,edition,mode)){check(false,"source crate mode fixture opens");continue;}
            // Independent lists from the archived raw VehicleTypes/CrateGoodie
            // corpus, including the original modes' intentional exceptions.
            std::set<std::string> eligible={"AMCV","HARV","SMCV","TNKD","TTNK","CMIN"};
            if(mode!=SkirmishMode::NavalWar)eligible.insert({"APOC","V3","SREF","MGTK"});
            if(std::string(edition)=="yr"){eligible.insert({"PCV","SMIN","BFRT"});}
            bool correct=true;std::set<std::string> seen;
            for(int sample=0;sample<128;++sample){
                World w=base;w.map.bldOccRef=&w.bldOcc;w.rng=Rng(7919ull*(sample+1));EID collector=INVALID_EID;
                const size_t before=w.ents.size();const bool picked=collect(w,1,6,collector);
                correct &= picked && w.ents.size()==before+2;
                if(w.ents.size()==before+2){const auto& award=w.ents.back();const auto& def=award.nativeIdentity.definition;
                    correct &= award.alive && award.player==0 && def && eligible.count(def->id);if(def)seen.insert(def->id);}
            }
            std::printf("SOURCE_CRATE UNIT edition=%s mode=%d seen=",edition,(int)mode);for(const auto& id:seen)std::printf("%s,",id.c_str());std::printf("\n");
            check(correct,"actual movement collects only original eligible unit rewards without production prerequisites");
            check(seen==eligible,"the real random unit crate can award the complete represented original pool across factions");
        }
        bool money=true;std::set<int> amounts;
        for(int sample=0;sample<16;++sample){World w=normal;w.map.bldOccRef=&w.bldOcc;w.rng=Rng(3571ull*(sample+1));EID collector=INVALID_EID;
            const bool picked=collect(w,0,0,collector);const int amount=(int)w.players[0].money;money &= picked && amount>=2000 && amount<=2900;amounts.insert(amount);}
        check(money && amounts.size()>1,"real random Money crates draw original Data through Data+900 instead of prototype fixed income");
        const std::array<int,19> originalShares={20,20,10,0,0,0,0,0,10,10,10,10,0,0,20,0,0,0,0};
        check(normal.nativeCrateRules.shares==originalShares && !normal.nativeCrateRules.water[1] && normal.nativeCrateRules.water[0],
              "all nineteen original shares and the water eligibility flags are imported independently");
        {
            World w=normal;w.map.bldOccRef=&w.bldOcc;w.spawnBuilding(0,BldType::ConYard,3,3,true);EID collector=INVALID_EID;
            const bool picked=collect(w,1,6,collector);const auto& award=w.ents.back();
            check(picked && award.nativeIdentity.definition && award.nativeIdentity.definition->id=="CMIN",
                  "a real unit crate prioritizes the country harvester when a construction yard has no miner");
            World rescue=normal;rescue.map.bldOccRef=&rescue.bldOcc;rescue.players[0].money=2000;
            const bool rescued=collect(rescue,0,0,collector);
            check(rescued && rescue.ents.back().nativeIdentity.definition && rescue.ents.back().nativeIdentity.definition->id=="AMCV" && rescue.players[0].money==2000,
                  "a baseless house with more than 1500 credits receives its own MCV instead of the drawn money reward");
            World water=normal;water.map.bldOccRef=&water.bldOcc;const auto ship=water.spawnUnit(0,UnitType::Destroyer,11.5f,10.5f);
            water.map.at(11,10).terrain=Terrain::Water;World::Crate c;c.x=11;c.y=10;c.kind=NATIVE_CRATE_BASE+1;
            const size_t before=water.ents.size();const bool paid=water.valid(ship) && water.pickupNativeCrate(water.ents[ship],c);
            check(paid && water.ents.size()==before && water.players[0].money>=2000 && water.players[0].money<=2900,
                  "an original non-water Unit reward becomes Money when collected by a ship");
        }
        for(int kind:{9,10,11,14}){
            World w=normal;w.map.bldOccRef=&w.bldOcc;const auto nearby=w.spawnUnit(1,UnitType::GI,12.5f,10.5f);
            const auto far=w.spawnUnit(0,UnitType::GI,22.5f,10.5f);EID collector=INVALID_EID;
            const bool picked=collect(w,kind,kind==14?2:kind==9?8:kind==10?9:7,collector,UnitType::GI);
            bool effect=picked && w.valid(collector) && w.valid(nearby) && w.valid(far);
            if(effect){if(kind==14)effect=w.ents[collector].vetRank==1 && w.ents[nearby].vetRank==1 && w.ents[far].vetRank==0;
                else effect=w.nativeCrateMultiplier(w.ents[collector].nativeIdentity,kind)==w.nativeCrateRules.data[kind]
                    && w.nativeCrateMultiplier(w.ents[nearby].nativeIdentity,kind)==w.nativeCrateRules.data[kind]
                    && w.nativeCrateMultiplier(w.ents[far].nativeIdentity,kind)==1;}
            check(effect,"actual fixed-kind pickup applies the source permanent upgrade or veteran reward inside its radius, including nearby enemies");
            if(kind==14 && picked)check(w.ents[collector].veterancyValue==(int)std::ceil(unitDef(w.ents[collector]).cost*g_gameRules.veteranRatio),
                "the Veteran reward sets the promoted experience boundary so later kills require only the next original rank's value");
            if(kind!=14 && picked){World::Crate c;c.x=11;c.y=10;c.kind=NATIVE_CRATE_BASE+kind;const auto before=w.players[0].money;
                const bool again=w.pickupNativeCrate(w.ents[collector],c);
                check(again && w.players[0].money>=before+2000 && w.players[0].money<=before+2900
                    && w.nativeCrateMultiplier(w.ents[collector].nativeIdentity,kind)==w.nativeCrateRules.data[kind],
                      "an already upgraded collector receives original Money instead of stacking a second permanent multiplier");}
        }
        {
            World w=normal;w.map.bldOccRef=&w.bldOcc;EID collector=INVALID_EID;
            bool promoted=collect(w,14,2,collector,UnitType::GI);
            const int base=promoted?(int)std::ceil(unitDef(w.ents[collector]).cost*g_gameRules.veteranRatio):0;
            const auto first=w.spawnUnit(1,UnitType::Engineer,14.5f,10.5f);const int firstValue=w.valid(first)?unitDef(w.ents[first]).cost:0;
            World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={collector};attack.a=first;w.applyCmd(0,attack);
            for(int frame=0;frame<1800 && w.valid(first) && w.valid(collector);++frame)w.update();
            promoted &= !w.valid(first) && w.valid(collector) && w.ents[collector].kills==1
                && w.ents[collector].veterancyValue==base+firstValue && w.ents[collector].vetRank==1;
            check(promoted,"after a real Veteran pickup, the first actual engineer kill retains veteran rank and adds its original value");
            const auto second=w.spawnUnit(1,UnitType::Engineer,15.5f,10.5f);const int secondValue=w.valid(second)?unitDef(w.ents[second]).cost:0;
            attack.a=second;w.applyCmd(0,attack);
            const auto worldPath=(std::filesystem::path(outputDir)/(std::string(edition)+"-veteran-world56.sav")).string();
            FILE* file=std::fopen(worldPath.c_str(),"wb");bool exact=file && w.saveGame(file);if(file)std::fclose(file);World copy;
            file=std::fopen(worldPath.c_str(),"rb");exact &= file && copy.loadGame(file);if(file)std::fclose(file);
            Game saved;saved.world=w;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.cfgRules=std::string(edition)=="ra2"?RulesChoice::RA2:RulesChoice::YR;
            saved.cfgAI=1;saved.cfgCountry=(int)Country::COUNT;saved.cfgMapSize=64;saved.cfgMapType=0;saved.cfgMoney=10000;saved.phase=Phase::InGame;
            const auto appPath=(std::filesystem::path(outputDir)/(std::string(edition)+"-veteran-app11.sav")).string();Game resumed;
            exact &= saved.saveGameFile(appPath.c_str()) && resumed.loadGameFile(appPath.c_str())
                && copy.checksum()==w.checksum() && resumed.world.checksum()==w.checksum();
            for(int frame=0;frame<1800 && exact && w.valid(second) && w.valid(collector);++frame){
                w.update();copy.update();resumed.world.update();exact &= w.checksum()==copy.checksum() && w.checksum()==resumed.world.checksum();}
            check(promoted && exact && !w.valid(second) && w.valid(collector) && w.ents[collector].kills==2
                && w.ents[collector].veterancyValue==base+firstValue+secondValue && w.ents[collector].vetRank==2,
                  "real combat reaches elite after only the remaining rank's value, identically across actual World55 and App11 continuation");
            World partial=normal;partial.map.bldOccRef=&partial.bldOcc;const auto actor=partial.spawnUnit(0,UnitType::GI,11.5f,10.5f);
            partial.ents[actor].veterancyValue=unitDef(partial.ents[actor]).cost;World::Crate c;c.x=11;c.y=10;c.kind=NATIVE_CRATE_BASE+14;
            const bool upgraded=partial.pickupNativeCrate(partial.ents[actor],c);
            check(upgraded && partial.ents[actor].vetRank==1 && partial.ents[actor].veterancyValue==base,
                  "a partially trained rookie is set to the original veteran boundary instead of retaining an extra fractional rank");
        }
        {
            World w=normal;w.map.bldOccRef=&w.bldOcc;const auto yard=w.spawnBuilding(0,BldType::ConYard,3,3,true);w.ents[yard].hp=1;
            const auto unit=w.spawnUnit(0,UnitType::GI,14.5f,10.5f);w.ents[unit].hp=1;const auto enemy=w.spawnUnit(1,UnitType::GI,32.5f,10.5f);w.ents[enemy].hp=1;
            EID collector=INVALID_EID;const bool healed=collect(w,2,1,collector);
            check(healed && w.ents[yard].hp==bldDef(w.ents[yard]).hp && w.ents[unit].hp==unitDef(w.ents[unit]).hp && w.ents[enemy].hp==1,
                  "HealBase restores owned units and structures without healing the opponent");
            World reveal=normal;reveal.map.bldOccRef=&reveal.bldOcc;for(auto& fog:reveal.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
            const bool shown=collect(reveal,8,10,collector);
            check(shown && std::count(reveal.map.fog[0].begin(),reveal.map.fog[0].end(),FOG_UNSEEN)==0
                && std::count(reveal.map.fog[1].begin(),reveal.map.fog[1].end(),FOG_UNSEEN)>0,
                  "Reveal removes only the collecting house's map shroud");
        }
        {
            World w=normal;w.map.bldOccRef=&w.bldOcc;w.spawnUnit(0,UnitType::GI,10.5f,10.5f);w.cratesEnabled=true;w.update();
            bool initial=w.crates.size()==2;for(const auto& c:w.crates)initial &= c.kind==NATIVE_CRATE_BASE+19 && c.remainingTicks>=2700 && c.remainingTicks<=10800;
            check(initial,"the first simulation update creates the original player-count crate population with randomized half-to-double Regen lifetimes");
            if(!w.crates.empty())w.crates[0].remainingTicks=1;
            const auto worldPath=(std::filesystem::path(outputDir)/(std::string(edition)+"-pending-world56.sav")).string();FILE* file=std::fopen(worldPath.c_str(),"wb");
            bool exact=file && w.saveGame(file);if(file)std::fclose(file);World copy;
            file=std::fopen(worldPath.c_str(),"rb");exact &= file && copy.loadGame(file);if(file)std::fclose(file);
            World original=w;original.map.bldOccRef=&original.bldOcc;exact &= copy.checksum()==original.checksum();
            for(int frame=0;frame<120 && exact;++frame){original.update();copy.update();exact &= copy.checksum()==original.checksum();}
            check(exact && copy.crates.size()==2 && std::none_of(copy.crates.begin(),copy.crates.end(),[](const auto& c){return c.remainingTicks<=1;}),
                  "a real World55 snapshot resumes a pending crate expiry and replacement with exact RNG and remaining lifetimes");
            Game saved;saved.world=w;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.cfgRules=std::string(edition)=="ra2"?RulesChoice::RA2:RulesChoice::YR;
            saved.cfgAI=1;saved.cfgCountry=(int)Country::COUNT;saved.cfgMapSize=64;saved.cfgMapType=0;saved.cfgMoney=10000;saved.phase=Phase::InGame;
            const auto appPath=(std::filesystem::path(outputDir)/(std::string(edition)+"-pending-app11.sav")).string();Game resumed;
            exact=saved.saveGameFile(appPath.c_str()) && resumed.loadGameFile(appPath.c_str()) && resumed.world.checksum()==w.checksum();
            for(int frame=0;frame<120 && exact;++frame){saved.world.update();resumed.world.update();exact &= saved.world.checksum()==resumed.world.checksum();}
            check(exact,"an actual App11 checkpoint restores the source rules, pending crate expiry and continuation");
            std::ifstream input(worldPath,std::ios::binary);const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
            const auto range=locateWorldSaveSection(bytes,WorldSaveSection::CrateLifetimes);bool rejected=range && range->length>=16;
            if(range)for(int corruption=0;corruption<6;++corruption){auto bad=bytes;uint32_t value=0;
                if(corruption<4){value=corruption==0?2:corruption==1?0xffffffffu:corruption==2?0xffffffffu:0;
                    const size_t at=corruption==0?range->offset:corruption==1?range->offset+4:corruption==2?range->offset+8:range->footer;memcpy(bad.data()+at,&value,4);}
                else if(corruption==4)bad.pop_back();else bad.push_back(0);
                FILE* f=tmpfile();World invalid;bool accepted=false;if(f){fwrite(bad.data(),1,bad.size(),f);rewind(f);accepted=invalid.loadGame(f);fclose(f);}rejected &= !accepted;
            }
            check(rejected,"invalid crate timer version/count/value/footer and truncated or trailing saves are rejected");
        }
        {
            const auto old=(std::filesystem::path("review_out/project-review-20261115/source-crates-baseline/build/source-match-audit")/(std::string(edition)+"-0")).string();
            FILE* file=std::fopen((old+"/start-world54.sav").c_str(),"rb");World legacy;const bool loaded=file && legacy.loadGame(file);if(file)std::fclose(file);
            check(loaded && !legacy.nativeCrateRules.available,"the archived actual World54 opening preserves its embedded Scene25 prototype-crate boundary");
            Game app;const bool restored=app.loadGameFile((old+"/tick-300-app11.sav").c_str());
            check(restored && !app.world.nativeCrateRules.available && !app.ais.empty(),"the archived actual App11/World54 source checkpoint restores its old rules and active AI");
        }
    }
    return failures;
}
