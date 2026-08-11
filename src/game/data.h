#pragma once
#include <cstdint>
#include <algorithm>
#include <string>
#include <vector>
#include "core/util.h"

// ===================== 阵营 =====================
enum class Faction : uint8_t { Allies = 0, Soviet = 1, China = 2, Yuri = 3 };

inline const char* factionName(Faction f) {
    switch (f) {
        case Faction::Allies: return "盟军";
        case Faction::Soviet: return "苏联";
        case Faction::China:  return "中国";
        case Faction::Yuri:   return "尤里";
    }
    return "?";
}

// ===================== 国家（RA2 原作：阵营内细分，各有特色单位/能力） =====================
enum class Country : uint8_t {
    None = 0,
    America, Korea, France, Germany, UK,     // 盟军系
    Russia, Cuba, Libya, Iraq,               // 苏联系
    China,                                   // 中国（无细分）
    Yuri,                                    // 尤里（无细分）
    COUNT
};

// 该国家归属的阵营（中国 → Faction::China）
inline Faction countryFaction(Country c) {
    switch (c) {
        case Country::America: case Country::Korea: case Country::France:
        case Country::Germany: case Country::UK: return Faction::Allies;
        case Country::Russia: case Country::Cuba:
        case Country::Libya:  case Country::Iraq: return Faction::Soviet;
        case Country::Yuri:   return Faction::Yuri;
        default: return Faction::China;
    }
}

