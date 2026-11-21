#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <memory>

namespace {
struct CleanupFixture {
    std::unique_ptr<World> storage=std::make_unique<World>();
    SkirmishAI bot;std::vector<EID> army;bool ready=false;
    CleanupFixture(int choice,Faction side) {
        auto& w=*storage;
        const auto country=side==Faction::Soviet?Country::Russia:side==Faction::Yuri?Country::Yuri:side==Faction::China?Country::China:Country::America;
        const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        ready=w.init(64,64,1717,2,0,{side,Faction::Allies},0,"maps/skirmish-reference.txt",true,true,profile,{country,Country::America});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        w.cratesEnabled=false;w.crates.clear();
        const auto type=side==Faction::Soviet?UnitType::Rhino:side==Faction::Yuri?UnitType::LasherTank:side==Faction::China?UnitType::Type99:UnitType::Grizzly;
        for(int i=0;i<12;++i){const auto id=w.spawnUnit(0,type,18.5f+i%4,19.5f+i/4);ready &= w.valid(id);army.push_back(id);}
        ready &= w.valid(w.spawnUnit(1,UnitType::MCV,25.5f,20.5f));
        for(auto& player:w.players){player.money=0;player.defeated=false;}
        w.map.reveal(0,20,20,8);
        bot.reset(0);bot.rallyPoint={20,20};bot.attackTimer=7;bot.pcfg.rallySize=1;
        bot.pcfg.useEngineers=bot.pcfg.useSpies=bot.pcfg.useSuperWeapon=false;
        // Initialize the observation map through a real think before adding
        // historical contacts; cold-map initialization clears stale memory.
        for(int i=0;i<15;++i)bot.update(w);
        w.orderStop(army);bot.attackWave=0;bot.attackTimer=7;bot.rallyPoint={20,20};
        ready &= bot.knowledgeValid(w);
    }
    void think(){for(int i=0;i<15;++i)bot.update(*storage);}
};
}

int GameRegression::aiCleanupRegression() {
    g_script.shutdown();int failures=0;
    const auto check=[&](bool ok,int choice,int faction,const char* text) {
        failures+=!ok;std::printf("AI_CLEANUP %s choice=%d faction=%d %s\n",ok?"PASS":"FAIL",choice,faction,text);std::fflush(stdout);
    };
    for(int choice=0;choice<3;++choice)for(int faction=0;faction<4;++faction) {
        if(choice && (faction==(int)Faction::China || (choice==1 && faction==(int)Faction::Yuri)))continue;
        for(auto type:{BldType::BattleLab,BldType::Radar,BldType::PowerPlant}) {
            CleanupFixture f(choice,(Faction)faction);auto& w=*f.storage;
            const auto building=w.spawnBuilding(1,type,40,20,true);
            if(!f.ready || !w.valid(building)){check(false,choice,faction,"valid remaining-building fixture");continue;}
            f.bot.knownBuildings={{40,20,1,type,w.tick}};f.think();bool advancing=true;
            for(EID id:f.army)advancing &= w.ents[id].attackMove.active && distf(w.ents[id].attackMove.x,w.ents[id].attackMove.y,40.5f,20.5f)<12;
            check(advancing && w.map.fogAt(0,40,20)==FOG_UNSEEN,choice,faction,
                "a remaining observed lab, radar or power plant receives a persistent approach through local decoys");
        }
        {
            CleanupFixture f(choice,(Faction)faction);auto& w=*f.storage;
            for(int y=15;y<=26;++y)for(int x=36;x<=48;++x)w.map.at(x,y).terrain=Terrain::Water;
            const auto yard=w.spawnBuilding(1,BldType::NavalYard,40,20,true);
            const auto lab=w.spawnBuilding(1,BldType::BattleLab,40,36,true);
            if(!f.ready || !w.valid(yard) || !w.valid(lab)){check(false,choice,faction,"valid unreachable-yard fixture");continue;}
            f.bot.knownBuildings={{40,20,1,BldType::NavalYard,w.tick},{40,36,1,BldType::BattleLab,w.tick}};
            f.think();bool advancing=true;
            for(EID id:f.army)advancing &= w.ents[id].attackMove.active && distf(w.ents[id].attackMove.x,w.ents[id].attackMove.y,40.5f,36.5f)<12;
            check(advancing,choice,faction,"an unreachable naval memory does not suppress a reachable remaining installation");
        }
        {
            CleanupFixture f(choice,(Faction)faction);auto& w=*f.storage;
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].player==1)w.kill(id,false);
            const auto hidden=w.spawnUnit(1,UnitType::MCV,58.5f,58.5f);w.players[1].defeated=false;
            bool setup=f.ready && w.valid(hidden) && w.orderMove({f.army[0],f.army[1]},32.5f,32.5f,false).accepted==2;
            const auto first=w.ents[f.army[0]],second=w.ents[f.army[1]];f.think();int scouts=0;
            for(EID id:f.army)scouts+=w.ents[id].attackMove.active;
            const auto retained=[&](EID id,const World::Ent& before){const auto& e=w.ents[id];return e.state==before.state && e.path==before.path
                && e.goalX==before.goalX && e.goalY==before.goalY && !e.attackMove.active;};
            check(setup && scouts==2 && retained(f.army[0],first) && retained(f.army[1],second),choice,faction,
                "two ordinary formation moves do not consume scouting slots or get overwritten by new scouts");
        }
        {
            CleanupFixture f(choice,(Faction)faction);auto& w=*f.storage;
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].player==1)w.kill(id,false);
            const auto hidden=w.spawnUnit(1,UnitType::MCV,58.5f,58.5f);w.players[1].defeated=false;
            const auto kind=w.ents[f.army.front()].utype;
            bool ready=f.ready && w.valid(hidden);
            for(int i=0;i<36;++i){const auto id=w.spawnUnit(0,kind,16.5f+i%8,24.5f+i/8);ready &= w.valid(id);f.army.push_back(id);}
            w.map.reveal(0,20,24,14);
            World altered=w;altered.map.bldOccRef=&altered.bldOcc;
            altered.ents[hidden].x=56.5f;altered.ents[hidden].y=2.5f;altered.unitOccSync(hidden);auto other=f.bot;
            f.think();for(int i=0;i<15;++i)other.update(altered);
            int scouts=0;bool separate=true,privateGoals=true;std::vector<Vec2f> goals;
            for(EID id:f.army){const auto& e=w.ents[id];const auto& b=altered.ents[id];
                privateGoals &= e.attackMove==b.attackMove && e.target==INVALID_EID;
                if(!e.attackMove.active)continue;++scouts;
                for(auto goal:goals)separate &= distf(goal.x,goal.y,e.attackMove.x,e.attackMove.y)>=3;
                goals.push_back({e.attackMove.x,e.attackMove.y});}
            check(ready && scouts==8 && separate && privateGoals,choice,faction,
                "a large idle army surveys eight separate fog regions without learning a hidden enemy position");
        }
    }
    return failures;
}
