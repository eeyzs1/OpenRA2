#pragma once
#include <cstdint>
#include <vector>

struct Ini;
struct NativeFlightProfile {
    int maxSpeed=0,rotation=0,slowdownDistance=500;
    bool available=false;
};
struct NativeFlightState {
    double throttle=0;
    uint64_t frame=0,turnStart=0,lastTick=0;
    uint16_t facing=0,startFacing=0,desiredFacing=0;
    int turnFrames=0,dx=0,dy=0;
    uint8_t half=0;
    bool started=false;
    bool operator==(const NativeFlightState&) const=default;
};
bool parseNativeFlightProfile(const Ini& ini,int type,NativeFlightProfile& profile);

namespace NativeFlightMath {
uint32_t tableChecksum();
std::vector<uint32_t> tableValues();
uint16_t direction(double dx,double dy);
uint16_t facing(const NativeFlightState& state);
void turn(NativeFlightState& state,uint16_t target,int rotation);
int distance(int x,int y,int targetX,int targetY);
double sine(double angle);
void planarStep(uint16_t facing,int speed,int x,int y,int& dx,int& dy);
// One original 15-Hz horizontal locomotor frame, in integer leptons. The
// caller supplies the desired facing separately, as the original aircraft AI
// does. Vertical flight and attack missions are separate from this integrator.
void advance(const NativeFlightProfile& profile,NativeFlightState& state,
             int x,int y,int targetX,int targetY,bool brake);
}
