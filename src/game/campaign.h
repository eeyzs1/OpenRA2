#pragma once
#include "game/data.h"
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
};

struct Trigger {
    TrigCond cond = TrigCond::Always;
    int c[5] = {0, 0, 0, 0, 0};       // 条件参数
    TrigAct act = TrigAct::Eva;
    int a[5] = {0, 0, 0, 0, -1};      // 动作参数（a3 默认 -1 = 不攻击移动）
    std::vector<UnitType> units;      // SpawnAt 用
    const char* msg = nullptr;        // Eva/Objective 用（中文）
    const char* msgEn = nullptr;      // 英文（空则回退 msg）
    bool once = true;                 // 仅触发一次
    bool fired = false;               // 运行时状态（Game 副本上修改）
    bool armed = false;               // PlayerBldLost 用：目标建筑曾存在过才允许触发（防开局即误判）
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
    int objective;                   // 0 歼灭所有敌军 1 存活至 objectiveTick 2 触发器决定胜负
    int objectiveTick;
    // ---- P7 扩展（默认成员初始化，旧关卡不受影响） ----
    const char* mapFile = nullptr;   // 手工地图（maps/xxx.txt）；空则程序生成
    bool noStartForce = false;       // true=不刷初始基地车部队（全部由地图文件放置）
    std::vector<Trigger> triggers;   // 触发器脚本
};

