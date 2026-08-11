#pragma once
#include "game/data.h"
#include <string>
#include <vector>

// 战役任务：波次脚本 + 胜利目标
struct MissionWave {
    int atTick;                   // 触发逻辑帧（30帧/秒）
    std::vector<UnitType> units;  // 为敌方玩家1刷出并攻击移动至玩家基地
};

// ===================== 触发器框架（RA2 式地图脚本） =====================
enum class TrigCond : uint8_t {
    Always = 0,     // 开局即触发
    Time,           // world.tick >= c0
    PlayerBldLost,  // 玩家 c0 的 btype(c1) 建筑已全灭（数量 == 0）
    PlayerAllDead,  // 玩家 c0 已被歼灭（defeated）
    UnitInRect,     // 玩家 c0 的任一单位进入矩形 (c1,c2)-(c3,c4)
    MoneyBelow,     // 玩家 c0 资金 < c1
    Script,         // 调用 Lua OnTriggerCond(tag) 返回 bool；tag 见 c 字符串
    UnitLost,       // 玩家 c0 的 UnitType(c1) 曾存在且现已全灭（英雄死亡失败等）
    BldCaptured,    // 玩家 c0 现拥有 >= c2（默认1）座 BldType(c1)（占领/移交后成立）
    ObjAllPrimary,  // 全部 GateWin 主目标已 CompleteObj
    UnitCountBelow, // 玩家 c0 的 UnitType(c1) 存活数 < c2
    PhaseAt,        // CampaignRuntime.phase >= c0
};

enum class TrigAct : uint8_t {
    SpawnAt,        // 在 (a1,a2) 为玩家 a0 刷 units；a3>=0 时攻击移动至 (a3,a4)
    Eva,            // EVA 播报 msg（对玩家 0）
    GiveMoney,      // 玩家 a0 资金 += a1
    RevealMap,      // 为玩家 a0 揭示圆形区域 (a1,a2) 半径 a3
    Win,            // 玩家 0 立即胜利
    Lose,           // 玩家 0 立即失败
    Objective,      // 更新 HUD 目标文本为 msg；若 a0>=0 则同步更新该条目标文案
    Script,         // 调用 Lua OnTrigger(tag)；tag 见 a 字符串
    CompleteObj,    // 标记 a0 号目标完成（多目标清单）
    SetPhase,       // CampaignRuntime.phase = a0
    EnableTag,      // 启用 Tag 匹配的触发器（Enabled=no 的延后脚本）
    Reinforce,      // 从地图边 a1(0N1E2S3W) 为玩家 a0 刷 units，攻击移动至 (a2,a3)（a2<0 则不移动）
    TimerStart,     // 启动限时：截止 tick = now+a0；a1!=0 时 HUD 可见
    TimerAbort,     // 取消限时
};

struct Trigger {
    TrigCond cond = TrigCond::Always;
    int c[5] = {0, 0, 0, 0, 0};       // 条件参数
    TrigAct act = TrigAct::Eva;
    int a[5] = {0, 0, 0, 0, -1};      // 动作参数（a3 默认 -1 = 不攻击移动）
    std::vector<UnitType> units;      // SpawnAt / Reinforce 用
    std::string msg;                  // Eva/Objective 用（中文）
    std::string msgEn;                // 英文（空则回退 msg）
    std::string tag;                  // Script / EnableTag 标识
    bool once = true;                 // 仅触发一次
    bool fired = false;               // 运行时状态（Game 副本上修改）
    bool armed = false;               // PlayerBldLost/UnitLost 用：目标曾存在过才允许触发（防开局即误判）
    bool enabled = true;              // Enabled=no → 需 EnableTag 后才求值
    int requiresPhase = -1;           // >=0 时仅当 runtime.phase >= 该值才求值
};

struct MissionObjective {
    std::string text;
    std::string textEn;
    bool primary = true;
    bool gateWin = true;              // Primary 且 gateWin：全部完成 → 胜（GateWin=no 用于「英雄存活」等）
};

struct MissionDef {
    std::string name;
    std::string nameEn;                // 英文名（空回退 name）
    std::string brief;
    std::string briefEn;               // 英文简报（空回退 brief）
    Faction playerFaction;
    std::vector<Faction> aiFactions; // 敌方阵营（数量=AI数）
    int mapSize;
    int mapType;                     // 0 大陆 1 岛屿 2 湖泊
    int money;
    std::vector<MissionWave> waves;
    int objective;                   // 0 歼灭所有敌军 1 存活至 objectiveTick 2 触发器决定胜负
    int objectiveTick;
    std::string mapFile;             // 手工地图（maps/xxx.txt）；空则程序生成
    bool noStartForce = false;       // true=不刷初始基地车部队（全部由地图文件放置）
    std::vector<Trigger> triggers;   // 触发器脚本
    int track = 0;                   // 0=融合自制 1=官方轨
    // ---- 战役壳 ----
    std::string lineId;              // 进度线：fc/fa/fs/fy / oa/os/ya/ys
    int lineIndex = 0;               // 线内序号 0-based；通关后解锁 lineIndex+1
    int nextMission = -1;            // 表内下一关索引（加载后解析；-1=无线）
    Country playerCountry = Country::None; // None=阵营默认首国
    std::vector<BldType> allowedBuildings; // 空=不限制
    std::vector<UnitType> allowedUnits;    // 空=不限制
    std::string briefArt;            // 可选简报静图路径
    std::vector<MissionObjective> objectives;
    bool winOnAllPrimary = true;     // Objective=2：全部 GateWin 主目标完成即胜（可被显式 Act=Win 覆盖）
    int startPhase = 0;              // 开局阶段（Phase=）
    bool timerVisibleDefault = false;// TimerVisible=：TimerStart 默认是否显示
};

// 战役任务表：首次调用时加载 assets/campaigns/（campaign.ini 列表 + 每关一个 INI），
// 目录缺失/为空时回退内置 32 关。返回常驻静态表引用。
const std::vector<MissionDef>& missionTable();

// 按 LineId 的解锁进度（已通关到的下一关 lineIndex；0=仅第一关解锁）
int campaignProgress(const std::string& lineId);
void campaignSetProgress(const std::string& lineId, int clearedNextIndex);
void campaignLoadProgress();
void campaignSaveProgress();
bool campaignMissionUnlocked(int missionIndex);
int campaignFindNextMission(int missionIndex); // 解析后的 nextMission；无则 -1

// 战役导出（--export-assets）：把内置 32 关写成 campaign.ini + mission01..32.ini 模板
void exportCampaigns(const char* dir);
