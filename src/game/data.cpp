#include "game/data.h"
#include "core/ini.h"
#include "raylib.h"
#include <algorithm>
#include <cstring>
#include <string>

// 阵营位掩码
static constexpr uint8_t FA = 1 << (int)Faction::Allies;
static constexpr uint8_t FS = 1 << (int)Faction::Soviet;
static constexpr uint8_t FC = 1 << (int)Faction::China;
static constexpr uint8_t FY = 1 << (int)Faction::Yuri;
static constexpr uint8_t ALLF = FA | FS | FC | FY;
static constexpr uint8_t FAF = FA | FC; // 盟军系（含融合中国共享空指）
static constexpr uint8_t FSF = FS | FC; // 苏军系（含融合中国共享雷达）

// 官方超武冷却：doc 分钟 × 30fps × 60
static constexpr int kSwMin = 30 * 60;

// 武器预设
static WeaponDef withWH(WeaponDef w, WeaponDef::Warhead h) { w.warhead = h; return w; }
static WeaponDef withReport(WeaponDef w, const char* r) { w.report = r; return w; }
static WeaponDef wNone() { return WeaponDef{0, 0, 0, false, false, "shell", 1, 1, 1}; }
static WeaponDef wRifle() { return withWH(WeaponDef{8, 4, 20, false, true, "bullet", 1.0f, 0.5f, 0.4f}, WeaponDef::Warhead::SmallArms); }
static WeaponDef wGiRifle() { return withReport(wRifle(), "GIAttack"); }
static WeaponDef wConscriptRifle() { return withReport(wRifle(), "ConscriptAttack"); }
static WeaponDef wHeavyRifle() { return withWH(WeaponDef{14, 5, 18, false, true, "bullet", 1.0f, 0.6f, 0.5f}, WeaponDef::Warhead::SmallArms); }
static WeaponDef wFlak() { return withWH(WeaponDef{12, 6, 24, true, true, "flak", 1.0f, 0.7f, 0.5f}, WeaponDef::Warhead::HE); }
static WeaponDef wTeslaBolt() { return withWH(WeaponDef{30, 5, 40, false, true, "tesla", 1.2f, 1.0f, 0.8f}, WeaponDef::Warhead::AP); }
static WeaponDef wSniper() { return withWH(WeaponDef{60, 8, 70, false, true, "bullet", 1.0f, 0.05f, 0.05f}, WeaponDef::Warhead::HollowPoint); }
// 辐射工兵：辐射射线，对步兵致命；部署后为区域辐射
static WeaponDef wRadiation() { return withWH(WeaponDef{50, 5, 30, false, true, "rad", 2.5f, 0.3f, 0.1f}, WeaponDef::Warhead::Radiation); }
// 超时空抹除：不造成伤害，命中叠加"抹除进度"（special: chrono）
static WeaponDef wChrono() { return WeaponDef{1, 6, 20, false, true, "chrono", 1.0f, 1.0f, 0.0f}; }
static WeaponDef wTankGun(int dmg, int rng, int cd) { return withWH(WeaponDef{dmg, rng, cd, false, true, "shell", 0.6f, 1.0f, 0.8f}, WeaponDef::Warhead::AP); }
static WeaponDef wPrism() { return withWH(WeaponDef{120, 7, 45, false, true, "prism", 1.0f, 0.8f, 1.4f}, WeaponDef::Warhead::HE); }
static WeaponDef wV3() { return withWH(WeaponDef{150, 14, 220, false, true, "missile", 0.5f, 1.0f, 1.5f, false, 1.5f}, WeaponDef::Warhead::HE); }
static WeaponDef wBomb(int dmg) { return withWH(WeaponDef{dmg, 3, 40, false, true, "shell", 0.5f, 1.0f, 1.5f}, WeaponDef::Warhead::HE); }   // 航弹：对建筑强
static WeaponDef wAirMissile() { return withWH(WeaponDef{80, 4, 30, false, true, "missile", 0.6f, 1.2f, 1.0f}, WeaponDef::Warhead::HE); } // 米格空空/地导弹
static WeaponDef wNavalGun() { return withWH(WeaponDef{60, 8, 50, false, true, "naval", 0.6f, 1.1f, 1.0f}, WeaponDef::Warhead::HE); }      // 舰炮：对舰艇/沿岸
static WeaponDef wTorpedo() { return withWH(WeaponDef{100, 7, 60, false, true, "torpedo", 0, 1.2f, 0.8f, true}, WeaponDef::Warhead::AP); } // 鱼雷：仅水上目标
static WeaponDef wAegisAA() { return withWH(WeaponDef{50, 10, 22, true, false, "missile", 0, 1.0f, 0}, WeaponDef::Warhead::HE); }          // 神盾防空：纯对空
static WeaponDef wPatriot() { return withWH(WeaponDef{45, 9, 20, true, false, "missile", 0, 1.2f, 0}, WeaponDef::Warhead::HE); }           // 爱国者飞弹：纯对空
static WeaponDef wKirovBomb() { return withWH(WeaponDef{300, 3, 55, false, true, "shell", 0.6f, 1.0f, 1.6f, false, 2.0f}, WeaponDef::Warhead::HE); } // 基洛夫航空炸弹：范围重击
static WeaponDef wDreadMissile() { return withWH(WeaponDef{200, 16, 210, false, true, "missile", 0.4f, 1.0f, 1.4f, false, 1.5f}, WeaponDef::Warhead::HE); } // 无畏舰远程导弹
static WeaponDef wIFVMissile() { return withWH(WeaponDef{25, 6, 50, true, true, "missile", 0.8f, 1.0f, 0.8f}, WeaponDef::Warhead::HE); }
static WeaponDef wTanyaGun() { return withWH(WeaponDef{120, 6, 10, false, true, "bullet", 1.5f, 0.3f, 1.0f}, WeaponDef::Warhead::HollowPoint); }
static WeaponDef wHornet() { return WeaponDef{80, 18, 150, false, true, "missile", 0.8f, 1.2f, 1.0f, false, 1.0f}; } // 航母舰载机打击
static WeaponDef wRocketGun() { return WeaponDef{10, 5, 18, true, true, "bullet", 1.0f, 0.6f, 0.5f}; }     // 火箭飞行兵卡宾枪
static WeaponDef wGGIRifle() { return WeaponDef{6, 4, 22, false, true, "bullet", 1.0f, 0.4f, 0.3f}; }      // 重装大兵（未部署）：冲锋枪
static WeaponDef wDroneBite() { return WeaponDef{30, 1, 8, false, true, "bullet", 0.2f, 2.0f, 0.0f}; }     // 恐怖机器人啃噬：反车辆
static WeaponDef wIvanBomb() { return WeaponDef{1, 2, 90, false, true, "shell", 0, 0, 0}; }                // 疯狂伊文：安放炸弹（特殊处理）

// ---- RA2 补全：新单位武器 ----
static WeaponDef wWarMinerGun() { return WeaponDef{10, 4, 20, false, true, "bullet", 1.0f, 0.5f, 0.3f}; } // 武装采矿车机枪
static WeaponDef wTDGun() { return withWH(WeaponDef{90, 5, 50, false, true, "shell", 0.15f, 2.2f, 0.3f}, WeaponDef::Warhead::AP); }       // 坦克杀手：反装甲专精
static WeaponDef wTerrorBomb() { return WeaponDef{150, 1, 60, false, true, "shell", 1.0f, 1.0f, 1.0f, false, 2.0f}; } // 恐怖分子自爆
static WeaponDef wDemoBomb() { return WeaponDef{400, 1, 120, false, true, "shell", 0.8f, 1.2f, 1.2f, false, 3.0f}; }  // 自爆卡车
static WeaponDef wSonic() { return withWH(WeaponDef{40, 6, 40, false, true, "shell", 0.5f, 1.2f, 0.5f, true}, WeaponDef::Warhead::HE); }  // 海豚音波：仅水上目标
static WeaponDef wSquidGrab() { return WeaponDef{15, 1, 30, false, true, "shell", 0.0f, 1.0f, 0.0f, true}; } // 乌贼缠绕：定身+持续伤害
static WeaponDef wFortressGun() { return WeaponDef{20, 5, 18, false, true, "bullet", 1.0f, 0.8f, 0.6f}; } // 战斗要塞机枪
static WeaponDef wNighthawkGun() { return WeaponDef{35, 4, 40, false, true, "bullet", 1.0f, 0.4f, 0.3f}; } // 夜鹰舱门机枪（rulesmd）
static WeaponDef wHornetBomb() { return WeaponDef{80, 3, 40, false, true, "shell", 0.5f, 1.0f, 1.2f, false, 0.5f}; } // 舰载机航弹
// ---- P6：海豹/尤里/偷科技武器 ----
static WeaponDef wSMG() { return withWH(WeaponDef{25, 4, 12, false, true, "bullet", 1.6f, 0.15f, 0.05f}, WeaponDef::Warhead::SmallArms); }      // 海豹冲锋枪：反步兵专精
static WeaponDef wPsychic() { return withWH(WeaponDef{1, 7, 90, false, true, "psi", 1.0f, 1.0f, 0.0f}, WeaponDef::Warhead::Psychic); }         // 心灵波：不造成伤害，命中夺取控制权（特殊处理）

