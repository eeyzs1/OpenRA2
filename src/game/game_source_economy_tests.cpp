#include "game_regression.h"
#include "game/script.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

int GameRegression::sourceEconomyRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* label){
        failures+=!ok;std::printf("SOURCE_ECONOMY %s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);
    };
    const auto type=[&](const char* id){
        for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==id)return i;
        return -1;
    };
    const auto replaceField=[](std::string text,const std::string& section,const char* key,const char* value){
        const auto start=text.find("["+section+"]"),end=text.find("\n[",start+1);
        const auto field=text.find(std::string(key)+" = ",start);
        if(start==std::string::npos || field==std::string::npos || (end!=std::string::npos && field>=end))return std::string{};
        text.replace(field,text.find('\n',field)-field,std::string(key)+" = "+value);return text;
    };
    const auto fresh=[&](int edition,Faction faction=Faction::Allies,const char* customProfile=nullptr){
        resetMatchInteraction();ais.clear();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;clearPlacement();
        selectRulesChoice(edition?RulesChoice::YR:RulesChoice::RA2);
        cfgCountry=(int)(faction==Faction::Yuri?Country::Yuri:Country::America);
        const auto profile=customProfile?std::string(customProfile):std::string("assets/rules/profiles/")+(edition?"yr":"ra2")+".ini";
        if(!world.init(64,64,711,2,0,{faction,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,profile.c_str(),
            {faction==Faction::Yuri?Country::Yuri:Country::America,Country::Russia}))return false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;world.crates.clear();
        for(auto& player:world.players)player.isAI=false;
        world.spawnBuilding(0,BldType::ConYard,3,3,true);world.spawnBuilding(1,BldType::ConYard,50,50,true);
        for(int i=0;i<5;++i)world.spawnBuilding(0,BldType::TeslaReactor,35+i*4,4,true,type("NAPOWR"));
        world.players[0].money=world.players[1].money=10000;return true;
    };
    const auto order=[&](World::Cmd::Type kind,const std::vector<EID>& ids,EID target){
        World::Cmd command{};command.type=kind;command.ids=ids;command.a=target;
        world.bindCommand(command);world.applyCmd(0,command);
    };
    const auto finish=[&](EID id){for(int frame=0;frame<180 && world.valid(id);++frame)world.update();};
    const auto roundTrip=[&](const char* label){
        const auto fileName=(std::filesystem::path(outputDir)/(std::string(label)+"-world56.sav")).string();
        FILE* file=std::fopen(fileName.c_str(),"wb");bool same=file && world.saveGame(file);if(file)std::fclose(file);
        World restored;file=std::fopen(fileName.c_str(),"rb");same &= file && restored.loadGame(file);if(file)std::fclose(file);
        same &= restored.checksum()==world.checksum();
        for(int frame=0;frame<90 && same;++frame){world.update();restored.update();same &= restored.checksum()==world.checksum();}
        return same;
    };

    // The actual original StageClass update, MissionHarvest gate and Harvesting
    // method yield one pickup per18 original frames with the default rate2.
    // Arrival and unloading remain separate from this core timing check.
    struct MiningCase {int edition;const char* name;bool gems;int rate,interval;};
    for(const auto c:{MiningCase{0,"CMIN",false,2,36},MiningCase{0,"CMIN",true,2,36},
            MiningCase{0,"HARV",false,2,36},MiningCase{0,"HARV",true,2,36},
            MiningCase{0,"CMIN",false,3,54},MiningCase{0,"HARV",true,5,90},
            MiningCase{1,"CMIN",false,0,20},MiningCase{1,"HARV",true,0,20}}) {
        const char* name=c.name;const bool gems=c.gems;std::string custom;
        if(c.edition==0 && c.rate!=2) {
            std::ifstream file("assets/rules/profiles/ra2.ini");std::string text{std::istreambuf_iterator<char>(file),{}};
            text=replaceField(std::move(text),"NativeProduction","HarvesterLoadRate",std::to_string(c.rate).c_str());
            if(text.empty()){check(false,"custom harvesting profile has its explicit source rate");return failures;}
            custom=(std::filesystem::path(outputDir)/("harvest-rate-"+std::to_string(c.rate)+".ini")).string();
            std::ofstream output(custom,std::ios::binary);output<<text;
        }
        if(!fresh(c.edition,Faction::Allies,custom.empty()?nullptr:custom.c_str()))return 1;
        const int index=type(name);const auto refinery=world.spawnBuilding(0,BldType::OreRefinery,20,20,true);
        if(index<0 || !world.valid(refinery))return 1;
        const auto miner=world.spawnUnit(0,world.nativeTypes[index]->unit.type,26.5f,23.5f,index);
        const int capacity=name[0]=='C'?20:40;
        auto& deposit=world.map.at(28,23);deposit.terrain=gems?Terrain::Gems:Terrain::Ore;deposit.ore=deposit.oreMax=200;
        World::Cmd command{};command.type=World::Cmd::Harvest;command.ids={miner};command.x=28.5f;command.y=23.5f;
        world.bindCommand(command);world.applyCmd(0,command);
        for(int frame=0;frame<2000 && world.ents[miner].state!=UState::HarvestDig;++frame)world.update();
        const bool arrived=world.ents[miner].state==UState::HarvestDig && world.ents[miner].oreLoad==0;
        World resumed;bool saved=false,exact=true,timing=arrived;int fullAt=0,previous=0,pickups=0,first=0;
        for(int elapsed=1;arrived && elapsed<=capacity*c.interval;++elapsed) {
            world.update();if(saved){resumed.update();exact &= resumed.checksum()==world.checksum();}
            const auto& actor=world.ents[miner];
            timing &= actor.oreLoad==elapsed/c.interval;
            if(actor.oreLoad>pickups){if(!first)first=elapsed;timing &= elapsed-previous==c.interval && actor.oreLoad==pickups+1;previous=elapsed;pickups=actor.oreLoad;}
            if(elapsed==17) {
                const auto suffix=c.edition?std::string("-yr"):c.rate!=2?"-rate"+std::to_string(c.rate):std::string{};
                const auto path=(std::filesystem::path(outputDir)/(std::string("gather-")+name+(gems?"-gems":"-ore")+suffix+"-mid-world60.sav")).string();
                FILE* file=std::fopen(path.c_str(),"wb");const bool written=file && world.saveGame(file);if(file)std::fclose(file);
                file=std::fopen(path.c_str(),"rb");saved=written && file && resumed.loadGame(file);if(file)std::fclose(file);
                exact &= saved && resumed.checksum()==world.checksum();
            }
            if(actor.oreLoad==capacity){fullAt=elapsed;break;}
        }
        std::printf("SOURCE_HARVEST edition=%d id=%s gems=%d rate=%d first=%d expected_first=%d full_ticks=%d expected_full=%d saved=%d exact=%d\n",
            c.edition,name,gems,c.rate,first,c.interval,fullAt,capacity*c.interval,saved,exact);
        check(arrived && timing && fullAt==capacity*c.interval && saved && exact,c.edition?
            "YR retains its current20tick harvest cadence and exact mid-gather continuation":
            "a legally ordered RA2 miner follows the independently proven core pickup cadence through a mid-gather save");
    }

    if(const char* directory=std::getenv("OPENRA2_LEGACY_GATHER_INPUT_DIR")) {
        for(const char* name:{"CMIN","HARV"})for(bool gems:{false,true}) {
            const auto path=(std::filesystem::path(directory)/(std::string("gather-")+name+(gems?"-gems":"-ore")+"-mid-world60.sav")).string();
            World old,copy;FILE* file=std::fopen(path.c_str(),"rb");bool exact=file && old.loadGame(file);if(file)std::fclose(file);
            EID miner=INVALID_EID;
            if(exact)for(EID id=0;id<(EID)old.ents.size();++id)if(old.valid(id) && old.ents[id].state==UState::HarvestDig
                && old.ents[id].oreCell.x==28 && old.ents[id].oreCell.y==23)miner=id;
            if(!old.valid(miner)){check(false,"actual pre-change mining checkpoint loads");continue;}
            exact &= old.nativeSceneVersion==43 && old.nativeProductionRules.version==6 && old.ents[miner].digTimer==17;
            file=tmpfile();exact &= file && old.saveGame(file);if(exact){rewind(file);exact=copy.loadGame(file);}if(file)std::fclose(file);
            for(int frame=1;frame<=83 && exact;++frame) {
                old.update();copy.update();const int expected=(frame+17)/20;
                exact &= old.checksum()==copy.checksum() && old.ents[miner].oreLoad==expected;
            }
            check(exact,"an actual pre-change Scene43 mid-gather save retains its20tick cadence and exact continuation");
        }
    }

    if(!fresh(0))return 1;
    {
        const auto source=world.nativeScenarioSource;
        auto missing=source;const auto field=missing.find("HarvesterLoadRate = ");
        if(field!=std::string::npos)missing.erase(field,missing.find('\n',field)-field+1);
        bool rejected=field!=std::string::npos;
        for(const auto& invalid:{missing,replaceField(source,"NativeProduction","HarvesterLoadRate","0"),
                replaceField(source,"NativeProduction","HarvesterLoadRate","1001"),
                replaceField(source,"NativeProduction","HarvesterLoadRate","nan"),
                replaceField(source,"NativeProduction","Version","6"),replaceField(source,"Scene","Version","43")}) {
            World bad;World::RulesSnapshot snapshot{invalid,true,true};
            rejected &= !invalid.empty() && !bad.init(64,64,711,2,0,{Faction::Allies,Faction::Soviet},0,
                "maps/skirmish-reference.txt",true,true,nullptr,{},&snapshot);
        }
        check(rejected,"missing or invalid mining rates and mismatched ordinary scene/production headers reject");
    }

    // Original GAME.EXE storage-value and house-credit calls independently
    // establish 25 credits per ore unit, 50 per gem unit (no purifier).
    for(int edition=0;edition<2;++edition)for(const char* name:{"CMIN","HARV"})for(bool gems:{false,true}) {
        if(!fresh(edition))return 1;
        const int index=type(name);const auto refinery=world.spawnBuilding(0,BldType::OreRefinery,20,20,true);
        if(index<0 || !world.valid(refinery))return 1;
        const auto miner=world.spawnUnit(0,world.nativeTypes[index]->unit.type,26.5f,23.5f,index);
        const int capacity=name[0]=='C'?20:40;
        auto& deposit=world.map.at(28,23);deposit.terrain=gems?Terrain::Gems:Terrain::Ore;deposit.ore=deposit.oreMax=200;
        World::Cmd command{};command.type=World::Cmd::Harvest;command.ids={miner};command.x=28.5f;command.y=23.5f;
        world.bindCommand(command);world.applyCmd(0,command);
        const auto opening=world.players[0].money;const auto income=world.harvestedIncome[0];
        World restored;bool full=false,saved=false,exact=true,finished=false;
        for(int frame=0;frame<10000 && world.valid(miner);++frame){
            world.update();if(saved){restored.update();exact &= restored.checksum()==world.checksum();}
            const auto& e=world.ents[miner];full |= e.oreLoad==capacity;
            if(!saved && e.state==UState::HarvestUnload && e.oreLoad==capacity){
                const auto path=(std::filesystem::path(outputDir)/("miner-"+std::to_string(edition)+"-"+name+(gems?"-gems":"-ore")+"-inflight-world60.sav")).string();
                FILE* f=std::fopen(path.c_str(),"wb");const bool written=f && world.saveGame(f);if(f)std::fclose(f);
                f=std::fopen(path.c_str(),"rb");saved=written && f && restored.loadGame(f);if(f)std::fclose(f);
                exact &= saved && restored.checksum()==world.checksum();
            }
            if(full && e.oreLoad==0 && world.harvestedIncome[0]>income){finished=true;break;}
        }
        const auto paid=world.players[0].money-opening;const int expected=capacity*(gems?50:25);
        std::printf("SOURCE_MINER edition=%d id=%s gems=%d capacity=%d income=%lld expected=%d full=%d saved=%d exact=%d finished=%d\n",
            edition,name,gems,capacity,(long long)paid,expected,full,saved,exact,finished);
        check(full && saved && exact && finished && paid==expected && world.harvestedIncome[0]-income==expected,
            "a full source miner actually harvests, returns and credits the original ore/gem value through an exact in-flight save");
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_MINER_INPUT_DIR")) {
        for(int edition=0;edition<2;++edition)for(const char* name:{"CMIN","HARV"})for(bool gems:{false,true}) {
            const auto path=(std::filesystem::path(directory)/("miner-"+std::to_string(edition)+"-"+name+(gems?"-gems":"-ore")+"-inflight-world60.sav")).string();
            World old,copy;FILE* f=std::fopen(path.c_str(),"rb");const bool loaded=f && old.loadGame(f);if(f)std::fclose(f);
            EID miner=INVALID_EID;
            for(EID id=0;id<(EID)old.ents.size();++id)if(old.valid(id) && old.ents[id].player==0 && old.ents[id].oreLoad>0)miner=id;
            if(!loaded || !old.valid(miner)){check(false,"actual legacy active miner input loads");continue;}
            const auto initial=old.players[0].money;const int expected=old.ents[miner].oreLoad*(gems?70:35);
            f=tmpfile();bool exact=f && old.saveGame(f);if(exact){rewind(f);exact=copy.loadGame(f);}if(f)std::fclose(f);
            for(int frame=0;frame<120 && old.ents[miner].oreLoad>0 && exact;++frame){old.update();copy.update();exact=old.checksum()==copy.checksum();}
            check(exact && old.nativeSceneVersion==37 && old.nativeProductionRules.version==5 && old.ents[miner].oreLoad==0
                && old.players[0].money-initial==expected,
                "an actual pre-change Scene37 active miner retains its original35/70 cashout and exact continuation");
        }
    }
    struct Product{const char* id;int refund;};
    for(const auto product:{Product{"E1",100},Product{"BRUTE",250},Product{"SLAV",0},Product{"TANY",750}}) {
        if(!fresh(1,Faction::Yuri))return 1;
        const auto grinder=world.spawnBuilding(0,BldType::Grinder,20,20,true);
        const int index=type(product.id);if(index<0 || !world.valid(grinder))return 1;
        const auto unit=world.spawnUnit(0,world.nativeTypes[index]->unit.type,19.5f,21.5f,index);
        const auto admission=world.entryRejection(unit,grinder,EntryKind::Garrison);
        if(std::string(product.id)=="E1") {
            camZoom=1;camX=-500;camY=280;sel={unit};sideMode=0;dragging=false;
            const auto anchor=bldScreenPos(world.ents[grinder]);
            updateHoverCursor((int)anchor.x,(int)anchor.y-20);issueSmartOrder((int)anchor.x,(int)anchor.y-20);
            check(cursorKind==CursorKind::Enter && world.ents[unit].state==UState::Boarding,
                "a real source grinder hover and right click accept the zero-capacity recycling entrance");
            world.map.reveal(0,22,22,12);bakeTerrain();
            const auto imagePath=(std::filesystem::path(outputDir)/"grinder-right-click.png").string();
            const auto hash=world.checksum();shotFile=imagePath;render();Image image=LoadImage(imagePath.c_str());
            check(image.data && world.checksum()==hash,"the source recycling interaction renders without changing simulation");
            UnloadImage(image);
        } else order(World::Cmd::Garrison,{unit},grinder);
        finish(unit);
        const auto gained=world.players[0].money-10000;
        std::printf("SOURCE_ECONOMY GRIND id=%s admission=%d consumed=%d refund=%lld expected=%d\n",
            product.id,(int)admission,!world.valid(unit),(long long)gained,product.refund);
        check(admission==EntryRejectReason::None && !world.valid(unit) && gained==product.refund,
            "a source grinder consumes an actual unit and uses original Soylent, including zero and a hero");
    }
    {
        if(!fresh(1,Faction::Yuri))return 1;
        const auto reactor=world.spawnBuilding(0,BldType::BioReactor,20,20,true);
        const int before=world.players[0].powerMade;bool admitted=true;
        for(int i=0;i<5;++i){
            const auto infantry=world.spawnUnit(0,UnitType::Initiate,19.5f,21.5f);
            admitted &= world.entryRejection(infantry,reactor,EntryKind::Garrison)==EntryRejectReason::None;
            order(World::Cmd::Garrison,{infantry},reactor);finish(infantry);admitted &= !world.valid(infantry);
        }
        const auto sixth=world.spawnUnit(0,UnitType::Initiate,19.5f,21.5f);
        const auto tank=world.spawnUnit(0,UnitType::LasherTank,18.5f,21.5f);
        check(admitted && world.ents[reactor].garrison.size()==5 && world.players[0].powerMade==before+500
            && world.entryRejection(sixth,reactor,EntryKind::Garrison)==EntryRejectReason::Full
            && world.entryRejection(tank,reactor,EntryKind::Garrison)==EntryRejectReason::Incompatible,
            "a source reactor holds five infantry, gains power, rejects a sixth and rejects vehicles");
        check(roundTrip("bio-full"),"five absorbed source infantry and their power resume exactly from World56");
        world.kill(sixth,false);world.kill(tank,false);
        const auto enemy=world.spawnUnit(1,UnitType::Conscript,23.5f,21.5f);
        const int health=world.ents[enemy].hp;world.ents[enemy].guard=true;
        for(int frame=0;frame<120;++frame)world.update();
        check(world.valid(enemy) && world.ents[enemy].hp==health,
            "absorbed source infantry generate power without firing through the reactor walls");
        world.kill(enemy,false);
        order(World::Cmd::Ungarrison,{reactor},reactor);
        check(world.ents[reactor].garrison.empty() && world.players[0].powerMade==before,
            "a real evacuation restores absorbed infantry and removes their additional power");
        if(!fresh(1,Faction::Yuri))return 1;
        const auto bunker=world.spawnBuilding(0,BldType::TankBunker,25,20,true);
        const auto bunkerTank=world.spawnUnit(0,UnitType::LasherTank,24.5f,21.5f);
        const auto bunkerAdmission=world.entryRejection(bunkerTank,bunker,EntryKind::Garrison);
        order(World::Cmd::Garrison,{bunkerTank},bunker);finish(bunkerTank);
        const bool entered=!world.valid(bunkerTank);
        const auto other=world.spawnUnit(0,UnitType::LasherTank,24.5f,21.5f);
        std::printf("SOURCE_ECONOMY BUNKER admission=%d capacity=%d entered=%d held=%zu next=%d\n",
            (int)bunkerAdmission,bldDef(world.ents[bunker]).garrisonCap,entered,world.ents[bunker].garrison.size(),
            (int)world.entryRejection(other,bunker,EntryKind::Garrison));
        check(entered && world.ents[bunker].garrison.size()==1
            && world.entryRejection(other,bunker,EntryKind::Garrison)==EntryRejectReason::Full,
            "the source one-passenger tank bunker accepts a tank of transport size three");
    }
    {
        if(!fresh(1,Faction::Yuri))return 1;
        world.spawnBuilding(0,BldType::Barracks,8,15,true,type("GAPILE"));
        const auto vat=world.spawnBuilding(0,BldType::CloningVat,20,25,true);
        world.ents[vat].rallyX=28;world.ents[vat].rallyY=30;
        const auto grinder=world.spawnBuilding(0,BldType::Grinder,20,20,true);
        const bool ordered=world.startUnitProd(0,UnitType::GI);
        for(int frame=0;frame<3000 && ordered && !world.producedUnits[0];++frame)world.update();
        std::vector<EID> infantry;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && !world.ents[id].isBuilding && world.ents[id].player==0
            && world.ents[id].nativeIdentity.definition->id=="E1")infantry.push_back(id);
        check(ordered && world.producedUnits[0]==1 && infantry.size()==2 && world.players[0].money==9800,
            "one paid source GI order produces the original and its free clone");
        bool rallied=false;for(auto id:infantry)rallied |= (int)world.ents[id].goalX==28 && (int)world.ents[id].goalY==30;
        check(rallied,"the actual free clone receives its cloning vat's rally order");
        order(World::Cmd::Garrison,infantry,grinder);
        for(int frame=0;frame<900;++frame)world.update();
        bool consumed=true;for(auto id:infantry)consumed &= !world.valid(id);
        check(consumed && world.players[0].money==10000,
            "recycling the paid GI and its clone refunds the original total without artificial profit");
        check(roundTrip("clone-recycle"),"actual clone recycling money and entity retirement survive World56");
    }
    for(int edition=0;edition<2;++edition)for(const char* identity:{"NACNST","NAREFN","NAPOWR","NATECH","NARADR","GAAIRC"}) {
        if(!fresh(edition))return 1;const int index=type(identity);if(index<0)return 1;
        const auto building=world.spawnBuilding(1,world.nativeTypes[index]->building.type,20,20,true,index);
        const auto spy=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);world.map.reveal(0,22,22,10);world.players[1].money=10001;
        const auto admission=world.attackRejection(spy,building);
        if(std::string(identity)=="NACNST") {
            camZoom=1;camX=-500;camY=0;sel={spy};sideMode=0;dragging=false;
            const auto anchor=bldScreenPos(world.ents[building]);
            updateHoverCursor((int)anchor.x,(int)anchor.y-20);
            check(cursorKind==CursorKind::NoMove,"a source spy sees a rejection cursor over an unspyable yard");
        }
        if(std::string(identity)=="NARADR"){
            world.map.fog[1][60*64+30]=FOG_SEEN;
            world.map.fog[0][60*64+30]=FOG_SEEN;
        }
        order(World::Cmd::Attack,{spy},building);finish(spy);
        const auto& a=world.players[0];const auto& b=world.players[1];const std::string id=identity;
        std::printf("SOURCE_ECONOMY SPY edition=%d target=%s admission=%d cash=%lld/%lld power=%d tech=%d\n",
            edition,identity,(int)admission,(long long)a.money,(long long)b.money,b.powerSabotage,a.stolenTech);
        if(id=="NACNST")check(admission!=AttackRejectReason::None && world.valid(spy) && a.money==10000 && b.money==10001,
            "an unspyable source yard rejects infiltration and preserves the spy and both balances");
        if(id=="NAREFN")check(!world.valid(spy) && a.money==15000 && b.money==5001,
            "a real refinery infiltration transfers exactly half of an odd balance, rounded down");
        if(id=="NAPOWR")check(!world.valid(spy) && b.powerSabotage==2000,
            "power espionage uses 1000 source frames at the native two-tick clock ratio");
        if(id=="NATECH")check(!world.valid(spy) && a.stolenTech==2 && a.money==10000 && b.money==10001,
            "a source laboratory grants technology without a synthetic money transfer");
        if(id=="NARADR" || id=="GAAIRC")check(!world.valid(spy) && !a.revealTimer && !a.vetCat[2],
            "source radar infiltration grants neither full-map vision nor aircraft veterancy");
        if(id=="NARADR")check(world.map.fog[1][60*64+30]==FOG_UNSEEN && world.map.fog[0][60*64+30]==FOG_SEEN,
            "radar espionage reshrouds the victim's explored terrain without erasing the infiltrator's terrain");
    }
    for(int edition=0;edition<2;++edition)for(auto owner:{Faction::Allies,Faction::Soviet}) {
        if(!fresh(edition))return 1;world.players[1].faction=owner;
        const auto lab=world.spawnBuilding(1,BldType::BattleLab,20,20,true,type("GATECH"));
        const auto spy=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);world.map.reveal(0,22,22,10);
        order(World::Cmd::Attack,{spy},lab);finish(spy);
        const int expected=edition==0 && owner==Faction::Soviet?2:1;
        check(!world.valid(spy) && world.players[0].stolenTech==expected,
            "a captured Allied laboratory follows RA2's current house side and YR's original laboratory identity");
    }
    {
        if(!fresh(1))return 1;
        const auto refinery=world.spawnBuilding(1,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const auto spy=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);world.map.reveal(0,22,22,10);
        const Credits maximum=std::numeric_limits<Credits>::max()/4;
        world.players[0].money=0;world.players[1].money=maximum;
        order(World::Cmd::Attack,{spy},refinery);finish(spy);
        check(world.players[0].money==maximum/2 && world.players[1].money==maximum-maximum/2,
            "source money theft remains exact at the 64-bit ledger ceiling");
        check(roundTrip("spy-rich"),"the 64-bit source transfer survives World56 and deterministic continuation");
        const auto next=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);
        world.players[0].money=maximum-1;world.players[1].money=10001;
        order(World::Cmd::Attack,{next},refinery);finish(next);
        check(world.players[0].money==maximum && world.players[1].money==10000,
            "limited recipient headroom transfers only one credit and preserves total money");
    }
    for(int edition=0;edition<2;++edition) {
        if(!fresh(edition))return 1;
        world.spawnBuilding(0,BldType::Barracks,8,15,true,type("GAPILE"));
        const auto barracks=world.spawnBuilding(1,BldType::Barracks,20,20,true,type("NAHAND"));
        const auto spy=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);world.map.reveal(0,22,22,10);
        order(World::Cmd::Attack,{spy},barracks);finish(spy);
        const bool infiltrated=!world.valid(spy);
        const bool ordered=world.startUnitProd(0,UnitType::Engineer);
        for(int frame=0;frame<3000 && !world.producedUnits[0];++frame)world.update();
        bool engineer=false;for(const auto& actor:world.ents)if(actor.alive && !actor.isBuilding && actor.player==0
            && actor.utype==UnitType::Engineer)engineer=actor.vetRank==0;
        check(infiltrated && world.players[0].vetCat[0] && ordered && engineer,
            "an infiltrated source barracks does not promote a Trainable=no engineer");
        world.spawnBuilding(0,BldType::CloningVat,20,25,true,type("NACLON"));
        const auto produced=world.producedUnits[0];const bool training=world.startUnitProd(0,UnitType::GI);
        for(int frame=0;frame<3000 && world.producedUnits[0]==produced;++frame)world.update();
        EID soldier=INVALID_EID;int veterans=0;bool experience=true;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && !world.ents[id].isBuilding && world.ents[id].player==0
            && world.ents[id].utype==UnitType::GI) {
            soldier=id;++veterans;experience &= world.ents[id].vetRank==1 && world.ents[id].veterancyValue==600;
        }
        check(training && veterans==2 && experience,
            "a paid spy-veteran GI and its actual free clone both start at the 600-credit experience boundary");
        check(roundTrip(edition?"veteran-yr":"veteran-ra2"),"factory-veteran originals and clones retain experience through World56 replay");
        if(world.valid(soldier)) {
            const auto target=world.spawnUnit(1,UnitType::Spy,world.ents[soldier].x+3,world.ents[soldier].y);
            world.map.reveal(0,(int)world.ents[soldier].x,(int)world.ents[soldier].y,10);
            order(World::Cmd::Attack,{soldier},target);
            for(int frame=0;frame<1200 && world.valid(target);++frame)world.update();
            check(!world.valid(target) && world.ents[soldier].vetRank==2 && world.ents[soldier].veterancyValue==1600,
                "a born veteran advances to elite after actually killing a 1000-credit spy");
        } else check(false,"the paid source veteran exists for real combat");
    }
    {
        if(!fresh(1))return 1;
        for(int y=10;y<40;++y)for(int x=40;x<60;++x)world.map.at(x,y).terrain=Terrain::Water;
        world.spawnBuilding(0,BldType::NavalYard,44,18,true,type("GAYARD"));
        const auto factory=world.spawnBuilding(1,BldType::WarFactory,20,20,true,type("NAWEAP"));
        const auto spy=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);world.map.reveal(0,22,22,10);
        order(World::Cmd::Attack,{spy},factory);finish(spy);
        const bool infiltrated=!world.valid(spy),ordered=world.startUnitProd(0,UnitType::Destroyer);
        for(int frame=0;frame<4000 && !world.producedUnits[0];++frame)world.update();
        bool veteran=false;for(const auto& actor:world.ents)if(actor.alive && !actor.isBuilding && actor.player==0
            && actor.utype==UnitType::Destroyer)veteran=actor.vetRank==1 && actor.veterancyValue==3000;
        check(infiltrated && world.players[0].vetCat[1] && !world.players[0].vetCat[3] && ordered && veteran,
            "source VehicleType espionage also promotes an actual paid naval vehicle from a shipyard");
    }
    if(!fresh(1,Faction::Yuri))return 1;
    const std::string source=world.nativeScenarioSource;
    const std::string row="NativeProduction.Type."+std::to_string(type("YAGRND"));
    bool malformed=true;
    for(const auto& invalid:{replaceField(source,row,"Soylent","-2"),replaceField(source,row,"Grinding","2"),
            replaceField(source,row,"Passengers","65"),replaceField(source,row,"SpyEffect","8"),
            replaceField(source,"NativeProduction","SpyMoneyFraction","nan"),
            replaceField(source,"NativeProduction","SpyBlackoutTicks","-1"),
            replaceField(source,"NativeProduction","SpyTechByOwner","2"),
            replaceField(source,"NativeProduction","OreValue","-1"),
            replaceField(source,"NativeProduction","GemValue","1000001"),
            replaceField(source,"NativeProduction","OreValue","nan"),
            replaceField(source,"NativeProduction","Version","5"),
            replaceField(source,"Scene","Version","37")}) {
        World rejected;World::RulesSnapshot rules{invalid,true,true};
        malformed &= !invalid.empty() && !rejected.init(64,64,711,2,0,{Faction::Yuri,Faction::Soviet},0,
            "maps/skirmish-reference.txt",true,true,nullptr,{},&rules);
    }
    check(malformed,"malformed source intake, refund, espionage, mineral prices and mismatched economy versions reject before the match starts");
    auto legacy=replaceField(source,"Scene","Version","28");
    legacy=replaceField(legacy,"NativeProduction","Version","2");
    // This is a synthetic old source, not an actual historical save. Remove
    // later catalog sections/references before claiming the Scene28 layout.
    {
        std::istringstream rows(legacy);std::string line,clean;bool catalogSection=false;
        while(std::getline(rows,line)) {
            if(!line.empty() && line.front()=='[')catalogSection=line.starts_with("[NativeWeapons]")
                || line.starts_with("[NativeCrew]") || line.starts_with("[NativeWeapon.") || line.starts_with("[NativeProjectile.") || line.starts_with("[NativeWarhead.");
            if(catalogSection || (line.starts_with("Gun") && line.find("Reference = ")!=std::string::npos)
                || line.starts_with("Crew") || line.starts_with("TemporalVersion = ") || line.starts_with("Warpable = ")
                || line.starts_with("NativeIFV = ") || line.starts_with("IFV") || line.starts_with("VehicleRepairable = ")
                || line.starts_with("ImmuneToPsionicWeapons = ") || line.starts_with("ImmuneToRadiation = ")
                || line.starts_with("ImmuneToPoison = ") || line.starts_with("OreValue = ") || line.starts_with("GemValue = "))continue;
            clean+=line+'\n';
        }
        legacy=std::move(clean);
    }
    for(int i=0;i<(int)world.nativeTypes.size();++i) {
        const auto mask=world.nativeTypes[i]->production.supportMask;
        const auto oldMask=((mask&3)?1:0)|((mask&4)?2:0)|((mask&8)?4:0);
        legacy=replaceField(legacy,"NativeProduction.Type."+std::to_string(i),"SupportMask",std::to_string(oldMask).c_str());
    }
    World old;World::RulesSnapshot oldRules{legacy,true,true};
    bool compatible=old.init(64,64,711,2,0,{Faction::Yuri,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,nullptr,{},&oldRules);
    if(compatible){
        const auto grinder=old.spawnBuilding(0,BldType::Grinder,20,20,true);
        const auto infantry=old.spawnUnit(0,UnitType::Initiate,19.5f,21.5f);
        compatible=old.nativeProductionRules.version==2 && old.entryRejection(infantry,grinder,EntryKind::Garrison)==EntryRejectReason::Unsupported;
    }
    check(compatible,"a synthesized Scene28 production-v2 fixture retains historical intake after removing later weapon metadata");
    const auto reactor=world.spawnBuilding(0,BldType::BioReactor,20,20,true);
    const auto infantry=world.spawnUnit(0,UnitType::Initiate,19.5f,21.5f);
    order(World::Cmd::Garrison,{infantry},reactor);finish(infantry);
    const auto fileName=(std::filesystem::path(outputDir)/"source-economy-app11.sav").string();
    const auto saved=world.checksum();
    check(saveGameFile(fileName.c_str()) && loadGameFile(fileName.c_str()) && world.checksum()==saved
        && world.nativeProductionRules.version==6 && world.ents[reactor].garrison.size()==1,
        "a real App11 save restores the source v6 rules, absorbed identity and power");
    shotFile.clear();std::printf("SOURCE_ECONOMY failures=%d\n",failures);return failures;
}
