#include "game_regression.h"
#include "game/script.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
Ini budgetCatalog(const Ini& original,int count,int version) {
    Ini ini;ini.sections.push_back({"NativeWeapons",{{"Version",std::to_string(version)},{"Weapons",std::to_string(count)},{"Projectiles",std::to_string(count)},{"Warheads","1"}}});
    ini.sections.push_back(*original.find("NativeWarhead.0"));
    const auto set=[](Ini::Section& row,const char* key,std::string value){
        for(auto& p:row.kv)if(p.first==key){p.second=std::move(value);return;}row.kv.emplace_back(key,std::move(value));
    };
    for(int i=0;i<count;++i) {
        auto weapon=*original.find("NativeWeapon.0");weapon.name="NativeWeapon."+std::to_string(i);
        set(weapon,"Id","Weapon"+std::to_string(i));set(weapon,"Projectile",std::to_string(i));set(weapon,"Warhead","0");
        ini.sections.push_back(std::move(weapon));
        auto projectile=*original.find("NativeProjectile.0");projectile.name="NativeProjectile."+std::to_string(i);
        set(projectile,"Id","Projectile"+std::to_string(i));set(projectile,"ShrapnelWeapon",i+1<count?std::to_string(i+1):"-1");
        set(projectile,"ShrapnelCount",i+1<count?"-1":"0");set(projectile,"AirburstWeapon","-1");set(projectile,"Airburst","0");
        ini.sections.push_back(std::move(projectile));
    }
    return ini;
}
}

