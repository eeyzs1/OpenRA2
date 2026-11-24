#include "game_regression.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {
World fixture(const char* profile="assets/rules/profiles/ra2.ini") {
    World w;
    w.init(64,64,20261124,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile,
        {Country::America,Country::Russia});
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& player:w.players){player.money=0;player.defeated=false;player.isAI=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);
    return w;
}
int type(const World& w,const char* name) {
    for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->id==name)return i;
    return -1;
}
void clearFeedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void repair(World& w,int player,EID id) {
    World::Cmd command{};command.type=World::Cmd::RepairBuilding;command.ids={id};
    w.bindCommand(command);w.applyCmd(player,command);
}
bool save(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"w+b");bool ok=f && w.saveGame(f);
    if(ok){std::rewind(f);ok=copy.loadGame(f) && w.checksum()==copy.checksum();}
    if(f)std::fclose(f);return ok;
}
struct Case {const char* name;const char* type;int phase,missing,cash,fee;};
constexpr Case cases[]{
    {"allied-power","GAPOWR",0,80,100,1},
    {"soviet-power","NAPOWR",9,80,100,1},
    {"barracks-phase","GAPILE",27,40,100,1},
    {"prism-rounding","ATESLA",0,24,100,2},
    {"tesla-tail","TESLA",0,1,2,2},
    {"allied-refinery","GAREFN",0,24,100,1},
    {"soviet-refinery","NAREFN",0,24,100,1},
    {"exact-cash","ATESLA",0,8,2,2},
    {"short-cash","ATESLA",0,8,1,2},
    {"empty-cash","GAPOWR",0,8,0,1},
};
}

