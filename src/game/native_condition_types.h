#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct NativeConditionState {
    bool active=false;
    // House JustBuilt*: last factory release, not an ever-built counter.
    std::vector<std::array<int,4>> lastBuilt; // structure, vehicle, infantry, aircraft
    std::array<uint8_t,50> locals{};
    std::map<std::string,uint8_t> discoveredHumans;
    // Source identities survive the passenger -> live entity transition.
    std::map<int,std::vector<std::string>> unloading;
};
