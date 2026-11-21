#pragma once
#include "game/native_ai_contract.h"
#include <algorithm>

// Scene42 adds original skirmish map context to the ordinary Scene37 rules.
// Its AI remains ordinary; the frozen Scene38-41 global-AI contracts are separate.
inline constexpr int nativeSkirmishMapSceneVersion=42;
inline constexpr int nativeSkirmishRulesSemantics=37;
// Scene43 adds source ore/gem credit values; other ordinary features keep37.
inline constexpr int nativeEconomySceneVersion=43;
// Scene44 calibrates the RA2 harvester pickup core; other features keep37.
inline constexpr int nativeHarvestSceneVersion=44;
inline constexpr int nativeGroundSceneVersion=45;
inline constexpr int nativeTravelSceneVersion=46;
inline constexpr bool nativeOrdinaryRulesScene(int version) {
    return version==nativeSkirmishRulesSemantics || version==nativeEconomySceneVersion || version==nativeHarvestSceneVersion
        || version==nativeGroundSceneVersion || version==nativeTravelSceneVersion;
}
inline constexpr int maximumNativeSceneVersion=std::max(nativeTravelSceneVersion,nativeGlobalAIContracts.back().sceneVersion);
