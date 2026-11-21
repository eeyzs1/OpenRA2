#include "game_regression.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>

int GameRegression::nativeGroundImpactRegression(const World& airborne,EID missile,const char* outputDir) {
    int failures=0;std::filesystem::create_directories(outputDir);
    const auto check=[&](bool ok,int scenario,const char* text) {
        failures+=!ok;std::printf("NATIVE_GROUND_IMPACT %s case=%s scenario=%d %s\n",ok?"PASS":"FAIL",outputDir,scenario,text);std::fflush(stdout);
    };
    const auto find=[&](World& w)->NativeRocketState* {
        for(auto& r:w.nativeRockets)if(r.actor==missile)return &r;return nullptr;
    };
    for(int scenario=0;scenario<6;++scenario) {
        World sample=airborne;sample.map.bldOccRef=&sample.bldOcc;
        auto* initial=find(sample);
        if(!sample.valid(missile) || !initial || initial->phase!=NativeRocketPhase::Flight){check(false,scenario,"an actual ordinary attack supplies the airborne missile fixture");continue;}
        if(sample.valid(initial->source))sample.kill(initial->source,false);
        auto* rocket=find(sample);
        if(!rocket || !sample.valid(missile)){check(false,scenario,"launcher retirement preserves the free-flight missile");continue;}
        const auto& definition=sample.nativeRocketDefinitions[rocket->definition];
        const float x=std::floor(rocket->destinationX-4.f)+.5f,y=std::floor(rocket->destinationY)+.5f;
        const int cx=(int)x,cy=(int)y;
        sample.map.at(cx,cy)=Cell{};
        if(scenario==1)sample.map.at(cx,cy).height=3;
        const float floor=sample.map.surfaceHeight(cx,cy,0)*104.f;
        const auto victim=sample.spawnUnit(1,UnitType::MCV,x,y);
        const int beforeHp=sample.valid(victim)?sample.ents[victim].hp:0;
        EID destination=INVALID_EID;
        for(EID id=0;id<(EID)sample.ents.size();++id) {
            const auto& e=sample.ents[id];if(e.alive && e.isBuilding && e.player==1 && e.btype==BldType::OreRefinery){destination=id;break;}
        }
        const int destinationHp=sample.valid(destination)?sample.ents[destination].hp:0;
        auto& actor=sample.ents[missile];
        // Derive boundary snapshots from real flight. The low orbit is a valid
        // persisted state of the same kind observed in the actual late App11.
        actor.x=actor.px=x;actor.y=actor.py=y;actor.target=INVALID_EID;
        rocket->altitude=floor+(scenario==2?2.f:scenario==4?512.f:scenario==5?1.f:0.f);
        rocket->speed=(float)std::min(255,(int)(definition.maxSpeed*256/100));
        rocket->pitch=0;rocket->heading=scenario==4?29529.96875f:1.57079632679f;
        rocket->cruised=rocket->diving=scenario!=3;rocket->phaseTicks=600000;
        actor.nativeAltitude=actor.previousNativeAltitude=rocket->altitude;actor.nativePitch=rocket->pitch;
        sample.airOccSync(missile);
        const bool setup=sample.valid(victim) && sample.valid(destination) && sample.validateNativeRocketState();
        if(!setup){check(false,scenario,"the contact boundary remains a valid source snapshot");continue;}
        const auto snapshot=[&](const char* suffix) {
            world=sample;world.map.bldOccRef=&world.bldOcc;ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
            selectRulesChoice(sample.nativeRocketDefinitions.size()==3?RulesChoice::YR:RulesChoice::RA2);
            phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;clearPlacement();
            const auto a=(std::filesystem::path(outputDir)/(std::to_string(scenario)+suffix+"-app11.sav")).string();
            const auto b=(std::filesystem::path(outputDir)/(std::to_string(scenario)+suffix+"-world56.sav")).string();
            FILE* file=std::fopen(b.c_str(),"wb");const bool saved=file && sample.saveGame(file);if(file)std::fclose(file);
            return saved && saveGameFile(a.c_str());
        };
        auto replay=std::make_unique<Game>();
        const auto saved=(std::filesystem::path(outputDir)/(std::to_string(scenario)+"-before-app11.sav")).string();
        bool exact=snapshot("-before") && replay->loadGameFile(saved.c_str()) && replay->world.checksum()==sample.checksum();
        sample.update();replay->world.update();exact &= replay->world.checksum()==sample.checksum();
        const bool alive=sample.valid(missile);
        if(scenario<=1) {
            const bool damaged=!sample.valid(victim) || sample.ents[victim].hp<beforeHp;
            check(!alive && damaged && sample.valid(destination) && sample.ents[destination].hp==destinationHp,scenario,
                "a descending missile contacts real terrain and applies its payload locally, without teleporting damage to the captured target");
        } else if(scenario==4) {
            rocket=find(sample);
            check(alive && rocket && std::abs(rocket->heading)<=3.1415928f,scenario,
                "a historical large flight heading advances within the bounded angle range");
        } else check(alive,scenario,scenario==2?"two leptons of clearance do not count as terrain contact":scenario==5?"one lepton of clearance does not count as terrain contact":"a launch climbing from ground level is not mistaken for a descending impact");
        for(int tick=0;tick<120 && exact;++tick) {
            sample.update();replay->world.update();exact &= replay->world.checksum()==sample.checksum();
            sample.presentationEvents.clear();sample.evaQueue.clear();replay->world.presentationEvents.clear();replay->world.evaQueue.clear();
        }
        check(exact && snapshot("-after"),scenario,"actual World56 and App11 contact/clearance states resume exactly for 121 ticks");
    }
    return failures;
}
