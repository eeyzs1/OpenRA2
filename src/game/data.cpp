#include "game/data.h"

// 阵营位掩码
static constexpr uint8_t FA = 1 << (int)Faction::Allies;
static constexpr uint8_t FS = 1 << (int)Faction::Soviet;
static constexpr uint8_t FC = 1 << (int)Faction::China;
static constexpr uint8_t ALLF = FA | FS | FC;

// 武器预设
static WeaponDef wNone() { return WeaponDef{0, 0, 0, false, false, "shell", 1, 1, 1}; }
static WeaponDef wRifle() { return WeaponDef{8, 4, 20, false, true, "bullet", 1.0f, 0.5f, 0.4f}; }
static WeaponDef wHeavyRifle() { return WeaponDef{14, 5, 18, false, true, "bullet", 1.0f, 0.6f, 0.5f}; }
static WeaponDef wFlak() { return WeaponDef{12, 6, 24, true, true, "flak", 1.0f, 0.7f, 0.5f}; }
static WeaponDef wTeslaBolt() { return WeaponDef{30, 5, 40, false, true, "tesla", 1.2f, 1.0f, 0.8f}; }
static WeaponDef wSniper() { return WeaponDef{60, 8, 70, false, true, "bullet", 1.0f, 0.05f, 0.05f}; }
// 辐射工兵：辐射射线，对步兵致命；部署后为区域辐射
static WeaponDef wRadiation() { return WeaponDef{50, 5, 30, false, true, "rad", 2.5f, 0.3f, 0.1f}; }
// 超时空抹除：不造成伤害，命中叠加"抹除进度"（special: chrono）
static WeaponDef wChrono() { return WeaponDef{1, 6, 20, false, true, "chrono", 1.0f, 1.0f, 0.0f}; }
static WeaponDef wTankGun(int dmg, int rng, int cd) { return WeaponDef{dmg, rng, cd, false, true, "shell", 0.6f, 1.0f, 0.8f}; }
static WeaponDef wPrism() { return WeaponDef{50, 7, 55, false, true, "prism", 1.0f, 0.8f, 1.4f}; }
static WeaponDef wV3() { return WeaponDef{150, 14, 220, false, true, "missile", 0.5f, 1.0f, 1.5f, false, 1.5f}; }
static WeaponDef wBomb(int dmg) { return WeaponDef{dmg, 3, 40, false, true, "shell", 0.5f, 1.0f, 1.5f}; }   // 航弹：对建筑强
static WeaponDef wAirMissile() { return WeaponDef{80, 4, 30, false, true, "missile", 0.6f, 1.2f, 1.0f}; } // 米格空空/地导弹
static WeaponDef wNavalGun() { return WeaponDef{60, 8, 50, false, true, "naval", 0.6f, 1.1f, 1.0f}; }      // 舰炮：对舰艇/沿岸
static WeaponDef wTorpedo() { return WeaponDef{100, 7, 60, false, true, "torpedo", 0, 1.2f, 0.8f, true}; } // 鱼雷：仅水上目标
static WeaponDef wAegisAA() { return WeaponDef{50, 10, 22, true, false, "missile", 0, 1.0f, 0}; }          // 神盾防空：纯对空

