#pragma once
#include <algorithm>
#include <array>

// Existing on-disk magic bytes are not a decimal encoding of the revision.
// This registry describes supported files; it does not upgrade their behavior.
namespace WorldSaveFormat {
struct Contract {int revision;std::array<char,8> magic;};
inline constexpr int currentRevision=60;
inline constexpr int groundRevision=63;
inline constexpr int travelRevision=64;
inline constexpr int dockRedirectRevision=65;
inline constexpr int dockApproachRevision=66;
inline constexpr int forwardDockRevision=67;
inline constexpr int wideDriveRevision=68;
inline constexpr int chronoBerthRevision=69;
inline constexpr int chronoYieldRevision=70;
inline constexpr int unloadStartupRevision=71;
inline constexpr int departureWaitRevision=72;
inline constexpr int driveExitRevision=73;
inline constexpr int cubicWideDriveRevision=74;
inline constexpr int cargoOwnerRevision=75;
inline constexpr int transportUnloadRevision=76;
inline constexpr int buildingSaleRevision=77;
// World75 wraps an explicit existing layout instead of changing its travel tags.
inline constexpr bool cargoOwnerBaseRevision(int revision) {
    return revision>=travelRevision && revision<=cubicWideDriveRevision;
}
inline constexpr bool requiresTravelAndDockRedirects(int revision) {
    return revision==forwardDockRevision || revision==wideDriveRevision
        || revision==chronoBerthRevision || revision==chronoYieldRevision || revision==unloadStartupRevision
        || revision==departureWaitRevision || revision==driveExitRevision || revision==cubicWideDriveRevision;
}
inline constexpr int ordinaryRevision(int productionVersion) {
    return productionVersion>=9?travelRevision:productionVersion>=8?groundRevision:currentRevision;
}
inline constexpr std::array<Contract,72> supported{{
    {6,{'R','A','2','W','R','L','D','6'}},
    {7,{'R','A','2','W','R','L','D','7'}},
    {8,{'R','A','2','W','R','L','D','8'}},
    {9,{'R','A','2','W','R','L','D','9'}},
    {10,{'R','A','2','W','R','L','D','A'}},
    {11,{'R','A','2','W','R','L','D','B'}},
    {12,{'R','A','2','W','R','L','D','C'}},
    {13,{'R','A','2','W','R','L','D','D'}},
    {14,{'R','A','2','W','R','L','D','E'}},
    {15,{'R','A','2','W','R','L','D','F'}},
    {16,{'R','A','2','W','R','L','D','G'}},
    {17,{'R','A','2','W','R','L','D','H'}},
    {18,{'R','A','2','W','R','L','D','I'}},
    {19,{'R','A','2','W','R','L','D','J'}},
    {20,{'R','A','2','W','R','L','D','K'}},
    {21,{'R','A','2','W','R','L','D','L'}},
    {22,{'R','A','2','W','R','L','D','M'}},
    {23,{'R','A','2','W','R','L','D','N'}},
    {24,{'R','A','2','W','R','L','D','O'}},
    {25,{'R','A','2','W','R','L','D','P'}},
    {26,{'R','A','2','W','R','L','D','Q'}},
    {27,{'R','A','2','W','R','L','D','R'}},
    {28,{'R','A','2','W','R','L','D','S'}},
    {29,{'R','A','2','W','R','L','D','T'}},
    {30,{'R','A','2','W','R','L','D','U'}},
    {31,{'R','A','2','W','R','L','D','V'}},
    {32,{'R','A','2','W','R','L','D','W'}},
    {33,{'R','A','2','W','R','L','D','X'}},
    {34,{'R','A','2','W','R','L','D','Y'}},
    {35,{'R','A','2','W','R','L','D','Z'}},
    {36,{'R','A','2','W','L','D','3','6'}},
    {37,{'R','A','2','W','L','D','3','7'}},
    {38,{'R','A','2','W','L','D','3','8'}},
    {39,{'R','A','2','W','L','D','3','9'}},
    {40,{'R','A','2','W','L','D','4','0'}},
    {41,{'R','A','2','W','L','D','4','1'}},
    {42,{'R','A','2','W','L','D','4','2'}},
    {43,{'R','A','2','W','L','D','4','3'}},
    {44,{'R','A','2','W','L','D','4','4'}},
    {45,{'R','A','2','W','L','D','4','5'}},
    {46,{'R','A','2','W','L','D','4','6'}},
    {47,{'R','A','2','W','L','D','4','7'}},
    {48,{'R','A','2','W','L','D','4','8'}},
    {49,{'R','A','2','W','L','D','4','9'}},
    {50,{'R','A','2','W','L','D','5','0'}},
    {51,{'R','A','2','W','L','D','5','1'}},
    {52,{'R','A','2','W','L','D','5','2'}},
    {53,{'R','A','2','W','L','D','5','3'}},
    {54,{'R','A','2','W','L','D','5','4'}},
    {55,{'R','A','2','W','L','D','5','5'}},
    {56,{'R','A','2','W','L','D','5','6'}},
    {57,{'R','A','2','W','L','D','5','7'}},
    {58,{'R','A','2','W','L','D','5','8'}},
    {59,{'R','A','2','W','L','D','5','9'}},
    {60,{'R','A','2','W','L','D','6','0'}},
    {61,{'R','A','2','W','L','D','6','1'}},
    {62,{'R','A','2','W','L','D','6','2'}},
    {63,{'R','A','2','W','L','D','6','3'}},
    {64,{'R','A','2','W','L','D','6','4'}},
    {65,{'R','A','2','W','L','D','6','5'}},
    {66,{'R','A','2','W','L','D','6','6'}},
    {67,{'R','A','2','W','L','D','6','7'}},
    {68,{'R','A','2','W','L','D','6','8'}},
    {69,{'R','A','2','W','L','D','6','9'}},
    {70,{'R','A','2','W','L','D','7','0'}},
    {71,{'R','A','2','W','L','D','7','1'}},
    {72,{'R','A','2','W','L','D','7','2'}},
    {73,{'R','A','2','W','L','D','7','3'}},
    {74,{'R','A','2','W','L','D','7','4'}},
    {75,{'R','A','2','W','L','D','7','5'}},
    {76,{'R','A','2','W','L','D','7','6'}},
    {77,{'R','A','2','W','L','D','7','7'}},
}};
inline constexpr const Contract* forRevision(int revision) {
    for(const auto& format:supported)if(format.revision==revision)return &format;
    return nullptr;
}
inline const Contract* identify(const char* magic) {
    for(const auto& format:supported)if(std::equal(format.magic.begin(),format.magic.end(),magic))return &format;
    return nullptr;
}
}