// ---- 尤复阵营：尤里单位武器 ----
static WeaponDef wPsychicFire() { return WeaponDef{25, 5, 25, false, true, "psi", 2.0f, 0.3f, 0.2f}; }    // 尤里新兵：心灵火焰，反步兵
static WeaponDef wBruteFist() { return WeaponDef{45, 1, 20, false, true, "bullet", 2.5f, 0.8f, 0.3f}; }   // 狂兽人：近战重击，反车辆
static WeaponDef wVirusRifle() { return WeaponDef{80, 9, 60, false, true, "bullet", 2.0f, 0.1f, 0.05f}; } // 病毒狙击手：超远程反步兵
static WeaponDef wLasherGun() { return withWH(WeaponDef{65, 5, 60, false, true, "shell", 0.7f, 1.0f, 0.7f}, WeaponDef::Warhead::AP); }    // 狂风坦克：主炮
static WeaponDef wGatling(int dmg, int cd) { return WeaponDef{dmg, 6, cd, true, true, "bullet", 1.0f, 0.8f, 0.4f}; } // 盖特：防空对地速射
static WeaponDef wMagnetron() { return WeaponDef{1, 7, 80, false, true, "shell", 0, 0.5f, 0}; }           // 磁电坦克：吊起车辆（特殊处理）
static WeaponDef wMasterMind() { return withWH(WeaponDef{1, 8, 70, false, true, "psi", 1.0f, 1.0f, 0.0f}, WeaponDef::Warhead::Psychic); }      // 主脑：多重心灵控制
static WeaponDef wDiscBeam() { return WeaponDef{90, 5, 80, false, true, "shell", 1.0f, 1.2f, 1.5f}; }     // 飞碟激光（rulesmd DiskLaser）
static WeaponDef wBoomerMissile() { return WeaponDef{180, 14, 200, false, true, "missile", 0.5f, 1.0f, 1.4f, false, 1.5f}; } // 雷鸣潜艇导弹
static WeaponDef wGatlingCannonGun() { return WeaponDef{30, 7, 15, true, true, "bullet", 1.0f, 0.7f, 0.3f}; } // 盖特机炮：防空对地

// ---- 尤复补全：YR 新增单位武器 ----
static WeaponDef wBorisAK() { return withWH(WeaponDef{60, 5, 10, false, true, "bullet", 1.8f, 0.3f, 0.15f}, WeaponDef::Warhead::SmallArms); }     // 鲍里斯 AK-47：反步兵专精
static WeaponDef wSiegeChopperMG() { return WeaponDef{15, 5, 20, true, true, "bullet", 1.0f, 0.5f, 0.3f}; } // 攻城直升机飞行机枪
static WeaponDef wChaosGas() { return WeaponDef{5, 3, 60, false, true, "shell", 0.5f, 0.5f, 0.1f, false, 2.0f}; } // 混乱毒气：范围伤害+混乱
static WeaponDef wPsychicTowerMC() { return WeaponDef{1, 7, 80, false, true, "psi", 1.0f, 1.0f, 0.0f}; }    // 心灵控制塔：自动心灵控制
static WeaponDef wYuriPrimeMC() { return withWH(WeaponDef{1, 12, 55, false, true, "psi", 1.0f, 1.0f, 0.0f}, WeaponDef::Warhead::Psychic); } // 尤里首脑：超远心控

// ---- RA2 补全：精英武器（RA2 原作：精英军衔武器质变） ----
static WeaponDef ewGrizzly() { return WeaponDef{80, 5, 45, false, true, "shell", 0.7f, 1.1f, 0.9f}; }
static WeaponDef ewRhino() { return WeaponDef{110, 5, 50, false, true, "shell", 0.7f, 1.1f, 0.9f}; }
static WeaponDef ewType99() { return WeaponDef{120, 6, 45, false, true, "shell", 0.7f, 1.1f, 0.9f}; }
static WeaponDef ewApoc() { return WeaponDef{130, 6, 60, true, true, "missile", 0.9f, 1.4f, 1.1f}; }
static WeaponDef ewPrism() { return WeaponDef{70, 7, 45, false, true, "prism", 1.0f, 0.9f, 1.5f, false, 1.5f}; }
static WeaponDef ewTeslaTank() { return WeaponDef{45, 5, 32, false, true, "tesla", 1.3f, 1.1f, 0.9f}; }
static WeaponDef ewV3() { return WeaponDef{200, 14, 190, false, true, "missile", 0.5f, 1.0f, 1.6f, false, 2.5f}; }
static WeaponDef ewTanya() { return WeaponDef{150, 6, 6, false, true, "bullet", 1.6f, 0.4f, 1.1f}; }
static WeaponDef ewRifle(int dmg) { return WeaponDef{dmg, 4, 16, false, true, "bullet", 1.1f, 0.6f, 0.5f}; }
static WeaponDef ewDestroyer() { return WeaponDef{90, 8, 40, false, true, "naval", 0.7f, 1.2f, 1.1f}; }
static WeaponDef ewDread() { return WeaponDef{300, 16, 180, false, true, "missile", 0.4f, 1.0f, 1.5f, false, 2.0f}; }
static WeaponDef ewKirov() { return WeaponDef{450, 3, 45, false, true, "shell", 0.6f, 1.0f, 1.7f, false, 2.5f}; }
static WeaponDef ewWarMiner() { return WeaponDef{16, 4, 16, false, true, "bullet", 1.1f, 0.6f, 0.4f}; }

// 精英武器实例表（供 UnitDef.elite 取地址，函数内 static 保证常量初始化后有效）
template <WeaponDef (*F)()> const WeaponDef* eliteOf() { static const WeaponDef w = F(); return &w; }

// 重装大兵部署后：反装甲炮（不可移动、不可被碾压）
// 部署武器为可写静态：rules.ini [DeployWeapon.*] 可覆盖
static WeaponDef wGgiDeploy{40, 6, 30, false, true, "missile", 0.3f, 1.6f, 0.4f};
static WeaponDef wGiDeploy{12, 5, 18, false, true, "bullet", 1.2f, 0.5f, 0.4f};
const WeaponDef& ggiDeployedWeapon() { return wGgiDeploy; }

// 美国大兵部署（沙袋工事）：射程与伤害提升，不可移动、不可被碾压
const WeaponDef& giDeployedWeapon() { return wGiDeploy; }

// 攻城直升机部署后：远程重炮（不可移动，对建筑/车辆强力，小范围溅射）
// 部署武器为可写静态：rules.ini [DeployWeapon.SiegeChopper] 可覆盖
static WeaponDef wSiegeDeploy{120, 12, 110, false, true, "shell", 0.4f, 1.6f, 1.8f, false, 1.5f};
const WeaponDef& siegeChopperDeployedWeapon() { return wSiegeDeploy; }