// ===================== 单位表 =====================
// 注：尾部追加 ammo 字段（0=无限）
static const UnitDef g_units[(int)UnitType::COUNT] = {
    // type, name, cost, btime, hp, speed, sight, armor, move, weapon, factions, prereq, ammo
    {UnitType::MCV,        "基地车",    2500, 500, 600, 24, 5, Armor::Heavy, MoveType::Vehicle, wNone(), ALLF, BldType::WarFactory, 0},
    {UnitType::Harvester,  "采矿车",    1400, 280, 700, 20, 4, Armor::Heavy, MoveType::Vehicle, wNone(), ALLF, BldType::OreRefinery},
    {UnitType::GI,         "美国大兵",   200, 60,  100, 14, 5, Armor::None, MoveType::Infantry, wRifle(), FA, BldType::COUNT},
    {UnitType::Conscript,  "动员兵",    100, 45,  90,  14, 5, Armor::None, MoveType::Infantry, wRifle(), FS, BldType::COUNT},
    {UnitType::PLA,        "解放军",    150, 50,  120, 13, 5, Armor::None, MoveType::Infantry, wHeavyRifle(), FC, BldType::COUNT},
    {UnitType::Engineer,   "工程师",    500, 120, 75,  14, 4, Armor::None, MoveType::Infantry, wNone(), ALLF, BldType::COUNT},
    {UnitType::AttackDog,  "军犬",      200, 50,  80,  8,  6, Armor::None, MoveType::Infantry, WeaponDef{50,1,15,false,true,"bullet",2.0f,0,0}, ALLF, BldType::COUNT},
    {UnitType::Spy,        "间谍",      1000,200, 75,  14, 6, Armor::None, MoveType::Infantry, wNone(), FA | FC, BldType::BattleLab},
    {UnitType::FlakTrooper,"高射步兵",   300, 70,  100, 14, 5, Armor::None, MoveType::Infantry, wFlak(), FS, BldType::COUNT},
    {UnitType::TeslaTrooper,"磁暴步兵",  500, 100, 160, 16, 5, Armor::Light, MoveType::Infantry, wTeslaBolt(), FS, BldType::Radar},
    {UnitType::Sniper,     "狙击手",    600, 110, 90,  16, 8, Armor::None, MoveType::Infantry, wSniper(), FA, BldType::Radar},
    {UnitType::Tanya,      "谭雅",      1500,300, 200, 12, 8, Armor::None, MoveType::Infantry, WeaponDef{120,6,10,false,true,"bullet",1.5f,0.3f,1.0f}, FA, BldType::BattleLab},
    {UnitType::Desolator,  "辐射工兵",   600, 110, 150, 14, 6, Armor::None, MoveType::Infantry, wRadiation(), FS, BldType::Radar},
    {UnitType::Chrono,     "超时空军团兵",1500,300, 125, 14, 8, Armor::None, MoveType::Infantry, wChrono(), FA, BldType::BattleLab},
    {UnitType::Grizzly,    "灰熊坦克",   700, 150, 300, 12, 6, Armor::Heavy, MoveType::Vehicle, wTankGun(30, 5, 35), FA, BldType::COUNT},
    {UnitType::Rhino,      "犀牛坦克",   900, 170, 400, 14, 6, Armor::Heavy, MoveType::Vehicle, wTankGun(40, 5, 40), FS, BldType::COUNT},
    {UnitType::Type99,     "99式坦克",  1200,190, 500, 12, 6, Armor::Heavy, MoveType::Vehicle, wTankGun(55, 6, 42), FC, BldType::COUNT},
    {UnitType::FlakTrack,  "高射炮车",   500, 110, 200, 10, 6, Armor::Light, MoveType::Vehicle, wFlak(), FS, BldType::COUNT},
    {UnitType::IFV,        "多功能步兵车",600, 110, 200, 8,  7, Armor::Light, MoveType::Vehicle, WeaponDef{20,6,20,true,true,"missile",0.8f,1.0f,0.8f}, FA, BldType::COUNT},
    {UnitType::PrismTank,  "光棱坦克",   1200,240, 180, 16, 7, Armor::Light, MoveType::Vehicle, wPrism(), FA, BldType::BattleLab},
    {UnitType::TeslaTank,  "磁能坦克",   1200,240, 320, 14, 7, Armor::Heavy, MoveType::Vehicle, wTeslaBolt(), FS, BldType::BattleLab},
    {UnitType::MirageTank, "幻影坦克",   1000,220, 250, 12, 7, Armor::Light, MoveType::Vehicle, wTankGun(45, 6, 38), FA | FC, BldType::BattleLab},
    {UnitType::V3Launcher, "V3火箭车",  800, 200, 150, 18, 6, Armor::Light, MoveType::Vehicle, wV3(), FS | FC, BldType::Radar},
    {UnitType::Apocalypse, "天启坦克",   1750,350, 800, 18, 7, Armor::Heavy, MoveType::Vehicle, WeaponDef{80,6,50,true,true,"shell",0.8f,1.2f,1.0f}, FS | FC, BldType::BattleLab},
    // 空军：speed 越小越快；ammo 打完返航装填
    {UnitType::Intruder,   "入侵者战机", 1200,240, 200, 3,  6, Armor::Light, MoveType::Air, wBomb(150), FA, BldType::COUNT, 1},
    {UnitType::MiG,        "米格战机",   1200,240, 260, 2,  6, Armor::Light, MoveType::Air, wAirMissile(), FS, BldType::COUNT, 2},
    {UnitType::BlackEagle, "黑鹰战机",   1500,300, 320, 2,  7, Armor::Light, MoveType::Air, wBomb(250), FC, BldType::COUNT, 1},
    // 海军：speed 越小越快；船厂生产
    {UnitType::Destroyer,  "驱逐舰",     1000,240, 600, 16, 7, Armor::Heavy, MoveType::Naval, wNavalGun(), FA, BldType::COUNT},
    {UnitType::Typhoon,    "台风潜艇",   1000,240, 600, 14, 6, Armor::Heavy, MoveType::Naval, wTorpedo(), FS, BldType::COUNT},
    {UnitType::Aegis,      "中华神盾舰", 1200,260, 800, 14, 9, Armor::Heavy, MoveType::Naval, wAegisAA(), FC, BldType::Radar},
    {UnitType::AmphTransport,"两栖运输船",900, 220, 300, 12, 5, Armor::Light, MoveType::Amphibious, wNone(), ALLF, BldType::COUNT, 0, 5},
};

