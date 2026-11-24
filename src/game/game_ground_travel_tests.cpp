#include "game_regression.h"
#include "game/script.h"
#include "game/world_priv.h"
#include "world_save_sections.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <optional>

int GameRegression::groundTravelRegression(const char* outputDir) {
    g_script.shutdown();
    std::filesystem::create_directories(outputDir);
    selectRulesChoice(RulesChoice::RA2);
    phase=Phase::InGame;localPlayer=0;netGame=false;matchSetup.reset();ais.clear();
    int failures=0;
    auto check=[&](bool passed,const char* message) {
        std::printf("GROUND_TRAVEL %s %s\n",passed?"PASS":"FAIL",message);
        failures+=!passed;
    };
    auto type=[&](const char* name) {
        for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name)return i;
        return -1;
    };
    auto fresh=[&](const World::RulesSnapshot* snapshot=nullptr) {
        if(!world.init(64,64,711,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,
            snapshot?nullptr:"assets/rules/profiles/ra2.ini",{Country::America,Country::Russia},snapshot))return false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;
        world.crates.clear();
        for(auto& player:world.players)player.isAI=false;
        world.spawnBuilding(0,BldType::ConYard,3,3,true);
        world.spawnBuilding(1,BldType::ConYard,50,50,true);
        world.players[0].money=10000;
        return true;
    };
    auto issue=[](World& gameWorld,World::Cmd::Type kind,EID id,int a,int b,float x,float y) {
        World::Cmd order{};
        order.type=kind;order.ids={id};order.a=a;order.b=b;order.x=x;order.y=y;
        gameWorld.bindCommand(order);
        gameWorld.applyCmd(0,order);
    };
    auto command=[&](World::Cmd::Type kind,EID id,int a=0,int b=0,float x=0,float y=0) {
        issue(world,kind,id,a,b,x,y);
    };
    const std::filesystem::path output(outputDir);
    auto roundTrip=[&](const World& original,const std::string& name,int ticks=300,const char* expectedHeader=nullptr) {
        const auto file=(output/(name+".sav")).string();
        FILE* stream=std::fopen(file.c_str(),"wb");
        bool same=stream && original.saveGame(stream);
        if(stream)std::fclose(stream);
        World resumed;
        stream=std::fopen(file.c_str(),"rb");
        if(stream && expectedHeader) {
            uint8_t header[13]{};
            same &= std::fread(header,1,sizeof header,stream)==sizeof header
                && worldSaveHeaderMatches(header,expectedHeader);
            std::rewind(stream);
        }
        same &= stream && resumed.loadGame(stream);
        if(stream)std::fclose(stream);
        World live=original;live.map.bldOccRef=&live.bldOcc;
        same &= live.checksum()==resumed.checksum();
        for(int i=0;i<ticks && same;++i) {
            live.update();resumed.update();
            same=live.checksum()==resumed.checksum();
        }
        return same;
    };
    auto checkApplication=[&](const char* name,uint8_t record,auto invalidField,size_t fieldOffset,const char* worldTag=nullptr) {
        const auto file=(output/(std::string(name)+"-app11.sav")).string();
        if(!saveGameFile(file.c_str()))return false;
        Game resumed;
        if(!resumed.loadGameFile(file.c_str()) || resumed.world.checksum()!=world.checksum())return false;
        std::ifstream in(file,std::ios::binary);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in),{}};
        const auto nested=embeddedWorldSaveOffset(bytes);
        if(!nested || (worldTag && !worldSaveHeaderMatches(std::span(bytes).subspan(*nested),worldTag)))return false;
        const auto block=locateWorldSaveSection(std::span(bytes).subspan(*nested),WorldSaveSection::NativeTravel);
        const size_t recordStart=8+4;
        if(!block || block->length<recordStart+1+fieldOffset+sizeof(invalidField))return false;
        const size_t index=*nested+block->offset+recordStart;
        if(bytes[index]!=record)return false;
        const auto before=world.checksum();
        const auto reject=[&](const char* suffix) {
            const auto invalid=(output/(std::string(name)+suffix+".sav")).string();
            std::ofstream out(invalid,std::ios::binary);
            out.write((const char*)bytes.data(),bytes.size());
            out.close();
            return out && !loadGameFile(invalid.c_str()) && world.checksum()==before;
        };
        bool mismatched=true;
        if(worldTag) {
            std::memcpy(bytes.data()+*nested,"RA2WLD64",8);
            mismatched=reject("-old-world");
            if(record==6) {
                std::memcpy(bytes.data()+*nested,"RA2WLD68",8);
                mismatched &= reject("-quadratic-world");
                std::memcpy(bytes.data()+*nested,"RA2WLD73",8);
                mismatched &= reject("-drive-exit-world");
            }
            std::memcpy(bytes.data()+*nested,worldTag,8);
        }
        if(record==6 && worldTag) {
            bytes[index]=4;
            mismatched &= reject("-quadratic-record");
            bytes[index]=record;
        }
        std::memcpy(bytes.data()+index+1+fieldOffset,&invalidField,sizeof(invalidField));
        return reject("-invalid") && mismatched;
    };
    constexpr double cardinalCreditPerTile = 256. * 7 / 11;
    const auto matchesArcPose = [](const World::Ent& vehicle, uint16_t heading) {
        const auto& arc = vehicle.driveArc;
        const auto position = NativeDriveMath::position(arc, arc.progress);
        const auto facing = NativeDriveMath::hullFacing(arc, arc.progress);
        return vehicle.x == position.x && vehicle.y == position.y
            && heading == NativeGroundMath::vector(facing.x, facing.y);
    };
    const auto retargetThroughExit = [&](const World& original, EID id, Vec2i destination, const std::string& name) {
        const auto& actor = original.ents[id];
        const auto heading = original.nativeHullHeading(actor, original.tick);
        World reference = original;
        reference.map.bldOccRef = &reference.bldOcc;
        World redirected = original;
        redirected.map.bldOccRef = &redirected.bldOcc;
        issue(redirected, World::Cmd::Move, id, 0, 0, destination.x + .5f, destination.y + .5f);
        const auto& updated = redirected.ents[id];
        const bool admitted = updated.state == UState::Moving && !updated.path.empty()
            && updated.path.back() == destination && updated.x == actor.x && updated.y == actor.y;
        const bool keptCurve = updated.driveArc == actor.driveArc
            && redirected.nativeHullHeading(updated, redirected.tick) == heading;
        const bool replay = roundTrip(redirected, name, 360);
        int comparisons = 0, equalSamples = 0, firstMovement = 0;
        for (int frame = 1; frame <= 120 && reference.ents[id].driveArc.active(); ++frame) {
            reference.update(); redirected.update();
            const auto& uninterrupted = reference.ents[id];
            ++comparisons;
            equalSamples += updated.x == uninterrupted.x && updated.y == uninterrupted.y
                && redirected.nativeHullHeading(updated, redirected.tick) == reference.nativeHullHeading(uninterrupted, reference.tick)
                && redirected.nativeTurretHeading(updated, redirected.tick) == reference.nativeTurretHeading(uninterrupted, reference.tick);
            if (!firstMovement && distf(updated.x, updated.y, actor.x, actor.y) > 1e-5f) firstMovement = frame;
        }
        for (int frame = 0; frame < 900 && updated.state != UState::Idle; ++frame) redirected.update();
        const bool arrived = updated.state == UState::Idle && distf(updated.x, updated.y, destination.x + .5f, destination.y + .5f) < .01f;
        std::printf("GROUND_TRAVEL_RETARGET admitted=%d progress=%.5f kept=%d equal=%d samples=%d first_motion=%d arrived=%d replay=%d case=%s\n",
            admitted, actor.driveArc.progress, keptCurve, equalSamples, comparisons, firstMovement, arrived, replay, name.c_str());
        return admitted && keptCurve && equalSamples == comparisons && comparisons > 0 && firstMovement == 1 && arrived && replay;
    };
    if(!fresh())return 1;
    for(auto& cell:world.map.cells)cell.terrain=Terrain::Water;
    for(int x=14;x<=26;++x)world.map.at(x,20)=Cell{};
    for(int y=20;y<=30;++y)world.map.at(26,y)=Cell{};
    const auto tank=world.spawnUnit(0,UnitType::Grizzly,14.5f,20.5f,type("MTNK"));
    if(!world.valid(tank))return 1;
    world.ents[tank].dir=world.ents[tank].turretDir=1;
    command(World::Cmd::Move,tank,0,0,26.5f,30.5f);
    const auto initialPath=world.ents[tank].path;
    check(initialPath.size()==22 && initialPath[11]==Vec2i{26,20} && initialPath[12]==Vec2i{26,21},
        "ordinary move admission builds the required right-angle route");
    const auto trajectory=std::filesystem::path(outputDir)/"tank-trajectory.csv";
    std::ofstream movement(trajectory);
    movement<<"tick,x,y,heading,path_index\n"<<std::setprecision(10);
    int curved=0;
    bool legal=true,tangent=true,paced=true,sampledArc=false,saveArc=false,retargeted=false,stopped=false,arcApp=false;
    bool reverseDirect=false,keptConvoyGate=false,mixedTravelReplay=false;
    bool sampledSharedTurn=false,continuedSharedTurn=false;
    int movingArcTicks=0,stallArcTicks=0,fullArcTicks=0;
    for(int t=0;t<1600;++t) {
        const auto previousArc=world.ents[tank].driveArc;
        world.update();
        const auto& actor=world.ents[tank];
        const auto heading=world.nativeHullHeading(actor,world.tick);
        movement<<world.tick<<','<<actor.x<<','<<actor.y<<','<<heading<<','<<actor.pathIdx<<'\n';
        legal &= world.map.passable((int)actor.x,(int)actor.y);
        if(actor.x>actor.px+1e-5f && actor.y>actor.py+1e-5f)++curved;
        if(actor.driveArc.active() && actor.driveArc.progress>0) {
            const double displacement=std::hypot(actor.x-actor.px,actor.y-actor.py);
            if(displacement>.00001) {
                ++movingArcTicks;
                tangent &= matchesArcPose(actor,heading);
                if(previousArc.active() && previousArc.progress>0) {
                    const double budget=actor.nativeMovement.heldSpeed*.5/cardinalCreditPerTile;
                    paced &= actor.driveArc.progress>previousArc.progress && displacement>=budget*.985 && displacement<=budget+.0001;
                    ++fullArcTicks;
                }
            } else ++stallArcTicks;
            if(!sampledSharedTurn && actor.driveArc.progress>=.15f) {
                sampledSharedTurn=true;
                continuedSharedTurn=retargetThroughExit(world,tank,{26,29},"tank-retarget-same-exit");
            }
            if(!sampledArc && actor.driveArc.progress>=.4f) {
                sampledArc=true;
                saveArc=roundTrip(world,"tank-mid-arc",360);
                arcApp=checkApplication("tank-arc",6,std::numeric_limits<float>::infinity(),18,"RA2WLD74");
                World mixed = world;
                mixed.map.bldOccRef = &mixed.bldOcc;
                for (int y = 35; y <= 43; ++y)
                    for (int x = 33; x <= 45; ++x) mixed.map.at(x, y) = Cell{};
                const auto mixedRefinery = mixed.spawnBuilding(0, BldType::OreRefinery, 36, 38, true, type("GAREFN"));
                const auto mixedMiner = mixed.spawnUnit(0, UnitType::ChronoMiner, 43.5f, 39.5f, type("CMIN"));
                if (mixed.valid(mixedRefinery) && mixed.valid(mixedMiner)) {
                    auto& returning = mixed.ents[mixedMiner];
                    returning.oreLoad = 20;
                    issue(mixed, World::Cmd::ReturnHarvester, mixedMiner, mixedRefinery, 0, 0, 0);
                    const bool pending = returning.chronoReturn.phase == ChronoReturnPhase::Charging
                        && returning.chronoReturn.directBerth && returning.chronoReturn.ticksLeft == ChronoReturnTiming::ready;
                    mixedTravelReplay = mixed.ents[tank].driveArc.cubic && pending
                        && roundTrip(mixed, "wide-arc-with-chrono-berth", 360,"RA2WLD74");
                }
                World alternate=world;alternate.map.bldOccRef=&alternate.bldOcc;
                const float x=actor.x,y=actor.y;
                issue(alternate,World::Cmd::Stop,tank,0,0,0,0);
                for(int i=0;i<20;++i)alternate.update();
                stopped=alternate.ents[tank].x==x && alternate.ents[tank].y==y && !alternate.ents[tank].driveArc.active();
                alternate=world;alternate.map.bldOccRef=&alternate.bldOcc;
                issue(alternate,World::Cmd::Move,tank,0,0,14.5f,20.5f);
                const auto& reversing=alternate.ents[tank];
                const bool released=!reversing.driveArc.active();
                const bool reverseSaved=roundTrip(alternate,"tank-reverse-after-order",360);
                World convoy=world;convoy.map.bldOccRef=&convoy.bldOcc;
                const auto follower=convoy.spawnUnit(0,UnitType::Grizzly,23.5f,20.5f,type("MTNK"));
                const auto previousGate=convoy.trafficDebug();
                if(convoy.valid(follower)) {
                    issue(convoy,World::Cmd::Move,follower,0,0,26.5f,30.5f);
                    issue(convoy,World::Cmd::Move,tank,0,0,14.5f,20.5f);
                    const auto currentGate=convoy.trafficDebug();
                    keptConvoyGate=previousGate.size()==1 && currentGate.size()==1
                        && currentGate.front().exit==previousGate.front().exit
                        && currentGate.front().remaining==previousGate.front().remaining
                        && convoy.ents[follower].state==UState::Moving;
                }
                int firstReverse=0,reverseError=0;
                reverseDirect=true;
                for(int i=0;i<700;++i) {
                    const float priorX=reversing.x,priorY=reversing.y;
                    alternate.update();
                    reverseDirect &= reversing.x<=x+.001f && reversing.y<=y+.001f;
                    if(!firstReverse && distf(reversing.x,reversing.y,priorX,priorY)>1e-5f) {
                        firstReverse=i+1;
                        const auto step=NativeGroundMath::vector(reversing.x-priorX,reversing.y-priorY);
                        reverseError=std::abs(NativeGroundMath::delta(alternate.nativeHullHeading(reversing,alternate.tick),step));
                    }
                }
                retargeted=released && distf(reversing.x,reversing.y,14.5f,20.5f)<.01f;
                const int allowance=reversing.nativeIdentity.definition->production.rotationRate*128;
                reverseDirect &= firstReverse>0 && reverseError<=allowance && reverseSaved;
                std::printf("GROUND_TRAVEL_REVERSE saved=%d stopped=%d released=%d first=%d error=%d direct=%d arrived=%d replay=%d convoy=%d x=%.5f y=%.5f path=%d/%zu\n",
                    saveArc,stopped,released,firstReverse,reverseError,reverseDirect,retargeted,reverseSaved,keptConvoyGate,
                    reversing.x,reversing.y,reversing.pathIdx,reversing.path.size());
            }
        }
        if(actor.state==UState::Idle)break;
    }
    check(legal && curved>=6 && distf(world.ents[tank].x,world.ents[tank].y,26.5f,30.5f)<.01f,
        "the tank advances on both axes around a legal bend and reaches the ordered cell");
    check(fullArcTicks>=5 && stallArcTicks==0 && tangent && paced,
        "a rolling tank uses the calibrated hull pose and its available drive distance through the right-angle bend");
    check(saveArc && stopped && retargeted,"mid-curve saving, Stop and a new real movement order preserve the physical turn position");
    check(mixedTravelReplay, "a wide drive arc and a pending direct Chrono return resume together from one save");
    check(reverseDirect,"a lone turning tank can reverse through the current lane without visiting its previous exit");
    check(keptConvoyGate,"an occupied lane retains its traffic phase when a tank reverses");
    check(sampledSharedTurn && continuedSharedTurn,
        "a new destination through the same exit continues the active curve and reaches the new order after saving");
    check(arcApp,"the application resumes an actual mid-curve save and rejects an invalid curve progress without replacing the world");
    std::printf("GROUND_TRAVEL_ARC moving_ticks=%d paused_ticks=%d curved_samples=%d full_ticks=%d tangent=%d paced=%d\n",
        movingArcTicks,stallArcTicks,curved,fullArcTicks,tangent,paced);

    const Vec2i wideCorner{26,20};
    const auto wideRoad=[&](int side,float startX=14.5f) {
        for(auto& cell:world.map.cells)cell.terrain=Terrain::Water;
        for(int x=14;x<=wideCorner.x;++x)world.map.at(x,wideCorner.y)=Cell{};
        for(int i=0;i<=10;++i)world.map.at(wideCorner.x,wideCorner.y+side*i)=Cell{};
        const EID id=world.spawnUnit(0,UnitType::Grizzly,startX,20.5f,type("MTNK"));
        if(world.valid(id))world.ents[id].dir=world.ents[id].turretDir=1;
        return id;
    };
    struct SteeringLandmark {int degrees;double lead,lateral;};
    constexpr std::array steering{
        SteeringLandmark{5,.56033,.057292},SteeringLandmark{45,.25,.214844},SteeringLandmark{85,.07552,.5191}
    };
    constexpr double referenceWideDistance=1.646717;
    for(int side:{1,-1}) {
        if(!fresh())return failures+1;
        const EID id=wideRoad(side);
        if(!world.valid(id))return failures+1;
        auto& vehicle=world.ents[id];
        const Vec2i exit{wideCorner.x,wideCorner.y+side};
        const float goalY=20.5f+side*10;
        command(World::Cmd::Move,id,0,0,26.5f,goalY);
        auto expectedPath=initialPath;
        for(auto& step:expectedPath)step.y=wideCorner.y+side*(step.y-wideCorner.y);
        const bool admitted=vehicle.state==UState::Moving && vehicle.path==expectedPath;

        const std::string name=side>0?"tank-wide-right":"tank-wide-left";
        bool legal=true,tangent=true,monotonic=true,handoff=true,beforeSaved=false,afterSaved=false;
        bool beforeSampled=false,afterSampled=false,finishedBend=false,wasWide=false;
        bool retargetSampled=false,retargetContinued=false;
        int entries=0,beforeSamples=0,afterSamples=0;
        std::array<Vec2f,steering.size()> angles{};
        size_t crossed=0;
        float firstHeadingLead=0.f,closest=1.f,lastX=25.5f,lastY=0.f;
        double distance=0.;
        for(int elapsed=0;elapsed<1600 && vehicle.state!=UState::Idle;++elapsed) {
            const auto beforeHeading=world.nativeHullHeading(vehicle,world.tick);
            const float beforeX=vehicle.x,beforeY=vehicle.y;
            world.update();
            const Vec2i cell{(int)vehicle.x,(int)vehicle.y};
            const float y=side*(vehicle.y-20.5f);
            const auto hull=world.nativeHullHeading(vehicle,world.tick);
            const auto& arc=vehicle.driveArc;
            legal &= vehicle.navLayer==0 && world.map.passable(cell.x,cell.y);
            if(!finishedBend && vehicle.x>=25.5f) {
                const float x=y>=1.f?26.5f:vehicle.x;
                const float nextY=std::min(y,1.f);
                distance+=std::hypot((double)x-lastX,(double)nextY-lastY);
                closest=std::min(closest,std::hypot(x-26.5f,nextY));
                lastX=x;lastY=nextY;
                finishedBend=y>=1.f;
            }
            const bool wide=arc.active() && arc.wide && arc.progress>0;
            if(wide) {
                entries+=!wasWide;
                const bool before=arc.progress<.5f;
                beforeSamples+=before;
                afterSamples+=!before;
                handoff &= arc.corner==wideCorner && arc.incoming==1 && arc.outgoing==Vec2i{0,side}
                    && arc.layer==0 && arc.cubic && vehicle.pathIdx==(before?11:12);
                legal &= cell==Vec2i{25,20} || cell==wideCorner || cell==exit;
                tangent &= matchesArcPose(vehicle,hull);
                const auto east=NativeGroundMath::fromDir(1);
                const int priorYaw=side*NativeGroundMath::delta(east,beforeHeading);
                const int yaw=side*NativeGroundMath::delta(east,hull);
                monotonic &= priorYaw>=0 && yaw>=priorYaw && yaw<=16384;
                while(crossed<steering.size()) {
                    const double threshold=steering[crossed].degrees*65536./360.;
                    if(priorYaw>=threshold || yaw<threshold)break;
                    const double fraction=(threshold-priorYaw)/(yaw-priorYaw);
                    angles[crossed++]={float(26.5-std::lerp(double(beforeX),double(vehicle.x),fraction)),
                        float(side*(std::lerp(double(beforeY),double(vehicle.y),fraction)-20.5))};
                }
                if(!firstHeadingLead && hull!=NativeGroundMath::fromDir(1)
                    && vehicle.x>vehicle.px && side*(vehicle.y-vehicle.py)>0.f)
                    firstHeadingLead=26.5f-vehicle.x;
                if(!beforeSampled && arc.progress>=.2f && before) {
                    beforeSampled=true;
                    beforeSaved=roundTrip(world,name+"-before-handoff",180,"RA2WLD74");
                }
                if(!afterSampled && arc.progress>=.6f) {
                    afterSampled=true;
                    afterSaved=roundTrip(world,name+"-after-handoff",180,"RA2WLD74");
                }
                if(!retargetSampled && cell==exit && vehicle.y!=exit.y+.5f) {
                    retargetSampled=true;
                    retargetContinued=retargetThroughExit(world,id,{26,20+side*9},name+"-retarget-in-exit");
                }
            }
            wasWide=wide;
        }
        const bool arrived=vehicle.state==UState::Idle && vehicle.x==26.5f && vehicle.y==goalY;
        bool calibrated=crossed==steering.size();
        for(size_t i=0;i<steering.size();++i) {
            calibrated &= std::abs(angles[i].x-steering[i].lead)<=.025
                && std::abs(angles[i].y-steering[i].lateral)<=.025;
        }
        std::printf("GROUND_TRAVEL_WIDE side=%d admitted=%d entries=%d samples=%d/%d lead=%.6f "
            "closest=%.6f distance=%.6f legal=%d tangent=%d handoff=%d replay=%d/%d arrived=%d\n",
            side,admitted,entries,beforeSamples,afterSamples,firstHeadingLead,closest,distance,legal,tangent,handoff,
            beforeSaved,afterSaved,arrived);
        std::printf("GROUND_TRAVEL_WIDE_YAW side=%d samples=%zu yaw5=%.6f,%.6f yaw45=%.6f,%.6f yaw85=%.6f,%.6f "
            "length=%.6f monotonic=%d calibrated=%d\n",side,crossed,angles[0].x,angles[0].y,angles[1].x,angles[1].y,
            angles[2].x,angles[2].y,distance,monotonic,calibrated);
        check(admitted && entries==1 && beforeSamples>=5 && afterSamples>=5 && firstHeadingLead>.5f
            && finishedBend && closest>=.30f && distance>=std::sqrt(2.)-.001 && distance<=1.72
            && legal && tangent && arrived,
            "a real cardinal Move turns before the corner, follows one wide bend and reaches the destination");
        check(calibrated && monotonic && std::abs(distance-referenceWideDistance)<=.015,
            "left and right wide turns cross the measured hull angles at the reference positions and path length");
        check(handoff && beforeSaved && afterSaved,
            "a wide bend transfers its waypoint at the midpoint and replays exactly from either side");
        check(retargetSampled && retargetContinued,
            "a Move within the exit cell continues the wide turn to its center with the same trajectory after a replan");
    }

    for(bool lateBlock:{false,true}) {
        if(!fresh())return failures+1;
        const EID id=wideRoad(1);
        if(!world.valid(id))return failures+1;
        // Both tanks leave the corridor through its south port, then park
        // beside each other in the bay. The corner route is unchanged.
        world.map.at(27,30)=Cell{};
        world.map.at(26,31)=Cell{};
        world.map.at(27,31)=Cell{};
        auto& vehicle=world.ents[id];
        command(World::Cmd::Move,id,0,0,26.5f,30.5f);
        const bool admitted=vehicle.state==UState::Moving && vehicle.path==initialPath;
        const auto blockExit=[&]() {
            const EID other=world.spawnUnit(0,UnitType::Grizzly,26.5f,21.5f,type("MTNK"));
            if(world.valid(other)) {
                world.ents[other].dir=world.ents[other].turretDir=3;
                command(World::Cmd::Guard,other);
            }
            return other;
        };
        EID blocker=lateBlock?INVALID_EID:blockExit();
        bool blocked=world.valid(blocker),legal=true,held=false,releaseOrdered=false,releaseAccepted=false,cleared=false;
        int waitTicks=0,wideSamples=0,compactSamples=0;
        for(int elapsed=0;elapsed<1600;++elapsed) {
            world.update();
            const auto& arc=vehicle.driveArc;
            if(arc.active() && arc.progress>0) {
                wideSamples+=arc.wide;
                compactSamples+=!arc.wide;
            }
            if(lateBlock && !blocked && arc.active() && arc.wide && arc.progress>=.5f
                && (int)vehicle.x==26 && (int)vehicle.y==20) {
                blocker=blockExit();
                blocked=world.valid(blocker);
            }
            const Vec2i cell{(int)vehicle.x,(int)vehicle.y};
            legal &= world.map.passable(cell.x,cell.y) && vehicle.navLayer==0;
            if(!blocked || !world.valid(blocker))continue;
            const auto& guard=world.ents[blocker];
            const Vec2i guardCell{(int)guard.x,(int)guard.y};
            const bool occupied=guardCell==Vec2i{26,21};
            legal &= world.map.passable(guardCell.x,guardCell.y) && guard.navLayer==0 && cell!=guardCell;
            cleared |= releaseAccepted && guardCell==Vec2i{26,22};
            if(!releaseOrdered && occupied && cell==wideCorner && vehicle.x==vehicle.px && vehicle.y==vehicle.py) {
                held=++waitTicks>=3;
                if(held) {
                    releaseOrdered=true;
                    command(World::Cmd::Move,blocker,0,0,27.5f,30.5f);
                    releaseAccepted=guard.state==UState::Moving && !guard.path.empty() && guard.path.back()==Vec2i{27,30};
                }
            } else waitTicks=0;
            if(vehicle.state==UState::Idle && guard.state==UState::Idle)break;
        }
        const bool arrived=vehicle.state==UState::Idle && vehicle.x==26.5f && vehicle.y==30.5f;
        const bool guardArrived=world.valid(blocker) && world.ents[blocker].state==UState::Idle
            && world.ents[blocker].x==27.5f && world.ents[blocker].y==30.5f;
        std::printf("GROUND_TRAVEL_WIDE_BLOCK late=%d admitted=%d blocked=%d wide=%d compact=%d "
            "legal=%d held=%d release=%d cleared=%d guard_arrived=%d arrived=%d\n",
            lateBlock,admitted,blocked,wideSamples,compactSamples,legal,held,releaseAccepted,cleared,guardArrived,arrived);
        check(admitted && blocked && legal && held && releaseAccepted && cleared && guardArrived && arrived
            && (lateBlock?wideSamples>0:wideSamples==0 && compactSamples>0),
            lateBlock ? "a blocker entering the path after wide admission is never crossed and the tank resumes after it clears"
                      : "an occupied exit selects a compact bend and remains blocked until a real Move clears it");
    }

    {
        if(!fresh())return failures+1;
        const EID turning=wideRoad(1,25.5f);
        for(int y=35;y<=43;++y)
            for(int x=33;x<=47;++x)world.map.at(x,y)=Cell{};
        const EID refinery=world.spawnBuilding(0,BldType::OreRefinery,36,38,true,type("NAREFN"));
        const EID leaving=world.spawnUnit(0,UnitType::WarMiner,40.5f,39.5f,type("HARV"));
        if(!world.valid(turning) || !world.valid(refinery) || !world.valid(leaving))return failures+1;
        const Vec2i deposit{45,39};
        auto& ore=world.map.at(deposit.x,deposit.y);
        ore.terrain=Terrain::Ore;ore.ore=ore.oreMax=80;
        auto& miner=world.ents[leaving];
        miner.dir=miner.turretDir=5;
        miner.oreLoad=40;
        command(World::Cmd::Harvest,leaving,deposit.x,deposit.y);
        const bool harvestAdmitted=miner.state==UState::HarvestGo && miner.oreCell==deposit
            && !miner.path.empty() && miner.path.back()==deposit;
        const auto receiver=world.entityRef(refinery);
        command(World::Cmd::ReturnHarvester,leaving,refinery);
        const bool returnAdmitted=miner.state==UState::HarvestReturn && miner.nativeDock.receiver==receiver;
        const auto drivingOut=[&] {
            return miner.nativeDock.phase==NativeDockPhase::DriveExit && miner.nativeDock.receiver==receiver
                && miner.claimsNativeDock() && miner.oreLoad==0 && miner.nativeMovement.started && miner.x>39.5f;
        };
        for(int t=0;t<600 && !drivingOut();++t)world.update();
        const bool startedExit=drivingOut() && world.harvestedIncome[0]==1000;
        command(World::Cmd::Move,turning,0,0,26.5f,30.5f);
        const auto& vehicle=world.ents[turning];
        const bool moveAdmitted=vehicle.state==UState::Moving && !vehicle.path.empty()
            && vehicle.path.front()==wideCorner && vehicle.path.back()==Vec2i{26,30};
        bool together=false,replay=false,app=false;
        for(int t=0;t<100 && drivingOut();++t) {
            world.update();
            const auto& arc=vehicle.driveArc;
            if(!arc.cubic || arc.progress<.4f || !drivingOut())continue;
            together=true;
            replay=roundTrip(world,"wide-arc-with-drive-exit",130,"RA2WLD74");
            app=checkApplication("wide-arc-with-drive-exit",6,std::numeric_limits<float>::infinity(),18,"RA2WLD74");
            break;
        }
        std::printf("GROUND_TRAVEL_WIDE_DRIVE_EXIT harvest=%d return=%d exit=%d move=%d together=%d replay=%d app=%d\n",
            harvestAdmitted,returnAdmitted,startedExit,moveAdmitted,together,replay,app);
        check(harvestAdmitted && returnAdmitted && startedExit && moveAdmitted && together && replay && app,
            "a real wide bend and an accelerating War Miner both resume from World74 and reject mismatched App headers");
    }

    {
        if (!fresh()) return failures + 1;
        const Vec2i entry{24, 20}, apron{24, 21}, destination{25, 21}, parking{26, 22}, deposit{34, 21};
        for (auto& cell : world.map.cells) cell.terrain = Terrain::Water;
        for (int y = 20; y <= 22; ++y)
            for (int x = 20; x <= 23; ++x) world.map.at(x, y) = Cell{};
        for (Vec2i cell : {entry, apron, destination, Vec2i{25, 22}, Vec2i{26, 21}, parking, deposit})
            world.map.at(cell.x, cell.y) = Cell{};
        auto& ore = world.map.at(deposit.x, deposit.y);
        ore.terrain = Terrain::Ore;
        ore.ore = ore.oreMax = 20;
        const EID refinery = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("GAREFN"));
        const EID miner = world.spawnUnit(0, UnitType::ChronoMiner, deposit.x + .5f, deposit.y + .5f, type("CMIN"));
        if (!world.valid(refinery) || !world.valid(miner)) return failures + 1;
        const auto receiver = world.entityRef(refinery);
        const auto incomeBefore = world.harvestedIncome[0];
        const int layer = world.ents[refinery].navLayer;
        auto& harvester = world.ents[miner];
        command(World::Cmd::Harvest, miner, deposit.x, deposit.y);
        const bool harvestAdmitted = harvester.state == UState::HarvestGo && harvester.oreCell == deposit;
        const auto claimsApron = [&] { return harvester.nativeDock.receiver == receiver && harvester.claimsNativeDock(); };
        const auto apronOccupant = [&] { return world.unitAtCell(apron.x, apron.y, layer); };
        bool filled = false, landed = false;
        for (int elapsed = 0; elapsed < 1200 && harvester.nativeDock.phase != NativeDockPhase::Unload; ++elapsed) {
            world.update();
            filled |= harvester.oreLoad == 20 && harvester.x == deposit.x + .5f && harvester.y == deposit.y + .5f
                && harvester.chronoReturn.phase == ChronoReturnPhase::Charging;
            landed |= filled && harvester.nativeDock.phase == NativeDockPhase::Align && claimsApron()
                && world.unitAtCell(23, 21, layer) == miner && apronOccupant() == INVALID_EID
                && harvester.oreLoad == 20 && world.harvestedIncome[0] == incomeBefore;
        }
        const bool unloading = harvester.nativeDock.phase == NativeDockPhase::Unload && claimsApron()
            && harvester.x == 23.5f && harvester.y == 21.5f && harvester.navLayer == layer && harvester.oreLoad == 20;
        const EID id = world.spawnUnit(0, UnitType::Grizzly, entry.x + .5f, entry.y + .5f, type("MTNK"));
        if (!world.valid(id)) return failures + 1;
        auto& vehicle = world.ents[id];
        vehicle.dir = vehicle.turretDir = 3;
        command(World::Cmd::Move, id, 0, 0, destination.x + .5f, destination.y + .5f);
        const bool admitted = vehicle.state == UState::Moving && vehicle.path == std::vector<Vec2i>{apron, destination};
        const bool emptyClaim = claimsApron() && apronOccupant() == INVALID_EID
            && world.cellHardBlockedForMove(apron.x, apron.y, id, layer);
        bool sameLayer = vehicle.navLayer == layer, exclusive = true, vacated = false, parkingAdmitted = false;
        int reservedEntry = 0, wideEntry = 0, hardBeforeEntry = 0, releasedAt = 0;
        for (int elapsed = 1; elapsed <= 900; ++elapsed) {
            const bool clearButReserved = claimsApron() && apronOccupant() == INVALID_EID
                && world.cellHardBlockedForMove(apron.x, apron.y, id, layer);
            world.update();
            sameLayer &= vehicle.navLayer == layer && harvester.navLayer == layer;
            const Vec2i vehicleCell{(int)vehicle.x, (int)vehicle.y}, minerCell{(int)harvester.x, (int)harvester.y};
            exclusive &= vehicleCell != minerCell;
            if (!reservedEntry && claimsApron() && vehicleCell == apron && apronOccupant() == id) {
                reservedEntry = elapsed;
                wideEntry = vehicle.driveArc.active() && vehicle.driveArc.wide && vehicle.driveArc.corner == apron;
                hardBeforeEntry = clearButReserved;
            }
            if (!releasedAt && harvester.nativeDock.phase == NativeDockPhase::None && apronOccupant() == miner
                && harvester.x == apron.x + .5f && harvester.y == apron.y + .5f) {
                releasedAt = elapsed;
                command(World::Cmd::Move, miner, 0, 0, parking.x + .5f, parking.y + .5f);
                parkingAdmitted = harvester.state == UState::Moving && !harvester.path.empty() && harvester.path.back() == parking;
            }
            vacated |= releasedAt && apronOccupant() != miner;
            if (vehicle.state == UState::Idle && harvester.state == UState::Idle) break;
        }
        const bool parked = harvester.state == UState::Idle && harvester.x == parking.x + .5f && harvester.y == parking.y + .5f;
        const bool arrived = vehicle.state == UState::Idle && vehicle.x == destination.x + .5f && vehicle.y == destination.y + .5f;
        const auto income = world.harvestedIncome[0] - incomeBefore;
        std::printf("GROUND_TRAVEL_DOCK_TURN harvest=%d filled=%d landed=%d unload=%d move=%d empty_reserved=%d same_layer=%d "
            "reserved_entry=%d wide_entry=%d hard_before_entry=%d exclusive=%d released=%d parking_order=%d vacated=%d parked=%d "
            "arrived=%d credit=%lld\n", harvestAdmitted, filled, landed, unloading, admitted, emptyClaim, sameLayer,
            reservedEntry, wideEntry, hardBeforeEntry, exclusive, releasedAt, parkingAdmitted, vacated, parked, arrived, (long long)income);
        check(harvestAdmitted && filled && landed && unloading && admitted && emptyClaim && sameLayer,
            "a normal Harvest claims an empty exit apron on the tracked tank's admitted right-angle route");
        check(reservedEntry == 0 && exclusive, "a drive arc never enters the refinery apron while the miner holds it");
        check(releasedAt > 0 && parkingAdmitted && vacated && parked && arrived && harvester.oreLoad == 0 && income == 500,
            "the miner unloads once and drives clear, allowing the tank to finish its original Move");
    }

    const World::RulesSnapshot stockRotation{world.nativeScenarioSource};
    World::RulesSnapshot fastRotation = stockRotation;
    const std::string grizzlySection = "\n[NativeProduction.Type." + std::to_string(type("MTNK")) + "]";
    const auto grizzlyHeader = fastRotation.source.find(grizzlySection);
    const auto grizzlyFields = grizzlyHeader == std::string::npos ? grizzlyHeader : grizzlyHeader + grizzlySection.size();
    const auto grizzlyEnd = fastRotation.source.find("\n[", grizzlyFields);
    const std::string stockRate = "\nROT = 5";
    const auto rotationField = fastRotation.source.find(stockRate, grizzlyFields);
    if (rotationField == std::string::npos || rotationField >= grizzlyEnd
        || fastRotation.source.find_first_of("\r\n", rotationField + stockRate.size()) != rotationField + stockRate.size()) {
        check(false, "the paired rolling test finds the Grizzly's single ROT field in the loaded profile");
        return failures;
    }
    fastRotation.source.replace(rotationField, stockRate.size(), "\nROT = 10");
    struct RotationSample {
        float x, y, progress;
        uint16_t heading;
        int pathIndex, speed;
        bool operator==(const RotationSample&) const = default;
    };
    struct RotationRun {
        std::vector<RotationSample> samples;
        int rotation = 0, maxSpeed = 0, entry = 0, exit = 0, pivot = 0, firstMotion = 0;
        bool admitted = false, aligned = false, arrived = false, parked = true, pivotArrived = false, replay = false;
    };
    RotationRun rotationRuns[2];
    for (int test = 0; test < 2; ++test) {
        const auto& snapshot = test == 0 ? stockRotation : fastRotation;
        if (!fresh(&snapshot)) return failures + 1;
        for (auto& cell : world.map.cells) cell.terrain = Terrain::Water;
        for (int x = 14; x <= 26; ++x) world.map.at(x, 20) = Cell{};
        for (int y = 20; y <= 30; ++y) world.map.at(26, y) = Cell{};
        const int definition = type("MTNK");
        const EID rolling = world.spawnUnit(0, UnitType::Grizzly, 14.5f, 20.5f, definition);
        if (!world.valid(rolling)) return failures + 1;
        auto& run = rotationRuns[test];
        {
            auto& vehicle = world.ents[rolling];
            vehicle.dir = vehicle.turretDir = 1;
            run.aligned = world.nativeHullHeading(vehicle, world.tick) == NativeGroundMath::fromDir(1);
            run.rotation = vehicle.nativeIdentity.definition->production.rotationRate;
            run.maxSpeed = vehicle.nativeIdentity.definition->movement.maxSpeed;
            command(World::Cmd::Move, rolling, 0, 0, 26.5f, 30.5f);
            run.admitted = vehicle.state == UState::Moving && vehicle.path == initialPath;
            bool saved = false;
            for (int elapsed = 1; elapsed <= 1600 && vehicle.state != UState::Idle; ++elapsed) {
                const auto previousArc = vehicle.driveArc;
                world.update();
                const auto& arc = vehicle.driveArc;
                run.samples.push_back({vehicle.x, vehicle.y, arc.progress, world.nativeHullHeading(vehicle, world.tick),
                    vehicle.pathIdx, vehicle.nativeMovement.heldSpeed});
                if (arc.corner == Vec2i{26, 20} && arc.progress > 0) {
                    if (!run.entry) run.entry = elapsed;
                    if (!saved && arc.progress >= .4f) {
                        saved = true;
                        run.replay = roundTrip(world, "tank-rolling-rot-" + std::to_string(run.rotation), 180);
                    }
                } else if (run.entry && !run.exit && previousArc.corner == Vec2i{26, 20} && !arc.active()
                    && vehicle.pathIdx == (previousArc.wide ? 13 : 12)) run.exit = elapsed;
            }
            run.arrived = vehicle.state == UState::Idle && vehicle.x == 26.5f && vehicle.y == 30.5f;
        }
        if (world.tick % 2) world.update();
        const EID pivot = world.spawnUnit(0, UnitType::Grizzly, 14.5f, 20.5f, definition);
        if (!world.valid(pivot)) return failures + 1;
        auto& vehicle = world.ents[pivot];
        vehicle.dir = vehicle.turretDir = 7;
        command(World::Cmd::Move, pivot, 0, 0, 19.5f, 20.5f);
        for (int elapsed = 1; elapsed <= 500 && vehicle.state != UState::Idle; ++elapsed) {
            world.update();
            if (!run.pivot && world.nativeHullHeading(vehicle, world.tick) == NativeGroundMath::fromDir(1)) run.pivot = elapsed;
            if (!run.firstMotion && vehicle.x != 14.5f) run.firstMotion = elapsed;
            if (!run.pivot) run.parked &= vehicle.x == 14.5f && vehicle.y == 20.5f;
        }
        run.pivotArrived = vehicle.state == UState::Idle && vehicle.x == 19.5f && vehicle.y == 20.5f;
        const auto entry = run.entry ? run.samples[run.entry - 1] : RotationSample{};
        const auto exit = run.exit ? run.samples[run.exit - 1] : RotationSample{};
        const int curveSteps = run.exit ? run.exit - run.entry + 1 : 0;
        std::printf("GROUND_TRAVEL_ROT_CASE rot=%d speed=%d held=%d aligned=%d admitted=%d corner=26,20 "
            "entry=%d:%.6f,%.6f exit=%d:%.6f,%.6f steps=%d arrived=%d pivot=%d first_motion=%d parked=%d pivot_arrived=%d replay=%d\n",
            run.rotation, run.maxSpeed, entry.speed, run.aligned, run.admitted, run.entry, entry.x, entry.y, run.exit, exit.x, exit.y,
            curveSteps, run.arrived, run.pivot, run.firstMotion, run.parked, run.pivotArrived, run.replay);
    }
    const auto& stock = rotationRuns[0];
    const auto& fast = rotationRuns[1];
    const auto difference = std::mismatch(stock.samples.begin(), stock.samples.end(), fast.samples.begin(), fast.samples.end());
    const int firstDifference = (int)(difference.first - stock.samples.begin()) + 1;
    const bool sameApproach = stock.entry > 0 && stock.entry == fast.entry && firstDifference >= stock.entry;
    const auto stockSample = difference.first != stock.samples.end() ? *difference.first : RotationSample{};
    const auto fastSample = difference.second != fast.samples.end() ? *difference.second : RotationSample{};
    std::printf("GROUND_TRAVEL_ROT_COMPARE approach=%d equal=%d first_difference=%d stock=%.6f,%.6f:%.6f:%u fast=%.6f,%.6f:%.6f:%u\n",
        sameApproach, stock.samples == fast.samples, stock.samples == fast.samples ? 0 : firstDifference,
        stockSample.x, stockSample.y, stockSample.progress, (unsigned)stockSample.heading,
        fastSample.x, fastSample.y, fastSample.progress, (unsigned)fastSample.heading);
    check(stock.rotation == 5 && fast.rotation == 10 && stock.maxSpeed > 0 && stock.maxSpeed == fast.maxSpeed
        && stock.aligned && fast.aligned && stock.admitted && fast.admitted && sameApproach && stock.exit > stock.entry
        && fast.exit > fast.entry && stock.arrived && fast.arrived && stock.replay && fast.replay,
        "matched Grizzlies have equal straight movement, follow the same admitted bend, arrive and replay");
    check(stock.pivot == 24 && fast.pivot == 12 && stock.firstMotion >= stock.pivot && fast.firstMotion >= fast.pivot
        && stock.parked && fast.parked && stock.pivotArrived && fast.pivotArrived,
        "native ROT still sets the time of the stationary turn before a straight Move");
    check(stock.samples == fast.samples, "changing stationary ROT leaves rolling bend position, progress and facing identical");

    for (bool beforeEntry : {false, true}) {
        const Vec2i corner{26, 20}, exit{26, 21}, targetOrigin{25, 33};
        const float entryX = corner.x + .5f - NativeDriveMath::cut;
        const std::string checkpoint = beforeEntry ? "tank-chase-entry" : "tank-chase-curve";
        bool witnessed = false, validRefresh = false, continued = false;
        for (int offset : {0, 8, 16, 24}) {
            // On this road, issuing Attack eight ticks later puts the refresh
            // between crossing into the corner cell and reaching the curve.
            if (beforeEntry && offset != 8) continue;
            if (!fresh()) return failures + 1;
            for (auto& cell : world.map.cells) cell.terrain = Terrain::Water;
            for (int x = 14; x <= corner.x; ++x) world.map.at(x, corner.y) = Cell{};
            // The uphill approach has normal tracked speed and keeps this
            // regression on the compact handoff inside the corner.
            for (int y = corner.y; y < targetOrigin.y; ++y) {
                auto& cell=world.map.at(corner.x,y);
                cell=Cell{};
                cell.height=1;
            }
            const int targetType = type("NAPOWR");
            if (targetType < 0) return failures + 1;
            const auto& footprint = world.nativeTypes[targetType]->building;
            for (int dy = 0; dy < footprint.h; ++dy)
                for (int dx = 0; dx < footprint.w; ++dx) world.map.at(targetOrigin.x + dx, targetOrigin.y + dy) = Cell{};
            const EID attacker = world.spawnUnit(0, UnitType::Grizzly, 14.5f, 20.5f, type("MTNK"));
            const EID target = world.spawnBuilding(1, BldType::TeslaReactor, targetOrigin.x, targetOrigin.y, true, targetType);
            if (!world.valid(attacker) || !world.valid(target)) return failures + 1;
            auto& tank = world.ents[attacker];
            tank.dir = tank.turretDir = 1;
            for (int t = 0; t < offset; ++t) world.update();
            const auto targetRef = world.entityRef(target);
            world.map.reveal(0, targetOrigin.x, targetOrigin.y, 5);
            command(World::Cmd::Attack, attacker, target);
            const bool admitted = tank.state == UState::Chasing && tank.target == target && tank.path.size() > 12
                && tank.path[11] == corner && tank.path[12] == exit;
            bool beforeReplay = false, afterReplay = false, originalPose = false;
            bool usedPlannedPath = false, keptArc = false, tangent = false, retainedTarget = false, inRange = false;
            uint64_t refreshTick = 0, requests = 0, rejectedPaths = 0;
            int firstMotion = -1, pathBefore = -1;
            float xBefore = 0, yBefore = 0, progressBefore = 0, xAfter = 0, yAfter = 0, progressAfter = 0;
            uint16_t headingBefore = 0, headingAfter = 0;
            std::vector<Vec2i> candidate;
            for (int elapsed = 0; elapsed < 1600 && tank.state == UState::Chasing; ++elapsed) {
                const auto arc = tank.driveArc;
                const auto expected = beforeEntry ? NativeDriveArc{corner, {0, 1}, 1, tank.navLayer, 0.f} : arc;
                const bool staged = beforeEntry
                    ? !arc.active() && tank.x >= corner.x && tank.x < entryX && tank.y == corner.y + .5f
                        && tank.x + tank.nativeMovement.heldSpeed * .5 / cardinalCreditPerTile > entryX + 1e-5
                    : arc.corner == corner && arc.progress > .1f && arc.progress < .75f;
                bool refresh = false;
                if (!witnessed && staged
                    && (world.tick + 1 + (uint64_t)attacker) % 30 == 0 && world.valid(targetRef) && tank.target == target
                    && tank.pathIdx + 1 < (int)tank.path.size() && tank.path[tank.pathIdx] == corner
                    && tank.path[tank.pathIdx + 1] == exit && (int)tank.x == corner.x && (int)tank.y == corner.y) {
                    World probe = world;
                    probe.map.bldOccRef = &probe.bldOcc;
                    refresh = attackPath(probe, probe.ents[attacker], probe.ents[target], candidate)
                        && !candidate.empty() && candidate.front() == exit;
                    if (refresh) {
                        witnessed = true;
                        pathBefore = tank.pathIdx;
                        xBefore = tank.x;
                        yBefore = tank.y;
                        progressBefore = arc.progress;
                        headingBefore = world.nativeHullHeading(tank, world.tick);
                        originalPose = beforeEntry ? headingBefore == NativeGroundMath::fromDir(1) : matchesArcPose(tank, headingBefore);
                        beforeReplay = roundTrip(world, checkpoint + "-before-repath", 180);
                        requests = world.repathRequests;
                        rejectedPaths = world.repathFailures;
                    }
                }
                const float priorX = tank.x, priorY = tank.y;
                world.update();
                if (refresh) {
                    refreshTick = world.tick;
                    requests = world.repathRequests - requests;
                    rejectedPaths = world.repathFailures - rejectedPaths;
                    usedPlannedPath = tank.path == candidate
                        || (tank.path.size() == candidate.size() + 1 && tank.path.front() == corner
                            && std::equal(candidate.begin(), candidate.end(), tank.path.begin() + 1));
                    xAfter = tank.x;
                    yAfter = tank.y;
                    progressAfter = tank.driveArc.progress;
                    headingAfter = world.nativeHullHeading(tank, world.tick);
                    keptArc = tank.driveArc.corner == expected.corner && tank.driveArc.incoming == expected.incoming
                        && tank.driveArc.outgoing == expected.outgoing && tank.driveArc.layer == expected.layer && progressAfter > progressBefore;
                    tangent = keptArc && matchesArcPose(tank, headingAfter);
                    retainedTarget = tank.state == UState::Chasing && tank.target == target && world.valid(targetRef);
                    afterReplay = roundTrip(world, checkpoint + "-after-repath", 180);
                }
                if (refreshTick && firstMotion < 0 && distf(tank.x, tank.y, priorX, priorY) > 1e-5f)
                    firstMotion = (int)(world.tick - refreshTick);
            }
            if (world.valid(targetRef) && tank.target == target && tank.state == UState::Attacking) {
                const auto& building = world.ents[target];
                float targetX = building.x, targetY = building.y;
                world.attackTargetPoint(tank, building, targetX, targetY);
                inRange = building.x == targetOrigin.x && building.y == targetOrigin.y
                    && distf(tank.x, tank.y, targetX, targetY) <= world.attackRange(tank, building);
            }
            const auto next = candidate.empty() ? Vec2i{-1, -1} : candidate.front();
            std::printf("%s offset=%d admitted=%d refresh=%llu requests=%llu failures=%llu "
                "path_before=%d candidate=%d,%d:%zu used=%d pose=%d replay=%d/%d target=%d range=%d\n",
                beforeEntry ? "GROUND_TRAVEL_CHASE_ENTRY" : "GROUND_TRAVEL_CHASE_CURVE",
                offset, admitted, (unsigned long long)refreshTick, (unsigned long long)requests, (unsigned long long)rejectedPaths,
                pathBefore, next.x, next.y, candidate.size(), usedPlannedPath, originalPose, beforeReplay, afterReplay, retainedTarget, inRange);
            if (witnessed) {
                std::printf("%s kept=%d tangent=%d first_motion=%d "
                    "before=%.6f,%.6f:%.6f:%u after=%.6f,%.6f:%.6f:%u\n",
                    beforeEntry ? "GROUND_TRAVEL_CHASE_ENTER" : "GROUND_TRAVEL_CHASE_CONTINUE",
                    keptArc, tangent, firstMotion, xBefore, yBefore, progressBefore, (unsigned)headingBefore,
                    xAfter, yAfter, progressAfter, (unsigned)headingAfter);
                validRefresh = admitted && refreshTick > 0 && requests == 1 && rejectedPaths == 0 && usedPlannedPath
                    && originalPose && beforeReplay && afterReplay && retainedTarget && inRange;
                continued = keptArc && tangent && firstMotion == 0 && xAfter > xBefore && yAfter > yBefore;
                break;
            }
        }
        check(validRefresh, beforeEntry
            ? "a bound Attack refreshes the same exit before arc entry, retains its target and reaches range after exact replay"
            : "a bound Attack performs a real same-exit chase refresh, retains the target and eventually reaches range");
        check(continued, beforeEntry
            ? "a same-exit chase refresh preserves the incoming leg and enters the planned curve on that update"
            : "a same-exit chase refresh advances the active arc on its tangent without a pause");
    }

    for (bool occupiedExit : {false, true}) {
        if (!fresh()) return 1;
        for (auto& cell : world.map.cells) cell.terrain = Terrain::Water;
        const Vec2i firstCorner{26, 20}, secondCorner{26, 21}, exit{27, 21};
        const Vec2i destination{39, 22}, blockerDestination{39, 24};
        for (int x = 14; x <= 26; ++x) world.map.at(x, 20) = Cell{};
        for (int x = 26; x < 35; ++x) world.map.at(x, 21) = Cell{};
        for (int y = 21; y <= 25; ++y)
            for (int x = 35; x <= 39; ++x) world.map.at(x, y) = Cell{};
        // A one-level approach keeps the two adjacent turns compact. The
        // temporary occupant only changes admission to the existing exit.
        world.map.at(26,20).height=1;
        for (int x=26;x<=39;++x)world.map.at(x,21).height=1;
        for (int y=22;y<=25;++y)
            for (int x=35;x<=39;++x)world.map.at(x,y).height=1;
        const int tankType = type("MTNK");
        const EID id = world.spawnUnit(0, UnitType::Grizzly, 14.5f, 20.5f, tankType);
        const EID blocker = occupiedExit ? world.spawnUnit(0, UnitType::Grizzly, 27.9f, 21.5f, tankType) : INVALID_EID;
        if (!world.valid(id) || (occupiedExit && !world.valid(blocker))) return 1;
        auto& vehicle = world.ents[id];
        vehicle.dir = vehicle.turretDir = 1;
        if (occupiedExit) world.ents[blocker].dir = world.ents[blocker].turretDir = 1;
        command(World::Cmd::Move, id, 0, 0, destination.x + .5f, destination.y + .5f);
        const bool admitted = vehicle.state == UState::Moving && vehicle.path.size() >= 14
            && vehicle.path[11] == firstCorner && vehicle.path[12] == secondCorner
            && vehicle.path[13] == exit && vehicle.path.back() == destination;
        bool safe = true, tangentAligned = true, blockerAdmitted = !occupiedExit, resumedWaiting = !occupiedExit;
        bool sampledSecond = false, savedSecondWithExpectedOccupancy = false, resumedSecond = false;
        bool enteredSecond = false, heldWhileOccupied = true;
        int firstArc = 0, secondArc = 0, postClearPivot = 0, alignedStartup = 0;
        int releasedAt = 0, clearedAt = 0;
        float waitingX = 0.f, waitingY = 0.f;
        uint16_t waitingHeading = 0;
        for (int elapsed = 1; elapsed <= 1000; ++elapsed) {
            world.update();
            const Vec2i cell{(int)vehicle.x, (int)vehicle.y};
            const auto heading = world.nativeHullHeading(vehicle, world.tick);
            const bool translated = distf(vehicle.x, vehicle.y, vehicle.px, vehicle.py) > 1e-5f;
            const auto& arc = vehicle.driveArc;
            if (arc.progress > 0 && translated && (arc.corner == firstCorner || arc.corner == secondCorner)) {
                if (arc.corner == firstCorner) ++firstArc;
                else ++secondArc;
                tangentAligned &= matchesArcPose(vehicle, heading);
            }
            safe &= world.map.passable(cell.x, cell.y) && world.unitAtCell(cell.x, cell.y) == id;
            if (occupiedExit) {
                const auto& leader = world.ents[blocker];
                const Vec2i occupied{(int)leader.x, (int)leader.y};
                safe &= cell != occupied && world.map.passable(occupied.x, occupied.y)
                    && world.unitAtCell(occupied.x, occupied.y) == blocker;
                enteredSecond |= cell == secondCorner;
                if (enteredSecond && occupied == exit) heldWhileOccupied &= cell == secondCorner;
                // Release after the tank tries to leave the second corner.
                // This works from the corner center or the end of an arc.
                if (!releasedAt && cell == secondCorner && occupied == exit && !translated && vehicle.blockTick >= 3) {
                    releasedAt = elapsed;
                    waitingX = vehicle.x;
                    waitingY = vehicle.y;
                    waitingHeading = heading;
                    command(World::Cmd::Move, blocker, 0, 0, blockerDestination.x + .5f, blockerDestination.y + .5f);
                    blockerAdmitted = leader.state == UState::Moving && !leader.path.empty() && leader.path.back() == blockerDestination;
                    resumedWaiting = roundTrip(world, "tank-adjacent-exit-wait", 480);
                }
                if (!clearedAt && releasedAt && occupied != exit) clearedAt = elapsed;
            }
            // The movement clock can restart once the exit is observed free.
            if ((!occupiedExit || (clearedAt && elapsed > clearedAt)) && cell == secondCorner && !translated) {
                if (heading == NativeGroundMath::fromDir(1)) ++alignedStartup;
                else ++postClearPivot;
            }
            if (!sampledSecond && arc.corner == secondCorner && arc.progress >= .4f) {
                sampledSecond = true;
                savedSecondWithExpectedOccupancy = cell == secondCorner
                    && (!occupiedExit || (!releasedAt && world.unitAtCell(exit.x, exit.y) == blocker));
                resumedSecond = roundTrip(world, occupiedExit ? "tank-adjacent-occupied-turn" : "tank-adjacent-free-turn", 320);
            }
            if (vehicle.state == UState::Idle && (!occupiedExit || world.ents[blocker].state == UState::Idle)) break;
        }
        const bool arrived = vehicle.state == UState::Idle
            && vehicle.x == destination.x + .5f && vehicle.y == destination.y + .5f;
        const bool blockerArrived = !occupiedExit || (world.ents[blocker].state == UState::Idle
            && world.ents[blocker].x == blockerDestination.x + .5f && world.ents[blocker].y == blockerDestination.y + .5f);
        const bool waitedAtCurveExit = !occupiedExit || (waitingX == secondCorner.x + .5f + NativeDriveMath::cut
            && waitingY == secondCorner.y + .5f && waitingHeading == NativeGroundMath::fromDir(1));
        std::printf("GROUND_TRAVEL_ADJACENT occupied=%d admitted=%d released=%d cleared=%d first_arc=%d second_arc=%d "
            "wait_xy=%.5f,%.5f wait_heading=%u post_clear_pivot=%d aligned_startup=%d safe=%d held=%d tangent=%d "
            "waiting_replay=%d second_occupancy=%d second_replay=%d arrived=%d blocker_arrived=%d\n",
            occupiedExit, admitted, releasedAt, clearedAt, firstArc, secondArc, waitingX, waitingY, (unsigned)waitingHeading,
            postClearPivot, alignedStartup, safe, heldWhileOccupied, tangentAligned, resumedWaiting, savedSecondWithExpectedOccupancy,
            resumedSecond, arrived, blockerArrived);
        check(admitted && safe && heldWhileOccupied && blockerAdmitted && (!occupiedExit || (releasedAt > 0 && clearedAt > releasedAt))
            && resumedWaiting && arrived && blockerArrived,
            "adjacent bend commands cross without overlapping the tank at the exit and replay from the wait");
        check(firstArc > 0 && secondArc > 0 && tangentAligned && savedSecondWithExpectedOccupancy && resumedSecond
            && waitedAtCurveExit && postClearPivot == 0,
            "a temporarily occupied exit preserves both curves and needs no pivot after it clears");
    }

    for (int side : {1, -1}) for (bool diagonalFirst : {false, true}) {
        if (!fresh()) return 1;
        for (auto& cell : world.map.cells) cell.terrain = Terrain::Water;
        const Vec2i near{14, 20}, corner{26, 20}, far{33, 20 + side * 7};
        for (int x = near.x; x <= corner.x; ++x) world.map.at(x, corner.y) = Cell{};
        for (int i = 1; i <= 7; ++i) {
            const int x = corner.x + i, y = corner.y + side * i;
            world.map.at(x, y) = Cell{};
            world.map.at(x - 1, y) = Cell{};
            world.map.at(x, y - side) = Cell{};
        }
        const Vec2i start = diagonalFirst ? far : near, destination = diagonalFirst ? near : far;
        const Vec2i incoming = diagonalFirst ? Vec2i{-1, -side} : Vec2i{1, 0};
        const Vec2i outgoing = diagonalFirst ? Vec2i{-1, 0} : Vec2i{1, side};
        const EID id = world.spawnUnit(0, UnitType::Grizzly, start.x + .5f, start.y + .5f, type("MTNK"));
        if (!world.valid(id)) return 1;
        auto& vehicle = world.ents[id];
        vehicle.dir = vehicle.turretDir = dirFromVec((float)incoming.x, (float)incoming.y);
        command(World::Cmd::Move, id, 0, 0, destination.x + .5f, destination.y + .5f);
        const bool admitted = vehicle.state == UState::Moving && !vehicle.path.empty() && vehicle.path.back() == destination;
        bool sampled = false, resumed = false, withinSpeed = true, tangentAligned = true;
        int fullSamples = 0;
        double minRatio = 1, maxRatio = 0;
        const double diagonalRate = 256. * 7 / (8 * std::sqrt(2.));
        for (int i = 0; i < 1000 && vehicle.state != UState::Idle; ++i) {
            const auto previous = vehicle.driveArc;
            world.update();
            const auto& arc = vehicle.driveArc;
            const bool expectedBend = arc.corner == corner && arc.outgoing == outgoing
                && NativeDriveMath::directions[arc.incoming] == incoming;
            if (!expectedBend || previous.corner != corner || previous.progress == 0) continue;
            tangentAligned &= matchesArcPose(vehicle, world.nativeHullHeading(vehicle, world.tick));
            const double displacement = std::hypot(vehicle.x - vehicle.px, vehicle.y - vehicle.py);
            // Use the neighboring tracks' rates. A diagonal sample covers 8 leptons along both axes.
            const double middle = (previous.progress + arc.progress) * .5;
            const double rate = diagonalFirst ? std::lerp(diagonalRate, cardinalCreditPerTile, middle)
                                              : std::lerp(cardinalCreditPerTile, diagonalRate, middle);
            const double budget = vehicle.nativeMovement.heldSpeed * .5 / rate;
            withinSpeed &= arc.progress > previous.progress && budget > 0 && displacement <= budget + .0001;
            if (budget > 0) {
                minRatio = std::min(minRatio, displacement / budget);
                maxRatio = std::max(maxRatio, displacement / budget);
                ++fullSamples;
            }
            if (!sampled && arc.progress >= .4f) {
                sampled = true;
                resumed = roundTrip(world, "tank-pace-" + std::to_string(side) + "-" + std::to_string(diagonalFirst), 300);
            }
        }
        const bool arrived = vehicle.state == UState::Idle && vehicle.x == destination.x + .5f && vehicle.y == destination.y + .5f;
        std::printf("GROUND_TRAVEL_CURVE_PACE side=%d diagonal_first=%d admitted=%d samples=%d min_ratio=%.5f max_ratio=%.5f "
            "speed=%d tangent=%d arrived=%d resumed=%d\n",
            side, diagonalFirst, admitted, fullSamples, minRatio, maxRatio, withinSpeed, tangentAligned, arrived, resumed);
        check(admitted && fullSamples >= 5 && minRatio >= .985 && withinSpeed && tangentAligned && arrived && resumed,
            "a legal 45-degree bend uses its available drive distance, follows the tangent and resumes from the curve");
    }

    for(int side:{1,-1}) {
        if(!fresh())return 1;
        for(auto& cell:world.map.cells)cell.terrain=Terrain::Water;
        world.map.at(20,20)=Cell{};
        // The uphill approach keeps both fast tanks on the compact entry.
        for(int y=14;y<=26;++y) {
            auto& road=world.map.at(21,y);
            road=Cell{};
            road.height=1;
        }
        const EID fastTank=world.spawnUnit(0,UnitType::Grizzly,20.5f,20.5f,type("MTNK"));
        if(!world.valid(fastTank))return 1;
        auto& vehicle=world.ents[fastTank];
        vehicle.dir=vehicle.turretDir=side>0?0:2;
        vehicle.vetRank=2;
        vehicle.crateSpeedBoost=2000;
        command(World::Cmd::Move,fastTank,0,0,21.5f,20.5f+side*6);
        int advancing=0;
        bool tangentAligned=true,withinDistance=true;
        bool entrySampled=false,entryResumed=false;
        std::optional<World> approach;
        for(int tick=0;tick<400 && vehicle.state!=UState::Idle;++tick) {
            const auto before=vehicle.driveArc;
            const int pathIndex=vehicle.pathIdx;
            if (!entrySampled && !before.active() && vehicle.x > 20.5f && vehicle.x <= 21.5f - NativeDriveMath::cut)
                approach.emplace(world);
            world.update();
            const bool advanced=vehicle.driveArc.active() && vehicle.driveArc.progress>before.progress;
            const bool leftCurve=before.active() && !vehicle.driveArc.active() && vehicle.pathIdx>pathIndex;
            if(advanced || leftCurve) {
                const auto heading=world.nativeHullHeading(vehicle,world.tick);
                const double displacement=std::hypot(vehicle.x-vehicle.px,vehicle.y-vehicle.py);
                const double budget=vehicle.nativeMovement.heldSpeed*.5/cardinalCreditPerTile;
                withinDistance &= displacement>1e-5 && displacement<=budget+.0001;
                tangentAligned &= advanced ? matchesArcPose(vehicle,heading)
                    : vehicle.x==21.5f && heading==NativeGroundMath::vector(0.f,(float)side);
                ++advancing;
            }
            if (!entrySampled && !before.active() && vehicle.driveArc.active() && approach) {
                entrySampled = true;
                approach->map.bldOccRef = &approach->bldOcc;
                const std::string name = side > 0 ? "tank-fast-right" : "tank-fast-left";
                entryResumed = roundTrip(*approach, name + "-before-entry", 180)
                    && roundTrip(world, name + "-entry", 180);
                std::printf("GROUND_TRAVEL_HANDOFF side=%d before_tick=%llu entry_tick=%llu progress=%.6f replay=%d\n",
                    side, (unsigned long long)approach->tick, (unsigned long long)world.tick,
                    vehicle.driveArc.progress, entryResumed);
            }
        }
        std::printf("GROUND_TRAVEL_TURN_BUDGET side=%d advancing=%d distance=%d tangent=%d\n",side,advancing,withinDistance,tangentAligned);
        check(advancing>0 && withinDistance && tangentAligned && distf(vehicle.x,vehicle.y,21.5f,20.5f+side*6)<.01f,
            "a fast tank follows the tangent and its movement budget when transitioning through a short curve");
        check(entrySampled && entryResumed,"save and resume on both sides of the handoff to the curve agrees tick for tick");
    }

    struct StraightStart { int facing; bool retarget; };
    const StraightStart starts[] = {{1, false}, {0, false}, {2, false}, {7, false},
                                    {3, false}, {6, false}, {5, false}, {1, true}};
    for (const auto [facing, retarget] : starts) {
        if (!fresh()) return 1;
        const EID id = world.spawnUnit(0, UnitType::Grizzly, 20.5f, 20.5f, type("MTNK"));
        if (!world.valid(id)) return failures + 1;
        auto& vehicle = world.ents[id];
        vehicle.dir = vehicle.turretDir = facing;
        bool rolling = false;
        if (retarget) {
            command(World::Cmd::Move, id, 0, 0, 34.5f, 20.5f);
            for (int i = 0; i < 100 && vehicle.x < 22.1f; ++i) world.update();
            rolling = vehicle.state == UState::Moving && vehicle.x > vehicle.px
                && vehicle.x >= 22.1f && vehicle.x < 22.5f && vehicle.y == 20.5f;
        }
        const Vec2i firstNode = retarget ? Vec2i{23, 19} : Vec2i{21, 20};
        const Vec2f destination = retarget ? Vec2f{28.5f, 14.5f} : Vec2f{26.5f, 20.5f};
        const auto wanted = NativeGroundMath::vector(firstNode.x + .5f - vehicle.x, firstNode.y + .5f - vehicle.y);
        const auto initialHeading = world.nativeHullHeading(vehicle, world.tick);
        const int startAngle = std::abs(NativeGroundMath::delta(initialHeading, wanted));
        const int turnAllowance = vehicle.nativeIdentity.definition->production.rotationRate * 128;
        const bool needsTurn = startAngle > turnAllowance;
        const int saveAt = startAngle > 8192 ? 8 : 4;
        const int firstDeadline = 2 * (startAngle / (2 * turnAllowance)) + 4;
        command(World::Cmd::Move, id, 0, 0, destination.x, destination.y);
        const bool admitted = vehicle.state == UState::Moving && !vehicle.path.empty() && vehicle.path.front() == firstNode;
        int first = 0, headingError = 0, peak = 0;
        bool replay = !needsTurn, promptTurn = !needsTurn, straight = true, waitingWithoutCredit = true;
        for (int elapsed = 1; elapsed <= 500 && vehicle.state != UState::Idle; ++elapsed) {
            const float previousX = vehicle.x, previousY = vehicle.y;
            world.update();
            const float dx = vehicle.x - previousX, dy = vehicle.y - previousY;
            const auto hull = world.nativeHullHeading(vehicle, world.tick);
            if (elapsed <= 3 && hull != initialHeading) promptTurn = true;
            straight &= dx >= 0 && (retarget ? dy <= 0 : dy == 0) && !vehicle.driveArc.active();
            if (std::hypot(dx, dy) > 1e-5f) {
                const int error = std::abs(NativeGroundMath::delta(hull, NativeGroundMath::vector(dx, dy)));
                if (!first) { first = elapsed; headingError = error; }
                peak = std::max(peak, error);
            } else if (!first && std::abs(NativeGroundMath::delta(hull, wanted)) > turnAllowance) {
                waitingWithoutCredit &= !vehicle.nativeMovement.started && vehicle.nativeMovement.credit == 0;
            }
            if (needsTurn && elapsed == saveAt && !first)
                replay = roundTrip(world, retarget ? "tank-retarget-straight-aim" : "tank-pivot-" + std::to_string(facing), 250);
        }
        const bool arrived = vehicle.state == UState::Idle && vehicle.x == destination.x && vehicle.y == destination.y;
        std::printf("GROUND_TRAVEL_PIVOT facing=%d retarget=%d rolling=%d start=%d admitted=%d first=%d deadline=%d "
            "error=%d peak=%d allowance=%d arrived=%d replay=%d prompt_turn=%d waiting_without_credit=%d straight=%d\n",
            facing, retarget, rolling, startAngle, admitted, first, firstDeadline, headingError, peak, turnAllowance,
            arrived, replay, promptTurn, waitingWithoutCredit, straight);
        check((!retarget || (rolling && needsTurn && startAngle < 8192)) && admitted && straight && arrived
            && replay && promptTurn && waitingWithoutCredit && first > 0 && first <= firstDeadline && peak <= turnAllowance,
            "the hull turns promptly and aligns before translating on its straight route");
    }

    struct StoppedPivot { Vec2i direction; int turretFacing; };
    const StoppedPivot stoppedPivots[] = {{{0, -1}, 2}, {{0, 1}, 0}, {{-1, 0}, 3}};
    for (int test = 0; test < 3; ++test) {
        if (!fresh()) return 1;
        const auto [direction, turretFacing] = stoppedPivots[test];
        const EID id = world.spawnUnit(0, UnitType::Grizzly, 20.5f, 20.5f, type("MTNK"));
        if (!world.valid(id)) return failures + 1;
        auto& vehicle = world.ents[id];
        vehicle.dir = 1;
        vehicle.turretDir = turretFacing;
        const float goalX = vehicle.x + 7.f * direction.x, goalY = vehicle.y + 7.f * direction.y;
        const auto wanted = NativeGroundMath::vector(direction.x, direction.y);
        command(World::Cmd::Move, id, 0, 0, goalX, goalY);
        for (int tick = 0; tick < 10 + test; ++tick) world.update();
        const auto hull = world.nativeHullHeading(vehicle, world.tick);
        const auto turret = world.nativeTurretHeading(vehicle, world.tick);
        const bool staged = vehicle.state == UState::Moving && vehicle.x == 20.5f && vehicle.y == 20.5f
            && hull != NativeGroundMath::fromDir(1) && turret != NativeGroundMath::fromDir(turretFacing)
            && hull != wanted && turret != wanted && hull != turret;
        command(World::Cmd::Stop, id);
        const bool stopped = vehicle.state == UState::Idle && vehicle.path.empty()
            && world.nativeHullHeading(vehicle, world.tick) == hull && world.nativeTurretHeading(vehicle, world.tick) == turret;
        const bool replay = roundTrip(world, "tank-pivot-stopped-" + std::to_string(test), 80);
        int hullDrift = 0, turretDrift = 0;
        bool stationary = true;
        for (int tick = 0; tick < 80; ++tick) {
            world.update();
            hullDrift = std::max(hullDrift, std::abs(NativeGroundMath::delta(hull, world.nativeHullHeading(vehicle, world.tick))));
            turretDrift = std::max(turretDrift, std::abs(NativeGroundMath::delta(turret, world.nativeTurretHeading(vehicle, world.tick))));
            stationary &= vehicle.state == UState::Idle && vehicle.x == 20.5f && vehicle.y == 20.5f;
        }
        command(World::Cmd::Move, id, 0, 0, goalX, goalY);
        const bool restarted = vehicle.state == UState::Moving
            && world.nativeHullHeading(vehicle, world.tick) == hull && world.nativeTurretHeading(vehicle, world.tick) == turret;
        int firstMotion = 0, firstError = 0;
        for (int tick = 1; tick <= 450 && vehicle.state == UState::Moving; ++tick) {
            const float x = vehicle.x, y = vehicle.y;
            world.update();
            if (!firstMotion && (x != vehicle.x || y != vehicle.y)) {
                firstMotion = tick;
                const auto moved = NativeGroundMath::vector(vehicle.x - x, vehicle.y - y);
                firstError = std::abs(NativeGroundMath::delta(world.nativeHullHeading(vehicle, world.tick), moved));
            }
        }
        const bool arrived = vehicle.state == UState::Idle && vehicle.x == goalX && vehicle.y == goalY;
        const int allowance = vehicle.nativeIdentity.definition->production.rotationRate * 128;
        std::printf("GROUND_TRAVEL_STOP_PIVOT case=%d started=%d stop=%d hull=%u turret=%u hull_drift=%d turret_drift=%d "
            "stationary=%d replay=%d restarted=%d first=%d error=%d arrived=%d\n", test, staged, stopped,
            (unsigned)hull, (unsigned)turret, hullDrift, turretDrift, stationary, replay, restarted, firstMotion, firstError, arrived);
        check(staged && stopped && replay && stationary && hullDrift == 0 && turretDrift == 0
            && restarted && firstMotion > 2 && firstError <= allowance && arrived,
            "Stop holds the current hull and turret angles during a timed pivot until the next move");
    }

    const Vec2i laneDirections[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    for (int road = 0; road < 4; ++road) {
        if (!fresh()) return 1;
        for (auto& cell : world.map.cells) cell.terrain = Terrain::Water;
        const auto direction = laneDirections[road];
        const Vec2i entry{24 - 8 * direction.x, 24 - 8 * direction.y};
        const Vec2i exit{24 + 16 * direction.x, 24 + 16 * direction.y};
        for (int tile = 0; tile <= 24; ++tile)
            world.map.at(entry.x + tile * direction.x, entry.y + tile * direction.y) = Cell{};
        const EID id = world.spawnUnit(0, UnitType::Grizzly, entry.x + .5f, entry.y + .5f, type("MTNK"));
        if (!world.valid(id)) return failures + 1;
        auto& vehicle = world.ents[id];
        vehicle.dir = vehicle.turretDir = NativeGroundMath::dir(NativeGroundMath::vector(direction.x, direction.y));
        command(World::Cmd::Move, id, 0, 0, exit.x + .5f, exit.y + .5f);
        const auto distanceFromEntry = [&] {
            return (vehicle.x - entry.x - .5f) * direction.x + (vehicle.y - entry.y - .5f) * direction.y;
        };
        const bool insideTargetCell = road % 2 != 0;
        const float trigger = insideTargetCell ? 5.75f : 5.2f;
        for (int elapsed = 0; elapsed < 400 && distanceFromEntry() < trigger; ++elapsed) world.update();
        const float turnAt = distanceFromEntry();
        const Vec2i tile{(int)vehicle.x, (int)vehicle.y};
        const auto oldPhase = world.trafficDebug();
        const bool staged = vehicle.state == UState::Moving && turnAt >= trigger && !vehicle.driveArc.active()
            && vehicle.pathIdx < (int)vehicle.path.size() && (vehicle.path[vehicle.pathIdx] == tile) == insideTargetCell
            && oldPhase.size() == 1 && oldPhase.front().occupants == 1 && !oldPhase.front().draining
            && oldPhase.front().exit == exit.y * world.map.w + exit.x;

        bool keptOccupiedPhase = true;
        if (road == 0) {
            World occupied = world;
            occupied.map.bldOccRef = &occupied.bldOcc;
            const EID other = occupied.spawnUnit(0, UnitType::Grizzly, tile.x + 3.5f, tile.y + .5f, type("MTNK"));
            issue(occupied, World::Cmd::Move, id, 0, 0, entry.x + .5f, entry.y + .5f);
            const auto phase = occupied.trafficDebug();
            keptOccupiedPhase = occupied.valid(other) && oldPhase.size() == 1 && phase.size() == 1
                && phase.front().occupants == 2 && phase.front().exit == oldPhase.front().exit
                && phase.front().remaining == oldPhase.front().remaining;
        }

        command(World::Cmd::Move, id, 0, 0, entry.x + .5f, entry.y + .5f);
        const bool admitted = vehicle.state == UState::Moving && !vehicle.path.empty()
            && vehicle.path.front() == Vec2i{tile.x - direction.x, tile.y - direction.y} && vehicle.path.back() == entry;
        const bool saved = roundTrip(world, "tank-reverse-straight-" + std::to_string(road), 650);
        const int allowance = vehicle.nativeIdentity.definition->production.rotationRate * 128;
        const int deadline = 2 * (32768 / (2 * allowance)) + 4;
        bool direct = true, admittedReverseExit = false;
        int firstMotion = 0, firstError = 0;
        float overshoot = 0.f;
        for (int elapsed = 1; elapsed <= 650 && vehicle.state != UState::Idle; ++elapsed) {
            const float oldX = vehicle.x, oldY = vehicle.y;
            world.update();
            const float dx = vehicle.x - oldX, dy = vehicle.y - oldY;
            overshoot = std::max(overshoot, distanceFromEntry() - turnAt);
            direct &= dx * direction.x + dy * direction.y <= 1e-5f
                && std::abs(dx * direction.y - dy * direction.x) <= 1e-5f;
            if (!firstMotion && std::hypot(dx, dy) > 1e-5f) {
                firstMotion = elapsed;
                firstError = std::abs(NativeGroundMath::delta(world.nativeHullHeading(vehicle, world.tick), NativeGroundMath::vector(dx, dy)));
            }
            for (const auto& phase : world.trafficDebug()) admittedReverseExit |= phase.exit == entry.y * world.map.w + entry.x;
        }
        const bool arrived = vehicle.state == UState::Idle && vehicle.x == entry.x + .5f && vehicle.y == entry.y + .5f;
        std::printf("GROUND_TRAVEL_STRAIGHT_REVERSE road=%d staged=%d admitted=%d new_exit=%d occupied_phase=%d "
            "first=%d deadline=%d error=%d overshoot=%.4f direct=%d arrived=%d replay=%d\n",
            road, staged, admitted, admittedReverseExit, keptOccupiedPhase, firstMotion, deadline, firstError, overshoot, direct, arrived, saved);
        check(staged && admitted && admittedReverseExit && keptOccupiedPhase && direct && arrived && saved
            && firstMotion > 2 && firstMotion <= deadline && firstError <= allowance,
            "a lone tank turns back on a narrow straight road and an occupied road retains its traffic phase");
    }

    if(!fresh())return 1;
    const EID refinery=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("GAREFN"));
    const EID miner=world.spawnUnit(0,UnitType::ChronoMiner,29.5f,21.5f,type("CMIN"));
    if(!world.valid(refinery) || !world.valid(miner))return 1;
    const Vec2i deposit{30,21};
    auto& ore=world.map.at(deposit.x,deposit.y);
    ore.terrain=Terrain::Ore;ore.ore=ore.oreMax=80;
    command(World::Cmd::Harvest,miner,deposit.x,deposit.y);
    const bool harvestAdmitted=world.ents[miner].state==UState::HarvestGo && world.ents[miner].oreCell==deposit;
    struct FirstPickup {
        int digAt=-1,pickedAt=-1;
        Vec2i digCell{-1,-1},pickupCell{-1,-1};
        Vec2f position{};
    };
    FirstPickup afterOrder,afterUnload;
    int fullAt=-1,warpAt=-1,alignAt=-1,unloadAt=-1,creditAt=-1,exitAt=-1;
    int arrivals=0,departures=0,otherEffects=0;
    unsigned checkpoints=0;
    bool replay=true,fullAtSource=false,landedWithCargo=false,noSettling=true,noEnter=true;
    const auto identity=world.entityRef(miner);
    std::ofstream economy(std::filesystem::path(outputDir)/"miner-cycle.csv");
    economy<<"tick,x,y,state,dock,warp,remaining,cargo,income\n";
    for(int t=0;t<2200;++t) {
        world.update();
        const auto& actor=world.ents[miner];
        economy<<world.tick<<','<<actor.x<<','<<actor.y<<','<<(int)actor.state<<','<<(int)actor.nativeDock.phase<<','
               <<(int)actor.chronoReturn.phase<<','<<(int)actor.chronoReturn.ticksLeft<<','<<actor.oreLoad<<','<<world.harvestedIncome[0]<<'\n';
        auto& first=world.harvestedIncome[0]>0?afterUnload:afterOrder;
        if(actor.state==UState::HarvestDig && actor.oreCell==deposit) {
            const Vec2i cell{(int)actor.x,(int)actor.y};
            if(first.digAt<0) {
                first.digAt=t;
                first.digCell=cell;
            }
            if(first.pickedAt<0 && actor.oreLoad==1) {
                first.pickedAt=t;
                first.pickupCell=cell;
                first.position={actor.x,actor.y};
            }
        }
        if(fullAt<0 && actor.oreLoad==20) {
            fullAt=t;
            fullAtSource=afterOrder.pickedAt>=0 && actor.x==afterOrder.position.x && actor.y==afterOrder.position.y
                && world.harvestedIncome[0]==0
                && actor.chronoReturn.phase==ChronoReturnPhase::Charging && actor.chronoReturn.directBerth;
        }
        int arrivalsThisTick=0,departuresThisTick=0;
        for(const auto& effect:world.effects)if(effect.kind==9 && effect.age==1) {
            if(effect.aux==(int)ChronoVisual::Arrival && effect.x==23.5f && effect.y==21.5f)++arrivalsThisTick;
            else if(effect.aux==(int)ChronoVisual::Departure && effect.x==afterOrder.position.x && effect.y==afterOrder.position.y)++departuresThisTick;
            else ++otherEffects;
        }
        arrivals+=arrivalsThisTick;
        departures+=departuresThisTick;
        if(warpAt<0 && arrivalsThisTick==1 && departuresThisTick==1) {
            warpAt=t;
            landedWithCargo=actor.x==23.5f && actor.y==21.5f && actor.oreLoad==20 && world.harvestedIncome[0]==0
                && actor.nativeDock.phase==NativeDockPhase::Align && actor.nativeDock.receiver==world.entityRef(refinery);
        }
        noSettling &= actor.chronoReturn.phase!=ChronoReturnPhase::Settling;
        noEnter &= actor.nativeDock.phase!=NativeDockPhase::Enter;
        if(alignAt<0 && actor.nativeDock.phase==NativeDockPhase::Align)alignAt=t;
        if(unloadAt<0 && actor.state==UState::HarvestUnload)unloadAt=t;
        if(creditAt<0 && world.harvestedIncome[0]>0)creditAt=t;
        if(exitAt<0 && actor.nativeDock.phase==NativeDockPhase::Exit)exitAt=t;
        int stage=-1;
        if(actor.chronoReturn.phase==ChronoReturnPhase::Charging && actor.chronoReturn.ticksLeft==ChronoReturnTiming::ready)stage=0;
        if(actor.nativeDock.phase>=NativeDockPhase::Align)stage=1+(int)actor.nativeDock.phase-(int)NativeDockPhase::Align;
        if(creditAt>=0 && actor.state==UState::HarvestGo)stage=5;
        if(stage>=0 && !(checkpoints&(1u<<stage))) {
            checkpoints|=1u<<stage;
            replay &= roundTrip(world,"miner-current-stage-"+std::to_string(stage),390);
        }
        if(afterUnload.pickedAt>=0)break;
    }
    const auto pickupOnDeposit=[&](const FirstPickup& first,const char* route) {
        std::printf("GROUND_TRAVEL_FIRST_PICKUP %s deposit=%d,%d digging=%d,%d dig_tick=%d cargo=%d,%d pickup_tick=%d\n",
            route,deposit.x,deposit.y,first.digCell.x,first.digCell.y,first.digAt+1,
            first.pickupCell.x,first.pickupCell.y,first.pickedAt+1);
        return first.digAt>=0 && first.pickedAt>first.digAt && first.digCell==deposit && first.pickupCell==deposit;
    };
    check(pickupOnDeposit(afterOrder,"harvest-order") && harvestAdmitted,
        "a normal Harvest order starts digging and collects its first ore on the target cell");
    check(pickupOnDeposit(afterUnload,"after-unload") && exitAt>=0 && afterUnload.digAt>exitAt,
        "after unloading and leaving the berth the miner starts digging and collects its next first ore on the target cell");
    std::printf("GROUND_TRAVEL_CYCLE full=%d warp=%d align=%d dock=%d credit=%d exit=%d mining=%d effects=%d/%d checkpoints=%u\n",
        fullAt,warpAt,alignAt,unloadAt,creditAt,exitAt,afterUnload.digAt,departures,arrivals,checkpoints);
    check(fullAt>=0 && warpAt-fullAt==ChronoReturnTiming::ready && alignAt==warpAt && unloadAt>alignAt && creditAt-unloadAt==32
        && exitAt-creditAt==30 && afterUnload.digAt>exitAt && world.harvestedIncome[0]==500 && world.valid(identity)
        && arrivals==1 && departures==1 && otherEffects==0 && fullAtSource && landedWithCargo && noSettling && noEnter
        && world.ents[miner].oreCell==deposit,
        "a naturally filled miner lands on the next update, docks, credits exactly once and walks back to its ore");
    constexpr unsigned directReturnCheckpoints=(1u<<0)|(1u<<1)|(1u<<3)|(1u<<4)|(1u<<5);
    check(checkpoints==directReturnCheckpoints && replay,
        "pending warp, berth alignment, unloading, exit and outbound travel each resume tick for tick");

    {
        if (!fresh()) return failures + 1;
        const Vec2i occupiedOre{30, 21}, spareOre{29, 22};
        for (const auto cell : {occupiedOre, spareOre}) {
            auto& tile = world.map.at(cell.x, cell.y);
            tile.terrain = Terrain::Ore;
            tile.ore = tile.oreMax = 80;
        }
        const EID working = world.spawnUnit(0, UnitType::WarMiner, 30.5f, 21.5f, type("HARV"));
        const EID arriving = world.spawnUnit(0, UnitType::WarMiner, 29.5f, 21.5f, type("HARV"));
        if (!world.valid(working) || !world.valid(arriving)) return failures + 1;
        command(World::Cmd::Harvest, working, occupiedOre.x, occupiedOre.y);
        world.update();
        command(World::Cmd::Harvest, arriving, occupiedOre.x, occupiedOre.y);
        const auto& first = world.ents[working];
        const auto& second = world.ents[arriving];
        const bool staged = first.state == UState::HarvestDig && first.oreCell == occupiedOre
            && second.state == UState::HarvestGo && second.oreCell == occupiedOre;
        int pickupAt = 0;
        for (int elapsed = 1; elapsed <= 240; ++elapsed) {
            world.update();
            if (second.oreLoad == 1) { pickupAt = elapsed; break; }
        }
        const Vec2i pickupCell{(int)second.x, (int)second.y};
        std::printf("GROUND_TRAVEL_ORE_CONTENTION staged=%d pickup=%d cell=%d,%d target=%d,%d first_cargo=%d\n",
            staged, pickupAt, pickupCell.x, pickupCell.y, second.oreCell.x, second.oreCell.y, first.oreLoad);
        check(staged && pickupAt > 0 && pickupCell == spareOre && second.oreCell == spareOre
            && first.state == UState::HarvestDig && first.oreCell == occupiedOre && first.oreLoad > 0,
            "a miner waiting for its occupied target harvests a reachable free ore cell while the first miner keeps digging");
    }

    for (bool atLanding : {false, true}) {
        if (!fresh()) return failures + 1;
        const EID receiver = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("GAREFN"));
        const float landingX = 23.5f, apronX = 24.5f, approachY = 21.5f;
        const float startX = atLanding ? landingX : 29.5f;
        const EID id = world.spawnUnit(0, UnitType::ChronoMiner, startX, approachY, type("CMIN"));
        if (!world.valid(receiver) || !world.valid(id)) return failures + 1;
        auto& actor = world.ents[id];
        actor.dir = actor.turretDir = 7;
        const auto departureHeading = world.nativeHullHeading(actor, world.tick);
        actor.oreLoad = 20;
        const auto identity = world.entityRef(id);
        const auto destination = world.entityRef(receiver);
        const auto startedMoney = world.players[0].money;
        const auto startedIncome = world.harvestedIncome[0];
        command(World::Cmd::ReturnHarvester, id, receiver);
        const bool admitted = actor.state == UState::HarvestReturn && actor.nativeDock.receiver == destination
            && actor.chronoReturn.phase == ChronoReturnPhase::Charging && actor.chronoReturn.directBerth;
        const bool commandOnly = actor.x == startX && actor.y == approachY && actor.oreLoad == 20
            && world.players[0].money == startedMoney && world.harvestedIncome[0] == startedIncome
            && std::none_of(world.effects.begin(), world.effects.end(), [](const auto& effect) { return effect.kind == 9; });
        int arrivals = 0, departures = 0, unexpected = 0, settling = 0, alignAt = 0, unloadAt = 0, creditAt = 0, exitAt = 0;
        int firstArrivals = 0, firstDepartures = 0, firstDock = 0, firstWarp = 0, deposits = 0;
        bool firstUpdate = false, paidInBay = false, entered = false;
        uint16_t firstHeading = 0;
        float firstX = 0;
        for (int elapsed = 1; elapsed <= 600; ++elapsed) {
            const auto priorIncome = world.harvestedIncome[0];
            world.update();
            for (const auto& effect : world.effects) if (effect.kind == 9 && effect.age == 1) {
                if (effect.aux == (int)ChronoVisual::Departure && effect.x == startX && effect.y == approachY) ++departures;
                else if (effect.aux == (int)ChronoVisual::Arrival && effect.x == landingX && effect.y == approachY) ++arrivals;
                else ++unexpected;
            }
            const auto dockPhase = actor.nativeDock.phase;
            entered |= dockPhase == NativeDockPhase::Enter;
            if (elapsed == 1) {
                firstX = actor.x;
                firstHeading = world.nativeHullHeading(actor, world.tick);
                firstDock = (int)dockPhase;
                firstWarp = (int)actor.chronoReturn.phase;
                firstArrivals = arrivals;
                firstDepartures = departures;
                firstUpdate = actor.x == landingX && actor.y == approachY && actor.navLayer == world.ents[receiver].navLayer
                    && firstHeading == departureHeading
                    && dockPhase == NativeDockPhase::Align && actor.chronoReturn.phase == ChronoReturnPhase::None
                    && actor.state == UState::HarvestReturn && actor.nativeDock.receiver == destination && actor.oreLoad == 20
                    && world.players[0].money == startedMoney && world.harvestedIncome[0] == startedIncome;
            }
            settling += actor.chronoReturn.phase == ChronoReturnPhase::Settling;
            if (!alignAt && dockPhase == NativeDockPhase::Align) alignAt = elapsed;
            if (!unloadAt && dockPhase == NativeDockPhase::Unload) unloadAt = elapsed;
            if (world.harvestedIncome[0] != priorIncome) {
                ++deposits;
                creditAt = elapsed;
                paidInBay = actor.x == 23.5f && actor.y == approachY && actor.oreLoad == 0 && dockPhase == NativeDockPhase::Unload;
            }
            if (!exitAt && dockPhase == NativeDockPhase::Exit) exitAt = elapsed;
            if (exitAt && actor.state == UState::Idle) break;
        }
        const int jumps = atLanding ? 0 : 1;
        const bool finished = world.valid(identity) && actor.state == UState::Idle && actor.nativeDock.phase == NativeDockPhase::None
            && actor.x == apronX && actor.y == approachY && actor.oreLoad == 0 && deposits == 1 && paidInBay && !entered
            && unloadAt > alignAt && creditAt - unloadAt == 32 && exitAt - creditAt == 30
            && world.players[0].money == startedMoney + 500 && world.harvestedIncome[0] == startedIncome + 500;
        std::printf("GROUND_TRAVEL_READY_CHRONO same_landing=%d admitted=%d command_only=%d first=%d x=%.3f dock=%d warp=%d heading=%u/%u "
            "first_effects=%d/%d effects=%d/%d unexpected=%d settling=%d align=%d unload=%d credit=%d exit=%d finished=%d\n",
            atLanding, admitted, commandOnly, firstUpdate, firstX, firstDock, firstWarp, (unsigned)departureHeading,
            (unsigned)firstHeading, firstDepartures, firstArrivals,
            departures, arrivals, unexpected, settling, alignAt, unloadAt, creditAt, exitAt, finished);
        check(admitted && commandOnly && finished, "a bound Chrono return only schedules motion and later completes one physical unload");
        check(firstUpdate && settling == 0 && firstArrivals == jumps && firstDepartures == jumps
            && arrivals == jumps && departures == jumps && unexpected == 0,
            "the first eligible Chrono update starts dock alignment and emits effects only for a distinct landing");
    }

    if(!fresh())return 1;
    const auto across=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("GAREFN"));
    for(int y=0;y<world.map.h;++y)world.map.at(27,y).terrain=Terrain::Water;
    const auto overseas=world.spawnUnit(0,UnitType::ChronoMiner,34.5f,21.5f,type("CMIN"));
    world.ents[overseas].oreLoad=20;world.ents[overseas].gemLoad=5;
    world.ents[overseas].oreCell={35,21};
    for(Vec2i deposit:{Vec2i{35,21},Vec2i{25,24}}) {
        auto& cell=world.map.at(deposit.x,deposit.y);cell.terrain=Terrain::Ore;cell.ore=cell.oreMax=80;
    }
    std::vector<Vec2i> impossible;
    const bool isolated=!world.map.findPath(34,21,24,21,impossible);
    const auto blocker=world.spawnUnit(0,UnitType::Grizzly,24.5f,21.5f,type("MTNK"));
    if(!world.valid(across) || !world.valid(overseas) || !world.valid(blocker))return failures+1;
    world.ents[blocker].dir=world.ents[blocker].turretDir=7;
    command(World::Cmd::Guard,blocker);
    command(World::Cmd::ReturnHarvester,overseas,across);
    const auto& returnActor=world.ents[overseas];
    const bool ready=returnActor.chronoReturn.phase==ChronoReturnPhase::Charging
        && returnActor.chronoReturn.ticksLeft==ChronoReturnTiming::ready && returnActor.chronoReturn.directBerth;
    const bool beforeReplay=roundTrip(world,"miner-apron-occupied-before-warp",500);
    bool landed=false,noEarlyCredit=true,oreFirst=false,blockedExit=false,exitReplay=false,blockerMoved=false,neverOverlapped=true;
    for(int t=0;t<900;++t) {
        world.update();
        const auto& actor=returnActor;
        const auto& guard=world.ents[blocker];
        const bool apronOccupied=world.unitAtCell(24,21,world.ents[across].navLayer)==blocker;
        if(t==0)landed=apronOccupied && actor.nativeDock.phase==NativeDockPhase::Align && actor.x==23.5f && actor.y==21.5f
            && world.unitAtCell(23,21,actor.navLayer)==overseas && actor.oreLoad==20 && world.harvestedIncome[0]==0;
        if(actor.state!=UState::HarvestUnload && actor.nativeDock.phase<NativeDockPhase::Exit && actor.oreLoad==20)
            noEarlyCredit &= world.harvestedIncome[0]==0;
        oreFirst |= actor.oreLoad==5 && actor.gemLoad==5 && world.harvestedIncome[0]==375;
        neverOverlapped &= (int)actor.x!=(int)guard.x || (int)actor.y!=(int)guard.y || actor.navLayer!=guard.navLayer;
        if(!blockedExit && apronOccupied && actor.nativeDock.phase==NativeDockPhase::Exit && actor.blockTick>=3) {
            blockedExit=actor.x<24.f && actor.y==21.5f && actor.oreLoad==0 && world.harvestedIncome[0]==625;
            if(blockedExit) {
                exitReplay=roundTrip(world,"miner-apron-occupied-at-exit",180);
                command(World::Cmd::Move,blocker,0,0,24.5f,18.5f);
                blockerMoved=guard.state==UState::Moving && !guard.path.empty() && guard.path.back()==Vec2i{24,18};
            }
        }
        if(actor.state==UState::HarvestDig && actor.x<27)break;
    }
    std::printf("GROUND_TRAVEL_CHRONO_APRON isolated=%d ready=%d landed=%d exit_wait=%d move=%d exclusive=%d replay=%d/%d credit=%lld\n",
        isolated,ready,landed,blockedExit,blockerMoved,neverOverlapped,beforeReplay,exitReplay,(long long)world.harvestedIncome[0]);
    check(isolated && ready && landed && noEarlyCredit && oreFirst && world.harvestedIncome[0]==625
        && world.ents[overseas].oreCell==Vec2i{25,24} && world.ents[overseas].state==UState::HarvestDig
        && blockedExit && blockerMoved && neverOverlapped && beforeReplay && exitReplay
        && world.ents[blocker].x==24.5f && world.ents[blocker].y==18.5f,
        "a Chrono Miner lands inside the berth across disconnected land, unloads with the apron occupied and exits after it clears");

    {
        if (!fresh()) return failures + 1;
        constexpr float berthX = 23.5f, apronX = 24.5f, dockY = 21.5f;
        constexpr int leadCargo = 20, queuedCargo = 12;
        const EID refinery = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("GAREFN"));
        const EID leadId = world.spawnUnit(0, UnitType::ChronoMiner, 34.5f, dockY, type("CMIN"));
        const EID queuedId = world.spawnUnit(0, UnitType::ChronoMiner, 26.5f, dockY, type("CMIN"));
        if (!world.valid(refinery) || !world.valid(leadId) || !world.valid(queuedId)) return failures + 1;
        auto& lead = world.ents[leadId];
        auto& queued = world.ents[queuedId];
        const auto target = world.entityRef(refinery);
        const int layer = world.ents[refinery].navLayer;
        const auto occupant = [&](int x) { return world.unitAtCell(x, 21, layer); };
        const auto atBerth = [&](const auto& miner) { return miner.x == berthX && miner.y == dockY && miner.navLayer == layer; };
        const auto onApron = [&] { return occupant(24) == queuedId; };
        queued.dir = queued.turretDir = 5;
        command(World::Cmd::Move, queuedId, 0, 0, apronX, dockY);
        const bool moveAdmitted = queued.state == UState::Moving && !queued.path.empty() && queued.path.back() == Vec2i{24, 21};
        for (int step = 0; step < 360 && queued.state != UState::Idle; ++step) world.update();
        const bool parked = moveAdmitted && queued.state == UState::Idle && queued.x == apronX && queued.y == dockY && onApron();

        lead.oreLoad = leadCargo;
        queued.oreLoad = queuedCargo;
        const auto startingIncome = world.harvestedIncome[0];
        const auto startingCash = world.players[0].money;
        command(World::Cmd::ReturnHarvester, leadId, refinery);
        const bool leadAdmitted = lead.x == 34.5f && lead.nativeDock.receiver == target && lead.claimsNativeDock()
            && lead.chronoReturn.phase == ChronoReturnPhase::Charging && lead.chronoReturn.directBerth;
        world.update();
        const bool leadLanded = atBerth(lead) && lead.nativeDock.phase == NativeDockPhase::Align
            && lead.nativeDock.receiver == target && occupant(23) == leadId && onApron();
        command(World::Cmd::ReturnHarvester, queuedId, refinery);
        const bool waiting = queued.state == UState::HarvestReturn && queued.nativeDock.receiver == target
            && queued.chronoReturn.phase == ChronoReturnPhase::Waiting && queued.chronoReturn.directBerth
            && queued.chronoReturn.preferReceiver && !queued.claimsNativeDock() && onApron();

        const auto activeYield = [](const World::Ent& miner) {
            return miner.state == UState::HarvestReturn && miner.chronoReturn.phase == ChronoReturnPhase::Waiting
                && miner.pathIdx < (int)miner.path.size();
        };
        const auto exclusiveLane = [layer](const World::Ent& first, const World::Ent& second) {
            return first.navLayer == layer && second.navLayer == layer
                && ((int)first.x != (int)second.x || (int)first.y != (int)second.y)
                && !(first.claimsNativeDock() && second.claimsNativeDock());
        };
        const auto verifyYieldApplication = [&] {
            const auto app = (output / "chrono-queued-exit-yield-app11.sav").string();
            if (!saveGameFile(app.c_str())) return false;
            Game resumed;
            const auto before = world.checksum();
            if (!resumed.loadGameFile(app.c_str()) || resumed.world.checksum() != before) return false;
            std::ifstream in(app, std::ios::binary);
            std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
            const auto nested = embeddedWorldSaveOffset(bytes);
            if (!nested || std::memcmp(bytes.data(), "RA2GAM11", 8)
                || !worldSaveHeaderMatches(std::span(bytes).subspan(*nested),"RA2WLD70")) return false;
            std::memcpy(bytes.data() + *nested, "RA2WLD69", 8);
            const auto downgraded = (output / "chrono-queued-exit-yield-world69-app11.sav").string();
            std::ofstream out(downgraded, std::ios::binary);
            out.write((const char*)bytes.data(), bytes.size());
            out.close();
            return out && !resumed.loadGameFile(downgraded.c_str()) && resumed.world.checksum() == before;
        };
        int yieldAt = 0, pausedFrames = 0;
        bool retryPaused = true, yieldReplay = false, appReplay = false, repeated = false, stopped = false, destroyed = false;
        int unloadedAt = 0, blockedAt = 0, clearAt = 0, leftAt = 0, landedAt = 0, deposits = 0, retries = 0;
        bool pendingReplay = false, keptOrder = true, traveledOnGround = true, noOverlap = true, correctlyPaid = true;
        for (int elapsed = 1; elapsed <= 900; ++elapsed) {
            const float previousX = queued.x, previousY = queued.y;
            const bool alreadyYielding = activeYield(queued);
            const auto beforeWait = queued.chronoReturn;
            const auto priorIncome = world.harvestedIncome[0];
            world.update();
            if (alreadyYielding && activeYield(queued)) {
                ++pausedFrames;
                retryPaused &= queued.chronoReturn == beforeWait && queued.nativeDock.receiver == target;
            }
            noOverlap &= exclusiveLane(lead, queued);
            if (!unloadedAt && lead.nativeDock.phase == NativeDockPhase::Unload && atBerth(lead) && occupant(23) == leadId)
                unloadedAt = elapsed;
            if (!blockedAt && lead.nativeDock.phase == NativeDockPhase::Exit && lead.nativeDock.receiver == target
                && lead.claimsNativeDock() && lead.blockTick > 0 && lead.x < 24.f && lead.y == dockY && onApron()
                && queued.chronoReturn.phase == ChronoReturnPhase::Waiting && queued.path.empty()) {
                blockedAt = elapsed;
                pendingReplay = roundTrip(world, "chrono-queued-exit-before-yield", 300);
            }
            if (!yieldAt && activeYield(queued) && (queued.x != previousX || queued.y != previousY)) {
                yieldAt = elapsed;
                const auto checkpoint = roundTrip(world, "chrono-queued-exit-yield", 300);
                std::ifstream raw(output / "chrono-queued-exit-yield.sav", std::ios::binary);
                uint8_t header[13]{};
                yieldReplay = checkpoint && raw.read(reinterpret_cast<char*>(header), sizeof header) && worldSaveHeaderMatches(header,"RA2WLD70");
                appReplay = verifyYieldApplication();
                {
                    World copy = world;
                    copy.map.bldOccRef = &copy.bldOcc;
                    issue(copy, World::Cmd::ReturnHarvester, queuedId, refinery, 0, 0, 0);
                    const auto& reissued = copy.ents[queuedId];
                    repeated = activeYield(reissued) && reissued.nativeDock.receiver == target && reissued.chronoReturn == queued.chronoReturn
                        && reissued.path == queued.path && reissued.pathIdx == queued.pathIdx && reissued.nativeMovement == queued.nativeMovement
                        && reissued.x == queued.x && reissued.y == queued.y && reissued.oreLoad == queuedCargo;
                }
                {
                    World copy = world;
                    copy.map.bldOccRef = &copy.bldOcc;
                    issue(copy, World::Cmd::Stop, queuedId, 0, 0, 0, 0);
                    const auto& canceled = copy.ents[queuedId];
                    stopped = canceled.state == UState::Idle && !canceled.autoHarvest && canceled.path.empty()
                        && canceled.chronoReturn == ChronoReturnState{} && canceled.nativeDock.phase == NativeDockPhase::None
                        && canceled.x == queued.x && canceled.y == queued.y && canceled.oreLoad == queuedCargo;
                }
                {
                    World copy = world;
                    copy.map.bldOccRef = &copy.bldOcc;
                    copy.kill(refinery, false);
                    copy.update();
                    const auto& canceled = copy.ents[queuedId];
                    destroyed = !copy.valid(target) && canceled.path.empty() && canceled.nativeDock.receiver != target
                        && canceled.pendingDockRefinery != target && canceled.x == queued.x && canceled.y == queued.y
                        && canceled.oreLoad == queuedCargo && copy.harvestedIncome[0] == world.harvestedIncome[0];
                }
            }
            if (!leftAt && !lead.claimsNativeDock() && lead.nativeDock.phase == NativeDockPhase::None
                && lead.x == apronX && lead.y == dockY && occupant(24) == leadId) leftAt = elapsed;
            if (!landedAt) {
                keptOrder &= queued.oreLoad == queuedCargo && queued.gemLoad == 0
                    && (queued.nativeDock.receiver == target || queued.pendingDockRefinery == target);
                if (atBerth(queued) && queued.nativeDock.phase == NativeDockPhase::Align && occupant(23) == queuedId) {
                    landedAt = elapsed;
                } else {
                    if (queued.chronoReturn.phase != ChronoReturnPhase::None)
                        keptOrder &= queued.chronoReturn.preferReceiver && queued.chronoReturn.directBerth;
                    if (queued.x != previousX || queued.y != previousY) {
                        traveledOnGround &= queued.px == previousX && queued.py == previousY
                            && distf(queued.x, queued.y, previousX, previousY) <= .5f
                            && world.map.passable((int)queued.x, (int)queued.y);
                    }
                    if (!clearAt && !onApron()) clearAt = elapsed;
                    retries += beforeWait.phase == ChronoReturnPhase::Waiting && beforeWait.ticksLeft == 1
                        && queued.chronoReturn.phase == ChronoReturnPhase::Waiting
                        && queued.chronoReturn.ticksLeft == ChronoReturnTiming::retry;
                }
            }
            if (world.harvestedIncome[0] != priorIncome) {
                ++deposits;
                const auto& paid = deposits == 1 ? lead : queued;
                const int quantity = deposits == 1 ? leadCargo : queuedCargo;
                correctlyPaid &= deposits <= 2 && atBerth(paid) && paid.oreLoad == 0
                    && paid.nativeDock.phase == NativeDockPhase::Unload && paid.nativeDock.receiver == target
                    && world.harvestedIncome[0] - priorIncome == 25 * quantity;
            }
            if (landedAt && lead.state == UState::Idle && queued.state == UState::Idle) break;
        }
        const auto income = world.harvestedIncome[0] - startingIncome;
        const bool finished = lead.state == UState::Idle && queued.state == UState::Idle
            && lead.oreLoad == 0 && queued.oreLoad == 0 && !lead.claimsNativeDock() && !queued.claimsNativeDock();
        std::printf("GROUND_TRAVEL_CHRONO_QUEUE_EXIT parked=%d admitted=%d first_landed=%d waiting=%d unload=%d blocked=%d "
            "clear=%d left=%d second_landed=%d retries=%d same_order=%d ground=%d no_overlap=%d paid=%d deposits=%d income=%lld "
            "finished=%d replay=%d lead=%.4f,%.4f:%d queued=%.4f,%.4f:%d timer=%d\n",
            parked, leadAdmitted, leadLanded, waiting, unloadedAt, blockedAt, clearAt, leftAt, landedAt, retries,
            keptOrder, traveledOnGround, noOverlap, correctlyPaid, deposits, (long long)income, finished, pendingReplay,
            lead.x, lead.y, (int)lead.nativeDock.phase, queued.x, queued.y, (int)queued.chronoReturn.phase, (int)queued.chronoReturn.ticksLeft);
        check(parked && leadAdmitted && leadLanded && waiting && unloadedAt > 0 && blockedAt > unloadedAt && pendingReplay,
            "two bound Chrono returns reach an occupied apron after the first miner unloads in its reserved berth");
        check(keptOrder && traveledOnGround && noOverlap && clearAt > blockedAt && leftAt > clearAt && landedAt >= leftAt
            && correctlyPaid && deposits == 2 && income == 25 * (leadCargo + queuedCargo) && finished,
            "a queued Chrono Miner drives clear, lets the lead exit and returns its unchanged cargo to the requested refinery");
        std::printf("GROUND_TRAVEL_CHRONO_QUEUE_PATH yield=%d paused=%d frames=%d replay=%d app=%d repeat=%d stop=%d destroyed=%d\n",
            yieldAt, retryPaused, pausedFrames, yieldReplay, appReplay, repeated, stopped, destroyed);
        check(yieldAt > blockedAt && pausedFrames > 0 && retryPaused && repeated && stopped && destroyed,
            "an active Chrono pullout pauses its retry, keeps its route on a repeat order, and clears it on Stop or refinery destruction");
        check(yieldReplay && appReplay,
            "an active Chrono pullout replays from World70 and rejects the same App11 with a World69 header atomically");
        if (const char* legacyInput = std::getenv("OPENRA2_CHRONO_QUEUE_WORLD69_INPUT")) {
            const auto loadArchive = [legacyInput](World& saved) {
                FILE* stream = std::fopen(legacyInput, "rb");
                if (!stream) return false;
                char header[8]{};
                const bool world69 = std::fread(header, 1, sizeof header, stream) == sizeof header
                    && std::memcmp(header, "RA2WLD69", sizeof header) == 0;
                std::rewind(stream);
                const bool loaded = world69 && saved.loadGame(stream);
                std::fclose(stream);
                return loaded;
            };
            World archived, resumed;
            const bool archiveLoaded = loadArchive(archived);
            bool archiveState = false, archiveInitial = false, archiveExact = false, archiveFinished = false;
            bool archiveNoOverlap = true, archivePaid = false;
            int archiveFrames = 0, archiveYieldAt = 0, archiveClearAt = 0, archiveLeftAt = 0, archiveLandedAt = 0, archiveDeposits = 0;
            if (archiveLoaded && archived.valid(target) && archived.valid(world.entityRef(leadId))
                && archived.valid(world.entityRef(queuedId))) {
                const auto& oldLead = archived.ents[leadId];
                const auto& oldQueued = archived.ents[queuedId];
                archiveState = archived.tick == 336 && oldLead.state == UState::HarvestUnload && oldLead.blockTick > 0
                    && oldLead.nativeDock.phase == NativeDockPhase::Exit && oldLead.claimsNativeDock()
                    && oldLead.x >= berthX && oldLead.x < 24.f && oldLead.y == dockY
                    && oldLead.oreLoad == 0 && archived.players[0].money == startingCash + 25 * leadCargo
                    && archived.harvestedIncome[0] == 0 && oldLead.nativeDock.receiver == target && oldQueued.nativeDock.receiver == target
                    && oldQueued.state == UState::HarvestReturn && oldQueued.chronoReturn.phase == ChronoReturnPhase::Waiting
                    && oldQueued.chronoReturn.directBerth && oldQueued.chronoReturn.preferReceiver
                    && oldQueued.path.empty() && oldQueued.pathIdx == 0 && oldQueued.x == apronX && oldQueued.y == dockY
                    && oldQueued.oreLoad == queuedCargo && exclusiveLane(oldLead, oldQueued);
                archiveInitial = archiveState && loadArchive(resumed) && archived.checksum() == resumed.checksum();
                archiveExact = archiveInitial;
                for (int elapsed = 1; elapsed <= 900 && archiveExact; ++elapsed) {
                    const float previousX = oldQueued.x, previousY = oldQueued.y;
                    const auto priorIncome = archived.harvestedIncome[0];
                    const auto priorCash = archived.players[0].money;
                    archived.update();
                    resumed.update();
                    ++archiveFrames;
                    archiveExact = archived.checksum() == resumed.checksum();
                    archiveNoOverlap &= exclusiveLane(oldLead, oldQueued);
                    if (!archiveYieldAt && activeYield(oldQueued) && (oldQueued.x != previousX || oldQueued.y != previousY)
                        && oldQueued.px == previousX && oldQueued.py == previousY) archiveYieldAt = elapsed;
                    if (!archiveClearAt && archived.unitAtCell(24, 21, layer) != queuedId) archiveClearAt = elapsed;
                    if (!archiveLeftAt && !oldLead.claimsNativeDock() && oldLead.nativeDock.phase == NativeDockPhase::None
                        && oldLead.x == apronX && oldLead.y == dockY) archiveLeftAt = elapsed;
                    if (!archiveLandedAt && atBerth(oldQueued) && oldQueued.nativeDock.phase == NativeDockPhase::Align)
                        archiveLandedAt = elapsed;
                    if (archived.harvestedIncome[0] != priorIncome) {
                        ++archiveDeposits;
                        archivePaid = archiveDeposits == 1 && atBerth(oldQueued) && oldQueued.oreLoad == 0
                            && oldQueued.nativeDock.phase == NativeDockPhase::Unload && oldQueued.nativeDock.receiver == target
                            && archived.harvestedIncome[0] - priorIncome == 25 * queuedCargo
                            && archived.players[0].money - priorCash == 25 * queuedCargo;
                    }
                    if (archiveLandedAt && oldLead.state == UState::Idle && oldQueued.state == UState::Idle) {
                        archiveFinished = oldLead.oreLoad == 0 && oldQueued.oreLoad == 0
                            && !oldLead.claimsNativeDock() && !oldQueued.claimsNativeDock()
                            && archived.players[0].money == startingCash + 25 * (leadCargo + queuedCargo)
                            && archived.harvestedIncome[0] == 25 * queuedCargo;
                        break;
                    }
                }
            }
            std::printf("GROUND_TRAVEL_CHRONO_QUEUE_WORLD69 loaded=%d state=%d initial=%d exact=%d frames=%d yield=%d "
                "clear=%d left=%d landed=%d no_overlap=%d paid=%d finished=%d\n",
                archiveLoaded, archiveState, archiveInitial, archiveExact, archiveFrames, archiveYieldAt,
                archiveClearAt, archiveLeftAt, archiveLandedAt, archiveNoOverlap, archivePaid, archiveFinished);
            check(archiveLoaded && archiveState && archiveInitial && archiveExact && archiveNoOverlap && archivePaid && archiveFinished
                && archiveYieldAt > 0 && archiveClearAt >= archiveYieldAt && archiveLeftAt > archiveClearAt
                && archiveLandedAt >= archiveLeftAt,
                "independent loads of the archived World69 apron wait yield and resume to the second deposit every tick");
        }
    }

    for(bool capture:{false,true}) {
        if(!fresh())return 1;
        const auto lost=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("GAREFN"));
        const auto recovering=world.spawnUnit(0,UnitType::ChronoMiner,34.5f,21.5f,type("CMIN"));
        world.ents[recovering].oreLoad=20;
        command(World::Cmd::ReturnHarvester,recovering,lost);
        const auto reference=world.entityRef(lost);
        if(capture)world.update();
        const auto& recoveringActor=world.ents[recovering];
        const bool interrupted=recoveringActor.nativeDock.receiver==reference && recoveringActor.oreLoad==20 && world.harvestedIncome[0]==0
            && (capture ? recoveringActor.nativeDock.phase==NativeDockPhase::Align && recoveringActor.chronoReturn.phase==ChronoReturnPhase::None
                    && recoveringActor.x==23.5f && recoveringActor.y==21.5f
                : recoveringActor.chronoReturn.phase==ChronoReturnPhase::Charging && recoveringActor.chronoReturn.ticksLeft==ChronoReturnTiming::ready
                    && recoveringActor.chronoReturn.directBerth && recoveringActor.x==34.5f && recoveringActor.y==21.5f);
        if(capture){world.changeActorOwner(lost,1);world.synchronizeBuildingOwners();}
        else world.kill(lost,false);
        const auto backup=world.spawnBuilding(0,BldType::OreRefinery,29,25,true,type("GAREFN"));
        bool released=false,cargoSafe=true;
        for(int t=0;t<700 && world.harvestedIncome[0]==0;++t) {
            world.update();
            const auto& actor=world.ents[recovering];
            if(!released) {
                cargoSafe &= actor.oreLoad==20 && world.harvestedIncome[0]==0;
                released=actor.nativeDock.receiver!=reference;
            }
        }
        check(interrupted && released && cargoSafe
            && world.harvestedIncome[0]==500 && world.harvestedIncome[1]==0
            && world.ents[recovering].nativeDock.receiver==world.entityRef(backup) && world.entityRef(backup)!=reference,
            "destroyed or captured receivers release their reservation and the same load reaches another owned refinery once");
    }

    if(!fresh())return 1;
    const auto reserved=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("GAREFN"));
    const EID first=world.spawnUnit(0,UnitType::ChronoMiner,29.5f,22.5f,type("CMIN"));
    const EID second=world.spawnUnit(0,UnitType::ChronoMiner,29.5f,24.5f,type("CMIN"));
    for(EID id:{first,second}){world.ents[id].oreLoad=20;command(World::Cmd::ReturnHarvester,id,reserved);}
    bool exclusive=world.ents[first].chronoReturn.phase==ChronoReturnPhase::Charging
        && world.ents[first].chronoReturn.ticksLeft==ChronoReturnTiming::ready
        && world.ents[first].chronoReturn.directBerth
        && world.ents[second].chronoReturn.phase==ChronoReturnPhase::Waiting && world.ents[second].chronoReturn.directBerth;
    check(checkApplication("miner-pending-warp",5,uint8_t{0},1,"RA2WLD69"),
        "the application resumes direct berth reservations and rejects mismatched version or timer without replacing the world");
    command(World::Cmd::Stop,first);
    exclusive &= !world.ents[first].claimsNativeDock() && world.ents[first].oreLoad==20;
    for(int t=0;t<650;++t) {
        world.update();exclusive &= !(world.ents[first].claimsNativeDock() && world.ents[second].claimsNativeDock());
    }
    check(exclusive && world.ents[first].x==29.5f && world.ents[first].oreLoad==20 && world.harvestedIncome[0]==500,
        "only one miner claims a berth, and Stop before the first update releases it without teleporting or spending the first load");

    if (!fresh()) return 1;
    const EID queuedRefinery = world.spawnBuilding(0, BldType::OreRefinery, 20, 20, true, type("GAREFN"));
    const EID incumbent = world.spawnUnit(0, UnitType::ChronoMiner, 29.5f, 22.5f, type("CMIN"));
    const EID waiter = world.spawnUnit(0, UnitType::ChronoMiner, 29.5f, 24.5f, type("CMIN"));
    if (!world.valid(queuedRefinery) || !world.valid(incumbent) || !world.valid(waiter)) return 1;
    world.ents[incumbent].oreLoad = 20;
    command(World::Cmd::ReturnHarvester, incumbent, queuedRefinery);
    const auto receiver = world.entityRef(queuedRefinery);
    const auto incumbentRef = world.entityRef(incumbent);
    for (int t = 0; t < 180 && !world.harvesterUnloading(world.ents[incumbent]); ++t) world.update();
    const bool docked = world.harvesterUnloading(world.ents[incumbent]) && world.ents[incumbent].nativeDock.receiver == receiver
        && world.ents[incumbent].oreLoad == 20 && world.harvestedIncome[0] == 0;
    world.ents[waiter].oreLoad = 20;

    World automatic = world;
    automatic.map.bldOccRef = &automatic.bldOcc;
    issue(automatic, World::Cmd::ReturnHarvester, waiter, INVALID_EID, 0, 0, 0);
    for (int tick = 0; tick < 4; ++tick) automatic.update();
    const auto autoBefore = automatic.ents[waiter].chronoReturn;
    const bool autoWaiting = autoBefore.phase == ChronoReturnPhase::Waiting && autoBefore.directBerth && !autoBefore.preferReceiver
        && autoBefore.ticksLeft == ChronoReturnTiming::retry - 4 && automatic.ents[waiter].nativeDock.receiver == receiver
        && automatic.ents[incumbent].claimsNativeDock();
    issue(automatic, World::Cmd::ReturnHarvester, waiter, queuedRefinery, 0, 0, 0);
    const auto autoAfter = automatic.ents[waiter].chronoReturn;
    const bool confirmed = autoAfter.phase == ChronoReturnPhase::Waiting && autoAfter.directBerth && autoAfter.preferReceiver
        && autoAfter.ticksLeft == autoBefore.ticksLeft && automatic.ents[waiter].nativeDock.receiver == receiver;
    const EID alternate = automatic.spawnBuilding(0, BldType::OreRefinery, 33, 25, true, type("GAREFN"));
    if (!automatic.valid(alternate)) return 1;
    issue(automatic, World::Cmd::ReturnHarvester, waiter, alternate, 0, 0, 0);
    const bool switched = automatic.ents[waiter].nativeDock.receiver == automatic.entityRef(alternate)
        && automatic.ents[waiter].chronoReturn.phase == ChronoReturnPhase::Charging
        && automatic.ents[waiter].chronoReturn.ticksLeft == ChronoReturnTiming::ready && automatic.ents[waiter].chronoReturn.directBerth;
    automatic.update();
    const auto& redirected = automatic.ents[waiter];
    const bool alternateArrival = redirected.chronoReturn.phase == ChronoReturnPhase::None
        && redirected.nativeDock.phase == NativeDockPhase::Align && redirected.nativeDock.receiver == automatic.entityRef(alternate)
        && redirected.x == 36.5f && redirected.y == 26.5f && redirected.oreLoad == 20 && automatic.harvestedIncome[0] == 0;
    std::printf("GROUND_TRAVEL_WAIT_CONFIRM auto_waiting=%d before=%d after=%d pinned=%d confirmed=%d switched=%d alternate_arrival=%d\n",
        autoWaiting, autoBefore.ticksLeft, autoAfter.ticksLeft, autoAfter.preferReceiver, confirmed, switched, alternateArrival);
    check(docked && autoWaiting && switched && alternateArrival, "a waiting Chrono Miner lands on the first update at a different ordered refinery");
    check(autoWaiting && confirmed, "confirming an automatically chosen busy refinery pins it without resetting the waiting clock");

    command(World::Cmd::ReturnHarvester, waiter, queuedRefinery);
    const bool stagedWaiting = docked && world.ents[incumbent].claimsNativeDock()
        && world.ents[waiter].chronoReturn.phase == ChronoReturnPhase::Waiting && world.ents[waiter].chronoReturn.directBerth
        && world.ents[waiter].chronoReturn.preferReceiver && world.ents[waiter].nativeDock.receiver == receiver;
    World repeated = world;
    repeated.map.bldOccRef = &repeated.bldOcc;
    struct WaitSchedule {
        int ready = 0, jump = 0, income = 0, arrivals = 0, departures = 0;
        bool heldCargo = true, correctLanding = true;
    } once, many;
    const auto observe = [&](const World& sample, WaitSchedule& schedule, int tick) {
        const auto& actor = sample.ents[waiter];
        if (!schedule.ready && actor.chronoReturn.phase == ChronoReturnPhase::Charging
            && actor.chronoReturn.ticksLeft == ChronoReturnTiming::ready && actor.chronoReturn.directBerth) schedule.ready = tick;
        if (actor.chronoReturn.phase == ChronoReturnPhase::Waiting || actor.chronoReturn.phase == ChronoReturnPhase::Charging)
            schedule.heldCargo &= actor.x == 29.5f && actor.y == 24.5f && actor.oreLoad == 20 && sample.harvestedIncome[0] == 0;
        if (!schedule.jump && actor.nativeDock.phase == NativeDockPhase::Align) {
            schedule.jump = tick;
            schedule.correctLanding = actor.chronoReturn.phase == ChronoReturnPhase::None && actor.nativeDock.receiver == receiver
                && actor.x == 23.5f && actor.y == 21.5f && actor.oreLoad == 20 && sample.harvestedIncome[0] == 0;
        }
        if (!schedule.income && sample.harvestedIncome[0] != 0) schedule.income = tick;
        for (const auto& effect : sample.effects) if (effect.kind == 9 && effect.age == 1) {
            schedule.departures += effect.aux == (int)ChronoVisual::Departure && effect.x == 29.5f && effect.y == 24.5f;
            schedule.arrivals += effect.aux == (int)ChronoVisual::Arrival && effect.x == 23.5f && effect.y == 21.5f;
        }
    };
    const auto finished = [&](const World& sample) {
        const auto& actor = sample.ents[waiter];
        return actor.state == UState::Idle && actor.oreLoad == 0 && actor.x == 24.5f && actor.y == 21.5f
            && sample.harvestedIncome[0] == 500 && !sample.valid(incumbentRef);
    };
    int blockedClicks = 0, clearClicks = 0, changedClocks = 0, firstBefore = 0, firstAfter = 0;
    bool released = false, waitingReplays = false, equalState = true, exclusiveClaims = true;
    for (int tick = 1; tick <= 650; ++tick) {
        if (tick == 8) {
            const bool inBay = world.harvesterUnloading(world.ents[incumbent]) && repeated.harvesterUnloading(repeated.ents[incumbent])
                && world.ents[incumbent].oreLoad == 20 && repeated.ents[incumbent].oreLoad == 20;
            world.kill(incumbent, false);
            repeated.kill(incumbent, false);
            released = inBay && !world.valid(incumbentRef) && !repeated.valid(incumbentRef)
                && world.harvestedIncome[0] == 0 && repeated.harvestedIncome[0] == 0;
            waitingReplays = world.ents[waiter].chronoReturn.phase == ChronoReturnPhase::Waiting
                && repeated.ents[waiter].chronoReturn.phase == ChronoReturnPhase::Waiting
                && roundTrip(world, "miner-current-wait-confirm-once", 480)
                && roundTrip(repeated, "miner-current-wait-confirm-repeated", 480);
        }
        auto& actor = repeated.ents[waiter];
        if (tick >= 3 && tick % 2 == 1 && actor.chronoReturn.phase == ChronoReturnPhase::Waiting) {
            const auto before = actor.chronoReturn;
            if (released) ++clearClicks;
            else ++blockedClicks;
            issue(repeated, World::Cmd::ReturnHarvester, waiter, queuedRefinery, 0, 0, 0);
            changedClocks += actor.chronoReturn != before;
            if (tick == 3) {
                firstBefore = before.ticksLeft;
                firstAfter = actor.chronoReturn.ticksLeft;
            }
        }
        world.update();
        repeated.update();
        equalState &= world.checksum() == repeated.checksum();
        exclusiveClaims &= !(world.valid(incumbentRef) && world.ents[incumbent].claimsNativeDock() && world.ents[waiter].claimsNativeDock())
            && !(repeated.valid(incumbentRef) && repeated.ents[incumbent].claimsNativeDock() && repeated.ents[waiter].claimsNativeDock());
        observe(world, once, tick);
        observe(repeated, many, tick);
        if (finished(world) && finished(repeated)) break;
    }
    const bool bothFinished = finished(world) && finished(repeated);
    const auto validSchedule = [](const WaitSchedule& schedule) {
        return schedule.ready > 0 && schedule.jump > schedule.ready && schedule.income > schedule.jump
            && schedule.heldCargo && schedule.correctLanding && schedule.arrivals == 1 && schedule.departures == 1;
    };
    std::printf("GROUND_TRAVEL_WAIT_REPEAT staged=%d released=%d blocked_clicks=%d open_clicks=%d first_before=%d first_after=%d "
        "clock_changes=%d ready=%d/%d jump=%d/%d credit=%d/%d arrivals=%d/%d departures=%d/%d replay=%d exact=%d finished=%d\n",
        stagedWaiting, released, blockedClicks, clearClicks, firstBefore, firstAfter, changedClocks,
        once.ready, many.ready, once.jump, many.jump, once.income, many.income,
        once.arrivals, many.arrivals, once.departures, many.departures, waitingReplays, equalState, bothFinished);
    check(stagedWaiting && released && exclusiveClaims && waitingReplays && bothFinished
        && validSchedule(once) && validSchedule(many) && blockedClicks >= 2 && clearClicks > 0
        && firstBefore == ChronoReturnTiming::retry - 2,
        "single and repeated return orders both wait at a real reservation, resume and complete one jump and unload");
    check(changedClocks == 0 && firstBefore == firstAfter && equalState
        && once.ready == ChronoReturnTiming::retry && once.jump == once.ready + ChronoReturnTiming::ready
        && once.ready == many.ready && once.jump == many.jump && once.income == many.income,
        "repeating a waiting return to the same refinery preserves the retry, jump and credit schedule after release");

    if (const char* directory = std::getenv("OPENRA2_LEGACY_CHRONO_INPUT_DIR")) {
        struct SavedReturn {
            const char* file;
            ChronoReturnPhase phase;
            int remaining;
            uint64_t tick;
            float x;
        };
        const std::filesystem::path archive(directory);
        for (const auto saved : {SavedReturn{"miner-stage-0.sav", ChronoReturnPhase::Charging, 6, 727, 29.5f},
                SavedReturn{"miner-stage-1.sav", ChronoReturnPhase::Settling, 4, 737, 24.5f},
                SavedReturn{"miner-waiting.sav", ChronoReturnPhase::Waiting, 12, 0, 34.5f}}) {
            World previous;
            const auto file = (archive / saved.file).string();
            FILE* stream = std::fopen(file.c_str(), "rb");
            const bool loaded = stream && previous.loadGame(stream);
            if (stream) std::fclose(stream);
            constexpr EID minerId = 3, refineryId = 2;
            const bool waiting = saved.phase == ChronoReturnPhase::Waiting;
            const bool charging = saved.phase == ChronoReturnPhase::Charging;
            bool preserved = false, countdown = false, readyAtExpiry = false, replayed = false, finished = false;
            int effects = 0;
            if (loaded && previous.valid(minerId) && previous.valid(refineryId)) {
                const auto minerRef = previous.entityRef(minerId);
                const auto refineryRef = previous.entityRef(refineryId);
                const auto& actor = previous.ents[minerId];
                const auto initialMoney = previous.players[0].money;
                const auto target = actor.chronoReturn;
                preserved = previous.tick == saved.tick && actor.utype == UnitType::ChronoMiner
                    && actor.state == UState::HarvestReturn && actor.nativeDock.phase == NativeDockPhase::Approach
                    && actor.nativeDock.receiver == refineryRef && target.phase == saved.phase && target.ticksLeft == saved.remaining
                    && !target.directBerth && target.preferReceiver == waiting && actor.x == saved.x && actor.y == 21.5f
                    && actor.oreLoad == 20 && actor.gemLoad == (waiting ? 5 : 0) && previous.harvestedIncome[0] == 0;
                if (preserved) {
                    replayed = roundTrip(previous, std::string("chrono-legacy-") + saved.file, 500);
                    countdown = true;
                    for (int step = 1; step <= saved.remaining; ++step) {
                        previous.update();
                        countdown &= previous.valid(minerRef) && actor.state == UState::HarvestReturn
                            && actor.nativeDock.receiver == refineryRef && actor.oreLoad == 20 && actor.gemLoad == (waiting ? 5 : 0)
                            && previous.harvestedIncome[0] == 0 && previous.players[0].money == initialMoney;
                        for (const auto& effect : previous.effects) if (effect.kind == 9 && effect.age == 1) ++effects;
                        if (step < saved.remaining) countdown &= actor.chronoReturn.phase == saved.phase
                            && actor.chronoReturn.ticksLeft == saved.remaining - step && actor.nativeDock.phase == NativeDockPhase::Approach
                            && actor.x == saved.x && actor.y == 21.5f && effects == 0;
                    }
                    readyAtExpiry = waiting
                        ? actor.chronoReturn.phase == ChronoReturnPhase::Waiting && actor.chronoReturn.ticksLeft == ChronoReturnTiming::retry
                            && !actor.chronoReturn.directBerth && actor.nativeDock.phase == NativeDockPhase::Approach && actor.x == saved.x && actor.y == 21.5f
                        : actor.chronoReturn.phase == ChronoReturnPhase::None && actor.nativeDock.phase == NativeDockPhase::Align
                            && actor.x == 24.5f && actor.y == 21.5f;
                    const int amount = waiting ? 625 : 500;
                    const Vec2i expectedOre = waiting ? Vec2i{25, 24} : Vec2i{30, 21};
                    for (int step = 0; step < 900 && actor.state != UState::HarvestDig; ++step) previous.update();
                    finished = previous.valid(minerRef) && actor.state == UState::HarvestDig && actor.oreCell == expectedOre
                        && actor.nativeDock.phase == NativeDockPhase::None && previous.harvestedIncome[0] == amount
                        && previous.players[0].money == initialMoney + amount;
                }
            }
            std::printf("GROUND_TRAVEL_LEGACY_CHRONO file=%s timer=%d loaded=%d preserved=%d countdown=%d expiry=%d effects=%d replay=%d finished=%d\n",
                saved.file, saved.remaining, loaded, preserved, countdown, readyAtExpiry, effects, replayed, finished);
            check(loaded && preserved && countdown && readyAtExpiry && effects == (charging ? 2 : 0) && replayed && finished,
                "a real historical Chrono save keeps its remaining timer and resumes the original return without early income");
        }
        Game oldApp;
        const auto file = (archive / "charged-miner-app11.sav").string();
        const bool loaded = oldApp.loadGameFile(file.c_str());
        const auto& saved = oldApp.world;
        check(loaded && saved.valid(2) && saved.valid(3) && saved.valid(4)
            && saved.ents[3].nativeDock.receiver == saved.entityRef(2) && saved.ents[4].nativeDock.receiver == saved.entityRef(2)
            && saved.ents[3].chronoReturn.phase == ChronoReturnPhase::Charging && saved.ents[3].chronoReturn.ticksLeft == 12
            && saved.ents[4].chronoReturn.phase == ChronoReturnPhase::Waiting && saved.ents[4].chronoReturn.ticksLeft == 12,
            "the real old App11 preserves both pending Chrono return timers and their generation-bound refinery");
    }

    if (const char* directory = std::getenv("OPENRA2_LEGACY_CHRONO_APRON_DIR")) {
        struct SavedApron {
            const char* file;
            EID miner;
            uint64_t tick;
            ChronoReturnPhase phase;
            uint8_t remaining;
            float x, y;
        };
        const std::filesystem::path archive(directory);
        for (const auto saved : {SavedApron{"miner-current-stage-0.sav", 3, 721, ChronoReturnPhase::Charging, 1, 29.5f, 21.5f},
                SavedApron{"miner-current-stage-1.sav", 3, 722, ChronoReturnPhase::None, 0, 24.5f, 21.5f},
                SavedApron{"miner-current-wait-confirm-once.sav", 4, 73, ChronoReturnPhase::Waiting, 5, 29.5f, 24.5f}}) {
            constexpr EID oldRefinery = 2;
            World previous;
            const auto source = (archive / saved.file).string();
            FILE* stream = std::fopen(source.c_str(), "rb");
            char version[8]{};
            const bool world64 = stream && std::fread(version, 1, sizeof version, stream) == sizeof version
                && std::memcmp(version, "RA2WLD64", sizeof version) == 0;
            if (stream) std::rewind(stream);
            const bool loaded = world64 && previous.loadGame(stream);
            if (stream) std::fclose(stream);
            const bool wasAligned = saved.phase == ChronoReturnPhase::None;
            const bool waiting = saved.phase == ChronoReturnPhase::Waiting;
            const int expectedAlign = saved.remaining + (waiting ? ChronoReturnTiming::ready : 0);
            bool preserved = false, replay = false, held = true, entered = false, creditInBerth = false, finished = false;
            int alignAt = -1, arrivals = 0, departures = 0, unexpected = 0;
            if (loaded && previous.valid(saved.miner) && previous.valid(oldRefinery)) {
                const auto& actor = previous.ents[saved.miner];
                const auto identity = previous.entityRef(saved.miner);
                const auto target = previous.entityRef(oldRefinery);
                const auto initialMoney = previous.players[0].money;
                preserved = previous.tick == saved.tick && actor.utype == UnitType::ChronoMiner
                    && actor.state == UState::HarvestReturn && actor.nativeDock.receiver == target
                    && actor.nativeDock.phase == (wasAligned ? NativeDockPhase::Align : NativeDockPhase::Approach)
                    && actor.chronoReturn.phase == saved.phase && actor.chronoReturn.ticksLeft == saved.remaining
                    && !actor.chronoReturn.directBerth && actor.chronoReturn.preferReceiver == waiting
                    && actor.x == saved.x && actor.y == saved.y && actor.oreLoad == 20 && previous.harvestedIncome[0] == 0;
                if (preserved) {
                    if (wasAligned) alignAt = 0;
                    replay = roundTrip(previous, std::string("chrono-legacy-apron-") + saved.file, 480);
                    for (int elapsed = 1; elapsed <= 900; ++elapsed) {
                        const auto priorIncome = previous.harvestedIncome[0];
                        previous.update();
                        if (elapsed < expectedAlign) {
                            const bool ready = elapsed == saved.remaining;
                            held &= !actor.chronoReturn.directBerth && actor.nativeDock.receiver == target
                                && actor.nativeDock.phase == NativeDockPhase::Approach && previous.harvestedIncome[0] == 0
                                && actor.x == saved.x && actor.y == saved.y && actor.oreLoad == 20
                                && actor.chronoReturn.phase == (ready ? ChronoReturnPhase::Charging : ChronoReturnPhase::Waiting)
                                && actor.chronoReturn.ticksLeft == (ready ? ChronoReturnTiming::ready : saved.remaining - elapsed);
                        }
                        if (alignAt < 0 && actor.nativeDock.phase == NativeDockPhase::Align) {
                            alignAt = elapsed;
                            held &= actor.x == 24.5f && actor.y == 21.5f && actor.oreLoad == 20
                                && actor.nativeDock.receiver == target && actor.chronoReturn == ChronoReturnState{};
                        }
                        entered |= actor.nativeDock.phase == NativeDockPhase::Enter && actor.x == 24.5f && actor.y == 21.5f;
                        if (previous.harvestedIncome[0] != priorIncome) {
                            creditInBerth = actor.nativeDock.phase == NativeDockPhase::Unload
                                && actor.x == 23.5f && actor.y == 21.5f && actor.oreLoad == 0;
                        }
                        for (const auto& effect : previous.effects) if (effect.kind == 9 && effect.age == 1) {
                            if (effect.aux == (int)ChronoVisual::Departure && effect.x == saved.x && effect.y == saved.y) ++departures;
                            else if (effect.aux == (int)ChronoVisual::Arrival && effect.x == 24.5f && effect.y == 21.5f) ++arrivals;
                            else ++unexpected;
                        }
                        const bool done = waiting ? actor.state == UState::Idle && actor.x == 24.5f && actor.y == 21.5f
                            : actor.state == UState::HarvestDig && actor.oreCell == Vec2i{30, 21};
                        if (done) {
                            finished = previous.valid(identity) && actor.nativeDock.phase == NativeDockPhase::None
                                && previous.harvestedIncome[0] == 500 && previous.players[0].money == initialMoney + 500;
                            break;
                        }
                    }
                }
            }
            const int jumps = wasAligned ? 0 : 1;
            std::printf("GROUND_TRAVEL_LEGACY_APRON file=%s loaded=%d preserved=%d held=%d align=%d expected=%d "
                "entered=%d effects=%d/%d unexpected=%d paid=%d finished=%d replay=%d\n",
                saved.file, loaded, preserved, held, alignAt, expectedAlign, entered, departures, arrivals, unexpected,
                creditInBerth, finished, replay);
            check(loaded && preserved && held && alignAt == expectedAlign && entered && arrivals == jumps && departures == jumps
                && unexpected == 0 && creditInBerth && finished && replay,
                "an archived World64 miner uses its exterior landing and backing-in motion at the saved phase and timer");
        }
    }

    if(const char* directory=std::getenv("OPENRA2_LEGACY_WIDE_HEADING_DIR")) {
        const auto archive=std::filesystem::path(directory);
        const auto readArchived=[&](World& previous,const std::string& name,const char* expected) {
            const auto path=(archive/(name+".sav")).string();
            FILE* stream=std::fopen(path.c_str(),"rb");
            if(!stream)return false;
            char header[8]{};
            const bool matched=std::fread(header,1,sizeof header,stream)==sizeof header
                && std::memcmp(header,expected,sizeof header)==0;
            std::rewind(stream);
            const bool loaded=matched && previous.loadGame(stream);
            std::fclose(stream);
            return loaded;
        };
        constexpr EID archivedWideTank=2;
        const auto quadraticPose=[](const World& previous,const World::Ent& actor,int side) {
            const auto& arc=actor.driveArc;
            if(!arc.active() || !arc.wide || arc.cubic)return false;
            const float remaining=1.f-arc.progress;
            return std::abs(actor.x-(26.5f-remaining*remaining))<1e-5f
                && std::abs(actor.y-(20.5f+side*arc.progress*arc.progress))<1e-5f
                && previous.nativeHullHeading(actor,previous.tick)==NativeGroundMath::vector(remaining,side*arc.progress);
        };
        for(int side:{1,-1})for(bool after:{false,true}) {
            const std::string name=(side>0?"tank-wide-right":"tank-wide-left")
                + std::string(after?"-after-handoff":"-before-handoff");
            World previous;
            const bool loaded=readArchived(previous,name,"RA2WLD68");
            bool preserved=false,replayed=false,advanced=false,quadratic=true,exited=false,arrived=false;
            int curveTicks=0;
            if(loaded && previous.valid(archivedWideTank)) {
                auto path=initialPath;
                for(auto& cell:path)cell.y=20+side*(cell.y-20);
                const auto& actor=previous.ents[archivedWideTank];
                const auto& arc=actor.driveArc;
                const float initialProgress=after?.6108919382f:.2199766338f;
                const int initialRightHeading=after?26852:19251;
                preserved=previous.tick==(after?238:227) && arc.progress==initialProgress && arc.corner==Vec2i{26,20}
                    && arc.incoming==1 && arc.outgoing==Vec2i{0,side} && arc.layer==0
                    && actor.state==UState::Moving && actor.path==path && actor.pathIdx==(after?12:11)
                    && previous.nativeHullHeading(actor,previous.tick)==(side>0?initialRightHeading:32768-initialRightHeading)
                    && quadraticPose(previous,actor,side);
                if(preserved) {
                    replayed=roundTrip(previous,"legacy-wide-heading-"+name,180,"RA2WLD68");
                    previous.update();
                    const int nextRightHeading=after?27551:19784;
                    const float nextX=after?26.3751f:25.9414f;
                    const float nextY=after?.4181f:.0638f;
                    advanced=arc.progress>initialProgress && quadraticPose(previous,actor,side)
                        && previous.nativeHullHeading(actor,previous.tick)==(side>0?nextRightHeading:32768-nextRightHeading)
                        && std::abs(actor.x-nextX)<.0001f && std::abs(side*(actor.y-20.5f)-nextY)<.0001f;
                    for(int t=1;t<1600 && actor.state!=UState::Idle;++t) {
                        const bool active=arc.active();
                        if(active) {
                            quadratic &= quadraticPose(previous,actor,side) && actor.navLayer==0 && actor.path==path
                                && actor.pathIdx==(arc.progress<NativeDriveMath::wideHandoff?11:12)
                                && previous.map.passable((int)actor.x,(int)actor.y);
                            ++curveTicks;
                        }
                        previous.update();
                        if(active && !arc.active()) {
                            const auto expectedHeading=NativeGroundMath::vector(0.f,float(side));
                            exited=actor.pathIdx==13 && actor.x==26.5f && side*(actor.y-20.5f)>=1.f
                                && previous.nativeHullHeading(actor,previous.tick)==expectedHeading;
                        }
                    }
                    arrived=actor.state==UState::Idle && !arc.active() && actor.x==26.5f && actor.y==20.5f+side*10;
                }
            }
            std::printf("GROUND_TRAVEL_LEGACY_WIDE side=%d after=%d loaded=%d preserved=%d advanced=%d quadratic=%d "
                "frames=%d exited=%d arrived=%d replay=%d\n",side,after,loaded,preserved,advanced,quadratic,curveTicks,exited,
                arrived,replayed);
            check(loaded && preserved && advanced && quadratic && curveTicks>0 && exited && arrived && replayed,
                "the archived World68 wide bend retains quadratic coordinates, hull and waypoint handoff");
        }
        constexpr const char* mixedName="wide-arc-with-chrono-berth";
        if(std::filesystem::is_regular_file(archive/(std::string(mixedName)+".sav"))) {
            World previous;
            const bool loaded=readArchived(previous,mixedName,"RA2WLD69");
            constexpr EID archivedChrono=4;
            const bool preserved=loaded && previous.tick==233 && previous.valid(archivedWideTank)
                && quadraticPose(previous,previous.ents[archivedWideTank],1) && previous.valid(archivedChrono)
                && previous.ents[archivedChrono].chronoReturn.directBerth
                && previous.ents[archivedChrono].chronoReturn.phase==ChronoReturnPhase::Charging;
            const bool replay=preserved && roundTrip(previous,"legacy-wide-heading-with-chrono",180,"RA2WLD69");
            check(replay,"a genuine World69 save keeps both the quadratic bend and the original pending Chrono return");
        }
    }

    struct ArchivedCurve {
        const char* variable;
        const char* checkpoint;
        float x,y,progress;
        uint16_t heading;
    };
    for(const ArchivedCurve archived:{
        ArchivedCurve{"OPENRA2_LEGACY_CURVE_INPUT","tank-old-active-curve",
            26.36716651916504f,20.561458587646484f,.40483570098876953f,22614},
        ArchivedCurve{"OPENRA2_LEGACY_WIDE_TURN_INPUT","tank-compact-before-wide",
            26.366926193237305f,20.561294555664062f,.40429407358169556f,22603}
    }) {
        const char* input=std::getenv(archived.variable);
        if(!input)continue;
        World previous;
        FILE* stream = std::fopen(input, "rb");
        char version[8]{};
        const bool world64 = stream && std::fread(version, 1, sizeof version, stream) == sizeof version
            && std::memcmp(version, "RA2WLD64", sizeof version) == 0;
        if (stream) std::rewind(stream);
        const bool loaded = world64 && previous.loadGame(stream);
        if (stream) std::fclose(stream);
        constexpr EID archivedTank = 2;
        const Vec2i destination{26, 30};
        bool preserved=false,advanced=false,replay=false,arrived=false,compact=true,exited=false,recorded=false;
        int compactFrames=0;
        if (loaded && previous.valid(archivedTank)) {
            const auto& actor = previous.ents[archivedTank];
            const auto& arc = actor.driveArc;
            preserved = previous.tick == 241 && actor.nativeIdentity.definition && actor.nativeIdentity.definition->id == "MTNK"
                && actor.player == 0 && actor.state == UState::Moving && actor.x == archived.x && actor.y == archived.y
                && previous.nativeHullHeading(actor, previous.tick) == archived.heading && arc.progress == archived.progress && !arc.wide
                && arc.corner == Vec2i{26, 20} && arc.incoming == 1 && arc.outgoing == Vec2i{0, 1} && arc.layer == 0
                && actor.pathIdx == 11 && actor.path == initialPath && actor.path.back() == destination;
        }
        if (preserved) {
            const auto tankRef = previous.entityRef(archivedTank);
            replay = roundTrip(previous,archived.checkpoint,360);
            std::ifstream reencoded(output/(std::string(archived.checkpoint)+".sav"),std::ios::binary);
            const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(reencoded),{}};
            const auto travel=locateWorldSaveSection(bytes,WorldSaveSection::NativeTravel);
            recorded=travel && travel->length>=13 && std::memcmp(bytes.data(),"RA2WLD64",8)==0
                && bytes[travel->offset+12]==1;

            previous.update();
            const auto& advancing = previous.ents[archivedTank];
            advanced = previous.valid(tankRef) && advancing.state == UState::Moving
                && advancing.driveArc.corner == Vec2i{26, 20} && advancing.driveArc.progress > archived.progress
                && advancing.x > archived.x && advancing.y > archived.y && advancing.path == initialPath
                && matchesArcPose(advancing, previous.nativeHullHeading(advancing, previous.tick));
            for (int t = 1; t < 1600 && previous.valid(tankRef) && previous.ents[archivedTank].state != UState::Idle; ++t) {
                const auto& vehicle=previous.ents[archivedTank];
                const bool wasActive=vehicle.driveArc.active();
                if(wasActive) {
                    const float progress=vehicle.driveArc.progress;
                    const float remaining=1.f-progress;
                    compact &= !vehicle.driveArc.wide && vehicle.pathIdx==11
                        && std::abs(vehicle.x-(26.5f-.375f*remaining*remaining))<1e-5f
                        && std::abs(vehicle.y-(20.5f+.375f*progress*progress))<1e-5f;
                    ++compactFrames;
                }
                previous.update();
                if(wasActive && !vehicle.driveArc.active())
                    exited=vehicle.pathIdx==12 && vehicle.x==26.5f && vehicle.y>=20.875f
                        && previous.nativeHullHeading(vehicle,previous.tick)==NativeGroundMath::fromDir(3);
            }
            const auto& atGoal = previous.ents[archivedTank];
            arrived = previous.valid(tankRef) && atGoal.state == UState::Idle && !atGoal.driveArc.active()
                && atGoal.x == destination.x + .5f && atGoal.y == destination.y + .5f;
        }
        std::printf("GROUND_TRAVEL_LEGACY_CURVE source=%s loaded=%d preserved=%d advanced=%d compact=%d frames=%d "
            "exited=%d recorded=%d arrived=%d replay=%d\n",
            archived.variable,loaded,preserved,advanced,compact,compactFrames,exited,recorded,arrived,replay);
        check(loaded && preserved && advanced && compact && compactFrames>0 && exited && recorded && arrived && replay,
            "the archived World64 mid-curve save stays compact through its exit and retains the original travel record");
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_TRAVEL_INPUT_DIR")) {
        std::string oldRules;
        for(const char* name:{"turn-midway.sav","load-CMIN-20-0-0.sav"}) {
            const auto file=(std::filesystem::path(directory)/name).string();
            FILE* stream=std::fopen(file.c_str(),"rb");World previous;
            const bool loaded=stream && previous.loadGame(stream);if(stream)std::fclose(stream);
            if(loaded)oldRules=previous.nativeScenarioSource;
            check(loaded && previous.nativeSceneVersion==45 && !previous.nativeTravelEnabled()
                && roundTrip(previous,std::string("legacy-world63-")+name,240),
                "a pre-change World63 save uses its embedded rules and resumes without travel state");
        }
        const World::RulesSnapshot snapshot{oldRules,true,true};
        if(!fresh(&snapshot))return failures+1;
        const auto target=world.spawnBuilding(0,BldType::OreRefinery,20,20,true,type("GAREFN"));
        const auto older=world.spawnUnit(0,UnitType::ChronoMiner,29.5f,21.5f,type("CMIN"));
        world.ents[older].oreLoad=20;command(World::Cmd::ReturnHarvester,older,target);
        check(!world.nativeTravelEnabled() && world.ents[older].x==24.5f && world.ents[older].chronoReturn.phase==ChronoReturnPhase::None,
            "a game restored from the embedded old rules still uses its immediate return jump");
    }
    return failures;
}
