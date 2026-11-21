#include "game/world.h"
#include <cmath>

bool World::nativeFlyToward(Ent& e,float tx,float ty) {
    const auto& profile=fixedWingProfile(e);
    auto& s=e.nativeFlight;
    const auto target=NativeFlightMath::direction((double)tx-e.x,(double)ty-e.y);
    if(!s.started) {
        e.x=(int)(e.x*256)/256.f;e.y=(int)(e.y*256)/256.f;
        constexpr int dx[]={1,1,1,0,-1,-1,-1,0},dy[]={-1,0,1,1,1,0,-1,-1};
        s={};s.started=true;s.facing=s.startFacing=s.desiredFacing=NativeFlightMath::direction(dx[e.dir&7],dy[e.dir&7]);
        NativeFlightMath::turn(s,target,profile.rotation);
    } else if(s.lastTick==tick)return distf(e.x,e.y,tx,ty)<=1.f/256;
    s.lastTick=tick;
    if(!s.half) {
        ++s.frame;NativeFlightMath::turn(s,target,profile.rotation);
        const bool brake=e.state==UState::Moving || e.state==UState::AttackMoving || e.state==UState::Returning;
        NativeFlightMath::advance(profile,s,(int)(e.x*256),(int)(e.y*256),(int)(tx*256),(int)(ty*256),brake);
    }
    e.x+=s.dx/512.f;e.y+=s.dy/512.f;s.half^=1;
    e.dir=e.turretDir=(((s.facing+4096)/8192)-1)&7;
    return distf(e.x,e.y,tx,ty)<=1.f/256;
}
