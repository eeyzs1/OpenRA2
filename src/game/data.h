#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "core/util.h"

// ===================== 阵营 =====================
enum class Faction : uint8_t { Allies = 0, Soviet = 1, China = 2 };

inline const char* factionName(Faction f) {
    switch (f) {
        case Faction::Allies: return "盟军";
        case Faction::Soviet: return "苏联";
        case Faction::China:  return "中国";
    }
    return "?";
}

// ===================== 单位类型 =====================
enum class UnitType : uint8_t {
    // 通用
    MCV = 0, Harvester,
    // 步兵
    GI, Conscript, PLA,          // 美国大兵 / 动员兵 / 中国解放军
    Engineer, AttackDog, Spy,
    FlakTrooper, TeslaTrooper,   // 苏系
    Sniper, Tanya,               // 盟系
    Desolator,                   // 辐射工兵（苏，部署辐射区）
    Chrono,                      // 超时空军团兵（盟，传送+抹除）
    GuardianGI,                  // 重装大兵（盟，可部署反装甲）
    CrazyIvan,                   // 疯狂伊文（苏，安放定时炸弹）
    // 车辆
    Grizzly, Rhino, Type99,      // 灰熊 / 犀牛 / 99式
    FlakTrack, IFV,
    PrismTank, TeslaTank, MirageTank,
    V3Launcher, Apocalypse,      // 天启
    TerrorDrone,                 // 恐怖机器人（苏，反车辆近战）
    // 空军
    Intruder, MiG, BlackEagle,   // 入侵者 / 米格 / 黑鹰战机
    Kirov,                       // 基洛夫空艇（苏，重工生产，无限弹药慢速轰炸）
    Rocketeer,                   // 火箭飞行兵（盟，兵营生产的空中步兵）
    // 海军
    Destroyer, Typhoon, Aegis,   // 驱逐舰(盟) / 台风潜艇(苏) / 中华神盾舰(中)
    SeaScorpion,                 // 海蝎（苏中，防空快艇）
    Dreadnought,                 // 无畏级战舰（苏，远程导弹轰炸）
    AircraftCarrier,             // 航空母舰（盟，超远程舰载机打击）
    AmphTransport,               // 两栖运输船（通用）
    COUNT
};

// 飞行高度（像素，仅渲染偏移）
constexpr int AIR_ALT = 24;

// ===================== 建筑类型 =====================
enum class BldType : uint8_t {
    ConYard = 0,
    PowerPlant, TeslaReactor, NuclearReactor,   // 电厂（盟/苏/中大）
    Barracks, WarFactory, OreRefinery,
    Radar, BattleLab,                           // 雷达 / 高科
    AirForceCmd,                                // 空指部(盟)
    NavalYard,                                  // 海军船厂（须建于水面）
    Pillbox, SentryGun, PrismTower, TeslaCoil,  // 防御
    FlakCannon, GrandCannon,                    // 高炮/巨炮
    PatriotMissile,                             // 爱国者飞弹（盟，对空防御）
    Wall,                                       // 围墙（通用，廉价阻挡）
    OrePurifier, IndustrialPlant,               // 矿石精炼器 / 工业工厂
    NukeSilo, WeatherDevice, IronCurtain,       // 超武：核弹井 / 天气控制器 / 铁幕装置
    ChronoSphere,                               // 超时空传送仪（盟，传送车辆）
    // 中立科技建筑（地图生成，工程师占领，不可建造：factionMask=0）
    OilDerrick,                                 // 科技油井：持续资金
    Hospital,                                   // 医院：步兵持续回血
    MachineShop,                                // 机械商店：车辆持续维修
    COUNT
};

// ===================== 超级武器 =====================
enum class SWType : uint8_t { Nuke = 0, Lightning, IronCurtain, ChronoShift, COUNT };

struct SWDef {
    SWType type;
    const char* name;
    int chargeTime;     // 充能帧数
    BldType fromBld;    // 提供建筑
};

const SWDef& swDef(SWType t);

// 建筑提供的超武（COUNT = 无）
SWType bldProvidesSW(BldType t);

// ===================== 武器 =====================
struct WeaponDef {
    int damage = 10;
    int range = 5;           // 格
    int cooldown = 30;       // 逻辑帧
    bool antiAir = false;
    bool antiGround = true;
    const char* projSprite = "shell"; // shell/bullet/tesla/prism/missile
    float vsInfantry = 1.0f; // 伤害系数
    float vsVehicle = 1.0f;
    float vsBuilding = 1.0f;
    bool navalOnly = false;  // 仅攻击水上目标（潜艇鱼雷）
    float splash = 0;        // 溅射半径（格，0=单体；V3 火箭范围杀伤）
};

// ===================== 单位定义 =====================
enum class Armor : uint8_t { None, Light, Heavy, Building };
enum class MoveType : uint8_t { Infantry, Vehicle, Air, Naval, Amphibious };

struct UnitDef {
    UnitType type;
    const char* name;
    int cost;
    int buildTime;      // 逻辑帧
    int hp;
    int speed;          // 每逻辑帧移动 1/speed 格（越大越慢）
    int sight;          // 视野半径
    Armor armor;
    MoveType move;
    WeaponDef weapon;
    uint8_t factionMask;   // bit: 1<<Faction
    BldType prereq;        // 前置建筑（COUNT = 仅需生产建筑）
    int ammo = 0;          // 弹药数（0=无限；战机返航装填）
    int cargoCap = 0;      // 运载容量（0=非运输单位）
    bool isInfantry() const { return move == MoveType::Infantry; }
    bool isAir() const { return move == MoveType::Air; }
    bool isNaval() const { return move == MoveType::Naval; }
    bool isAmphib() const { return move == MoveType::Amphibious; }
    bool canHarvet() const { return type == UnitType::Harvester; }
    // 寻路域：0 陆地 1 水面 2 两栖
    int pathDomain() const { return isNaval() ? 1 : (isAmphib() ? 2 : 0); }
    // 生产队列类别：0 步兵 1 车辆 2 空军 3 海军（各自独立排队）
    int prodCat() const {
        if (isInfantry()) return 0;
        if (isNaval() || isAmphib()) return 3;
        if (isAir()) return 2;
        return 1;
    }
};

// 特殊武器（运行时切换）：重装大兵部署后的反装甲炮
const WeaponDef& ggiDeployedWeapon();

// ===================== 建筑定义 =====================
struct BldDef {
    BldType type;
    const char* name;
    int cost;
    int buildTime;
    int hp;
    int w, h;           // 占地（格）
    int power;          // 正=产出 负=消耗
    int sight;
    WeaponDef weapon;   // 防御建筑用；无武器 damage=0
    uint8_t factionMask;
    BldType prereq;
    bool capturable;
};

const UnitDef& unitDef(UnitType t);
const BldDef& bldDef(BldType t);

// 生产建筑判断
bool isFactoryFor(BldType b, const UnitDef& u); // 兵营产步兵，重工产车辆
// 某阵营可建造的列表
std::vector<BldType> buildableBlds(Faction f);
std::vector<UnitType> trainableUnits(Faction f, bool naval = false);

// 建筑占地每格相对坐标
inline std::vector<Vec2i> bldFootprint(const BldDef& d) {
    std::vector<Vec2i> v;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++) v.push_back({x, y});
    return v;
}
