#include "game/native_jumpjet.h"
#include "game/native_movement_math.h"
#include "core/ini.h"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace {
namespace M=NativeMovementMath;
using Phase=NativeJumpjetPhase;
bool integer(const Ini::Section* s,const char* key,int& value,int low,int high) {
    const char* p=s?s->get(key):nullptr;if(!p)return false;
    const auto end=p+strlen(p);const auto result=std::from_chars(p,end,value);
    return result.ec==std::errc{} && result.ptr==end && value>=low && value<=high;
}
bool scalar(const Ini::Section* s,const char* key,float& value) {
    const char* p=s?s->get(key):nullptr;if(!p)return false;
    double number=0;const auto end=p+strlen(p);const auto result=std::from_chars(p,end,number);
    if(result.ec!=std::errc{} || result.ptr!=end || !std::isfinite(number) || number<0 || number>1000000)return false;
    value=(float)M::storedFloat(number);return true;
}
void turn(const NativeJumpjetProfile& p,NativeJumpjetState& s,int x,int y) {
    NativeFlightMath::turn(s.motion,NativeFlightMath::direction((double)s.goalX-x,(double)s.goalY-y),std::min(127,p.turnRate)*256);
}
}

bool parseNativeJumpjetProfile(const Ini& ini,int type,NativeJumpjetProfile& p) {
    p={};const auto* row=ini.find(("Type."+std::to_string(type)).c_str());
    std::string loco=row&&row->get("OriginalLocomotor")?row->get("OriginalLocomotor"):"";
    std::transform(loco.begin(),loco.end(),loco.begin(),[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;});
    if(loco!="{92612c46-f71f-11d1-ac9f-006008055bb5}")return !row || !row->has("JumpjetVersion");
    int balloon=0,infantry=0,noWobbles=0;
    if(!integer(row,"JumpjetVersion",p.version,1,2) || !integer(row,"JumpjetSpeed",p.speed,1,1000000)
       || !integer(row,"JumpjetTurnRate",p.turnRate,1,1000000) || !integer(row,"JumpjetHeight",p.height,1,1000000)
       || !integer(row,"JumpjetDeviation",p.deviation,0,1000000) || !integer(row,"JumpjetBalloon",balloon,0,1)
       || !integer(row,"JumpjetInfantry",infantry,0,1) || !integer(row,"JumpjetNoWobbles",noWobbles,0,1)
       || !scalar(row,"JumpjetClimb",p.climb) || !scalar(row,"JumpjetCrash",p.crash)
       || !scalar(row,"JumpjetAcceleration",p.acceleration) || !scalar(row,"JumpjetWobbles",p.wobbles)
       || p.climb<=0 || p.acceleration<=0 || (!noWobbles && p.wobbles<=0))return false;
    p.balloon=balloon!=0;p.infantry=infantry!=0;p.noWobbles=noWobbles!=0;p.available=true;return true;
}

