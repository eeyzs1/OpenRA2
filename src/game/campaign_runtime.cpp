#include "game/campaign_runtime.h"

void CampaignRuntime::reset(const MissionDef& md) {
    phase = 0;
    timerDeadline = -1;
    timerVisible = false;
    winOnAllPrimary = md.winOnAllPrimary;
    armed = true;
    (void)md;
}

void CampaignRuntime::startTimer(int deadlineTick, bool visible) {
    timerDeadline = deadlineTick;
    timerVisible = visible;
}

void CampaignRuntime::abortTimer() {
    timerDeadline = -1;
    timerVisible = false;
}

int CampaignRuntime::timerRemaining(uint64_t tick) const {
    if (timerDeadline < 0) return -1;
    if ((uint64_t)timerDeadline <= tick) return 0;
    return (int)(((uint64_t)timerDeadline - tick + 29) / 30); // 向上取整秒
}

bool CampaignRuntime::timerExpired(uint64_t tick) const {
    return timerDeadline >= 0 && tick >= (uint64_t)timerDeadline;
}

bool CampaignRuntime::allGateWinDone(const MissionDef& md, const std::vector<bool>& objDone) const {
    if (!winOnAllPrimary) return false;
    bool any = false;
    for (size_t i = 0; i < md.objectives.size(); i++) {
        if (!md.objectives[i].primary || !md.objectives[i].gateWin) continue;
        any = true;
        if (i >= objDone.size() || !objDone[i]) return false;
    }
    return any;
}
