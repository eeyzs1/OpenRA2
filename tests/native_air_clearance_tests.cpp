#include "game/native_jumpjet.h"
#include "fixtures/native_air_clearance_reference.h"
#include <cstdio>

int main() {
    int failures=0;
    for(const auto& c:NativeAirClearanceReference::cases){
        NativeAirProbeCell here{c.floor,c.building?c.building*104:-1,c.bridge?416:0,c.building!=0 || c.other};
        NativeAirProbeCell next{c.next,c.nextBuilding?c.nextBuilding*104:-1,c.nextBridge?416:0,c.nextBuilding!=0 || c.nextOther};
        const auto offset=NativeJumpjetMath::probeOffset((uint16_t)c.heading);
        const int height=NativeJumpjetMath::clearance(here,next,c.speed>0,c.enabled);
        const bool moving=c.speed>0;
        const bool ok=height==c.result && c.x==25600+(moving?offset.x*256:0) && c.y==25600+(moving?offset.y*256:0);
        std::printf("AIR_CLEARANCE %s %s heading=%d height=%d expected=%d\n",ok?"PASS":"FAIL",c.name,c.heading,height,c.result);failures+=!ok;
    }
    return failures?1:0;
}
