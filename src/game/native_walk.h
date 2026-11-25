#pragma once
#include "game/native_flight.h"
#include "game/native_movement_math.h"

// Walk uses the same source atan/trig/root primitives as horizontal flight.
// Inputs and committed positions are integer leptons, one original15-Hz frame.
namespace NativeWalkMath {
inline int speed(int maximum,double house,double bonus,double veteran,double current=1) {
    int result=(int)NativeMovementMath::multiply(NativeMovementMath::multiply(maximum,house),bonus);
    result=(int)NativeMovementMath::multiply(result,veteran);
    return (int)NativeMovementMath::multiply(result,current);
}
struct Step {int x=0,y=0;bool arrived=false;};
inline Step advance(int x,int y,int targetX,int targetY,int speed) {
    if(NativeFlightMath::distance(x,y,targetX,targetY)<17)return {targetX,targetY,true};
    int dx=0,dy=0;
    NativeFlightMath::planarStep(NativeFlightMath::direction(targetX-x,targetY-y),speed,x,y,dx,dy);
    return {x+dx,y+dy,false};
}
}
