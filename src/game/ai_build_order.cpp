#include "game/ai.h"
#include <algorithm>
#include <array>

// ===================== 建造序列（人格驱动） =====================
void SkirmishAI::doBuildOrder(World& w) {
    Player& p = w.players[player];
    // RA2 双队列：建筑队列忙时仍可开防御，反之亦然
    bool naval = detectWater(w);

    // 阵营差异化建筑类型
    BldType powerT    = p.faction == Faction::Allies ? BldType::PowerPlant
                      : p.faction == Faction::Yuri   ? BldType::BioReactor
                      : BldType::TeslaReactor;
    BldType defT      = p.faction == Faction::Allies ? BldType::Pillbox
                      : p.faction == Faction::Yuri   ? BldType::GatlingCannon
                      : BldType::SentryGun;
    BldType advDefT   = p.faction == Faction::Allies ? BldType::PrismTower
                      : p.faction == Faction::Yuri   ? BldType::PsychicTower
                      : BldType::TeslaCoil;
    BldType bigPowerT = p.faction == Faction::Allies ? BldType::PowerPlant
                      : p.faction == Faction::Yuri   ? BldType::BioReactor
                      : BldType::NuclearReactor;
    // 尤里超武1：心灵控制仪（替代盟军天气控制器/苏军核弹井）
    BldType swT  = p.faction == Faction::Allies ? BldType::WeatherDevice
                 : p.faction == Faction::Yuri   ? BldType::PsychicDominator
                 : BldType::NukeSilo;
    // 超武2 阵营替换：尤里基因突变器（映射到 IronCurtain SW）
    BldType sw2T = p.faction == Faction::Yuri ? BldType::GeneticMutator : BldType::IronCurtain;

    // 电力不足优先补电
    if (p.powerMade - p.powerUsed < 30 && w.hasBld(player, BldType::ConYard) && !p.bldProd.active) {
        BldType want = w.hasBld(player, BldType::BattleLab) ? bigPowerT : powerT;
        if (w.prereqMet(player, bldDef(want)) && p.money >= w.buildingProductionCost(player,want)) {
            w.startBldProd(player, want);
            return;
        }
    }

    auto queueFree = [&](BldType t) {
        return !(isDefenseBld(t) ? p.defProd.active : p.bldProd.active);
    };

    // 用户自定义建造序列（rules.ini [AIBuild.<Faction>] BuildOrder=...）
    const AIBuildConfig& aiCfg = g_aiBuild[(int)p.faction];
    if (aiCfg.enabled && !aiCfg.buildOrder.empty()) {
        std::array<int,(size_t)BldType::COUNT> required{};
        for (const std::string& name : aiCfg.buildOrder) {
            BldType t;
            if (!bldTypeByName(name.c_str(), t) || t==BldType::COUNT) continue;
            // Each occurrence is one requested building at this point in the
            // sequence, including deliberate repeated facilities.
            const int want=++required[(size_t)t];
            if (!queueFree(t)) continue;
            const BldDef& d = bldDef(t);
            if (!w.modeAllowsBuilding(player, t)) continue;
            if (t == BldType::NavalYard && (!naval || !navalSiteAvailable(w))) continue;
            if (w.countBlds(player, t) >= want) continue;
            if (!w.prereqMet(player, d)) continue;
            if (p.money < Credits(w.buildingProductionCost(player,t)) + 300) continue;
            w.startBldProd(player, t);
            return;
        }
        return;  // 自定义序列遍历完毕，不回退到内置序列
    }

    if(buildAirDefense(w))return;

    // ---- 人格驱动的内置建造序列 ----
    bool skipNuclearReactor = false, skipServiceDepot = false, skipLateTech = false;
    bool skipNavalYard = false, skipBattleLab = false;
    int refineryWant = 2;
    // 防御建筑数量按 defensePriority 缩放（base=2）
    int defWant = (int)(2 * pcfg.defensePriority);
    if (defWant < 1) defWant = 1;
    if (defWant > 4) defWant = 4;
    // 科技冲刺：高 techRush 时 BattleLab 前置（在船厂/防御之前）；低 techRush 时延后
    bool battleLabEarly = (pcfg.techRush >= 1.5f);
    if (pcfg.techRush < 0.5f) skipBattleLab = true;  // 极慢科技：延后高科

    switch (personality) {
        case AIPersonality::Rusher:
            skipNuclearReactor = true;
            refineryWant = 1;        // 跳过第二矿厂
            skipServiceDepot = true;
            skipLateTech = true;     // 跳过复制中心/裂缝/间谍卫星/心灵探测器/碉堡
            skipBattleLab = false;   // Rusher 仍建高科（出英雄/特种单位）
            defWant = 2;             // 建造 2 个防御
            break;
        case AIPersonality::Turtler:
            defWant = 3;             // 3 个防御
            battleLabEarly = true;   // 高科前置（在船厂之前）
            break;
        case AIPersonality::Steamroller:
            refineryWant = 3;        // 早建 2、3 号矿厂
            defWant = 1;             // 少建防御
            break;
        case AIPersonality::Technician:
            skipNavalYard = true;    // 跳过船厂
            skipLateTech = true;     // 跳过额外防御/杂项
            defWant = 1;             // 跳过额外防御
            battleLabEarly = true;   // 极速冲高科
            break;
        default: break;  // Balanced：保持默认
    }

    // 高科前置（Turtler/Technician）：在船厂/防御之前尝试建造 BattleLab
    if (battleLabEarly && !skipBattleLab && !w.hasBld(player, BldType::BattleLab) && queueFree(BldType::BattleLab)) {
        const BldDef& d = bldDef(BldType::BattleLab);
        if ((d.factionMask & (1 << (int)p.faction)) && w.prereqMet(player, d) && p.money >= Credits(w.buildingProductionCost(player,BldType::BattleLab)) + 300) {
            w.startBldProd(player, BldType::BattleLab);
            return;
        }
    }

    // 基础建造序列：电厂→矿厂→兵营→重工→矿厂→矿厂→雷达→船厂→防御→空指→高科防御→高科→...
    // 第 2 个 OreRefinery 为 2 号矿厂；第 3 个 OreRefinery（Steamroller 早建）
    static const BldType order[] = {
        BldType::TeslaReactor,  // 苏系电厂（中国也是）；盟军在下文替换
        BldType::OreRefinery,
        BldType::Barracks,
        BldType::WarFactory,
        BldType::OreRefinery,   // 第二矿厂
        BldType::OreRefinery,   // 第三矿厂（仅 Steamroller 会建；其他人格 want<=2 跳过）
        BldType::Radar,
        BldType::NavalYard,     // 海军船厂（仅水域图，前置=重工）
        BldType::SentryGun,
        BldType::AirForceCmd,   // 空指部：解锁战机
        BldType::TeslaCoil,
        BldType::BattleLab,
        BldType::ServiceDepot,   // 维修厂（通用，前置=重工，自动修理车辆）
        BldType::RobotControl,   // 机器人指挥中心（盟，遥控坦克前置）
        BldType::Grinder,        // 回收炉（尤里专属，前置=重工，回收单位换钱；其他阵营自动跳过）
        BldType::CloningVat,     // 复制中心（尤里；其他阵营 factionMask 自动跳过）
        BldType::GapGenerator,   // 裂缝产生器（盟，苏军自动跳过）
        BldType::SpySat,         // 间谍卫星（盟）
        BldType::PsychicSensor,  // 心灵探测器（苏）
        BldType::BattleBunker,   // 战斗碉堡（苏）
        BldType::TankBunker,     // 坦克碉堡（苏）
        BldType::NuclearReactor,
        BldType::NukeSilo,      // 超武1：核弹井（盟替换为天气控制器，尤里替换为心灵控制仪）
        BldType::TeslaCoil,
        BldType::IronCurtain,   // 超武2：铁幕（盟军无；尤里替换为基因突变器）
    };
    int refineryStage=0;
    for (BldType t : order) {
        BldType rt = t;
        if (t == BldType::TeslaReactor) rt = powerT;
        if (t == BldType::SentryGun) rt = defT;
        if (t == BldType::TeslaCoil) rt = advDefT;
        if (t == BldType::NuclearReactor) rt = bigPowerT;
        if (t == BldType::NukeSilo) rt = swT;
        if (t == BldType::IronCurtain) rt = sw2T;

        // 人格跳过
        if (t == BldType::NuclearReactor && skipNuclearReactor) continue;
        if (t == BldType::ServiceDepot && skipServiceDepot) continue;
        if (t == BldType::NavalYard && (skipNavalYard || !naval || !navalSiteAvailable(w))) continue;
        if (t == BldType::BattleLab && skipBattleLab) continue;
        bool isLateTech = (t == BldType::Grinder || t == BldType::CloningVat || t == BldType::GapGenerator
                        || t == BldType::SpySat || t == BldType::PsychicSensor
                        || t == BldType::BattleBunker || t == BldType::TankBunker
                        || t == BldType::RobotControl);
        if (isLateTech && skipLateTech) continue;

        const BldDef& d = bldDef(rt);
        if (!w.modeAllowsBuilding(player, rt)) continue;
        if (!queueFree(rt)) continue;
        // 已建够数量则跳过；防御建筑允许 defWant 个，矿厂允许 refineryWant 个
        int want;
        if (rt == defT || rt == advDefT) want = defWant;
        // Expansion entries occur after barracks and the first factory. Do
        // not satisfy the entire eventual refinery cap at the opening entry.
        else if (rt == BldType::OreRefinery) want = std::min(refineryWant,++refineryStage);
        else want = 1;
        if (w.countBlds(player, rt) >= want) continue;
        if (!w.prereqMet(player, d)) continue;
        if (p.money < Credits(w.buildingProductionCost(player,rt)) + 300) continue;  // 留点余钱
        w.startBldProd(player, rt);
        return;
    }
}

