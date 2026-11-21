#pragma once
#include "game/data.h"
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

bool parseNativeMovementRules(const Ini& ini,int houses,bool required,NativeMovementRules& rules);
bool parseNativeMovementProfile(const Ini& ini,int type,NativeMovementProfile& profile);
