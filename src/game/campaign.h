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

inline const std::vector<MissionDef>& missionTable() {
    static const std::vector<MissionDef> tbl = {
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
            "决胜时刻",
            "苏盟两军环伺大湖。坚守十分钟等待反攻。",
            Faction::China, {Faction::Soviet, Faction::Allies},
            96, 2, 15000,
            {
                {3600,  {UnitType::Rhino, UnitType::Rhino, UnitType::Conscript, UnitType::Conscript}},
                {7200,  {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GI, UnitType::Intruder}},
                {10800, {UnitType::TeslaTank, UnitType::TeslaTank, UnitType::Rhino, UnitType::Rhino}},
                {14400, {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::MiG}},
            },
            1, 18000
        },
    };
    return tbl;
}