namespace NativeJumpjetMath {
NativeAirProbeOffset probeOffset(uint16_t heading) {
    constexpr int x[]={0,1,1,1,0,-1,-1,-1},y[]={-1,-1,0,1,1,1,0,-1};
    const int direction=(((heading>>12)+1)>>1)&7;return {x[direction],y[direction]};
}
int clearance(const NativeAirProbeCell& current,const NativeAirProbeCell& next,bool moving,bool active) {
    const auto height=[&](const NativeAirProbeCell& c){return c.floor+(active && c.buildingHeight>=0?c.buildingHeight:c.occupied?85:0);};
    int here=height(current)+current.bridgeHeight;if(!moving)return here;
    here+=next.bridgeHeight;const int there=height(next);
    return there>here?there:(here+there)/2;
}
double nextSpeed(const NativeJumpjetProfile& p,double current,double desired) {
    if(desired>current)current=std::min((double)p.speed,M::add(current,p.acceleration));
    if(desired<current)current=std::max(0.,M::subtract(current,M::multiply(p.acceleration,1.5)));
    return current;
}
void advance(const NativeJumpjetProfile& p,NativeJumpjetState& s,int x,int y,int z,int ground,int ahead,bool hasTarget) {
    const int beforeX=x,beforeY=y,beforeZ=z;
    auto& motion=s.motion;++motion.frame;
    s.speed=nextSpeed(p,s.speed,s.desiredSpeed);
    if((s.phase==Phase::Hover || s.phase==Phase::Travel) && !p.noWobbles)
        s.wobble=M::add(s.wobble,M::dividePositive(6.283185307179586,M::dividePositive(15,p.wobbles)));
    else s.wobble=0;
    const int wanted=(int)M::add(M::multiply(NativeFlightMath::sine(s.wobble),p.deviation),s.goalHeight);
    const bool sameCell=x/256==s.goalX/256 && y/256==s.goalY/256;
    const int surface=s.phase==Phase::Ground || s.phase==Phase::Descent || (sameCell && !p.balloon)?ground:ahead;
    const int relative=z-surface;
    if(relative<wanted)z=relative+p.climb>wanted?z+wanted-relative:(int)M::add(z,p.climb);
    else if(relative>wanted)z=std::max(ground,relative-p.climb<wanted?z+wanted-relative:(int)M::subtract(z,p.climb));
    if(!sameCell && (relative<s.goalHeight/2 || relative<s.goalHeight/4))s.speed=0;
    motion.facing=NativeFlightMath::facing(motion);
    NativeFlightMath::planarStep(motion.facing,(int)s.speed,x,y,motion.dx,motion.dy);x+=motion.dx;y+=motion.dy;
    switch(s.phase) {
    case Phase::Ground:
        if(s.moving){s.speed=s.desiredSpeed=0;s.goalHeight=p.height;s.phase=Phase::Ascent;
            motion.startFacing=motion.desiredFacing=motion.facing;motion.turnFrames=0;motion.turnStart=motion.frame;}
        break;
    case Phase::Ascent:
        if(z-ground>=s.goalHeight)s.phase=Phase::Hover;
        else if(z-ground>s.goalHeight/(!p.infantry && p.balloon?2:4)) {
            s.desiredSpeed=p.speed;turn(p,s,x,y);
            if(x/256==s.goalX/256 && y/256==s.goalY/256 && !p.balloon){s.goalHeight=z-ground;s.phase=Phase::Travel;}
        }
        break;
    case Phase::Hover:
        if(s.moving) {
            if(x==s.goalX && y==s.goalY){if(!hasTarget)s.phase=p.balloon?Phase::Hover:Phase::Descent;}
            else {turn(p,s,x,y);s.phase=Phase::Travel;}
        }
        break;
    case Phase::Travel: {
        turn(p,s,x,y);
        const auto difference=(uint16_t)(motion.desiredFacing-NativeFlightMath::facing(motion));
        const int angle=(((int)difference>>7)+1)>>1&255;
        const int distance=NativeFlightMath::distance(x,y,s.goalX,s.goalY);
        if(distance<20){x=s.goalX;y=s.goalY;s.speed=s.desiredSpeed=0;s.phase=hasTarget||p.balloon?Phase::Hover:Phase::Descent;}
        else if(distance<p.speed){s.desiredSpeed=p.speed/8;if(!hasTarget)s.goalHeight=p.height/2;}
        else if(distance<p.speed*2){s.desiredSpeed=p.speed/4;if(!hasTarget)s.goalHeight=p.height/2;
            if(angle>p.turnRate)s.desiredSpeed=std::max(1,p.speed/10);}
        else if(distance<p.speed*50/p.turnRate){s.desiredSpeed=p.speed/2;if(!hasTarget)s.goalHeight=(int)M::multiply(p.height,.75);
            if(angle>p.turnRate*5)s.desiredSpeed=std::max(1,p.speed/5);}
        else {s.desiredSpeed=p.speed;s.goalHeight=p.height;}
        if(p.balloon)s.goalHeight=p.height;
        break;
    }
    case Phase::Descent:
        s.desiredSpeed=0;s.goalHeight=0;
        if(z<=ground){x=s.goalX;y=s.goalY;s.speed=0;s.moving=false;} // Ground occupation/mission handoff belongs to World.
        break;
    }
    motion.dx=x-beforeX;motion.dy=y-beforeY;s.dz=z-beforeZ;s.ground=ground;
}
}
