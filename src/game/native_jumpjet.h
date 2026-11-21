#pragma once
#include "game/native_flight.h"

struct NativeJumpjetProfile {
    bool available=false,balloon=false,infantry=false,noWobbles=false;
    int version=1,speed=0,turnRate=1,height=500,deviation=40;
    float climb=5,crash=5,acceleration=2,wobbles=.15f;
};
enum class NativeJumpjetPhase:uint8_t { Ground,Ascent,Hover,Travel,Descent };
struct NativeJumpjetState {
    NativeFlightState motion;
    NativeJumpjetPhase phase=NativeJumpjetPhase::Ground;
    double speed=0,desiredSpeed=0,wobble=0;
    int goalX=0,goalY=0,goalHeight=0,dz=0,ground=0;
    bool moving=false,requested=false;
    bool operator==(const NativeJumpjetState&) const=default;
};
bool parseNativeJumpjetProfile(const Ini& ini,int type,NativeJumpjetProfile& profile);
struct NativeAirProbeCell {
    int floor=0,buildingHeight=-1,bridgeHeight=0;
    bool occupied=false;
};
struct NativeAirProbeOffset {int x=0,y=0;};
namespace NativeJumpjetMath {
NativeAirProbeOffset probeOffset(uint16_t heading);
int clearance(const NativeAirProbeCell& current,const NativeAirProbeCell& next,bool moving,bool active=true);
double nextSpeed(const NativeJumpjetProfile& profile,double current,double desired);
// One 15-Hz frame of the original healthy actor movement core. Terrain
// sampling, map occupation, mission commands and sound are supplied by World.
void advance(const NativeJumpjetProfile& profile,NativeJumpjetState& state,
             int x,int y,int z,int ground,int ahead,bool hasTarget);
}
