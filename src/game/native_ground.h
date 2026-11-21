#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Local GAME.EXE FacingClass (4b8a30/4b8be0). Division precedes
// multiplication in Current; retaining that order preserves retarget traces.
struct NativeFacingState {
    uint16_t start=0,target=0;
    uint64_t since=0;
    uint32_t duration=0;
    bool operator==(const NativeFacingState&) const=default;
};
namespace NativeGroundMath {
inline int delta(uint16_t from,uint16_t to) {const unsigned d=(unsigned(to)-from)&65535;return d<32768?(int)d:(int)d-65536;}
inline uint16_t current(const NativeFacingState& s,uint64_t frame) {
    const uint64_t elapsed=frame>=s.since?frame-s.since:0;
    if(!s.duration || elapsed>=s.duration)return s.target;
    return uint16_t(int(s.target)-(delta(s.start,s.target)/(int)s.duration)*(int)(s.duration-elapsed));
}
inline void desire(NativeFacingState& s,uint16_t target,int rate,uint64_t frame) {
    if(s.target==target)return;
    s.start=current(s,frame);s.target=target;s.since=frame;
    s.duration=rate?std::abs(delta(s.start,target))/(std::clamp(rate,1,127)*256):0;
}
inline uint16_t fromDir(int dir) {return uint16_t((dir+1)*8192);}
inline int dir(uint16_t raw) {return (((unsigned(raw)+4096)/8192)-1)&7;}
inline int facing32(uint16_t raw) {return (((unsigned(raw)+1024)/2048)-4)&31;}
inline uint16_t vector(float x,float y) {return uint16_t((int)std::lround(std::atan2(double(x),-double(y))*(65536./6.2831853071795864769)));}
}
struct NativeGroundState {
    bool initialized=false;
    NativeFacingState hull,turret;
    bool operator==(const NativeGroundState&) const=default;
};
enum class NativeDockPhase : uint8_t { None,Approach,Align,Enter,Unload,Exit,ForwardEntry,DriveExit };
