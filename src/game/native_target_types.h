#pragma once
#include "core/ini.h"
#include <array>

struct NativeTargetWeapon {
    bool present=false,air=false,ground=true;
    float range=0;
    std::array<float,11> verses{};
};

// Immutable source data for the team leader's whole-map target scan.
// ThreatPosed is deliberately absent: it is not the target rating.
struct NativeTargetProfile {
    bool available=false,legal=true,invisible=false,storage=false,factory=false,undeploys=false;
    bool detectDisguise=false,aiDetectDisguise=false;
    float specialThreat=0;
    // My effectiveness, target effectiveness, special threat, strength, distance.
    std::array<float,5> coefficients{};
    std::array<NativeTargetWeapon,2> weapons;
};

bool parseNativeTargetProfile(const Ini::Section* section,NativeTargetProfile& profile);
