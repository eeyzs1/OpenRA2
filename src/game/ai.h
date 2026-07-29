#pragma once
#include "game/world.h"

// ===================== AI 难度（4 档） =====================
enum class AIDiff : uint8_t {
    Easy = 0,    // 思考慢(30帧) 进攻阈值高(15+) 不用超武 不用工程师/间谍 采矿车少
    Normal = 1,  // 思考中(15帧) 进攻阈值中(8+)  偶用超武 基础工程师
    Hard = 2,    // 思考快(10帧) 进攻阈值低(6+)  常用超武 混编部队 集火 残血撤退
    Brutal = 3,  // 思考极快(7帧) 进攻阈值极低(5+) 资源加成 完美微操 扩张基地
};

// ===================== AI 人格（5 种战术风格） =====================
enum class AIPersonality : uint8_t {
    Balanced = 0,    // 均衡型：标准建造序列，攻守兼备
    Rusher = 1,      // 速攻型：早期暴兵，轻科技，快速进攻，少防御
    Turtler = 2,     // 龟缩型：重防御，科技冲刺，超武优先，被动防守
    Steamroller = 3, // 轰压型：大规模集结后碾压，慢但猛，多矿厂多重工
    Technician = 4,  // 科技型：极速冲高科，英雄/特种单位为主，少量精锐
};

// 人格配置参数
struct AIPersonalityConfig {
    float aggression = 1.0f;       // 进攻欲望倍率（阈值 = base / aggression）
    float defensePriority = 1.0f;  // 防御建筑优先级倍率
    float techRush = 1.0f;         // 科技冲刺速度（高科/超武优先）
    float econFocus = 1.0f;        // 经济投入倍率（采矿车数量/矿厂数量）
    float unitMix = 1.0f;          // 兵种混编程度（1.0=标准混编）
    int rallySize = 8;             // 进攻前集结单位数
    bool useEngineers = true;      // 是否主动使用工程师
    bool useSpies = true;          // 是否使用间谍
    bool useSuperWeapon = true;    // 是否使用超武
    bool expandBase = false;       // 是否扩张分基地
    bool retreatDamaged = false;   // 是否残血撤退
    bool focusFire = false;        // 是否集火
};

// 遭遇战 AI：建造顺序 + 持续暴兵 + 波次进攻 + 战术微操
class SkirmishAI {
public:
    int player = -1;
    int thinkTimer = 0;
    int attackWave = 0;
    int attackTimer = 0;
    AIDiff difficulty = AIDiff::Normal;
    AIPersonality personality = AIPersonality::Balanced;
    AIPersonalityConfig pcfg;      // 人格参数（由 personality 初始化，可被 rules.ini 覆盖）
    int8_t hasWater = -1;
    int8_t navalPlaceable = -1;
    int navalCheckCd = 0;
    int navalFail = 0;
    // 战术状态
    Vec2i rallyPoint = {-1, -1};   // 进攻集结点
    int rallyCheckTimer = 0;       // 集结点检查计时
    int expandTimer = 0;           // 扩张冷却（避免频繁造 MCV）
    int defenseCheckTimer = 0;     // 防御反应计时
    EID lastAttacker = INVALID_EID; // 上次攻击我方基地的敌方单位（防御反应用）

    void reset(int p);
    void update(World& w);

    // 初始化人格参数
    void initPersonality();

private:
    void doBuildOrder(World& w);
    void doProduction(World& w);
    void doAttack(World& w);
    void doSuperWeapon(World& w);
    void doSupport(World& w);
    void doEngineers(World& w);
    void doTactics(World& w);       // 战术微操：集火/残血撤退/防御反应
    void doExpansion(World& w);     // 分基地扩张
    bool tryPlaceBld(World& w, BldType t);
    bool detectWater(World& w);
    bool navalSiteAvailable(World& w);
    Vec2i findArmyCenter(World& w);
    int countArmy(World& w);
    int thinkInterval() const;      // 根据难度返回思考间隔
    int attackThreshold() const;    // 根据难度+人格返回进攻阈值
    float resourceBonus() const;    // Brutal 难度资源加成倍率
};
