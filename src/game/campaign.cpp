#include "game/campaign.h"
#include "core/ini.h"
#include "raylib.h"
#include <cstring>
#include <cstdio>

// ===================== 内置任务表（assets/campaigns 缺失时的回退） =====================
// 32 关：中国 8 + 盟军 8 + 苏军 8 + 尤里 8，难度递进；含手工地图/触发器脚本关。
// 该表与 assets/campaigns/*.ini 内容一致，外部文件优先。
static std::vector<MissionDef> buildBuiltinMissions() {
    return {
        // ==================== 中国战役（0-7） ====================
        {
            "边境冲突", "Border Skirmish",
            "苏军越境进犯。建立基地，击退增援，歼灭敌军。",
            "Soviet forces cross the border. Build your base, repel reinforcements, and eliminate all enemies.",
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
            "近海防御", "Coastal Defense",
            "盟军舰队自海上来袭。建设海军，歼灭敌军。",
            "An Allied fleet approaches from the sea. Build your navy and eliminate all enemies.",
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
            "共和之盾", "Shield of the Republic",
            "苏军两路夹击边境重镇。坚守八分钟，等待主力回援。",
            "Two Soviet armies close in. Hold the town for eight minutes until reinforcements arrive.",
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
            "利剑出鞘", "Sword Unsheathed",
            "远征军登陆盟军控制的群岛。夺取制海权，歼灭两路盟军。",
            "Expedition forces land on Allied-held islands. Win the seas and eliminate both Allied bases.",
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
            "戈壁前哨", "Gobi Outpost",
            "边境戈壁发现盟军前哨站。占领区域科技设施，摧毁前哨。",
            "An Allied outpost is found in the border Gobi. Capture tech structures and destroy it.",
            Faction::China, {Faction::Allies},
            96, 0, 11000,
            {
                {3600,  {UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {8100,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::Grizzly}},
                {13500, {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::IFV}},
            },
            0, 0, "", false,
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
            "长江天堑", "Yangtze Rampart",
            "敌军隔江对峙。控制桥梁要道，坚守十分钟并歼灭来犯之敌。",
            "The enemy holds the far bank. Control the bridges, hold ten minutes, and destroy the invaders.",
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
            "深海雷霆", "Deep Sea Thunder",
            "盟军航母战斗群封锁海峡。集结海军，全歼敌方舰队与岸防基地。",
            "An Allied carrier group blockades the strait. Mass the fleet and annihilate enemy ships and shore base.",
            Faction::China, {Faction::Allies, Faction::Allies},
            128, 1, 15000,
            {
                {3600,  {UnitType::Destroyer, UnitType::Destroyer, UnitType::Dolphin}},
                {7200,  {UnitType::Aegis, UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder, UnitType::Intruder}},
                {10800, {UnitType::AircraftCarrier, UnitType::Destroyer, UnitType::Destroyer, UnitType::Dolphin}},
                {14400, {UnitType::AircraftCarrier, UnitType::Aegis, UnitType::Destroyer, UnitType::Intruder, UnitType::Intruder}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "注意：敌方拥有航空母舰，优先击沉！", "Warning: enemy aircraft carriers sighted. Sink them first!"},
                {TrigCond::Time, {7200,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,12,12},
                 {UnitType::Aegis, UnitType::Aegis, UnitType::Type99, UnitType::Type99},
                 "我方增援舰队抵达战场。", "Our reinforcement fleet has arrived."},
            }
        },
        {
            "共和之辉", "Glory of the Republic",
            "终局之战。苏联盟军残部结成同盟负隅顽抗，三线出击，解放全境。",
            "The final battle. Soviet and Allied remnants fight as one — strike on three fronts and liberate all.",
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
            0, 0, "", false,
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
            "自由之光", "Light of Freedom",
            "苏俄钢铁师团兵临城下。指挥盟军残部建立防线，歼灭来犯之敌。",
            "Russian armored division approaches. Rally the Allied remnants and destroy the invaders.",
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
            "深海猎杀", "Deep Sea Hunt",
            "苏俄台风潜艇群封锁近海航道。率舰队突围，摧毁沿岸苏军基地。",
            "Typhoon subs blockade the strait. Break out with the fleet and destroy the Soviet coastal base.",
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
            "时空风暴", "Chrono Storm",
            "苏军倾巢而出直扑时空研究所。坚守十分钟，确保超时空传送仪运转。",
            "Soviets march on the Chrono research center. Hold for ten minutes to keep the Chronosphere safe.",
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
            "决战莫斯科", "Battle for Moscow",
            "总攻苏俄心脏地带。击溃克里姆林宫卫戍部队，终结战争。",
            "Assault the heart of Soviet power. Crush the Kremlin guard and end the war.",
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
            "敌后武工队", "Behind Enemy Lines",
            "一支小队渗透苏军腹地：谭雅、间谍与工程师。摧毁核弹发射井，全身而退。",
            "A small team infiltrates Soviet territory: Tanya, a spy and an engineer. Destroy the silo and get out.",
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
            "诺曼底回响", "Normandy Echoes",
            "登陆作战：夺取滩头阵地，建立前进基地，歼灭守军。",
            "Amphibious assault: take the beachhead, build a forward base, and eliminate the defenders.",
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
            "方舟守护", "Ark Guardian",
            "超时空传送仪原型机暴露。不惜一切代价保护它，坚守十二分钟。",
            "The Chronosphere prototype is exposed. Protect it at all costs for twelve minutes.",
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
            1, 21600, "", false,
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
            "自由终章", "Freedom's Finale",
            "苏俄残余势力集结于北极圈最后据点。发动总攻，彻底终结战争。",
            "Soviet remnants gather at their last arctic stronghold. Launch the final assault and end the war.",
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
            0, 0, "", false,
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
            "钢铁洪流", "Steel Torrent",
            "盟军前哨扼守边境平原。以装甲集群碾碎防线，歼灭全部守军。",
            "Allied outposts hold the border plains. Crush their line with armored waves and eliminate them all.",
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
            "岛屿争夺", "Island Clash",
            "盟军航母战斗群盘踞资源群岛。夺取制海权并登陆夺岛。",
            "An Allied carrier group controls the resource islands. Win the seas and take the islands back.",
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
            "红色警戒", "Red Alert",
            "盟军与仆从国联军合围核设施。坚守九分钟，保住战略反击力量。",
            "Allied and vassal forces surround the nuclear facility. Hold for nine minutes to save our arsenal.",
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
            "核子黎明", "Nuclear Dawn",
            "最后的决战。击溃盟军与仆从国联军，让赤旗插遍世界。",
            "The final battle. Crush the Allied coalition and raise the red flag over the world.",
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
            "乌拉尔防线", "Ural Defense Line",
            "盟军远征军兵锋直指乌拉尔工业区。坚守十一分钟，等待战略预备队。",
            "The Allied expedition drives at the Ural industry zone. Hold eleven minutes for strategic reserves.",
            Faction::Soviet, {Faction::Allies, Faction::Allies},
            96, 0, 15000,
            {
                {2700,  {UnitType::GI, UnitType::GI, UnitType::Grizzly, UnitType::Grizzly}},
                {5400,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::IFV}},
                {8100,  {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::Intruder}},
                {10800, {UnitType::PrismTank, UnitType::Grizzly, UnitType::MirageTank, UnitType::BlackEagle}},
                {14400, {UnitType::MirageTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {18000, {UnitType::PrismTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::BlackEagle, UnitType::BlackEagle}},
            },
            1, 19800, "", false,
            {
                {TrigCond::Time, {9900,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "再坚持一半时间，战略预备队即将抵达。", "Hold half as long again — strategic reserves are coming."},
                {TrigCond::Time, {5400,0,0,0,0}, TrigAct::GiveMoney, {0,2500,0,0,-1}, {},
                 "后方工厂全力支援：资金 +2500。", "Rear factories at full capacity: +2500 credits."},
                {TrigCond::Time, {12600,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,-1,0},
                 {UnitType::Boris, UnitType::SiegeChopper, UnitType::SiegeChopper},
                 "鲍里斯与攻城直升机编队增援抵达！", "Boris and Siege Chopper squadron have arrived!"},
            }
        },
        {
            "黑海舰队", "Black Sea Fleet",
            "盟军舰队侵入黑海。出动黑海舰队，夺回制海权并摧毁沿岸基地。",
            "An Allied fleet invades the Black Sea. Sortie the fleet, retake the sea, and destroy the shore bases.",
            Faction::Soviet, {Faction::Allies, Faction::Allies},
            128, 1, 15000,
            {
                {3600,  {UnitType::Destroyer, UnitType::Destroyer, UnitType::Dolphin}},
                {7200,  {UnitType::Aegis, UnitType::Destroyer, UnitType::Destroyer, UnitType::Intruder}},
                {10800, {UnitType::AircraftCarrier, UnitType::Destroyer, UnitType::Dolphin, UnitType::Dolphin}},
                {14400, {UnitType::AircraftCarrier, UnitType::Aegis, UnitType::Destroyer, UnitType::Destroyer}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "黑海不容侵犯。全歼来犯舰队！", "The Black Sea is not to be violated. Annihilate them!"},
                {TrigCond::Time, {8100,0,0,0,0}, TrigAct::SpawnAt, {0,10,10,14,14},
                 {UnitType::Dreadnought, UnitType::Typhoon, UnitType::Typhoon},
                 "无畏级战舰增援抵达。", "Dreadnought reinforcements have arrived."},
            }
        },
        {
            "心灵征服", "Psychic Conquest",
            "尤里的心灵部队接受实战检验。以心灵控制瓦解盟军防线，歼灭敌军。",
            "Yuri's psychic corps faces its combat trial. Break the Allied line with mind control and destroy them.",
            Faction::Soviet, {Faction::Allies},
            96, 0, 13000,
            {
                {3600,  {UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {7200,  {UnitType::GuardianGI, UnitType::GuardianGI, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::PrismTank, UnitType::Grizzly, UnitType::MirageTank}},
            },
            0, 0, "", false,
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
            "世界革命", "World Revolution",
            "革命的最后一战。盟军三方联军困兽犹斗，全线进攻，解放全世界。",
            "The last battle of the revolution. Three Allied armies fight cornered — attack on all fronts.",
            Faction::Soviet, {Faction::Allies, Faction::Allies, Faction::Allies},
            128, 0, 20000,
            {
                {3600,  {UnitType::Grizzly, UnitType::Grizzly, UnitType::GI, UnitType::GI}},
                {7200,  {UnitType::PrismTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::MirageTank, UnitType::MirageTank, UnitType::Intruder, UnitType::Intruder}},
                {14400, {UnitType::PrismTank, UnitType::PrismTank, UnitType::PrismTank, UnitType::BlackEagle}},
                {18000, {UnitType::PrismTank, UnitType::MirageTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly}},
                {21600, {UnitType::PrismTank, UnitType::MirageTank, UnitType::MirageTank, UnitType::BlackEagle, UnitType::BlackEagle}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "全世界无产者，联合起来！这是最后的斗争。", "Workers of the world, unite! This is the final struggle."},
                {TrigCond::Time, {7200,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,-1,0},
                 {UnitType::Boris, UnitType::SiegeChopper, UnitType::SiegeChopper, UnitType::Apocalypse},
                 "鲍里斯率领攻城直升机与天启编队增援！", "Boris leads a Siege Chopper and Apocalypse reinforcement!"},
                {TrigCond::PlayerAllDead, {1,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "第一路敌军已覆灭！", "The first enemy army has fallen!"},
                {TrigCond::PlayerAllDead, {3,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "最后一路敌军摇摇欲坠，胜利在望！", "The last enemy army is crumbling. Victory is near!"},
            }
        },
        // ==================== 尤里战役（24-31，尤复阵营） ====================
        {
            "心灵觉醒", "Psychic Awakening",
            "尤里脱离苏军独立。建立心灵部队基地，用心灵控制瓦解盟军前哨。",
            "Yuri breaks from the Soviets. Establish a psychic corps base and crush the Allied outpost with mind control.",
            Faction::Yuri, {Faction::Allies},
            64, 0, 10000,
            {
                {2700,  {UnitType::GI, UnitType::GI, UnitType::Grizzly}},
                {6300,  {UnitType::GuardianGI, UnitType::Grizzly, UnitType::Grizzly}},
                {10800, {UnitType::PrismTank, UnitType::Grizzly, UnitType::GI, UnitType::GI}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "尤里已独立：尤里新兵的心灵火焰可焚烧敌兵。", "Yuri is free: Initiates burn foes with psychic fire."},
                {TrigCond::Time, {2700,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,-1,0},
                 {UnitType::MasterMind},
                 "主脑坦克增援抵达：可多重心灵控制敌方单位。", "Master Mind reinforcement: mind-control multiple enemies."},
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "区域内的科技电厂与前哨站可由工程师占领，强化电力与维修。", "Tech power plants and outposts in the area can be captured for power and repairs."},
            }
        },
        {
            "狂兽横行", "Brute Rampage",
            "苏军反扑。以狂兽人近战重击配合狂风坦克，歼灭苏军基地。",
            "The Soviets counterattack. Crush their base with Brutes and Lasher Tanks.",
            Faction::Yuri, {Faction::Soviet},
            96, 0, 12000,
            {
                {3600,  {UnitType::Conscript, UnitType::Rhino, UnitType::Rhino}},
                {7200,  {UnitType::TeslaTrooper, UnitType::TeslaTrooper, UnitType::Rhino, UnitType::FlakTrack}},
                {10800, {UnitType::TeslaTank, UnitType::Rhino, UnitType::Rhino, UnitType::Conscript, UnitType::Conscript}},
                {14400, {UnitType::Boris, UnitType::SiegeChopper, UnitType::TeslaTank, UnitType::Rhino}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "狂兽人近战重甲，可撕碎车辆。", "Brutes tear through vehicles with heavy melee."},
                {TrigCond::Time, {3600,0,0,0,0}, TrigAct::SpawnAt, {0,6,6,-1,0},
                 {UnitType::Brute, UnitType::Brute, UnitType::Brute},
                 "狂兽人小队已空降。", "Brute squad has arrived."},
                {TrigCond::Time, {14400,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "警告：苏军派出英雄鲍里斯与攻城直升机！", "Warning: Soviet hero Boris and Siege Choppers incoming!"},
            }
        },
        {
            "雷鸣深海", "Boomer Depths",
            "盟军舰队封锁海域。建造船厂，以雷鸣潜艇的导弹鱼雷双武器歼灭敌舰。",
            "The Allied fleet blockades the seas. Build a naval yard and sink them with Boomer dual weapons.",
            Faction::Yuri, {Faction::Allies},
            96, 1, 14000,
            {
                {3600,  {UnitType::Destroyer, UnitType::Destroyer}},
                {7200,  {UnitType::Destroyer, UnitType::Aegis, UnitType::Intruder}},
                {12600, {UnitType::Destroyer, UnitType::Destroyer, UnitType::AircraftCarrier}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "雷鸣潜艇：潜射导弹对地、鱼雷对舰，尤里海军主力。", "Boomer: missiles for land, torpedoes for ships."},
            }
        },
        {
            "磁电狩猎", "Magnetron Hunt",
            "盟军两路围剿。用磁电坦克吊起敌方车辆，配合盖特坦克防空。",
            "Two Allied armies hunt you. Lift their vehicles with Magnetrons and screen with Gatling Tanks.",
            Faction::Yuri, {Faction::Allies, Faction::Allies},
            96, 0, 15000,
            {
                {3600,  {UnitType::Grizzly, UnitType::GI, UnitType::GI}},
                {7200,  {UnitType::MirageTank, UnitType::Grizzly, UnitType::Rocketeer, UnitType::Rocketeer}},
                {10800, {UnitType::PrismTank, UnitType::MirageTank, UnitType::Grizzly, UnitType::Intruder}},
                {14400, {UnitType::PrismTank, UnitType::PrismTank, UnitType::MirageTank, UnitType::BlackEagle}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "磁电坦克可吊起敌方车辆使其无法行动。", "Magnetrons lift enemy vehicles, immobilizing them."},
                {TrigCond::Time, {5400,0,0,0,0}, TrigAct::SpawnAt, {0,7,7,-1,0},
                 {UnitType::Magnetron, UnitType::Magnetron, UnitType::GatlingTank},
                 "磁电/盖特小队增援抵达。", "Magnetron/Gatling reinforcement arrived."},
            }
        },
        {
            "飞碟降临", "Disc Descent",
            "苏军重兵集结。飞碟可吸电瘫痪其建筑，配合雷鸣潜艇两面夹击。",
            "Soviet masses their forces. Floating Discs drain their power while Boomers strike from the sea.",
            Faction::Yuri, {Faction::Soviet, Faction::Soviet},
            128, 0, 17000,
            {
                {3600,  {UnitType::Rhino, UnitType::Rhino, UnitType::FlakTrooper, UnitType::FlakTrooper}},
                {7200,  {UnitType::TeslaTank, UnitType::Rhino, UnitType::FlakTrack, UnitType::FlakTrack}},
                {10800, {UnitType::Apocalypse, UnitType::Rhino, UnitType::Rhino, UnitType::Kirov}},
                {14400, {UnitType::Apocalypse, UnitType::Apocalypse, UnitType::TeslaTank, UnitType::MiG, UnitType::MiG}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "飞碟吸电可使敌方建筑瘫痪，注意防空。", "Discs drain power to paralyze enemy bases; mind the AA."},
                {TrigCond::Time, {5400,0,0,0,0}, TrigAct::SpawnAt, {0,9,9,-1,0},
                 {UnitType::FloatingDisc, UnitType::FloatingDisc},
                 "飞碟编队已就位。", "Floating Disc squadron is ready."},
            }
        },
        {
            "混乱之雨", "Chaos Rain",
            "盟军大举进攻。坚守十分钟，混乱无人机的毒气将使敌军自相残杀。",
            "The Allies attack in force. Hold for ten minutes — Chaos Drone gas turns their ranks against each other.",
            Faction::Yuri, {Faction::Allies},
            96, 0, 16000,
            {
                {1800,  {UnitType::Grizzly, UnitType::GI, UnitType::GI}},
                {5400,  {UnitType::PrismTank, UnitType::Grizzly, UnitType::Grizzly, UnitType::GuardianGI}},
                {9000,  {UnitType::MirageTank, UnitType::PrismTank, UnitType::Grizzly, UnitType::Rocketeer, UnitType::Rocketeer}},
                {12600, {UnitType::PrismTank, UnitType::MirageTank, UnitType::MirageTank, UnitType::BlackEagle}},
                {16200, {UnitType::PrismTank, UnitType::PrismTank, UnitType::MirageTank, UnitType::Grizzly, UnitType::Grizzly}},
            },
            1, 18000, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "混乱无人机毒气使敌军陷入自相残杀。", "Chaos Drone gas drives enemies to attack each other."},
                {TrigCond::Time, {3600,0,0,0,0}, TrigAct::SpawnAt, {0,8,8,-1,0},
                 {UnitType::ChaosDrone, UnitType::ChaosDrone},
                 "混乱无人机增援抵达。", "Chaos Drone reinforcement arrived."},
            }
        },
        {
            "基因计划", "Genetic Plan",
            "盟苏联军试图摧毁基因突变器。守护超武，待充能完毕发动基因突变。",
            "Allied-Soviet joint forces target your Genetic Mutator. Defend it until charged, then unleash mutation.",
            Faction::Yuri, {Faction::Allies, Faction::Soviet},
            128, 0, 20000,
            {
                {3600,  {UnitType::Grizzly, UnitType::Rhino, UnitType::GI, UnitType::Conscript}},
                {7200,  {UnitType::PrismTank, UnitType::TeslaTank, UnitType::Grizzly, UnitType::Rhino}},
                {10800, {UnitType::MirageTank, UnitType::Apocalypse, UnitType::PrismTank, UnitType::TeslaTank}},
                {14400, {UnitType::PrismTank, UnitType::Apocalypse, UnitType::Apocalypse, UnitType::BlackEagle, UnitType::MiG}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "基因突变器：把敌方步兵变为狂兽人。", "Genetic Mutator: turn enemy infantry into Brutes."},
                {TrigCond::PlayerBldLost, {0,(int)BldType::GeneticMutator,0,0,0}, TrigAct::Lose, {0,0,0,0,-1}, {},
                 "基因突变器被毁，计划失败。", "Genetic Mutator destroyed. The plan fails."},
            }
        },
        {
            "世界支配", "World Dominion",
            "最终之战。三方联军围攻心灵控制仪。充能完毕，发动心灵支配统御世界。",
            "The final battle. Three armies besiege your Psychic Dominator. Charge it and dominate the world.",
            Faction::Yuri, {Faction::Allies, Faction::Soviet, Faction::China},
            128, 0, 22000,
            {
                {3600,  {UnitType::Grizzly, UnitType::Rhino, UnitType::Type99, UnitType::GI, UnitType::Conscript}},
                {7200,  {UnitType::PrismTank, UnitType::TeslaTank, UnitType::Type99, UnitType::Apocalypse}},
                {10800, {UnitType::MirageTank, UnitType::Apocalypse, UnitType::Type99, UnitType::PrismTank, UnitType::Kirov}},
                {14400, {UnitType::PrismTank, UnitType::Apocalypse, UnitType::Type99, UnitType::BlackEagle, UnitType::MiG, UnitType::Kirov}},
                {18000, {UnitType::PrismTank, UnitType::MirageTank, UnitType::Apocalypse, UnitType::Apocalypse, UnitType::Type99, UnitType::BlackEagle}},
            },
            0, 0, "", false,
            {
                {TrigCond::Always, {0,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "心灵控制仪充能完毕后，可范围控制敌方单位。", "Charge the Psychic Dominator to mind-control all enemies in range."},
                {TrigCond::PlayerAllDead, {1,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "盟军已溃散！", "The Allies have collapsed!"},
                {TrigCond::PlayerAllDead, {2,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "苏军已覆灭！", "The Soviets have fallen!"},
                {TrigCond::PlayerAllDead, {3,0,0,0,0}, TrigAct::Eva, {0,0,0,0,-1}, {},
                 "最后一路敌军摇摇欲坠，世界即将臣服！", "The last army crumbles. The world is yours!"},
            }
        },
    };
}

// ===================== 外部战役加载（assets/campaigns/） =====================
static bool trigCondByName(const char* s, TrigCond& out) {
    if (!s) return false;
    if (!strcmp(s, "Always")) { out = TrigCond::Always; return true; }
    if (!strcmp(s, "Time")) { out = TrigCond::Time; return true; }
    if (!strcmp(s, "PlayerBldLost")) { out = TrigCond::PlayerBldLost; return true; }
    if (!strcmp(s, "PlayerAllDead")) { out = TrigCond::PlayerAllDead; return true; }
    if (!strcmp(s, "UnitInRect")) { out = TrigCond::UnitInRect; return true; }
    if (!strcmp(s, "MoneyBelow")) { out = TrigCond::MoneyBelow; return true; }
    if (!strcmp(s, "Script")) { out = TrigCond::Script; return true; }
    return false;
}
static bool trigActByName(const char* s, TrigAct& out) {
    if (!s) return false;
    if (!strcmp(s, "SpawnAt")) { out = TrigAct::SpawnAt; return true; }
    if (!strcmp(s, "Eva")) { out = TrigAct::Eva; return true; }
    if (!strcmp(s, "GiveMoney")) { out = TrigAct::GiveMoney; return true; }
    if (!strcmp(s, "RevealMap")) { out = TrigAct::RevealMap; return true; }
    if (!strcmp(s, "Win")) { out = TrigAct::Win; return true; }
    if (!strcmp(s, "Lose")) { out = TrigAct::Lose; return true; }
    if (!strcmp(s, "Objective")) { out = TrigAct::Objective; return true; }
    if (!strcmp(s, "Script")) { out = TrigAct::Script; return true; }
    return false;
}

// 逗号分隔单位列表："Conscript,Rhino" → units
static bool parseUnitList(const char* s, std::vector<UnitType>& out) {
    if (!s) return false;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", s);
    bool ok = true;
    for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        UnitType ut;
        if (!unitTypeByName(tok, ut)) { ok = false; break; }
        out.push_back(ut);
    }
    return ok && !out.empty();
}

// 逗号分隔阵营列表："Soviet,Soviet" → aiFactions
static bool parseFactionList(const char* s, std::vector<Faction>& out) {
    if (!s) return false;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", s);
    bool ok = true;
    for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        Faction f;
        if (!factionByName(tok, f)) { ok = false; break; }
        out.push_back(f);
    }
    return ok && !out.empty();
}

// 单关文件：[General] + [Wave.N]... + [Trig.N]...
static bool loadMissionFile(const char* path, MissionDef& md) {
    Ini ini;
    if (!ini.load(path)) return false;
    const Ini::Section* g = ini.find("General");
    if (!g) return false;
    const char* v;
    if (!(v = g->get("Name"))) return false;
    md.name = v;
    if ((v = g->get("NameEn"))) md.nameEn = v;
    if ((v = g->get("Brief"))) md.brief = v;
    if ((v = g->get("BriefEn"))) md.briefEn = v;
    if (!(v = g->get("Faction")) || !factionByName(v, md.playerFaction)) return false;
    if (!(v = g->get("AI")) || !parseFactionList(v, md.aiFactions)) return false;
    md.mapSize = Ini::toInt(g->get("MapSize"), 96);
    md.mapType = Ini::toInt(g->get("MapType"), 0);
    md.money = Ini::toInt(g->get("Money"), 10000);
    md.objective = Ini::toInt(g->get("Objective"), 0);
    md.objectiveTick = Ini::toInt(g->get("ObjectiveTick"), 0);
    if ((v = g->get("MapFile"))) md.mapFile = v;
    md.noStartForce = Ini::toBool(g->get("NoStartForce"), false);
    for (const Ini::Section& sec : ini.sections) {
        if (sec.name.rfind("Wave.", 0) == 0) {
            MissionWave w{};
            w.atTick = Ini::toInt(sec.get("At"), 0);
            if (!(v = sec.get("Units")) || !parseUnitList(v, w.units)) {
                TraceLog(LOG_WARNING, "RA2 campaign: %s [%s] bad Units, wave skipped", path, sec.name.c_str());
                continue;
            }
            md.waves.push_back(std::move(w));
        } else if (sec.name.rfind("Trig.", 0) == 0) {
            Trigger t;
            if (!(v = sec.get("Cond")) || !trigCondByName(v, t.cond)) {
                TraceLog(LOG_WARNING, "RA2 campaign: %s [%s] bad Cond, trigger skipped", path, sec.name.c_str());
                continue;
            }
            if (!(v = sec.get("Act")) || !trigActByName(v, t.act)) {
                TraceLog(LOG_WARNING, "RA2 campaign: %s [%s] bad Act, trigger skipped", path, sec.name.c_str());
                continue;
            }
            for (int i = 0; i < 5; i++) {
                char k[4];
                snprintf(k, sizeof(k), "C%d", i);
                t.c[i] = Ini::toInt(sec.get(k), 0);
                snprintf(k, sizeof(k), "A%d", i);
                t.a[i] = Ini::toInt(sec.get(k), i == 3 ? -1 : 0);
            }
            if ((v = sec.get("Units"))) parseUnitList(v, t.units);
            if ((v = sec.get("Msg"))) t.msg = v;
            if ((v = sec.get("MsgEn"))) t.msgEn = v;
            if ((v = sec.get("Tag"))) t.tag = v;
            t.once = Ini::toBool(sec.get("Once"), true);
            md.triggers.push_back(std::move(t));
        }
    }
    return true;
}

const std::vector<MissionDef>& missionTable() {
    static const std::vector<MissionDef> tbl = [] {
        std::vector<MissionDef> v;
        Ini list;
        if (list.load("assets/campaigns/campaign.ini")) {
            if (const Ini::Section* ms = list.find("Missions")) {
                for (const auto& p : ms->kv) {
                    if (p.first != "Mission") continue; // 重复键 Mission=xxx.ini，顺序即战役顺序
                    char path[256];
                    snprintf(path, sizeof(path), "assets/campaigns/%s", p.second.c_str());
                    MissionDef md{};
                    if (loadMissionFile(path, md)) v.push_back(std::move(md));
                    else TraceLog(LOG_WARNING, "RA2 campaign: %s load failed, mission skipped", path);
                }
            }
        }
        if (v.empty()) {
            TraceLog(LOG_INFO, "RA2 campaign: assets/campaigns not found, using built-in 32 missions");
            v = buildBuiltinMissions();
        } else {
            TraceLog(LOG_INFO, "RA2 campaign: %d missions loaded from assets/campaigns", (int)v.size());
        }
        return v;
    }();
    return tbl;
}

// ===================== 战役导出（--export-assets）：内置 32 关写成 INI 模板 =====================
static const char* trigCondKey(TrigCond c) {
    switch (c) {
        case TrigCond::Always: return "Always";
        case TrigCond::Time: return "Time";
        case TrigCond::PlayerBldLost: return "PlayerBldLost";
        case TrigCond::PlayerAllDead: return "PlayerAllDead";
        case TrigCond::UnitInRect: return "UnitInRect";
        case TrigCond::MoneyBelow: return "MoneyBelow";
        case TrigCond::Script: return "Script";
    }
    return "?";
}
static const char* trigActKey(TrigAct a) {
    switch (a) {
        case TrigAct::SpawnAt: return "SpawnAt";
        case TrigAct::Eva: return "Eva";
        case TrigAct::GiveMoney: return "GiveMoney";
        case TrigAct::RevealMap: return "RevealMap";
        case TrigAct::Win: return "Win";
        case TrigAct::Lose: return "Lose";
        case TrigAct::Objective: return "Objective";
        case TrigAct::Script: return "Script";
    }
    return "?";
}
static void writeUnitList(FILE* f, const char* key, const std::vector<UnitType>& units) {
    fprintf(f, "%s=", key);
    for (size_t i = 0; i < units.size(); i++)
        fprintf(f, "%s%s", i ? "," : "", unitTypeKey(units[i]));
    fprintf(f, "\n");
}

void exportCampaigns(const char* dir) {
    MakeDirectory("assets");
    MakeDirectory(dir);
    std::vector<MissionDef> v = buildBuiltinMissions(); // 导出内置模板（与外部文件无关）
    char path[256];
    // 列表文件：战役顺序 = Mission 键顺序；中 8 / 盟 8 / 苏 8 / 尤里 8（选关页四阵营分页）
    snprintf(path, sizeof(path), "%s/campaign.ini", dir);
    if (FILE* f = fopen(path, "wb")) {
        fprintf(f, "; OpenRA2 campaign list - one Mission=<file> per line, order = campaign order.\n");
        fprintf(f, "; First 8 = China tab, next 8 = Allies tab, next 8 = Soviet tab, last 8 = Yuri tab.\n[Missions]\n");
        for (size_t i = 0; i < v.size(); i++) fprintf(f, "Mission=mission%02d.ini\n", (int)i + 1);
        fclose(f);
        TraceLog(LOG_INFO, "RA2 export: %s written", path);
    }
    for (size_t i = 0; i < v.size(); i++) {
        const MissionDef& md = v[i];
        snprintf(path, sizeof(path), "%s/mission%02d.ini", dir, (int)i + 1);
        FILE* f = fopen(path, "wb");
        if (!f) { TraceLog(LOG_WARNING, "RA2 export: cannot write %s", path); continue; }
        fprintf(f, "; OpenRA2 mission - see assets/README.txt for key reference\n");
        fprintf(f, "[General]\n");
        fprintf(f, "Name=%s\n", md.name.c_str());
        fprintf(f, "NameEn=%s\n", md.nameEn.c_str());
        fprintf(f, "Brief=%s\n", md.brief.c_str());
        fprintf(f, "BriefEn=%s\n", md.briefEn.c_str());
        fprintf(f, "Faction=%s\n", factionKey(md.playerFaction));
        fprintf(f, "AI=");
        for (size_t j = 0; j < md.aiFactions.size(); j++)
            fprintf(f, "%s%s", j ? "," : "", factionKey(md.aiFactions[j]));
        fprintf(f, "\n");
        fprintf(f, "MapSize=%d\nMapType=%d\nMoney=%d\n", md.mapSize, md.mapType, md.money);
        fprintf(f, "Objective=%d\nObjectiveTick=%d\n", md.objective, md.objectiveTick);
        if (!md.mapFile.empty()) fprintf(f, "MapFile=%s\n", md.mapFile.c_str());
        if (md.noStartForce) fprintf(f, "NoStartForce=yes\n");
        for (size_t j = 0; j < md.waves.size(); j++) {
            fprintf(f, "\n[Wave.%d]\nAt=%d\n", (int)j + 1, md.waves[j].atTick);
            writeUnitList(f, "Units", md.waves[j].units);
        }
        for (size_t j = 0; j < md.triggers.size(); j++) {
            const Trigger& t = md.triggers[j];
            fprintf(f, "\n[Trig.%d]\n", (int)j + 1);
            fprintf(f, "Cond=%s\n", trigCondKey(t.cond));
            for (int k = 0; k < 5; k++) fprintf(f, "C%d=%d\n", k, t.c[k]);
            fprintf(f, "Act=%s\n", trigActKey(t.act));
            for (int k = 0; k < 5; k++) fprintf(f, "A%d=%d\n", k, t.a[k]);
            if (!t.units.empty()) writeUnitList(f, "Units", t.units);
            if (!t.msg.empty()) fprintf(f, "Msg=%s\n", t.msg.c_str());
            if (!t.msgEn.empty()) fprintf(f, "MsgEn=%s\n", t.msgEn.c_str());
            if (!t.tag.empty()) fprintf(f, "Tag=%s\n", t.tag.c_str());
            if (!t.once) fprintf(f, "Once=no\n");
        }
        fclose(f);
    }
    TraceLog(LOG_INFO, "RA2 export: %d missions written to %s", (int)v.size(), dir);
}
