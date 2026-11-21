#pragma once
#include "core/util.h"
#include <cmath>
#include <cstdint>

// Vanilla script 53/54 geometry. Inputs are source leptons (256 per cell).
// RA2 uses quantized atan/sine tables, then truncates toward zero. It does
// not clamp AISafeDistance to half the distance between the two bases.
namespace NativeTeamGeometry {
inline double angle(int y,int x) {
    constexpr float step=.024413354694843292f,halfPi=1.5707963705062866f;
    constexpr double pi=3.14159265358979323846;
    if(!x)return !y?0.0:y>0?(double)halfPi:-(double)halfPi;
    const int index=std::abs((int)(((double)(float)y/(float)x)/step));
    double a=index<4097?(double)(float)std::atan(index*(double)step):(double)halfPi;
    if(x<0)a=pi-a;
    return y<0?-a:a;
}
inline double sine(double theta,bool cosine) {
    const int raw=(int)(theta*2607.594482421875);
    int index=(raw/2)%8192;if(index<0)index+=8192;
    if(cosine)index+=2048;
    if((raw&1) && index<(cosine?10239:8191))++index;
    return (float)std::sin(index*(6.28318530717958647692/8192));
}
inline int addTruncated(int anchor,double delta) {
    const int whole=(int)delta;int result=anchor+whole;const double fraction=delta-whole;
    // Keep sub-lepton signs at exact integer anchors. A rounded double add
    // would lose them, unlike the source's directed x87 rounding.
    if(result>0 && fraction<0)--result;
    else if(result<0 && fraction>0)++result;
    return result;
}
inline Vec2i cell(Vec2i own,Vec2i enemy,int distance,bool enemyAnchor,int fallbackDirection=-1) {
    constexpr double halfPi=1.5707963267948966,scale=-10430.060040584269,radians=-9.587672516830327e-05;
    const int dx=enemyAnchor?own.x-enemy.x:enemy.x-own.x;
    const int dy=enemyAnchor?enemy.y-own.y:own.y-enemy.y;
    const int direction=fallbackDirection<0?(int)((angle(dy,dx)-halfPi)*scale):fallbackDirection;
    const double theta=((int)(int16_t)(uint16_t)direction-0x3fff)*radians;
    const auto anchor=enemyAnchor?enemy:own;
    return {addTruncated(anchor.x,sine(theta,true)*distance*256)/256,
            addTruncated(anchor.y,-sine(theta,false)*distance*256)/256};
}
}