// ===================== 单位表 =====================
// 注：尾部追加 ammo 字段（0=无限）
// 表为可写静态：启动时 loadRules() 用 assets/rules/rules.ini 逐项覆盖
static UnitDef g_units[(int)UnitType::COUNT] = {
    // type, name, cost, btime, hp, speed, sight, armor, move, weapon, factions, prereq, ammo
    {UnitType::MCV,        "基地车",    3000, 500, 1000, 24, 5, Armor::Heavy, MoveType::Vehicle, wNone(), ALLF, BldType::WarFactory, 0},
    {UnitType::Harvester,  "采矿车",    1400, 280, 1000, 20, 4, Armor::Heavy, MoveType::Vehicle, wNone(), FC, BldType::OreRefinery},
    {UnitType::GI,         "美国大兵",   200, 60,  125, 14, 5, Armor::None, MoveType::Infantry, wGiRifle(), FA, BldType::COUNT},
    {UnitType::Conscript,  "动员兵",    100, 45,  125, 14, 5, Armor::None, MoveType::Infantry, wConscriptRifle(), FS, BldType::COUNT},
    {UnitType::PLA,        "解放军",    150, 50,  120, 13, 5, Armor::None, MoveType::Infantry, wHeavyRifle(), FC, BldType::COUNT},
    {UnitType::Engineer,   "工程师",    500, 120, 75,  14, 4, Armor::None, MoveType::Infantry, wNone(), ALLF, BldType::COUNT},
    {UnitType::AttackDog,  "军犬",      200, 50,  80,  8,  6, Armor::None, MoveType::Infantry, WeaponDef{50,1,15,false,true,"bullet",2.0f,0,0}, ALLF, BldType::COUNT},
    {UnitType::Spy,        "间谍",      1000,200, 75,  14, 6, Armor::None, MoveType::Infantry, wNone(), FA | FC, BldType::BattleLab},
    {UnitType::FlakTrooper,"高射步兵",   300, 70,  100, 14, 5, Armor::None, MoveType::Infantry, wFlak(), FS, BldType::COUNT},
    {UnitType::TeslaTrooper,"磁暴步兵",  500, 100, 160, 16, 5, Armor::Light, MoveType::Infantry, wTeslaBolt(), FS, BldType::Radar},
    {UnitType::Sniper,     "狙击手",    600, 110, 90,  16, 8, Armor::None, MoveType::Infantry, wSniper(), FA, BldType::Radar, 0, 0, nullptr, Country::UK},
    {UnitType::Tanya,      "谭雅",      1500,300, 200, 12, 8, Armor::None, MoveType::Infantry, wTanyaGun(), FA, BldType::BattleLab, 0, 0, eliteOf<ewTanya>()},
    {UnitType::Desolator,  "辐射工兵",   600, 110, 150, 14, 6, Armor::None, MoveType::Infantry, wRadiation(), FS, BldType::Radar, 0, 0, nullptr, Country::Iraq},
    {UnitType::Chrono,     "超时空军团兵",1500,300, 125, 14, 8, Armor::None, MoveType::Infantry, wChrono(), FA, BldType::BattleLab},
    {UnitType::GuardianGI, "重装大兵",   400, 90,  150, 15, 5, Armor::None, MoveType::Infantry, wGGIRifle(), FA, BldType::COUNT},
    {UnitType::CrazyIvan,  "疯狂伊文",   600, 110, 120, 12, 5, Armor::None, MoveType::Infantry, wIvanBomb(), FS, BldType::Radar},
    {UnitType::Grizzly,    "灰熊坦克",   700, 150, 300, 12, 6, Armor::Heavy, MoveType::Vehicle, withReport(wTankGun(65, 5, 60), "GrizzlyTankAttack"), FA, BldType::COUNT, 0, 0, eliteOf<ewGrizzly>()},
    {UnitType::Rhino,      "犀牛坦克",   900, 170, 400, 14, 6, Armor::Heavy, MoveType::Vehicle, withReport(wTankGun(90, 5, 65), "RhinoTankAttack"), FS, BldType::COUNT, 0, 0, eliteOf<ewRhino>()},
    {UnitType::Type99,     "99式坦克",  1200,190, 500, 12, 6, Armor::Heavy, MoveType::Vehicle, withReport(wTankGun(100, 6, 60), "RhinoTankAttack"), FC, BldType::COUNT, 0, 0, eliteOf<ewType99>()},
    {UnitType::FlakTrack,  "高射炮车",   500, 110, 180, 10, 6, Armor::Light, MoveType::Vehicle, wFlak(), FS, BldType::COUNT, 0, 0},
    {UnitType::IFV,        "多功能步兵车",600, 110, 200, 8,  7, Armor::Light, MoveType::Vehicle, wIFVMissile(), FA, BldType::COUNT, 0, 1},
    {UnitType::PrismTank,  "光棱坦克",   1200,240, 250, 16, 7, Armor::Light, MoveType::Vehicle, wPrism(), FA, BldType::BattleLab, 0, 0, eliteOf<ewPrism>()},
    {UnitType::TeslaTank,  "磁能坦克",   1200,240, 300, 14, 7, Armor::Heavy, MoveType::Vehicle, wTeslaBolt(), FS, BldType::BattleLab, 0, 0, eliteOf<ewTeslaTank>(), Country::Russia},
    {UnitType::MirageTank, "幻影坦克",   1000,220, 200, 12, 7, Armor::Light, MoveType::Vehicle, withReport(wTankGun(100, 7, 70), "MirageTankAttack"), FA | FC, BldType::BattleLab},
    {UnitType::V3Launcher, "V3火箭车",  800, 200, 150, 18, 6, Armor::Light, MoveType::Vehicle, wV3(), FS | FC, BldType::Radar, 0, 0, eliteOf<ewV3>()},
    {UnitType::Apocalypse, "天启坦克",   1750,350, 800, 18, 7, Armor::Heavy, MoveType::Vehicle, withReport(WeaponDef{100,6,80,true,true,"shell",0.8f,1.2f,1.0f}, "ApocalypseAttackGround"), FS | FC, BldType::BattleLab, 0, 0, eliteOf<ewApoc>()},
    {UnitType::TerrorDrone,"恐怖机器人", 500, 100, 100, 6,  5, Armor::Light, MoveType::Vehicle, wDroneBite(), FS | FC, BldType::Radar},
    // 空军：speed 越小越快；ammo 打完返航装填
    {UnitType::Intruder,   "入侵者战机", 1200,240, 150, 3,  6, Armor::Light, MoveType::Air, wBomb(150), FA | FC, BldType::COUNT, 1},
    {UnitType::MiG,        "米格战机",   1200,240, 260, 2,  6, Armor::Light, MoveType::Air, wAirMissile(), FS, BldType::COUNT, 2},
    {UnitType::BlackEagle, "黑鹰战机",   1200,300, 200, 2,  7, Armor::Light, MoveType::Air, wBomb(250), FA | FC, BldType::COUNT, 1, 0, nullptr, Country::Korea},
    // 基洛夫：ammo=0 不返航（自由飞行轰炸）；火箭飞行兵：兵营生产的空中步兵
    {UnitType::Kirov,      "基洛夫空艇", 2000,400, 2000,5,  6, Armor::Heavy, MoveType::Air, wKirovBomb(), FS | FC, BldType::BattleLab, 0, 0, eliteOf<ewKirov>()},
    {UnitType::Rocketeer,  "火箭飞行兵", 600, 120, 125, 6,  7, Armor::None, MoveType::Air, wRocketGun(), FA, BldType::COUNT, 0},
    // 海军：speed 越小越快；船厂生产
    {UnitType::Destroyer,  "驱逐舰",     1000,240, 600, 16, 7, Armor::Heavy, MoveType::Naval, wNavalGun(), FA, BldType::COUNT, 0, 0, eliteOf<ewDestroyer>()},
    {UnitType::Typhoon,    "台风潜艇",   1000,240, 600, 14, 6, Armor::Heavy, MoveType::Naval, wTorpedo(), FS | FC, BldType::COUNT},
    {UnitType::Aegis,      "中华神盾舰", 1200,260, 800, 14, 9, Armor::Heavy, MoveType::Naval, wAegisAA(), FC, BldType::Radar},
    {UnitType::SeaScorpion,"海蝎",       600, 130, 400, 10, 7, Armor::Light, MoveType::Naval, wFlak(), FS | FC, BldType::COUNT},
    {UnitType::Dreadnought,"无畏级战舰", 2000,400, 800, 18, 8, Armor::Heavy, MoveType::Naval, wDreadMissile(), FS, BldType::BattleLab, 0, 0, eliteOf<ewDread>()},
    {UnitType::AircraftCarrier,"航空母舰",2000,400, 800, 16, 8, Armor::Heavy, MoveType::Naval, wHornet(), FA, BldType::BattleLab, 0, 3}, // 载机量 3（cargo 计舰载机）
    {UnitType::AmphTransport,"两栖运输船",900, 220, 300, 12, 5, Armor::Light, MoveType::Amphibious, wNone(), ALLF, BldType::COUNT, 0, 5},
    // ---- RA2 补全：采矿车阵营差异化 ----
    {UnitType::ChronoMiner,"超时空采矿车",1400,280, 1000, 20, 4, Armor::Heavy, MoveType::Vehicle, wNone(), FA, BldType::OreRefinery},
    {UnitType::WarMiner,   "武装采矿车", 1400,280, 1000, 20, 4, Armor::Heavy, MoveType::Vehicle, wWarMinerGun(), FS, BldType::OreRefinery, 0, 0, eliteOf<ewWarMiner>()},
    // ---- RA2 补全：国家特色单位 ----
    {UnitType::TankDestroyer,"坦克杀手", 900, 170, 400, 12, 6, Armor::Heavy, MoveType::Vehicle, wTDGun(), FA, BldType::COUNT, 0, 0, nullptr, Country::Germany},
    {UnitType::Terrorist,  "恐怖分子",   200, 45,  60, 12, 5, Armor::None, MoveType::Infantry, wTerrorBomb(), FS, BldType::COUNT, 0, 0, nullptr, Country::Cuba},
    {UnitType::DemoTruck,  "自爆卡车",   1500,300, 150, 14, 6, Armor::Light, MoveType::Vehicle, wDemoBomb(), FS, BldType::Radar, 0, 0, nullptr, Country::Libya},
    // ---- RA2 补全：运输/海军/特殊 ----
    {UnitType::Nighthawk,  "夜鹰直升机", 1000,220, 175, 4,  7, Armor::Light, MoveType::Air, wNighthawkGun(), FA, BldType::COUNT, 0, 5},
    {UnitType::Dolphin,    "海豚",       500, 110, 200, 8,  7, Armor::Light, MoveType::Naval, wSonic(), FA, BldType::COUNT},
    {UnitType::Squid,      "巨型乌贼",   1000,220, 200, 10, 6, Armor::Light, MoveType::Naval, wSquidGrab(), FS, BldType::Radar},
    {UnitType::RobotTank,  "遥控坦克",   600, 150, 180, 10, 6, Armor::Light, MoveType::Amphibious, wTankGun(65, 5, 60), FA, BldType::RobotControl},
    {UnitType::BattleFortress,"战斗要塞",2000,400, 600, 14, 7, Armor::Heavy, MoveType::Vehicle, wFortressGun(), FA, BldType::BattleLab, 0, 5},
    {UnitType::Hornet,     "舰载机",     0,   0,   100, 3,  5, Armor::Light, MoveType::Air, wHornetBomb(), 0, BldType::COUNT, 1},
    // ---- P6：海豹部队/尤里/偷科技单位 ----
    // 海豹部队：可游泳渡水（寻路域 2），冲锋枪反步兵，近身 C4 爆破建筑/舰船
    {UnitType::NavySEAL,   "海豹部队",   1000,200, 125, 12, 7, Armor::None, MoveType::Infantry, wSMG(), FA, BldType::AirForceCmd},
    // 尤里：心灵控制敌方地面单位（夺取控制权；自身死亡则被控单位复原）
    {UnitType::Yuri,       "尤里",       1200,240, 100, 14, 8, Armor::None, MoveType::Infantry, wPsychic(), FY, BldType::BattleLab},
    // 超时空突击队：渗透盟军作战实验室解锁；传送机动 + 冲锋枪 + C4
    {UnitType::ChronoCommando,"超时空突击队",2000,300, 150, 12, 8, Armor::None, MoveType::Infantry, wSMG(), ALLF, BldType::BattleLab},
    // 心灵突击队：渗透苏军/中国作战实验室解锁；心灵控制 + C4
    {UnitType::PsiCommando,"心灵突击队", 1500,280, 125, 13, 8, Armor::None, MoveType::Infantry, wPsychic(), ALLF, BldType::BattleLab},
    // ---- 尤复阵营：尤里专属单位 ----
    {UnitType::Initiate,   "尤里新兵",   200, 60,  100, 14, 5, Armor::None, MoveType::Infantry, wPsychicFire(), FY, BldType::COUNT},
    {UnitType::Brute,      "狂兽人",     500, 110, 250, 12, 5, Armor::Heavy, MoveType::Infantry, wBruteFist(), FY, BldType::Barracks},
    {UnitType::Virus,      "病毒狙击手", 600, 110, 90,  16, 8, Armor::None, MoveType::Infantry, wVirusRifle(), FY, BldType::Radar},
    {UnitType::LasherTank, "狂风坦克",   700, 150, 300, 12, 6, Armor::Heavy, MoveType::Vehicle, wLasherGun(), FY, BldType::COUNT},
    {UnitType::GatlingTank,"盖特坦克",   600, 130, 210, 10, 6, Armor::Light, MoveType::Vehicle, wGatling(25, 16), FY, BldType::COUNT},
    {UnitType::Magnetron,  "磁电坦克",   1000,240, 150, 14, 7, Armor::Light, MoveType::Vehicle, wMagnetron(), FY, BldType::Radar},
    {UnitType::MasterMind, "主脑坦克",   1750,350, 500, 14, 7, Armor::Heavy, MoveType::Vehicle, wMasterMind(), FY, BldType::BattleLab},
    {UnitType::FloatingDisc,"飞碟",      1750,350, 600, 5,  7, Armor::Heavy, MoveType::Air, wDiscBeam(), FY, BldType::BattleLab, 0},
    {UnitType::Boomer,     "雷鸣潜艇",   2000,400, 1200, 16, 8, Armor::Heavy, MoveType::Naval, wBoomerMissile(), FY, BldType::BattleLab},
    // ---- 尤复补全：YR 新增单位 ----
    // 鲍里斯：苏军英雄（YR 替代尤里），AK-47 反步兵，可呼叫米格空袭建筑
    {UnitType::Boris,      "鲍里斯",     2000,300, 200, 12, 8, Armor::None, MoveType::Infantry, wBorisAK(), FS, BldType::BattleLab},
    // 攻城直升机：飞行机枪 / 部署后远程炮击（战车工厂生产）
    {UnitType::SiegeChopper,"攻城直升机", 1400,240, 300, 4, 7, Armor::Light, MoveType::Air, wSiegeChopperMG(), FS, BldType::Radar, 0},
    // 混乱机器人：尤里地面无人战车（Chaos Drone），释放毒气使敌军自相残杀（战车工厂生产）
    {UnitType::ChaosDrone, "混乱机器人",  800, 160, 200, 5, 6, Armor::Light, MoveType::Vehicle, wChaosGas(), FY, BldType::BattleLab, 0},
    // 奴隶：尤里采矿步兵；奴隶矿车：尤里采矿车并可部署为卸货点
    {UnitType::Slave,      "奴隶",        30,  20,  70, 12, 4, Armor::None, MoveType::Infantry, wNone(), 0, BldType::OreRefinery},
    {UnitType::SlaveMiner,  "奴隶矿车",  1500, 280, 2000, 18, 5, Armor::Heavy, MoveType::Vehicle, wNone(), FY, BldType::OreRefinery},
    // YR：尤里英雄；渗透苏军实验室解锁的超时空伊文
    {UnitType::YuriPrime,  "尤里首脑",   1500, 300, 150, 10, 9, Armor::Flak, MoveType::Infantry, wYuriPrimeMC(), FY, BldType::BattleLab},
    {UnitType::ChronoIvan, "超时空伊文", 1200, 240, 100, 12, 8, Armor::None, MoveType::Infantry, wIvanBomb(), ALLF, BldType::BattleLab},
};

