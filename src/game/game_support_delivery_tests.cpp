#include "game_regression.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>

// Exercise physical delivery through the same player command as the sidebar.
int GameRegression::supportDeliveryRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* label){failures+=!ok;std::printf("SUPPORT_DELIVERY %s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);};
    for(int choice=0;choice<3;++choice) {
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;
        selectRulesChoice(choice==0?RulesChoice::Fusion:choice==1?RulesChoice::RA2:RulesChoice::YR);
        cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        const bool initialized=world.init(64,64,2117,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,profile,{Country::America,Country::Russia});
        check(initialized,"a real two-house rules world initializes for support delivery");if(!initialized)continue;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        for(auto& player:world.players){player.isAI=false;player.defeated=false;}
        world.cratesEnabled=false;world.crates.clear();world.rebuildUnitOcc();
        world.spawnBuilding(0,BldType::ConYard,3,3,true);world.spawnBuilding(1,BldType::ConYard,55,55,true);
        world.spawnBuilding(0,BldType::TechAirport,10,12,true);
        const auto base=world;
        world.players[0].support[0]={world.supportRule(SupportType::Paradrop).rechargeTicks,true};
        const auto admissionHash=world.checksum();
        check(world.supportRejection(0,SupportType::Paradrop,3.5f,3.5f)==SuperweaponRejectReason::None && world.checksum()==admissionHash,
            "support admission preserves a building target with nearby legal landing cells without mutating the world");
        const auto fileName=[&](const std::string& name,const char* suffix){return (std::filesystem::path(outputDir)/(std::to_string(choice)+"-"+name+suffix)).string();};
        const auto snapshot=[&](const std::string& name){
            const auto raw=fileName(name,"-world57.sav"),app=fileName(name,"-app11.sav");
            FILE* file=std::fopen(raw.c_str(),"wb");bool exact=file && world.saveGame(file);if(file)std::fclose(file);
            World clone;file=std::fopen(raw.c_str(),"rb");exact &= file && clone.loadGame(file);if(file)std::fclose(file);
            auto replay=std::make_unique<Game>();exact &= saveGameFile(app.c_str()) && replay->loadGameFile(app.c_str());
            exact &= world.checksum()==clone.checksum() && world.checksum()==replay->world.checksum();
            for(int i=0;i<120 && exact;++i){world.update();clone.update();replay->world.update();
                exact &= world.checksum()==clone.checksum() && world.checksum()==replay->world.checksum();}
            check(exact,"actual World57 and App11 support states resume exactly for 120 ticks");
            return exact;
        };
        const auto renderStage=[&](const char* name){
            camZoom=1;camX=-650;camY=650;shotAtDisplaySize=true;world.map.reveal(0,32,32,64);bakeTerrain();
            bool readonly=true;for(const auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){
                SetWindowSize(size.x,size.y);const auto hash=world.checksum();const auto path=fileName(std::string(name)+"-"+std::to_string(size.x),".png");shotFile=path;render();
                Image image=LoadImage(path.c_str());readonly &= image.data && image.width==size.x && image.height==size.y && world.checksum()==hash;UnloadImage(image);
            }
            shotAtDisplaySize=false;shotFile.clear();check(readonly,"both display sizes render real support actors without advancing simulation");
        };
        const auto count=[&](bool air){int n=0;for(const auto& e:world.ents)if(e.alive && e.player==0 && !e.isBuilding && unitDef(e).isAir()==air)++n;return n;};
        const auto launch=[&]{world.players[0].support[0]={world.supportRule(SupportType::Paradrop).rechargeTicks,true};
            World::Cmd cmd{};cmd.type=World::Cmd::Paradrop;cmd.a=1;cmd.x=32.5f;cmd.y=32.5f;world.bindCommand(cmd);world.applyCmd(0,cmd);};
        launch();check(count(false)==0 && count(true)==1,"a drop command launches one actual aircraft before creating ground infantry");
        snapshot("approach");
        EID plane=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && unitDef(world.ents[id]).isAir())plane=id;
        if(world.valid(plane))world.damage(plane,100000,1);
        for(int i=0;i<1800;++i)world.update();
        check(count(false)==0 && count(true)==0,"destroying an inbound transport loses its undropped payload");
        launch();bool descending=false;
        for(int i=0;i<1800 && !descending;++i){world.update();for(const auto& e:world.ents)if(e.alive && !e.isBuilding && unitDef(e).isInfantry() && e.airborne())descending=true;}
        check(descending,"delivered infantry exists in the air before landing");
        EID falling=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].supportAir==2){falling=id;break;}
        if(world.valid(falling)) {
            auto& unit=world.ents[falling];const auto before=unit.nativeAltitude;
            world.update();world.update();world.update();world.update();
            check(unit.nativeAltitude==before-1 && unit.supportFallSpeed==-2,"the first two original falling frames apply velocities zero and minus one before acceleration");
            const auto move=world.orderMove({falling},40.5f,40.5f,false);
            check(move.accepted==0 && unit.airborne() && unit.occCell<0 && unit.airBucket>=0,"a falling passenger rejects ground movement and belongs only to the air occupancy index");
        } else {check(false,"a falling passenger is available for physics verification");check(false,"a falling passenger is available for occupancy verification");}
        snapshot("descent");renderStage("descent");
        for(int i=0;i<2400;++i)world.update();
        bool landed=count(false)==6;for(const auto& e:world.ents)if(e.alive && !e.isBuilding && unitDef(e).isInfantry())landed &= !e.airborne() && e.occCell>=0;
        check(landed && count(true)==0,"all six surviving passengers land and the transport leaves the map");
        snapshot("landed");
        const auto stable=world.checksum();
        std::ifstream input(fileName("approach","-app11.sav"),std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(input)),{});
        const auto header=bytes.rfind("DLVR");bool rejected=header!=std::string::npos && header+69<bytes.size();
        for(int offset:{4,8,17,25,26,51,69}) {
            auto corrupt=bytes;if(rejected)corrupt[header+offset]=(char)255;
            const auto path=fileName("corrupt-"+std::to_string(offset),".sav");FILE* file=std::fopen(path.c_str(),"wb");
            const bool wrote=file && std::fwrite(corrupt.data(),1,corrupt.size(),file)==corrupt.size();if(file)std::fclose(file);
            rejected &= wrote && !loadGameFile(path.c_str()) && world.checksum()==stable;
        }
        check(rejected,"corrupt mission versions, flags, generations, phases and falling state reject atomically in App11");
        // Branch from the same legal pre-order world for loss and terrain cases.
        world=base;world.map.bldOccRef=&world.bldOcc;launch();
        EID inbound=world.supportMissions.empty()?-1:world.supportMissions[0].aircraft.id;
        if(world.valid(inbound)) {
            const auto& planeActor=world.ents[inbound];const float defenseX=planeActor.x<world.map.w/2.f?planeActor.x+2:planeActor.x-2;
            const auto ground=world.spawnUnit(1,UnitType::Conscript,defenseX,planeActor.y);
            check(world.attackRejection(ground,inbound)==AttackRejectReason::NoAntiAir,"an ordinary ground-only weapon cannot attack the support aircraft");
            EID flak=-1;for(int i=0;i<10;++i)flak=world.spawnUnit(1,UnitType::FlakTrooper,defenseX-(i%5),planeActor.y+(i<5?-2:2));
            for(int i=0;i<300 && world.valid(inbound);++i){world.update();if(choice==2 && i%30==0){const auto& f=world.ents[flak];const auto gun=world.effWeapon(f,&world.ents[inbound]);
                std::printf("SUPPORT_DELIVERY_AA_TRACE frame=%d hp=%d plane=%.3f,%.3f flak=%.3f,%.3f state=%d cd=%d fire=%d projectiles=%zu range=%.3f aa=%d target=%d\n",i,world.ents[inbound].hp,world.ents[inbound].x,world.ents[inbound].y,f.x,f.y,(int)f.state,f.atkCd,f.fireAnim,world.projs.size(),gun.range,(int)gun.antiAir,f.target);}}
            std::printf("SUPPORT_DELIVERY_AA choice=%d planeAlive=%d hp=%d flak=%d state=%d target=%d sourceSecondary=%d\n",choice,(int)world.valid(inbound),world.ents[inbound].hp,flak,(int)world.ents[flak].state,world.ents[flak].target,(int)unitDef(world.ents[flak]).hasAirWeapon);
            check(!world.valid(inbound) && count(false)==0,"actual autonomous source anti-air fire destroys the inbound transport before its payload arrives");
        } else {check(false,"an inbound transport exists for ground weapon rejection");check(false,"an inbound transport exists for actual anti-air interception");}
        world=base;world.map.bldOccRef=&world.bldOcc;launch();
        while(world.tick<base.tick+1200 && std::none_of(world.ents.begin(),world.ents.end(),[](const auto& e){return e.alive && e.supportAir==2;}))world.update();
        EID victim=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].supportAir==2){victim=id;break;}
        if(world.valid(victim))world.damage(victim,100000,1);
        for(int i=0;i<2600;++i)world.update();
        check(victim>=0 && count(false)==5,"a killed parachuting passenger is lost and is never recreated on landing");
        world=base;world.map.bldOccRef=&world.bldOcc;launch();
        for(int i=0;i<600;++i)world.update();
        for(auto& cell:world.map.cells)cell.terrain=Terrain::Water;
        for(int i=0;i<2200;++i)world.update();
        check(count(false)==0,"a drop zone that becomes water during descent loses its passengers instead of spawning ground units on water");
        world=base;world.map.bldOccRef=&world.bldOcc;
        const auto source=world.bldAt(10,12);launch();if(world.valid(source))world.kill(source,false);
        for(int i=0;i<2400;++i)world.update();
        check(count(false)==6 && !world.hasSupportSource(0,SupportType::Paradrop),"losing the source building preserves an already launched physical mission");
        world=base;world.map.bldOccRef=&world.bldOcc;
        // A two-level navigation fixture, rather than a flat Terrain::Bridge
        // tile, verifies that descent chooses the real upper walking surface.
        for(auto& cell:world.map.cells)cell.terrain=Terrain::Water;
        world.map.bridgeCells.resize(world.map.cells.size());
        for(int y=28;y<=37;++y)for(int x=24;x<=40;++x)world.map.bridgeCells[(size_t)y*world.map.w+x]={0,6,0};
        launch();for(int i=0;i<2400;++i)world.update();bool onDeck=count(false)==6;
        for(const auto& e:world.ents)if(e.alive && !e.isBuilding && e.player==0 && unitDef(e).isInfantry())onDeck &= e.navLayer==1 && !e.airborne() && e.occCell>=0;
        check(onDeck,"a two-level bridge fixture receives surviving passengers on its upper walking surface");
        for(const auto faction:{Faction::Soviet,Faction::Yuri,Faction::China}) {
            if((choice==1 && faction==Faction::Yuri) || (choice && faction==Faction::China))continue;
            world=base;world.map.bldOccRef=&world.bldOcc;matchSetup.reset();
            const auto country=faction==Faction::Soviet?Country::Russia:faction==Faction::Yuri?Country::Yuri:Country::China;
            world.players[0].faction=faction;world.players[0].country=country;cfgCountry=(int)country;
            launch();snapshot("payload-"+std::to_string((int)faction));for(int i=0;i<2400;++i)world.update();
            const auto expected=faction==Faction::Soviet?UnitType::Conscript:faction==Faction::Yuri?UnitType::Initiate:UnitType::PLA;
            bool typed=count(false)==(faction==Faction::Soviet?9:6);
            for(const auto& e:world.ents)if(e.alive && !e.isBuilding && e.player==0)typed &= e.utype==expected && !e.airborne();
            check(typed,"a physical generic drop delivers its own original faction payload after descent");
        }
        world=base;world.map.bldOccRef=&world.bldOcc;matchSetup.reset();
        world.players[0].faction=Faction::Soviet;world.players[0].country=Country::Russia;cfgCountry=(int)Country::Russia;
        int american=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id=="AMRADR")american=i;
        world.spawnBuilding(0,BldType::AirForceCmd,20,6,true,american);
        // Fusion models the American country capability directly; captured
        // source identity is tested in the two original rules profiles.
        if(!choice){world.players[0].faction=Faction::Allies;world.players[0].country=Country::America;cfgCountry=(int)Country::America;}
        world.players[0].support[1]={world.supportRule(SupportType::AmericanParadrop).rechargeTicks,true};
        World::Cmd americanDrop{};americanDrop.type=World::Cmd::Paradrop;americanDrop.a=2;americanDrop.x=32.5f;americanDrop.y=32.5f;world.bindCommand(americanDrop);world.applyCmd(0,americanDrop);
        snapshot("captured-american");for(int i=0;i<2400;++i)world.update();bool americanPayload=count(false)==8;
        for(const auto& e:world.ents)if(e.alive && !e.isBuilding && e.player==0)americanPayload &= e.utype==UnitType::GI && !e.airborne();
        check(americanPayload,choice?"the independently launched captured American support retains all eight GI under a Soviet house":"American support physically delivers all eight GI after descent");
        if(choice!=1) {
            world=base;world.map.bldOccRef=&world.bldOcc;matchSetup.reset();cfgCountry=(int)world.players[0].country;
            if(!choice){world.players[0].faction=Faction::Soviet;world.players[0].country=Country::Russia;cfgCountry=(int)Country::Russia;}
            int radar=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id=="NARADR")radar=i;
            world.spawnBuilding(0,BldType::Radar,20,6,true,radar);
            world.players[0].support[2]={world.supportRule(SupportType::SpyPlane).rechargeTicks,true};
            for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
            World::Cmd spy{};spy.type=World::Cmd::Paradrop;spy.a=3;spy.x=32.5f;spy.y=32.5f;world.bindCommand(spy);world.applyCmd(0,spy);
            check(world.supportMissions.size()==1 && world.map.fogAt(0,32,32)==FOG_UNSEEN,"spy support launches a real aircraft without instantly revealing its distant target");
            snapshot("spyplane");bool revealed=false,continuous=false;
            for(int i=0;i<1200 && !world.supportMissions.empty();++i) {
                world.update();revealed |= world.map.fogAt(0,32,32)==FOG_VISIBLE;
                if(world.supportMissions.empty())break;
                const auto& m=world.supportMissions.front();
                if(world.valid(m.aircraft)) {
                    const auto& scout=world.ents[m.aircraft.id];
                    if(distf(scout.x,scout.y,m.x,m.y)<12 && world.map.fogAt(0,(int)scout.x,(int)scout.y+4)==FOG_VISIBLE)continuous=true;
                }
            }
            world.update();
            check(revealed && continuous && world.supportMissions.empty() && world.map.fogAt(0,32,32)==FOG_SEEN,
                "the spy aircraft updates its moving camera footprint and leaves explored fog after departure");
        }
    }
    World historical;FILE* legacy=std::fopen("tests/fixtures/legacy-support-world56.sav","rb");
    bool compatible=legacy && historical.loadGame(legacy);if(legacy)std::fclose(legacy);
    compatible &= !historical.supportDeliveryEnabled && historical.independentSupportEnabled && historical.nativeProductionRules.version==4;
    World migrated;FILE* saved=tmpfile();compatible &= saved && historical.saveGame(saved);
    if(saved){rewind(saved);compatible &= migrated.loadGame(saved);std::fclose(saved);}
    compatible &= historical.checksum()==migrated.checksum();
    for(int i=0;i<180 && compatible;++i){historical.update();migrated.update();compatible &= historical.checksum()==migrated.checksum();}
    historical.players[0].support[0]={historical.supportRule(SupportType::Paradrop).rechargeTicks,true};
    const auto infantryCount=[&]{int n=0;for(const auto& e:historical.ents)n += e.alive && !e.isBuilding && unitDef(e).isInfantry() && !e.airborne();return n;};
    const int before=infantryCount();const bool used=historical.orderSupport(0,SupportType::Paradrop,40.5f,40.5f);
    check(compatible && used && infantryCount()-before==6 && historical.supportMissions.empty(),"an actual World56 retains immediate support after a World57 resave and exact continuation");
    sim=SimInput{};shotFile.clear();shotAtDisplaySize=false;
    std::printf("SUPPORT_DELIVERY failures=%d\n",failures);return failures;
}
