#include "game_regression.h"
#include "game/ai.h"
#include <algorithm>
#include <cstdio>
#include <memory>

int aiBuildOrderRegression() {
    int failures=0;
    for(int choice=0;choice<3;++choice)for(int faction=0;faction<4;++faction) {
        if(choice && (faction==(int)Faction::China || (choice==1 && faction==(int)Faction::Yuri)))continue;
        for(int scenario=0;scenario<10;++scenario) {
            auto storage=std::make_unique<World>();auto& w=*storage;const auto side=(Faction)faction;
            const auto country=side==Faction::Soviet?Country::Russia:side==Faction::Yuri?Country::Yuri:side==Faction::China?Country::China:Country::UK;
            const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
            bool setup=w.init(64,64,1847,2,0,{side,Faction::Soviet},0,nullptr,false,false,profile,{country,Country::Russia});
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
            for(auto& cell:w.map.cells)cell=Cell{};w.cratesEnabled=false;w.crates.clear();
            const auto power=side==Faction::Allies?BldType::PowerPlant:side==Faction::Yuri?BldType::BioReactor:BldType::TeslaReactor;
            setup &= w.valid(w.spawnBuilding(0,BldType::ConYard,4,4,true));
            setup &= w.valid(w.spawnBuilding(1,BldType::ConYard,50,50,true));
            setup &= w.valid(w.spawnBuilding(0,BldType::OreRefinery,4,15,true));
            if(scenario==9)setup &= w.valid(w.spawnBuilding(0,BldType::OreRefinery,12,15,true));
            if(scenario!=0 && scenario!=3 && scenario!=8)setup &= w.valid(w.spawnBuilding(0,BldType::Barracks,20,4,true));
            if(scenario!=0 && scenario!=1 && scenario!=3 && scenario!=4 && scenario!=8)
                setup &= w.valid(w.spawnBuilding(0,BldType::WarFactory,28,4,true));
            for(int i=0;i<8;++i)setup &= w.valid(w.spawnBuilding(0,power,4+i*5,30,true));
            const EID miner=w.spawnUnit(0,harvesterType(side),20.5f,20.5f);setup &= w.valid(miner);
            if(!setup){std::printf("AI_BUILD_ORDER FAIL choice=%d faction=%d scenario=%d valid fixture\n",choice,faction,scenario);++failures;continue;}
            w.ents[miner].autoHarvest=false;for(auto& p:w.players){p.money=50000;p.defeated=false;}w.update();
            const auto savedConfig=g_aiBuild[faction];auto& cfg=g_aiBuild[faction];cfg.enabled=scenario>=3 && scenario<=7;
            cfg.buildOrder={bldTypeKey(power),"OreRefinery","Barracks","WarFactory","OreRefinery"};
            if(scenario==6)cfg.buildOrder={"Barracks","Barracks",bldTypeKey(power)};
            if(scenario==7)cfg.buildOrder={bldTypeKey(power),"OreRefinery","Barracks"};
            SkirmishAI bot;bot.reset(0);if(scenario>=8){bot.personality=AIPersonality::Steamroller;bot.initPersonality();}
            const auto funds=w.players[0].money;
            for(int i=0;i<15;++i)bot.update(w);
            const auto& job=w.players[0].bldProd;
            const auto wanted=scenario==0 || scenario==3 || scenario==6 || scenario==8?BldType::Barracks:
                scenario==1 || scenario==4?BldType::WarFactory:BldType::OreRefinery;
            const bool ordered=scenario==7?!job.active:job.active && job.typeIdx==w.productionSelector(0,false,(int)wanted);
            const bool conserved=w.players[0].money==funds && job.paid==0;
            World replay;FILE* f=tmpfile();bool exact=f && w.saveGame(f);if(exact){rewind(f);exact=replay.loadGame(f);}if(f)fclose(f);
            SkirmishAI other=bot;exact &= replay.checksum()==w.checksum();
            for(int i=0;i<2 && exact;++i){w.update();replay.update();bot.update(w);other.update(replay);exact &= w.checksum()==replay.checksum() && bot==other;}
            g_aiBuild[faction]=savedConfig;
            const bool ok=ordered && conserved && exact;failures+=!ok;
            std::printf("AI_BUILD_ORDER %s choice=%d faction=%d scenario=%d ordered=%d conserved=%d exact=%d selected=%d expected=%d\n",
                ok?"PASS":"FAIL",choice,faction,scenario,ordered,conserved,exact,job.active?job.typeIdx:-1,scenario==7?-1:w.productionSelector(0,false,(int)wanted));
            std::fflush(stdout);
        }
    }
    return failures;
}