int GameRegression::nativeShrapnelRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;g_script.shutdown();
    const auto check=[&](bool ok,const char* label) {
        failures+=!ok;std::printf("NATIVE_SHRAPNEL %s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);
    };
    check(IsWindowHidden(),"the native shrapnel helper starts with a hidden window");
    for(const char* edition:{"ra2","yr"}) {
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;
        selectRulesChoice(edition[0]=='r'?RulesChoice::RA2:RulesChoice::YR);
        cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        auto& w=world;const std::string profile=std::string("assets/rules/profiles/")+edition+".ini";
        const bool loaded=w.init(64,64,1943,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile.c_str(),{Country::America,Country::Russia});
        check(loaded && w.nativeCellOrderEnabled(),"the current edition enables the Scene33 cell-object order contract");if(!loaded)continue;
        Ini original;std::shared_ptr<const NativeWeaponCatalog> parsed;
        const bool source=original.load(profile.c_str());
        check(source && parseNativeWeaponCatalog(budgetCatalog(original,10,1),true,parsed)
            && parseNativeWeaponCatalog(budgetCatalog(original,8,2),true,parsed),
            "legacy negative-count graphs and bounded ownerless three-way expansion remain valid");
        check(source && !parseNativeWeaponCatalog(budgetCatalog(original,9,2),true,parsed),
            "ownerless negative-count expansion exceeding 4096 nodes rejects before simulation");
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& player:w.players){player.isAI=false;player.defeated=false;}
        w.cratesEnabled=false;w.crates.clear();w.effects.clear();w.rebuildUnitOcc();
        w.spawnBuilding(0,BldType::ConYard,2,2,true);w.spawnBuilding(1,BldType::ConYard,55,55,true);
        const auto base=w;
        const auto reset=[&]{w=base;w.map.bldOccRef=&w.bldOcc;};
        const auto target=[&](int player,float x,float y,UnitType type=UnitType::Rhino) {
            const EID id=w.spawnUnit(player,type,x,y);if(w.valid(id))w.ents[id].tpSick=500;return id;
        };
        const auto prism=[&]{return w.spawnUnit(0,UnitType::PrismTank,11.5f,16.5f);};
        const auto fire=[&](EID a,EID b) {
            if(!w.valid(a) || !w.valid(b))return false;
            const int hp=w.ents[b].hp;if(w.orderAttack({a},b,false).accepted!=1)return false;
            for(int tick=0;tick<250 && w.valid(b) && w.ents[b].hp==hp;++tick)w.update();
            return !w.valid(b) || w.ents[b].hp<hp;
        };
        const auto path=[&](const std::string& name){return (std::filesystem::path(outputDir)/(std::string(edition)+"-"+name)).string();};
        const auto snapshot=[&](const std::string& name,int ticks) {
            const auto raw=path(name+"-world59.sav"),app=path(name+"-app11.sav");
            FILE* f=std::fopen(raw.c_str(),"wb");bool exact=f && w.saveGame(f);if(f)std::fclose(f);
            World copy;f=std::fopen(raw.c_str(),"rb");exact &= f && copy.loadGame(f);if(f)std::fclose(f);
            auto replay=std::make_unique<Game>();exact &= saveGameFile(app.c_str()) && replay->loadGameFile(app.c_str());
            const auto before=w;
            exact &= w.checksum()==copy.checksum() && w.checksum()==replay->world.checksum();
            for(int i=0;i<ticks && exact;++i) {
                w.update();copy.update();replay->world.update();
                exact &= w.checksum()==copy.checksum() && w.checksum()==replay->world.checksum();
            }
            check(exact,"World59 and App11 restore real source state and replay every tick exactly");
            w=before;w.map.bldOccRef=&w.bldOcc;return exact;
        };
        {
            const auto a=prism(),b=target(1,16.5f,16.5f);std::vector<EID> ids;bool legal=w.valid(a)&&w.valid(b);
            for(auto delta:{Vec2i{1,-1},Vec2i{0,-1},Vec2i{-1,-1},Vec2i{-1,0},Vec2i{1,0},Vec2i{-1,1},Vec2i{0,1},Vec2i{1,1}}) {
                const auto id=target(1,16.5f+delta.x,16.5f+delta.y);ids.push_back(id);legal &= w.valid(id);
            }
            check(legal,"eight ring-one recipients spawn with their normal source hit points");
            bool ordered=legal && fire(a,b);
            for(int i=0;i<(int)ids.size();++i)ordered &= w.ents[ids[i]].hp==(i<5?385:400);
            check(ordered,"five fragments select NE,N,NW,W,E in original table order");
            snapshot("ring-one",120);
        }
        {
            reset();const auto a=prism(),b=target(1,16.5f,16.5f),edge=target(1,19.5f,17.5f);
            bool hit=fire(a,b) && w.valid(edge) && w.ents[edge].hp==385;
            check(hit,"original radius three includes offset 3,1 beyond Euclidean radius three");
        }
        {
            reset();const auto a=prism(),b=target(1,16.5f,16.5f),yard=w.spawnBuilding(1,BldType::ConYard,18,15,true);
            const int hp=w.valid(yard)?w.ents[yard].hp:0;
            check(fire(a,b) && w.valid(yard) && w.ents[yard].hp==hp-300,
                "five occupied foundation cells select and damage the same building five times");
        }
        {
            reset();const auto a=prism(),b=target(1,16.5f,16.5f);bool rays=fire(a,b);int beams=0;
            for(const auto& e:w.effects)if(e.kind==3 && e.aux==2) {
                ++beams;if(e.x==16.5f && e.y==16.5f)rays &= e.x2>=14.5f && e.x2<=18.5f && e.y2>=14.5f && e.y2<=18.5f;
            }
            check(rays && beams==6,"unfilled recipients still create five bounded random ground rays");
            snapshot("ground-rays",120);
        }
        {
            reset();const auto a=prism(),b=target(1,16.9f,16.1f);
            const auto hidden=target(1,17.5f,15.5f,UnitType::Conscript),head=target(0,17.5f,15.5f,UnitType::GI);
            bool legal=w.valid(hidden) && w.valid(head);
            for(auto delta:{Vec2i{0,-1},Vec2i{-1,-1},Vec2i{-1,0},Vec2i{1,0},Vec2i{-1,1}})legal &= w.valid(target(1,16.5f+delta.x,16.5f+delta.y));
            check(legal && w.nativeCellFirstObject(17,15)==head,"a later allied entrant is the first object ahead of an enemy in the same cell");
            check(legal && fire(a,b) && w.ents[hidden].hp==unitDef(w.ents[hidden]).hp,
                "an allied first object blocks the enemy behind it without consuming a fragment");
        }
        {
            reset();const auto aircraft=w.spawnUnit(0,UnitType::Intruder,11.5f,24.5f),enemy=target(1,16.5f,24.5f);
            bool parked=w.valid(aircraft) && w.valid(enemy);
            if(parked) {
                w.ents[aircraft].state=UState::Landed;w.unitOccSync(aircraft);w.airOccSync(aircraft);
                FILE* file=std::tmpfile();parked=file && w.saveGame(file);if(file)std::fclose(file);
                parked &= !w.ents[aircraft].airborne() && w.ents[aircraft].nativeCellOrder>0;
            }
            check(parked,"a parked source aircraft has a valid savable ground-cell entry");
            const bool ordered=parked && w.orderAttack({aircraft},enemy,false).accepted==1;
            check(ordered && w.ents[aircraft].airborne() && !w.ents[aircraft].nativeCellOrder
                && w.ents[aircraft].occCell<0 && w.ents[aircraft].airBucket>=0 && w.validateNativeCellOrder(),
                "an attack order commits takeoff occupancy immediately before the next simulation tick");
            snapshot("takeoff-order",120);
        }
        {
            reset();for(int y=23;y<29;++y)for(int x=23;x<29;++x)w.map.at(x,y).terrain=Terrain::Water;
            const auto squid=w.spawnUnit(1,UnitType::Squid,25.5f,25.5f),host=w.spawnUnit(0,UnitType::Destroyer,26.5f,25.5f);
            bool attached=w.valid(squid) && w.valid(host);
            if(attached) {
                // The state written by the stock squid-grab update path.
                w.ents[squid].parasiting=true;w.ents[squid].parasiteHost=host;w.ents[host].parasite=squid;
                w.unitOccSync(squid);w.airOccSync(squid);
                FILE* file=std::tmpfile();attached=file && w.saveGame(file);if(file)std::fclose(file);
            }
            check(attached,"a grabbed source squid has a valid savable off-cell attachment state");
            if(attached)w.orderStop({squid});
            check(attached && !w.ents[squid].parasiting && w.ents[host].parasite==INVALID_EID
                && w.ents[squid].nativeCellOrder>0 && w.ents[squid].occCell>=0 && w.validateNativeCellOrder(),
                "Stop commits a released squid's ground-cell entry before the next simulation tick");
            snapshot("stopped-squid",120);
        }
        {
            reset();const auto a=target(1,17.5f,15.5f,UnitType::Conscript),b=target(0,17.5f,15.5f,UnitType::GI);
            const EID low=std::min(a,b),high=std::max(a,b);bool legal=w.valid(a) && w.valid(b);
            if(!legal){check(false,"two distinct entry-order controls spawn");continue;}
            if(legal) {
                for(auto id:{low,high}){w.ents[id].x=20.5f;w.unitOccSync(id);}
                for(auto id:{high,low}){w.ents[id].x=17.5f;w.unitOccSync(id);}
            }
            const auto hash=w.checksum();const auto serial=w.nativeCellSerial;
            w.rebuildUnitOcc();w.rebuildUnitOcc();
            check(legal && w.nativeCellFirstObject(17,15)==low && w.checksum()==hash && w.nativeCellSerial==serial,
                "rebuilding the derived occupancy index preserves entry history that differs from EID order");
            snapshot("entry-order",120);
            if(legal) {
                w.ents[high].x=20.5f;w.unitOccSync(high);w.ents[high].x=17.5f;w.unitOccSync(high);
            }
            check(legal && w.nativeCellFirstObject(17,15)==high && w.checksum()!=hash && w.validateNativeCellOrder(),
                "leaving and reentering a cell moves the entrant to the head and changes the simulation checksum");
            snapshot("reentered",120);
            if(legal) {
                w.ents[high].navLayer=1;w.unitOccSync(high);
                check(w.nativeCellFirstObject(17,15)==low,"bridge AltObject occupants do not hide the ground FirstObject");
                w.ents[high].navLayer=0;w.unitOccSync(high);w.kill(high,false);
                check(w.nativeCellFirstObject(17,15)==low && w.validateNativeCellOrder(),"retiring the newest object exposes the previous entrant without a stale identity");
            }
            // App loading must reject corruption without replacing this world.
            std::ifstream in(path("entry-order-app11.sav"),std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
            const auto start=embeddedWorldSaveOffset(bytes);
            const size_t begin=start.value_or(bytes.size());
            const auto section=start?locateWorldSaveSection(std::span(bytes).subspan(begin),WorldSaveSection::NativeCellOrder):std::nullopt;
            bool rejected=section.has_value();const auto stable=w.checksum();
            std::vector<std::vector<uint8_t>> corrupt;
            if(section) {
                const size_t offset=begin+section->offset;
                for(size_t field:{size_t{0},size_t{4},size_t{8},size_t{17}}){auto copy=bytes;copy[offset+field]=255;corrupt.push_back(std::move(copy));}
                auto zero=bytes;std::fill(zero.begin()+offset+9,zero.begin()+offset+17,0);corrupt.push_back(std::move(zero));
                auto duplicate=bytes;std::memcpy(duplicate.data()+offset+21+high*8,duplicate.data()+offset+21+low*8,8);corrupt.push_back(std::move(duplicate));
                auto missing=bytes;missing.resize(offset);missing[begin+7]='7';corrupt.push_back(std::move(missing));
            }
            int n=0;for(const auto& data:corrupt) {
                const auto file=path("corrupt-"+std::to_string(n++)+".sav");FILE* f=std::fopen(file.c_str(),"wb");
                const bool wrote=f && std::fwrite(data.data(),1,data.size(),f)==data.size();if(f)std::fclose(f);
                rejected &= wrote && !loadGameFile(file.c_str()) && w.checksum()==stable;
            }
            check(rejected && corrupt.size()==7,"bad headers, flags, counts, serials, duplicate entries and missing World58 state reject atomically");
        }
    }
    return failures;
}