// ===================== 建筑表 =====================
static BldDef g_blds[(int)BldType::COUNT] = {
    // type, name, cost, btime, hp, w,h, power, sight, weapon, factions, prereq, capturable
    // w,h = art.ini Foundation（西木 SHP 北尖居中；勿缩小占地）
    {BldType::ConYard,      "建造厂",   3000, 600, 1000, 4,4, 0,    6, wNone(), ALLF, BldType::COUNT, true},
    {BldType::PowerPlant,   "发电厂",   800,  160, 750,  2,2, 200,  4, wNone(), FA, BldType::COUNT, true},
    {BldType::TeslaReactor, "磁能反应堆",600,  130, 750,  3,2, 150,  4, wNone(), FS | FC, BldType::COUNT, true},
    {BldType::NuclearReactor,"核子反应堆",1000, 220, 800, 4,4, 2000, 4, wNone(), FS | FC, BldType::BattleLab, true},
    {BldType::Barracks,     "兵营",     500,  110, 500,  3,2, -20,  5, wNone(), ALLF, BldType::COUNT, true},
    {BldType::WarFactory,   "战车工厂", 2000, 400, 1000, 5,3, -40,  5, wNone(), ALLF, BldType::Barracks, true},
    {BldType::OreRefinery,  "矿石精炼厂",2000, 280, 1000,  4,3, -50,  5, wNone(), ALLF, BldType::COUNT, true},
    {BldType::Radar,        "雷达站",   1000, 200, 1000,  2,2, -50,  10, wNone(), FSF, BldType::OreRefinery, true},
    {BldType::BattleLab,    "作战实验室",2000, 400, 500,  3,2, -100, 6, wNone(), ALLF, BldType::Radar, true},
    {BldType::AirForceCmd,  "空指部",   1000, 220, 600,  3,2, -50,  8, wNone(), ALLF, BldType::OreRefinery, true},
    {BldType::NavalYard,    "海军船厂", 1000, 220, 1500, 4,4, -20,  5, wNone(), ALLF, BldType::WarFactory, true},
    {BldType::Pillbox,      "机枪碉堡",  500,  100, 400,  1,1, 0,   6, wHeavyRifle(), FA, BldType::Barracks, false},
    {BldType::SentryGun,    "哨戒炮",   500,  100, 400,  1,1, 0,   6, wHeavyRifle(), FS | FC, BldType::Barracks, false},
    {BldType::PrismTower,   "光棱塔",   1500, 280, 600,  1,1, -75, 8, wPrism(), FA, BldType::Radar, false},
    {BldType::TeslaCoil,    "磁暴线圈",  1500, 280, 600,  1,1, -75, 8, wTeslaBolt(), FS | FC, BldType::Radar, false},
    {BldType::FlakCannon,   "高射炮",   1000, 200, 900,  1,1, -50, 8, wFlak(), FS | FC, BldType::Radar, false},
    {BldType::GrandCannon,  "巨炮",     2000, 360, 700,  2,2, -100,10, WeaponDef{120,10,90,false,true,"shell",0.5f,1.2f,1.2f}, FA, BldType::BattleLab, false, 0, Country::France},
    {BldType::PatriotMissile,"爱国者飞弹",1000, 200, 900,  1,1, -50, 8, wPatriot(), FA, BldType::Radar, false},
    {BldType::Wall,         "围墙",     50,   20,  200,  1,1, 0,   1, wNone(), ALLF, BldType::COUNT, false},
    {BldType::OrePurifier,  "矿石精炼器",2500, 500, 900,  3,3, -200,5, wNone(), FA | FC, BldType::BattleLab, true},
    {BldType::IndustrialPlant,"工业工厂",2500, 500, 900, 3,3, -200,5, wNone(), FS, BldType::BattleLab, true},
    // 超武建筑：高耗电，建成后对应超武开始充能
    {BldType::NukeSilo,     "核弹发射井",5000, 600, 1000, 3,3, -200,5, wNone(), FS | FC, BldType::BattleLab, true},
    {BldType::WeatherDevice,"天气控制器",5000, 600, 1000, 3,3, -200,5, wNone(), FA, BldType::BattleLab, true},
    {BldType::IronCurtain,  "铁幕装置",  2500, 500, 750,  3,3, -200,5, wNone(), FS | FC, BldType::BattleLab, true},
    {BldType::ChronoSphere, "超时空传送仪",2500,500, 750,  4,3, -200,5, wNone(), FA, BldType::BattleLab, true},
    // 中立科技建筑：不由玩家建造（factionMask=0），工程师占领后生效
    {BldType::OilDerrick,   "科技油井",  0,   0,   1000, 2,2, 0,   4, wNone(), 0, BldType::COUNT, true},
    {BldType::Hospital,     "医院",      0,   0,   800,  6,4, 0,   4, wNone(), 0, BldType::COUNT, true},
    {BldType::MachineShop,  "机械商店",  0,   0,   800,  3,3, 0,   4, wNone(), 0, BldType::COUNT, true},
    // ---- RA2 补全：高级建筑 ----
    {BldType::CloningVat,   "复制中心",  2500, 500, 800,  2,2, -100,5, wNone(), FY, BldType::BattleLab, false},
    {BldType::ServiceDepot, "维修厂",    800,  180, 1200,  3,3, -25, 5, wNone(), ALLF, BldType::WarFactory, false},
    {BldType::GapGenerator, "裂缝产生器",1600, 320, 600,  1,1, -100,6, wNone(), FA, BldType::BattleLab, false},
    {BldType::SpySat,       "间谍卫星",  1500, 300, 800,  2,2, -100,8, wNone(), FA, BldType::BattleLab, false},
    {BldType::PsychicSensor,"心灵探测器",1000, 200, 750,  2,2, -50, 10, wNone(), FY, BldType::OreRefinery, true},
    {BldType::BattleBunker, "战斗碉堡",  500,  100, 700,  2,2, 0,   6, wNone(), FS, BldType::Barracks, false, 5},
    {BldType::TankBunker,   "坦克碉堡",  400,  90,  900,  2,2, 0,   5, wNone(), FS, BldType::WarFactory, false, 1}, // 进驻 1 辆车辆对外射击
    // ---- RA2 补全：中立科技建筑（工程师占领生效）----
    {BldType::TechAirport,  "科技机场",  0,   0,   1000, 3,3, 0,   6, wNone(), 0, BldType::COUNT, true},
    {BldType::SecretLab,    "秘密实验室",0,   0,   800,  3,3, 0,   5, wNone(), 0, BldType::COUNT, true},
    {BldType::CivHouse,     "民房",      0,   0,   600,  3,2, 0,   3, wNone(), 0, BldType::COUNT, false, 8},
    // ---- 尤复阵营：尤里专属建筑 ----
    {BldType::BioReactor,   "生化反应堆", 600,  130, 500,  2,2, 150,  4, wNone(), FY, BldType::COUNT, true, 5},
    {BldType::GatlingCannon,"盖特机炮",  1000, 200, 500,  1,1, -50,  8, wGatlingCannonGun(), FY, BldType::Radar, false},
    {BldType::Grinder,      "回收炉",    1000, 200, 800,  3,3, -50,  5, wNone(), FY, BldType::WarFactory, false, 99},
    {BldType::GeneticMutator,"基因突变器",2500, 500, 900,  3,3, -150, 5, wNone(), FY, BldType::BattleLab, true},
    {BldType::PsychicDominator,"心灵控制仪",3000,600,1000, 3,3, -150, 5, wNone(), FY, BldType::BattleLab, true},
    // ---- 尤复补全：YR 新增建筑 ----
    {BldType::PsychicTower, "心灵控制塔",  1500, 280, 900, 1,1, -75, 8, wPsychicTowerMC(), FY, BldType::Radar, false},
    {BldType::RobotControl, "机器人指挥中心",600, 140, 600, 2,2, -100, 5, wNone(), FA, BldType::Radar, true},
    {BldType::TechPowerPlant,"科技电厂",    0,   0,   600, 2,2, 200,  4, wNone(), 0, BldType::COUNT, true},
    {BldType::TechOutpost,  "科技前哨站",   0,   0,   800, 4,3, 50,   6, wNone(), 0, BldType::COUNT, true},
};