int GameRegression::buildingRepairRegression(const char* outputDir) {
    const std::filesystem::path out(outputDir);std::filesystem::create_directories(out);int failures=0;
    const auto check=[&](bool ok,const char* kind,const char* name){
        failures+=!ok;std::printf("BUILDING_REPAIR %s %s %s\n",ok?"PASS":"FAIL",kind,name);std::fflush(stdout);
    };
    selectRulesChoice(RulesChoice::RA2);
    for(const auto& c:cases) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;
        const int index=type(w,c.type);
        if(index<0){check(false,"SETUP",c.name);continue;}
        const auto& def=*w.nativeTypes[index];
        const EID building=w.spawnBuilding(0,def.building.type,20,20,true,index);
        for(int at=0;at<c.phase;++at)w.update();
        if(!w.valid(building)){check(false,"SETUP",c.name);continue;}
        w.ents[building].hp=def.building.hp-c.missing;w.players[0].money=c.cash;
        repair(w,0,building);
        bool admitted=w.ents[building].repairing && !w.presentationEvents.empty()
            && w.presentationEvents.back().building.accepted;
        World copy,middle;
        bool exact=save(w,copy,out/(std::string(c.name)+"-ordered.sav")),midExact=true,middleActive=false;
        std::ofstream trace(out/(std::string(c.name)+".trace"));trace<<w.checksum()<<'\n';
        std::ofstream(out/(std::string(c.name)+".case"))<<building<<'\n';
        int expectedHp=w.ents[building].hp,expectedCash=c.cash,first=-1,steps=0;
        bool enabled=true,behavior=true;
        for(int at=1;at<=600;++at) {
            const int before=w.ents[building].hp;
            w.update();if(exact){copy.update();exact=w.checksum()==copy.checksum();}
            if(middleActive && midExact){middle.update();midExact=w.checksum()==middle.checksum();}
            if(enabled && w.tick%28==0) {
                if(expectedCash<c.fee)enabled=false;
                else {expectedCash-=c.fee;expectedHp=std::min(def.building.hp,expectedHp+8);enabled=expectedHp<def.building.hp;}
            }
            if(w.ents[building].hp!=before){if(first<0)first=w.tick;++steps;}
            behavior &= w.ents[building].hp==expectedHp && w.players[0].money==expectedCash && w.ents[building].repairing==enabled;
            trace<<w.checksum()<<'\n';
            if(at==13){midExact=save(w,middle,out/(std::string(c.name)+"-midway.sav"));middleActive=true;}
            clearFeedback(w);clearFeedback(copy);clearFeedback(middle);
        }
        std::ofstream(out/(std::string(c.name)+".final"))<<w.checksum()<<'\n';
        std::printf("BUILDING_REPAIR OBSERVED name=%s scene=%d first_tick=%d steps=%d hp=%d cash=%lld expected_hp=%d expected_cash=%d admitted=%d exact=%d midway=%d\n",
            c.name,w.nativeRulesBaseVersion,first,steps,w.ents[building].hp,(long long)w.players[0].money,expectedHp,expectedCash,admitted,exact,midExact);
        check(admitted && behavior,"COMMAND",c.name);check(exact && midExact,"REPLAY",c.name);
    }
    if(const char* input=std::getenv("OPENRA2_LEGACY_BUILDING_REPAIR60_INPUT_DIR"))for(const auto& c:cases) {
        const std::filesystem::path dir(input);World w;
        FILE* f=std::fopen((dir/(std::string(c.name)+"-ordered.sav")).string().c_str(),"rb");
        bool exact=f && w.loadGame(f);if(f)std::fclose(f);
        std::ifstream trace(dir/(std::string(c.name)+".trace")),final(dir/(std::string(c.name)+".final"));
        uint64_t value=0;exact &= w.nativeRulesBaseVersion==60 && bool(trace>>value) && value==w.checksum();
        for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>value) && value==w.checksum();clearFeedback(w);}
        exact &= bool(final>>value) && value==w.checksum();check(exact,"LEGACY60",c.name);
    }
    for(int variant=0;variant<3;++variant) {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;const int index=type(w,"GAPOWR");
        const EID building=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,index);
        const int initial=variant==1?742:726;w.ents[building].hp=initial;w.players[0].money=variant==1?0:100;
        if(variant==2)w.players[0].powerSabotage=1000;
        repair(w,0,building);bool correct=true;
        for(int at=1;at<=112;++at) {
            if(variant==0 && (at==14 || at==30))repair(w,0,building);
            if(variant==1 && at==30)w.players[0].money=1;
            if(variant==1 && at==36)repair(w,0,building);
            w.update();
            if(variant==0 && w.tick==28)correct &= w.ents[building].hp==initial && !w.ents[building].repairing;
            if(variant==0 && w.tick==55)correct &= w.ents[building].hp==initial && w.players[0].money==100;
            if(variant==1 && w.tick==35)correct &= w.ents[building].hp==initial && !w.ents[building].repairing;
            if(variant==2)correct &= w.players[0].lowPower();
            clearFeedback(w);
        }
        correct &= w.ents[building].hp==750 && !w.ents[building].repairing && w.players[0].money==(variant==1?0:97);
        World resumed;correct &= save(w,resumed,out/("control-"+std::to_string(variant)+".sav"));
        check(correct,"CONTROL",variant==0?"toggle-preserves-global-phase":variant==1?"funds-require-reenable":"low-power-does-not-pause");
    }
    {
        World w=fixture();w.map.bldOccRef=&w.bldOcc;const int index=type(w,"GAPOWR");
        const EID building=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,index);bool correct=true;
        for(int variant=0;variant<4;++variant) {
            auto& e=w.ents[building];e.hp=variant==0?750:700;e.constructAnim=variant==1?100:0;e.selling=variant==2;
            repair(w,variant==3?1:0,building);
            correct &= !e.repairing && !w.presentationEvents.empty() && !w.presentationEvents.back().building.accepted;
            clearFeedback(w);
        }
        check(correct,"ADMISSION","full-constructing-selling-foreign");
    }
    {
        World pending;FILE* f=std::fopen((out/"allied-power-midway.sav").string().c_str(),"rb");
        bool exact=f && pending.loadGame(f);if(f)std::fclose(f);
        world=std::move(pending);world.map.bldOccRef=&world.bldOcc;ais.clear();
        const auto before=world.checksum();const auto app=out/"repair-app11.sav";auto copy=std::make_unique<Game>();
        exact &= saveGameFile(app.string().c_str()) && copy->loadGameFile(app.string().c_str()) && copy->world.checksum()==before;
        std::ifstream in(app,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(in),{}};int rejected=0;
        const auto declared="Version = "+std::to_string(nativeCurrentRulesSceneVersion);
        const std::pair<std::string,std::string> invalid[]{
            {declared,"Version = "+std::to_string(nativeCurrentRulesSceneVersion+1)},{declared,"Version = 60"},
            {"BuildingRepairFrames = 14","BuildingRepairFrames = 00"},{"BuildingRepairStep = 8","BuildingRepairStep = 0"},
            {"BuildingRepairCost = 2","BuildingRepairCost = 0"}};
        for(const auto& [from,to]:invalid) {
            auto broken=bytes;const auto at=broken.find(from);bool atomic=at!=std::string::npos;
            if(atomic)broken.replace(at,from.size(),to);
            const auto file=out/("rejected-"+std::to_string(rejected)+".sav");
            {std::ofstream output(file,std::ios::binary);output.write(broken.data(),broken.size());atomic &= bool(output);}
            atomic &= !loadGameFile(file.string().c_str()) && world.checksum()==before;exact &= atomic;rejected+=atomic;
        }
        std::ofstream trace(out/"repair-app.trace");trace<<world.checksum()<<'\n';
        for(int at=0;at<600 && exact;++at){world.update();copy->world.update();exact=world.checksum()==copy->world.checksum();trace<<world.checksum()<<'\n';clearFeedback(world);clearFeedback(copy->world);}
        std::ofstream(out/"repair-app.final")<<world.checksum()<<'\n';check(exact && rejected==5,"APP","active-repair-and-atomic-rejections");
    }
    for(auto choice:{RulesChoice::YR,RulesChoice::Fusion}) {
        selectRulesChoice(choice);World w=fixture(choice==RulesChoice::YR?"assets/rules/profiles/yr.ini":nullptr);w.map.bldOccRef=&w.bldOcc;
        const EID building=w.spawnBuilding(0,BldType::PowerPlant,20,20,true);const auto d=bldDef(w.ents[building]);
        int hp=d.hp-80,cash=100;bool enabled=true,correct=true;w.ents[building].hp=hp;w.players[0].money=cash;repair(w,0,building);
        for(int at=1;at<=600;++at){w.update();if(enabled && w.tick%15==0){const int heal=std::min((d.hp+19)/20,d.hp-hp);const int cost=std::max(1,(d.cost*15*heal+d.hp-1)/(100*d.hp));if(cash<cost)enabled=false;else{cash-=cost;hp+=heal;enabled=hp<d.hp;}}
            correct &= w.ents[building].hp==hp && w.players[0].money==cash && w.ents[building].repairing==enabled;clearFeedback(w);}
        check(correct,"LEGACY",choice==RulesChoice::YR?"yr43-pips":"fusion-pips");
    }
    selectRulesChoice(RulesChoice::RA2);
    return failures;
}
