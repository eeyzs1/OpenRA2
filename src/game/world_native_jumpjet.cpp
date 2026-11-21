#include "game/world.h"
#include <algorithm>
#include <cmath>

namespace {
using Phase=NativeJumpjetPhase;
int ground(const World& world,float x,float y,bool deck=false) {
    const int cx=std::clamp((int)std::floor(x),0,world.map.w-1),cy=std::clamp((int)std::floor(y),0,world.map.h-1);
    return world.map.surfaceHeight(cx,cy,deck && world.map.hasDeck(cx,cy)?1:0)*104;
}
}
bool World::usesNativeJumpjet(const Ent& e) const {
    return nativeJumpjetEnabled && !e.isBuilding && e.nativeIdentity.definition && e.nativeIdentity.definition->jumpjet.available;
}
void World::initializeNativeJumpjet(Ent& e) {
    if(!usesNativeJumpjet(e) || e.nativeJumpjet.motion.started)return;
    const bool navigation=usesNativeAirNavigation(e);
    if(navigation){e.x=(int)(e.x*256)/256.f;e.y=(int)(e.y*256)/256.f;
        e.navLayer=e.previousNavLayer=e.goalLayer=(uint8_t)(map.hasDeck((int)e.x,(int)e.y)?1:0);}
    auto& s=e.nativeJumpjet;s={};auto& m=s.motion;m.started=true;
    constexpr int dx[]={1,1,1,0,-1,-1,-1,0},dy[]={-1,0,1,1,1,0,-1,-1};
    m.facing=m.startFacing=m.desiredFacing=NativeFlightMath::direction(dx[e.dir&7],dy[e.dir&7]);
    s.goalX=(int)(e.x*256);s.goalY=(int)(e.y*256);s.ground=ground(*this,e.x,e.y,navigation);
    e.nativeAltitude=e.previousNativeAltitude=(float)s.ground;
    if(!e.nativeIdentity.definition->jumpjet.balloon && e.utype!=UnitType::SiegeChopper && e.state==UState::Circling)e.state=UState::Landed;
}
bool World::nativeJumpjetToward(Ent& e,float x,float y) {
    initializeNativeJumpjet(e);auto& s=e.nativeJumpjet;
    if(usesNativeAirNavigation(e) && s.phase==Phase::Descent && distf(e.x,e.y,x,y)>=.4f){
        // Original Move_To changes descent back to ascent at 0x52f927.
        s.phase=Phase::Ascent;s.goalHeight=e.nativeIdentity.definition->jumpjet.height;
        if(s.motion.half && s.dz<0){e.nativeAltitude=std::ceil(e.nativeAltitude);s.dz=0;}
    }
    s.requested=true;s.moving=true;s.goalX=(int)(x*256);s.goalY=(int)(y*256);
    if(distf(e.x,e.y,x,y)<.4f)return true;
    const auto& p=e.nativeIdentity.definition->jumpjet;
    if(usesNativeAirNavigation(e) && p.balloon && s.phase==Phase::Travel && s.speed==0 && s.desiredSpeed>0
        && (int)e.x==s.goalX/256 && (int)e.y==s.goalY/256 && NativeJumpjetMath::nextSpeed(p,0,s.desiredSpeed)==0){
        // The original speed core can settle at zero inside the destination
        // cell (ZEP: acceleration 10, reduced desired speed 2). Finish the cell
        // mission here instead of requesting that stationary track forever.
        s.requested=s.moving=false;s.desiredSpeed=0;return true;
    }
    return false;
}
void World::updateNativeJumpjet(Ent& e,EID actor) {
    if(!usesNativeJumpjet(e))return;initializeNativeJumpjet(e);
    auto& s=e.nativeJumpjet;auto& m=s.motion;const auto& p=e.nativeIdentity.definition->jumpjet;
    const bool navigation=usesNativeAirNavigation(e);
    if(m.frame && m.lastTick==tick)return;m.lastTick=tick;
    // Siege deployment retains the existing dedicated animation/weapon
    // controller. Keep the physical height synchronized throughout that
    // transition; its exact YR deployment timing remains separate work.
    if(e.deploying() || e.deployed) {
        s.speed=s.desiredSpeed=0;m.dx=m.dy=s.dz=0;m.half=0;s.moving=false;
        const float remaining=std::clamp((float)e.deployAnim/std::max(1,g_gameRules.chopperLandingTicks),0.f,1.f);
        const float fraction=e.deployed?0.f:e.state==UState::DeployLanding?remaining:e.state==UState::DeployTakeoff?1-remaining:0;
        s.ground=ground(*this,e.x,e.y,navigation);
        const float height=s.ground+p.height*fraction;
        e.nativeAltitude=e.state==UState::DeployLanding?std::min(e.nativeAltitude,height)
            :e.state==UState::DeployTakeoff?std::max(e.nativeAltitude,height):height;
        // The deployment animation hands integer leptons back to the native
        // movement core; a fractional remainder would otherwise persist forever.
        e.nativeAltitude=(float)(int)e.nativeAltitude;
        if(navigation && e.nativeAltitude<=s.ground)e.navLayer=e.goalLayer=(uint8_t)(map.hasDeck((int)e.x,(int)e.y)?1:0);
        s.phase=e.deployed?Phase::Ground:Phase::Hover;s.goalHeight=e.deployed?0:p.height;return;
    }
    const bool target=valid(e.target) && (e.state==UState::Chasing || e.state==UState::Attacking);
    const bool keepHover=target || e.utype==UnitType::SiegeChopper;
    const bool autoHover=p.balloon || e.utype==UnitType::SiegeChopper;
    const int surface=ground(*this,e.x,e.y,navigation);
    const int cx=(int)std::floor(e.x),cy=(int)std::floor(e.y);
    const int landingLayer=navigation?nativeAirLandingLayer(cx,cy,actor):0;
    const bool canLand=navigation?landingLayer>=0:map.inBounds(cx,cy) && map.passable(cx,cy) && bldAt(cx,cy)==INVALID_EID
        && !groundUnitBlocksCell(cx,cy,actor,0);
    if(navigation && canLand && (!e.nativeAirUnload || s.phase!=Phase::Ground))e.nativeAirLandingRetry=0;
    if(navigation && !s.requested && !autoHover && !keepHover && !canLand && s.phase!=Phase::Ground)
        findNativeAirLanding(e,actor);
    if(!s.requested) {
        s.desiredSpeed=0;s.goalX=(int)(e.x*256);s.goalY=(int)(e.y*256);
        if(s.phase==Phase::Ground)s.moving=autoHover || (navigation && !canLand);
        else if(!autoHover && !keepHover && canLand){
            // Stop selects the cell center; the original descent snaps to it
            // only when it hands the actor back to ground occupation.
            s.goalX=cx*256+128;s.goalY=cy*256+128;
            s.phase=Phase::Descent;s.goalHeight=0;s.moving=false;
        }
        else {
            s.moving=false;
            if(s.phase==Phase::Descent){
                s.phase=Phase::Hover;s.goalHeight=p.height;
                if(m.half && s.dz<0){if(navigation)e.nativeAltitude=std::ceil(e.nativeAltitude);s.dz=0;}
            }
        }
        if(navigation && !canLand)s.goalHeight=p.height;
    }
    if(!m.half) {
        const int direction=(((NativeFlightMath::facing(m)+4096)/8192)&7);
        constexpr int dx[]={0,1,1,1,0,-1,-1,-1},dy[]={-1,-1,0,1,1,1,0,-1};
        int ahead=ground(*this,e.x,e.y,true);
        if(s.speed>0)ahead=std::max(ahead,ground(*this,e.x+dx[direction],e.y+dy[direction],true));
        if(navigation){auto probe=m;++probe.frame;const auto offset=NativeJumpjetMath::probeOffset(NativeFlightMath::facing(probe));
            ahead=NativeJumpjetMath::clearance(nativeAirProbeCell(cx,cy),nativeAirProbeCell(cx+offset.x,cy+offset.y),NativeJumpjetMath::nextSpeed(p,s.speed,s.desiredSpeed)>0);}
        NativeJumpjetMath::advance(p,s,(int)(e.x*256),(int)(e.y*256),(int)e.nativeAltitude,surface,ahead,keepHover || !canLand);
        if(!s.requested && (autoHover || keepHover)) {
            s.desiredSpeed=0;m.startFacing=m.desiredFacing=m.facing;m.turnFrames=0;m.turnStart=m.frame;
        }
    }
    e.x+=m.dx/512.f;e.y+=m.dy/512.f;e.nativeAltitude+=s.dz*.5f;m.half^=1;
    s.ground=ground(*this,e.x,e.y,navigation);e.dir=e.turretDir=(((m.facing+4096)/8192)-1)&7;
    if(e.nativeAltitude<=s.ground && s.phase==Phase::Descent) {
        const int layer=navigation?nativeAirLandingLayer((int)e.x,(int)e.y,actor):0;
        if(navigation && layer<0){s.phase=Phase::Hover;s.goalHeight=p.height;s.speed=s.desiredSpeed=0;
            e.nativeAltitude=s.ground+1.f;m.dx=m.dy=s.dz=0;m.half=0;return;}
        e.nativeAltitude=(float)s.ground;s.phase=Phase::Ground;s.moving=false;s.speed=s.desiredSpeed=0;
        if(navigation)e.navLayer=e.goalLayer=(uint8_t)layer;
        if(!s.requested && !target)e.state=UState::Landed;
    } else if(e.nativeAltitude>s.ground && e.state==UState::Landed)e.state=UState::Circling;
    if(navigation && e.nativeAirUnload && !e.airborne() && s.phase==Phase::Ground && tick>=e.nativeAirLandingRetry){
        const bool retry=e.nativeAirLandingRetry!=0;e.nativeAirUnload=false;e.nativeAirLandingRetry=0;const int owner=e.player;
        const auto released=releaseContainer(actor,ReleaseKind::Unload);
        if(!retry || released.released || released.rejected)emitReleaseResult(owner,actor,ReleaseKind::Unload,released);
    }
}