// ===================== 建筑表 =====================
static const BldDef g_blds[(int)BldType::COUNT] = {
    // type, name, cost, btime, hp, w,h, power, sight, weapon, factions, prereq, capturable
    {BldType::ConYard,      "建造厂",   3000, 600, 1500, 3,3, 0,    6, wNone(), ALLF, BldType::COUNT, false},
    {BldType::PowerPlant,   "发电厂",   800,  160, 600,  2,2, 200,  4, wNone(), FA, BldType::COUNT, true},
    {BldType::TeslaReactor, "磁能反应堆",600,  130, 500,  2,2, 150,  4, wNone(), FS | FC, BldType::COUNT, true},
    {BldType::NuclearReactor,"核子反应堆",1000, 220, 800, 3,3, 500,  4, wNone(), FS | FC, BldType::BattleLab, true},
    {BldType::Barracks,     "兵营",     500,  110, 700,  2,2, -20,  5, wNone(), ALLF, BldType::COUNT, true},
    {BldType::WarFactory,   "战车工厂", 2000, 400, 1000, 3,3, -40,  5, wNone(), ALLF, BldType::Barracks, false},
    {BldType::OreRefinery,  "矿石精炼厂",1400, 280, 900,  3,2, -40,  5, wNone(), ALLF, BldType::COUNT, true},
    {BldType::Radar,        "雷达站",   1000, 200, 600,  2,2, -50,  10, wNone(), ALLF, BldType::OreRefinery, true},
    {BldType::BattleLab,    "作战实验室",2000, 400, 700,  2,2, -100, 6, wNone(), ALLF, BldType::Radar, true},
    {BldType::AirForceCmd,  "空指部",   1000, 220, 600,  2,2, -50,  8, wNone(), ALLF, BldType::OreRefinery, true},
    {BldType::NavalYard,    "海军船厂", 1000, 220, 1000, 3,3, -20,  5, wNone(), ALLF, BldType::WarFactory, false},
    {BldType::Pillbox,      "机枪碉堡",  500,  100, 400,  1,1, 0,   6, wHeavyRifle(), FA, BldType::Barracks, false},
    {BldType::SentryGun,    "哨戒炮",   500,  100, 400,  1,1, 0,   6, wHeavyRifle(), FS | FC, BldType::Barracks, false},
    {BldType::PrismTower,   "光棱塔",   1500, 280, 500,  1,1, -75, 8, wPrism(), FA, BldType::Radar, false},
    {BldType::TeslaCoil,    "磁暴线圈",  1500, 280, 600,  1,1, -75, 8, wTeslaBolt(), FS | FC, BldType::Radar, false},
    {BldType::FlakCannon,   "高射炮",   1000, 200, 500,  1,1, -50, 8, wFlak(), FS | FC, BldType::Radar, false},
    {BldType::GrandCannon,  "巨炮",     2000, 360, 700,  2,2, -100,10, WeaponDef{120,10,90,false,true,"shell",0.5f,1.2f,1.2f}, FA, BldType::BattleLab, false},
    {BldType::OrePurifier,  "矿石精炼器",2500, 500, 900,  2,2, -200,5, wNone(), FA | FC, BldType::BattleLab, true},
    {BldType::IndustrialPlant,"工业工厂",2500, 500, 900, 3,2, -200,5, wNone(), FS, BldType::BattleLab, true},
    // 超武建筑：高耗电，建成后对应超武开始充能
    {BldType::NukeSilo,     "核弹发射井",3000, 600, 1000, 2,2, -150,5, wNone(), FS | FC, BldType::BattleLab, true},
    {BldType::WeatherDevice,"天气控制器",3000, 600, 1000, 3,2, -150,5, wNone(), FA, BldType::BattleLab, true},
    {BldType::IronCurtain,  "铁幕装置",  2500, 500, 900,  2,2, -150,5, wNone(), FS | FC, BldType::BattleLab, true},
    // 中立科技建筑：不由玩家建造（factionMask=0），工程师占领后生效
    {BldType::OilDerrick,   "科技油井",  0,   0,   1000, 2,2, 0,   4, wNone(), 0, BldType::COUNT, true},
    {BldType::Hospital,     "医院",      0,   0,   800,  2,2, 0,   4, wNone(), 0, BldType::COUNT, true},
    {BldType::MachineShop,  "机械商店",  0,   0,   800,  2,2, 0,   4, wNone(), 0, BldType::COUNT, true},
};

