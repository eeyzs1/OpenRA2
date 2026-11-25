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
// Scene47 carries the source Crusher/Crushable type contract.
inline constexpr int nativeCrushingSceneVersion=47;
// Scene48 resolves source crushing only after a physical movement contact.
inline constexpr int nativeCrushContactSceneVersion=48;
// Scene49 lets an admitted infantry path leave its already shared origin cell.
inline constexpr int nativeInfantryDepartureSceneVersion=49;
// Scene50 enables source pre-entry crusher warnings and infantry scattering.
inline constexpr int nativeCrushWarningSceneVersion=50;
// Scene51 separates forced allied-infantry blockage scatter from that warning.
inline constexpr int nativeDriveBlockageSceneVersion=51;
// The same unpublished cohort recognizes exits past an occupied dwell prefix.
inline constexpr int nativeReciprocalDwellSceneVersion=51;
// The same unpublished cohort retains physical observation under gap shroud.
inline constexpr int nativeLocalGapSightSceneVersion=51;
// Scene52 calibrates the vanilla RA2 flat-cell gap footprint.
inline constexpr int nativeGapAreaSceneVersion=52;
// Scene53 resolves the combined projected gap field at ground-cell height.
inline constexpr int nativeGapGroundHeightSceneVersion=53;
// Scene54 includes the source slope rise in ground-cell gap queries.
inline constexpr int nativeGapGroundSlopeSceneVersion=54;
// Scene55 admits source ground passengers by size in ordinary unit transports.
inline constexpr int nativeSizedTransportSceneVersion=55;
// Scene56 preserves ordinary cargo owners when their transport changes house.
inline constexpr int nativeCargoOwnerSceneVersion=56;
// Scene57 schedules ground transport unloading one passenger per dispatch.
inline constexpr int nativeTransportUnloadSceneVersion=57;
// Scene58 calibrates ground transport exit/facing and adjacent release order.
inline constexpr int nativeTransportExitSceneVersion=58;
// Scene59 admits passengers from a house allied with the carrier owner.
inline constexpr int nativeAlliedBoardingSceneVersion=59;
// Scene60 transfers ground passengers only after entering the carrier XY cell.
inline constexpr int nativeTransportBoardingSceneVersion=60;
// The same unpublished cohort preserves age across isolated-air survey goals.
inline constexpr int nativeIsolatedSurveyAgeSceneVersion=60;
// Scene61 calibrates source building wrench repair cadence, step and charge.
inline constexpr int nativeBuildingRepairSceneVersion=61;
// Scene62 pays source building sale proceeds only after demolition completes.
inline constexpr int nativeBuildingSaleSceneVersion=62;
// Scene63 keeps a construction yard cash sale distinct from MCV repacking.
inline constexpr int nativeConstructionYardSaleSceneVersion=63;
// Scene64 creates source building sale survivors and preserves capture history.
inline constexpr int nativeSaleSurvivorsSceneVersion=64;
// Scene65 restores the primary nearby-cell search before adjacent Scatter.
inline constexpr int nativeNearbyScatterSceneVersion=65;
// The same unpublished cohort avoids unavailable automatic AI repair.
inline constexpr int nativeAutomaticServiceSceneVersion=65;
// Scene66 calibrates base-height projection tiers in Nearby selection.
inline constexpr int nativeNearbyHeightSceneVersion=66;
// Scene67 routes player infantry Scatter through the source eligibility/search.
inline constexpr int nativePlayerScatterSceneVersion=67;
// Scene68 calibrates manual ground/naval Unit Scatter and its zero-draw search.
inline constexpr int nativeUnitScatterSceneVersion=68;
// Scene69 commits source Walk positions and speed arithmetic at native frames.
inline constexpr int nativeWalkStepSceneVersion=69;
inline constexpr int nativeCurrentRulesSceneVersion=nativeWalkStepSceneVersion;
inline constexpr bool nativeOrdinaryRulesScene(int version) {
    return version==nativeSkirmishRulesSemantics || version==nativeEconomySceneVersion || version==nativeHarvestSceneVersion
        || version==nativeGroundSceneVersion || version==nativeTravelSceneVersion || version==nativeCrushingSceneVersion
        || version==nativeCrushContactSceneVersion || version==nativeInfantryDepartureSceneVersion || version==nativeCrushWarningSceneVersion
        || version==nativeDriveBlockageSceneVersion || version==nativeGapAreaSceneVersion || version==nativeGapGroundHeightSceneVersion
        || version==nativeGapGroundSlopeSceneVersion || version==nativeSizedTransportSceneVersion
        || version==nativeCargoOwnerSceneVersion || version==nativeTransportUnloadSceneVersion || version==nativeTransportExitSceneVersion
        || version==nativeAlliedBoardingSceneVersion || version==nativeTransportBoardingSceneVersion
        || version==nativeBuildingRepairSceneVersion || version==nativeBuildingSaleSceneVersion
        || version==nativeConstructionYardSaleSceneVersion || version==nativeSaleSurvivorsSceneVersion
        || version==nativeNearbyScatterSceneVersion || version==nativeNearbyHeightSceneVersion
        || version==nativePlayerScatterSceneVersion || version==nativeUnitScatterSceneVersion
        || version==nativeWalkStepSceneVersion;
}
inline constexpr int maximumNativeSceneVersion=std::max(nativeCurrentRulesSceneVersion,nativeGlobalAIContracts.back().sceneVersion);
