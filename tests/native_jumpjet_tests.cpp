#include "game/native_jumpjet.h"
#include "fixtures/native_jumpjet_reference.h"
#include <cstdio>
#include <bit>

int main() {
    int failures=0;
    for(const auto& c:NativeJumpjetReference::cases) {
        NativeJumpjetProfile p;p.available=true;p.speed=c.speed;p.turnRate=c.turn;p.climb=(float)c.climb;p.height=c.height;
        p.acceleration=(float)c.acceleration;p.wobbles=c.wobbles;p.deviation=c.deviation;p.noWobbles=c.noWobbles;p.balloon=c.balloon;p.infantry=c.infantry;
        NativeJumpjetState s;s.phase=(NativeJumpjetPhase)c.phase;s.goalX=25600;s.goalY=25600-c.distance;
        s.goalHeight=c.phase?c.height:0;s.moving=c.distance!=0;
        int x=25600,y=25600,z=c.initialHeight;bool same=true;
        for(int i=0;i<c.count;++i) {
            if(c.retarget && i+1==20){s.goalX=51200;s.goalY=25600;}
            NativeJumpjetMath::advance(p,s,x,y,z,c.ground,c.ahead,false);x+=s.motion.dx;y+=s.motion.dy;z+=s.dz;
            const auto& e=c.frames[i];
            const bool row=x==e.x && y==e.y && z==e.z && s.goalHeight==e.height && (int)s.phase==e.phase
                && s.motion.facing==e.body && s.motion.desiredFacing==e.to && s.motion.startFacing==e.from && s.motion.turnFrames==e.duration
                && std::bit_cast<uint64_t>(s.speed)==e.speed && std::bit_cast<uint64_t>(s.desiredSpeed)==e.desired
                && std::bit_cast<uint64_t>(s.wobble)==e.wobble;
            if(same && !row)std::printf("JUMPJET_MATH TRACE %s frame=%d xyz=%d,%d,%d expected=%d,%d,%d speed=%.17g desired=%.17g height=%d phase=%d body=%d to=%d from=%d duration=%d wobble=%016llx expected=%016llx\n",
                c.name,i+1,x,y,z,e.x,e.y,e.z,s.speed,s.desiredSpeed,s.goalHeight,(int)s.phase,s.motion.facing,s.motion.desiredFacing,s.motion.startFacing,s.motion.turnFrames,
                (unsigned long long)std::bit_cast<uint64_t>(s.wobble),(unsigned long long)e.wobble);
            same &= row;
        }
        std::printf("JUMPJET_MATH %s %s (%d original frames)\n",same?"PASS":"FAIL",c.name,c.count);failures+=!same;
    }
    return failures?1:0;
}