// ===================== 超武表 =====================
static SWDef g_sws[(int)SWType::COUNT] = {
    {SWType::Nuke,             "战术核弹",  kSwMin * 10, BldType::NukeSilo},      // 官方 10 分钟
    {SWType::Lightning,        "闪电风暴",  kSwMin * 10, BldType::WeatherDevice},
    {SWType::IronCurtain,      "铁幕",      kSwMin * 5,  BldType::IronCurtain},   // 官方 5 分钟
    {SWType::ChronoShift,      "超时空传送",kSwMin * 7,  BldType::ChronoSphere},
    {SWType::GeneticMutator,   "基因突变",  kSwMin * 5,  BldType::GeneticMutator},
    {SWType::PsychicDominator, "心灵控制",  kSwMin * 10, BldType::PsychicDominator},
    {SWType::ForceShield,      "力场护盾",  kSwMin * 5,  BldType::BattleLab},     // YR：Battle Lab
};

const SWDef& swDef(SWType t) { return g_sws[(int)t]; }

SWType bldProvidesSW(BldType t) {
    for (int i = 0; i < (int)SWType::COUNT; i++)
        if (g_sws[i].fromBld == t) return (SWType)i;
    return SWType::COUNT;
}

const UnitDef& unitDef(UnitType t) { return g_units[(int)t]; }
const BldDef& bldDef(BldType t) { return g_blds[(int)t]; }

bool unitHasTurret(UnitType t) {
    switch (t) {
        case UnitType::Grizzly: case UnitType::Rhino: case UnitType::Type99:
        case UnitType::Apocalypse: case UnitType::PrismTank: case UnitType::TeslaTank:
        case UnitType::IFV: case UnitType::FlakTrack: case UnitType::MirageTank:
        case UnitType::RobotTank:
        case UnitType::LasherTank: case UnitType::GatlingTank:
        case UnitType::Magnetron:
        case UnitType::FloatingDisc:
        case UnitType::SlaveMiner:
        // 海军炮塔舰：边走边打
        case UnitType::Destroyer: case UnitType::Aegis: case UnitType::Dreadnought:
        case UnitType::SeaScorpion:
            return true;
        default: return false;
    }
}

bool isFactoryFor(BldType b, const UnitDef& u) {
    if (u.type == UnitType::Rocketeer) return b == BldType::Barracks;    // 火箭飞行兵出自兵营
    if (u.type == UnitType::Kirov) return b == BldType::WarFactory;      // 基洛夫出自战车工厂（RA2 原作）
    if (u.type == UnitType::RobotTank) return b == BldType::WarFactory;  // 遥控坦克：两栖但属战车
    if (u.type == UnitType::Nighthawk) return b == BldType::WarFactory;  // 夜鹰直升机出自战车工厂（RA2 原作）
    if (u.type == UnitType::SiegeChopper) return b == BldType::WarFactory; // 攻城直升机出自战车工厂（YR）
    if (u.type == UnitType::ChaosDrone) return b == BldType::WarFactory;  // 混乱机器人出自战车工厂（YR）
    if (u.type == UnitType::FloatingDisc) return b == BldType::WarFactory; // 飞碟出自战车工厂（YR）
    if (u.type == UnitType::Slave) return b == BldType::OreRefinery; // 奴隶由奴隶矿车体系产出
    if (u.isNaval() || u.isAmphib()) return b == BldType::NavalYard;
    if (u.isInfantry()) return b == BldType::Barracks;
    if (u.isAir()) return b == BldType::AirForceCmd;
    // 采矿车：战车工厂生产（前置仍为精炼厂）；不再从精炼厂出口
    return b == BldType::WarFactory;
}

std::vector<BldType> buildableBlds(Faction f) {
    std::vector<BldType> v;
    for (int i = 0; i < (int)BldType::COUNT; i++)
        if (g_blds[i].factionMask & (1 << (int)f)) v.push_back((BldType)i);
    return v;
}

std::vector<UnitType> trainableUnits(Faction f, bool naval) {
    std::vector<UnitType> v;
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        const UnitDef& u = g_units[i];
        if (!(u.factionMask & (1 << (int)f))) continue;
        bool nav = u.isNaval() || u.isAmphib();
        if (nav == naval) v.push_back((UnitType)i);
    }
    return v;
}

// ===================== 枚举规范名表（INI/地图/战役文件共用） =====================
static const char* kUnitKey[(int)UnitType::COUNT] = {
    "MCV", "Harvester",
    "GI", "Conscript", "PLA", "Engineer", "AttackDog", "Spy",
    "FlakTrooper", "TeslaTrooper", "Sniper", "Tanya",
    "Desolator", "Chrono", "GuardianGI", "CrazyIvan",
    "Grizzly", "Rhino", "Type99", "FlakTrack", "IFV",
    "PrismTank", "TeslaTank", "MirageTank", "V3Launcher", "Apocalypse", "TerrorDrone",
    "Intruder", "MiG", "BlackEagle", "Kirov", "Rocketeer",
    "Destroyer", "Typhoon", "Aegis", "SeaScorpion", "Dreadnought", "AircraftCarrier", "AmphTransport",
    "ChronoMiner", "WarMiner",
    "TankDestroyer", "Terrorist", "DemoTruck",
    "Nighthawk", "Dolphin", "Squid", "RobotTank", "BattleFortress", "Hornet",
    "NavySEAL", "Yuri", "ChronoCommando", "PsiCommando",
    "Initiate", "Brute", "Virus", "LasherTank", "GatlingTank", "Magnetron", "MasterMind", "FloatingDisc", "Boomer",
    "Boris", "SiegeChopper", "ChaosDrone",
    "Slave", "SlaveMiner",
    "YuriPrime", "ChronoIvan",
};
static const char* kBldKey[(int)BldType::COUNT] = {
    "ConYard", "PowerPlant", "TeslaReactor", "NuclearReactor",
    "Barracks", "WarFactory", "OreRefinery", "Radar", "BattleLab", "AirForceCmd", "NavalYard",
    "Pillbox", "SentryGun", "PrismTower", "TeslaCoil", "FlakCannon", "GrandCannon", "PatriotMissile",
    "Wall", "OrePurifier", "IndustrialPlant",
    "NukeSilo", "WeatherDevice", "IronCurtain", "ChronoSphere",
    "OilDerrick", "Hospital", "MachineShop",
    "CloningVat", "ServiceDepot", "GapGenerator", "SpySat", "PsychicSensor", "BattleBunker", "TankBunker",
    "TechAirport", "SecretLab", "CivHouse",
    "BioReactor", "GatlingCannon", "Grinder", "GeneticMutator", "PsychicDominator",
    "PsychicTower", "RobotControl", "TechPowerPlant", "TechOutpost",
};
static const char* kCountryKey[(int)Country::COUNT] = {
    "None", "America", "Korea", "France", "Germany", "UK",
    "Russia", "Cuba", "Libya", "Iraq", "China", "Yuri",
};
static const char* kFactionKey[4] = {"Allies", "Soviet", "China", "Yuri"};
static const char* kSWKey[(int)SWType::COUNT] = {"Nuke", "Lightning", "IronCurtain", "ChronoShift", "GeneticMutator", "PsychicDominator", "ForceShield"};

template <size_t N>
static bool lookupKey(const char* const (&tbl)[N], int base, const char* s, int& out) {
    if (!s) return false;
    for (size_t i = 0; i < N; i++)
        if (strcmp(tbl[i], s) == 0) { out = base + (int)i; return true; }
    return false;
}

