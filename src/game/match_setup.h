#pragma once
#include "game/data.h"
#include <array>
#include <cstdio>

enum class RulesChoice : uint8_t { Fusion,RA2,YR,COUNT };
struct MatchSetup {
    RulesChoice rules=RulesChoice::Fusion;
    std::string originalMap;
    uint64_t seed=0;
    int country=(int)Country::China,color=0,ai=2,money=10000,size=96,terrain=0,mode=0;
    bool reference=false,crates=true,alliance=false,sharedVision=true,shortGame=false,superweapons=true,repacks=true;
    std::array<int,7> aiColors{1,2,3,4,5,6,7};
    std::array<int,7> aiCountries{(int)Country::COUNT,(int)Country::COUNT,(int)Country::COUNT,(int)Country::COUNT,(int)Country::COUNT,(int)Country::COUNT,(int)Country::COUNT};
    std::array<int,7> aiDifficulty{1,1,1,1,1,1,1},aiPersonality{};
};
bool validMatchSetup(const MatchSetup& setup);
bool writeMatchSetup(FILE* file,const std::optional<MatchSetup>& setup);
bool readMatchSetup(FILE* file,std::optional<MatchSetup>& setup,bool originalMap=false);
