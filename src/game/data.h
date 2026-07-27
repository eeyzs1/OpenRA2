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

// ===================== 国家（RA2 原作：阵营内细分，各有特色单位/能力） =====================
enum class Country : uint8_t {
    None = 0,
    America, Korea, France, Germany, UK,     // 盟军系
    Russia, Cuba, Libya, Iraq,               // 苏联系
    China,                                   // 中国（无细分）
    COUNT
};

// 该国家归属的阵营（中国 → Faction::China）
inline Faction countryFaction(Country c) {
    switch (c) {
        case Country::America: case Country::Korea: case Country::France:
        case Country::Germany: case Country::UK: return Faction::Allies;
        case Country::Russia: case Country::Cuba:
        case Country::Libya:  case Country::Iraq: return Faction::Soviet;
        default: return Faction::China;
    }
}

// 阵营可选国家列表（盟军 5 国 / 苏联 4 国 / 中国 1 国）
inline std::vector<Country> countriesOf(Faction f) {
    switch (f) {
        case Faction::Allies: return {Country::America, Country::Korea, Country::France, Country::Germany, Country::UK};
        case Faction::Soviet: return {Country::Russia, Country::Cuba, Country::Libya, Country::Iraq};
        default: return {Country::China};
    }
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
    // ---- RA2 补全：采矿车阵营差异化 ----
    ChronoMiner,                 // 超时空采矿车（盟，满载瞬移回精炼厂）
    WarMiner,                    // 武装采矿车（苏，带机枪可反击）
    // ---- RA2 补全：国家特色单位 ----
    TankDestroyer,               // 坦克杀手（德，反装甲专精）
    Terrorist,                   // 恐怖分子（古巴，自爆步兵）
    DemoTruck,                   // 自爆卡车（利比亚，大范围自爆）
    // ---- RA2 补全：运输/海军/特殊 ----
    Nighthawk,                   // 夜鹰直升机（盟，空中运输步兵）
    Dolphin,                     // 海豚（盟，反潜探测，音波武器）
    Squid,                       // 巨型乌贼（苏，缠绕敌舰定身）
    RobotTank,                   // 遥控坦克（盟，两栖悬浮轻坦）
    BattleFortress,              // 战斗要塞（盟，重甲运兵，载兵加成火力）
    Hornet,                      // 航母舰载机（不可生产，航母自动放飞）
    // ---- RA2 补全 P6：心灵控制/两栖步兵/偷科技 ----
    NavySEAL,                    // 海豹部队（盟，可游泳，C4 爆破建筑/舰船）
    Yuri,                        // 尤里（苏，心灵控制敌方地面单位）
    ChronoCommando,              // 超时空突击队（偷盟高科解锁：传送+C4+冲锋枪）
    PsiCommando,                 // 心灵突击队（偷苏/中高科解锁：心灵控制+C4）
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
    // ---- RA2 补全：高级建筑 ----
    CloningVat,                                 // 复制中心（苏，步兵产出时免费复制一个）
    ServiceDepot,                               // 维修厂（通用，车辆进场维修/解除寄生）
    GapGenerator,                               // 裂缝产生器（盟，大范围黑幕覆盖）
    SpySat,                                     // 间谍卫星（盟，建成后全图点亮）
    PsychicSensor,                              // 心灵探测器（苏，显示敌方攻击目标线）
    BattleBunker,                               // 战斗碉堡（苏，可进驻 5 名步兵对外射击）
    TankBunker,                                 // 坦克碉堡（苏，进驻 1 辆坦克对外射击并加防）
    // ---- RA2 补全：中立科技建筑 ----
    TechAirport,                                // 科技机场（中立，占领后获得伞兵技能）
    SecretLab,                                  // 秘密实验室（中立，占领后解锁特色单位）
    CivHouse,                                   // 民房（中立，可进驻 8 名步兵，可被摧毁）
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
    const WeaponDef* elite = nullptr; // 精英武器（RA2 原作：精英军衔武器质变；nullptr=无）
    Country countryReq = Country::None; // 国家限制（None=该阵营通用；否则仅对应国家可生产）
    bool isInfantry() const { return move == MoveType::Infantry; }
    bool isAir() const { return move == MoveType::Air; }
    bool isNaval() const { return move == MoveType::Naval; }
    bool isAmphib() const { return move == MoveType::Amphibious; }
    bool canHarvet() const { return type == UnitType::Harvester || type == UnitType::ChronoMiner || type == UnitType::WarMiner; }
    // RA2 原作：海豹部队/谭雅可游泳渡水（两栖步兵，仍走兵营队列）
    bool canSwim() const { return type == UnitType::NavySEAL || type == UnitType::Tanya; }
    // RA2 原作：C4 爆破手（近身爆破建筑；会游泳的还可炸舰船）
    bool hasC4() const { return type == UnitType::Tanya || type == UnitType::NavySEAL
                          || type == UnitType::ChronoCommando || type == UnitType::PsiCommando; }
    // RA2 原作：心灵控制者（尤里/心灵突击队）
    bool isPsychic() const { return type == UnitType::Yuri || type == UnitType::PsiCommando; }
    // 寻路域：0 陆地 1 水面 2 两栖
    int pathDomain() const { return canSwim() ? 2 : (isNaval() ? 1 : (isAmphib() ? 2 : 0)); }
    // 生产队列类别：0 步兵 1 车辆 2 空军 3 海军（各自独立排队）
    int prodCat() const {
        if (isInfantry()) return 0;
        if (type == UnitType::RobotTank) return 1; // 遥控坦克：两栖但走车辆队列
        if (isNaval() || isAmphib()) return 3;
        if (isAir()) return 2;
        return 1;
    }
};

