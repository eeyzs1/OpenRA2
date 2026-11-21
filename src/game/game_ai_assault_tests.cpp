#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <memory>

namespace {
struct Fixture {
    std::unique_ptr<World> storage;
    SkirmishAI bot;
    std::vector<EID> army;
    EID base=INVALID_EID,decoy=INVALID_EID;
    bool ready=false;
    Fixture(int choice,Faction side,int count=12):storage(std::make_unique<World>()) {
        auto& w=*storage;
        const auto country=side==Faction::Soviet?Country::Russia:side==Faction::Yuri?Country::Yuri:side==Faction::China?Country::China:Country::America;
        const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        ready=w.init(64,64,1717,2,0,{side,Faction::Allies},0,"maps/skirmish-reference.txt",true,true,profile,{country,Country::America});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        w.cratesEnabled=false;w.crates.clear();
        const auto type=side==Faction::Soviet?UnitType::Rhino:side==Faction::Yuri?UnitType::LasherTank:side==Faction::China?UnitType::Type99:UnitType::Grizzly;
        for(int i=0;i<count;++i){const auto id=w.spawnUnit(0,type,18.5f+i%4,19.5f+i/4);ready &= w.valid(id);army.push_back(id);}
        base=w.spawnBuilding(1,BldType::ConYard,40,20,true);
        decoy=w.spawnUnit(1,UnitType::MCV,25.5f,20.5f);
        ready &= w.valid(base) && w.valid(decoy);
        w.map.reveal(0,20,20,8);
        bot.reset(0);bot.rallyPoint={20,20};bot.attackTimer=7;bot.pcfg.rallySize=1;
        bot.pcfg.useEngineers=bot.pcfg.useSpies=bot.pcfg.useSuperWeapon=false;
    }
};
void think(SkirmishAI& bot,World& world) {for(int i=0;i<15;++i)bot.update(world);}
bool retained(const World::Ent& a,const World::Ent& b) {
    return a.attackMove.active==b.attackMove.active && a.attackMove.owner==b.attackMove.owner
        && a.attackMove.x==b.attackMove.x && a.attackMove.y==b.attackMove.y && a.attackMove.layer==b.attackMove.layer
        && a.attackMove.retryAt==b.attackMove.retryAt;
}
}

