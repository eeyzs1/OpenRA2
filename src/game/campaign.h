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
// 条件类型
enum class TrigCond : uint8_t {
    Always = 0,     // 开局即触发
    Time,           // world.tick >= c0
    PlayerBldLost,  // 玩家 c0 的 btype(c1) 建筑已全灭（数量 == 0）
    PlayerAllDead,  // 玩家 c0 已被歼灭（defeated）
    UnitInRect,     // 玩家 c0 的任一单位进入矩形 (c1,c2)-(c3,c4)
    MoneyBelow,     // 玩家 c0 资金 < c1
    Script,         // 调用 Lua OnTriggerCond(tag) 返回 bool；tag 见 c 字符串
};

// 动作类型
enum class TrigAct : uint8_t {
    SpawnAt,        // 在 (a1,a2) 为玩家 a0 刷 units；a3>=0 时攻击移动至 (a3,a4)
    Eva,            // EVA 播报 msg（对玩家 0）
    GiveMoney,      // 玩家 a0 资金 += a1
    RevealMap,      // 为玩家 a0 揭示圆形区域 (a1,a2) 半径 a3
    Win,            // 玩家 0 立即胜利
    Lose,           // 玩家 0 立即失败
    Objective,      // 更新 HUD 目标文本为 msg
    Script,         // 调用 Lua OnTrigger(tag)；tag 见 a 字符串
};

struct Trigger {
    TrigCond cond = TrigCond::Always;
    int c[5] = {0, 0, 0, 0, 0};       // 条件参数
    TrigAct act = TrigAct::Eva;
    int a[5] = {0, 0, 0, 0, -1};      // 动作参数（a3 默认 -1 = 不攻击移动）
    std::vector<UnitType> units;      // SpawnAt 用
    std::string msg;                  // Eva/Objective 用（中文）
    std::string msgEn;                // 英文（空则回退 msg）
    std::string tag;                  // Script 条件/动作的 Lua 标识
    bool once = true;                 // 仅触发一次
    bool fired = false;               // 运行时状态（Game 副本上修改）
    bool armed = false;               // PlayerBldLost 用：目标建筑曾存在过才允许触发（防开局即误判）
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
};

// 战役任务表：首次调用时加载 assets/campaigns/（campaign.ini 列表 + 每关一个 INI），
// 目录缺失/为空时回退内置 32 关。返回常驻静态表引用。
const std::vector<MissionDef>& missionTable();

// 战役导出（--export-assets）：把内置 32 关写成 campaign.ini + mission01..32.ini 模板
void exportCampaigns(const char* dir);
