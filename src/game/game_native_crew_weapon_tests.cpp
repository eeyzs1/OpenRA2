#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
std::vector<uint8_t> bytes(const World& w) {
    FILE* f=std::tmpfile();std::vector<uint8_t> result;
    if(f && w.saveGame(f) && !std::fseek(f,0,SEEK_END)) {
        const auto size=std::ftell(f);if(size>0){result.resize((size_t)size);std::rewind(f);if(std::fread(result.data(),1,result.size(),f)!=result.size())result.clear();}
    }
    if(f)std::fclose(f);return result;
}
bool load(const std::vector<uint8_t>& input,World& w) {
    FILE* f=std::tmpfile();bool ok=f && !input.empty() && std::fwrite(input.data(),1,input.size(),f)==input.size();
    if(ok){std::rewind(f);ok=w.loadGame(f);}if(f)std::fclose(f);return ok;
}
}

int GameRegression::nativeCrewWeaponRegression(const char* outputDir) {
    int failures=0;g_script.shutdown();
    const auto check=[&](bool pass,const std::string& label){failures+=!pass;std::printf("TRANSPORT_WEAPON %s %s\n",pass?"PASS":"FAIL",label.c_str());std::fflush(stdout);};
    resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;phase=Phase::InGame;
    selectRulesChoice(RulesChoice::YR);cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
    auto& w=world;const bool ready=w.init(64,64,2311,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,"assets/rules/profiles/yr.ini");
    check(ready && w.nativeCrewRules.version==2,"current YR Scene43/Crew2 profile loads");if(!ready)return failures;
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};for(auto& p:w.players){p.isAI=false;p.defeated=false;}
    w.cratesEnabled=false;w.crates.clear();w.effects.clear();w.rebuildUnitOcc();w.rebuildAirOcc();
    w.spawnBuilding(0,BldType::ConYard,2,2,true);w.spawnBuilding(1,BldType::ConYard,55,55,true);
    const auto base=w;const auto reset=[&]{w=base;w.map.bldOccRef=&w.bldOcc;};
    const auto board=[&](const char* raw,int rank) {
        const EID bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);const int type=w.nativeUnitIndex(raw);
        const EID passenger=type<0?INVALID_EID:w.spawnNativeActor(type,0,12.5f,13.5f,256,0,"transport-weapon-passenger","","Guard");
        const bool spawned=w.valid(bf) && w.valid(passenger);check(spawned,std::string(raw)+" source infantry spawned");if(!spawned)return INVALID_EID;
        w.ents[passenger].vetRank=rank;const uint64_t uid=w.ents[passenger].combatActor;
        check(w.orderBoard({passenger},bf).accepted==1,std::string(raw)+" real boarding order accepted");
        for(int i=0;i<40 && w.valid(passenger);++i)w.update();
        check(!w.valid(passenger) && w.ents[bf].cargo.size()==1 && w.ents[bf].cargo[0].combatActor==uid,std::string(raw)+" real boarding keeps UID and retires handle");
        w.ents[bf].atkCd=1000000;return bf;
    };
    const auto save=[&](const std::string& label) {
        const auto stem=(std::filesystem::path(outputDir)/("weapon-"+label)).string();const auto input=bytes(w);
        {std::ofstream output(stem+"-world60.sav",std::ios::binary);output.write((const char*)input.data(),input.size());}
        World replay;auto app=std::make_unique<Game>();const auto snapshot=w;
        bool exact=!input.empty() && load(input,replay) && bytes(replay)==input && saveGameFile((stem+"-app11.sav").c_str()) && app->loadGameFile((stem+"-app11.sav").c_str());
        exact &= w.checksum()==replay.checksum() && w.checksum()==app->world.checksum();
        for(int i=0;i<120 && exact;++i){w.update();replay.update();app->world.update();exact=w.checksum()==replay.checksum() && w.checksum()==app->world.checksum();}
        check(exact,label+" actual World60/App11 resumes on all 120 ticks");w=snapshot;w.map.bldOccRef=&w.bldOcc;
    };
    struct Case {const char* actor;const char* normal;const char* elite;bool building;bool temporal;};
    const Case cases[]={
        {"E1","Para","ParaE",false,false},{"GGI","MissileLauncher","MissileLauncherE",false,false},
        {"GHOST","MP5","MP5E",false,false},{"TANY","DoublePistols","DoublePistolsE",false,false},
        {"GHOST","MP5","MP5E",true,false},{"TANY","DoublePistols","DoublePistolsE",true,false},
        {"CLEG","NeutronRifle","NeutronRifleE",false,true}};
    for(const auto& c:cases)for(int rank:{0,2}) {
        reset();const EID bf=board(c.actor,rank);if(!w.valid(bf) || w.ents[bf].cargo.empty())continue;
        const bool hero=std::string(c.actor)=="GHOST" || std::string(c.actor)=="TANY";
        const EID target=c.building?w.spawnBuilding(1,BldType::PowerPlant,14,12,true):w.spawnUnit(1,hero?UnitType::Conscript:UnitType::Rhino,16.5f,12.5f);
        w.ents[target].nativeIdentity.sleeping=true;w.ents[bf].cargo[0].atkCd=0;
        const std::string label=std::string(c.actor)+(rank?"-elite":"-normal")+(c.building?"-building":"-ground");
        const auto weapon=w.nativeCrewWeapon(w.ents[bf],w.ents[bf].cargo[0],&w.ents[target]);
        check(weapon.nativeSource && weapon.nativeSource->id==(rank?c.elite:c.normal),label+" selects original normal/elite weapon");
        const int hp=w.ents[target].hp;const uint64_t uid=w.ents[bf].cargo[0].combatActor;w.updateNativeCrew(bf);int projectiles=0;
        for(const auto& p:w.projs)if(p.sourceActor==uid && p.w.nativeSource){++projectiles;check(p.w.nativeSource->id==(rank?c.elite:c.normal),label+" emitted projectile carries selected source");}
        const int delta=hp-w.ents[target].hp;
        check(w.ents[bf].cargo[0].atkCd>0 && (delta>0 || projectiles>0 || w.ents[target].immobilized()),label+" real fire changes cooldown and target/projectile state");
        if(c.building)check(w.valid(target) && delta>0 && delta<10,label+" primary uses source 1% building verses");
        if(c.temporal)check(w.ents[target].immobilized(),label+" automatic Temporal selection retained");
        // Original SSA heavy verses=40%; floor(30*.4) and floor(32*.4) are 12.
        if(std::string(c.actor)=="E1")check(weapon.damage==(rank?32:30) && weapon.cooldown==(rank?18:30) && delta==12,label+" source multipliers applied once");
        if(std::string(c.actor)=="GGI")check(weapon.damage==(rank?66:48) && weapon.cooldown==(rank?24:80) && weapon.antiAir && weapon.antiGround,label+" source missile multipliers and domains");
        save(label);
        if(std::string(c.actor)=="E1" || std::string(c.actor)=="GGI") {
            check(w.orderUnload({bf}).released==1,label+" real unload releases infantry");EID unloaded=INVALID_EID;
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].combatActor==uid)unloaded=id;
            bool correct=w.valid(unloaded) && !w.ents[unloaded].deployed;
            if(correct){const auto foot=w.effWeapon(w.ents[unloaded],&w.ents[target]);correct=foot.nativeSource && foot.nativeSource->id==(rank?"M60E":"M60");}
            check(correct,label+" unload retains UID and ordinary undeployed weapon");
        }
    }
    for(bool air:{false,true})for(int rank:{0,2}) {
        reset();const EID bf=board("GGI",rank);if(!w.valid(bf) || w.ents[bf].cargo.empty())continue;
        const EID target=w.spawnUnit(1,air?UnitType::Rocketeer:UnitType::Rhino,21.5f,12.5f);w.ents[target].nativeIdentity.sleeping=true;w.ents[bf].cargo[0].atkCd=1000000;
        if(air){w.orderMove({target},21.5f,12.5f,false);for(int i=0;i<160 && !w.ents[target].airborne();++i)w.update();}
        check(w.ents[target].airborne()==air,"actual target establishes the required air/ground domain");
        w.spawnUnit(0,UnitType::MCV,23.5f,12.5f);w.update();auto& passenger=w.ents[bf].cargo[0];passenger.atkCd=0;const auto uid=passenger.combatActor;
        const float distance=distf(w.ents[bf].x,w.ents[bf].y,w.ents[target].x,w.ents[target].y);
        check(distance>8 && distance<10,"actual target is outside M60 and inside source missile range");w.updateNativeCrew(bf);bool shot=false;
        for(const auto& p:w.projs)if(p.sourceActor==uid && p.target==target && p.w.nativeSource)shot|=p.w.nativeSource->id==(rank?"MissileLauncherE":"MissileLauncher");
        const std::string label=std::string("GGI-")+(rank?"elite":"normal")+(air?"-air-long":"-ground-long");check(shot,label+" real source missile emitted");save(label);
    }
    {
        Ini ini;check(ini.load("assets/rules/profiles/yr.ini"),"source metadata readable for corruption checks");
        const auto* source=ini.find(("Type."+std::to_string(w.nativeUnitIndex("GGI"))).c_str());
        check(source!=nullptr,"source GGI crew row exists");
        if(source) {
            const auto changed=[&](const char* key,const char* value) {auto row=*source;
                for(auto it=row.kv.begin();it!=row.kv.end();++it)if(it->first==key){if(value)it->second=value;else row.kv.erase(it);break;}return row;};
            NativeCrewProfile parsed;const auto* catalog=w.nativeWeaponCatalog.get();
            check(parseNativeCrewProfile(source,2,catalog,parsed) && parsed.transportWeapon==1,"valid explicit source slot accepted");
            for(const char* bad:{"-2","32","2147483648","1x",""}) {
                const auto row=changed("CrewTransportWeapon",bad);check(!parseNativeCrewProfile(&row,2,catalog,parsed),std::string("invalid forced slot rejected: ")+bad);
            }
            for(const char* key:{"CrewTransportWeapon","CrewTransportReference","CrewEliteTransportReference"}) {
                const auto row=changed(key,nullptr);check(!parseNativeCrewProfile(&row,2,catalog,parsed),std::string("required field rejected when absent: ")+key);
            }
            for(const char* key:{"CrewTransportReference","CrewEliteTransportReference"}) {
                for(const auto& bad:std::vector<std::string>{"-2",std::to_string(catalog->weapons.size()),"0x"}) {
                    const auto row=changed(key,bad.c_str());check(!parseNativeCrewProfile(&row,2,catalog,parsed),std::string("invalid catalog reference rejected: ")+key+"="+bad);
                }
            }
            const auto automatic=changed("CrewTransportWeapon","-1");check(!parseNativeCrewProfile(&automatic,2,catalog,parsed),"automatic slot rejects stray fixed references");
            check(!parseNativeCrewProfile(source,1,catalog,parsed) && !parseNativeCrewProfile(source,0,catalog,parsed),"Crew2 metadata cannot masquerade as Crew1 or a pre-crew scene");
            auto legacy=*source;std::erase_if(legacy.kv,[](const auto& p){return p.first=="CrewTransportWeapon" || p.first=="CrewTransportReference" || p.first=="CrewEliteTransportReference";});
            check(parseNativeCrewProfile(&legacy,1,catalog,parsed) && parsed.transportWeapon==-1,"Crew1 retains historical automatic selection");
        }
        for(const char* version:{"0","3","2x"}) {
            auto corrupt=ini;for(auto& row:corrupt.sections)if(row.name=="NativeCrew")for(auto& [key,value]:row.kv)if(key=="Version")value=version;
            NativeCrewRules rules;check(!parseNativeCrewRules(corrupt,true,rules),std::string("unsupported crew version rejected: ")+version);
        }
        std::ifstream profile("assets/rules/profiles/yr.ini",std::ios::binary);const std::string text{std::istreambuf_iterator<char>(profile),{}};
        for(const auto& mutation:std::vector<std::pair<std::string,std::string>>{{"CrewVersion = 2","CrewVersion = 1"},{"[NativeCrew]\nVersion = 2","[NativeCrew]\nVersion = 1"},{"[Scene]\nVersion = 43","[Scene]\nVersion = 38"}}) {
            auto corrupt=text;const auto position=corrupt.find(mutation.first);World rejected;
            if(position!=std::string::npos)corrupt.replace(position,mutation.first.size(),mutation.second);
            check(position!=std::string::npos && !rejected.restoreNativeDefinitions(corrupt),"embedded scene rejects mismatched or future crew contract: "+mutation.second);
        }
    }
    // Fixed numbered slots must scan by catalog range even when ordinary
    // Primary/Secondary slots are empty. Missing references must stay silent.
    for(bool missing:{false,true})for(int rank:{0,2}) {
        reset();const EID bf=board("GGI",rank);if(!w.valid(bf) || w.ents[bf].cargo.empty())continue;
        auto& p=w.ents[bf].cargo[0];auto definition=std::make_shared<NativeActorDefinition>(*p.nativeIdentity.definition);
        definition->crew.transportWeapon=3;if(missing)definition->crew.transportWeapons[rank>=2?1:0]=-1;else definition->guns.slots={};
        p.nativeIdentity.definition=definition;p.atkCd=0;const auto uid=p.combatActor;
        const EID target=w.spawnUnit(1,UnitType::Rhino,missing?16.5f:21.5f,12.5f);w.ents[target].nativeIdentity.sleeping=true;const int hp=w.ents[target].hp;w.updateNativeCrew(bf);
        bool shot=false;for(const auto& projectile:w.projs)shot|=projectile.sourceActor==uid && projectile.target==target;
        check(missing?!shot && p.atkCd==0 && hp==w.ents[target].hp:shot,missing?"explicit empty source slot never falls back":"numbered fixed slot scans and fires using actual catalog range");
    }
    for(const char* name:{"GGI-normal-ground","E1-elite-ground","GHOST-normal-building","CLEG-elite-ground"}) {
        std::ifstream input(std::string("tests/fixtures/native-crew-v60/")+name+"-world60.sav",std::ios::binary);std::vector<uint8_t> historical{std::istreambuf_iterator<char>(input),{}};
        World old;const bool loaded=load(historical,old);check(loaded && old.nativeSceneVersion==36 && old.nativeCrewRules.version==1,std::string(name)+" real pre-change World60 retains Scene36/Crew1");
        if(loaded) {
            check(bytes(old)==historical,std::string(name)+" historical payload remains byte-identical");
            bool selected=false;for(const auto& carrier:old.ents)if(carrier.alive && !carrier.cargo.empty()) {
                const auto& p=carrier.cargo.front();const World::Ent* target=nullptr;
                if(std::string(name).starts_with("GHOST"))for(const auto& victim:old.ents)if(victim.alive && victim.player==1 && victim.isBuilding && victim.btype==BldType::PowerPlant){target=&victim;break;}
                const auto weapon=old.nativeCrewWeapon(carrier,p,target);
                const std::string expected=std::string(name).starts_with("GGI")?"M60":std::string(name).starts_with("E1")?"M60E":std::string(name).starts_with("GHOST")?"Sapper":"NeutronRifleE";
                selected=weapon.nativeSource && weapon.nativeSource->id==expected;
            }
            check(selected,std::string(name)+" legacy passenger retains historical weapon choice");
            World replay;bool exact=load(historical,replay);for(int i=0;i<120 && exact;++i){old.update();replay.update();exact=old.checksum()==replay.checksum();}
            check(exact,std::string(name)+" historical checkpoint resumes deterministically");
        }
    }
    return failures;
}
