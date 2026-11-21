#pragma once
#include "core/util.h"
#include <cstdint>

struct NativeDriveArc {
    Vec2i corner{-1,-1};
    Vec2i outgoing;
    uint8_t incoming=0;
    uint8_t layer=0;
    float progress=0;
    bool wide=false;
    bool cubic=false;

    bool active() const { return corner.x>=0; }
    bool operator==(const NativeDriveArc&) const=default;
};

enum class ChronoReturnPhase : uint8_t { None,Waiting,Charging,Settling };
struct ChronoReturnState {
    ChronoReturnPhase phase=ChronoReturnPhase::None;
    uint8_t ticksLeft=0;
    bool preferReceiver=false;
    bool directBerth=false;
    bool operator==(const ChronoReturnState&) const=default;
};
namespace ChronoReturnTiming {
// Recheck a ready landing on the next logic tick.
inline constexpr uint8_t retry=12,ready=1;
}

namespace NativeUnloadTiming {
// The unload mission checks its gate before the stage advances on the first frame.
inline constexpr int startupTicks=2;
// One frame to commit Harvest, then 16 frames to its first dispatch in the
// stock reference run. Keep this wait deterministic in the project.
inline constexpr int departureWaitTicks=2*(1+16);
}

namespace NativeDriveMath {
// A compact curve stays inside the corner cell. A wide turn connects the
// adjacent cell centers and hands its second leg off inside the corner.
inline constexpr float cut=.375f;
inline constexpr float wideHandoff=.5f;
inline constexpr float cubicEntryControl=.33f;
inline constexpr float cubicExitControl=.24f;
inline constexpr Vec2i directions[]={{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1}};

inline bool cardinalRightAngle(Vec2i in,Vec2i out) {
    return (in.x==0)!=(out.x==0) && (in.y==0)!=(out.y==0);
}
inline Vec2f position(const NativeDriveArc& arc,float t) {
    const auto in=directions[arc.incoming];
    const float reach=arc.wide?1.f:cut;
    const float u=1-t;
    const float before=arc.cubic?u*u*(u+3*cubicEntryControl*t):reach*u*u;
    const float after=arc.cubic?t*t*(t+3*cubicExitControl*u):reach*t*t;
    return {arc.corner.x+.5f-before*in.x+after*arc.outgoing.x,
            arc.corner.y+.5f-before*in.y+after*arc.outgoing.y};
}
inline Vec2f hullFacing(const NativeDriveArc& arc,float t) {
    const auto in=directions[arc.incoming];
    const float u=1-t;
    // The center continues through the curve while the hull turns more
    // gradually at the entry and exit.
    const float from=arc.cubic?u*u*(1+u):u,to=arc.cubic?t*t*(1+t):t;
    return {from*in.x+to*arc.outgoing.x,from*in.y+to*arc.outgoing.y};
}
}
