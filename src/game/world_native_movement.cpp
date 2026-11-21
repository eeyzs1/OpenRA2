#include "game/world.h"
#include "game/world_priv.h"
#include "game/native_movement_math.h"
#include <algorithm>
#include <cmath>

namespace {
// GAME.EXE straight drive/ship tracks: 24 cardinal or 32 diagonal samples,
// each consuming seven speed-accumulator units. Cardinal offsets advance
// 11 leptons per sample until 253, then the final three leptons.
double phaseForProgress(double progress,bool diagonal) {
    if(diagonal)return progress*7/8;
    return progress<=253?progress*7/11:161+(progress-253)*7/3;
}
double progressForPhase(double phase,bool diagonal) {
    if(diagonal)return phase*8/7;
    return phase<=161?phase*11/7:253+(phase-161)*3/7;
}
int truncatedSpeed(double speed) {
    return (int)std::clamp(speed,0.,100000000.);
}
int nativeLand(uint8_t tile) {
    // TMP terrain byte is not LandType. Original IsometricTileType mapping.
    constexpr uint8_t land[16]={0,8,8,8,8,10,9,3,3,2,6,1,1,0,7,3};
    return tile<16?land[tile]:3;
}
}

void World::prepareNativeMovement(Ent& e,int x,int y,int layer,Vec2i goal) {
    const auto& profile=e.nativeIdentity.definition->movement;
    auto& state=e.nativeMovement;
    const bool hover=profile.locomotor==NativeLocomotor::Hover;
    if(state.lastTick!=tick && (!state.started || state.lastTick+1!=tick))state={};
    if(state.nodeX!=x || state.nodeY!=y || state.nodeLayer!=layer) {
        state.nodeX=x;state.nodeY=y;state.nodeLayer=layer;
        double target=1;
        if(!hover) {
            const auto& cell=map.at(x,y);
            int land=layer?1:map.native.tiles.size()==map.cells.size()?nativeLand(map.native.tiles[(size_t)y*map.w+x].terrainType)
                :cell.terrain==Terrain::Water?2:cell.terrain==Terrain::Rough?7:cell.terrain==Terrain::Bridge?1:0;
            if(!layer && cell.ore>0 && (cell.terrain==Terrain::Ore || cell.terrain==Terrain::Gems))land=5;
            target=land>=0 && land<(int)nativeMovementRules.land.size()
                ?std::min(1.,nativeMovementRules.land[land][(size_t)profile.speedType]):0;
            const int delta=map.surfaceHeight(x,y,layer)-map.surfaceHeight((int)e.x,(int)e.y,e.navLayer);
            const bool tracked=profile.speedType==NativeSpeedType::Track;
            if(delta>0)target=NativeMovementMath::multiply(target,tracked?nativeMovementRules.trackedUphill:nativeMovementRules.wheeledUphill);
            else if(delta<0)target=NativeMovementMath::multiply(target,tracked?nativeMovementRules.trackedDownhill:nativeMovementRules.wheeledDownhill);
            // Original normal drive falls back to half speed for zero cost;
            // admission and path legality remain the separate surface checks.
            if(target==0)target=.5;
            if(e.hp<=maxHpFor(e,unitDef(e))*nativeMovementRules.conditionYellow)target=NativeMovementMath::multiply(target,.75);
        }
        state.targetThrottle=target;
    }
    if(state.lastTick!=tick) {
        const bool justStarted=!state.started;
        state.lastTick=tick;state.started=true;
        if(state.half==0) {
            const double remaining=std::hypot(goal.x+.5-e.x,goal.y+.5-e.y);
            if(hover) {
                const double dx=x+.5-e.x,dy=y+.5-e.y;
                double boost=1;
                if(e.pathIdx+1<(int)e.path.size()) {
                    const auto following=e.path[e.pathIdx+1];
                    if(dirFromVec((float)dx,(float)dy)==dirFromVec((float)(following.x-x),(float)(following.y-y)))boost=nativeMovementRules.hoverBoost;
                }
                const double target=std::min(1.,NativeMovementMath::multiply(boost,remaining<1?.5:1.));
                if(state.throttle<target)state.throttle=std::min(target,NativeMovementMath::add(state.throttle,
                    nativeMovementRules.hoverAcceleration>0?NativeMovementMath::dividePositive(1,NativeMovementMath::multiply(nativeMovementRules.hoverAcceleration,900)):1));
                else if(state.throttle>target)state.throttle=std::max(0.,NativeMovementMath::subtract(state.throttle,
                    nativeMovementRules.hoverBrake>0?NativeMovementMath::dividePositive(1,NativeMovementMath::multiply(nativeMovementRules.hoverBrake,900)):1));
            } else if(!profile.accelerates || profile.passive)state.throttle=state.targetThrottle;
            else {
                if(remaining*256<profile.slowdownDistance)
                    state.throttle=std::max((double).3f,NativeMovementMath::subtract(state.throttle,NativeMovementMath::multiply(profile.maxSpeed,profile.deacceleration)));
                else if(state.throttle<state.targetThrottle)state.throttle=std::min(state.targetThrottle,NativeMovementMath::add(state.throttle,profile.acceleration));
                else if(state.throttle>state.targetThrottle)state.throttle=std::max(state.targetThrottle,NativeMovementMath::subtract(state.throttle,NativeMovementMath::multiply(profile.maxSpeed,profile.deacceleration)));
            }
            const double house=movementSpeedMultiplier(e.player);
            double boost=nativeCrateMultiplier(e.nativeIdentity,10);
            if(e.crateSpeedBoost>0)boost*=1.25;
            int speed=truncatedSpeed(NativeMovementMath::multiply(NativeMovementMath::multiply(profile.maxSpeed,house),boost));
            speed=truncatedSpeed(NativeMovementMath::multiply(speed,profile.veteranSpeed[std::clamp(e.vetRank,0,2)]));
            speed=truncatedSpeed(NativeMovementMath::multiply(speed,state.throttle));
            // An ordinary track start first establishes its driver. An
            // active refinery departure uses its first computed speed.
            const bool delayFirstCredit=justStarted && e.nativeDock.phase!=NativeDockPhase::DriveExit;
            state.heldSpeed=delayFirstCredit?0:speed;
        }
        state.half^=1;
        state.credit+=state.heldSpeed*.5; // two deterministic 30-Hz halves
    }
}

float World::nativeMovementStep(Ent& e,int x,int y,int layer,Vec2i goal,float stopBefore) {
    const auto& profile=e.nativeIdentity.definition->movement;
    auto& state=e.nativeMovement;
    if(!profile.maxSpeed){state={};return 0;}
    const double dx=x+.5-e.x,dy=y+.5-e.y,dist=std::hypot(dx,dy),axis=std::max(std::abs(dx),std::abs(dy));
    if(dist<1e-8)return 0;
    prepareNativeMovement(e,x,y,layer,goal);
    if(state.credit<=0)return 0;
    if(profile.locomotor==NativeLocomotor::Hover) {
        const double used=std::min(state.credit,dist*256);state.credit-=used;
        return (float)(used/256);
    }
    const bool diagonal=std::abs(dx)>1e-6 && std::abs(dy)>1e-6;
    const double progress=256-axis*256;
    const double phase=phaseForProgress(progress,diagonal);
    const double end=stopBefore>0?phaseForProgress(256-stopBefore*axis/dist*256,diagonal):diagonal?224:168;
    const double used=std::min(state.credit,std::max(0.,end-phase));
    state.credit=std::max(0.,state.credit-used);
    const double advanced=progressForPhase(phase+used,diagonal)-progress;
    return (float)std::min(dist,advanced/256*dist/axis);
}