// 战役关卡（24 关：中国 8 + 盟军 8 + 苏军 8，难度递进；含手工地图/触发器脚本关）
inline const std::vector<MissionDef>& missionTable() {
    static const std::vector<MissionDef> tbl = {
        // ==================== 中国战役（0-7） ====================
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
        {
            "戈壁前哨",
            "边境戈壁发现盟军前哨站。占领区域科技设施，摧毁前哨。",
            Faction::China, {Faction::Allies},
            96, 0, 11000,
            {
                {3600,  {UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {8100,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::Grizzly}},
                {13500, {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::IFV}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "侦察报告：区域内有科技机场与油井，建议派工程师占领。", "Recon: a tech airport and oil derricks in the area. Send engineers."},
                {TrigCond::Time, {9000,0,0,0,0}, TrigAct::GiveMoney, {0,3000,0,0,-1}, {},
                 "后方补给抵达：资金 +3000。", "Supply arrived: +3000 credits."},
                {TrigCond::PlayerBldLost, {1,(int)BldType::ConYard,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "敌方建造厂已摧毁，继续清剿残敌！", "Enemy construction yard destroyed. Mop up the rest!"},
            }
        },
        {
            "长江天堑",
            "敌军隔江对峙。控制桥梁要道，坚守十分钟并歼灭来犯之敌。",
            Faction::China, {Faction::Soviet, Faction::Soviet},
            96, 0, 13000,
            {
                {2700,  {UnitType::Conscript, UnitType::Conscript, UnitType::Rhino, UnitType::Rhino}},
                {5400,  {UnitType::FlakTrooper, UnitType::FlakTrooper, UnitType::Rhino, UnitType::V3Launcher}},
                {8100,  {UnitType::TeslaTrooper, UnitType::TeslaTrooper, UnitType::Rhino, UnitType::Rhino, UnitType::Apocalypse}},
                {10800, {UnitType::Kirov, UnitType::Rhino, UnitType::Rhino}},
                {13500, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::V3Launcher, UnitType::V3Launcher}},
                {16200, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::TeslaTank, UnitType::TeslaTank}},
            },
            0, 0, "maps/china_yangtze.txt", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "长江只有两座桥可通行，布防桥头！", "Only two bridges cross the Yangtze. Defend the bridgeheads!"},
                {TrigCond::Time, {14400,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "敌军主力正在集结，准备总攻！", "Enemy main force is massing for a final push!"},
                {TrigCond::Time, {5400,0,0,0,0}, TrigAct::GiveMoney, {0,2500,0,0,-1}, {},
                 "军委追加军费：资金 +2500。", "High command granted extra funds: +2500."},
            }
        },
        {
            "深海雷霆",
            "盟军航母战斗群封锁海峡。集结海军，全歼敌方舰队与岸防基地。",
            Faction::China, {Faction::Allies, Faction::Allies},
            128, 1, 15000,
            {
                {3600,  {UnitType::Destroyer, UnitType::Destroyer, UnitType::Dolphin}},
                {7200,  {UnitType::Aegis, UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder, UnitType::Intruder}},
                {10800, {UnitType::AircraftCarrier, UnitType::Destroyer, UnitType::Destroyer, UnitType::Dolphin}},
                {14400, {UnitType::AircraftCarrier, UnitType::Aegis, UnitType::Destroyer, UnitType::Intruder, UnitType::Intruder}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "注意：敌方拥有航空母舰，优先击沉！", "Warning: enemy aircraft carriers sighted. Sink them first!"},
                {TrigCond::Time, {7200,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,12,12},
                 {UnitType::Aegis, UnitType::Aegis, UnitType::Type99, UnitType::Type99},
                 "我方增援舰队抵达战场。", "Our reinforcement fleet has arrived."},
            }
        },
        {
            "共和之辉",
            "终局之战。苏联盟军残部结成同盟负隅顽抗，三线出击，解放全境。",
            Faction::China, {Faction::Soviet, Faction::Allies, Faction::Soviet},
            128, 0, 20000,
            {
                {3600,  {UnitType::Rhino, UnitType::Rhino, UnitType::Conscript, UnitType::Conscript}},
                {7200,  {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GuardianGI}},
                {10800, {UnitType::Apocalypse, UnitType::Rhino, UnitType::Rhino, UnitType::V3Launcher}},
                {14400, {UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::MirageTank}},
                {18000, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Kirov, UnitType::Kirov}},
                {21600, {UnitType::Apocalypse, UnitType::TeslaTank, UnitType::PrismTank, UnitType::MirageTank, UnitType::Kirov}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "这是最后一战。为了共和国，前进！", "This is the final battle. For the Republic, advance!"},
                {TrigCond::PlayerAllDead, {1,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "第一路敌军已被歼灭！", "The first enemy army has been eliminated!"},
                {TrigCond::PlayerAllDead, {2,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "第二路敌军已被歼灭！", "The second enemy army has been eliminated!"},
            }
        },
        // ==================== 盟军战役（8-15） ====================
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
        {
            "敌后武工队",
            "一支小队渗透苏军腹地：谭雅、间谍与工程师。摧毁核弹发射井，全身而退。",
            Faction::Allies, {Faction::Soviet},
            96, 0, 0,
            {},
            2, 0, "maps/allied_sabotage.txt", true,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Objective, {0,0,0,0,-1}, {},
                 "目标：摧毁核弹发射井（谭雅 C4 或强攻）", "Objective: destroy the nuclear silo (Tanya C4 or assault)"},
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "小队已就位。间谍可渗透电厂断电，谭雅负责爆破。", "Team in position. Spy can cut power; Tanya handles demolition."},
                {TrigCond::PlayerBldLost, {1,(int)BldType::NukeSilo,0,0,0}, TrigAct::Win, {0,0,0,0,-1}, {},
                 "核弹发射井已摧毁，任务完成！", "Nuclear silo destroyed. Mission accomplished!"},
                {TrigCond::PlayerAllDead, {0,0,0,0,0}, TrigAct::Lose, {0,0,0,0,-1}, {},
                 "小队全灭，任务失败。", "The team was wiped out. Mission failed."},
            }
        },
        {
            "诺曼底回响",
            "登陆作战：夺取滩头阵地，建立前进基地，歼灭守军。",
            Faction::Allies, {Faction::Soviet},
            96, 1, 11000,
            {
                {3600,  {UnitType::Conscript, UnitType::Conscript, UnitType::Rhino}},
                {7200,  {UnitType::Typhoon, UnitType::Typhoon, UnitType::SeaScorpion}},
                {10800, {UnitType::Rhino, UnitType::Rhino, UnitType::V3Launcher, UnitType::TeslaTrooper}},
                {14400, {UnitType::Dreadnought, UnitType::Typhoon, UnitType::Rhino, UnitType::Rhino}},
            },
            0, 0, "maps/allied_landing.txt", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "登陆开始！先夺滩头，再图纵深。", "Landing commenced! Secure the beachhead first."},
                {TrigCond::Time, {3600,0,0,0,0}, TrigAct::SpawnAt, {0,6,90,12,84},
                 {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GI, UnitType::GuardianGI},
                 "后续登陆部队抵达滩头。", "Follow-up landing forces have arrived."},
                {TrigCond::Time, {9000,0,0,0,0}, TrigAct::GiveMoney, {0,3000,0,0,-1}, {},
                 "本土增援物资抵达：资金 +3000。", "Reinforcements from home: +3000 credits."},
            }
        },
        {
            "方舟守护",
            "超时空传送仪原型机暴露。不惜一切代价保护它，坚守十二分钟。",
            Faction::Allies, {Faction::Soviet, Faction::Soviet},
            96, 0, 16000,
            {
                {2700,  {UnitType::Conscript, UnitType::Conscript, UnitType::Rhino, UnitType::Rhino}},
                {5400,  {UnitType::TeslaTrooper, UnitType::TeslaTrooper, UnitType::Rhino, UnitType::V3Launcher}},
                {8100,  {UnitType::Kirov, UnitType::Rhino, UnitType::Rhino, UnitType::FlakTrooper}},
                {10800, {UnitType::Apocalypse, UnitType::Rhino, UnitType::Rhino, UnitType::MiG}},
                {14400, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Kirov}},
                {18000, {UnitType::Apocalypse, UnitType::TeslaTank, UnitType::TeslaTank, UnitType::V3Launcher, UnitType::V3Launcher}},
            },
            1, 21600, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Objective, {0,0,0,0,-1}, {},
                 "目标：坚守 12 分钟，超时空传送仪不可被毁", "Objective: hold 12 min; the Chronosphere must survive"},
                {TrigCond::PlayerBldLost, {0,(int)BldType::ChronoSphere,0,0,0}, TrigAct::Lose, {0,0,0,0,-1}, {},
                 "超时空传送仪被摧毁，任务失败。", "The Chronosphere was destroyed. Mission failed."},
                {TrigCond::Time, {10800,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "坚持住！时间过半，援军正在路上。", "Hang on! Half way there. Reinforcements en route."},
            }
        },
        {
            "自由终章",
            "苏俄残余势力集结于北极圈最后据点。发动总攻，彻底终结战争。",
            Faction::Allies, {Faction::Soviet, Faction::Soviet, Faction::Soviet},
            128, 2, 20000,
            {
                {3600,  {UnitType::Rhino, UnitType::Rhino, UnitType::Conscript, UnitType::Conscript}},
                {7200,  {UnitType::TeslaTank, UnitType::TeslaTank, UnitType::Rhino, UnitType::Rhino}},
                {10800, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::V3Launcher, UnitType::V3Launcher}},
                {14400, {UnitType::Kirov, UnitType::Kirov, UnitType::MiG, UnitType::MiG}},
                {18000, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Apocalypse, UnitType::TeslaTank}},
                {21600, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Kirov, UnitType::TeslaTank, UnitType::TeslaTank}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "为了自由世界，发起最后的冲锋！", "For the free world — one final charge!"},
                {TrigCond::PlayerAllDead, {1,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "敌方第一军团覆灭！", "Enemy first army destroyed!"},
                {TrigCond::PlayerAllDead, {2,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "敌方第二军团覆灭！", "Enemy second army destroyed!"},
            }
        },
        // ==================== 苏军战役（16-23） ====================
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
        {
            "乌拉尔防线",
            "盟军远征军兵锋直指乌拉尔工业区。坚守十一分钟，等待战略预备队。",
            Faction::Soviet, {Faction::Allies, Faction::Allies},
            96, 0, 15000,
            {
                {2700,  {UnitType::GI, UnitType::GI, UnitType::Grizzly, UnitType::Grizzly}},
                {5400,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::IFV}},
                {8100,  {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::Intruder}},
                {10800, {UnitType::PrismTank, UnitType::PrismTank, UnitType::MirageTank, UnitType::BlackEagle}},
                {14400, {UnitType::MirageTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {18000, {UnitType::PrismTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::BlackEagle, UnitType::BlackEagle}},
            },
            1, 19800, nullptr, false,
            {
                {TrigCond::Time, {9900,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "再坚持一半时间，战略预备队即将抵达。", "Hold half as long again — strategic reserves are coming."},
                {TrigCond::Time, {5400,0,0,0,0}, TrigAct::GiveMoney, {0,2500,0,0,-1}, {},
                 "后方工厂全力支援：资金 +2500。", "Rear factories at full capacity: +2500 credits."},
            }
        },
        {
            "黑海舰队",
            "盟军舰队侵入黑海。出动黑海舰队，夺回制海权并摧毁沿岸基地。",
            Faction::Soviet, {Faction::Allies, Faction::Allies},
            128, 1, 15000,
            {
                {3600,  {UnitType::Destroyer, UnitType::Destroyer, UnitType::Dolphin}},
                {7200,  {UnitType::Aegis, UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder}},
                {10800, {UnitType::AircraftCarrier, UnitType::Destroyer, UnitType::Dolphin, UnitType::Dolphin}},
                {14400, {UnitType::AircraftCarrier, UnitType::Aegis, UnitType::Destroyer, UnitType::Destroyer}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "黑海不容侵犯。全歼来犯舰队！", "The Black Sea is not to be violated. Annihilate them!"},
                {TrigCond::Time, {8100,0,0,0,0}, TrigAct::SpawnAt, {0,10,10,14,14},
                 {UnitType::Dreadnought, UnitType::Typhoon, UnitType::Typhoon},
                 "无畏级战舰增援抵达。", "Dreadnought reinforcements have arrived."},
            }
        },
        {
            "心灵征服",
            "尤里的心灵部队接受实战检验。以心灵控制瓦解盟军防线，歼灭敌军。",
            Faction::Soviet, {Faction::Allies},
            96, 0, 13000,
            {
                {3600,  {UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {7200,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::PrismTank, UnitType::Grizzly, UnitType::MirageTank}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "尤里已抵达前线：心灵控制可策反敌方单位。", "Yuri has arrived: mind control turns enemy units."},
                {TrigCond::Time, {2700,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,-1,0},
                 {UnitType::Yuri, UnitType::Yuri},
                 "心灵专家增援抵达。", "Psi corps reinforcements have arrived."},
                {TrigCond::PlayerBldLost, {1,(int)BldType::BattleLab,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "敌方作战实验室已毁，其科技优势不复存在。", "Enemy battle lab destroyed. Their tech edge is gone."},
            }
        },
        {
            "世界革命",
            "革命的最后一战。盟军三方联军困兽犹斗，全线进攻，解放全世界。",
            Faction::Soviet, {Faction::Allies, Faction::Allies, Faction::Allies},
            128, 0, 20000,
            {
                {3600,  {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GI}},
                {7200,  {UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::MirageTank, UnitType::MirageTank, UnitType::Intruder, UnitType::Intruder}},
                {14400, {UnitType::PrismTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::BlackEagle}},
                {18000, {UnitType::MirageTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {21600, {UnitType::PrismTank, UnitType::MirageTank, UnitType::MirageTank, UnitType::BlackEagle, UnitType::BlackEagle}},
            },
            0, 0, nullptr, false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "全世界无产者，联合起来！这是最后的斗争。", "Workers of the world, unite! This is the final struggle."},
                {TrigCond::PlayerAllDead, {1,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "第一路敌军已覆灭！", "The first enemy army has fallen!"},
                {TrigCond::PlayerAllDead, {3,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "最后一路敌军摇摇欲坠，胜利在望！", "The last enemy army is crumbling. Victory is near!"},
            }
        },
    };
    return tbl;
}