int GameRegression::aiAssaultRegression() {
    g_script.shutdown();int failures=0;
    const auto check=[&](bool ok,int choice,int faction,const char* label) {
        failures+=!ok;std::printf("AI_ASSAULT %s choice=%d faction=%d %s\n",ok?"PASS":"FAIL",choice,faction,label);std::fflush(stdout);
    };
    for(int choice=0;choice<3;++choice)for(int faction=0;faction<4;++faction) {
        if(choice && (faction==(int)Faction::China || (choice==1 && faction==(int)Faction::Yuri)))continue;
        {
            Fixture f(choice,(Faction)faction);auto& w=*f.storage;
            if(!f.ready){check(false,choice,faction,"valid source assault fixture");continue;}
            think(f.bot,w);bool local=true;
            for(EID id:f.army)local &= w.ents[id].target==f.decoy && !w.ents[id].attackMove.active;
            check(local && f.bot.knownBuildings.empty(),choice,faction,"an undiscovered base never replaces the actually visible local contact");
        }
        {
            Fixture f(choice,(Faction)faction);auto& w=*f.storage;if(!f.ready)return failures+1;
            w.map.reveal(0,41,21,5);think(f.bot,w);bool objective=true;
            for(EID id:f.army) {
                const auto& unit=w.ents[id];
                objective &= unit.attackMove.active && unit.target==INVALID_EID
                    && distf(unit.attackMove.x,unit.attackMove.y,40.5f,20.5f)<12;
            }
            check(objective && f.bot.attackWave==1,choice,faction,"a wave has an accepted infrastructure destination despite a closer visible decoy");
            const auto id=f.army.front();w.orderMove({id},45.5f,45.5f,true);
            w.ents[id].state=UState::Idle;w.ents[id].clearPath();w.ents[id].attackMove.retryAt=w.tick+LOGIC_FPS;
            const auto before=w.ents[id];f.bot.rallyPoint={5,45};f.bot.rallyCheckTimer=900;
            think(f.bot,w);
            check(retained(before,w.ents[id]) && w.ents[id].state==UState::Idle,choice,faction,
                "temporary path retry cannot be replaced by another ordinary rally order");
        }
        {
            Fixture f(choice,(Faction)faction);auto& w=*f.storage;if(!f.ready)return failures+1;
            w.map.reveal(0,41,21,5);f.bot.attackTimer=0;think(f.bot,w);
            for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_SEEN);
            w.map.reveal(0,20,20,8);w.kill(f.base,false);
            World changed=w;changed.map.bldOccRef=&changed.bldOcc;
            w.spawnBuilding(1,BldType::ConYard,50,46,true);changed.spawnBuilding(1,BldType::ConYard,6,50,true);
            f.bot.attackTimer=7;f.bot.rallyCheckTimer=0;SkirmishAI other=f.bot;
            think(f.bot,w);think(other,changed);bool remembered=f.bot.knownBuildings==other.knownBuildings;
            for(EID id:f.army) {
                const auto& a=w.ents[id];const auto& b=changed.ents[id];
                remembered &= a.attackMove.active && retained(a,b) && a.target==INVALID_EID && b.target==INVALID_EID
                    && distf(a.attackMove.x,a.attackMove.y,40.5f,20.5f)<12;
            }
            check(remembered,choice,faction,"hidden destruction and different hidden replacement bases cannot change the remembered assault destination");
        }
        for(int visible=0;visible<2;++visible) {
            Fixture f(choice,(Faction)faction,1);auto& w=*f.storage;if(!f.ready)return failures+1;
            w.kill(f.base,false);w.kill(f.decoy,false);w.kill(f.army.front(),false);
            w.spawnBuilding(0,BldType::ConYard,25,25,true);
            const auto type=faction==0?UnitType::Grizzly:faction==1?UnitType::Rhino:faction==2?UnitType::Type99:UnitType::LasherTank;
            const auto defender=w.spawnUnit(0,type,26.5f,36.5f),enemy=w.spawnUnit(1,UnitType::MCV,17.5f,25.5f);
            if(!w.valid(defender) || !w.valid(enemy))return failures+1;
            // The fixture has replaced both opening forces. Their temporary
            // elimination is setup, not the state of the match being tested.
            for(auto& player:w.players)player.defeated=false;
            w.ents[defender].guard=true;
            for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
            w.map.reveal(0,26,26,7);w.map.reveal(0,26,36,5);if(visible)w.map.reveal(0,17,25,2);
            f.bot.reset(0);think(f.bot,w);
            const bool targeted=w.ents[defender].target==enemy || f.bot.lastAttacker==enemy;
            if(targeted!=(bool)visible)std::printf("AI_ASSAULT DEFENSE_STATE choice=%d faction=%d visible=%d active=%d defeated=%d yard=%d defender=%d player=%d state=%d guard=%d target=%d enemy=%d fog=%d last=%d timer=%d\n",
                choice,faction,visible,w.players[0].active,w.players[0].defeated,w.hasBld(0,BldType::ConYard),defender,w.ents[defender].player,
                (int)w.ents[defender].state,w.ents[defender].guard,w.ents[defender].target,enemy,w.map.fogAt(0,17,25),f.bot.lastAttacker,f.bot.defenseCheckTimer);
            check(targeted==(bool)visible && (w.map.fogAt(0,17,25)==FOG_VISIBLE)==(bool)visible,choice,faction,
                visible?"a genuinely visible base raid still gets a local defense":"a hidden nearby raider never enters defensive target state");
        }
        for(int visible=0;visible<2;++visible) {
            Fixture f(choice,(Faction)faction,2);auto& w=*f.storage;if(!f.ready)return failures+1;
            w.kill(f.base,false);w.kill(f.decoy,false);
            const auto enemy=w.spawnUnit(1,UnitType::MCV,30.5f,20.5f);if(!w.valid(enemy))return failures+1;
            for(auto& player:w.players)player.defeated=false;
            const auto shooter=f.army[0],helper=f.army[1];w.ents[helper].guard=true;
            w.ents[shooter].state=UState::Attacking;w.ents[shooter].target=enemy;
            for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
            w.map.reveal(0,20,20,7);if(visible)w.map.reveal(0,30,20,2);
            f.bot.reset(0);f.bot.pcfg.focusFire=true;think(f.bot,w);
            check((w.ents[helper].target==enemy)==(bool)visible,choice,faction,
                visible?"visible focus fire may recruit an idle nearby helper":"a retained but currently hidden combat target cannot reveal itself to idle helpers");
        }
    }
    for(int choice=0;choice<3;++choice) {
        Fixture f(choice,Faction::Soviet);auto& w=*f.storage;if(!f.ready)return failures+1;
        w.map.reveal(0,41,21,5);think(f.bot,w);
        World replay;FILE* file=std::tmpfile();bool equal=file && w.saveGame(file);
        if(file){std::rewind(file);equal &= replay.loadGame(file);std::fclose(file);}
        SkirmishAI other=f.bot;
        const auto baseRef=w.entityRef(f.base);bool damaged=false;
        for(int tick=0;tick<1500 && equal;++tick) {
            w.update();replay.update();f.bot.update(w);other.update(replay);
            equal &= w.checksum()==replay.checksum() && f.bot==other;
            damaged |= !w.valid(baseRef) || w.ents[f.base].hp<bldDef(w.ents[f.base]).hp;
            w.presentationEvents.clear();w.evaQueue.clear();replay.presentationEvents.clear();replay.evaQueue.clear();
        }
        check(equal && damaged,choice,(int)Faction::Soviet,"real combat reaches infrastructure and resumes for 1500 exact ticks after a World56 roundtrip with paired AI state");
        Fixture hard(choice,Faction::Soviet);auto& hw=*hard.storage;if(!hard.ready)return failures+1;
        hw.kill(hard.base,false);hard.base=hw.spawnBuilding(1,BldType::ConYard,44,12,true);
        const auto factory=hw.spawnBuilding(1,BldType::WarFactory,44,28,true);
        hw.map.reveal(0,45,14,5);hw.map.reveal(0,45,30,5);
        hard.bot.difficulty=AIDiff::Hard;hard.bot.pcfg.focusFire=false;think(hard.bot,hw);bool split=hw.valid(factory);
        for(size_t i=0;i<hard.army.size();++i) {
            const auto& unit=hw.ents[hard.army[i]];
            split &= unit.attackMove.active && (i%2==0?unit.attackMove.y<21:unit.attackMove.y>21);
        }
        check(split,choice,(int)Faction::Soviet,"hard groups pressure two observed installations of one opponent with retained orders");
    }
    for(int mode=0;mode<4;++mode) {
        Fixture f(0,Faction::Allies);auto& w=*f.storage;if(!f.ready)return failures+1;
        w.kill(f.base,false);w.kill(f.decoy,false);
        const EID base=w.spawnBuilding(1,BldType::ConYard,mode==3?25:24,18,true);
        const EID plant=w.spawnBuilding(1,BldType::PowerPlant,23,21,true);
        for(auto& p:w.players)p.defeated=false;
        const EID oldWatcher=f.army.back();w.kill(oldWatcher,false);
        const EID watcher=w.spawnUnit(0,UnitType::GI,21.5f,21.5f);
        if(!w.valid(watcher))return failures+1;
        w.orderMove({watcher},30.5f,19.5f,true);
        std::vector<EID> others(f.army.begin(),f.army.end()-1);w.orderMove(others,5.5f,5.5f,false);
        bool incidental=false;
        for(int frame=0;frame<120 && w.valid(plant);++frame) {
            w.update();const auto& unit=w.ents[watcher];
            incidental=unit.state==UState::Attacking && unit.target==plant && unit.attackMove.active;
            if(incidental)break;
        }
        check(incidental && w.valid(base),0,0,"a real retained assault acquires the nearby peripheral power plant");
        if(!incidental)continue;
        f.bot.attackTimer=0;think(f.bot,w); // Observe the base without starting another wave.
        if(mode==1) {
            std::fill(w.map.fog[0].begin(),w.map.fog[0].end(),FOG_UNSEEN);
            w.map.reveal(0,23,22,1); // The remembered base is currently hidden.
        }
        if(mode==2)w.orderGuard({watcher});
        const auto before=w.ents[watcher];
        f.bot.attackTimer=7;f.bot.rallyPoint={20,20};f.bot.rallyCheckTimer=900;
        think(f.bot,w);const auto& after=w.ents[watcher];
        if(mode==0)check(after.target==base && (after.state==UState::Attacking || after.state==UState::Chasing),0,0,
            "a visible nearby construction yard takes priority over incidental peripheral fire");
        else check(after.target==before.target && retained(before,after) && after.guard==before.guard,0,0,
            mode==1?"hidden strategic infrastructure cannot redirect a visible local engagement":mode==2?
            "an explicit guard order is preserved during strategic target selection":"a distant known base does not interrupt an ongoing local engagement");
    }
    for(int choice=0;choice<3;++choice)for(auto type:{UnitType::Intruder,UnitType::Kirov})for(int scenario=0;scenario<4;++scenario) {
        const auto side=type==UnitType::Intruder?Faction::Allies:Faction::Soviet;
        Fixture f(choice,side);auto& w=*f.storage;if(!f.ready)return failures+1;
        const auto air=w.spawnUnit(0,type,scenario==2?10.5f:20.5f,20.5f);if(!w.valid(air))return failures+1;
        w.ents[air].state=UState::Circling;w.ents[air].ammo=unitDef(w.ents[air]).ammo;
        if(scenario)w.map.reveal(0,41,21,5);
        if(scenario>=2){if(w.orderAttack({air},f.decoy).accepted!=1)return failures+1;
            if(scenario==2 && w.ents[air].state!=UState::Chasing)return failures+1;
            if(scenario==3)w.ents[air].state=UState::Attacking;}
        think(f.bot,w);const auto& actor=w.ents[air];
        const auto expected=scenario==1 || scenario==2?f.base:f.decoy;
        check(actor.target==expected,choice,(int)side,scenario==0?
            "an air raid cannot target undiscovered enemy infrastructure":scenario==1?
            "an available air raid chooses visible strategic infrastructure over a mobile decoy":scenario==2?
            "air pursuit can switch from a mobile decoy to visible strategic infrastructure":
            "active local air fire retains its target and weapon burst");
    }
    return failures+aiCleanupRegression();
}
