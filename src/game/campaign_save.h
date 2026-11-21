#pragma once
#include "game/campaign_runtime.h"
#include <cstdio>

struct CampaignSnapshot {
    int difficulty=1;
    CampaignRuntime runtime;
    std::vector<bool> objectives;
    bool gameOver=false,victory=false;
    bool operator==(const CampaignSnapshot&) const = default;
};

// Compare the complete installed definition before restoring its mutable state.
// A changed/reordered mission catalog must never silently run a different script.
bool writeCampaignSnapshot(FILE* file,const MissionDef* mission,const CampaignSnapshot& state);
bool readCampaignSnapshot(FILE* file,const MissionDef* mission,CampaignSnapshot& state);
