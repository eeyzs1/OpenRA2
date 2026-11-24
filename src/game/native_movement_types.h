#pragma once
#include "game/data.h"
#include <algorithm>
#include <array>
#include <cstdint>

struct Ini;
enum class NativeLocomotor : uint8_t { None,Drive,Ship,Hover,Teleport };
enum class NativeSpeedType : uint8_t { Foot,Track,Wheel,Float,Winged,Hover,Amphibious,Creep,FloatBeach };

struct NativeMovementProfile {
    NativeLocomotor locomotor=NativeLocomotor::None;
    NativeSpeedType speedType=NativeSpeedType::Track;
    int maxSpeed=0,slowdownDistance=500;
    bool accelerates=true,passive=false;
    double acceleration=.03,deacceleration=.002;
    std::array<double,3> veteranSpeed{1,1,1};
    bool available() const{return locomotor!=NativeLocomotor::None;}
};

struct NativeMovementRules {
    bool available=false;
    std::array<std::array<double,9>,12> land{};
    std::array<double,MAX_PLAYERS> houseSpeed{1,1,1,1,1,1,1,1};
    double trackedUphill=1,trackedDownhill=1,wheeledUphill=1,wheeledDownhill=1;
    double conditionYellow=.5,hoverAcceleration=.02,hoverBrake=.03,hoverBoost=1.5;
};

struct NativeMovementState {
    double throttle=0,targetThrottle=0,credit=0;
    uint64_t lastTick=0;
    int nodeX=-1,nodeY=-1,nodeLayer=-1,heldSpeed=0;
    uint8_t half=0;
    bool started=false;
    bool operator==(const NativeMovementState&) const=default;
};

struct NativeCrushWarningRules {
    bool available=false,playerScatter=false;
    int computerIQ=0,scatterIQ=0;
};

bool parseNativeMovementRules(const Ini& ini,int houses,bool required,NativeMovementRules& rules);
struct NativeTransportUnloadRules {
    bool available=false;
    int rateFrames=14,randomFrames=2,movingFrames=10,stageFrames=1;
    int maxTicks() const {return 2*std::max({rateFrames+randomFrames,movingFrames,stageFrames});}
};
enum class NativeTransportUnloadPhase : uint8_t { None,Prepare,Ready,Release,Finish };
struct NativeTransportUnloadState {
    NativeTransportUnloadPhase phase=NativeTransportUnloadPhase::None;
    int ticks=0;
    bool active() const {return phase!=NativeTransportUnloadPhase::None;}
    bool operator==(const NativeTransportUnloadState&) const=default;
};
namespace NativeTransportMath {
inline constexpr std::array<std::array<int,2>,8> directions{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
struct Exit {int direction=-1,facing=4;};
inline Exit selectExit(uint16_t heading,uint8_t available,bool isTrain) {
    const uint16_t rear=static_cast<uint16_t>(heading+0x7fff);
    const int wanted=static_cast<int8_t>((((rear>>7)+1)>>1)&255);
    int best=-1;Exit result;
    for(int d=0;d<8;++d) {
        int difference=static_cast<int8_t>(d*32)-wanted;if(difference<0)difference=-difference;
        const int score=((available&(1u<<d))?128:-128)-difference-(d==4?100:0);
        if(best==-1 || score>best){best=score;result.direction=d;}
    }
    if(best<=0)result.direction=-1;
    else result.facing=(result.direction+4)&7;
    if(isTrain)result.facing=(((heading>>12)+1)>>1)&7;
    return result;
}
struct Release {int direction=-1;bool moveFar=false;};
inline Release selectRelease(uint16_t heading,uint8_t nearCells,uint8_t farCells) {
    const uint16_t rear=static_cast<uint16_t>(heading+0x7fff);
    const int first=(((rear>>12)+1)>>1)&7;
    for(int i=0;i<8;++i){const int d=(first+i)&7;if((nearCells&farCells)&(1u<<d))return {d,true};}
    // The original retry path starts at offset1, not at the first rear cell.
    for(int i=1;i<8;++i){const int d=(first+i)&7;if(nearCells&(1u<<d))return {d,false};}
    return {};
}
}
bool parseNativeMovementProfile(const Ini& ini,int type,NativeMovementProfile& profile);
