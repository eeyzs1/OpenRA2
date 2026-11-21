#pragma once
#include <array>
#include <string_view>

namespace ApplicationSaveFormat {
struct Contract {
    int revision;
    std::string_view magic;
    bool snapshots,cellMasks,nativeActors,nativeFrames,matchSetup;
    int requiredWorldRevision;
    bool originalMap=false;
};
// GAME6 is the verified, script-free skirmish envelope preceding CAMP/LUA
// snapshots. Its importer accepts only the verified World29 combination.
inline constexpr std::array<Contract,7> supported{{
    {6,"RA2GAME6",false,false,false,false,false,29},
    {7,"RA2GAME7",true,false,false,false,false,0},
    {8,"RA2GAME8",true,true,false,false,false,0},
    {9,"RA2GAME9",true,true,true,false,false,0},
    {10,"RA2GAM10",true,true,true,true,false,0},
    {11,"RA2GAM11",true,true,true,true,true,0},
    {12,"RA2GAM12",true,true,true,true,true,0,true}
}};
inline constexpr const Contract& current=supported[5]; // ordinary/campaign envelopes keep App11
inline constexpr const Contract& originalSkirmish=supported[6];
inline const Contract* identify(const char* magic) {
    for(const auto& format:supported)if(format.magic==std::string_view(magic,8))return &format;
    return nullptr;
}
}
