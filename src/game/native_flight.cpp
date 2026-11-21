#include "game/native_flight.h"
#include "game/native_movement_math.h"
#include "core/ini.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>

namespace {
namespace M=NativeMovementMath;
constexpr double pi=3.141592653589793;
constexpr float atanStep=0.024413354694843292f,trigScale=2607.594482421875f;
float single(double value){return (float)M::storedFloat(value);}
struct Tables {
    std::array<float,10240> sine{};
    std::array<float,4097> tangent{};
    std::array<uint32_t,16384> root{};
    Tables() {
        for(size_t i=0;i<sine.size();++i)sine[i]=single(std::sin(i*pi/4096));
        // The original table generator's extended-precision pi leaves these
        // two nonzero axis entries. Retain their numerical values as well.
        sine[4096]=std::bit_cast<float>(0x250d3000u);sine[8192]=std::bit_cast<float>(0xa58d3000u);
        float ratio=0;
        for(auto& v:tangent){v=single(std::atan((double)ratio));ratio=single((double)ratio+atanStep);}
        for(size_t i=0;i<root.size();++i) {
            const double value=i<8192?1+i/8192.:2+(i-8192)/4096.;
            root[i]=std::bit_cast<uint32_t>(single(std::sqrt(value)))-(127u<<23);
        }
    }
};
const Tables& tables(){static const Tables result;return result;}
double root(double value) {
    if(value==0)return 0;
    const uint32_t bits=std::bit_cast<uint32_t>(single(value));
    const int exponent=(int)(bits>>23)-127;
    const auto index=((bits&0x7fffff)|((exponent&1)?0x800000:0))>>10;
    const int half=exponent>=0?exponent/2:(exponent-1)/2;
    return std::bit_cast<float>(tables().root[index]+((uint32_t)(half+127)<<23));
}
double trig(double value,bool cosine) {
    const int sample=(int)M::multiply(value,trigScale);
    int index=(sample/2)%8192;if(index<0)index+=8192;
    if(cosine)index+=2048;
    if((sample&1) && index<(cosine?10239:8191))++index;
    return tables().sine[index];
}
double divide(double a,double b){return M::corrected(a/b,std::fma(-a/b,b,a));}
double originalAtan2(double y,double x) {
    x=single(x);y=single(y);
    if(x==0)return y==0?0:y>0?(double)1.5707963705062866f:(double)-1.5707963705062866f;
    const double index=std::abs(divide(divide(y,x),atanStep));
    double value=index>=4097?(double)1.5707963705062866f:tables().tangent[(size_t)index];
    if(x<0)value=M::subtract(pi,value);
    return y<0?-value:value;
}
bool integer(const Ini::Section* s,const char* key,int& value,int high) {
    const char* p=s?s->get(key):nullptr;if(!p)return false;
    const auto r=std::from_chars(p,p+strlen(p),value);
    return r.ec==std::errc{} && !*r.ptr && value>=0 && value<=high;
}
}

bool parseNativeFlightProfile(const Ini& ini,int type,NativeFlightProfile& result) {
    result={};const auto* s=ini.find(("Type."+std::to_string(type)).c_str());
    const char* p=s?s->get("OriginalLocomotor"):nullptr;
    std::string loco=p?p:"";
    std::transform(loco.begin(),loco.end(),loco.begin(),[](unsigned char c){return c>='A'&&c<='Z'?c+32:c;});
    if(loco!="{4a582746-9839-11d1-b709-00a024ddafd1}")return !s || !s->has("FlightVersion");
    int speed=0,rotation=0,version=0;
    if(!integer(s,"FlightVersion",version,1) || version!=1 || !integer(s,"OriginalSpeed",speed,1000000)
       || !integer(s,"FlightROT",rotation,1000000) || !integer(s,"FlightSlowdownDistance",result.slowdownDistance,1000000))return false;
    result.maxSpeed=std::min(255,std::min(speed,100)*256/100);
    result.rotation=std::min(rotation,127)*256;result.available=true;return true;
}

namespace NativeFlightMath {
int distance(int x,int y,int tx,int ty) {
    const double dx=(double)tx-x,dy=(double)ty-y;
    return (int)root(M::add(M::multiply(dx,dx),M::multiply(dy,dy)));
}
double sine(double angle){return trig(angle,false);}
void planarStep(uint16_t facing,int speed,int x,int y,int& dx,int& dy) {
    const double angle=M::multiply((int16_t)facing-16383,-9.587672516830327e-05);
    dx=(int)M::add(x,M::multiply(trig(angle,true),speed))-x;
    dy=(int)M::subtract(y,M::multiply(trig(angle,false),speed))-y;
}
std::vector<uint32_t> tableValues() {
    std::vector<uint32_t> result;
    for(auto v:tables().sine)result.push_back(std::bit_cast<uint32_t>(v));
    for(auto v:tables().tangent)result.push_back(std::bit_cast<uint32_t>(v));
    for(auto v:tables().root)result.push_back(v);return result;
}
uint32_t tableChecksum() {
    uint32_t h=2166136261u;
    const auto item=[&](uint32_t bits){for(int i=0;i<4;++i){h=(h^(uint8_t)bits)*16777619u;bits>>=8;}};
    for(auto v:tables().sine)item(std::bit_cast<uint32_t>(v));
    for(auto v:tables().tangent)item(std::bit_cast<uint32_t>(v));
    for(auto v:tables().root)item(v);return h;
}
uint16_t direction(double dx,double dy) {
    return (uint16_t)(int)M::multiply(M::subtract(originalAtan2(-dy,dx),pi/2),-10430.060040584269);
}
uint16_t facing(const NativeFlightState& s) {
    const auto elapsed=s.frame-s.turnStart;
    if(!s.turnFrames || elapsed>=(uint64_t)s.turnFrames)return s.desiredFacing;
    const int delta=(int16_t)(s.desiredFacing-s.startFacing);
    return (uint16_t)(s.desiredFacing-(delta/s.turnFrames)*(s.turnFrames-(int)elapsed));
}
void turn(NativeFlightState& s,uint16_t target,int rotation) {
    if(target==s.desiredFacing)return;
    s.startFacing=facing(s);s.desiredFacing=target;s.turnStart=s.frame;
    s.turnFrames=rotation?std::abs((int16_t)(target-s.startFacing))/rotation:0;
}
void advance(const NativeFlightProfile& p,NativeFlightState& s,int x,int y,int tx,int ty,bool brake) {
    s.facing=facing(s);
    const int speed=(int)M::multiply(p.maxSpeed,s.throttle);
    planarStep(s.facing,speed,x,y,s.dx,s.dy);
    const double dx=(double)tx-x-s.dx,dy=(double)ty-y-s.dy;
    const int distance=(int)root(M::add(M::multiply(dx,dx),M::multiply(dy,dy)));
    double target=brake && p.slowdownDistance?std::min(1.,divide(distance,p.slowdownDistance)):1;
    if(target<.1) {
        if(distance>85)target=.1;
        else {target=0;s.throttle=M::multiply(s.throttle,.5);}
    }
    if(!target && !s.throttle && distance>0)s.throttle=.05;
    if(s.throttle<target)s.throttle=std::min(target,M::add(s.throttle,.1));
    else if(s.throttle>target)s.throttle=std::max(target,M::subtract(s.throttle,.1));
}
}
