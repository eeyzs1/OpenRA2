#pragma once
#include <cmath>

// Original current-speed arithmetic uses x87 53-bit precision with truncation
// (control word 0x0e7f). Correct a nearest-rounded result using its residual,
// without changing the process/thread rounding mode used by other subsystems.
namespace NativeMovementMath {
inline double corrected(double rounded,double residual) {
    return (rounded>0 && residual<0) || (rounded<0 && residual>0)?std::nextafter(rounded,0.):rounded;
}
inline double multiply(double a,double b) {
    const double value=a*b;
    return corrected(value,std::fma(a,b,-value));
}
inline double add(double a,double b) {
    const double value=a+b,part=value-a;
    return corrected(value,(a-(value-part))+(b-part));
}
inline double subtract(double a,double b){return add(a,-b);}
inline double dividePositive(double a,double b) {
    const double value=a/b;
    return corrected(value,std::fma(-value,b,a));
}
inline double storedFloat(double value) {
    float result=(float)value;
    if((result>0 && result>value) || (result<0 && result<value))result=std::nextafter(result,0.f);
    return result;
}
}
