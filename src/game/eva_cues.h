#pragma once
#include <cstdint>
#include <string_view>

// Semantic announcement identities. No files, audio APIs or localization here.
#define RA2_EVA_CUES(X) \
    X(None, "") \
    X(ConstructionComplete, "EVA_ConstructionComplete") \
    X(NewConstructionOptions, "EVA_NewConstructionOptions") \
    X(UnitReady, "EVA_UnitReady") \
    X(Building, "EVA_Building") \
    X(Training, "EVA_Training") \
    X(OnHold, "EVA_OnHold") \
    X(Canceled, "EVA_Canceled") \
    X(InsufficientFunds, "EVA_InsufficientFunds") \
    X(LowPower, "EVA_LowPower") \
    X(BaseUnderAttack, "EVA_OurBaseIsUnderAttack") \
    X(MinerUnderAttack, "EVA_OreMinerUnderAttack") \
    X(UnitLost, "EVA_UnitLost") \
    X(UnitPromoted, "EVA_UnitPromoted") \
    X(BuildingCaptured, "EVA_BuildingCaptured") \
    X(BuildingInfiltrated, "EVA_BuildingInfiltrated") \
    X(NewTechnology, "EVA_NewTechnologyAcquired") \
    X(UnitRepaired, "EVA_UnitRepaired") \
    X(Repairing, "EVA_Repairing") \
    X(StructureSold, "EVA_StructureSold") \
    X(CannotDeploy, "EVA_CannotDeployHere") \
    X(SelectTarget, "EVA_SelectTarget") \
    X(NukeDetected, "EVA_NuclearSiloDetected") \
    X(NukeLaunched, "EVA_NuclearMissileLaunched") \
    X(NukeReady, "EVA_NuclearMissileReady") \
    X(IronDetected, "EVA_IronCurtainDetected") \
    X(IronActivated, "EVA_IronCurtainActivated") \
    X(IronReady, "EVA_IronCurtainReady") \
    X(ChronoDetected, "EVA_ChronosphereDetected") \
    X(ChronoActivated, "EVA_ChronosphereActivated") \
    X(ChronoReady, "EVA_ChronosphereReady") \
    X(WeatherDetected, "EVA_WeatherDeviceReady") \
    X(StormCreated, "EVA_LightningStormCreated") \
    X(StormReady, "EVA_LightningStormReady") \
    X(MissionAccomplished, "EVA_MissionAccomplished") \
    X(MissionFailed, "EVA_MissionFailed") \
    X(Victory, "EVA_YouAreVictorious") \
    X(Defeat, "EVA_YouHaveLost") \
    X(BattleTerminated, "EVA_BattleControlTerminated")

enum class EvaCue : uint8_t {
#define EVA_ENUM(name,key) name,
    RA2_EVA_CUES(EVA_ENUM)
#undef EVA_ENUM
    COUNT
};
inline std::string_view evaCueName(EvaCue cue) {
    static constexpr std::string_view names[]={
#define EVA_NAME(name,key) key,
        RA2_EVA_CUES(EVA_NAME)
#undef EVA_NAME
    };
    return (unsigned)cue<(unsigned)EvaCue::COUNT?names[(unsigned)cue]:std::string_view{};
}