bool unitTypeByName(const char* s, UnitType& out) { int v; if (!lookupKey(kUnitKey, 0, s, v)) return false; out = (UnitType)v; return true; }
bool bldTypeByName(const char* s, BldType& out) {
    if (s && (!strcmp(s, "COUNT") || !strcmp(s, "None"))) { out = BldType::COUNT; return true; }
    int v; if (!lookupKey(kBldKey, 0, s, v)) return false; out = (BldType)v; return true;
}
bool factionByName(const char* s, Faction& out) { int v; if (!lookupKey(kFactionKey, 0, s, v)) return false; out = (Faction)v; return true; }
bool countryByName(const char* s, Country& out) { int v; if (!lookupKey(kCountryKey, 0, s, v)) return false; out = (Country)v; return true; }
bool swTypeByName(const char* s, SWType& out) { int v; if (!lookupKey(kSWKey, 0, s, v)) return false; out = (SWType)v; return true; }
const char* unitTypeKey(UnitType t) { int i = (int)t; return (i >= 0 && i < (int)UnitType::COUNT) ? kUnitKey[i] : "?"; }
const char* bldTypeKey(BldType t) { int i = (int)t; return (i >= 0 && i < (int)BldType::COUNT) ? kBldKey[i] : "?"; }
const char* countryKey(Country c) { int i = (int)c; return (i >= 0 && i < (int)Country::COUNT) ? kCountryKey[i] : "?"; }
const char* factionKey(Faction f) { int i = (int)f; return (i >= 0 && i < 4) ? kFactionKey[i] : "?"; }
const char* swTypeKey(SWType t) { int i = (int)t; return (i >= 0 && i < (int)SWType::COUNT) ? kSWKey[i] : "?"; }

// ===================== 外部规则加载（assets/rules/rules.ini 覆盖内置数值） =====================
// 覆盖名/贴图串的持久存储（std::string 对象静态常驻，c_str 在 loadRules 单次调用后稳定）
// 全局游戏规则 + AI 建造配置 + 单位变体（rules.ini 可选节）
GameRules g_gameRules;
AIBuildConfig g_aiBuild[4];
std::vector<UnitVariant> g_variants;
const UnitVariant* findVariant(const std::string& name) {
    for (const auto& v : g_variants) if (v.name == name) return &v;
    return nullptr;
}

static std::string g_unitNameStr[(int)UnitType::COUNT];
static std::string g_unitProjStr[(int)UnitType::COUNT];
static std::string g_unitEliteProjStr[(int)UnitType::COUNT];
static std::string g_bldNameStr[(int)BldType::COUNT];
static std::string g_bldProjStr[(int)BldType::COUNT];
static std::string g_swNameStr[(int)SWType::COUNT];
static std::string g_deployProjStr[2];
// 精英武器可写池：首次 loadRules 时把内置精英武器复制入池并重指 UnitDef.elite
static WeaponDef g_elitePool[(int)UnitType::COUNT];
static bool g_elitePooled = false;

static void poolEliteWeapons() {
    if (g_elitePooled) return;
    g_elitePooled = true;
    for (int i = 0; i < (int)UnitType::COUNT; i++)
        if (g_units[i].elite) { g_elitePool[i] = *g_units[i].elite; g_units[i].elite = &g_elitePool[i]; }
}

static bool parseArmor(const char* s, Armor& out) {
    if (!s) return false;
    if (!strcmp(s, "None")) { out = Armor::None; return true; }
    if (!strcmp(s, "Flak")) { out = Armor::Flak; return true; }
    if (!strcmp(s, "Plate")) { out = Armor::Plate; return true; }
    if (!strcmp(s, "Light")) { out = Armor::Light; return true; }
    if (!strcmp(s, "Medium")) { out = Armor::Medium; return true; }
    if (!strcmp(s, "Heavy")) { out = Armor::Heavy; return true; }
    if (!strcmp(s, "Wood")) { out = Armor::Wood; return true; }
    if (!strcmp(s, "Steel")) { out = Armor::Steel; return true; }
    if (!strcmp(s, "Concrete") || !strcmp(s, "Building")) { out = Armor::Concrete; return true; }
    if (!strcmp(s, "Special1")) { out = Armor::Special1; return true; }
    if (!strcmp(s, "Special2")) { out = Armor::Special2; return true; }
    return false;
}
static bool parseWarhead(const char* s, WeaponDef::Warhead& out) {
    if (!s) return false;
    static const char* names[] = {"Legacy", "SmallArms", "AP", "HE", "HollowPoint", "Psychic", "Radiation"};
    for (int i = 0; i < (int)WeaponDef::Warhead::COUNT; ++i)
        if (!strcmp(s, names[i])) { out = (WeaponDef::Warhead)i; return true; }
    return false;
}
static bool parseMove(const char* s, MoveType& out) {
    if (!s) return false;
    if (!strcmp(s, "Infantry")) { out = MoveType::Infantry; return true; }
    if (!strcmp(s, "Vehicle")) { out = MoveType::Vehicle; return true; }
    if (!strcmp(s, "Air")) { out = MoveType::Air; return true; }
    if (!strcmp(s, "Naval")) { out = MoveType::Naval; return true; }
    if (!strcmp(s, "Amphibious")) { out = MoveType::Amphibious; return true; }
    return false;
}
// 阵营掩码："All"/"None" 或 "Allies|Soviet|China" 管道组合
static bool parseFactionMask(const char* s, uint8_t& out) {
    if (!s) return false;
    if (!strcmp(s, "All")) { out = 0b1111; return true; }
    if (!strcmp(s, "None")) { out = 0; return true; }
    uint8_t m = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", s);
    bool any = false;
    for (char* tok = strtok(buf, "|"); tok; tok = strtok(nullptr, "|")) {
        Faction f;
        if (!factionByName(tok, f)) return false;
        m |= (uint8_t)(1 << (int)f);
        any = true;
    }
    if (any) out = m;
    return any;
}

// 武器键覆盖：prefix 为 "Weapon" / "Elite" / nullptr（直接键，部署武器用）
static void loadWeaponKeys(const Ini::Section& sec, const char* prefix, WeaponDef& w, std::string& projSlot) {
    char key[32];
    auto kb = [&](const char* k) -> const char* {
        if (!prefix) return k;
        snprintf(key, sizeof(key), "%s.%s", prefix, k);
        return key;
    };
    const char* v;
    if ((v = sec.get(kb("Damage")))) w.damage = Ini::toInt(v, w.damage);
    if ((v = sec.get(kb("Range")))) w.range = Ini::toInt(v, w.range);
    if ((v = sec.get(kb("Cooldown")))) w.cooldown = Ini::toInt(v, w.cooldown);
    if ((v = sec.get(kb("AntiAir")))) w.antiAir = Ini::toBool(v, w.antiAir);
    if ((v = sec.get(kb("AntiGround")))) w.antiGround = Ini::toBool(v, w.antiGround);
    if ((v = sec.get(kb("Proj")))) { projSlot = v; w.projSprite = projSlot.c_str(); }
    if ((v = sec.get(kb("VsInf")))) w.vsInfantry = Ini::toFloat(v, w.vsInfantry);
    if ((v = sec.get(kb("VsVeh")))) w.vsVehicle = Ini::toFloat(v, w.vsVehicle);
    if ((v = sec.get(kb("VsBld")))) w.vsBuilding = Ini::toFloat(v, w.vsBuilding);
    if ((v = sec.get(kb("NavalOnly")))) w.navalOnly = Ini::toBool(v, w.navalOnly);
    if ((v = sec.get(kb("Splash")))) w.splash = Ini::toFloat(v, w.splash);
    if ((v = sec.get(kb("Warhead")))) parseWarhead(v, w.warhead);
}