// ===================== 超武表 =====================
static const SWDef g_sws[(int)SWType::COUNT] = {
    {SWType::Nuke,        "战术核弹",  30 * 60 * 3, BldType::NukeSilo},      // 3 分钟
    {SWType::Lightning,   "闪电风暴",  30 * 60 * 3, BldType::WeatherDevice},
    {SWType::IronCurtain, "铁幕",      30 * 60 * 2, BldType::IronCurtain},   // 2 分钟
};

const SWDef& swDef(SWType t) { return g_sws[(int)t]; }

SWType bldProvidesSW(BldType t) {
    for (int i = 0; i < (int)SWType::COUNT; i++)
        if (g_sws[i].fromBld == t) return (SWType)i;
    return SWType::COUNT;
}

const UnitDef& unitDef(UnitType t) { return g_units[(int)t]; }
const BldDef& bldDef(BldType t) { return g_blds[(int)t]; }

bool isFactoryFor(BldType b, const UnitDef& u) {
    if (u.isNaval() || u.isAmphib()) return b == BldType::NavalYard;
    if (u.isInfantry()) return b == BldType::Barracks;
    if (u.isAir()) return b == BldType::AirForceCmd;
    return b == BldType::WarFactory || (u.type == UnitType::Harvester && b == BldType::OreRefinery);
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
