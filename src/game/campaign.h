#pragma once
#include "game/data.h"
#include <vector>

// 战役任务：波次脚本 + 胜利目标
struct MissionWave {
    int atTick;                   // 触发逻辑帧（30帧/秒）
    std::vector<UnitType> units;  // 为敌方玩家1刷出并攻击移动至玩家基地
};

struct MissionDef {
    const char* name;
    const char* brief;
    Faction playerFaction;
    std::vector<Faction> aiFactions; // 敌方阵营（数量=AI数）
    int mapSize;
    int mapType;                     // 0 大陆 1 岛屿 2 湖泊
    int money;
    std::vector<MissionWave> waves;
    int objective;                   // 0 歼灭所有敌军 1 存活至 objectiveTick
    int objectiveTick;
};

// 战役关卡（12 关：中国 4 + 盟军 4 + 苏军 4，难度递进，含歼灭/防守/海军/多线作战）
inline const std::vector<MissionDef>& missionTable() {
    static const std::vector<MissionDef> tbl = {
        // ---- 中国战役 ----
        {
            "边境冲突",
            "苏军越境进犯。建立基地，击退增援，歼灭敌军。",
            Faction::China, {Faction::Soviet},
            64, 0, 9000,
            {
                {2700,  {UnitType::Conscript, UnitType::Conscript, UnitType::Conscript, UnitType::Rhino}},
                {6300,  {UnitType::Conscript, UnitType::Conscript, UnitType::FlakTrooper, UnitType::Rhino, UnitType::Rhino}},
                {10800, {UnitType::TeslaTrooper, UnitType::TeslaTrooper, UnitType::Rhino, UnitType::Rhino, UnitType::V3Launcher}},
            },
            0, 0
        },
        {
            "近海防御",
            "盟军舰队自海上来袭。建设海军，歼灭敌军。",
            Faction::China, {Faction::Allies},
            96, 1, 12000,
            {
                {3600,  {UnitType::Destroyer}},
                {7200,  {UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder, UnitType::Intruder}},
                {12600, {UnitType::Destroyer, UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder, UnitType::Intruder}},
            },
            0, 0
        },
        {
            "共和之盾",
            "苏军两路夹击边境重镇。坚守八分钟，等待主力回援。",
            Faction::China, {Faction::Soviet, Faction::Soviet},
            96, 0, 14000,
            {
                {2700,  {UnitType::Conscript, UnitType::Conscript, UnitType::Rhino, UnitType::Rhino}},
                {5400,  {UnitType::FlakTrooper, UnitType::FlakTrooper, UnitType::Rhino, UnitType::V3Launcher}},
                {8100,  {UnitType::TeslaTrooper, UnitType::TeslaTrooper, UnitType::Rhino, UnitType::Rhino, UnitType::Apocalypse}},
                {10800, {UnitType::Kirov, UnitType::Rhino, UnitType::Rhino}},
                {13500, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::V3Launcher, UnitType::V3Launcher}},
            },
            1, 14400
        },
        {
            "利剑出鞘",
            "远征军登陆盟军控制的群岛。夺取制海权，歼灭两路盟军。",
            Faction::China, {Faction::Allies, Faction::Allies},
            128, 1, 16000,
            {
                {3600,  {UnitType::Destroyer, UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {7200,  {UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::AircraftCarrier, UnitType::Destroyer, UnitType::PrismTank, UnitType::PrismTank}},
                {14400, {UnitType::Destroyer, UnitType::Destroyer, UnitType::Destroyer, UnitType::MirageTank, UnitType::MirageTank}},
            },
            0, 0
        },
        // ---- 盟军战役 ----
        {
            "自由之光",
            "苏俄钢铁师团兵临城下。指挥盟军残部建立防线，歼灭来犯之敌。",
            Faction::Allies, {Faction::Soviet},
            64, 0, 9000,
            {
                {2700,  {UnitType::Conscript, UnitType::Conscript, UnitType::Rhino}},
                {6300,  {UnitType::TeslaTrooper, UnitType::Rhino, UnitType::Rhino, UnitType::FlakTrooper}},
                {10800, {UnitType::TeslaTank, UnitType::Rhino, UnitType::Rhino, UnitType::V3Launcher}},
            },
            0, 0
        },
        {
            "深海猎杀",
            "苏俄台风潜艇群封锁近海航道。率舰队突围，摧毁沿岸苏军基地。",
            Faction::Allies, {Faction::Soviet},
            96, 1, 13000,
            {
                {3600,  {UnitType::Typhoon, UnitType::Typhoon}},
                {7200,  {UnitType::Typhoon, UnitType::Typhoon, UnitType::SeaScorpion, UnitType::SeaScorpion}},
                {10800, {UnitType::Typhoon, UnitType::Typhoon, UnitType::Typhoon, UnitType::Dreadnought}},
                {14400, {UnitType::Rhino, UnitType::Rhino, UnitType::Rhino, UnitType::AmphTransport}},
            },
            0, 0
        },
        {
            "时空风暴",
            "苏军倾巢而出直扑时空研究所。坚守十分钟，确保超时空传送仪运转。",
            Faction::Allies, {Faction::Soviet, Faction::Soviet},
            96, 2, 15000,
            {
                {2700,  {UnitType::Conscript, UnitType::Conscript, UnitType::Rhino, UnitType::Rhino}},
                {5400,  {UnitType::TeslaTrooper, UnitType::TeslaTrooper, UnitType::Rhino, UnitType::V3Launcher}},
                {8100,  {UnitType::Kirov, UnitType::Rhino, UnitType::Rhino, UnitType::FlakTrooper}},
                {10800, {UnitType::Apocalypse, UnitType::Rhino, UnitType::Rhino, UnitType::MiG}},
                {14400, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Kirov}},
                {17100, {UnitType::Apocalypse, UnitType::TeslaTank, UnitType::TeslaTank, UnitType::V3Launcher, UnitType::V3Launcher}},
            },
            1, 18000
        },
        {
            "决战莫斯科",
            "总攻苏俄心脏地带。击溃克里姆林宫卫戍部队，终结战争。",
            Faction::Allies, {Faction::Soviet, Faction::Soviet},
            128, 0, 18000,
            {
                {3600,  {UnitType::Rhino, UnitType::Rhino, UnitType::Conscript, UnitType::Conscript, UnitType::Conscript}},
                {7200,  {UnitType::TeslaTank, UnitType::TeslaTank, UnitType::Rhino, UnitType::Rhino}},
                {10800, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::V3Launcher, UnitType::V3Launcher}},
                {14400, {UnitType::Kirov, UnitType::Kirov, UnitType::MiG, UnitType::MiG}},
                {18000, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Apocalypse, UnitType::TeslaTank}},
            },
            0, 0
        },
        // ---- 苏军战役 ----
        {
            "钢铁洪流",
            "盟军前哨扼守边境平原。以装甲集群碾碎防线，歼灭全部守军。",
            Faction::Soviet, {Faction::Allies},
            64, 0, 9000,
            {
                {2700,  {UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {6300,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::Grizzly, UnitType::Grizzly, UnitType::PrismTank, UnitType::IFV}},
            },
            0, 0
        },
        {
            "岛屿争夺",
            "盟军航母战斗群盘踞资源群岛。夺取制海权并登陆夺岛。",
            Faction::Soviet, {Faction::Allies},
            96, 1, 13000,
            {
                {3600,  {UnitType::Destroyer, UnitType::Destroyer}},
                {7200,  {UnitType::Destroyer, UnitType::Dolphin, UnitType::Dolphin, UnitType::Intruder}},
                {10800, {UnitType::AircraftCarrier, UnitType::Destroyer, UnitType::Destroyer}},
                {14400, {UnitType::Grizzly, UnitType::Grizzly, UnitType::Grizzly, UnitType::AmphTransport}},
            },
            0, 0
        },
        {
            "红色警戒",
            "盟军与仆从国联军合围核设施。坚守九分钟，保住战略反击力量。",
            Faction::Soviet, {Faction::Allies, Faction::China},
            96, 0, 15000,
            {
                {2700,  {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GI}},
                {5400,  {UnitType::PLA, UnitType::PLA, UnitType::Type99, UnitType::Type99}},
                {8100,  {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::Intruder}},
                {10800, {UnitType::Type99, UnitType::Type99, UnitType::Type99, UnitType::PLA}},
                {13500, {UnitType::PrismTank, UnitType::PrismTank, UnitType::MirageTank, UnitType::BlackEagle}},
            },
            1, 16200
        },
        {
            "核子黎明",
            "最后的决战。击溃盟军与仆从国联军，让赤旗插遍世界。",
            Faction::Soviet, {Faction::Allies, Faction::Allies},
            128, 2, 18000,
            {
                {3600,  {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GI, UnitType::GuardianGI}},
                {7200,  {UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::MirageTank, UnitType::MirageTank, UnitType::Intruder, UnitType::Intruder}},
                {14400, {UnitType::PrismTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::BlackEagle}},
                {18000, {UnitType::MirageTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
            },
            0, 0
        },
    };
    return tbl;
}
