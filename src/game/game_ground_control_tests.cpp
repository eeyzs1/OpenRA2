#include "game/game.h"
#include "game_regression.h"
#include "game/world_priv.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "fixtures/native_ground_reference.h"
#include "world_save_sections.h"
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <optional>
#include <tuple>

int GameRegression::groundControlRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    g_script.shutdown();ais.clear();netGame=false;phase=Phase::InGame;localPlayer=0;
    constexpr int firstDumpTicks = 32, dumpInterval = 30;
    constexpr int singleExitTicks = firstDumpTicks + dumpInterval;
    constexpr int mixedExitTicks = singleExitTicks + dumpInterval;
    constexpr int exitHandoffTicks = 34, linearApronSteps = 26, linearExitTicks = 52;
    constexpr int driveDispatchTick = singleExitTicks + exitHandoffTicks;
    constexpr int referenceFirstMove = 102, referenceApronEntry = 144, referenceApronCenter = 164;
    constexpr float linearExitStep = 5.f/256.f, linearFirstExitX = 23.5f + linearExitStep;
    auto check=[&](bool ok,const char* label){std::printf("GROUND %s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    auto type=[&](const char* name){for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name)return i;return -1;};
    auto fresh=[&](){
        if(!world.init(64,64,711,2,0,{Faction::Soviet,Faction::Allies},0,"maps/skirmish-reference.txt",true,true,
            "assets/rules/profiles/ra2.ini",{Country::Russia,Country::America}))return false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;world.crates.clear();for(auto& p:world.players)p.isAI=false;
        world.spawnBuilding(0,BldType::ConYard,3,3,true);world.spawnBuilding(1,BldType::ConYard,50,50,true);
        world.players[0].money=10000;return true;
    };
    auto order=[&](World::Cmd::Type kind,std::vector<EID> ids,EID target=INVALID_EID,float x=0,float y=0){
        World::Cmd c{};c.type=kind;c.ids=std::move(ids);c.a=target;c.x=x;c.y=y;world.bindCommand(c);world.applyCmd(0,c);
    };
    const auto orderExitFixtureHarvest=[&](EID id,Vec2i field) {
        auto& ore=world.map.at(field.x,field.y);
        ore.ore=ore.oreMax=80;
        World::Cmd harvest{};
        harvest.type=World::Cmd::Harvest;harvest.ids={id};harvest.a=field.x;harvest.b=field.y;
        world.bindCommand(harvest);world.applyCmd(0,harvest);
        const auto& actor=world.ents[id];
        return actor.autoHarvest && actor.state==UState::HarvestGo && actor.oreCell==field
            && !actor.path.empty() && actor.path.back()==field;
    };
    auto advance=[&](int ticks){for(int i=0;i<ticks;++i)world.update();};
    auto roundTrip=[&](const std::string& name,int ticks=90,const char* expectedHeader=nullptr){
        const auto path=(std::filesystem::path(outputDir)/(name+".sav")).string();
        FILE* f=std::fopen(path.c_str(),"wb");bool ok=f && world.saveGame(f);if(f)std::fclose(f);
        World copy;f=std::fopen(path.c_str(),"rb");
        if(f && expectedHeader) {
            uint8_t header[13]{};
            ok=ok && std::fread(header,1,sizeof header,f)==sizeof header && worldSaveHeaderMatches(header,expectedHeader);
            std::rewind(f);
        }
        ok=ok && f && copy.loadGame(f);if(f)std::fclose(f);
        ok=ok && copy.checksum()==world.checksum();World source=world;source.map.bldOccRef=&source.bldOcc;
        for(int i=0;i<ticks && ok;++i){source.update();copy.update();ok=source.checksum()==copy.checksum();}
        return ok;
    };
    auto capture=[&](EID actor,const char* name){
        world.map.reveal(0,(int)world.ents[actor].x,(int)world.ents[actor].y,24);
        camZoom=1;camX=(world.ents[actor].x-world.ents[actor].y)*TILE_W/2-(SCREEN_W-sidebarW)/2.f;
        camY=(world.ents[actor].x+world.ents[actor].y)*TILE_H/2-SCREEN_H/2.f;
        interpAlpha=1;fogMaskTick=-1;bakeTerrain();sel={actor};
        const auto path=(std::filesystem::path(outputDir)/(std::string(name)+".png")).string();const auto before=world.checksum();
        shotFile=path;render();shotFile.clear();const auto rect=unitScreenRect(world.ents[actor]);
        check(FileExists(path.c_str()) && before==world.checksum() && pickUnit((int)(rect.x+rect.width/2),(int)(rect.y+rect.height/2))==actor,
            "the actual dock/turn pose renders and remains selectable without mutating simulation");
    };
    for(const auto& reference:OriginalGroundReference::facing) {
        NativeFacingState state;state.start=state.target=reference.start;
        NativeGroundMath::desire(state,reference.target,reference.rate,0);bool same=true;
        for(int frame=0;frame<33;++frame) {
            const auto target=frame>=reference.retargetFrame && reference.retargetFrame>=0?reference.retarget:reference.target;
            // Repeated unchanged requests must not restart the turn timer.
            NativeGroundMath::desire(state,target,reference.rate,frame);
            same &= NativeGroundMath::current(state,frame)==reference.raw[frame];
        }
        std::printf("GROUND_REFERENCE rate=%d case=%d samples=33 exact=%d\n",reference.rate,reference.caseId,same);
        check(same,"original facing samples match, including half-turn ties, wrapping and mid-turn retargets");
    }
    if(!fresh())return 1;
    const auto ref=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
    const auto miner=world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));
    world.ents[miner].oreLoad=40;order(World::Cmd::ReturnHarvester,{miner},ref);
    world.update();world.update();
    check(world.players[0].money==10000,"no unloading credit while the vehicle is outside the actual bay");
    bool partial=false,inside=false;int paidAt=0;
    for(int elapsed=3;elapsed<=600;++elapsed){
        world.update();const auto& e=world.ents[miner];
        partial|=e.oreLoad>0 && e.oreLoad<40;
        if(world.players[0].money>10000){paidAt=elapsed;inside=std::abs(e.x-23.5f)<.01f && std::abs(e.y-21.5f)<.01f;break;}
    }
    check(paidAt>firstDumpTicks && inside && !partial && world.players[0].money==11000,"a loaded HARV enters the bay and credits one original bulk load after the dump gate");
    // Each phase is reached through the same return order used by the HUD/AI.
    if(!fresh())return 1;
    const auto bay=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
    const auto vehicle=world.spawnUnit(0,UnitType::WarMiner,29.5f,25.5f,type("HARV"));
    const Vec2i rememberedField{28,21};
    world.ents[vehicle].dir=world.ents[vehicle].turretDir=7;world.ents[vehicle].oreLoad=40;
    const bool harvestAdmitted=orderExitFixtureHarvest(vehicle,rememberedField);
    order(World::Cmd::ReturnHarvester,{vehicle},bay);
    unsigned phases=0;bool exact=true,forwardEntry=false,forward=false,parked=true;int enteredAt=0,creditedAt=0,leftAt=0;
    const auto dockIdentity=world.entityRef(bay);
    const auto exitLayer=world.ents[bay].navLayer;
    auto previousIncome=world.harvestedIncome[0];
    const auto initialDockIncome=previousIncome;
    int dockDeposits=0,dispatchAt=-1,firstMoveAt=-1,firstApronAt=-1,releasedAt=-1;
    bool stationaryExit=true,exitClaim=true,dispatchClock=false,firstMotion=false,resumedHarvest=false,rollingSaved=false;
    std::optional<World> waitingExit,drivingExit;
    struct DriveSample {int tick,x,speed;double throttle;};
    // Original archive-occupancy trace. Compare after both 30 Hz halves of a Drive frame.
    constexpr DriveSample driveSamples[]{
        {97,6016,0,.03},{99,6016,0,.06},{101,6016,0,.09},{103,6017,1,.12},
        {109,6025,2,.21},{129,6074,5,.51},{143,6136,7,.72},{145,6147,7,.75},
        {161,6251,9,.99},{163,6267,10,1.},
    };
    int checkedSamples=0;
    double largestSampleError=0;
    bool driveSamplesMatch=true;
    std::ofstream trajectory(std::filesystem::path(outputDir)/"dock-trajectory.csv");
    trajectory<<"elapsed,phase,x,y,heading,cargo,cash,raw_x,throttle,speed,half,credit\n"<<std::setprecision(12);
    for(int elapsed=0;elapsed<1000;++elapsed) {
        const auto& e=world.ents[vehicle];const auto p=e.nativeDock.phase;const unsigned flag=1u<<(unsigned)p;
        const auto heading=world.nativeHullHeading(e,world.tick);
        const auto& drive=e.nativeMovement;
        const bool driveExit=p==NativeDockPhase::DriveExit;
        trajectory<<elapsed<<','<<(int)p<<','<<e.x<<','<<e.y<<','<<heading<<','<<e.oreLoad<<','<<world.players[0].money
            <<','<<e.x*256.<<','<<drive.throttle<<','<<drive.heldSpeed<<','<<(int)drive.half<<','<<drive.credit<<'\n';
        if(p!=NativeDockPhase::None && !(phases&flag)){
            phases|=flag;exact &= roundTrip("dock-phase-"+std::to_string((int)p),180,driveExit?"RA2WLD73":nullptr);
            if(p==NativeDockPhase::Unload)capture(vehicle,"docked-harv");
            if(driveExit) {
                capture(vehicle,"leaving-harv");
                waitingExit.emplace(world);
            }
        }
        if(p==NativeDockPhase::ForwardEntry && e.x<e.px && (int)e.x==24)forwardEntry |= heading==49152;
        if(driveExit && e.x>e.px)forward |= heading==16384;
        if(p==NativeDockPhase::Unload) {
            if(!enteredAt)enteredAt=elapsed;
            parked &= e.x==23.5f && e.y==21.5f;
        }
        if(world.harvestedIncome[0]!=previousIncome) {
            ++dockDeposits;
            previousIncome=world.harvestedIncome[0];
        }
        if(!creditedAt && world.players[0].money>10000)creditedAt=elapsed;
        if(driveExit) {
            if(!leftAt)leftAt=elapsed;
            const int age=elapsed-leftAt;
            const int sinceUnload=elapsed-enteredAt;
            exitClaim &= e.nativeDock.receiver==dockIdentity && e.claimsNativeDock() && e.navLayer==exitLayer
                && e.y==21.5f && heading==16384 && e.oreLoad==0 && e.gemLoad==0
                && world.unitAtCell((int)e.x,21,exitLayer)==vehicle && world.players[0].money==11000
                && e.autoHarvest && e.oreCell==rememberedField;
            if(age<exitHandoffTicks)
                stationaryExit &= e.x==23.5f && e.digTimer==age-exitHandoffTicks
                    && e.waitingForNativeDeparture() && !drive.started;
            if(drive.started && dispatchAt<0) {
                dispatchAt=elapsed;
                dispatchClock=e.x==23.5f && e.digTimer==0 && !e.waitingForNativeDeparture()
                    && drive.lastTick==world.tick && drive.half==1 && drive.heldSpeed==0 && drive.credit==0
                    && std::abs(drive.throttle-.03)<.000001;
            }
            if(firstMoveAt<0 && e.x!=23.5f) {
                firstMoveAt=elapsed;
                firstMotion=e.x>23.5f && std::abs(e.x*256.-6017.)<=1. && e.digTimer==0;
            }
            if(checkedSamples<(int)std::size(driveSamples) && sinceUnload==driveSamples[checkedSamples].tick) {
                const auto& sample=driveSamples[checkedSamples++];
                const auto error=std::abs(e.x*256.-sample.x);
                largestSampleError=std::max(largestSampleError,error);
                driveSamplesMatch &= error<=2. && drive.started && drive.lastTick==world.tick && drive.half==0
                    && drive.heldSpeed==sample.speed && std::abs(drive.throttle-sample.throttle)<.000001;
            }
            if(sinceUnload==130 && drive.started && e.x>23.5f && e.x<24.f) {
                rollingSaved=roundTrip("dock-exit-driving",40,"RA2WLD73");
                drivingExit.emplace(world);
            }
            if(firstApronAt<0 && (int)e.x==24)firstApronAt=elapsed;
        }
        if(p==NativeDockPhase::None && e.oreLoad==0) {
            releasedAt=elapsed;
            exitClaim &= !e.claimsNativeDock() && e.nativeDock.receiver==World::EntityRef{}
                && e.x>=24.5f && e.x<24.55f && e.y==21.5f && world.unitAtCell(24,21,exitLayer)==vehicle
                && world.players[0].money==11000 && world.harvestedIncome[0]==initialDockIncome+1000;
            resumedHarvest=e.state==UState::HarvestGo && e.oreCell==rememberedField
                && !e.path.empty() && e.path.back()==rememberedField && e.x>24.5f
                && drive.started && drive.lastTick==world.tick && drive.nodeX==25 && drive.nodeY==21
                && drive.throttle>.95;
            break;
        }
        world.update();
    }
    const unsigned livePhases=(1u<<(unsigned)NativeDockPhase::Approach) | (1u<<(unsigned)NativeDockPhase::ForwardEntry)
        | (1u<<(unsigned)NativeDockPhase::Unload) | (1u<<(unsigned)NativeDockPhase::DriveExit);
    check(phases==livePhases && exact,"approach, forward entry, dumping and exit each survive an exact World save");
    check(forwardEntry && forward && parked && creditedAt-enteredAt==firstDumpTicks && leftAt-creditedAt==dumpInterval,
        "a War Miner drives west into its bay, stays parked for both dump cycles and drives east to leave");
    std::printf("GROUND_DOCK_HANDOFF credit=%d exit=%d dispatch=%d first_move=%d first_apron=%d released=%d "
        "stationary=%d claim=%d deposits=%d harvest=%d resumed=%d samples=%d error_leptons=%.4f rolling_save=%d\n",
        creditedAt-enteredAt,leftAt-enteredAt,dispatchAt-enteredAt,firstMoveAt-enteredAt,firstApronAt-enteredAt,
        releasedAt-enteredAt,stationaryExit,exitClaim,dockDeposits,harvestAdmitted,resumedHarvest,checkedSamples,
        largestSampleError,rollingSaved);
    check(harvestAdmitted && leftAt && stationaryExit && exitClaim && firstMotion && dockDeposits==1 && resumedHarvest
        && dispatchClock && dispatchAt-enteredAt==driveDispatchTick && firstMoveAt-enteredAt==referenceFirstMove
        && std::abs(firstApronAt-enteredAt-referenceApronEntry)<=2 && std::abs(releasedAt-enteredAt-referenceApronCenter)<=2
        && driveSamplesMatch && checkedSamples==(int)std::size(driveSamples) && rollingSaved,
        "the normal War Miner accelerates from its reserved berth and carries travel credit into its admitted ore route");
    if(waitingExit) {
        const auto restoreExit=[&] {
            world=*waitingExit;
            world.map.bldOccRef=&world.bldOcc;
        };
        restoreExit();
        const auto output=std::filesystem::path(outputDir);
        const auto file=(output/"dock-exit-wait-app11.sav").string();
        const auto original=world.checksum();
        Game restored;
        bool app=false,rejected=false,replayed=false;
        if(saveGameFile(file.c_str()) && restored.loadGameFile(file.c_str()) && restored.world.checksum()==original) {
            std::ifstream stream(file,std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
            const auto embedded=embeddedWorldSaveOffset(bytes);
            app=embedded && std::memcmp(bytes.data(),"RA2GAM11",8)==0
                && worldSaveHeaderMatches(std::span(bytes).subspan(*embedded),"RA2WLD73");
            if(app) {
                std::memcpy(bytes.data()+*embedded,"RA2WLD72",8);
                const auto bad=(output/"dock-exit-wait-world72-app11.sav").string();
                std::ofstream out(bad,std::ios::binary);
                out.write((const char*)bytes.data(),bytes.size());out.close();
                rejected=out && !restored.loadGameFile(bad.c_str()) && restored.world.checksum()==original;
                replayed=true;
                for(int t=0;t<referenceApronCenter+3-singleExitTicks && replayed;++t) {
                    world.update();
                    restored.world.update();
                    replayed=world.checksum()==restored.world.checksum();
                }
            }
        }
        restoreExit();
        advance(8);
        const auto& interrupted=world.ents[vehicle];
        const auto beforeRepeat=world.checksum();
        order(World::Cmd::ReturnHarvester,{vehicle},bay);
        const bool repeated=interrupted.digTimer==8-exitHandoffTicks && world.checksum()==beforeRepeat;
        order(World::Cmd::Stop,{vehicle});
        const bool stopped=interrupted.state==UState::Idle && interrupted.x==23.5f && !interrupted.autoHarvest;
        world.update();
        const bool escaped=interrupted.x==linearFirstExitX && interrupted.digTimer==0
            && interrupted.nativeDock.phase==NativeDockPhase::Exit && interrupted.nativeMovement==NativeMovementState{}
            && interrupted.nativeDock.receiver==dockIdentity && interrupted.claimsNativeDock();
        advance(linearExitTicks-1);
        const bool stoppedAtApron=interrupted.x==24.5f && interrupted.y==21.5f
            && interrupted.nativeDock.phase==NativeDockPhase::None && interrupted.state==UState::Idle
            && interrupted.oreLoad==0 && world.harvestedIncome[0]==initialDockIncome+1000;
        std::printf("GROUND_DOCK_HANDOFF_SAVE app11=%d rejected72=%d replay=%d repeated=%d stop=%d escaped=%d apron=%d\n",
            app,rejected,replayed,repeated,stopped,escaped,stoppedAtApron);
        check(app && rejected && replayed,
            "App11 restores the War Miner Drive exit and rejects a World72 header without changing the active game");
        check(repeated && stopped && escaped && stoppedAtApron,
            "reissuing the same refinery keeps the dispatch timer while Stop allows immediate physical departure");

        restoreExit();
        const auto otherBay=world.spawnBuilding(0,BldType::OreRefinery,38,20,true,type("GAREFN"));
        const auto chrono=world.spawnUnit(0,UnitType::ChronoMiner,44.5f,21.5f,type("CMIN"));
        bool mixedSave=false;
        if(world.valid(otherBay) && world.valid(chrono)) {
            auto& unloading=world.ents[chrono];
            unloading.dir=unloading.turretDir=NativeGroundMath::dir(16384);
            unloading.oreLoad=20;
            order(World::Cmd::ReturnHarvester,{chrono},otherBay);
            for(int t=0;t<8 && !world.harvesterUnloading(unloading);++t)world.update();
            const auto& departing=world.ents[vehicle];
            mixedSave=world.harvesterUnloading(unloading) && unloading.digTimer==-2
                && departing.nativeDock.phase==NativeDockPhase::DriveExit && departing.waitingForNativeDeparture()
                && roundTrip("dock-exit-wait-with-unload-startup",120,"RA2WLD73");
        }
        check(mixedSave,"a World73 exit timer and another miner's active unload startup both resume exactly");
    }
    const auto groupMoveFromDock=[&](const World& snapshot,const char* stage,const char* sourceName) {
        constexpr Vec2i berth{23,21},apron{24,21},island{11,18},islandSlot{35,19},goal{35,21};
        const auto source=(std::filesystem::path(outputDir)/sourceName).string();
        World loaded;
        FILE* file=std::fopen(source.c_str(),"rb");
        uint8_t header[13]{};
        bool loaded73=false;
        if(file) {
            loaded73=std::fread(header,1,sizeof header,file)==sizeof header && worldSaveHeaderMatches(header,"RA2WLD73");
            std::rewind(file);
            loaded73=loaded73 && loaded.loadGame(file) && loaded.valid(vehicle);
            std::fclose(file);
        }
        if(!loaded73) {
            std::printf("GROUND_DOCK_GROUP_MOVE stage=%s loaded73=0\n",stage);
            return false;
        }
        world=std::move(loaded);
        world.map.bldOccRef=&world.bldOcc;
        for(int y=island.y-1;y<=island.y+1;++y)
            for(int x=island.x-1;x<=island.x+1;++x)
                if(Vec2i{x,y}!=island)world.map.at(x,y).terrain=Terrain::Water;
        const EID isolated=world.spawnUnit(0,UnitType::WarMiner,island.x+.5f,island.y+.5f,type("HARV"));
        if(!world.valid(isolated)) {
            std::printf("GROUND_DOCK_GROUP_MOVE stage=%s loaded73=1 isolated=0\n",stage);
            return false;
        }
        const auto& e=world.ents[vehicle];
        const auto& first=world.ents[isolated];
        const auto& prior=snapshot.ents[vehicle];
        const auto& def=unitDef(e);
        const int domain=def.pathDomain();
        const bool underBridge=def.fitsUnderBridge();
        const uint8_t gateAccess=world.nativeGateAccess(e);
        // The balance survives a load. The diagnostic income counter starts over.
        const auto incomeAfterLoad=world.harvestedIncome[0];
        const auto holdsDock=[&] {
            return e.claimsNativeDock() && e.nativeDock.receiver==dockIdentity && e.navLayer==exitLayer
                && world.unitAtCell((int)e.x,berth.y,exitLayer)==vehicle;
        };
        const auto emptyAfterPayment=[&] {
            return e.oreLoad==0 && e.gemLoad==0 && world.players[0].money==snapshot.players[0].money
                && world.harvestedIncome[0]==incomeAfterLoad;
        };
        const bool started=e.nativeDock.phase==NativeDockPhase::DriveExit && e.digTimer==prior.digTimer
            && e.nativeMovement==prior.nativeMovement && e.x==prior.x && (int)e.x==berth.x && e.y==berth.y+.5f
            && e.autoHarvest && holdsDock() && emptyAfterPayment()
            && snapshot.players[0].money==11000 && snapshot.harvestedIncome[0]==initialDockIncome+1000;
        const bool sameProfile=unitDef(first).pathDomain()==domain && unitDef(first).fitsUnderBridge()==underBridge
            && world.nativeGateAccess(first)==gateAccess && first.navLayer==exitLayer;
        std::vector<Vec2i> route;
        const bool isolatedRoute=!world.map.findPath(island.x,island.y,islandSlot.x,islandSlot.y,route,
            20000,domain,nullptr,exitLayer,exitLayer,underBridge,gateAccess);
        const bool usableApron=world.bldBlocked(berth.x,berth.y,exitLayer)
            && world.map.surfacePassable(islandSlot.x,islandSlot.y,domain,exitLayer,true,underBridge,gateAccess)
            && world.map.stepLayer(berth.x,berth.y,exitLayer,apron.x,apron.y,domain,true,underBridge,gateAccess)==exitLayer
            && world.map.findPath(berth.x,berth.y,goal.x,goal.y,route,20000,domain,nullptr,exitLayer,exitLayer,underBridge,gateAccess)
            && !route.empty() && route.front()==apron && route.back()==goal;

        World solo=world;
        solo.map.bldOccRef=&solo.bldOcc;
        World::Cmd soloMove{};
        soloMove.type=World::Cmd::Move;
        soloMove.ids={vehicle};
        soloMove.x=goal.x+.5f;
        soloMove.y=goal.y+.5f;
        solo.bindCommand(soloMove);
        solo.applyCmd(0,soloMove);
        const auto& single=solo.ents[vehicle];
        const bool soloAdmitted=single.state==UState::Moving && !single.path.empty()
            && single.path.front()==apron && single.path.back()==goal;

        // Eastward formation tries the unreachable northern slot first. Its failed route builds the shared component map.
        order(World::Cmd::Move,{isolated,vehicle},0,35.5f,20.5f);
        const auto move=!world.presentationEvents.empty() && world.presentationEvents.back().kind==PresentationKind::MoveOrder
            ? world.presentationEvents.back().move : MoveOrderResult{};
        const auto hasMovePath=[&] {
            return e.state==UState::Moving && !e.autoHarvest && e.goalLayer==exitLayer
                && !e.path.empty() && e.path.front()==apron && e.path.back()==goal;
        };
        const bool accepted=std::find(move.acceptedUnits.begin(),move.acceptedUnits.end(),vehicle)!=move.acceptedUnits.end();
        const bool admitted=accepted && hasMovePath() && e.nativeDock.phase==NativeDockPhase::DriveExit
            && e.digTimer==prior.digTimer && e.nativeMovement==prior.nativeMovement && e.x==prior.x && holdsDock();
        bool saved=false,demoted=false,evacuated=false,arrived=false;
        if(admitted) {
            saved=roundTrip(std::string("dock-group-")+stage+"-ordered",linearExitTicks+8,"RA2WLD73");
            world.update();
            demoted=e.nativeDock.phase==NativeDockPhase::Exit && e.digTimer==0 && e.nativeMovement==NativeMovementState{}
                && std::abs(e.x-prior.x-linearExitStep)<.00001f;
            bool safe=true;
            for(int elapsed=1;e.exitingNativeDock() && elapsed<linearExitTicks;++elapsed) {
                const float previousX=e.x;
                safe &= holdsDock() && hasMovePath() && emptyAfterPayment() && e.y==berth.y+.5f
                    && world.nativeHullHeading(e,world.tick)==16384;
                world.update();
                safe &= e.x>=previousX && e.x<=apron.x+.5f;
            }
            evacuated=safe && e.nativeDock.phase==NativeDockPhase::None && !e.claimsNativeDock()
                && e.x==apron.x+.5f && e.y==apron.y+.5f && world.unitAtCell(apron.x,apron.y,exitLayer)==vehicle
                && hasMovePath() && emptyAfterPayment();
            if(evacuated) {
                for(int elapsed=0;elapsed<720 && e.state==UState::Moving;++elapsed)world.update();
                arrived=e.state==UState::Idle && e.x==goal.x+.5f && e.y==goal.y+.5f
                    && world.unitAtCell(goal.x,goal.y,exitLayer)==vehicle && emptyAfterPayment()
                    && first.x==island.x+.5f && first.y==island.y+.5f && first.path.empty();
            }
        }
        std::printf("GROUND_DOCK_GROUP_MOVE stage=%s loaded73=1 started=%d same_profile=%d isolated_route=%d apron=%d solo=%d "
            "accepted=%d adjusted=%d rejected=%d reason=%d failed=%d miner=%d miner_accepted=%d admitted=%d "
            "saved=%d demoted=%d evacuated=%d arrived=%d x=%.4f y=%.4f state=%d path=%d/%zu\n",
            stage,started,sameProfile,isolatedRoute,usableApron,soloAdmitted,move.accepted,move.adjusted,move.rejected,(int)move.reason,
            move.failedEntity,vehicle,accepted,admitted,saved,demoted,evacuated,arrived,e.x,e.y,(int)e.state,e.pathIdx,e.path.size());
        return started && sameProfile && isolatedRoute && usableApron && soloAdmitted && admitted
            && saved && demoted && evacuated && arrived;
    };
    check(waitingExit && groupMoveFromDock(*waitingExit,"waiting","dock-phase-7.sav"),
        "a grouped Move admits the waiting War Miner and survives a save before physical departure");
    check(drivingExit && groupMoveFromDock(*drivingExit,"driving","dock-exit-driving.sav"),
        "a grouped Move from inside the berth keeps the driving War Miner's route after evacuation");
    waitingExit.reset();
    if(drivingExit) {
        world=std::move(*drivingExit);
        world.map.bldOccRef=&world.bldOcc;
        const Vec2i moveGoal{27,21};
        const auto& interrupted=world.ents[vehicle];
        const float start=interrupted.x;
        order(World::Cmd::Move,{vehicle},0,moveGoal.x+.5f,moveGoal.y+.5f);
        const bool admitted=interrupted.state==UState::Moving && !interrupted.path.empty()
            && interrupted.path.back()==moveGoal && !interrupted.autoHarvest;
        world.update();
        const bool demoted=interrupted.nativeDock.phase==NativeDockPhase::Exit && interrupted.claimsNativeDock()
            && interrupted.nativeDock.receiver==dockIdentity && interrupted.nativeMovement==NativeMovementState{}
            && std::abs(interrupted.x-start-linearExitStep)<.00001f;
        for(int t=0;t<linearExitTicks && interrupted.exitingNativeDock();++t)world.update();
        const bool evacuated=interrupted.nativeDock.phase==NativeDockPhase::None && interrupted.x==24.5f
            && interrupted.y==21.5f && interrupted.state==UState::Moving && !interrupted.path.empty()
            && interrupted.path.back()==moveGoal && world.harvestedIncome[0]==initialDockIncome+1000;
        std::printf("GROUND_DOCK_DRIVE_MOVE admitted=%d demoted=%d evacuated=%d\n",admitted,demoted,evacuated);
        check(admitted && demoted && evacuated,"Move during an accelerated exit preserves its route after physical evacuation");
    }
    drivingExit.reset();
    struct ArchivedExit {const char* environment;const char* header;const char* name;bool waiting;};
    constexpr ArchivedExit oldExits[]{
        {"OPENRA2_LEGACY_DOCK_EXIT_INPUT","RA2WLD64","legacy-dock-exit",false},
        {"OPENRA2_LEGACY_DOCK_WAIT_INPUT","RA2WLD72","legacy-dock-wait",true},
    };
    for(const auto& old:oldExits)if(const char* path=std::getenv(old.environment)) {
        World historical;
        FILE* input=std::fopen(path,"rb");
        char header[8]{};
        const bool version=input && std::fread(header,1,sizeof header,input)==sizeof header
            && std::memcmp(header,old.header,sizeof header)==0;
        if(version)std::rewind(input);
        const bool loaded=version && historical.loadGame(input);
        if(input)std::fclose(input);
        bool active=false,replayed=false,linear=true,cleared=false;
        const int firstStep=old.waiting?exitHandoffTicks:1;
        const int releaseStep=firstStep+linearExitTicks-1;
        int oldFirstMove=-1,oldFirstApron=-1,oldRelease=-1;
        if(loaded) {
            world=std::move(historical);
            world.map.bldOccRef=&world.bldOcc;
            const World::EntityRef oldRefinery{2,1},oldMiner{3,1};
            if(world.valid(oldRefinery) && world.valid(oldMiner)) {
                const auto& actor=world.ents[oldMiner.id];
                const auto income=world.harvestedIncome[0];
                active=world.tick==474 && actor.utype==UnitType::WarMiner && actor.nativeDock.phase==NativeDockPhase::Exit
                    && actor.nativeDock.receiver==oldRefinery && actor.claimsNativeDock()
                    && actor.digTimer==(old.waiting?-exitHandoffTicks:0)
                    && actor.x==23.5f && actor.y==21.5f && actor.oreLoad==0 && world.players[0].money==11000;
                replayed=active && roundTrip(old.name,releaseStep,old.header);
                for(int t=1;t<=releaseStep;++t) {
                    world.update();
                    const int moves=std::max(0,t-firstStep+1);
                    const float expected=std::min(24.5f,23.5f+moves*linearExitStep);
                    linear &= actor.x==expected && actor.y==21.5f && actor.nativeMovement==NativeMovementState{};
                    if(t<releaseStep)linear &= actor.nativeDock.phase==NativeDockPhase::Exit
                        && actor.nativeDock.receiver==oldRefinery && actor.claimsNativeDock();
                    if(oldFirstMove<0 && actor.x>23.5f)oldFirstMove=t;
                    if(oldFirstApron<0 && (int)actor.x==24)oldFirstApron=t;
                    if(oldRelease<0 && actor.nativeDock.phase==NativeDockPhase::None)oldRelease=t;
                }
                cleared=actor.x==24.5f && actor.y==21.5f && actor.nativeDock.phase==NativeDockPhase::None
                    && !actor.claimsNativeDock() && world.players[0].money==11000 && world.harvestedIncome[0]==income;
            }
        }
        std::printf("GROUND_DOCK_EXIT_LEGACY version=%s loaded=%d active=%d replay=%d linear=%d "
            "first_move=%d first_apron=%d released=%d cleared=%d\n",
            old.header,loaded,active,replayed,linear,singleExitTicks+oldFirstMove,singleExitTicks+oldFirstApron,
            singleExitTicks+oldRelease,cleared);
        check(loaded && active && replayed && linear && cleared && oldFirstMove==firstStep
            && oldFirstApron==firstStep+linearApronSteps-1 && oldRelease==releaseStep,
            "an actual archived War Miner exit keeps its linear departure, reservation and prior credit");
    }
    {
        if (!fresh()) return 1;
        constexpr uint16_t west = 49152, east = 16384;
        constexpr uint16_t firstEastTurnSample = 0xbfee; // Local 1.08 ROT5 west-to-east request.
        const EID receiver = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("NAREFN"));
        const EID inbound = world.spawnUnit(0, UnitType::WarMiner, 25.5f, 21.5f, type("HARV"));
        if (!world.valid(receiver) || !world.valid(inbound)) return failures + 1;
        auto& actor = world.ents[inbound];
        actor.dir = actor.turretDir = NativeGroundMath::dir(west);
        actor.oreLoad = 40;
        const auto identity = world.entityRef(inbound);
        const auto refinery = world.entityRef(receiver);
        const auto layer = world.ents[receiver].navLayer;
        const auto initialCash = world.players[0].money;
        const auto initialIncome = world.harvestedIncome[0];
        const auto start = world.nativeHullHeading(actor, world.tick);
        const int rot = actor.nativeIdentity.definition ? actor.nativeIdentity.definition->production.rotationRate : 0;
        order(World::Cmd::ReturnHarvester, {inbound}, receiver);
        const bool admitted = start == west && rot == 5 && actor.state == UState::HarvestReturn && actor.nativeDock.receiver == refinery;
        int bayTick = 0, eastRequest = 0, eastTick = 0, unloadTick = 0, payTick = 0, exitTick = 0, doneTick = 0, deposits = 0;
        float bayX = 0, requestX = 0, requestY = 0;
        int requestLayer = -1;
        uint16_t bayHeading = 0, requestHeading = 0;
        bool held = true, stationary = true, earlyCredit = false, forwardExit = false, legalPayment = false;
        bool enteredWest = false, redirectedOutside = false, savedApproach = false, savedTurning = false, checkedApp = false;
        std::optional<World> turningInBay;
        for (int elapsed = 1; elapsed <= 600; ++elapsed) {
            const auto priorIncome = world.harvestedIncome[0];
            world.update();
            const auto dockPhase = actor.nativeDock.phase;
            const auto heading = world.nativeHullHeading(actor, world.tick);
            if (!bayTick && (int)actor.x == 23 && (int)actor.y == 21 && actor.navLayer == layer
                && world.unitAtCell(23, 21, layer) == inbound) {
                bayTick = elapsed;
                bayX = actor.x;
                bayHeading = heading;
            }
            if (!eastRequest && actor.nativeGround.initialized && actor.nativeGround.hull.target == east) {
                eastRequest = elapsed;
                requestX = actor.x;
                requestY = actor.y;
                requestLayer = actor.navLayer;
                requestHeading = heading;
            }
            if (!eastTick && heading == east) eastTick = elapsed;
            if (!enteredWest && dockPhase == NativeDockPhase::ForwardEntry && heading == west && actor.x < actor.px && (int)actor.x == 24) {
                enteredWest = true;
                savedApproach = roundTrip("dock-west-forward-entry", 180, "RA2WLD67");
                World rerouted = world;
                rerouted.map.bldOccRef = &rerouted.bldOcc;
                const auto alternate = rerouted.spawnBuilding(0, BldType::OreRefinery, 29, 25, true, type("NAREFN"));
                if (rerouted.valid(alternate)) {
                    World::Cmd request{};
                    request.type = World::Cmd::ReturnHarvester;
                    request.ids = {inbound};
                    request.a = alternate;
                    rerouted.bindCommand(request);
                    rerouted.applyCmd(0, request);
                    const auto& diverted = rerouted.ents[inbound];
                    const auto destination = rerouted.entityRef(alternate);
                    redirectedOutside = diverted.nativeDock.phase == NativeDockPhase::Approach && !diverted.claimsNativeDock()
                        && diverted.nativeDock.receiver == destination && diverted.pendingDockRefinery == destination
                        && diverted.x == actor.x && diverted.y == actor.y && diverted.navLayer == layer
                        && diverted.nativeGround.hull == actor.nativeGround.hull && diverted.oreLoad == actor.oreLoad
                        && rerouted.harvestedIncome[0] == initialIncome;
                }
            }
            if (!turningInBay && dockPhase == NativeDockPhase::ForwardEntry && actor.x == 23.5f && actor.y == 21.5f
                && heading != east && actor.nativeGround.hull.target == east && actor.oreLoad == 40
                && world.harvestedIncome[0] == initialIncome) {
                turningInBay.emplace(world);
                savedTurning = roundTrip("dock-west-turning-in-bay", 180, "RA2WLD67");
                const auto file = std::filesystem::path(outputDir) / "dock-west-app11.sav";
                const auto checksum = world.checksum();
                Game restored;
                const bool restoredApp = saveGameFile(file.string().c_str()) && restored.loadGameFile(file.string().c_str())
                    && restored.world.checksum() == checksum;
                std::ifstream state(file, std::ios::binary);
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(state), {}};
                const auto nested = embeddedWorldSaveOffset(bytes);
                const auto redirect = nested ? locateWorldSaveSection(std::span(bytes).subspan(*nested),
                    WorldSaveSection::NativeDockRedirect) : std::nullopt;
                if (restoredApp && redirect && redirect->length == 8) {
                    bytes.resize(*nested + redirect->offset);
                    checkedApp = true;
                    for (char revision : {'7', '4'}) {
                        bytes[*nested + 7] = static_cast<uint8_t>(revision);
                        const auto invalid = std::filesystem::path(outputDir) / (std::string("dock-west-bad-world6") + revision + ".sav");
                        { std::ofstream out(invalid, std::ios::binary);out.write((const char*)bytes.data(), bytes.size()); }
                        checkedApp &= !restored.loadGameFile(invalid.string().c_str()) && restored.world.checksum() == checksum;
                    }
                }
            }
            if (dockPhase != NativeDockPhase::None) {
                held &= actor.nativeDock.receiver == refinery && actor.dockRefinery == receiver;
                if (bayTick) held &= actor.claimsNativeDock();
            }
            if (dockPhase == NativeDockPhase::Unload) {
                if (!unloadTick) unloadTick = elapsed;
                stationary &= world.harvesterUnloading(actor) && actor.x == 23.5f && actor.y == 21.5f && heading == east;
            }
            if (!unloadTick) earlyCredit |= world.players[0].money != initialCash || world.harvestedIncome[0] != initialIncome;
            if (world.harvestedIncome[0] != priorIncome) {
                ++deposits;
                payTick = elapsed;
                legalPayment = dockPhase == NativeDockPhase::Unload && actor.x == 23.5f && actor.y == 21.5f && actor.oreLoad == 0;
            }
            if (dockPhase == NativeDockPhase::Exit) {
                if (!exitTick) exitTick = elapsed;
                forwardExit |= actor.x > actor.px && heading == east;
            }
            if (exitTick && dockPhase == NativeDockPhase::None && actor.state == UState::Idle) { doneTick = elapsed; break; }
        }
        const bool finished = world.valid(identity) && doneTick > exitTick && actor.x == 24.5f && actor.y == 21.5f && actor.oreLoad == 0
            && world.players[0].money == initialCash + 1000 && world.harvestedIncome[0] == initialIncome + 1000;
        const bool eastInside = eastRequest == bayTick && (int)requestX == 23 && (int)requestY == 21
            && requestLayer == layer && bayHeading == firstEastTurnSample && requestHeading == firstEastTurnSample;
        std::printf("GROUND_DOCK_WEST admitted=%d rot=%d east_request=%d:%.5f,%.5f:%d,%d,%d:%u bay=%d:%.5f:%u "
            "east=%d unload=%d pay=%d exit=%d released=%d held=%d deposits=%d finished=%d east_inside=%d\n",
            admitted, rot, eastRequest, requestX, requestY, (int)requestX, (int)requestY, requestLayer, (unsigned)requestHeading,
            bayTick, bayX, (unsigned)bayHeading, eastTick, unloadTick, payTick, exitTick, doneTick, held, deposits, finished, eastInside);
        check(admitted && bayTick > 0 && eastRequest > 0 && eastTick >= eastRequest && unloadTick >= eastTick
            && held && stationary && !earlyCredit && deposits == 1 && legalPayment && payTick - unloadTick == firstDumpTicks
            && exitTick - payTick == dumpInterval && forwardExit && finished,
            "a west-facing loaded War Miner holds its bay, unloads once while facing east and drives out at the normal cadence");
        check(bayTick > 0 && eastRequest > 0 && eastInside,
            "a west-facing War Miner requests the final east-facing pose on the tick it enters the refinery bay cell");
        check(enteredWest && savedApproach && savedTurning,
            "a westbound approach and an unfinished turn in the bay both resume from exact World67 saves");
        check(checkedApp, "App11 restores World67 and rejects a missing redirect block or an extended pose tagged World64 atomically");
        check(redirectedOutside,
            "a new refinery order before entering the bay releases its claim and plans a return from the current pose");
        if (turningInBay) for (bool redirect : {false, true}) {
            world = *turningInBay;
            world.map.bldOccRef = &world.bldOcc;
            const EID alternate = redirect ? world.spawnBuilding(0, BldType::OreRefinery, 29, 25, true, type("NAREFN")) : INVALID_EID;
            const auto next = world.entityRef(alternate);
            auto& interrupted = world.ents[inbound];
            const auto atCommand = world.nativeHullHeading(interrupted, world.tick);
            order(redirect ? World::Cmd::ReturnHarvester : World::Cmd::Stop, {inbound}, alternate);
            const bool admittedOrder = redirect ? world.valid(next) && interrupted.nativeDock.phase == NativeDockPhase::Exit
                && interrupted.pendingDockRefinery == next
                : interrupted.state == UState::Idle && interrupted.nativeDock.phase == NativeDockPhase::ForwardEntry
                    && !interrupted.hasPendingDockReturn() && roundTrip("dock-west-stop-command", 180, "RA2WLD67");
            if (!redirect) world.update();
            const bool startedExit = interrupted.nativeDock.phase == NativeDockPhase::Exit && interrupted.nativeDock.receiver == refinery
                && interrupted.pendingDockRefinery == next && interrupted.x == 23.5f && interrupted.y == 21.5f
                && atCommand != east && world.nativeHullHeading(interrupted, world.tick) != east
                && interrupted.oreLoad == 40 && world.harvestedIncome[0] == initialIncome;
            const bool savedExit = roundTrip(redirect ? "dock-west-redirect-turn" : "dock-west-stop-turn", 180, "RA2WLD67");
            bool safeExit = true, waitedInBay = false, turnedBeforeMoving = false;
            for (int elapsed = 0; elapsed < 180 && interrupted.nativeDock.phase == NativeDockPhase::Exit; ++elapsed) {
                world.update();
                const auto heading = world.nativeHullHeading(interrupted, world.tick);
                waitedInBay |= interrupted.x == 23.5f && interrupted.px == 23.5f && heading != east;
                if (interrupted.x > interrupted.px) turnedBeforeMoving |= heading == east;
                safeExit &= interrupted.x >= interrupted.px && interrupted.y == 21.5f && interrupted.navLayer == layer
                    && (interrupted.x == interrupted.px || heading == east)
                    && interrupted.oreLoad == 40 && world.harvestedIncome[0] == initialIncome;
            }
            const bool cleared = interrupted.x == 24.5f && interrupted.y == 21.5f && interrupted.nativeDock.receiver != refinery
                && world.nativeHullHeading(interrupted, world.tick) == east && !interrupted.claimsNativeDock()
                && (redirect ? interrupted.pendingDockRefinery == next && interrupted.state == UState::HarvestReturn
                             : !interrupted.hasPendingDockReturn() && interrupted.state == UState::Idle);
            bool paidAtNewBay = !redirect;
            if (redirect) for (int elapsed = 0; elapsed < 1200 && world.harvestedIncome[0] == initialIncome; ++elapsed) {
                world.update();
                if (world.harvestedIncome[0] != initialIncome) paidAtNewBay = interrupted.nativeDock.receiver == next
                    && world.harvesterUnloading(interrupted) && interrupted.x == 32.5f && interrupted.y == 26.5f
                    && interrupted.oreLoad == 0 && world.harvestedIncome[0] == initialIncome + 1000;
            }
            std::printf("GROUND_DOCK_WEST_EXIT redirect=%d admitted=%d started=%d saved=%d safe=%d waited=%d faced_east=%d cleared=%d paid=%d\n",
                redirect, admittedOrder, startedExit, savedExit, safeExit, waitedInBay, turnedBeforeMoving, cleared, paidAtNewBay);
            check(admittedOrder && startedExit && savedExit && safeExit && waitedInBay && turnedBeforeMoving && cleared && paidAtNewBay,
                "Stop or a new refinery order resumes an unfinished exit turn, clears the old bay facing east and preserves the load");
        }
    }
    for (const auto kind : {World::Cmd::Stop, World::Cmd::Move, World::Cmd::ReturnHarvester}) {
        if (!fresh()) return 1;
        constexpr uint16_t west = 49152, east = 16384;
        const bool moving = kind == World::Cmd::Move, redirect = kind == World::Cmd::ReturnHarvester;
        const char* name = redirect ? "redirect" : moving ? "move" : "stop";
        const int refineryType = type("GAREFN");
        const EID receiver = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, refineryType);
        const EID alternate = redirect ? world.spawnBuilding(0, BldType::OreRefinery, 38, 20, true, refineryType) : INVALID_EID;
        const EID id = world.spawnUnit(0, UnitType::ChronoMiner, 29.5f, 21.5f, type("CMIN"));
        if (!world.valid(receiver) || (redirect && !world.valid(alternate)) || !world.valid(id)) return failures + 1;
        const auto dock = world.entityRef(receiver), destination = world.entityRef(alternate);
        const auto layer = world.ents[receiver].navLayer;
        auto& actor = world.ents[id];
        actor.dir = actor.turretDir = NativeGroundMath::dir(west);
        actor.oreLoad = 20;
        const auto initialCash = world.players[0].money;
        const auto initialIncome = world.harvestedIncome[0];
        const auto unpaidLoad = [&] {
            return actor.oreLoad == 20 && world.harvestedIncome[0] == initialIncome && world.players[0].money == initialCash;
        };
        const auto inBay = [&] {
            return actor.x == 23.5f && actor.y == 21.5f && actor.navLayer == layer && world.unitAtCell(23, 21, layer) == id;
        };
        order(World::Cmd::ReturnHarvester, {id}, receiver);
        world.update();
        const bool landed = actor.nativeDock.phase == NativeDockPhase::Align && actor.nativeDock.receiver == dock
            && actor.claimsNativeDock() && inBay() && unpaidLoad() && world.nativeHullHeading(actor, world.tick) == west;

        order(kind, {id}, moving ? 0 : alternate, 29.5f, 21.5f);
        const bool admitted = inBay() && unpaidLoad() && actor.nativeDock.receiver == dock && actor.pendingDockRefinery == destination
            && world.nativeHullHeading(actor, world.tick) == west
            && (redirect ? actor.state == UState::HarvestReturn && actor.nativeDock.phase == NativeDockPhase::Exit
                         : actor.nativeDock.phase == NativeDockPhase::Align && actor.state == (moving ? UState::Moving : UState::Idle));
        if (!redirect) world.update();
        const bool exiting = actor.nativeDock.phase == NativeDockPhase::Exit && actor.nativeDock.receiver == dock
            && actor.pendingDockRefinery == destination && actor.claimsNativeDock() && inBay() && unpaidLoad()
            && world.nativeHullHeading(actor, world.tick) != east;
        const bool saved = exiting && roundTrip(std::string("chrono-berth-exit-") + name, 180, "RA2WLD69");
        if (redirect) {
            const auto app = std::filesystem::path(outputDir) / "chrono-berth-exit-app11.sav";
            const auto checksum = world.checksum();
            Game restored;
            const bool loaded = saved && saveGameFile(app.string().c_str()) && restored.loadGameFile(app.string().c_str())
                && restored.world.checksum() == checksum && restored.world.ents[id].pendingDockRefinery == destination;
            std::ifstream stream(app, std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), {}};
            const auto embedded = embeddedWorldSaveOffset(bytes);
            bool rejected = false;
            if (loaded && embedded && std::memcmp(bytes.data(), "RA2GAM11", 8) == 0
                    && worldSaveHeaderMatches(std::span(bytes).subspan(*embedded),"RA2WLD69")) {
                bytes[*embedded + 7] = '8';
                const auto bad = std::filesystem::path(outputDir) / "chrono-berth-exit-bad-world68.sav";
                bool written = false;
                {
                    std::ofstream out(bad, std::ios::binary);
                    written = bool(out.write((const char*)bytes.data(), bytes.size()));
                }
                rejected = written && !restored.loadGameFile(bad.string().c_str()) && restored.world.checksum() == checksum;
            }
            check(loaded && rejected, "App11 restores a turning Chrono Miner exit and atomically rejects its state with a World68 header");
        }
        bool physicalExit = true, turnedInBay = false, droveEast = false;
        for (int elapsed = 0; elapsed < 180 && actor.nativeDock.phase == NativeDockPhase::Exit; ++elapsed) {
            const float previous = actor.x;
            world.update();
            const auto heading = world.nativeHullHeading(actor, world.tick);
            turnedInBay |= inBay() && previous == actor.x && heading != east;
            droveEast |= actor.x > previous && heading == east;
            physicalExit &= actor.x >= previous && actor.x <= 24.5f && actor.y == 21.5f && actor.navLayer == layer
                && (actor.x == previous || heading == east) && unpaidLoad();
            if (actor.nativeDock.phase == NativeDockPhase::Exit)
                physicalExit &= actor.nativeDock.receiver == dock && actor.claimsNativeDock() && actor.pendingDockRefinery == destination;
        }
        const bool left = actor.x == 24.5f && actor.y == 21.5f && world.nativeHullHeading(actor, world.tick) == east
            && actor.nativeDock.receiver != dock && world.unitAtCell(24, 21, layer) == id
            && (!redirect || actor.nativeDock.receiver == destination);
        bool completed = false;
        if (redirect) {
            bool newLanding = false, paidInNewBay = true;
            int deposits = 0;
            for (int elapsed = 0; elapsed < 500; ++elapsed) {
                const auto previous = world.harvestedIncome[0];
                world.update();
                const bool newBay = actor.nativeDock.receiver == destination && actor.x == 41.5f && actor.y == 21.5f;
                newLanding |= newBay && actor.nativeDock.phase == NativeDockPhase::Align && unpaidLoad();
                if (world.harvestedIncome[0] != previous) {
                    ++deposits;
                    paidInNewBay &= newBay && world.harvesterUnloading(actor);
                }
                if (!actor.oreLoad && actor.nativeDock.phase == NativeDockPhase::None) break;
            }
            completed = newLanding && paidInNewBay && deposits == 1 && actor.oreLoad == 0
                && world.harvestedIncome[0] == initialIncome + 500 && world.players[0].money == initialCash + 500;
        } else {
            for (int elapsed = 0; elapsed < 500 && actor.state != UState::Idle; ++elapsed) world.update();
            completed = actor.state == UState::Idle && unpaidLoad() && actor.y == 21.5f && actor.x == (moving ? 29.5f : 24.5f);
        }
        std::printf("GROUND_CHRONO_BERTH_EXIT case=%s landed=%d admitted=%d exiting=%d saved=%d physical=%d "
            "turned=%d east=%d left=%d completed=%d\n",
            name, landed, admitted, exiting, saved, physicalExit, turnedInBay, droveEast, left, completed);
        check(landed && admitted && exiting && saved && physicalExit && turnedInBay && droveEast && left && completed
            && actor.nativeDock.phase == NativeDockPhase::None && !actor.hasPendingDockReturn(),
            "an interrupted Chrono Miner turns east inside the berth and leaves before executing its new order");
    }
    {
        if (!fresh()) return 1;
        constexpr uint16_t west = 49152, east = 16384;
        constexpr Vec2i island{11, 18}, goal{35, 21};
        for (int y = island.y - 1; y <= island.y + 1; ++y) {
            for (int x = island.x - 1; x <= island.x + 1; ++x) {
                if (Vec2i{x, y} != island) world.map.at(x, y).terrain = Terrain::Water;
            }
        }
        const EID refinery = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("GAREFN"));
        const int minerType = type("CMIN");
        const EID isolated = world.spawnUnit(0, UnitType::ChronoMiner, island.x + .5f, island.y + .5f, minerType);
        const EID returning = world.spawnUnit(0, UnitType::ChronoMiner, 29.5f, 21.5f, minerType);
        if (!world.valid(refinery) || !world.valid(isolated) || !world.valid(returning)) return failures + 1;
        auto& actor = world.ents[returning];
        const auto& first = world.ents[isolated];
        const auto dock = world.entityRef(refinery);
        actor.dir = actor.turretDir = NativeGroundMath::dir(west);
        actor.oreLoad = 20;
        actor.gemLoad = 4;
        const auto initialIncome = world.harvestedIncome[0];
        const auto initialCash = world.players[0].money;
        order(World::Cmd::ReturnHarvester, {returning}, refinery);
        world.update();
        const bool landed = actor.nativeDock.phase == NativeDockPhase::Align && actor.nativeDock.receiver == dock
            && actor.x == 23.5f && actor.y == 21.5f && actor.navLayer == 0 && world.unitAtCell(23, 21, 0) == returning
            && world.nativeHullHeading(actor, world.tick) == west;

        // The eastward formation tries (35,19) for the isolated miner first.
        // Its failed route builds the shared connectivity map before the second route.
        std::vector<Vec2i> testPath;
        const bool routes = world.bldBlocked(23, 21) && world.map.surfacePassable(35, 19, 0, 0)
            && !world.map.findPath(island.x, island.y, 35, 19, testPath)
            && world.map.findPath(24, 21, goal.x, goal.y, testPath);
        const auto issuedAt = world.tick;
        order(World::Cmd::Move, {isolated, returning}, 0, 35.5f, 20.5f);
        const auto move = !world.presentationEvents.empty() && world.presentationEvents.back().kind == PresentationKind::MoveOrder
            ? world.presentationEvents.back().move : MoveOrderResult{};
        const bool admitted = std::find(move.acceptedUnits.begin(), move.acceptedUnits.end(), returning) != move.acceptedUnits.end()
            && actor.state == UState::Moving && actor.nativeDock.phase == NativeDockPhase::Align
            && !actor.path.empty() && actor.path.back() == goal;
        world.update();
        const bool startedExit = actor.nativeDock.phase == NativeDockPhase::Exit && actor.nativeDock.receiver == dock
            && actor.x == 23.5f && actor.y == 21.5f && world.nativeHullHeading(actor, world.tick) != east;

        bool safe = true, movedEast = false, cleared = false;
        // Include the eleven-tile trip from the apron and deceleration on approach.
        for (int elapsed = 0; elapsed < 720; ++elapsed) {
            const bool exiting = actor.nativeDock.phase == NativeDockPhase::Exit;
            const float previousX = actor.x;
            world.update();
            if (exiting) {
                const auto heading = world.nativeHullHeading(actor, world.tick);
                movedEast |= actor.x > previousX && heading == east;
                safe &= actor.x >= previousX && actor.x <= 24.5f && actor.y == 21.5f
                    && (actor.x == previousX || heading == east);
                cleared |= actor.nativeDock.phase == NativeDockPhase::None && actor.x == 24.5f
                    && world.unitAtCell(24, 21, 0) == returning;
            }
            safe &= first.x == island.x + .5f && first.y == island.y + .5f && first.state == UState::Idle && first.path.empty()
                && actor.oreLoad == 20 && actor.gemLoad == 4 && world.harvestedIncome[0] == initialIncome
                && world.players[0].money == initialCash;
            if (actor.state == UState::Idle && actor.nativeDock.phase == NativeDockPhase::None) break;
        }
        const bool arrived = actor.state == UState::Idle && actor.nativeDock.phase == NativeDockPhase::None
            && actor.x == goal.x + .5f && actor.y == goal.y + .5f && world.unitAtCell(goal.x, goal.y, 0) == returning;
        std::printf("GROUND_CHRONO_BERTH_GROUP landed=%d routes=%d accepted=%d adjusted=%d rejected=%d "
            "failed_entity=%d admitted=%d exit=%d safe=%d east=%d cleared=%d arrived=%d ticks=%llu "
            "x=%.4f y=%.4f state=%d path=%d/%zu block=%d\n",
            landed, routes, move.accepted, move.adjusted, move.rejected, move.failedEntity, admitted, startedExit,
            safe, movedEast, cleared, arrived, (unsigned long long)(world.tick - issuedAt),
            actor.x, actor.y, (int)actor.state, actor.pathIdx, actor.path.size(), actor.blockTick);
        check(landed && routes && admitted && startedExit && safe && movedEast && cleared && arrived,
            "a Chrono Miner leaves its berth for a group Move when an earlier miner has no route off its island");
    }
    // Bulk rounding and ore-before-gems are frozen from actual original calls.
    struct Load {const char* id;int ore,gems,plain,bonus;};
    for(const auto load:{Load{"CMIN",20,0,500,625},Load{"HARV",40,0,1000,1250},Load{"CMIN",0,20,1000,1250},
            Load{"HARV",0,40,2000,2500},Load{"CMIN",10,10,750,937},Load{"HARV",7,3,325,405},Load{"CMIN",1,0,25,31}})for(int purifier=0;purifier<2;++purifier) {
        if(!fresh())return 1;
        const auto receiver=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        if(purifier) {
            world.spawnBuilding(0,BldType::OrePurifier,33,32,true,type("GAOREP"));
            for(int i=0;i<4;++i)world.spawnBuilding(0,BldType::TeslaReactor,35+i*4,4,true,type("NAPOWR"));
        }
        const auto mover=world.spawnUnit(0,world.nativeTypes[type(load.id)]->unit.type,29.5f,21.5f,type(load.id));
        world.ents[mover].oreLoad=load.ore+load.gems;world.ents[mover].gemLoad=load.gems;
        order(World::Cmd::ReturnHarvester,{mover},receiver);
        for(int t=0;t<800 && world.ents[mover].nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
        const auto& actor=world.ents[mover];
        const bool reached=actor.nativeDock.phase==NativeDockPhase::Unload;
        const auto dockTick=world.tick;
        const bool stockOreLoad=!purifier && !load.gems && load.ore==World::harvesterCapacity(actor.utype);
        int observedTicks=0;
        if(stockOreLoad) {
            const auto prefix=std::string("unload-start-")+load.id;
            const auto refinery=world.entityRef(receiver);
            const auto atStart=[&](int timer) {
                return world.harvesterUnloading(actor) && actor.nativeDock.receiver==refinery && actor.claimsNativeDock()
                    && actor.x==23.5f && actor.y==21.5f && world.nativeHullHeading(actor,world.tick)==0x4000
                    && actor.digTimer==timer && actor.oreLoad==load.ore && world.harvestedIncome[0]==0 && world.players[0].money==10000;
            };
            const bool minusTwo=atStart(-2) && roundTrip(prefix+"-minus-two",120,"RA2WLD71");
            bool currentApp=false, rejects70=false, rejects64=false, rejects72=false, rejects73=false;
            const auto file=(std::filesystem::path(outputDir)/(prefix+"-app11.sav")).string();
            const auto before=world.checksum();
            Game restored;
            if(saveGameFile(file.c_str()) && restored.loadGameFile(file.c_str()) && restored.world.checksum()==before) {
                std::ifstream stream(file,std::ios::binary);
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
                const auto embedded=embeddedWorldSaveOffset(bytes);
                currentApp=embedded && std::memcmp(bytes.data(),"RA2GAM11",8)==0
                    && worldSaveHeaderMatches(std::span(bytes).subspan(*embedded),"RA2WLD71");
                if(currentApp) {
                    const auto reject=[&](const char* version) {
                        std::memcpy(bytes.data()+*embedded,version,8);
                        const auto bad=(std::filesystem::path(outputDir)/(prefix+"-bad-"+version+".sav")).string();
                        std::ofstream out(bad,std::ios::binary);
                        out.write((const char*)bytes.data(),bytes.size());out.close();
                        return out && !restored.loadGameFile(bad.c_str()) && restored.world.checksum()==before;
                    };
                    rejects70=reject("RA2WLD70");
                    rejects64=reject("RA2WLD64");
                    rejects72=reject("RA2WLD72");
                    rejects73=reject("RA2WLD73");
                }
            }
            world.update();++observedTicks;
            const bool minusOne=atStart(-1) && roundTrip(prefix+"-minus-one",120,"RA2WLD71");
            world.update();++observedTicks;
            const bool armed=atStart(0) && roundTrip(prefix+"-armed",120,"RA2WLD64");
            bool rejectedWithoutStartup=false;
            const auto directory=std::filesystem::path(outputDir);
            const auto armedApp=(directory/(prefix+"-armed-app11.sav")).string();
            if(armed && currentApp && saveGameFile(armedApp.c_str())) {
                std::ifstream stream(armedApp,std::ios::binary);
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
                const auto embedded=embeddedWorldSaveOffset(bytes);
                if(embedded && worldSaveHeaderMatches(std::span(bytes).subspan(*embedded),"RA2WLD64")) {
                    const auto nested=std::span(bytes).subspan(*embedded);
                    const auto travel=locateWorldSaveSection(nested,WorldSaveSection::NativeTravel);
                    const auto sale=locateWorldSaveSection(nested,WorldSaveSection::BuildingSale);
                    if(travel && travel->footer+4==(sale?sale->offset:nested.size())) {
                        constexpr uint8_t emptyRedirects[]={'D','D','R','1',0,0,0,0,8,0,0,0};
                        if(sale) {
                            const int base=71;std::memcpy(bytes.data()+*embedded+8,&base,4);
                            bytes.insert(bytes.begin()+*embedded+sale->offset,std::begin(emptyRedirects),std::end(emptyRedirects));
                        } else {
                            std::memcpy(bytes.data()+*embedded,"RA2WLD71",8);
                            bytes.insert(bytes.end(),std::begin(emptyRedirects),std::end(emptyRedirects));
                        }
                        const auto invalid=(directory/(prefix+"-bad-no-startup.sav")).string();
                        std::ofstream out(invalid,std::ios::binary);
                        out.write((const char*)bytes.data(),bytes.size());out.close();
                        rejectedWithoutStartup=out && !restored.loadGameFile(invalid.c_str()) && restored.world.checksum()==before;
                    }
                }
            }
            std::printf("GROUND_UNLOAD_START id=%s minus_two=%d minus_one=%d armed=%d app11=%d rejected70=%d "
                "rejected64=%d rejected_empty71=%d rejected_empty72=%d rejected_empty73=%d\n",
                load.id,minusTwo,minusOne,armed,currentApp,rejects70,rejects64,rejectedWithoutStartup,rejects72,rejects73);
            check(minusTwo && minusOne && armed && currentApp && rejects70 && rejects64 && rejectedWithoutStartup && rejects72 && rejects73,
                "the two actual unload startup ticks replay from World71, and App11 rejects incorrect World headers atomically");
        }
        advance(firstDumpTicks-1-observedTicks);bool gate=world.players[0].money==10000 && actor.oreLoad==load.ore+load.gems;
        world.update();
        const int first=load.ore?load.ore*25:load.gems*50;
        gate &= world.players[0].money==10000+first+(purifier?first/4:0);
        gate &= actor.oreLoad==(load.ore?load.gems:0) && actor.gemLoad==(load.ore?load.gems:0)
            && world.harvesterUnloading(actor) && actor.x==23.5f && actor.y==21.5f;
        gate &= roundTrip(std::string("load-")+load.id+"-"+std::to_string(load.ore)+"-"+std::to_string(load.gems)+"-"+std::to_string(purifier));
        int laterCreditAt=0,exitAt=0,laterDeposits=0;
        const auto advanceDock=[&] {
            const auto income=world.harvestedIncome[0];
            world.update();
            const int elapsed=(int)(world.tick-dockTick);
            if(income!=world.harvestedIncome[0]) {
                ++laterDeposits;laterCreditAt=elapsed;
                gate &= world.harvesterUnloading(actor) && actor.x==23.5f && actor.y==21.5f && actor.oreLoad==0 && actor.gemLoad==0;
            }
            if(!exitAt && actor.nativeDock.phase==NativeDockPhase::Exit)exitAt=elapsed;
        };
        if(stockOreLoad) {
            const auto prefix=std::string("unload-origin-")+load.id;
            const auto heading=world.nativeHullHeading(actor,world.tick);
            const float x=actor.x,y=actor.y;
            const int color=world.players[actor.player].colorId;
            const auto& unload=g_sprites.unitUnload(actor.utype,actor.dir,color);
            const bool displayedUnload=world.harvesterUnloading(actor);
            capture(mover,(prefix+"-parked").c_str());
            for(int t=0;t<60 && actor.nativeDock.phase==NativeDockPhase::Unload;++t)advanceDock();
            const auto& travel=g_sprites.unitBodyFacing(actor.utype,unitBodyFacing(actor),unitBodyFrame(actor),color);
            const bool stationary=actor.nativeDock.phase==NativeDockPhase::Exit && actor.x==x && actor.y==y
                && world.nativeHullHeading(actor,world.tick)==heading;
            capture(mover,(prefix+"-departure").c_str());
            const bool sameAnchor=unload.ox==travel.ox && unload.oy==travel.oy
                && unload.tex.width==travel.tex.width && unload.tex.height==travel.tex.height;
            std::printf("GROUND_UNLOAD_POSE id=%s displayed=%d stationary=%d discharge=%d,%d,%d,%d drive=%d,%d,%d,%d aligned=%d\n",
                load.id,displayedUnload,stationary,unload.ox,unload.oy,unload.tex.width,unload.tex.height,
                travel.ox,travel.oy,travel.tex.width,travel.tex.height,sameAnchor);
            check(displayedUnload && stationary && !world.harvesterUnloading(actor) && sameAnchor,
                "the actual unloading and departure meshes keep the same ground anchor at the stationary handoff");
        }
        for(int t=0;t<500 && actor.nativeDock.phase!=NativeDockPhase::None;++t)advanceDock();
        const int expected=purifier?load.bonus:load.plain;
        const bool mixed=load.ore>0 && load.gems>0;
        std::printf("GROUND_LOAD id=%s ore=%d gems=%d purifier=%d income=%lld expected=%d later_credit=%d exit=%d\n",
            load.id,load.ore,load.gems,purifier,(long long)(world.players[0].money-10000),expected,laterCreditAt,exitAt);
        check(reached && gate && actor.oreLoad==0 && world.players[0].money==10000+expected && world.harvestedIncome[0]==expected
            && laterDeposits==(mixed?1:0) && laterCreditAt==(mixed?firstDumpTicks+dumpInterval:0)
            && exitAt==(mixed?mixedExitTicks:singleExitTicks),
            "a real return order preserves original dump timing, type order, bulk credits and purifier rounding");
    }
    struct PoweredDeposit {
        int elapsed;
        Credits amount;
        int supply, demand, cargo, gems;
        bool purifierReady, lowPower;
    };
    for (bool restorePower : {true, false}) {
        if (!fresh()) return 1;
        const EID refinery = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("NAREFN"));
        const EID purifier = world.spawnBuilding(0, BldType::OrePurifier, 33, 32, true, type("GAOREP"));
        const EID radar = world.spawnBuilding(0, BldType::AirForceCmd, 43, 32, true, type("GAAIRC"));
        const EID reactor = world.spawnBuilding(0, BldType::TeslaReactor, 35, 4, true, type("NAPOWR"));
        EID spare = restorePower ? INVALID_EID : world.spawnBuilding(0, BldType::TeslaReactor, 40, 4, true, type("NAPOWR"));
        const EID miner = world.spawnUnit(0, UnitType::WarMiner, 24.5f, 21.5f, type("HARV"));
        if (!world.valid(refinery) || !world.valid(purifier) || !world.valid(radar) || !world.valid(reactor)
            || !world.valid(miner) || (!restorePower && !world.valid(spare))) {
            check(false, "purifier power fixture places actual refinery, purifier, radar, reactors and miner");
            continue;
        }
        const auto purifierRef = world.entityRef(purifier);
        const auto refineryRef = world.entityRef(refinery);
        const auto& player = world.players[0];
        const auto ready = [&] {
            return world.valid(purifierRef) && world.ents[purifier].player == 0
                && !world.ents[purifier].constructAnim && !world.ents[purifier].selling;
        };
        const int firstSupply = restorePower ? 150 : 300;
        const int secondSupply = restorePower ? 300 : 150;
        const bool initialPower = player.powerMade == firstSupply && player.powerUsed == 300
            && player.lowPower() == restorePower && player.powerSabotage == 0 && ready();
        const Credits startingMoney = player.money;
        const auto startingIncome = world.harvestedIncome[0];
        world.ents[miner].oreLoad = 20;
        world.ents[miner].gemLoad = 10;
        order(World::Cmd::ReturnHarvester, {miner}, refinery);
        for (int elapsed = 0; elapsed < 500 && !world.harvesterUnloading(world.ents[miner]); ++elapsed) world.update();
        const bool docked = world.harvesterUnloading(world.ents[miner]) && world.ents[miner].nativeDock.receiver == refineryRef
            && world.ents[miner].oreLoad == 20 && world.ents[miner].gemLoad == 10 && player.money == startingMoney;
        const std::string name = restorePower ? "purifier-power-restored" : "purifier-power-lost";
        std::vector<PoweredDeposit> deposits;
        bool inBay = true, switched = false, savedBefore = false, savedAfter = false, droveOut = false;
        int exitAt = 0, exits = 0;
        for (int elapsed = 1; elapsed <= 300 && docked; ++elapsed) {
            const auto priorPhase = world.ents[miner].nativeDock.phase;
            const Credits priorMoney = player.money;
            world.update();
            const auto& actor = world.ents[miner];
            const auto dockPhase = actor.nativeDock.phase;
            if (dockPhase == NativeDockPhase::Unload)
                inBay &= actor.x == 23.5f && actor.y == 21.5f && actor.nativeDock.receiver == refineryRef;
            if (dockPhase == NativeDockPhase::Exit) {
                if (priorPhase != NativeDockPhase::Exit) { exitAt = elapsed; ++exits; }
                droveOut |= actor.x > actor.px && actor.y == 21.5f;
            }
            if (player.money != priorMoney) {
                deposits.push_back({elapsed, player.money - priorMoney, player.powerMade, player.powerUsed,
                    actor.oreLoad, actor.gemLoad, ready(), player.lowPower()});
                inBay &= dockPhase == NativeDockPhase::Unload;
                if (deposits.size() == 1) {
                    savedBefore = roundTrip(name + "-before", 120);
                    if (restorePower) spare = world.spawnBuilding(0, BldType::TeslaReactor, 40, 4, true, type("NAPOWR"));
                    else world.kill(spare, false);
                    switched = world.valid(spare) == restorePower && player.powerMade == secondSupply
                        && player.powerUsed == 300 && player.lowPower() != restorePower && ready();
                    savedAfter = roundTrip(name + "-after", 120);
                }
            }
            if (dockPhase == NativeDockPhase::None) break;
        }
        const auto& actor = world.ents[miner];
        const bool left = exitAt == mixedExitTicks && exits == 1 && droveOut && actor.nativeDock.phase == NativeDockPhase::None
            && actor.x == 24.5f && actor.y == 21.5f && actor.oreLoad == 0 && actor.gemLoad == 0;
        const bool twoDeposits = deposits.size() == 2;
        const bool powerAtDeposit = twoDeposits && deposits[0].supply == firstSupply && deposits[1].supply == secondSupply
            && deposits[0].demand == 300 && deposits[1].demand == 300 && deposits[0].purifierReady && deposits[1].purifierReady
            && deposits[0].lowPower == restorePower && deposits[1].lowPower != restorePower;
        const bool cadence = twoDeposits && deposits[0].elapsed == firstDumpTicks && deposits[1].elapsed == singleExitTicks
            && deposits[0].cargo == 10 && deposits[0].gems == 10 && deposits[1].cargo == 0 && deposits[1].gems == 0;
        const Credits expectedOre = restorePower ? 250 : 312;
        const Credits expectedGems = restorePower ? 625 : 500;
        std::printf("GROUND_PURIFIER_POWER restore=%d initial=%d switched=%d docked=%d parked=%d cadence=%d exit=%d left=%d replay=%d/%d",
            restorePower, initialPower, switched, docked, inBay, cadence, exitAt, left, savedBefore, savedAfter);
        for (const auto& deposit : deposits) {
            std::printf(" credit=%d:%lld:%d/%d:%d/%d:%d", deposit.elapsed, (long long)deposit.amount, deposit.supply,
                deposit.demand, deposit.cargo, deposit.gems, deposit.purifierReady);
        }
        std::printf(" expected=%lld/%lld\n", (long long)expectedOre, (long long)expectedGems);
        check(initialPower && switched && powerAtDeposit && docked && inBay && cadence && left && savedBefore && savedAfter,
            "a real mixed-load return stays docked through both power levels and exits once after exact save resumption");
        check(twoDeposits && deposits[0].amount == expectedOre && deposits[1].amount == expectedGems
            && world.harvestedIncome[0] - startingIncome == (uint64_t)(expectedOre + expectedGems),
            "native unloading applies the purifier bonus only to batches credited at full power");
    }
    struct CreditCueCase {int owner,room;Credits ore,gems;};
    for(const auto test:{CreditCueCase{0,350,150,200},CreditCueCase{1,350,150,200},
                         CreditCueCase{0,100,100,0},CreditCueCase{0,0,0,0}}) {
        if(!fresh())return 1;
        const auto refinery=world.spawnBuilding(test.owner,BldType::OreRefinery,20,20,true,type(test.owner?"GAREFN":"NAREFN"));
        const auto miner=test.owner?world.spawnUnit(1,UnitType::ChronoMiner,24.5f,21.5f,type("CMIN"))
            :world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));
        if(!world.valid(refinery) || !world.valid(miner))return failures+1;
        auto& actor=world.ents[miner];
        actor.oreLoad=10;actor.gemLoad=4;
        auto& player=world.players[test.owner];
        player.money=g_gameRules.maxMoney-test.room;
        World::Cmd returnOrder{};returnOrder.type=World::Cmd::ReturnHarvester;returnOrder.ids={miner};returnOrder.a=refinery;
        world.bindCommand(returnOrder);world.applyCmd(test.owner,returnOrder);
        for(int t=0;t<500 && actor.nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
        const bool docked=actor.nativeDock.phase==NativeDockPhase::Unload && actor.nativeDock.receiver==world.entityRef(refinery);
        const auto dockTick=world.tick;
        const auto sounds=[](const World& scene) {
            std::vector<std::tuple<uint64_t,int,bool>> cues;
            for(const auto& cue:scene.presentationEvents)if(cue.kind==PresentationKind::Sound && cue.sound==Sfx::Cash)
                cues.emplace_back(cue.tick,cue.player,cue.positional);
            return cues;
        };
        std::vector<std::pair<int,Credits>> credits,expectedCredits;
        std::vector<std::pair<int,int>> cues,expectedCues;
        const auto expected=[&](int elapsed,Credits amount) {
            if(amount>0){expectedCredits.emplace_back(elapsed,amount);expectedCues.emplace_back(elapsed,test.owner);}
        };
        expected(firstDumpTicks,test.ore);expected(singleExitTicks,test.gems);
        World replay;bool saved=false,same=true,inBay=true,nonPositional=true;
        for(int elapsed=1;elapsed<=400 && actor.nativeDock.phase!=NativeDockPhase::None;++elapsed) {
            const Credits prior=player.money;
            world.presentationEvents.clear();world.update();
            const auto signals=sounds(world);
            for(const auto& [tick,owner,positional]:signals) {
                cues.emplace_back((int)(tick-dockTick),owner);
                nonPositional &= !positional;
            }
            if(player.money!=prior) {
                credits.emplace_back(elapsed,player.money-prior);
                inBay &= world.harvesterUnloading(actor) && actor.x==23.5f && actor.y==21.5f;
            }
            if(saved) {
                replay.presentationEvents.clear();replay.update();
                same &= replay.checksum()==world.checksum() && sounds(replay)==signals;
            }
            if(elapsed==firstDumpTicks) {
                const auto path=(std::filesystem::path(outputDir)/("credit-sound-"+std::to_string(test.owner)+"-"+std::to_string(test.room)+".sav")).string();
                FILE* file=std::fopen(path.c_str(),"wb");
                const bool written=file && world.saveGame(file);if(file)std::fclose(file);
                file=std::fopen(path.c_str(),"rb");
                saved=written && file && replay.loadGame(file);if(file)std::fclose(file);
                same &= saved && replay.checksum()==world.checksum() && sounds(replay).empty();
            }
        }
        std::printf("GROUND_CREDIT_SOUND owner=%d room=%d docked=%d replay=%d",test.owner,test.room,docked,saved && same);
        for(const auto& [elapsed,amount]:credits)std::printf(" credit=%d:%lld",elapsed,(long long)amount);
        for(const auto& [elapsed,owner]:cues)std::printf(" cue=%d:%d",elapsed,owner);
        std::printf("\n");
        check(docked && inBay && saved && same && nonPositional && credits==expectedCredits && cues==expectedCues
            && actor.nativeDock.phase==NativeDockPhase::None && actor.oreLoad==0
            && world.harvestedIncome[test.owner]==test.ore+test.gems,
            "each positive native deposit cues its owner on the credit tick, including after a save and at the income cap");
    }
    for(bool closeRoad:{false,true}) {
        if(!fresh())return 1;
        for(int y=0;y<world.map.h;++y)if(y!=21 && y!=22)world.map.at(26,y).terrain=Terrain::Water;
        const Vec2i field{33,21},backup{15,24};
        for(const auto cell:{field,backup}) {
            auto& deposit=world.map.at(cell.x,cell.y);
            deposit.terrain=Terrain::Ore;deposit.ore=deposit.oreMax=80;
        }
        const auto refinery=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const auto miner=world.spawnUnit(0,UnitType::WarMiner,32.5f,21.5f,type("HARV"));
        if(!world.valid(refinery) || !world.valid(miner))return failures+1;
        World::Cmd harvest{};harvest.type=World::Cmd::Harvest;harvest.ids={miner};harvest.a=field.x;harvest.b=field.y;
        world.bindCommand(harvest);world.applyCmd(0,harvest);
        const auto& actor=world.ents[miner];
        const bool admitted=actor.state==UState::HarvestGo && actor.oreCell==field;
        const int reactor=world.productionSelector(0,false,(int)BldType::TeslaReactor);
        if(closeRoad)order(World::Cmd::StartBldProd,{},reactor);
        for(int t=0;t<2200 && actor.nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
        const bool loaded=actor.nativeDock.phase==NativeDockPhase::Unload && actor.oreLoad==40
            && actor.nativeDock.receiver==world.entityRef(refinery) && world.harvestedIncome[0]==0;
        const auto dockedAt=world.tick;
        std::vector<Vec2i> route;
        const bool open=world.map.findPath(24,21,field.x,field.y,route);
        bool placed=false;
        if(closeRoad && reactor>=0 && world.productionItem(0,false,reactor).ready) {
            order(World::Cmd::PlaceBuilding,{},reactor,25,21);
            const auto obstacle=world.bldAt(26,21);
            placed=world.valid(obstacle) && world.ents[obstacle].btype==BldType::TeslaReactor;
        }
        const bool oldRoute=world.map.findPath(24,21,field.x,field.y,route);
        const bool backupRoute=world.map.findPath(24,21,backup.x,backup.y,route);
        check(admitted && loaded && open && backupRoute && placed==closeRoad && oldRoute!=closeRoad,
            "a naturally loaded miner reaches the bay before a legal construction order closes its old route");
        const bool savedInBay=roundTrip(closeRoad?"outbound-sealed-bay":"outbound-open-bay",240);
        int creditedAfter=-1,exitAfter=-1;
        for(int t=0;t<400 && actor.nativeDock.phase!=NativeDockPhase::None;++t) {
            world.update();
            if(creditedAfter<0 && world.harvestedIncome[0]>0)creditedAfter=(int)(world.tick-dockedAt);
            if(exitAfter<0 && actor.exitingNativeDock())exitAfter=(int)(world.tick-dockedAt);
        }
        const auto expected=closeRoad?backup:field;
        const bool atApron=closeRoad?actor.x==24.5f:actor.x>=24.5f && actor.x<24.55f;
        const bool routed=actor.state==UState::HarvestGo && atApron && actor.y==21.5f
            && world.unitAtCell(24,21,actor.navLayer)==miner
            && actor.oreCell==expected && !actor.path.empty() && actor.path.back()==expected;
        const auto selectedAtExit=actor.oreCell;
        const bool savedOnRoad=roundTrip(closeRoad?"outbound-sealed-path":"outbound-open-path",240);
        int idleTicks=0,withoutPath=0,resumed=-1;
        for(int t=0;t<600;++t) {
            if(actor.state==UState::HarvestDig && actor.oreCell==expected){resumed=t;break;}
            idleTicks+=actor.state==UState::Idle;
            withoutPath+=actor.state==UState::HarvestGo && actor.pathIdx>=(int)actor.path.size();
            world.update();
        }
        std::printf("GROUND_OUTBOUND closed=%d loaded=%d placed=%d old_route=%d exit_field=%d,%d routed=%d idle=%d no_path=%d resume=%d credit_at=%d exit_at=%d income=%lld replay=%d\n",
            closeRoad,loaded,placed,oldRoute,selectedAtExit.x,selectedAtExit.y,routed,idleTicks,withoutPath,resumed,
            creditedAfter,exitAfter,(long long)world.harvestedIncome[0],savedInBay && savedOnRoad);
        check(routed && resumed>0 && idleTicks==0 && withoutPath==0 && creditedAfter==firstDumpTicks && exitAfter==singleExitTicks
            && actor.nativeDock.phase==NativeDockPhase::None && actor.oreLoad==0 && world.harvestedIncome[0]==1000
            && savedInBay && savedOnRoad,
            "the mine selected at exit has a route and resumes after the same single unload, including saves on each side of departure");
    }
    if(!fresh())return 1;
    const auto shared=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
    const auto firstMiner=world.spawnUnit(0,UnitType::WarMiner,27.5f,21.5f,type("HARV"));
    const auto secondMiner=world.spawnUnit(0,UnitType::WarMiner,27.5f,24.5f,type("HARV"));
    world.ents[firstMiner].oreLoad=world.ents[secondMiner].oreLoad=40;order(World::Cmd::ReturnHarvester,{firstMiner,secondMiner},shared);
    bool exclusive=true,separate=true,queueSaved=false;
    for(int t=0;t<1600;++t) {
        world.update();int claims=0;for(const auto id:{firstMiner,secondMiner})claims+=world.ents[id].nativeDock.phase>=NativeDockPhase::Align;
        exclusive &= claims<=1;
        separate &= distf(world.ents[firstMiner].x,world.ents[firstMiner].y,world.ents[secondMiner].x,world.ents[secondMiner].y)>.2f;
        if(!queueSaved && claims==1 && world.ents[secondMiner].nativeDock.phase==NativeDockPhase::Approach){queueSaved=roundTrip("two-miner-queue",180);}
        if(world.harvestedIncome[0]==2000 && world.ents[secondMiner].nativeDock.phase==NativeDockPhase::None)break;
    }
    check(exclusive && separate && queueSaved && world.harvestedIncome[0]==2000,
        "two loaded miners share one physical bay without overlapping or blocking its exit, including a queued save");
    std::printf("GROUND_QUEUE exclusive=%d separate=%d saved=%d income=%lld first=%.3f,%.3f/%d second=%.3f,%.3f/%d\n",
        exclusive,separate,queueSaved,(long long)world.harvestedIncome[0],world.ents[firstMiner].x,world.ents[firstMiner].y,(int)world.ents[firstMiner].nativeDock.phase,
        world.ents[secondMiner].x,world.ents[secondMiner].y,(int)world.ents[secondMiner].nativeDock.phase);
    for (int height : {MAP_MAX_CLIMB, MAP_MAX_CLIMB + 1}) {
        if (!fresh()) return 1;
        const bool cliff = height > MAP_MAX_CLIMB;
        for (int y = 20; y < 23; ++y)
            for (int x = 20; x < 24; ++x) world.map.at(x, y).height = (uint8_t)height;
        world.spawnBuilding(0, BldType::PowerPlant, 17, 20, true, type("NAPOW"));
        if (cliff) world.spawnBuilding(0, BldType::PowerPlant, 31, 17, true, type("NAPOW"));
        const bool placeable = world.canPlace(BldType::OreRefinery, 20, 20, 0)
            && (!cliff || world.canPlace(BldType::OreRefinery, 31, 20, 0));
        const EID preferred = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("NAREFN"));
        const EID alternate = cliff ? world.spawnBuilding(0, BldType::OreRefinery, 31, 20, true, type("NAREFN")) : INVALID_EID;
        const EID first = world.spawnUnit(0, UnitType::WarMiner, 27.5f, 21.5f, type("HARV"));
        const EID second = world.spawnUnit(0, UnitType::WarMiner, 27.5f, 25.5f, type("HARV"));
        if (!world.valid(preferred) || (cliff && !world.valid(alternate)) || !world.valid(first) || !world.valid(second)) return 1;
        world.ents[first].oreLoad = world.ents[second].oreLoad = 40;
        order(World::Cmd::ReturnHarvester, {first, second}, preferred);
        const auto expected = world.entityRef(cliff ? alternate : preferred);
        const float dockX = world.ents[expected.id].x + 3.5f;
        const bool selected = world.ents[first].nativeDock.receiver == expected && world.ents[second].nativeDock.receiver == expected;
        bool sampled = false, resumed = false, exclusiveBay = true, validDeposits = true, crossedCliff = false;
        bool savedCliffBay = false;
        int deposits = 0;
        for (int t = 0; t < 1600; ++t) {
            const auto& lead = world.ents[first];
            const auto& follow = world.ents[second];
            const bool queued = (lead.claimsNativeDock() && follow.nativeDock.phase == NativeDockPhase::Approach)
                || (follow.claimsNativeDock() && lead.nativeDock.phase == NativeDockPhase::Approach);
            if (!sampled && queued) {
                sampled = true;
                resumed = roundTrip("sloped-bay-queue-" + std::to_string(height), 270);
            }
            const auto atBay = [&](const World::Ent& miner) {
                return miner.nativeDock.phase == NativeDockPhase::Unload && miner.nativeDock.receiver == expected
                    && miner.x == dockX && miner.y == 21.5f && miner.oreLoad == 40;
            };
            const bool ready = atBay(lead) || atBay(follow);
            const auto income = world.harvestedIncome[0];
            world.update();
            exclusiveBay &= !(lead.claimsNativeDock() && follow.claimsNativeDock());
            for (EID id : {first, second}) {
                const auto& miner = world.ents[id];
                if (cliff && miner.nativeDock.receiver.id == preferred && miner.x < 24.f) {
                    crossedCliff = true;
                    if (!savedCliffBay && miner.nativeDock.phase == NativeDockPhase::Unload)
                        savedCliffBay = roundTrip("cliff-bay-before-credit", 180);
                }
            }
            if (world.harvestedIncome[0] != income) {
                ++deposits;
                validDeposits &= ready && world.harvestedIncome[0] - income == 1000;
            }
            if (!lead.oreLoad && !follow.oreLoad && lead.nativeDock.phase == NativeDockPhase::None
                    && follow.nativeDock.phase == NativeDockPhase::None) break;
        }
        std::printf("GROUND_DOCK_SLOPE height=%d placeable=%d selected=%d crossed_cliff=%d saved_cliff=%d "
            "queued=%d replay=%d exclusive=%d valid_deposits=%d deposits=%d income=%lld\n",
            height, placeable, selected, crossedCliff, savedCliffBay, sampled, resumed, exclusiveBay, validDeposits,
            deposits, (long long)world.harvestedIncome[0]);
        check(placeable && selected && !crossedCliff && sampled && resumed && exclusiveBay && validDeposits
            && deposits == 2 && world.harvestedIncome[0] == 2000,
            "loaded miners share a traversable bay and reroute away from an impassable dock edge");
    }
    if (const char* input = std::getenv("OPENRA2_LEGACY_CLIFF_BAY_INPUT")) {
        World previous;
        FILE* stream = std::fopen(input, "rb");
        const bool loaded = stream && previous.loadGame(stream);
        if (stream) std::fclose(stream);
        check(loaded, "load the actual older World64 created inside an inaccessible bay");
        if (loaded) {
            world = std::move(previous);
            world.map.bldOccRef = &world.bldOcc;
            const EID cliffRef = world.bldAt(20, 20), safeRef = world.bldAt(31, 20);
            const auto safeBay = world.entityRef(safeRef);
            std::vector<EID> miners;
            EID inBay = INVALID_EID;
            for (EID id = 0; id < (EID)world.ents.size(); ++id) {
                const auto& miner = world.ents[id];
                if (!miner.alive || miner.isBuilding || miner.utype != UnitType::WarMiner) continue;
                miners.push_back(id);
                if (miner.nativeDock.phase == NativeDockPhase::Unload && miner.nativeDock.receiver.id == cliffRef
                        && miner.x == 23.5f && miner.y == 21.5f && miner.oreLoad == 40) inBay = id;
            }
            const bool staged = world.valid(inBay) && world.valid(safeRef) && world.valid(cliffRef)
                && miners.size() == 2 && world.players[0].money == 10000 && world.harvestedIncome[0] == 0;
            bool savedExit = false, exitedWithCargo = false, legalIncome = true;
            int deposits = 0;
            if (staged) for (int tick = 0; tick < 1600; ++tick) {
                const bool ready = std::any_of(miners.begin(), miners.end(), [&](EID id) {
                    const auto& miner = world.ents[id];
                    return miner.nativeDock.phase == NativeDockPhase::Unload && miner.nativeDock.receiver == safeBay
                        && miner.x == 34.5f && miner.y == 21.5f && miner.oreLoad == 40;
                });
                const auto income = world.harvestedIncome[0];
                world.update();
                const auto& returning = world.ents[inBay];
                if (!savedExit && returning.nativeDock.phase == NativeDockPhase::Exit)
                    savedExit = roundTrip("legacy-cliff-bay-exit", 300);
                exitedWithCargo |= returning.x == 24.5f && returning.y == 21.5f && returning.oreLoad == 40;
                if (world.harvestedIncome[0] != income) {
                    ++deposits;
                    legalIncome &= ready && world.harvestedIncome[0] - income == 1000;
                }
                if (world.harvestedIncome[0] == 2000 && std::all_of(miners.begin(), miners.end(), [&](EID id) {
                    const auto& miner = world.ents[id];
                    return miner.oreLoad == 0 && miner.nativeDock.phase == NativeDockPhase::None;
                })) break;
            }
            std::printf("GROUND_LEGACY_CLIFF loaded=%d staged=%d exited_with_cargo=%d replay=%d legal_income=%d deposits=%d income=%lld\n",
                loaded, staged, exitedWithCargo, savedExit, legalIncome, deposits, (long long)world.harvestedIncome[0]);
            check(staged && exitedWithCargo && savedExit && legalIncome && deposits == 2
                && world.players[0].money == 12000 && world.harvestedIncome[0] == 2000,
                "the old cliffside occupant evacuates with its cargo and uses the accessible bay");
        }
    }
    for(bool stopFirst:{false,true}) {
        if(!fresh())return 1;
        const EID receiver=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const EID approaching=world.spawnUnit(0,UnitType::WarMiner,27.5f,21.5f,type("HARV"));
        const EID waiting=world.spawnUnit(0,UnitType::WarMiner,27.5f,24.5f,type("HARV"));
        if(!world.valid(receiver) || !world.valid(approaching) || !world.valid(waiting))return 1;
        auto& lead=world.ents[approaching];
        auto& follower=world.ents[waiting];
        lead.oreLoad=follower.oreLoad=40;
        order(World::Cmd::ReturnHarvester,{approaching},receiver);
        for(int t=0;t<800 && lead.x>=25.f;++t)world.update();
        const float restartX=lead.x;
        const bool interrupted=lead.nativeDock.phase==NativeDockPhase::Approach && (int)lead.x==24
            && lead.x>24.52f && lead.y==21.5f;
        if(stopFirst) {
            order(World::Cmd::Stop,{approaching});
            advance(3);
        }
        const bool preserved=lead.x==restartX && lead.oreLoad==40 && world.harvestedIncome[0]==0;
        order(World::Cmd::ReturnHarvester,{approaching,waiting},receiver);
        const bool hasCenterLeg=lead.path.size()==1 && lead.path.front()==Vec2i{24,21};
        const bool resumed=roundTrip(stopFirst?"dock-stop-and-return":"dock-repeat-return",550);
        int deposits=0;
        bool safe=true;
        for(int t=0;t<1600;++t) {
            const auto income=world.harvestedIncome[0];
            const auto ready=[](const World::Ent& e) {
                return e.oreLoad==40 && e.nativeDock.phase==NativeDockPhase::Unload && e.x==23.5f && e.y==21.5f;
            };
            const bool docked=ready(lead) || ready(follower);
            world.update();
            if(world.harvestedIncome[0]!=income) {
                ++deposits;
                safe &= docked && world.harvestedIncome[0]-income==1000;
            }
            safe &= (!lead.claimsNativeDock() || !follower.claimsNativeDock())
                && distf(lead.x,lead.y,follower.x,follower.y)>.2f;
            if(!lead.oreLoad && !follower.oreLoad && lead.nativeDock.phase==NativeDockPhase::None
                && follower.nativeDock.phase==NativeDockPhase::None)break;
        }
        std::printf("GROUND_DOCK_RESTART stop=%d staged=%d x=%.4f leg=%d replay=%d safe=%d deposits=%d income=%lld\n",
            stopFirst,interrupted,restartX,hasCenterLeg,resumed,safe,deposits,(long long)world.harvestedIncome[0]);
        check(interrupted && preserved && hasCenterLeg && resumed && safe && deposits==2
            && !lead.oreLoad && !follower.oreLoad && world.harvestedIncome[0]==2000 && world.players[0].money==12000
            && lead.nativeDock.phase==NativeDockPhase::None && follower.nativeDock.phase==NativeDockPhase::None,
            "restarting a return inside the approach cell centers the loaded miner and then serves both loads once");
    }
    // The far refinery requires the War Miner to drive around the footprint.
    constexpr int redirectWatchTicks = 1200;
    for (bool chrono : {false, true}) {
        if (!fresh()) return 1;
        const int owner = chrono ? 1 : 0;
        const auto unitType = chrono ? UnitType::ChronoMiner : UnitType::WarMiner;
        const char* minerName = chrono ? "CMIN" : "HARV";
        const int refineryType = type(chrono ? "GAREFN" : "NAREFN");
        const EID refinery = world.spawnBuilding(owner, BldType::OreRefinery, 20, 20, true, refineryType);
        const EID other = world.spawnBuilding(owner, BldType::OreRefinery, 38, 20, true, refineryType);
        const EID id = world.spawnUnit(owner, unitType, 28.5f, 21.5f, type(minerName));
        if (!world.valid(refinery) || !world.valid(other) || !world.valid(id)) return 1;
        auto& miner = world.ents[id];
        miner.dir = miner.turretDir = 5;
        miner.oreLoad = World::harvesterCapacity(unitType);
        miner.gemLoad = 4;
        const auto expectedIncome = (miner.oreLoad - miner.gemLoad) * world.nativeProductionRules.oreValue
            + miner.gemLoad * world.nativeProductionRules.gemValue;
        const auto request = [&](World& running, EID receiver) {
            World::Cmd cmd{};
            cmd.type = World::Cmd::ReturnHarvester;
            cmd.ids = {id};
            cmd.a = receiver;
            running.bindCommand(cmd);
            running.applyCmd(owner, cmd);
        };
        request(world, refinery);
        const auto dock = world.entityRef(refinery);
        const bool admitted = miner.nativeDock.receiver == dock;

        const char* phaseNames[] = {"ready", chrono ? "landed" : "entry-reserved", chrono ? "turn-in-berth" : "entry-driving", "ore", "gems", "exit"};
        const auto sampledPhase = [&] {
            if (miner.chronoReturn.phase == ChronoReturnPhase::Charging
                && miner.chronoReturn.ticksLeft == ChronoReturnTiming::ready) return 0;
            if (chrono && miner.nativeDock.phase == NativeDockPhase::Align)
                return world.nativeHullHeading(miner, world.tick) == NativeGroundMath::fromDir(5) ? 1 : 2;
            if (miner.nativeDock.phase == NativeDockPhase::ForwardEntry) return miner.x >= 24.48f ? 1 : 2;
            if (miner.nativeDock.phase == NativeDockPhase::Unload && miner.digTimer == 5) return miner.oreLoad > miner.gemLoad ? 3 : 4;
            if (miner.nativeDock.phase == NativeDockPhase::Exit && miner.x > 23.55f) return 5;
            return -1;
        };
        const int firstPhase = chrono ? 0 : 1;
        unsigned sampled = 0;
        int confirmed = 0, clicks = 0, deposits = 0, arrivals = 0;
        bool exact = true, saved = true, switched = false, redirected = false, alignedInBerth = true;
        std::optional<World> repeated;
        for (int tick = 0; tick < 900; ++tick) {
            if (chrono) alignedInBerth &= miner.nativeDock.phase != NativeDockPhase::Enter
                && (miner.nativeDock.phase != NativeDockPhase::Align || (miner.x == 23.5f && miner.y == 21.5f));
            const int stage = sampledPhase();
            if (stage >= 0 && !(sampled & (1u << stage))) {
                sampled |= 1u << stage;
                if (stage == firstPhase) {
                    repeated.emplace(world);
                    repeated->map.bldOccRef = &repeated->bldOcc;
                }
                if (stage == firstPhase || stage == 4)
                    saved &= roundTrip(std::string("repeat-return-") + minerName + "-" + phaseNames[stage], 260);

                World probe = world;
                probe.map.bldOccRef = &probe.bldOcc;
                request(probe, refinery);
                const auto& after = probe.ents[id];
                const bool equal = world.checksum() == probe.checksum();
                confirmed += equal;
                std::printf("GROUND_REPEAT_RETURN stage=%s type=%s exact=%d dock_before=%d dock_after=%d "
                    "warp_before=%d warp_after=%d timer_before=%d timer_after=%d dump_before=%d dump_after=%d\n",
                    phaseNames[stage], minerName, equal, (int)miner.nativeDock.phase, (int)after.nativeDock.phase,
                    (int)miner.chronoReturn.phase, (int)after.chronoReturn.phase, miner.chronoReturn.ticksLeft,
                    after.chronoReturn.ticksLeft, miner.digTimer, after.digTimer);

                if (stage == 3) {
                    World redirect = world;
                    redirect.map.bldOccRef = &redirect.bldOcc;
                    request(redirect, other);
                    const auto& moved = redirect.ents[id];
                    switched = moved.nativeDock.phase == NativeDockPhase::Exit && moved.oreLoad == miner.oreLoad
                        && redirect.harvestedIncome[owner] == world.harvestedIncome[owner];
                    const auto destination = redirect.entityRef(other);
                    const auto& newBay = redirect.ents[other];
                    bool exited = false, paidAtDestination = true;
                    int redirectDeposits = 0, lastPayment = 0;
                    float paidX = 0.f;
                    for (int elapsed = 1; elapsed <= redirectWatchTicks; ++elapsed) {
                        const auto before = redirect.harvestedIncome[owner];
                        redirect.update();
                        exited |= moved.x == 24.5f && moved.y == 21.5f && moved.nativeDock.receiver != dock;
                        if (redirect.harvestedIncome[owner] > before) {
                            ++redirectDeposits;
                            lastPayment = elapsed;
                            paidX = moved.x;
                            paidAtDestination &= exited && moved.nativeDock.receiver == destination
                                && moved.x == newBay.x + 3.5f && moved.y == newBay.y + 1.5f;
                        }
                        if (!moved.oreLoad && moved.nativeDock.phase == NativeDockPhase::None) break;
                    }
                    const auto income = redirect.harvestedIncome[owner];
                    redirected = exited && paidAtDestination && redirectDeposits == 2 && income == expectedIncome
                        && !moved.oreLoad && moved.nativeDock.phase == NativeDockPhase::None;
                    std::printf("GROUND_REDIRECT type=%s left=%d paid_x=%.2f expected_x=%.2f deposits=%d "
                        "last_payment_tick=%d income=%lld\n", minerName, exited, paidX, newBay.x + 3.5f,
                        redirectDeposits, lastPayment, (long long)income);
                }
            }
            const auto income = world.harvestedIncome[owner];
            if (repeated) {
                if (tick % 4 == 0 && miner.claimsNativeDock()) {
                    request(*repeated, refinery);
                    ++clicks;
                }
                repeated->update();
                for (const auto& effect : repeated->effects)
                    arrivals += effect.kind == 9 && effect.aux == (int)ChronoVisual::Arrival && effect.age == 1;
            }
            world.update();
            deposits += world.harvestedIncome[owner] > income;
            if (repeated) exact &= world.checksum() == repeated->checksum();
            if (!miner.oreLoad && miner.nativeDock.phase == NativeDockPhase::None) break;
        }
        const bool finished = repeated && !repeated->ents[id].oreLoad
            && repeated->ents[id].nativeDock.phase == NativeDockPhase::None
            && repeated->harvestedIncome[owner] == expectedIncome && world.harvestedIncome[owner] == expectedIncome;
        std::printf("GROUND_REPEAT_COMPLETE type=%s admitted=%d phases=%u confirmed=%d clicks=%d exact=%d saved=%d aligned_in_berth=%d "
            "redirect=%d deposits=%d arrivals=%d finished=%d income=%lld\n", minerName, admitted, sampled, confirmed, clicks,
            exact, saved, alignedInBerth, switched, deposits, arrivals, finished, (long long)world.harvestedIncome[owner]);
        check(admitted && sampled == (chrono ? 63u : 62u) && confirmed == (chrono ? 6 : 5) && clicks > 4 && alignedInBerth
            && exact && saved && switched && deposits == 2 && arrivals == (chrono ? 1 : 0) && finished,
            "repeated orders to the reserved refinery preserve the whole return and a different refinery still starts an exit");
        check(redirected, "after clearing the old bay, the explicitly selected refinery receives both cargo batches");
    }
    {
        if (!fresh()) return 1;
        const int refineryType = type("NAREFN");
        const EID first = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, refineryType);
        const EID second = world.spawnBuilding(0, BldType::OreRefinery, 38, 20, true, refineryType);
        const EID third = world.spawnBuilding(0, BldType::OreRefinery, 34, 32, true, refineryType);
        const EID id = world.spawnUnit(0, UnitType::WarMiner, 24.5f, 21.5f, type("HARV"));
        if (!world.valid(first) || !world.valid(second) || !world.valid(third) || !world.valid(id)) return 1;
        world.ents[id].oreLoad = 40;
        world.ents[id].gemLoad = 4;
        order(World::Cmd::ReturnHarvester, {id}, first);
        for (int tick = 0; tick < 500 && !world.harvestedIncome[0]; ++tick) world.update();
        const auto oreCredits = 36 * world.nativeProductionRules.oreValue;
        const auto gemCredits = 4 * world.nativeProductionRules.gemValue;
        const auto firstRef = world.entityRef(first), secondRef = world.entityRef(second);
        const World orePaid = world;
        check(world.harvestedIncome[0] == oreCredits && world.ents[id].gemLoad == 4 && world.ents[id].oreLoad == 4
            && world.ents[id].nativeDock.phase == NativeDockPhase::Unload,
            "the live redirect fixture starts with the ore paid and gems still aboard");

        enum class Change { Retarget, BackToFirst, Recycle, Capture, LoseBay, Stop };
        const char* names[] = {"latest", "back-to-first", "recycled", "captured", "old-bay-lost", "stopped"};
        for (auto change : {Change::Retarget, Change::BackToFirst, Change::Recycle, Change::Capture, Change::LoseBay, Change::Stop}) {
            world = orePaid;
            world.map.bldOccRef = &world.bldOcc;
            order(World::Cmd::ReturnHarvester, {id}, second);
            advance(3);
            const bool partialExit = world.ents[id].nativeDock.phase == NativeDockPhase::Exit
                && world.ents[id].nativeDock.receiver == firstRef && world.ents[id].pendingDockRefinery == secondRef
                && world.ents[id].x > 23.5f && world.ents[id].x < 24.5f;
            bool changed = true;
            EID destination = first;
            switch (change) {
            case Change::Retarget:
                destination = third;
                order(World::Cmd::ReturnHarvester, {id}, destination);
                break;
            case Change::BackToFirst:
                order(World::Cmd::ReturnHarvester, {id}, first);
                break;
            case Change::Recycle:
                world.kill(second, false);
                changed = world.spawnBuilding(0, BldType::OreRefinery, 38, 20, true, refineryType) == second
                    && !world.valid(secondRef);
                break;
            case Change::Capture:
                world.changeActorOwner(second, 1);
                world.synchronizeBuildingOwners();
                world.changeActorOwner(second, 0);
                world.synchronizeBuildingOwners();
                break;
            case Change::LoseBay:
                destination = second;
                world.kill(first, false);
                changed = world.ents[id].nativeDock.phase == NativeDockPhase::None;
                break;
            case Change::Stop:
                destination = INVALID_EID;
                order(World::Cmd::Stop, {id});
                break;
            }
            const auto expected = world.entityRef(destination);
            const auto pending = world.ents[id].pendingDockRefinery;
            if (change == Change::Retarget || change == Change::BackToFirst || change == Change::LoseBay) {
                changed &= pending == expected;
                const auto before = world.checksum();
                order(World::Cmd::ReturnHarvester, {id}, destination);
                changed &= world.checksum() == before;
            } else {
                changed &= pending == World::EntityRef{};
            }
            const std::string name = std::string("dock-redirect-") + names[(int)change];
            const bool replay = roundTrip(name, redirectWatchTicks);
            if (change == Change::Retarget) {
                const auto file = std::filesystem::path(outputDir) / (name + "-app11.sav");
                const bool written = saveGameFile(file.string().c_str());
                Game resumed;
                const bool loaded = written && resumed.loadGameFile(file.string().c_str())
                    && resumed.world.checksum() == world.checksum();
                std::ifstream saved(file, std::ios::binary);
                std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(saved), {}};
                const auto nested = embeddedWorldSaveOffset(bytes);
                const auto block = nested ? locateWorldSaveSection(std::span(bytes).subspan(*nested),
                    WorldSaveSection::NativeDockRedirect) : std::nullopt;
                bool rejected = false;
                if (block && block->length == 24) {
                    bytes[*nested + block->offset + 16] ^= 1; // pending refinery generation
                    const auto badFile = std::filesystem::path(outputDir) / (name + "-bad-generation.sav");
                    {std::ofstream out(badFile, std::ios::binary);out.write((const char*)bytes.data(),bytes.size());}
                    const auto before = world.checksum();
                    rejected = !loadGameFile(badFile.string().c_str()) && world.checksum() == before;
                }
                check(loaded && rejected, "App restores a pending refinery and atomically rejects the wrong target generation");
            }
            int deposits = 0;
            bool rightBay = true;
            const auto& miner = world.ents[id];
            for (int tick = 0; tick < redirectWatchTicks; ++tick) {
                const auto previous = world.harvestedIncome[0];
                world.update();
                if (world.harvestedIncome[0] != previous) {
                    ++deposits;
                    rightBay &= world.valid(expected) && miner.nativeDock.receiver == expected
                        && miner.x == world.ents[expected.id].x + 3.5f && miner.y == world.ents[expected.id].y + 1.5f;
                }
                if (miner.nativeDock.phase == NativeDockPhase::None && !miner.hasPendingDockReturn()
                    && (!miner.oreLoad || change == Change::Stop)) break;
            }
            const bool stopped = change == Change::Stop;
            const auto earned = world.harvestedIncome[0] - oreCredits;
            std::printf("GROUND_REDIRECT_RESUME case=%s mid_exit=%d changed=%d replay=%d deposits=%d right_bay=%d "
                "unpaid_cargo=%d earned=%lld x=%.3f y=%.3f phase=%d route=%d/%zu\n", names[(int)change], partialExit,
                changed, replay, deposits, rightBay, miner.oreLoad, (long long)earned,
                miner.x, miner.y, (int)miner.nativeDock.phase, miner.pathIdx, miner.path.size());
            check(partialExit && changed && replay && rightBay && deposits == (stopped ? 0 : 1)
                && earned == (stopped ? 0 : gemCredits) && miner.oreLoad == (stopped ? 4 : 0)
                && miner.nativeDock.phase == NativeDockPhase::None && !miner.hasPendingDockReturn(),
                "the pending return survives saves and resolves the latest valid command without duplicating income");
        }
    }
    if (const char* directory = std::getenv("OPENRA2_LEGACY_DOCK_FACING_INPUT_DIR")) {
        struct ArchivedDock {
            const char* name;
            const char* header;
            uint64_t tick;
            EID actor, pending;
            NativeDockPhase stage;
            int cargo, gems, cash, credit, deposits;
        };
        constexpr ArchivedDock archives[]{
            {"dock-phase-2", "RA2WLD64", 323, 3, INVALID_EID, NativeDockPhase::Align, 40, 0, 10000, 1000, 1},
            {"dock-phase-3", "RA2WLD64", 362, 3, INVALID_EID, NativeDockPhase::Enter, 40, 0, 10000, 1000, 1},
            {"dock-phase-4", "RA2WLD64", 414, 3, INVALID_EID, NativeDockPhase::Unload, 40, 0, 10000, 1000, 1},
            {"dock-phase-5", "RA2WLD64", 474, 3, INVALID_EID, NativeDockPhase::Exit, 0, 0, 11000, 0, 0},
            {"dock-redirect-latest", "RA2WLD65", 99, 5, 4, NativeDockPhase::Exit, 4, 4, 10900, 200, 1},
            {"explicit-busy-wait", "RA2WLD66", 109, 5, 2, NativeDockPhase::Approach, 40, 4, 10600, 1100, 2},
        };
        for (const auto& saved : archives) {
            const auto path = (std::filesystem::path(directory) / (std::string(saved.name) + ".sav")).string();
            World historical;
            FILE* input = std::fopen(path.c_str(), "rb");
            char header[8]{};
            const bool matched = input && std::fread(header, 1, sizeof header, input) == sizeof header
                && std::memcmp(header, saved.header, sizeof header) == 0;
            if (matched) std::rewind(input);
            const bool loaded = matched && historical.loadGame(input);
            if (input) std::fclose(input);
            bool exact = false, resumed = false, safe = true, previouslyAligned = false, sawForward = false;
            int deposits = 0;
            const World::EntityRef receiver{2, 1};
            const auto pending = saved.pending == INVALID_EID ? World::EntityRef{} : World::EntityRef{saved.pending, 1};
            const auto destination = saved.pending == INVALID_EID ? receiver : pending;
            if (loaded && historical.tick == saved.tick && historical.valid(saved.actor) && historical.valid(receiver)
                && historical.valid(destination) && historical.players[0].money == saved.cash) {
                world = std::move(historical);
                world.map.bldOccRef = &world.bldOcc;
                const auto& hauler = world.ents[saved.actor];
                const auto& receivingBay = world.ents[destination.id];
                const bool original = hauler.utype == UnitType::WarMiner && hauler.nativeDock.phase == saved.stage
                    && hauler.nativeDock.receiver == receiver && hauler.pendingDockRefinery == pending
                    && hauler.oreLoad == saved.cargo && hauler.gemLoad == saved.gems;
                exact = original && roundTrip(std::string("legacy-dock-facing-") + saved.name, 180, saved.header);
                const auto initialIncome = world.harvestedIncome[0];
                for (int elapsed = 0; original && elapsed < redirectWatchTicks; ++elapsed) {
                    const int cargo = hauler.oreLoad;
                    const auto income = world.harvestedIncome[0];
                    world.update();
                    const auto dockPhase = hauler.nativeDock.phase;
                    const auto heading = world.nativeHullHeading(hauler, world.tick);
                    sawForward |= dockPhase == NativeDockPhase::ForwardEntry;
                    previouslyAligned |= dockPhase == NativeDockPhase::Enter && hauler.x < hauler.px && heading == 16384;
                    if (cargo != hauler.oreLoad || income != world.harvestedIncome[0]) {
                        ++deposits;
                        safe &= cargo > hauler.oreLoad && world.harvestedIncome[0] > income && heading == 16384
                            && dockPhase == NativeDockPhase::Unload && hauler.nativeDock.receiver == destination
                            && hauler.x == receivingBay.x + 3.5f && hauler.y == receivingBay.y + 1.5f;
                    }
                    if (!hauler.oreLoad && dockPhase == NativeDockPhase::None) break;
                }
                resumed = original && hauler.oreLoad == 0 && hauler.gemLoad == 0 && hauler.state == UState::Idle
                    && hauler.nativeDock.phase == NativeDockPhase::None && !hauler.hasPendingDockReturn()
                    && hauler.x == receivingBay.x + 4.5f && hauler.y == receivingBay.y + 1.5f
                    && world.harvestedIncome[0] == initialIncome + saved.credit && world.players[0].money == saved.cash + saved.credit;
            }
            const bool legacyEntry = saved.stage == NativeDockPhase::Align || saved.stage == NativeDockPhase::Enter;
            std::printf("GROUND_DOCK_LEGACY case=%s header=%s loaded=%d exact=%d legacy_entry=%d forward_entry=%d safe=%d deposits=%d finished=%d\n",
                saved.name, saved.header, loaded, exact, previouslyAligned, sawForward, safe, deposits, resumed);
            check(loaded && exact && safe && resumed && deposits == saved.deposits
                && (!legacyEntry || previouslyAligned) && sawForward == (saved.pending != INVALID_EID),
                "an archived refinery save keeps its in-flight order and settles only the remaining cargo");
        }
    }
    if (const char* directory = std::getenv("OPENRA2_LEGACY_TRAVEL_INPUT_DIR")) {
        if (!fresh()) return 1;
        const auto file = (std::filesystem::path(directory) / "load-CMIN-20-0-0.sav").string();
        World historical;
        FILE* input = std::fopen(file.c_str(), "rb");
        const bool loaded = input && historical.loadGame(input);
        if (input) std::fclose(input);
        bool resumed = loaded && historical.nativeProductionRules.version == 8;
        if (resumed) {
            world = std::move(historical);
            world.map.bldOccRef = &world.bldOcc;
            EID miner = INVALID_EID;
            for (EID id = 0; id < (EID)world.ents.size(); ++id)
                if (world.valid(id) && world.ents[id].utype == UnitType::ChronoMiner
                    && world.ents[id].nativeDock.phase == NativeDockPhase::Unload) miner = id;
            const EID receiver = world.spawnBuilding(0, BldType::OreRefinery, 38, 20, true, type("NAREFN"));
            resumed = world.valid(miner) && world.valid(receiver);
            if (resumed) {
                const auto income = world.harvestedIncome[0];
                order(World::Cmd::ReturnHarvester, {miner}, receiver);
                advance(3);
                resumed = world.ents[miner].hasPendingDockReturn() && roundTrip("dock-redirect-from-world63", 300);
                bool reached = false;
                for (int tick = 0; tick < 300 && !reached; ++tick) {
                    world.update();
                    const auto& e = world.ents[miner];
                    reached = e.nativeDock.phase == NativeDockPhase::Unload && e.nativeDock.receiver == world.entityRef(receiver);
                }
                resumed &= reached && world.harvestedIncome[0] == income;
            }
        }
        check(resumed, "a real World63 restores an empty miner and saves its new redirected exit without adding travel rules");
    }
    if(!fresh())return 1;
    const auto busy=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
    const auto incumbent=world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));world.ents[incumbent].oreLoad=40;
    order(World::Cmd::ReturnHarvester,{incumbent},busy);advance(2);
    const auto available=world.spawnBuilding(0,BldType::OreRefinery,30,20,true,type("NAREFN"));
    const auto diverted=world.spawnUnit(0,UnitType::WarMiner,25.5f,24.5f,type("HARV"));world.ents[diverted].oreLoad=40;
    order(World::Cmd::ReturnHarvester,{diverted});
    check(world.ents[diverted].nativeDock.receiver==world.entityRef(available) && roundTrip("free-refinery",180),
        "automatic return chooses a reachable free refinery instead of queuing behind a busy nearby one");
    advance(1000);check(world.harvestedIncome[0]==2000,"both refinery reservations complete their natural unloading jobs");
    for (bool fromBay : {false, true}) {
        if (!fresh()) return 1;
        const EID booked = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("NAREFN"));
        const EID spare = world.spawnBuilding(0, BldType::OreRefinery, 30, 20, true, type("NAREFN"));
        const EID original = fromBay ? world.spawnBuilding(0, BldType::OreRefinery, 36, 20, true, type("NAREFN")) : INVALID_EID;
        const Vec2f waitingAt = fromBay ? Vec2f{40.5f, 21.5f} : Vec2f{27.5f, 24.5f};
        const EID current = world.spawnUnit(0, UnitType::WarMiner, 24.5f, 21.5f, type("HARV"));
        const EID ordered = world.spawnUnit(0, UnitType::WarMiner, waitingAt.x, waitingAt.y, type("HARV"));
        if (!world.valid(booked) || !world.valid(spare) || !world.valid(current) || !world.valid(ordered)
            || (fromBay && !world.valid(original))) return 1;
        const auto atBooked = world.entityRef(booked);
        auto& leader = world.ents[current];
        auto& follower = world.ents[ordered];
        leader.dir = leader.turretDir = follower.dir = follower.turretDir = 1;
        leader.oreLoad = 24;
        order(World::Cmd::ReturnHarvester, {current}, booked);
        if (fromBay) {
            follower.oreLoad = 40;
            follower.gemLoad = 4;
            order(World::Cmd::ReturnHarvester, {ordered}, original);
        }
        for (int tick = 0; tick < 500 && (leader.nativeDock.phase != NativeDockPhase::Unload
            || (fromBay && follower.nativeDock.phase != NativeDockPhase::Unload)); ++tick) world.update();
        const bool occupied = leader.nativeDock.phase == NativeDockPhase::Unload && leader.nativeDock.receiver == atBooked
            && leader.oreLoad == 24 && world.harvestedIncome[0] == 0
            && (!fromBay || (follower.nativeDock.phase == NativeDockPhase::Unload
                && follower.nativeDock.receiver == world.entityRef(original) && follower.oreLoad == 40));
        if (!fromBay) {
            follower.oreLoad = 40;
            follower.gemLoad = 4;
        }
        order(World::Cmd::ReturnHarvester, {ordered}, booked);
        const bool admitted = fromBay ? follower.nativeDock.phase == NativeDockPhase::Exit
            && follower.pendingDockRefinery == atBooked
            : follower.nativeDock.receiver == atBooked && follower.nativeDock.phase == NativeDockPhase::Approach;
        const bool savedOrder = roundTrip(fromBay ? "explicit-busy-redirect" : "explicit-busy-order", 240);
        for (int tick = 0; tick < 180 && follower.nativeDock.phase == NativeDockPhase::Exit; ++tick) world.update();
        advance(55);
        const bool waiting = follower.nativeDock.receiver == atBooked && !follower.claimsNativeDock()
            && follower.x == waitingAt.x && follower.y == waitingAt.y && follower.oreLoad == 40 && follower.gemLoad == 4
            && leader.claimsNativeDock();
        std::printf("GROUND_EXPLICIT_WAIT from_bay=%d x=%.5f y=%.5f expected=%.2f,%.2f phase=%d lead=%d ore=%d gems=%d preferred=%d\n",
            fromBay, follower.x, follower.y, waitingAt.x, waitingAt.y, (int)follower.nativeDock.phase,
            (int)leader.nativeDock.phase, follower.oreLoad, follower.gemLoad, follower.hasPendingDockReturn());
        const auto receiverAfterWait = follower.nativeDock.receiver.id;
        const bool savedQueue = roundTrip(fromBay ? "explicit-busy-wait-after-exit" : "explicit-busy-wait", 940);
        if (!fromBay) {
            const auto file = std::filesystem::path(outputDir) / "explicit-busy-app11.sav";
            const bool written = saveGameFile(file.string().c_str());
            Game copy;
            const bool loaded = written && copy.loadGameFile(file.string().c_str())
                && copy.world.checksum() == world.checksum();
            std::ifstream saved(file, std::ios::binary);
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(saved), {}};
            const auto nested = embeddedWorldSaveOffset(bytes);
            const auto block = nested ? locateWorldSaveSection(std::span(bytes).subspan(*nested),
                WorldSaveSection::NativeDockRedirect) : std::nullopt;
            bool rejected = block && block->length == 24 && worldSaveHeaderMatches(std::span(bytes).subspan(*nested),"RA2WLD66");
            if (rejected) for (bool wrongHeader : {false, true}) {
                auto malformed = bytes;
                if (wrongHeader) {
                    malformed[*nested + 7] = '5';
                } else {
                    const auto replacement = world.entityRef(spare);
                    const auto record = *nested + block->offset + 12;
                    std::memcpy(malformed.data() + record, &replacement.id, sizeof(replacement.id));
                    std::memcpy(malformed.data() + record + sizeof(replacement.id), &replacement.generation, sizeof(replacement.generation));
                }
                const auto bad = std::filesystem::path(outputDir) /
                    (wrongHeader ? "explicit-busy-bad-header.sav" : "explicit-busy-bad-receiver.sav");
                { std::ofstream out(bad, std::ios::binary); out.write((const char*)malformed.data(), malformed.size()); }
                const auto before = world.checksum();
                rejected &= !loadGameFile(bad.string().c_str()) && world.checksum() == before;
            }
            check(loaded && rejected, "App restores a waiting ground miner and rejects a mismatched bay or historical header atomically");

            World stopped = world;
            stopped.map.bldOccRef = &stopped.bldOcc;
            World::Cmd stop{};
            stop.type = World::Cmd::Stop;
            stop.ids = {ordered};
            stopped.bindCommand(stop);
            stopped.applyCmd(0, stop);
            for (int tick = 0; tick < 200; ++tick) stopped.update();
            const auto& idle = stopped.ents[ordered];
            check(idle.state == UState::Idle && idle.nativeDock.phase == NativeDockPhase::None
                && !idle.hasPendingDockReturn() && idle.oreLoad == 40 && idle.gemLoad == 4
                && stopped.harvestedIncome[0] == 600,
                "Stop cancels a waiting refinery order and keeps the loaded cargo aboard");

            for (bool repeat : {true, false}) {
                World budgeted = world;
                budgeted.map.bldOccRef = &budgeted.bldOcc;
                const auto issue = [&](World::Cmd::Type kind, EID miner, EID target = INVALID_EID) {
                    World::Cmd cmd{};
                    cmd.type = kind;
                    cmd.ids = {miner};
                    cmd.a = target;
                    budgeted.bindCommand(cmd);
                    budgeted.applyCmd(0, cmd);
                };
                if (!repeat) issue(World::Cmd::ReturnHarvester, ordered);
                const auto& stagedMiner = budgeted.ents[ordered];
                const bool staged = stagedMiner.nativeDock.phase == NativeDockPhase::Approach
                    && stagedMiner.nativeDock.receiver == budgeted.entityRef(repeat ? booked : spare)
                    && stagedMiner.pendingDockRefinery == (repeat ? atBooked : World::EntityRef{});

                const EID helper = budgeted.spawnUnit(0, UnitType::WarMiner, 52.5f, 24.5f, type("HARV"));
                if (!budgeted.valid(helper)) {
                    check(false, "spawn the path-search fixture's helper miner");
                    continue;
                }
                constexpr int routeBudget = 8;
                for (int attempt = 0; attempt < routeBudget && budgeted.repathsThisTick < routeBudget; ++attempt)
                    issue(World::Cmd::ReturnHarvester, helper, attempt % 2 ? booked : spare);
                issue(World::Cmd::Stop, helper);
                const int usedBudget = budgeted.repathsThisTick;

                issue(World::Cmd::ReturnHarvester, ordered, booked);
                const auto& awaiting = budgeted.ents[ordered];
                const bool retained = awaiting.hasPendingDockReturn() && awaiting.pendingDockRefinery == atBooked
                    && awaiting.oreLoad == 40 && awaiting.gemLoad == 4
                    && awaiting.x == waitingAt.x && awaiting.y == waitingAt.y;
                World resumed;
                FILE* file = std::tmpfile();
                bool saved = file && budgeted.saveGame(file);
                if (saved) {
                    std::rewind(file);
                    saved = resumed.loadGame(file) && resumed.checksum() == budgeted.checksum();
                }
                if (file) std::fclose(file);

                budgeted.update();
                if (saved) resumed.update();
                const auto& queued = budgeted.ents[ordered];
                const bool queuedAfterReset = queued.nativeDock.phase == NativeDockPhase::Approach
                    && queued.nativeDock.receiver == atBooked && queued.pendingDockRefinery == atBooked
                    && queued.oreLoad == 40 && queued.gemLoad == 4 && !queued.claimsNativeDock()
                    && budgeted.ents[current].claimsNativeDock();
                bool exact = saved && resumed.checksum() == budgeted.checksum();
                bool correctReceiver = true;
                int deposits = 0;
                for (int tick = 0; tick < 940 && exact; ++tick) {
                    const int ore = budgeted.ents[ordered].oreLoad;
                    budgeted.update();
                    resumed.update();
                    exact = resumed.checksum() == budgeted.checksum();
                    const auto& miner = budgeted.ents[ordered];
                    if (miner.oreLoad < ore) {
                        ++deposits;
                        correctReceiver &= miner.nativeDock.phase == NativeDockPhase::Unload
                            && miner.nativeDock.receiver == atBooked && miner.x == 23.5f && miner.y == 21.5f;
                    }
                    if (!miner.oreLoad && miner.nativeDock.phase == NativeDockPhase::None) break;
                }
                const bool paid = correctReceiver && deposits == 2 && !budgeted.ents[ordered].oreLoad
                    && !budgeted.ents[ordered].gemLoad && budgeted.harvestedIncome[0] == 1700;
                std::printf("GROUND_EXPLICIT_BUDGET repeat=%d staged=%d budget=%d retained=%d saved=%d next=%d exact=%d deposits=%d paid=%d\n",
                    repeat, staged, usedBudget, retained, saved, queuedAfterReset, exact, deposits, paid);
                check(staged && usedBudget == routeBudget && retained && saved && queuedAfterReset && exact && paid,
                    "a selected berth survives an exhausted route budget and unloads after an immediate save and reload");
            }
        }
        int deposits = 0;
        float paidX = 0.f;
        bool exclusive = true, correctBay = true;
        for (int tick = 0; tick < 1250; ++tick) {
            const int before = follower.oreLoad;
            world.update();
            exclusive &= !(leader.claimsNativeDock() && follower.claimsNativeDock()
                && leader.nativeDock.receiver == follower.nativeDock.receiver);
            if (follower.oreLoad < before) {
                ++deposits;
                paidX = follower.x;
                correctBay &= follower.nativeDock.phase == NativeDockPhase::Unload && follower.nativeDock.receiver == atBooked
                    && follower.x == 23.5f && follower.y == 21.5f;
            }
            if (!leader.oreLoad && !follower.oreLoad && leader.nativeDock.phase == NativeDockPhase::None
                    && follower.nativeDock.phase == NativeDockPhase::None) break;
        }
        const auto income = world.harvestedIncome[0];
        std::printf("GROUND_EXPLICIT_BUSY from_bay=%d occupied=%d admitted=%d target=%d after_wait=%d waiting=%d "
            "saved_order=%d saved_wait=%d exclusive=%d paid_x=%.2f deposits=%d income=%lld\n",
            fromBay, occupied, admitted, booked, receiverAfterWait, waiting, savedOrder, savedQueue,
            exclusive, paidX, deposits, (long long)income);
        check(occupied && admitted && waiting && savedOrder && savedQueue && exclusive && correctBay && paidX == 23.5f
            && deposits == 2 && income == 1700 && !leader.oreLoad && !follower.oreLoad
            && leader.nativeDock.phase == NativeDockPhase::None && follower.nativeDock.phase == NativeDockPhase::None,
            "an explicit ground return waits for the chosen live berth and unloads both cargo batches there");
    }
    for(const auto interrupted:{NativeDockPhase::None,NativeDockPhase::Approach,NativeDockPhase::Unload}) {
        if(!fresh())return 1;
        const EID frozenRef=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const EID spareRef=world.spawnBuilding(0,BldType::OreRefinery,31,20,true,type("NAREFN"));
        const EID miner=world.spawnUnit(0,UnitType::WarMiner,27.5f,21.5f,type("HARV"));
        if(!world.valid(frozenRef) || !world.valid(spareRef) || !world.valid(miner))return 1;
        world.ents[miner].oreLoad=40;
        if(interrupted!=NativeDockPhase::None)order(World::Cmd::ReturnHarvester,{miner},frozenRef);
        if(interrupted==NativeDockPhase::Approach)advance(12);
        if(interrupted==NativeDockPhase::Unload)
            for(int t=0;t<800 && world.ents[miner].nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
        const bool staged=world.ents[miner].nativeDock.phase==interrupted;
        const EID caster=world.spawnUnit(1,UnitType::Chrono,19.5f,19.5f,type("CLEG"));
        if(!world.valid(caster))return 1;
        world.map.reveal(1,20,20,8);
        World::Cmd attack{};
        attack.type=World::Cmd::Attack;attack.ids={caster};attack.a=frozenRef;
        world.bindCommand(attack);world.applyCmd(1,attack);
        for(int t=0;t<20 && !world.ents[frozenRef].immobilized();++t)world.update();
        const bool frozen=world.ents[frozenRef].immobilized() && world.nativeTemporalBeams.contains(caster);
        if(interrupted==NativeDockPhase::None)order(World::Cmd::ReturnHarvester,{miner},frozenRef);
        const auto& vehicle=world.ents[miner];
        const auto spareIdentity=world.entityRef(spareRef);
        const bool resumed=roundTrip("frozen-refinery-"+std::to_string((int)interrupted),550);
        bool rerouted=false,heldCargo=true,legalDeposit=true,stillFrozen=true;
        bool exited=interrupted!=NativeDockPhase::Unload;
        int deposits=0;
        const auto repaths=world.repathRequests;
        for(int t=0;t<1100;++t) {
            const auto income=world.harvestedIncome[0];
            const bool atSpareBay=vehicle.nativeDock.phase==NativeDockPhase::Unload
                && vehicle.nativeDock.receiver==spareIdentity && vehicle.x==34.5f && vehicle.y==21.5f;
            const auto phase=vehicle.nativeDock.phase;
            if(deposits==0)heldCargo &= vehicle.oreLoad==40;
            rerouted |= vehicle.nativeDock.receiver==spareIdentity;
            world.update();
            exited |= phase==NativeDockPhase::Exit && vehicle.x==24.5f && vehicle.y==21.5f;
            stillFrozen &= world.valid(frozenRef) && world.ents[frozenRef].immobilized();
            if(world.harvestedIncome[0]!=income) {
                ++deposits;
                legalDeposit &= atSpareBay && world.harvestedIncome[0]-income==1000;
            }
            if(!vehicle.oreLoad && vehicle.nativeDock.phase==NativeDockPhase::None)break;
        }
        std::printf("GROUND_DOCK_DISABLED stage=%d staged=%d frozen=%d rerouted=%d exited=%d replay=%d deposits=%d income=%lld replans=%llu\n",
            (int)interrupted,staged,frozen && stillFrozen,rerouted,exited,resumed,deposits,(long long)world.harvestedIncome[0],
            (unsigned long long)(world.repathRequests-repaths));
        check(staged && frozen && stillFrozen && rerouted && exited && resumed && heldCargo && legalDeposit && deposits==1
            && world.harvestedIncome[0]==1000 && vehicle.oreLoad==0 && vehicle.nativeDock.phase==NativeDockPhase::None,
            "a frozen receiver releases a loaded miner to a functioning refinery before any unloading credit");
    }
    for(int scenario=0;scenario<3;++scenario) {
        if(!fresh())return 1;
        const EID primary=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const EID alternate=scenario==2?INVALID_EID:world.spawnBuilding(0,BldType::OreRefinery,31,20,true,type("NAREFN"));
        const EID blocker=world.spawnUnit(0,UnitType::Rhino,27.5f,21.5f,type("HTNK"));
        const EID miner=world.spawnUnit(0,UnitType::WarMiner,29.5f,21.5f,type("HARV"));
        if(!world.valid(primary) || !world.valid(blocker) || !world.valid(miner) || (scenario!=2 && !world.valid(alternate)))return 1;
        order(World::Cmd::Move,{blocker},0,24.5f,21.5f);
        for(int t=0;t<600 && world.ents[blocker].state!=UState::Idle;++t)world.update();
        const bool parked=world.ents[blocker].x==24.5f && world.ents[blocker].y==21.5f;
        const bool guard=scenario!=1;
        if(guard)order(World::Cmd::Guard,{blocker});
        auto& hauler=world.ents[miner];hauler.oreLoad=40;
        order(World::Cmd::ReturnHarvester,{miner},primary);
        const bool admitted=hauler.nativeDock.receiver==world.entityRef(primary);
        const EID intended=scenario==0?alternate:primary;
        const auto expectedReceiver=world.entityRef(intended);
        const float dockX=world.ents[intended].x+3.5f;
        bool waited=false,resumed=false,rerouted=false,guardHeld=true,cargoHeld=true,legalDeposit=true;
        int deposits=0;
        for(int t=0;t<1600;++t) {
            if(!waited && hauler.blockTick>=6) {
                waited=true;
                resumed=roundTrip("occupied-approach-"+std::to_string(scenario),260);
            }
            if(scenario==2 && t==310)order(World::Cmd::Move,{blocker},0,24.5f,16.5f);
            if(guard && (scenario!=2 || t<310))
                guardHeld &= world.ents[blocker].x==24.5f && world.ents[blocker].y==21.5f && world.ents[blocker].guard;
            if(!deposits)cargoHeld &= hauler.oreLoad==40;
            const auto income=world.harvestedIncome[0];
            const bool atBay=hauler.nativeDock.phase==NativeDockPhase::Unload && hauler.nativeDock.receiver==expectedReceiver
                && hauler.x==dockX && hauler.y==21.5f && (scenario!=2 || t>310);
            rerouted |= world.valid(alternate) && hauler.nativeDock.receiver==world.entityRef(alternate);
            world.update();
            if(world.harvestedIncome[0]!=income) {
                ++deposits;legalDeposit &= atBay && world.harvestedIncome[0]-income==1000;
            }
            if(!hauler.oreLoad && hauler.nativeDock.phase==NativeDockPhase::None)break;
        }
        std::printf("GROUND_DOCK_TRAFFIC scenario=%d parked=%d admitted=%d waited=%d replay=%d rerouted=%d guard=%d deposits=%d income=%lld\n",
            scenario,parked,admitted,waited,resumed,rerouted,guardHeld,deposits,(long long)world.harvestedIncome[0]);
        check(parked && admitted && waited && resumed && guardHeld && cargoHeld && legalDeposit && rerouted==(scenario==0)
            && deposits==1 && world.harvestedIncome[0]==1000 && hauler.oreLoad==0 && hauler.nativeDock.phase==NativeDockPhase::None,
            "a loaded miner resolves a blocked approach through yielding, a free refinery, or waiting for the sole approach");
    }
    // An unrelated parked vehicle can obstruct the exit after docking. It is
    // moved through ordinary cooperative traffic, never teleported or crushed.
    if(!fresh())return 1;
    const auto obstructed=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
    const auto leaving=world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));world.ents[leaving].oreLoad=40;
    // A separate field leaves time to observe the exit without a second delivery in the existing window.
    const bool blockedHarvestAdmitted=orderExitFixtureHarvest(leaving,{44,21});
    order(World::Cmd::ReturnHarvester,{leaving},obstructed);
    for(int t=0;t<800 && world.ents[leaving].nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
    const auto parkedTank=world.spawnUnit(0,UnitType::Grizzly,24.5f,21.5f,type("MTNK"));
    advance(singleExitTicks+1);
    const auto& leavingActor=world.ents[leaving];
    const bool waiting=leavingActor.nativeDock.phase==NativeDockPhase::DriveExit && leavingActor.waitingForNativeDeparture()
        && leavingActor.digTimer==1-exitHandoffTicks;
    const bool exitSaved=roundTrip("blocked-exit",240,"RA2WLD73");
    int crossingRefusedAt=-1;
    bool noBankedCredit=true,separateDuringExit=true;
    for(int t=1;t<=500;++t) {
        world.update();
        if(leavingActor.nativeDock.phase!=NativeDockPhase::DriveExit || !world.valid(parkedTank))continue;
        const auto& blocker=world.ents[parkedTank];
        const bool apronOccupied=(int)blocker.x==24 && (int)blocker.y==21 && blocker.navLayer==leavingActor.navLayer;
        if(!apronOccupied)continue;
        separateDuringExit &= leavingActor.x<24.f && world.unitAtCell(23,21,leavingActor.navLayer)==leaving;
        if(leavingActor.blockTick==0)continue;
        if(crossingRefusedAt<0 && leavingActor.digTimer==0 && leavingActor.x>23.9f && leavingActor.claimsNativeDock()
            && world.unitAtCell(24,21,leavingActor.navLayer)==parkedTank
            && world.cellHardBlockedForMove(24,21,leaving,leavingActor.navLayer))
            crossingRefusedAt=singleExitTicks+1+t;
        noBankedCredit &= leavingActor.nativeMovement.credit==0;
    }
    const bool blocked=std::abs(crossingRefusedAt-referenceApronEntry)<=2;
    check(blockedHarvestAdmitted && waiting && blocked && exitSaved && noBankedCredit && separateDuringExit && world.valid(parkedTank)
        && world.ents[leaving].nativeDock.phase==NativeDockPhase::None
        && world.harvestedIncome[0]==1000 && world.ents[parkedTank].x!=24.5f,
        "a blocked exit waits, survives saving and clears through legal friendly yielding without extra credits");
    std::printf("GROUND_BLOCKED waited=%d blocked=%d refused_at=%d credit_cleared=%d separate=%d saved=%d "
        "phase=%d miner=%.4f,%.4f blocker=%.4f,%.4f path=%d income=%lld\n",
        waiting,blocked,crossingRefusedAt,noBankedCredit,separateDuringExit,exitSaved,
        (int)world.ents[leaving].nativeDock.phase,world.ents[leaving].x,world.ents[leaving].y,world.ents[parkedTank].x,world.ents[parkedTank].y,
        (int)world.ents[parkedTank].path.size()-world.ents[parkedTank].pathIdx,(long long)world.harvestedIncome[0]);
    if(!fresh())return 1;
    const auto lowerRef=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
    const auto lowerMiner=world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));world.ents[lowerMiner].oreLoad=40;
    order(World::Cmd::ReturnHarvester,{lowerMiner},lowerRef);
    for(int t=0;t<800 && world.ents[lowerMiner].nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
    const auto deckOwner=world.spawnBuilding(0,BldType::Wall,34,4,true);
    world.map.bridgeCells.assign(world.map.cells.size(),{});
    for(int y=20;y<=22;++y)for(int x=24;x<=26;++x)world.map.bridgeCells[world.cellIdx(x,y)]={deckOwner,6,1};
    const auto upper=world.spawnUnit(0,UnitType::Grizzly,25.5f,21.5f,type("MTNK"));
    world.ents[upper].navLayer=world.ents[upper].previousNavLayer=1;world.unitOccSync(upper);
    const bool layers=world.cellHardBlockedForMove(24,21,upper,0) && !world.cellHardBlockedForMove(24,21,upper,1);
    World::Cmd upperMove{};upperMove.type=World::Cmd::Move;upperMove.ids={upper};upperMove.x=24.5f;upperMove.y=21.5f;upperMove.b=2;
    world.bindCommand(upperMove);world.applyCmd(0,upperMove);advance(240);
    check(layers && world.ents[upper].navLayer==1 && distf(world.ents[upper].x,world.ents[upper].y,24.5f,21.5f)<.1f,
        "a ground-level refinery exit reservation leaves a separate bridge-deck movement path available");
    // Explicit orders leave the bay before continuing; Stop retains its cargo.
    for(const auto interrupted:{NativeDockPhase::ForwardEntry,NativeDockPhase::Unload,NativeDockPhase::Exit})for(bool moving:{false,true}) {
        if(!fresh())return 1;
        const auto receiver=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const auto mover=world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));world.ents[mover].oreLoad=40;
        order(World::Cmd::ReturnHarvester,{mover},receiver);
        const auto atInterruption=[&] {
            const auto& e=world.ents[mover];
            return e.nativeDock.phase==interrupted && (interrupted!=NativeDockPhase::ForwardEntry || (int)e.x==23);
        };
        for(int t=0;t<800 && !atInterruption();++t)world.update();
        const bool reached=atInterruption();
        const int cargo=world.ents[mover].oreLoad;const auto cash=world.players[0].money;
        order(moving?World::Cmd::Move:World::Cmd::Stop,{mover},0,29.5f,21.5f);
        const bool saved=roundTrip("interrupt-"+std::to_string((int)interrupted)+"-"+std::to_string(moving),180);
        advance(500);const auto& e=world.ents[mover];
        check(reached && saved && e.nativeDock.phase==NativeDockPhase::None && e.x>=24.5f && e.oreLoad==cargo && world.players[0].money==cash
            && (!moving || distf(e.x,e.y,29.5f,21.5f)<.1f),"Stop/Move during entry, unloading or exit preserves cargo and the admitted order after leaving the bay");
    }
    for(int mutation=0;mutation<3;++mutation) {
        if(!fresh())return 1;
        const auto receiver=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
        const auto mover=world.spawnUnit(0,UnitType::WarMiner,24.5f,21.5f,type("HARV"));world.ents[mover].oreLoad=40;
        order(World::Cmd::ReturnHarvester,{mover},receiver);
        for(int t=0;t<800 && world.ents[mover].nativeDock.phase!=NativeDockPhase::Unload;++t)world.update();
        const bool reached=world.ents[mover].nativeDock.phase==NativeDockPhase::Unload;const auto oldRef=world.entityRef(receiver);
        if(mutation==0)world.kill(receiver,false);
        else {world.changeActorOwner(mutation==1?receiver:mover,1);world.synchronizeBuildingOwners();}
        const bool saved=roundTrip("receiver-change-"+std::to_string(mutation),90);advance(200);
        check(reached && saved && world.ents[mover].nativeDock.phase==NativeDockPhase::None && world.ents[mover].oreLoad==40
            && world.players[0].money==10000 && world.harvestedIncome[0]==0,"destruction or ownership changes release the bay without paying an invalid receiver or losing cargo");
        if(mutation==0) {
            const auto replacement=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("NAREFN"));
            check(replacement==receiver && !world.valid(oldRef) && world.ents[mover].nativeDock.receiver!=world.entityRef(replacement),
                "a replacement refinery cannot inherit an old generation-bound dock claim");
        }
    }
    if(!fresh())return 1;
    const auto tank=world.spawnUnit(0,UnitType::Grizzly,12.5f,12.5f,type("MTNK"));
    world.ents[tank].dir=7;world.ents[tank].turretDir=7;
    order(World::Cmd::Move,{tank},0,20.5f,12.5f);world.update();world.update();
    check(world.ents[tank].dir!=1 && world.ents[tank].x==12.5f,"a north-facing ROT5 tank cannot instantly drive east");
    check(roundTrip("turn-midway",90),"a physically turning tank resumes with identical movement and angles");
    advance(9);capture(tank,"turning-grizzly");
    for(int t=0;t<180 && world.ents[tank].x<13.5f;++t)world.update();
    check(world.nativeHullHeading(world.ents[tank],world.tick)==16384 && world.ents[tank].x>12.5f,"the tank completes its timed eastward turn and moves through the real order path");
    // Turn the turret toward a target behind the moving hull. It must not fire
    // through a 180-degree turn, and aiming must not rotate the stationary hull.
    if(!fresh())return 1;
    const auto attacker=world.spawnUnit(0,UnitType::Grizzly,15.5f,15.5f,type("MTNK"));
    const auto victim=world.spawnUnit(1,UnitType::Rhino,12.5f,15.5f,type("HTNK"));
    world.ents[attacker].dir=world.ents[attacker].turretDir=1;
    world.map.reveal(0,15,15,15);order(World::Cmd::Attack,{attacker},victim);const int hp=world.ents[victim].hp;
    advance(6);
    check(world.nativeHullHeading(world.ents[attacker],world.tick)==16384 && world.nativeTurretHeading(world.ents[attacker],world.tick)!=49152
        && world.ents[victim].hp==hp && world.ents[attacker].atkCd==0,
        "an independent turret consumes ROT time before firing at a target behind the hull");
    check(roundTrip("turret-midway",90),"a mid-aim save preserves the independent turret and first firing tick");
    advance(100);check(world.ents[victim].hp<hp,"the correctly aimed tank eventually fires through normal combat");
    // Exercise the real atomic application loader, whose staged World decoder
    // is also responsible for rejecting unsupported extension combinations.
    const auto statePath=std::filesystem::path(outputDir)/"ground-app11.sav";
    const bool appWritten=saveGameFile(statePath.string().c_str());
    std::ifstream savedFile(statePath,std::ios::binary);const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(savedFile),{}};
    const auto found=embeddedWorldSaveOffset(bytes);
    const size_t worldStart=found.value_or(bytes.size());
    const auto section=found?locateWorldSaveSection(std::span(bytes).subspan(worldStart),WorldSaveSection::NativeGround):std::nullopt;
    const bool hasRecord=section && section->length>=58;
    bool rejected=appWritten && hasRecord;
    for(int damage=0;damage<6 && hasRecord;++damage) {
        auto bad=bytes;
        if(damage==0)bad[worldStart+7]='0';
        if(damage==1)bad[worldStart+section->offset]^=1;
        if(damage==2)bad.resize(bad.size()-1);
        if(damage==3)bad[worldStart+section->offset+8+4]=2; // invalid initialized flag
        if(damage==4)bad[worldStart+section->offset+8+4+1+12]=129; // impossible ROT duration
        if(damage==5)bad[worldStart+section->offset+50]=1; // orphan dock generation
        const auto badPath=std::filesystem::path(outputDir)/("ground-app-invalid-"+std::to_string(damage)+".sav");
        {std::ofstream output(badPath,std::ios::binary);output.write((const char*)bad.data(),bad.size());}
        const auto before=world.checksum();rejected &= !loadGameFile(badPath.string().c_str()) && world.checksum()==before;
    }
    check(rejected,"malformed ground state and mismatched historical headers reject atomically");
    // Natural-match regression: a fixed-wing scout can reach a destination
    // within the flight controller's .4-cell arrival tolerance. It must finish
    // that order instead of resuming the same near-center leg forever.
    if(!fresh())return 1;
    const auto scout=world.spawnUnit(0,UnitType::Intruder,12.3f,12.3f,type("ORCA"));
    World::Cmd flight{};flight.type=World::Cmd::Move;flight.ids={scout};flight.x=12.5f;flight.y=12.5f;flight.attackMove=true;
    world.bindCommand(flight);world.applyCmd(0,flight);
    check(roundTrip("air-arrival",60),"a near-arrival flight order survives saving before completion");
    advance(2);
    check(!world.ents[scout].attackMove.active && world.ents[scout].state==UState::Circling,
        "a fixed-wing aircraft completes its physically arrived attack-move leg and remains available for the next scouting order");
    if(!fresh())return 1;
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
    for(auto& p:world.players)p.money=0;
    const auto recon1=world.spawnUnit(0,UnitType::Kirov,20.5f,20.5f,type("ZEP"));
    const auto recon2=world.spawnUnit(0,UnitType::Kirov,20.5f,20.5f,type("ZEP"));
    World hidden=world;hidden.map.bldOccRef=&hidden.bldOcc;hidden.kill(1,false);hidden.spawnBuilding(1,BldType::ConYard,42,45,true);
    SkirmishAI survey,otherSurvey;survey.reset(0);otherSurvey=survey;
    for(int i=0;i<15;++i){survey.update(world);otherSurvey.update(hidden);}
    const auto& one=world.ents[recon1];const auto& two=world.ents[recon2];
    check(one.attackMove.active && two.attackMove.active && distf(one.goalX,one.goalY,two.goalX,two.goalY)>=unitDef(one).sight-1,
        "automatic aircraft surveying assigns distinct fog regions instead of duplicating the same slow flight");
    check(survey.knownBuildings.empty() && otherSurvey.knownBuildings.empty()
        && one.goalX==hidden.ents[recon1].goalX && one.goalY==hidden.ents[recon1].goalY
        && two.goalX==hidden.ents[recon2].goalX && two.goalY==hidden.ents[recon2].goalY,
        "air-survey coordination uses only own orders and fog, independent of hidden enemy replacement");
    if (const char* directory=std::getenv("OPENRA2_LEGACY_UNLOAD_START_INPUT_DIR")) {
        struct SavedLoad { const char* file; UnitType type; int cargo; };
        for (const auto saved : {SavedLoad{"dock-phase-4.sav",UnitType::WarMiner,40},
                SavedLoad{"miner-current-stage-3.sav",UnitType::ChronoMiner,20}}) {
            const auto path=(std::filesystem::path(directory)/saved.file).string();
            World old, replay;
            FILE* file=std::fopen(path.c_str(),"rb");
            char header[8]{};
            const bool world64=file && std::fread(header,1,sizeof header,file)==sizeof header
                && std::memcmp(header,"RA2WLD64",sizeof header)==0;
            if(file)std::rewind(file);
            const bool loaded=world64 && old.loadGame(file);
            if(file)std::fclose(file);
            bool started=false,exact=false,held=true,paidInBay=false,cleared=false;
            int firstCredit=0,exitAt=0,deposits=0;
            if(loaded) {
                EID minerId=INVALID_EID;
                for(EID id=0;id<(EID)old.ents.size();++id)if(old.valid(id) && !old.ents[id].isBuilding
                    && old.ents[id].utype==saved.type && old.harvesterUnloading(old.ents[id]))minerId=id;
                if(old.valid(minerId)) {
                    const auto& actor=old.ents[minerId];
                    const auto receiver=actor.nativeDock.receiver;
                    const auto incomeBefore=old.harvestedIncome[0];
                    const auto moneyBefore=old.players[0].money;
                    const auto atBerth=[&] {return old.harvesterUnloading(actor) && actor.x==23.5f && actor.y==21.5f
                        && actor.nativeDock.receiver==receiver && old.valid(receiver) && actor.navLayer==old.ents[receiver.id].navLayer;};
                    started=old.nativeTravelEnabled() && atBerth() && actor.digTimer==0 && actor.claimsNativeDock()
                        && actor.oreLoad==saved.cargo && actor.gemLoad==0 && incomeBefore==0;
                    file=std::tmpfile();
                    exact=started && file && old.saveGame(file);
                    if(exact) {
                        std::rewind(file);
                        exact=std::fread(header,1,sizeof header,file)==sizeof header && std::memcmp(header,"RA2WLD64",sizeof header)==0;
                        std::rewind(file);
                        exact=exact && replay.loadGame(file) && replay.checksum()==old.checksum();
                    }
                    if(file)std::fclose(file);
                    for(int elapsed=1;elapsed<=180 && started;++elapsed) {
                        const auto prior=old.harvestedIncome[0];
                        old.update();
                        if(exact) {replay.update();exact=replay.checksum()==old.checksum();}
                        if(elapsed<dumpInterval)held &= atBerth() && actor.oreLoad==saved.cargo
                            && actor.digTimer==elapsed && old.harvestedIncome[0]==incomeBefore && old.players[0].money==moneyBefore;
                        if(old.harvestedIncome[0]!=prior) {
                            ++deposits;firstCredit=elapsed;
                            paidInBay=atBerth() && actor.oreLoad==0 && old.harvestedIncome[0]-prior==25*saved.cargo;
                        }
                        if(!exitAt && actor.nativeDock.phase==NativeDockPhase::Exit)exitAt=elapsed;
                        if(exitAt && actor.nativeDock.phase==NativeDockPhase::None) {
                            cleared=actor.x==24.5f && actor.y==21.5f && actor.oreLoad==0 && !actor.claimsNativeDock()
                                && old.players[0].money-moneyBefore==25*saved.cargo;
                            break;
                        }
                    }
                }
            }
            std::printf("GROUND_UNLOAD_LEGACY file=%s loaded=%d armed=%d held=%d credit=%d exit=%d deposits=%d paid=%d cleared=%d replay=%d\n",
                saved.file,loaded,started,held,firstCredit,exitAt,deposits,paidInBay,cleared,exact);
            check(loaded && started && held && firstCredit==dumpInterval && exitAt==2*dumpInterval
                && deposits==1 && paidInBay && cleared && exact,
                "an actual World64 miner saved at timer zero keeps its first deposit at tick 30 and exits normally");
        }
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_GROUND_INPUT_DIR")) {
        for(const char* identity:{"CMIN","HARV"})for(bool gems:{false,true}) {
            const auto path=std::filesystem::path(directory)/(std::string("miner-0-")+identity+(gems?"-gems":"-ore")+"-inflight-world60.sav");
            World old,copy;FILE* f=std::fopen(path.string().c_str(),"rb");bool ok=f && old.loadGame(f);if(f)std::fclose(f);
            EID minerId=INVALID_EID;for(EID i=0;i<(EID)old.ents.size();++i)if(old.valid(i) && old.ents[i].oreLoad>0)minerId=i;
            const auto opening=old.players[0].money;const int expected=old.valid(minerId)?old.ents[minerId].oreLoad*(gems?50:25):0;
            f=tmpfile();ok=ok && f && old.saveGame(f);if(ok){std::rewind(f);ok=copy.loadGame(f);}if(f)std::fclose(f);
            for(int t=0;t<90 && ok;++t){old.update();copy.update();ok=old.checksum()==copy.checksum();}
            check(ok && old.nativeSceneVersion==44 && old.nativeProductionRules.version==7 && !old.nativeGroundEnabled()
                && old.players[0].money-opening==expected,"an actual pre-change Scene44 active miner retains the old unload behavior and exact continuation");
        }
    }
    shotFile.clear();
    std::printf("GROUND_RESULT failures=%d\n",failures);return failures;
}