// 阵营可选国家列表（盟军 5 国 / 苏联 4 国 / 中国 1 国 / 尤里 1 国）
inline std::vector<Country> countriesOf(Faction f) {
    switch (f) {
        case Faction::Allies: return {Country::America, Country::Korea, Country::France, Country::Germany, Country::UK};
        case Faction::Soviet: return {Country::Russia, Country::Cuba, Country::Libya, Country::Iraq};
        case Faction::Yuri:   return {Country::Yuri};
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
    // ---- 尤复阵营：尤里专属单位 ----
    Initiate,                    // 尤里新兵（心灵火焰反步兵）
    Brute,                       // 狂兽人（近战重甲步兵）
    Virus,                       // 病毒狙击手（远程反步兵）
    LasherTank,                  // 狂风坦克（尤里主战坦克）
    GatlingTank,                 // 盖特坦克（防空对地速射）
    Magnetron,                   // 磁电坦克（吊起敌方车辆）
    MasterMind,                  // 主脑坦克（多重心灵控制）
    FloatingDisc,                // 飞碟（空中，吸电/瘫痪建筑）
    Boomer,                      // 雷鸣潜艇（尤里海军，导弹+鱼雷）
    // ---- 尤复补全：YR 新增单位 ----
    Boris,                       // 鲍里斯（苏英雄，AK47+呼叫米格空袭建筑）
    SiegeChopper,                // 攻城直升机（苏，飞行机枪/部署远程炮击）
    ChaosDrone,                  // 混乱机器人（尤里 Chaos Drone，地面毒气无人车）
    // ---- 尤复：奴隶矿车经济 ----
    Slave,                       // 奴隶（步兵采矿，返回精炼厂/奴隶矿车）
    SlaveMiner,                   // 奴隶矿车（尤里采矿车；展开后作卸货点并自动产奴）
    // ---- YR 补洞：英雄 / 渗透 ----
    YuriPrime,                   // 尤里首脑（英雄；远程心控，可控建筑）
    ChronoIvan,                  // 超时空伊文（渗透苏军实验室；炸弹+超时空机动）
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
    CloningVat,                                 // 复制中心（尤里，步兵产出时免费复制一个）
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
    // ---- 尤复阵营：尤里专属建筑 ----
    BioReactor,                                 // 生化反应堆（尤里电厂，步兵进驻增电）
    GatlingCannon,                              // 盖特机炮（尤里防御，防空对地速射）
    Grinder,                                    // 回收炉（尤里支持，回收单位换钱）
    GeneticMutator,                             // 基因突变器（尤里超武1：把步兵变狂兽人）
    PsychicDominator,                           // 心灵控制仪（尤里超武2：范围心灵控制+伤害）
    // ---- 尤复补全：YR 新增建筑 ----
    PsychicTower,                               // 心灵控制塔（尤里防御，自动控制接近的敌方单位）
    RobotControl,                               // 机器人指挥中心（YR 盟军；遥控坦克前置，断电则停摆）
    TechPowerPlant,                             // 科技电厂（中立，占领后提供 200 电力）
    TechOutpost,                                // 科技前哨站（中立，占领后维修/治疗周边单位）
    // ---- 战役专用装置（不可建造；地图/触发器放置）----
    PsychicBeacon,                              // 心灵信标：范围心控；摧毁以解放
    PsychicAmplifier,                           // 心灵放大器：更大范围心控（限时关）
    TimeMachine,                                // 时间机器（YR）：需供电；可损毁脚本装置
    COUNT
};

// ===================== 超级武器 =====================
enum class SWType : uint8_t { Nuke = 0, Lightning, IronCurtain, ChronoShift, GeneticMutator, PsychicDominator, ForceShield, COUNT };

struct SWDef {
    SWType type;
    const char* name;
    int chargeTime;     // 充能帧数
    BldType fromBld;    // 提供建筑
};

const SWDef& swDef(SWType t);

// 建筑提供的超武（COUNT = 无）
SWType bldProvidesSW(BldType t);

// ===================== 武器 / 弹道（YR rulesmd Projectile 子集） =====================
// SpeedLeptons：原作 leptons/帧（1 格=256）；Arcing 强制 50。
// OpenRA2@30fps：cells/tick = leptons/512（对齐原作 15fps 墙钟位移）。
struct WeaponDef;

struct ProjectileDef {
    const char* name = "Cannon";
    const char* image = "120MM";
    bool arcing = false;      // 抛物线，瞄开火落点，不追踪
    bool inviso = false;      // 瞬时命中（无飞行体）
    bool vertical = false;    // 垂直弹（基洛夫等）
    bool inaccurate = false;  // 开火时散布落点
    bool proximity = false;   // 近炸
    int rot = 0;              // >0 制导；0 非追踪
    bool aa = false;
    bool ag = true;
    int speedLeptons = 40;
};

const ProjectileDef& projectileDef(const char* name);
const ProjectileDef& projectileForWeapon(const WeaponDef& w);
void loadProjectiles(const char* path);

struct WeaponDef {
    int damage = 10;
    int range = 5;           // 格
    int cooldown = 30;       // 逻辑帧
    bool antiAir = false;
    bool antiGround = true;
    const char* projSprite = "shell"; // 表现/桥接标签
    float vsInfantry = 1.0f;
    float vsVehicle = 1.0f;
    float vsBuilding = 1.0f;
    bool navalOnly = false;
    float splash = 0;
    enum class Warhead : uint8_t { Legacy, SmallArms, AP, HE, HollowPoint, Psychic, Radiation, COUNT };
    Warhead warhead = Warhead::Legacy;
    const char* report = "";
    // 原作弹道（可选；默认由 Proj 桥接到 projectiles.ini）
    const char* projectile = ""; // Projectile.* 名
    int speedLeptons = -1;       // <0 用弹道默认；武器 Speed（leptons/帧）
};

// ===================== 单位定义 =====================
// 原版 rules(md).ini 装甲域；Building 是旧配置名，作为 Concrete 的兼容别名。
enum class Armor : uint8_t {
    None, Flak, Plate, Light, Medium, Heavy, Wood, Steel, Concrete, Special1, Special2,
    Building = Concrete,
    COUNT = 11
};
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
    bool canHarvet() const {
        return type == UnitType::Harvester || type == UnitType::ChronoMiner
            || type == UnitType::WarMiner || type == UnitType::Slave || type == UnitType::SlaveMiner;
    }
    // RA2 原作：海豹部队/谭雅可游泳渡水（两栖步兵，仍走兵营队列）
    bool canSwim() const { return type == UnitType::NavySEAL || type == UnitType::Tanya; }
    // RA2 原作：C4 爆破手（近身爆破建筑；会游泳的还可炸舰船）
    bool hasC4() const { return type == UnitType::Tanya || type == UnitType::NavySEAL
                          || type == UnitType::ChronoCommando || type == UnitType::PsiCommando; }
    // RA2 原作：心灵控制者（尤里/心灵突击队）
    bool isPsychic() const { return type == UnitType::Yuri || type == UnitType::PsiCommando
                                 || type == UnitType::YuriPrime; }
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
// 攻城直升机部署后：远程重炮（对建筑/车辆强力，溅射）
const WeaponDef& siegeChopperDeployedWeapon();

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

// 独立炮塔（车体朝移动方向、炮塔朝目标）：模拟层边走边打用，勿依赖 gfx
bool unitHasTurret(UnitType t);

// ===================== 外部规则加载（素材外置化） =====================
// 启动时读取 assets/rules/rules.ini，逐项覆盖内置数值；文件/键缺失保持内置默认。
// INI 节格式：[Unit.Grizzly] [Bld.ConYard] [SW.Nuke] [DeployWeapon.GI]，键见 assets/README.txt。
void loadRules(const char* path);
void loadProjectiles(const char* path);
// 内容根叠加载：按 contentResolveStack 顺序依次 patch（后写覆盖）
void loadRulesFromContent();
void loadProjectilesFromContent();

// 自定义单位变体（rules.ini [Unit.XXX] 带 Base= 键；或 units.csv）
// 变体复用基础类型的全部游戏逻辑，仅覆盖数值；可通过 Lua / 战役 / 地图名调用
// Buildable: 0=仅刷出名 1=仅刷出名(yes) 2=replace 写回 Base 的 UnitDef（遭遇战侧栏生效）
struct UnitVariant {
    std::string name;            // 变体名（如 "HeavyGrizzly"）
    UnitType base = UnitType::COUNT; // 基础类型
    int cost = -1;               // -1=不覆盖
    int hp = -1;
    int speed = -1;
    int sight = -1;
    std::string weaponProj;      // 覆盖武器贴图
    int weaponDmg = -1;
    int weaponRange = -1;
    int weaponCooldown = -1;
    float vsInf = -1, vsVeh = -1, vsBld = -1;
    std::string displayName;     // 可选显示名
    std::string art;             // 可选贴图 stem（unit_<art>_*.png）；空=用 base
    int buildable = 0;           // 0=no 1=yes 2=replace
};
extern std::vector<UnitVariant> g_variants;
const UnitVariant* findVariant(const std::string& name);

// 自定义建筑变体（[Bld.XXX] Base=ConYard ...；或 buildings.csv）
struct BldVariant {
    std::string name;
    BldType base = BldType::COUNT;
    int cost = -1;
    int hp = -1;
    bool overridePower = false;
    int power = 0;
    std::string displayName;
    std::string art;
    int buildable = 0; // 0=no 1=yes 2=replace
};
extern std::vector<BldVariant> g_bldVariants;
const BldVariant* findBldVariant(const std::string& name);

// 解析生成名：枚举名或变体名 → 基础类型（变体可选返回）
bool resolveUnitSpawn(const char* name, UnitType& out, const UnitVariant** varOut = nullptr);
bool resolveBldSpawn(const char* name, BldType& out, const BldVariant** varOut = nullptr);

// 规则导出（--export-assets）：把内置数值全量写成 rules.ini 模板
void exportRules(const char* path);

// ===================== 全局游戏规则（rules.ini [GameRules] 节） =====================
struct GameRules {
    int maxMoney = 999999;       // 资金上限
    int startingMoneyCap = 50000; // 遭遇战初始资金上限
    float lowPowerSpeedFactor = 0.5f; // 低电时生产速度系数
    float veteranismDmgBonus[3] = {1.0f, 1.1f, 1.3f}; // 新兵/老兵/精英伤害加成
    float veteranArmorBonus[3] = {1.0f, 0.8f, 0.6f};  // 承伤倍率
    float veteranSpeedBonus[3] = {1.0f, 1.2f, 1.4f};  // 移速倍率
    float veteranRofBonus[3] = {1.0f, 0.8f, 0.6f};    // ROF 间隔倍率
    int veteranSelfHeal[3] = {0, 0, 2};                // 每次自愈量（仅精英/三级）
    float veteranRatio = 3.0f;                         // 每级所需摧毁价值 / 自身价值
    int bioReactorPowerPerOccupant = 100;
    float grinderRefund = 1.0f;
    float warheadVerses[(int)WeaponDef::Warhead::COUNT][(int)Armor::COUNT] = {};
    int crateInterval = 1800;    // 补给箱生成间隔（帧）
    int oreRegrowRate = 1;       // 矿脉再生速率
    GameRules() {
        for (auto& row : warheadVerses) for (float& v : row) v = 1.0f;
        const float small[] = {1,1,1,.5f,.25f,.25f,.75f,.25f,.1f,1,1};
        const float ap[] = {.25f,.25f,.25f,1,1,1,.75f,.75f,.5f,1,1};
        const float he[] = {1,1,1,.75f,.5f,.25f,1,.75f,.5f,1,1};
        const float hp[] = {1,1,1,.05f,.05f,.05f,.05f,.05f,.05f,1,1};
        const float psi[] = {1,1,1,1,1,1,0,0,0,0,0};
        const float rad[] = {1,1,1,.2f,.1f,.1f,.05f,.05f,.05f,0,0};
        const float* rows[] = {nullptr, small, ap, he, hp, psi, rad};
        for (int w = 1; w < (int)WeaponDef::Warhead::COUNT; ++w)
            for (int a = 0; a < (int)Armor::COUNT; ++a) warheadVerses[w][a] = rows[w][a];
    }
};
extern GameRules g_gameRules;

inline bool isHero(UnitType t) {
    return t == UnitType::Tanya || t == UnitType::Boris || t == UnitType::YuriPrime;
}

// 民房可进驻步兵（RA2/YR：大兵系；中国解放军=动员兵对位；尤里新兵；重装大兵）
inline bool canGarrisonCivHouse(UnitType t) {
    switch (t) {
        case UnitType::GI: case UnitType::Conscript: case UnitType::PLA:
        case UnitType::Initiate: case UnitType::GuardianGI:
            return true;
        default: return false;
    }
}

// Legacy 武器继续读取 VsInf/VsVeh/VsBld；显式 Warhead 则按目标装甲矩阵结算。
inline float weaponVsArmor(const WeaponDef& w, Armor armor, bool infantry, bool building) {
    if (w.warhead == WeaponDef::Warhead::Legacy)
        return building ? w.vsBuilding : (infantry ? w.vsInfantry : w.vsVehicle);
    int wi = (int)w.warhead, ai = std::clamp((int)armor, 0, (int)Armor::COUNT - 1);
    return g_gameRules.warheadVerses[wi][ai];
}

// 核心经规则统一入口：宝石为普通矿的 2 倍；矿石精炼器提高 25% 收益；
// 工业工厂仅降低战车工厂车辆造价 25%，绝不改变生产速度。
inline int oreUnitValue(bool gems) {
    return gems ? 70 : 35;
}
inline int oreIncomeWithPurifier(int value, bool hasPurifier) {
    return hasPurifier ? value * 125 / 100 : value;
}
inline int industrialPlantUnitCost(int baseCost, bool isWarFactoryVehicle, bool hasIndustrialPlant) {
    return isWarFactoryVehicle && hasIndustrialPlant ? baseCost * 75 / 100 : baseCost;
}

// ===================== AI 建造序列定制（rules.ini [AIBuild.*] 节） =====================
// 用户可定制各阵营 AI 的建造顺序与生产偏好，覆盖内置默认
struct AIBuildConfig {
    bool enabled = false;        // 是否使用自定义序列（rules.ini 中定义则 true）
    std::vector<std::string> buildOrder; // 建筑枚举名顺序（如 PowerPlant,OreRefinery,...）
    int harvesterTarget = 3;     // 维持采矿车数量
    int attackWaveSize = 8;      // 进攻波次最小单位数
    bool saveForSuperWeapon = true; // 攒钱建超武
};
extern AIBuildConfig g_aiBuild[4]; // 索引 = (int)Faction

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
// 可设集结点的生产建筑（兵营/重工/船厂/空指/精炼厂/复制中心）
inline bool isRallyBuilding(BldType t) {
    return t == BldType::Barracks || t == BldType::WarFactory || t == BldType::NavalYard
        || t == BldType::AirForceCmd || t == BldType::OreRefinery || t == BldType::CloningVat;
}
// 某阵营可建造的列表
std::vector<BldType> buildableBlds(Faction f);
std::vector<UnitType> trainableUnits(Faction f, bool naval = false);
// 国家特色判定：该国家是否有伞兵技能（美国/占领科技机场）
inline bool countryHasParadrop(Country c) { return c == Country::America; }
// 阵营对应的采矿车类型（RA2 原作：盟军超时空矿车 / 苏军武装矿车 / 尤里奴隶矿车）
inline UnitType harvesterType(Faction f) {
    switch (f) {
        case Faction::Allies: return UnitType::ChronoMiner;
        case Faction::Soviet: return UnitType::WarMiner;
        case Faction::Yuri:   return UnitType::SlaveMiner;
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
        case BldType::BattleBunker:
        case BldType::BioReactor: return 1;
        case BldType::TankBunker: return 2;
        case BldType::Grinder: return 3; // 己方单位进入后立即回收
        default: return 0;
    }
}

// RA2：防御页签 / 独立防御建造队列（含支援防御与超武建筑）
inline bool isDefenseBld(BldType t) {
    switch (t) {
        case BldType::Pillbox: case BldType::SentryGun: case BldType::FlakCannon:
        case BldType::GatlingCannon: case BldType::PrismTower: case BldType::TeslaCoil:
        case BldType::PsychicTower: case BldType::GrandCannon: case BldType::PatriotMissile:
        case BldType::Wall: case BldType::BattleBunker: case BldType::TankBunker:
        case BldType::GapGenerator: case BldType::SpySat: case BldType::PsychicSensor:
        case BldType::NukeSilo: case BldType::WeatherDevice: case BldType::IronCurtain:
        case BldType::ChronoSphere: case BldType::GeneticMutator: case BldType::PsychicDominator:
        case BldType::PsychicBeacon: case BldType::PsychicAmplifier:
            return true;
        default: return false;
    }
}

// 每玩家限一（精炼器 / 克隆缸 / 各超武建筑）
inline bool isUniqueBld(BldType t) {
    switch (t) {
        case BldType::OrePurifier: case BldType::CloningVat:
        case BldType::NukeSilo: case BldType::WeatherDevice: case BldType::IronCurtain:
        case BldType::ChronoSphere: case BldType::GeneticMutator: case BldType::PsychicDominator:
        case BldType::PsychicBeacon: case BldType::PsychicAmplifier: case BldType::TimeMachine:
            return true;
        default: return false;
    }
}

// 战役心灵装置：信标/放大器对附近敌军单位施加范围心控
inline bool isPsychicAreaBld(BldType t) {
    return t == BldType::PsychicBeacon || t == BldType::PsychicAmplifier;
}

// 心灵控制免疫（RA2 原作）：军犬/心灵单位/机器人（恐怖机器人/遥控坦克）/采矿车/英雄（谭雅）/战斗要塞
inline bool psychicImmune(UnitType t) {
    switch (t) {
        case UnitType::AttackDog: case UnitType::Yuri: case UnitType::PsiCommando:
        case UnitType::YuriPrime:
        case UnitType::TerrorDrone: case UnitType::RobotTank:
        case UnitType::Harvester: case UnitType::ChronoMiner: case UnitType::WarMiner:
        case UnitType::SlaveMiner: case UnitType::Slave:
        case UnitType::Tanya: case UnitType::BattleFortress:
        case UnitType::Boris: case UnitType::ChaosDrone:
            return true;
        default: return false;
    }
}

// 偷科技单位（渗透敌作战实验室解锁，见 Player::stolenTech）
// bit0=超时空突击队（盟高科） bit1=心灵突击队（苏/中高科） bit2=超时空伊文（苏高科）
inline int stolenTechBit(UnitType t) {
    if (t == UnitType::ChronoCommando) return 1;
    if (t == UnitType::PsiCommando) return 2;
    if (t == UnitType::ChronoIvan) return 4;
    return 0;
}
