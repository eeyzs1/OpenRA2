#pragma once
#include "core/ini.h"
#include <vector>

struct NativeAircraftDefinition {
    int launcher=-1,aircraft=-1,capacity=0;
    int regenerate=0,reload=0,launchDelay=20; // Original 15-Hz frames.
    int weaponSlot=0;
    bool underwaterOnly=false,noSpawnAlt=false;
};
bool parseNativeAircraftDefinitions(const Ini& source,int types,std::vector<NativeAircraftDefinition>& destination);
