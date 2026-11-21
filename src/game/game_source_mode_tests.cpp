#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

int GameRegression::sourceModeRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("SOURCE_MODE %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto fresh=[&](World& w,const char* edition,Faction faction,SkirmishMode mode,bool refinery=true){
        w.skirmishMode=mode;const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        if(!w.init(64,64,1951,1,1,{faction,Faction::Soviet},0,nullptr,false,false,profile.c_str()))return false;
        w.ents.clear();w.freeList.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto& c=w.map.at(x,y);c.height=0;c.ore=c.oreMax=0;c.blocked=false;c.overlay=Overlay::None;c.terrain=x<35?Terrain::Clear:Terrain::Water;}
        for(auto& p:w.players){p.isAI=false;p.money=100000;p.defeated=false;}w.cratesEnabled=false;w.rebuildUnitOcc();w.rebuildAirOcc();
        w.spawnBuilding(0,BldType::ConYard,3,3,true);w.spawnBuilding(0,BldType::WarFactory,12,3,true);
        w.spawnBuilding(0,BldType::Barracks,4,11,true);w.spawnBuilding(0,BldType::BattleLab,19,11,true);
        if(refinery)w.spawnBuilding(0,BldType::OreRefinery,4,20,true);
        w.spawnBuilding(0,BldType::Radar,19,3,true);w.spawnBuilding(0,BldType::AirForceCmd,26,3,true);
        w.spawnBuilding(0,BldType::NavalYard,39,4,true);
        const auto power=faction==Faction::Allies?BldType::PowerPlant:faction==Faction::Soviet?BldType::TeslaReactor:BldType::BioReactor;
        for(int i=0;i<5;++i)w.spawnBuilding(0,power,5+i*5,28,true);
        return w.valid(w.spawnBuilding(1,BldType::ConYard,22,48,true));
    };
    const auto admission=[&](const World& source,bool unit,int base,bool allowed,const char* label){
        World w=source;w.map.bldOccRef=&w.bldOcc;const int selector=w.productionSelector(0,unit,base);
        World::Cmd cmd{};cmd.type=unit?World::Cmd::StartUnitProd:World::Cmd::StartBldProd;cmd.a=selector;
        w.applyProductionCommand(0,cmd);bool accepted=false;
        for(const auto& e:w.presentationEvents)if(e.kind==PresentationKind::ProductionOrder && e.production.action==ProductionAction::Start)accepted=e.production.accepted;
        const bool visible=selector>=0 && w.nativeProductionVisible(0,selector);
        const bool mode=unit?w.modeAllowsUnit(0,(UnitType)base):w.modeAllowsBuilding(0,(BldType)base);
        std::printf("SOURCE_MODE ADMISSION %s unit=%d base=%d allowed=%d visible=%d mode=%d accepted=%d\n",label,unit,base,allowed,visible,mode,accepted);
        // Admission mutates queues: the direct result is authoritative; the
        // public visibility and mode queries must agree before prerequisites.
        const bool publicStart=source.productionStartRejection(0,unit,base)==ProductionRejectReason::None;
        check(selector>=0 && accepted==allowed && visible==allowed && mode==allowed && publicStart==allowed,label);
    };
    for(const char* edition:{"ra2","yr"})for(auto faction:{Faction::Allies,Faction::Soviet,Faction::Yuri}) {
        if(faction==Faction::Yuri && std::string(edition)=="ra2")continue;
        std::printf("SOURCE_MODE CASE edition=%s faction=%d\n",edition,(int)faction);
        if(faction==Faction::Allies){
            World normal;const bool loaded=fresh(normal,edition,faction,SkirmishMode::Battle);
            int index=normal.rulesTypeIndex(0,false,(int)BldType::AirForceCmd);
            check(loaded && index>=0 && normal.nativeTypes[index]->id=="AMRADR","an ordinary American source opening selects its required AMRADR air command variant");
            admission(normal,false,(int)BldType::AirForceCmd,true,"an ordinary American source base can actually order its air command");
            normal.players[0].country=Country::Korea;index=normal.rulesTypeIndex(0,false,(int)BldType::AirForceCmd);
            check(index>=0 && normal.nativeTypes[index]->id=="GAAIRC","a Korean source base continues to select GAAIRC");
            admission(normal,false,(int)BldType::AirForceCmd,true,"an ordinary Korean source base can actually order its air command");
        }
        World meat;bool ok=fresh(meat,edition,faction,SkirmishMode::MeatGrinder);check(ok,"source facilities and funds are supplied only for the mode-admission mechanism fixture");if(!ok)continue;
        const auto advanced=faction==Faction::Allies?UnitType::PrismTank:faction==Faction::Soviet?UnitType::Apocalypse:UnitType::MasterMind;
        admission(meat,true,(int)advanced,false,"original Meat Grinder disables this advanced unit through the source mode overlay");
        admission(meat,false,(int)BldType::BattleLab,false,"original Meat Grinder disables the source battle laboratory");
        admission(meat,false,(int)BldType::NavalYard,false,"original Meat Grinder disables the source naval yard");
        const auto basic=faction==Faction::Allies?UnitType::Grizzly:faction==Faction::Soviet?UnitType::Rhino:UnitType::LasherTank;
        admission(meat,true,(int)basic,true,"ordinary source combat production remains legal in Meat Grinder");
        if(faction==Faction::Allies){
            admission(meat,false,(int)BldType::AirForceCmd,true,"the original Meat Grinder overlay retains the Allied air command building");
            admission(meat,true,(int)UnitType::Intruder,false,"the original Meat Grinder overlay specifically disables ORCA");
            meat.players[0].country=Country::Korea;
            admission(meat,true,(int)UnitType::BlackEagle,true,"original Meat Grinder does not blanket-disable the Korean BEAG entry");
        }
        World naval;ok=fresh(naval,edition,faction,SkirmishMode::NavalWar);check(ok,"source Naval War admission fixture opens");
        admission(naval,true,(int)basic,false,"original Naval War disables the source main battle tank");
        const auto ship=faction==Faction::Allies?UnitType::Destroyer:faction==Faction::Soviet?UnitType::Typhoon:UnitType::Boomer;
        admission(naval,true,(int)ship,true,"original Naval War retains paid source ship production");
        World mega;ok=fresh(mega,edition,faction,SkirmishMode::Megawealth,false);check(ok,"source Megawealth fixture has power and barracks but no supplied refinery");
        admission(mega,false,(int)BldType::OreRefinery,false,"original Megawealth disables the source refinery");
        admission(mega,true,(int)harvesterType(faction),false,"original Megawealth disables the source miner");
        admission(mega,false,(int)BldType::WarFactory,true,"the original Megawealth prerequisite override allows a factory without a refinery");
        World battle;ok=fresh(battle,edition,faction,SkirmishMode::Battle);battle.superweaponsEnabled=false;
        const auto super=faction==Faction::Allies?BldType::WeatherDevice:faction==Faction::Soviet?BldType::NukeSilo:BldType::PsychicDominator;
        admission(battle,false,(int)super,false,"the superweapon match option is enforced by source visibility and direct production commands");
        // A permitted order must physically complete and retain its source identity.
        const int before=meat.countUnits(0,basic);const auto money=meat.players[0].money;meat.startUnitProd(0,basic);
        for(int frame=0;frame<2500 && meat.countUnits(0,basic)==before;++frame)meat.update();
        bool typed=false;for(const auto& e:meat.ents)if(e.alive && !e.isBuilding && e.player==0 && e.utype==basic)typed=e.nativeIdentity.definition && e.nativeIdentity.definition->rulesOnly;
        check(typed && meat.countUnits(0,basic)==before+1 && meat.players[0].money<money,"a permitted source mode order pays and produces one actual typed combat unit");
        for(auto [mode,label,state]:{std::tuple{SkirmishMode::MeatGrinder,"meat",&meat},
                                    std::tuple{SkirmishMode::NavalWar,"naval",&naval},
                                    std::tuple{SkirmishMode::Megawealth,"mega",&mega}}){
            const auto path=(std::filesystem::path(outputDir)/(std::string(edition)+"-"+std::to_string((int)faction)+"-"+label+"-world56.sav")).string();
            FILE* file=std::fopen(path.c_str(),"wb");bool same=file && state->saveGame(file);if(file)std::fclose(file);
            World copy;file=std::fopen(path.c_str(),"rb");same &= file && copy.loadGame(file);if(file)std::fclose(file);
            same &= copy.skirmishMode==mode && copy.nativeProductionRules.sourceModes && copy.checksum()==state->checksum();
            for(int frame=0;frame<180 && same;++frame){state->update();copy.update();same &= state->checksum()==copy.checksum();}
            check(same,"an actual World55 mode snapshot rebuilds the selected source overrides and resumes exactly");
        }
    }
    for(const char* edition:{"ra2","yr"})for(auto mode:{SkirmishMode::MeatGrinder,SkirmishMode::NavalWar,SkirmishMode::Megawealth}){
        selectRulesChoice(std::string(edition)=="ra2"?RulesChoice::RA2:RulesChoice::YR);
        cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgReferenceMap=false;cfgGameMode=(int)mode;cfgMoney=10000;
        cfgCrates=false;cfgAlliance=false;cfgSuperweapons=false;cfgShortGame=false;
        aiCountry[0]=(int)(std::string(edition)=="ra2"?Country::Russia:Country::Yuri);aiDiff[0]=(int)AIDiff::Normal;aiPersonality[0]=(int)AIPersonality::Balanced;
        newGame(1981);bool started=phase==Phase::InGame && world.skirmishMode==mode && world.nativeProductionRules.sourceModes;
        if(mode==SkirmishMode::MeatGrinder){const int index=world.rulesTypeIndex(0,false,(int)BldType::BattleLab);started &= index>=0 && world.nativeTypes[index]->production.techLevel==11;}
        if(mode==SkirmishMode::NavalWar)started &= world.countUnits(0,UnitType::Grizzly)==0 && world.countUnits(1,UnitType::Rhino)==0 && world.countUnits(1,UnitType::LasherTank)==0;
        check(started,"the actual Game opening chooses source mode definitions before creating its starting actors");if(!started)continue;
        if(mode==SkirmishMode::Megawealth){
            bool factory=false,army=false,legal=true;
            for(int frame=0;frame<18000 && world.matchOutcome(1)==MatchOutcome::Running;++frame){world.update();for(auto& bot:ais)bot.update(world);
                factory |= world.hasBld(1,BldType::WarFactory);army |= world.producedUnits[1]>0;
                legal &= !world.hasBld(1,BldType::OreRefinery) && world.countUnits(1,harvesterType(world.players[1].faction))==0;
                world.presentationEvents.clear();world.evaQueue.clear();}
            std::printf("SOURCE_MODE NATURAL edition=%s tick=%llu factory=%d army=%d legal=%d money=%lld\n",edition,(unsigned long long)world.tick,factory,army,legal,(long long)world.players[1].money);
            check(factory && army && legal,"an ordinary 10000-credit Megawealth AI opening develops paid military production without a refinery or miner");
        }
        const auto path=(std::filesystem::path(outputDir)/(std::string(edition)+"-mode-"+std::to_string((int)mode)+"-app11.sav")).string();Game copy;
        bool same=saveGameFile(path.c_str()) && copy.loadGameFile(path.c_str()) && copy.world.nativeProductionRules.sourceModes
            && copy.world.skirmishMode==mode && copy.cfgGameMode==(int)mode && copy.world.checksum()==world.checksum() && copy.ais==ais;
        for(int frame=0;frame<180 && same;++frame){world.update();for(auto& bot:ais)bot.update(world);copy.world.update();for(auto& bot:copy.ais)bot.update(copy.world);
            same &= world.checksum()==copy.world.checksum() && ais==copy.ais;
            world.presentationEvents.clear();world.evaQueue.clear();copy.world.presentationEvents.clear();copy.world.evaQueue.clear();}
        check(same,"an actual App11 mode checkpoint restores setup, effective source rules and AI decisions");
        if(mode==SkirmishMode::NavalWar){
            std::ifstream input(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{});
            const auto header=bytes.find("[Rules.Mode.5.Override.0]");const auto field=bytes.find("TechLevel = 11",header);
            bool corrupt=header!=std::string::npos && field!=std::string::npos;
            const auto invalid=(std::filesystem::path(outputDir)/(std::string(edition)+"-invalid-inactive-mode-app11.sav")).string();
            if(corrupt){bytes.replace(field+12,2,"xx");FILE* file=std::fopen(invalid.c_str(),"wb");corrupt=file && std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();if(file)std::fclose(file);}
            const auto held=world.checksum();check(corrupt && !loadGameFile(invalid.c_str()) && world.checksum()==held,
                "a malformed inactive source mode in a real App11 file is rejected without changing the live match");
        }
    }
    for(auto rules:{RulesChoice::Fusion,RulesChoice::RA2,RulesChoice::YR}) {
        selectRulesChoice(rules);cfgCountry=(int)Country::America;cfgAI=1;aiCountry[0]=(int)Country::Russia;
        cfgMapSize=64;cfgMapType=2;cfgReferenceMap=false;cfgGameMode=(int)SkirmishMode::LandRush;cfgMoney=10000;
        sim={};sim.active=true;sim.pos={-100,-100};newGame(20261119);
        bool legal=phase==Phase::InGame;int vehicles=0;
        for(EID id=0;id<(EID)world.ents.size();++id){const auto& e=world.ents[id];
            if(!e.alive || e.isBuilding || e.utype!=UnitType::MCV)continue;
            ++vehicles;legal &= world.map.passable((int)e.x,(int)e.y) && world.canDeployMcv(id)
                && e.x==e.px && e.y==e.py && world.unitAtCell((int)e.x,(int)e.y,e.navLayer)==id;
        }
        check(legal && vehicles==2,"the normal Land Rush lake opening creates deployable, indexed MCVs with matching interpolation positions");
        if(!legal)continue;
        const auto path=(std::filesystem::path(outputDir)/("land-rush-"+std::to_string((int)rules)+"-app11.sav")).string();Game copy;
        bool exact=saveGameFile(path.c_str()) && copy.loadGameFile(path.c_str()) && copy.world.checksum()==world.checksum();
        for(int frame=0;frame<90 && exact;++frame){world.update();copy.world.update();exact &= world.checksum()==copy.world.checksum();}
        check(exact,"the actual Land Rush application opening saves and resumes the same simulation");
    }
    for(int edition=0;edition<3;++edition)for(int terrain=0;terrain<7;++terrain)for(int players:{2,8}) {
        std::vector<Faction> factions;std::vector<Country> countries;
        const int sides=edition==0?4:edition==1?2:3;
        for(int p=0;p<players;++p){const auto side=edition==2 && p%3==2?Faction::Yuri:(Faction)(p%sides);
            factions.push_back(side);countries.push_back(side==Faction::Allies?Country::America:side==Faction::Soviet?Country::Russia:side==Faction::Yuri?Country::Yuri:Country::China);}
        const char* profile=edition==1?"assets/rules/profiles/ra2.ini":edition==2?"assets/rules/profiles/yr.ini":nullptr;
        World first;first.skirmishMode=SkirmishMode::LandRush;
        bool legal=first.init(96,96,941,0,players,factions,terrain,nullptr,false,false,profile,countries);
        int vehicles=0;
        for(EID id=0;id<(EID)first.ents.size();++id){const auto& e=first.ents[id];if(!e.alive || e.isBuilding)continue;
            legal &= first.map.passable((int)e.x,(int)e.y) && first.unitAtCell((int)e.x,(int)e.y,e.navLayer)==id && e.x==e.px && e.y==e.py;
            if(e.utype==UnitType::MCV){++vehicles;legal &= first.canDeployMcv(id);}
        }
        World second;second.skirmishMode=SkirmishMode::LandRush;
        legal &= second.init(96,96,941,0,players,factions,terrain,nullptr,false,false,profile,countries) && second.checksum()==first.checksum();
        std::vector<std::pair<int,EID>> mcvs;
        for(EID id=0;id<(EID)first.ents.size();++id){const auto& e=first.ents[id];if(e.alive && !e.isBuilding && e.utype==UnitType::MCV)mcvs.push_back({e.player,id});}
        for(const auto [player,id]:mcvs){World::Cmd command;command.type=World::Cmd::Deploy;command.ids={id};
            first.applyCmd(player,command);second.applyCmd(player,command);
            legal &= first.countBlds(player,BldType::ConYard)==1 && second.countBlds(player,BldType::ConYard)==1;}
        legal &= first.checksum()==second.checksum();
        std::printf("SOURCE_MODE LAND_RUSH edition=%d terrain=%d players=%d vehicles=%d legal=%d\n",edition,terrain,players,vehicles,legal);
        check(legal && vehicles==players,"Land Rush armies spawn legally and every MCV actually deploys deterministically across all map families and two/eight players");
    }
    return failures;
}
