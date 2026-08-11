#pragma once
#include "game/campaign.h"
#include <cstdint>
#include <vector>

// 战役关卡运行时状态机（选关/简报之外的「剧本战役」核心）
// 不进 World checksum：表现与任务脚本状态由 Game 持有。
struct CampaignRuntime {
    int phase = 0;
    int timerDeadline = -1;   // >=0：到点未 Abort 则失败
    bool timerVisible = false;
    bool winOnAllPrimary = true; // Objective=2 时：全部 GateWin 主目标完成 → 胜
    bool armed = false;

    void reset(const MissionDef& md);
    void setPhase(int p) { phase = p; }
    void startTimer(int deadlineTick, bool visible);
    void abortTimer();
    int timerRemaining(uint64_t tick) const; // 秒；无计时返回 -1
    bool timerExpired(uint64_t tick) const;
    bool allGateWinDone(const MissionDef& md, const std::vector<bool>& objDone) const;
};
