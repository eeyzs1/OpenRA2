#pragma once
#include <bit>
#include <cstdint>

struct NativeSourceCoordinates {int x=0,y=0,z=0;};

inline NativeSourceCoordinates nativeSourceBuildingCoordinates(NativeSourceCoordinates location,int width,int height) {
    // Original Building.GetCoords uses base foundation dimensions without bib.
    // Preserve 32-bit wrap, including the original zero-size foundation entry.
    location.x=std::bit_cast<int32_t>((uint32_t)location.x+((uint32_t)width<<7)-128u);
    location.y=std::bit_cast<int32_t>((uint32_t)location.y+((uint32_t)height<<7)-128u);
    return location;
}
