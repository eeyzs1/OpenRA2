#include "game/world.h"
#include <array>
#include <cmath>

namespace {
uint16_t heading(const NativeDriveArc& arc,float progress) {
    const auto facing=NativeDriveMath::hullFacing(arc,progress);
    return NativeGroundMath::vector(facing.x,facing.y);
}

double cubicDistance(float progress) {
    constexpr int segments=128;
    static const auto distance=[] {
        const auto speed=[](double t) {
            constexpr double a=NativeDriveMath::cubicEntryControl,b=NativeDriveMath::cubicExitControl;
            const double u=1-t;
            const double dx=3*((1-a)*u*u+2*a*t*u),dy=3*((1-b)*t*t+2*b*t*u);
            return std::hypot(dx,dy);
        };
        // Integrate the authored centerline once. Each interval has positive
        // length so the distance search can advance exactly to the handoff.
        std::array<double,segments+1> cumulative{};
        double start=speed(0);
        for(int i=0;i<segments;++i) {
            const double center=speed((i+.5)/segments),end=speed((i+1.)/segments);
            cumulative[i+1]=cumulative[i]+(start+4*center+end)/(6*segments);
            start=end;
        }
        return cumulative;
    }();
    const double sample=progress*segments;
    const int i=std::min((int)sample,segments-1);
    return std::lerp(distance[i],distance[i+1],sample-i);
}
}

bool World::usesDriveArc(const Ent& e) const {
    if(!nativeTravelEnabled() || !usesNativeGround(e) || !nativeMovementRules.available || unitDef(e).canHarvet())return false;
    const auto& type=*e.nativeIdentity.definition;
    const auto& drive=type.movement;
    return drive.locomotor==NativeLocomotor::Drive && drive.speedType==NativeSpeedType::Track
        && drive.maxSpeed>0 && type.production.rotationRate>0;
}

bool World::driveArcMatchesRoute(const Ent& e) const {
    const auto& arc=e.driveArc;
    if(!arc.active() || !usesDriveArc(e) || (arc.cubic && !arc.wide) || e.pathIdx<0
        || !map.inBounds(arc.corner.x,arc.corner.y) || arc.incoming>=8 || arc.layer!=e.navLayer
        || !std::isfinite(arc.progress) || arc.progress<0 || arc.progress>=1)return false;
    const int cornerIndex=e.pathIdx-(arc.wide && arc.progress>=NativeDriveMath::wideHandoff);
    if(cornerIndex<0 || (size_t)cornerIndex+1>=e.path.size())return false;
    const auto next=e.path[cornerIndex+1];
    if(!map.inBounds(next.x,next.y) || e.path[cornerIndex]!=arc.corner || arc.outgoing!=Vec2i{next.x-arc.corner.x,next.y-arc.corner.y}
        || std::abs(arc.outgoing.x)>1 || std::abs(arc.outgoing.y)>1 || arc.outgoing==Vec2i{})return false;
    if(arc.wide) {
        const auto in=NativeDriveMath::directions[arc.incoming];
        if(!NativeDriveMath::cardinalRightAngle(in,arc.outgoing) || !map.inBounds(arc.corner.x-in.x,arc.corner.y-in.y))return false;
    }
    const int turn=std::abs(NativeGroundMath::delta(heading(arc,0),heading(arc,1)));
    if(turn<8192 || turn>24576)return false;
    const auto at=NativeDriveMath::position(arc,arc.progress);
    return std::abs(e.x-at.x)<.001f && std::abs(e.y-at.y)<.001f;
}

NativeDriveArc World::planDriveArc(const Ent& e,Vec2i corner,int layer,EID id) const {
    if(!usesDriveArc(e) || e.navLayer!=layer || e.pathIdx+1>=(int)e.path.size())return {};
    const auto next=e.path[e.pathIdx+1];
    const Vec2i out{next.x-corner.x,next.y-corner.y};
    if(std::abs(out.x)>1 || std::abs(out.y)>1 || out==Vec2i{} || !map.inBounds(next.x,next.y))return {};
    const float dx=corner.x+.5f-e.x,dy=corner.y+.5f-e.y;
    const float length=std::max(std::abs(dx),std::abs(dy));
    if(length<NativeDriveMath::cut-.0001f || length>1.001f)return {};
    const int incoming=NativeGroundMath::dir(NativeGroundMath::vector(dx,dy));
    const auto in=NativeDriveMath::directions[incoming];
    if(std::abs(dx-length*in.x)>.001f || std::abs(dy-length*in.y)>.001f)return {};
    NativeDriveArc arc{corner,out,(uint8_t)incoming,(uint8_t)layer,0};
    const int turn=std::abs(NativeGroundMath::delta(heading(arc,0),heading(arc,1)));
    if(turn<8192 || turn>24576)return {};
    const int height=map.surfaceHeight(corner.x,corner.y,layer);
    if(height!=map.surfaceHeight((int)e.x,(int)e.y,e.navLayer) || height!=map.surfaceHeight(next.x,next.y,layer)
        || movementStep(e,corner.x,corner.y,layer,next.x,next.y)!=layer || bldBlocked(next.x,next.y,layer))return {};
    // Each half uses the ordinary admission for a single path step. Start a
    // wide turn only if there is already room for the second half.
    if(id!=INVALID_EID && dx==in.x && dy==in.y && NativeDriveMath::cardinalRightAngle(in,out)
        && movementStep(e,(int)e.x,(int)e.y,e.navLayer,corner.x,corner.y)==layer
        && !waitingForNativeGate(e,next) && !cellHardBlockedForMove(next.x,next.y,id,layer)) {
        const EID reserved=movementReservations.size()==movementSlotCount()
            ?movementReservations[movementSlot(next.x,next.y,layer)]:INVALID_EID;
        arc.wide=reserved==INVALID_EID || reserved==id;
        arc.cubic=arc.wide;
    }
    return arc;
}