// 特殊武器（运行时切换）：重装大兵部署后的反装甲炮
const WeaponDef& ggiDeployedWeapon();
// 美国大兵部署（沙袋工事）：射程与伤害提升
const WeaponDef& giDeployedWeapon();

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
    int garrisonCap = 0;               // 可进驻步兵数（0=不可进驻：民房/战斗碉堡）
    Country countryReq = Country::None; // 国家限制（None=该阵营通用）
};

const UnitDef& unitDef(UnitType t);
const BldDef& bldDef(BldType t);

// ===================== 外部规则加载（素材外置化） =====================
// 启动时读取 assets/rules/rules.ini，逐项覆盖内置数值；文件/键缺失保持内置默认。
// INI 节格式：[Unit.Grizzly] [Bld.ConYard] [SW.Nuke] [DeployWeapon.GI]，键见 assets/README.txt。
void loadRules(const char* path);

// 规则导出（--export-assets）：把内置数值全量写成 rules.ini 模板
void exportRules(const char* path);

// 枚举名 ↔ 枚举值（INI/地图/战役文件共用一套规范名，如 "Grizzly"、"PrismTower"）
bool unitTypeByName(const char* s, UnitType& out);
bool bldTypeByName(const char* s, BldType& out);
bool factionByName(const char* s, Faction& out);
bool countryByName(const char* s, Country& out);
bool swTypeByName(const char* s, SWType& out);
const char* unitTypeKey(UnitType t); // 规范枚举名（未知返回 "?"）
const char* bldTypeKey(BldType t);
const char* countryKey(Country c);
const char* factionKey(Faction f);
const char* swTypeKey(SWType t);

// 生产建筑判断
bool isFactoryFor(BldType b, const UnitDef& u); // 兵营产步兵，重工产车辆
// 某阵营可建造的列表
std::vector<BldType> buildableBlds(Faction f);
std::vector<UnitType> trainableUnits(Faction f, bool naval = false);
// 国家特色判定：该国家是否有伞兵技能（美国/占领科技机场）
inline bool countryHasParadrop(Country c) { return c == Country::America; }
// 阵营对应的采矿车类型（RA2 原作：盟军超时空矿车 / 苏军武装矿车）
inline UnitType harvesterType(Faction f) {
    switch (f) {
        case Faction::Allies: return UnitType::ChronoMiner;
        case Faction::Soviet: return UnitType::WarMiner;
        default: return UnitType::Harvester;
    }
}

// 建筑占地每格相对坐标
inline std::vector<Vec2i> bldFootprint(const BldDef& d) {
    std::vector<Vec2i> v;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++) v.push_back({x, y});
    return v;
}

// 建筑进驻类型（RA2 原作）：0 不可进驻 1 步兵（民房/战斗碉堡）2 车辆（坦克碉堡）
inline int garrisonDomain(BldType t) {
    switch (t) {
        case BldType::CivHouse:
        case BldType::BattleBunker: return 1;
        case BldType::TankBunker: return 2;
        default: return 0;
    }
}

// 心灵控制免疫（RA2 原作）：军犬/心灵单位/机器人（恐怖机器人/遥控坦克）/采矿车/英雄（谭雅）/战斗要塞
inline bool psychicImmune(UnitType t) {
    switch (t) {
        case UnitType::AttackDog: case UnitType::Yuri: case UnitType::PsiCommando:
        case UnitType::TerrorDrone: case UnitType::RobotTank:
        case UnitType::Harvester: case UnitType::ChronoMiner: case UnitType::WarMiner:
        case UnitType::Tanya: case UnitType::BattleFortress:
            return true;
        default: return false;
    }
}

// 偷科技单位（渗透敌作战实验室解锁，见 Player::stolenTech）
// bit0=超时空突击队（盟高科） bit1=心灵突击队（苏/中高科）
inline int stolenTechBit(UnitType t) {
    if (t == UnitType::ChronoCommando) return 1;
    if (t == UnitType::PsiCommando) return 2;
    return 0;
}
