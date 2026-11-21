#pragma once
#include <array>

// Source content, simulation state and save blocks have separate revisions.
// Keep historical global-AI behavior in one immutable contract. Local mission
// AI and the installed ordinary Scene43 profiles do not use a global-AI contract.
struct NativeGlobalAIContract {
    int sceneVersion, aiVersion, eventVersion, worldVersion, stateVersion;
    bool houseControl, nearbyGeometry, sourceCoordinates;
    int queryPadding;
};
inline constexpr std::array<NativeGlobalAIContract,4> nativeGlobalAIContracts{{
    {38,2,5,61,1,false,false,false,0},
    {39,3,6,62,2,true,false,false,0},
    {40,4,7,62,2,true,true,false,0},
    {41,5,8,62,2,true,true,true,32}
}};
inline constexpr const NativeGlobalAIContract* nativeGlobalAIContractForScene(int version) {
    for(const auto& contract:nativeGlobalAIContracts)if(contract.sceneVersion==version)return &contract;
    return nullptr;
}
inline constexpr const NativeGlobalAIContract* nativeGlobalAIContractForRules(int version) {
    for(const auto& contract:nativeGlobalAIContracts)if(contract.aiVersion==version)return &contract;
    return nullptr;
}
inline constexpr const NativeGlobalAIContract* nativeGlobalAIContractForEvents(int version) {
    for(const auto& contract:nativeGlobalAIContracts)if(contract.eventVersion==version)return &contract;
    return nullptr;
}
inline constexpr bool nativeGlobalAIFeaturesMatch(const NativeGlobalAIContract* contract,
    bool house,bool nearby,bool coordinates) {
    return house==(contract && contract->houseControl)
        && nearby==(contract && contract->nearbyGeometry)
        && coordinates==(contract && contract->sourceCoordinates);
}