bool World::advanceDriveArc(Ent& e,bool aimTurret) {
    auto& arc=e.driveArc;
    const auto startHeading=heading(arc,arc.progress);
    const int alignAllowance=e.nativeIdentity.definition->production.rotationRate*128;
    if(std::abs(NativeGroundMath::delta(nativeHullHeading(e,tick),startHeading))>alignAllowance) {
        desireNativeHull(e,startHeading);
        return false;
    }
    const auto node=e.path[e.pathIdx];
    prepareNativeMovement(e,node.x,node.y,arc.layer,e.path.back());
    auto& credit=e.nativeMovement.credit;
    if(credit<=0)return false;

    const auto in = NativeDriveMath::directions[arc.incoming];
    const int dx = arc.outgoing.x - in.x, dy = arc.outgoing.y - in.y;
    const double change = std::hypot((double)dx, (double)dy);
    constexpr double diagonalLength = 1.414213562373095;
    constexpr double cardinalRate = 256. * 7 / 11;
    constexpr double diagonalRate = 256. * 7 / (8 * diagonalLength);
    const auto perUnit = [&](Vec2i step) { return step.x && step.y ? diagonalRate : cardinalRate; };
    const double entryRate = perUnit(in), exitRate = perUnit(arc.outgoing);
    // These bends have non-collinear tangents. Integrate the length of
    // in + t * (out - in), weighted by the adjacent straight tracks' rates.
    const double along = (in.x * dx + in.y * dy) / change;
    const double across = std::abs(in.x * dy - in.y * dx) / change;
    const double rateSlope = (exitRate - entryRate) / change;
    const double baseRate = entryRate - rateSlope * along;
    const double reach=arc.wide?1.:NativeDriveMath::cut;
    const auto creditIntegral = [&](float progress) {
        if(arc.cubic)return cubicDistance(progress)*cardinalRate;
        const double x = along + progress * change;
        const double length = std::sqrt(x * x + across * across);
        const double distance = x * length + across * across * std::asinh(x / across);
        const double weighted = baseRate * distance + (2. / 3) * rateSlope * length * length * length;
        return weighted * reach / change;
    };
    const double origin = creditIntegral(arc.progress);
    const auto fits = [&](float progress) {
        return creditIntegral(progress) - origin <= credit;
    };
    constexpr double a=NativeDriveMath::cubicEntryControl,b=NativeDriveMath::cubicExitControl;
    // The cubic derivative's sums along both axes are a weighted average of these three values.
    constexpr double cubicMinRate=3*std::min({1-a,a+b,1-b})/diagonalLength*cardinalRate;
    const double minRate = arc.cubic?cubicMinRate:2 * reach * across * std::min(entryRate, exitRate);
    const float limit=arc.wide && arc.progress<NativeDriveMath::wideHandoff?NativeDriveMath::wideHandoff:1.f;
    float next = (float)std::min((double)limit, arc.progress + credit / minRate);
    if (!fits(next)) {
        float low = arc.progress, high = next;
        for (int i = 0; i < 16; ++i) {
            const float middle = (low + high) * .5f;
            if (fits(middle)) low = middle;
            else high = middle;
        }
        next = low;
    }
    if(next<=arc.progress)return false;
    credit = next == limit ? std::max(0., credit - (creditIntegral(next) - origin)) : 0;
    arc.progress=next;
    const auto facing=heading(arc,next);
    e.nativeGround.hull={facing,facing,tick/2,0};
    updateNativeGroundFacing(e);
    if(!aimTurret)desireNativeTurret(e,facing);
    return true;
}
