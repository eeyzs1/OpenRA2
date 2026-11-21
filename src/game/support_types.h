#pragma once
#include <array>
#include <cstdint>
#include <string>

enum class SupportType : uint8_t { Paradrop, AmericanParadrop, SpyPlane, PsychicReveal, COUNT };
constexpr int SUPPORT_COUNT=(int)SupportType::COUNT;
struct SupportCharge { int ticks=0;bool ready=false; };
struct SupportRule {
    bool available=false,powered=false;
    int rechargeTicks=0;
    std::string icon;
    std::array<std::string,3> infantry; // Allied, Soviet and Yuri payloads.
    std::array<int,3> count{};
};

struct SupportDeliveryRules {
    int flightLevel=1500,parachuteFall=-3,dropRadius=1024;
    int spyRadius=6,spyRange=20;
};
