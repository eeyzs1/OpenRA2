#pragma once
#include "core/ini.h"
#include <array>
#include <string>
#include <vector>

constexpr int NATIVE_CRATE_BASE=11000;
struct NativeCrateRules {
    bool available=false;
    float radius=0;
    int soloMoney=0;
    std::array<float,19> data{};
    bool randomCrates=false,freeMCV=true;
    int minimum=1,maximum=255;
    float regenMinutes=3;
    std::array<int,19> shares{};
    std::array<bool,19> water{};
    std::vector<std::string> vehicles,baseUnits,constructionYards,harvesters;
    std::string unitCrateType;
};
struct NativeCrateBonus {float armor=1,firepower=1,speed=1;};
bool parseNativeCrateRules(const Ini& source,NativeCrateRules& out);