void loadRules(const char* path) {
    Ini ini;
    if (!ini.load(path)) {
        TraceLog(LOG_INFO, "RA2 rules: %s not found, using built-in defaults", path);
        return;
    }
    poolEliteWeapons();
    int patched = 0;
    for (const Ini::Section& sec : ini.sections) {
        const std::string& sn = sec.name;
        auto warn = [&](const char* what) {
            TraceLog(LOG_WARNING, "RA2 rules: [%s] unknown %s, skipped", sn.c_str(), what);
        };
        if (sn.rfind("Unit.", 0) == 0) {
            UnitType ut;
            const char* unitName = sn.c_str() + 5;
            if (!unitTypeByName(unitName, ut)) {
                // 自定义单位变体：[Unit.MyCustomTank] Base=Grizzly ...
                const char* baseStr = sec.get("Base");
                if (baseStr && unitTypeByName(baseStr, ut)) {
                    UnitVariant var;
                    var.name = unitName;
                    var.base = ut;
                    const char* v;
                    if ((v = sec.get("Cost"))) var.cost = Ini::toInt(v, -1);
                    if ((v = sec.get("HP"))) var.hp = Ini::toInt(v, -1);
                    if ((v = sec.get("Speed"))) var.speed = Ini::toInt(v, -1);
                    if ((v = sec.get("Sight"))) var.sight = Ini::toInt(v, -1);
                    if ((v = sec.get("Weapon.Proj"))) var.weaponProj = v;
                    if ((v = sec.get("Weapon.Damage"))) var.weaponDmg = Ini::toInt(v, -1);
                    if ((v = sec.get("Weapon.Range"))) var.weaponRange = Ini::toInt(v, -1);
                    if ((v = sec.get("Weapon.Cooldown"))) var.weaponCooldown = Ini::toInt(v, -1);
                    if ((v = sec.get("Weapon.VsInf"))) var.vsInf = Ini::toFloat(v, -1);
                    if ((v = sec.get("Weapon.VsVeh"))) var.vsVeh = Ini::toFloat(v, -1);
                    if ((v = sec.get("Weapon.VsBld"))) var.vsBld = Ini::toFloat(v, -1);
                    g_variants.push_back(std::move(var));
                    TraceLog(LOG_INFO, "RA2 rules: variant '%s' registered (base=%s)", unitName, baseStr);
                } else {
                    warn("unit (use Base=<existing> to define a variant)");
                }
                continue;
            }
            UnitDef& d = g_units[(int)ut];
            const char* v;
            if ((v = sec.get("Name"))) { g_unitNameStr[(int)ut] = v; d.name = g_unitNameStr[(int)ut].c_str(); }
            if ((v = sec.get("Cost"))) d.cost = Ini::toInt(v, d.cost);
            if ((v = sec.get("BuildTime"))) d.buildTime = Ini::toInt(v, d.buildTime);
            if ((v = sec.get("HP"))) d.hp = Ini::toInt(v, d.hp);
            if ((v = sec.get("Speed"))) d.speed = Ini::toInt(v, d.speed);
            if ((v = sec.get("Sight"))) d.sight = Ini::toInt(v, d.sight);
            if ((v = sec.get("Armor"))) { if (!parseArmor(v, d.armor)) warn("Armor"); }
            if ((v = sec.get("Move"))) { if (!parseMove(v, d.move)) warn("Move"); }
            if ((v = sec.get("Factions"))) { if (!parseFactionMask(v, d.factionMask)) warn("Factions"); }
            if ((v = sec.get("Prereq"))) { if (!bldTypeByName(v, d.prereq)) warn("Prereq"); }
            if ((v = sec.get("Ammo"))) d.ammo = Ini::toInt(v, d.ammo);
            if ((v = sec.get("Cargo"))) d.cargoCap = Ini::toInt(v, d.cargoCap);
            if ((v = sec.get("Country"))) { if (!countryByName(v, d.countryReq)) warn("Country"); }
            loadWeaponKeys(sec, "Weapon", d.weapon, g_unitProjStr[(int)ut]);
            // 精英武器：有任何 Elite.* 键即启用（原本无精英武器时以基础武器为底）
            bool hasEliteKey = false;
            for (const auto& p : sec.kv)
                if (p.first.rfind("Elite.", 0) == 0) { hasEliteKey = true; break; }
            if (hasEliteKey) {
                if (!d.elite) { g_elitePool[(int)ut] = d.weapon; d.elite = &g_elitePool[(int)ut]; }
                loadWeaponKeys(sec, "Elite", g_elitePool[(int)ut], g_unitEliteProjStr[(int)ut]);
            }
            patched++;
        } else if (sn.rfind("Bld.", 0) == 0) {
            BldType bt;
            if (!bldTypeByName(sn.c_str() + 4, bt) || bt == BldType::COUNT) { warn("building"); continue; }
            BldDef& d = g_blds[(int)bt];
            const char* v;
            if ((v = sec.get("Name"))) { g_bldNameStr[(int)bt] = v; d.name = g_bldNameStr[(int)bt].c_str(); }
            if ((v = sec.get("Cost"))) d.cost = Ini::toInt(v, d.cost);
            if ((v = sec.get("BuildTime"))) d.buildTime = Ini::toInt(v, d.buildTime);
            if ((v = sec.get("HP"))) d.hp = Ini::toInt(v, d.hp);
            if ((v = sec.get("W"))) d.w = Ini::toInt(v, d.w);
            if ((v = sec.get("H"))) d.h = Ini::toInt(v, d.h);
            if ((v = sec.get("Power"))) d.power = Ini::toInt(v, d.power);
            if ((v = sec.get("Sight"))) d.sight = Ini::toInt(v, d.sight);
            if ((v = sec.get("Factions"))) { if (!parseFactionMask(v, d.factionMask)) warn("Factions"); }
            if ((v = sec.get("Prereq"))) { if (!bldTypeByName(v, d.prereq)) warn("Prereq"); }
            if ((v = sec.get("Capturable"))) d.capturable = Ini::toBool(v, d.capturable);
            if ((v = sec.get("Garrison"))) d.garrisonCap = Ini::toInt(v, d.garrisonCap);
            if ((v = sec.get("Country"))) { if (!countryByName(v, d.countryReq)) warn("Country"); }
            loadWeaponKeys(sec, "Weapon", d.weapon, g_bldProjStr[(int)bt]);
            patched++;
        } else if (sn.rfind("SW.", 0) == 0) {
            SWType st;
            if (!swTypeByName(sn.c_str() + 3, st)) { warn("superweapon"); continue; }
            SWDef& d = g_sws[(int)st];
            const char* v;
            if ((v = sec.get("Name"))) { g_swNameStr[(int)st] = v; d.name = g_swNameStr[(int)st].c_str(); }
            if ((v = sec.get("ChargeTime"))) d.chargeTime = Ini::toInt(v, d.chargeTime);
            if ((v = sec.get("FromBld"))) { if (!bldTypeByName(v, d.fromBld)) warn("FromBld"); }
            patched++;
        } else if (sn.rfind("DeployWeapon.", 0) == 0) {
            const char* who = sn.c_str() + 13;
            if (!strcmp(who, "GuardianGI")) loadWeaponKeys(sec, nullptr, wGgiDeploy, g_deployProjStr[0]);
            else if (!strcmp(who, "GI")) loadWeaponKeys(sec, nullptr, wGiDeploy, g_deployProjStr[1]);
            else { warn("deploy weapon"); continue; }
            patched++;
        } else if (sn == "GameRules") {
            const char* v;
            if ((v = sec.get("MaxMoney"))) g_gameRules.maxMoney = Ini::toInt(v, g_gameRules.maxMoney);
            if ((v = sec.get("StartingMoneyCap"))) g_gameRules.startingMoneyCap = Ini::toInt(v, g_gameRules.startingMoneyCap);
            if ((v = sec.get("LowPowerSpeedFactor"))) g_gameRules.lowPowerSpeedFactor = Ini::toFloat(v, g_gameRules.lowPowerSpeedFactor);
            if ((v = sec.get("CrateInterval"))) g_gameRules.crateInterval = Ini::toInt(v, g_gameRules.crateInterval);
            if ((v = sec.get("OreRegrowRate"))) g_gameRules.oreRegrowRate = Ini::toInt(v, g_gameRules.oreRegrowRate);
            if ((v = sec.get("VeteranRatio"))) g_gameRules.veteranRatio = Ini::toFloat(v, g_gameRules.veteranRatio);
            if ((v = sec.get("BioReactorPowerPerOccupant"))) g_gameRules.bioReactorPowerPerOccupant = Ini::toInt(v, g_gameRules.bioReactorPowerPerOccupant);
            if ((v = sec.get("GrinderRefund"))) g_gameRules.grinderRefund = Ini::toFloat(v, g_gameRules.grinderRefund);
            if ((v = sec.get("VeteranDmgBonus"))) { // 逗号分隔 3 个值
                int a = 0, b = 0, c = 0;
                if (sscanf(v, "%d,%d,%d", &a, &b, &c) == 3) {
                    g_gameRules.veteranismDmgBonus[0] = a / 100.0f;
                    g_gameRules.veteranismDmgBonus[1] = b / 100.0f;
                    g_gameRules.veteranismDmgBonus[2] = c / 100.0f;
                }
            }
            auto loadPct3 = [&](const char* key, float (&dst)[3]) {
                if (const char* x = sec.get(key)) {
                    int a = 0, b = 0, c = 0;
                    if (sscanf(x, "%d,%d,%d", &a, &b, &c) == 3) {
                        dst[0] = a / 100.0f; dst[1] = b / 100.0f; dst[2] = c / 100.0f;
                    }
                }
            };
            loadPct3("VeteranArmorBonus", g_gameRules.veteranArmorBonus);
            loadPct3("VeteranSpeedBonus", g_gameRules.veteranSpeedBonus);
            loadPct3("VeteranRofBonus", g_gameRules.veteranRofBonus);
            if ((v = sec.get("VeteranSelfHeal"))) {
                sscanf(v, "%d,%d,%d", &g_gameRules.veteranSelfHeal[0],
                       &g_gameRules.veteranSelfHeal[1], &g_gameRules.veteranSelfHeal[2]);
            }
            g_gameRules.maxMoney = std::max(0, g_gameRules.maxMoney);
            g_gameRules.startingMoneyCap = std::clamp(g_gameRules.startingMoneyCap, 0, g_gameRules.maxMoney);
            g_gameRules.lowPowerSpeedFactor = std::clamp(g_gameRules.lowPowerSpeedFactor, 0.01f, 1.0f);
            g_gameRules.crateInterval = std::max(0, g_gameRules.crateInterval);
            g_gameRules.oreRegrowRate = std::max(0, g_gameRules.oreRegrowRate);
            g_gameRules.veteranRatio = std::clamp(g_gameRules.veteranRatio, 0.1f, 100.0f);
            g_gameRules.bioReactorPowerPerOccupant = std::max(0, g_gameRules.bioReactorPowerPerOccupant);
            g_gameRules.grinderRefund = std::clamp(g_gameRules.grinderRefund, 0.0f, 2.0f);
            for (float& bonus : g_gameRules.veteranismDmgBonus) bonus = std::clamp(bonus, 0.0f, 10.0f);
            patched++;
        } else if (sn.rfind("Warhead.", 0) == 0) {
            WeaponDef::Warhead wh;
            if (!parseWarhead(sn.c_str() + 8, wh) || wh == WeaponDef::Warhead::Legacy) {
                warn("warhead"); continue;
            }
            const char* v = sec.get("Verses");
            float vals[(int)Armor::COUNT] = {};
            int got = v ? sscanf(v, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
                &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5],
                &vals[6], &vals[7], &vals[8], &vals[9], &vals[10]) : 0;
            if (got != (int)Armor::COUNT) { warn("Verses (11 comma-separated multipliers)"); continue; }
            for (int i = 0; i < (int)Armor::COUNT; ++i)
                g_gameRules.warheadVerses[(int)wh][i] = std::clamp(vals[i], 0.0f, 10.0f);
            patched++;
        } else if (sn.rfind("AIBuild.", 0) == 0) {
            const char* facStr = sn.c_str() + 8;
            Faction f;
            if (!factionByName(facStr, f)) { warn("AI faction"); continue; }
            AIBuildConfig& cfg = g_aiBuild[(int)f];
            cfg.enabled = true;
            const char* v;
            if ((v = sec.get("BuildOrder"))) {
                cfg.buildOrder.clear();
                char buf[1024];
                snprintf(buf, sizeof(buf), "%s", v);
                for (char* tok = strtok(buf, ","); tok; tok = strtok(nullptr, ","))
                    cfg.buildOrder.push_back(tok);
            }
            if ((v = sec.get("HarvesterTarget"))) cfg.harvesterTarget = Ini::toInt(v, cfg.harvesterTarget);
            if ((v = sec.get("AttackWaveSize"))) cfg.attackWaveSize = Ini::toInt(v, cfg.attackWaveSize);
            if ((v = sec.get("SaveForSuperWeapon"))) cfg.saveForSuperWeapon = Ini::toBool(v, cfg.saveForSuperWeapon);
            patched++;
        } else {
            warn("section");
        }
    }
    TraceLog(LOG_INFO, "RA2 rules: %s loaded, %d sections applied", path, patched);
}

// ===================== 规则导出（--export-assets）：全量数值写成 rules.ini 模板 =====================
static const char* armorKey(Armor a) {
    switch (a) {
        case Armor::None: return "None";
        case Armor::Flak: return "Flak";
        case Armor::Plate: return "Plate";
        case Armor::Light: return "Light";
        case Armor::Medium: return "Medium";
        case Armor::Heavy: return "Heavy";
        case Armor::Wood: return "Wood";
        case Armor::Steel: return "Steel";
        case Armor::Concrete: return "Concrete";
        case Armor::Special1: return "Special1";
        case Armor::Special2: return "Special2";
        case Armor::COUNT: break;
    }
    return "?";
}
static const char* warheadKey(WeaponDef::Warhead w) {
    static const char* names[] = {"Legacy", "SmallArms", "AP", "HE", "HollowPoint", "Psychic", "Radiation"};
    int i = (int)w;
    return i >= 0 && i < (int)WeaponDef::Warhead::COUNT ? names[i] : "Legacy";
}
static const char* moveKey(MoveType m) {
    switch (m) {
        case MoveType::Infantry: return "Infantry";
        case MoveType::Vehicle: return "Vehicle";
        case MoveType::Air: return "Air";
        case MoveType::Naval: return "Naval";
        case MoveType::Amphibious: return "Amphibious";
    }
    return "?";
}
static std::string factionMaskStr(uint8_t m) {
    if (m == 0) return "None";
    if (m == ALLF) return "All";
    std::string s;
    for (int i = 0; i < 4; i++)
        if (m & (1 << i)) { if (!s.empty()) s += '|'; s += kFactionKey[i]; }
    return s;
}
// 武器键写出：prefix 非空时键名为 "<prefix>.<Key>"（Weapon/Elite），否则直接键名（DeployWeapon）
static void writeWeaponKeys(FILE* f, const char* prefix, const WeaponDef& w) {
    auto key = [&](const char* k, std::string& buf) -> const char* {
        if (!prefix) return k;
        buf = prefix;
        buf += '.';
        buf += k;
        return buf.c_str();
    };
    std::string b;
    fprintf(f, "%s=%d\n", key("Damage", b), w.damage);
    fprintf(f, "%s=%d\n", key("Range", b), w.range);
    fprintf(f, "%s=%d\n", key("Cooldown", b), w.cooldown);
    fprintf(f, "%s=%s\n", key("AntiAir", b), w.antiAir ? "yes" : "no");
    fprintf(f, "%s=%s\n", key("AntiGround", b), w.antiGround ? "yes" : "no");
    fprintf(f, "%s=%s\n", key("Proj", b), w.projSprite);
    fprintf(f, "%s=%g\n", key("VsInf", b), w.vsInfantry);
    fprintf(f, "%s=%g\n", key("VsVeh", b), w.vsVehicle);
    fprintf(f, "%s=%g\n", key("VsBld", b), w.vsBuilding);
    fprintf(f, "%s=%s\n", key("NavalOnly", b), w.navalOnly ? "yes" : "no");
    fprintf(f, "%s=%g\n", key("Splash", b), w.splash);
    fprintf(f, "%s=%s\n", key("Warhead", b), warheadKey(w.warhead));
}

void exportRules(const char* path) {
    MakeDirectory("assets");
    MakeDirectory("assets/rules");
    FILE* f = fopen(path, "wb");
    if (!f) { TraceLog(LOG_WARNING, "RA2 export: cannot write %s", path); return; }
    fprintf(f, "; OpenRA2 rules - unit/building/superweapon stats. Edit values to customize;\n");
    fprintf(f, "; delete keys or sections to fall back to built-in defaults. See assets/README.txt.\n\n");
    fprintf(f, "[GameRules]\nVeteranRatio=%g\nVeteranDmgBonus=%g,%g,%g\n",
            g_gameRules.veteranRatio, g_gameRules.veteranismDmgBonus[0] * 100,
            g_gameRules.veteranismDmgBonus[1] * 100, g_gameRules.veteranismDmgBonus[2] * 100);
    fprintf(f, "VeteranArmorBonus=%g,%g,%g\nVeteranSpeedBonus=%g,%g,%g\nVeteranRofBonus=%g,%g,%g\n",
            g_gameRules.veteranArmorBonus[0] * 100, g_gameRules.veteranArmorBonus[1] * 100, g_gameRules.veteranArmorBonus[2] * 100,
            g_gameRules.veteranSpeedBonus[0] * 100, g_gameRules.veteranSpeedBonus[1] * 100, g_gameRules.veteranSpeedBonus[2] * 100,
            g_gameRules.veteranRofBonus[0] * 100, g_gameRules.veteranRofBonus[1] * 100, g_gameRules.veteranRofBonus[2] * 100);
    fprintf(f, "VeteranSelfHeal=%d,%d,%d\nBioReactorPowerPerOccupant=%d\nGrinderRefund=%g\n\n",
            g_gameRules.veteranSelfHeal[0], g_gameRules.veteranSelfHeal[1], g_gameRules.veteranSelfHeal[2],
            g_gameRules.bioReactorPowerPerOccupant, g_gameRules.grinderRefund);
    for (int wi = 1; wi < (int)WeaponDef::Warhead::COUNT; ++wi) {
        fprintf(f, "[Warhead.%s]\nVerses=", warheadKey((WeaponDef::Warhead)wi));
        for (int ai = 0; ai < (int)Armor::COUNT; ++ai)
            fprintf(f, "%s%g", ai ? "," : "", g_gameRules.warheadVerses[wi][ai]);
        fprintf(f, "\n\n");
    }
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        const UnitDef& d = g_units[i];
        fprintf(f, "[Unit.%s]\n", kUnitKey[i]);
        fprintf(f, "Name=%s\n", d.name);
        fprintf(f, "Cost=%d\nBuildTime=%d\nHP=%d\nSpeed=%d\nSight=%d\n", d.cost, d.buildTime, d.hp, d.speed, d.sight);
        fprintf(f, "Armor=%s\nMove=%s\n", armorKey(d.armor), moveKey(d.move));
        fprintf(f, "Factions=%s\n", factionMaskStr(d.factionMask).c_str());
        fprintf(f, "Prereq=%s\n", d.prereq == BldType::COUNT ? "None" : bldTypeKey(d.prereq));
        fprintf(f, "Ammo=%d\nCargo=%d\nCountry=%s\n", d.ammo, d.cargoCap, countryKey(d.countryReq));
        writeWeaponKeys(f, "Weapon", d.weapon);
        if (d.elite) writeWeaponKeys(f, "Elite", *d.elite);
        fprintf(f, "\n");
    }
    for (int i = 0; i < (int)BldType::COUNT; i++) {
        const BldDef& d = g_blds[i];
        fprintf(f, "[Bld.%s]\n", kBldKey[i]);
        fprintf(f, "Name=%s\n", d.name);
        fprintf(f, "Cost=%d\nBuildTime=%d\nHP=%d\nW=%d\nH=%d\nPower=%d\nSight=%d\n",
                d.cost, d.buildTime, d.hp, d.w, d.h, d.power, d.sight);
        fprintf(f, "Factions=%s\n", factionMaskStr(d.factionMask).c_str());
        fprintf(f, "Prereq=%s\n", d.prereq == BldType::COUNT ? "None" : bldTypeKey(d.prereq));
        fprintf(f, "Capturable=%s\nGarrison=%d\nCountry=%s\n",
                d.capturable ? "yes" : "no", d.garrisonCap, countryKey(d.countryReq));
        writeWeaponKeys(f, "Weapon", d.weapon);
        fprintf(f, "\n");
    }
    for (int i = 0; i < (int)SWType::COUNT; i++) {
        const SWDef& d = g_sws[i];
        fprintf(f, "[SW.%s]\n", kSWKey[i]);
        fprintf(f, "Name=%s\nChargeTime=%d\nFromBld=%s\n\n", d.name, d.chargeTime, bldTypeKey(d.fromBld));
    }
    fprintf(f, "[DeployWeapon.GuardianGI]\n");
    writeWeaponKeys(f, nullptr, wGgiDeploy);
    fprintf(f, "\n[DeployWeapon.GI]\n");
    writeWeaponKeys(f, nullptr, wGiDeploy);
    // AI 人格配置模板（遭遇战 UI 中选择；此节仅作文档参考，实际参数由代码内置）
    fprintf(f, "\n; ==================== AI 人格配置（文档参考） ====================\n");
    fprintf(f, "; 难度：0=简单(思考30帧/阈值15+/无超武) 1=普通(15帧/8+) 2=困难(10帧/6+/集火/撤退) 3=残酷(7帧/5+/资源加成35%%)\n");
    fprintf(f, "; 人格：0=均衡 1=速攻(早期暴兵/轻科技) 2=龟缩(重防御/超武优先) 3=轰压(大规模集结/扩张) 4=科技(极速高科/精锐)\n");
    fprintf(f, "; 在遭遇战设置中为每个 AI 槽位选择难度和人格\n");
    fclose(f);
    TraceLog(LOG_INFO, "RA2 export: %s written", path);
}
