#include "game/ai.h"
#include "game/script.h"
#include <vector>
#include <algorithm>

// ===================== 初始化 =====================
void SkirmishAI::reset(int p) {
    player = p;
    thinkTimer = 0;
    attackWave = 0;
    attackTimer = 0;
    difficulty = AIDiff::Normal;
    personality = AIPersonality::Balanced;
    hasWater = -1;
    navalPlaceable = -1;
    navalCheckCd = 0;
    navalFail = 0;
    rallyPoint = { -1, -1 };
    rallyCheckTimer = 0;
    expandTimer = 0;
    defenseCheckTimer = 0;
    lastAttacker = INVALID_EID;
    pcfg = AIPersonalityConfig{};
    initPersonality();
}

// 根据人格设置 pcfg 字段（rules.ini 覆盖后由 game.h 重新设置 personality 时再次调用）
void SkirmishAI::initPersonality() {
    pcfg = AIPersonalityConfig{};  // 先恢复默认（=Balanced）
    switch (personality) {
        case AIPersonality::Rusher:
            pcfg.aggression = 1.8f;
            pcfg.defensePriority = 0.5f;
            pcfg.techRush = 0.4f;
            pcfg.econFocus = 0.8f;
            pcfg.rallySize = 5;
            pcfg.useEngineers = false;
            pcfg.useSpies = false;
            pcfg.expandBase = false;
            // Rusher 仍建 BattleLab（轻科技但需高科解锁英雄/特种单位）
            break;
        case AIPersonality::Turtler:
            pcfg.aggression = 0.4f;
            pcfg.defensePriority = 2.0f;
            pcfg.techRush = 1.5f;
            pcfg.econFocus = 1.2f;
            pcfg.rallySize = 15;
            pcfg.useSuperWeapon = true;
            pcfg.expandBase = false;
            break;
        case AIPersonality::Steamroller:
            pcfg.aggression = 0.7f;
            pcfg.defensePriority = 0.8f;
            pcfg.techRush = 0.6f;
            pcfg.econFocus = 1.8f;
            pcfg.rallySize = 18;
            pcfg.useEngineers = true;
            pcfg.useSpies = false;
            pcfg.expandBase = true;
            break;
        case AIPersonality::Technician:
            pcfg.aggression = 0.6f;
            pcfg.defensePriority = 1.2f;
            pcfg.techRush = 2.5f;
            pcfg.econFocus = 0.7f;
            pcfg.rallySize = 4;
            pcfg.useEngineers = true;
            pcfg.useSpies = true;
            pcfg.expandBase = false;
            break;
        default: break;  // Balanced：保持默认
    }
}

// ===================== 难度参数 =====================
int SkirmishAI::thinkInterval() const {
    switch (difficulty) {
        case AIDiff::Easy:   return 30;
        case AIDiff::Normal: return 15;
        case AIDiff::Hard:   return 10;
        case AIDiff::Brutal: return 7;
    }
    return 15;
}

int SkirmishAI::attackThreshold() const {
    int base;
    switch (difficulty) {
        case AIDiff::Easy:   base = 15; break;
        case AIDiff::Normal: base = 8;  break;
        case AIDiff::Hard:   base = 6;  break;
        case AIDiff::Brutal: base = 5;  break;
        default: base = 8; break;
    }
    int t = (int)(base / pcfg.aggression);
    t += attackWave * 2;
    if (t < 3) t = 3;
    return t;
}

float SkirmishAI::resourceBonus() const {
    // 不再用思考间隔复利刷钱；Brutal 仅在采矿结算等处可接此倍率（当前不主动加钱）
    return 1.0f;
}

// ===================== 主更新 =====================
void SkirmishAI::update(World& w) {
    Player& p = w.players[player];
    if (!p.active || p.defeated) return;
    // Lua AI hook：脚本返回 true 则跳过内置 AI（用户可完全接管 AI 决策）
    if (g_script.onAiThink(player)) return;

    // 逐帧递减的冷却计时（不受思考间隔门控，保证真实时间精度）
    if (expandTimer > 0) expandTimer--;
    if (defenseCheckTimer > 0) defenseCheckTimer--;
    if (rallyCheckTimer > 0) rallyCheckTimer--;

    // 思考间隔按难度分级
    if (++thinkTimer < thinkInterval()) return;
    thinkTimer = 0;

    // 基地车立即展开（仅当基地丢失时重建；扩张用 MCV 由 doExpansion 处理移动与部署）
    for (size_t i = 0; i < w.ents.size(); i++) {
        World::Ent& e = w.ents[i];
        if (!e.alive || e.isBuilding || e.player != player || e.utype != UnitType::MCV) continue;
        if (e.state != UState::Idle) continue;  // 不展开移动中的 MCV
        if (!w.hasBld(player, BldType::ConYard)) w.orderDeploy((int)i);
    }

    doBuildOrder(w);
    doProduction(w);

    // 建筑/防御生产就绪后直接放置（RA2 双队列）
    auto tryReady = [&](ProdItem& pr) {
        if (!pr.active || !pr.ready) return;
        BldType t = (BldType)pr.typeIdx;
        if (tryPlaceBld(w, t)) {
            navalFail = 0;
        } else if (t == BldType::NavalYard && ++navalFail > 40) {
            w.cancelBldProd(player, t);
            navalFail = 0;
            navalPlaceable = 0;
        }
    };
    tryReady(p.bldProd);
    tryReady(p.defProd);

    doEngineers(w);
    doAttack(w);
    doSuperWeapon(w);
    doSupport(w);
    doTactics(w);
    doExpansion(w);
}

// ===================== 地图/船厂探测（沿用原实现） =====================
// 全图扫描是否有 3x3 以上水域（决定是否在建造序列中加入船厂）
bool SkirmishAI::detectWater(World& w) {
    if (hasWater >= 0) return hasWater;
    hasWater = 0;
    for (int y = 0; y + 2 < w.map.h && !hasWater; y += 2)
        for (int x = 0; x + 2 < w.map.w && !hasWater; x += 2) {
            bool allW = true;
            for (int dy = 0; dy < 3 && allW; dy++)
                for (int dx = 0; dx < 3 && allW; dx++)
                    if (w.map.at(x + dx, y + dy).terrain != Terrain::Water) allW = false;
            if (allW) hasWater = 1;
        }
    return hasWater;
}

// 基地建造半径内是否存在可放置 3x3 船厂的水域（带缓存，避免每次思考全图扫描）
bool SkirmishAI::navalSiteAvailable(World& w) {
    if (navalCheckCd > 0 && navalPlaceable >= 0) { navalCheckCd--; return navalPlaceable; }
    navalCheckCd = 10;  // 10 次思考（约 5 秒）后重查
    const BldDef& d = bldDef(BldType::NavalYard);
    navalPlaceable = 0;
    for (int y = 0; y + d.h <= w.map.h && !navalPlaceable; y++)
        for (int x = 0; x + d.w <= w.map.w && !navalPlaceable; x++) {
            if (w.map.at(x, y).terrain != Terrain::Water) continue;
            if (w.canPlace(BldType::NavalYard, x, y, player)) navalPlaceable = 1;
        }
    return navalPlaceable;
}

bool SkirmishAI::tryPlaceBld(World& w, BldType t) {
    const BldDef& d = bldDef(t);
    // 船厂须建于水面：全图扫描 3x3 水域（离基地越近越好）
    if (t == BldType::NavalYard) {
        int ccx = w.map.w / 2, ccy = w.map.h / 2;
        for (const World::Ent& e : w.ents)
            if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
                ccx = (int)e.x; ccy = (int)e.y; break;
            }
        int bx = -1, by = -1;
        float best = 1e9f;
        for (int y = 0; y + d.h <= w.map.h; y++)
            for (int x = 0; x + d.w <= w.map.w; x++) {
                if (w.map.at(x, y).terrain != Terrain::Water) continue;  // 粗筛
                if (!w.canPlace(t, x, y, player)) continue;
                float dist = distf((float)x, (float)y, (float)ccx, (float)ccy);
                if (dist < best) { best = dist; bx = x; by = y; }
            }
        if (bx < 0) return false;
        return w.placeBuilding(player, t, bx, by);
    }
    // 围绕建造厂螺旋搜索可放置位置（以建造厂中心为原点）
    int ccx = -1, ccy = -1, yardW = 3, yardH = 3;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            const BldDef& yd = bldDef(e.btype);
            yardW = yd.w; yardH = yd.h;
            ccx = (int)e.x + yardW / 2;
            ccy = (int)e.y + yardH / 2;
            break;
        }
    if (ccx < 0) return false;
    // 占地外缘间距：至少空 1 格，避免贴边视觉重叠（围墙除外）
    auto tooClose = [&](int bx, int by) {
        if (t == BldType::Wall) return false;
        const int gap = 1;
        for (const World::Ent& e : w.ents) {
            if (!e.alive || !e.isBuilding || e.player != player) continue;
            if (e.btype == BldType::Wall) continue;
            const BldDef& ed = bldDef(e.btype);
            int sepX = std::max(0, std::max(bx - ((int)e.x + ed.w), (int)e.x - (bx + d.w)));
            int sepY = std::max(0, std::max(by - ((int)e.y + ed.h), (int)e.y - (by + d.h)));
            if (std::max(sepX, sepY) < gap) return true;
        }
        return false;
    };
    // 从建造厂外缘开始螺旋，避免塞进紧贴西北角
    int startR = std::max(2, (std::max(yardW, yardH) + 1) / 2);
    for (int r = startR; r <= 18; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int bx = ccx + dx - d.w / 2, by = ccy + dy - d.h / 2;
                if (!w.canPlace(t, bx, by, player)) continue;
                if (tooClose(bx, by)) continue;
                return w.placeBuilding(player, t, bx, by);
            }
    // 回退：间距放宽仍放不下时允许贴边（保证能落成）
    for (int r = 1; r <= 18; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int bx = ccx + dx - d.w / 2, by = ccy + dy - d.h / 2;
                if (!w.canPlace(t, bx, by, player)) continue;
                return w.placeBuilding(player, t, bx, by);
            }
    return false;
}

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
        if (w.prereqMet(player, bldDef(want)) && p.money >= bldDef(want).cost) {
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
        for (const std::string& name : aiCfg.buildOrder) {
            BldType t;
            if (!bldTypeByName(name.c_str(), t)) continue;
            if (!queueFree(t)) continue;
            const BldDef& d = bldDef(t);
            if (!w.modeAllowsBuilding(player, t)) continue;
            if (t == BldType::NavalYard && (!naval || !navalSiteAvailable(w))) continue;
            int want = (t == BldType::OreRefinery) ? 2 : 1;
            if (w.countBlds(player, t) >= want) continue;
            if (!w.prereqMet(player, d)) continue;
            if (p.money < d.cost + 300) continue;
            w.startBldProd(player, t);
            return;
        }
        return;  // 自定义序列遍历完毕，不回退到内置序列
    }

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
        if ((d.factionMask & (1 << (int)p.faction)) && w.prereqMet(player, d) && p.money >= d.cost + 300) {
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
        BldType::Grinder,        // 回收炉（尤里专属，前置=重工，回收单位换钱；其他阵营自动跳过）
        BldType::CloningVat,     // 复制中心（苏，盟军 factionMask 自动跳过）
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
                        || t == BldType::BattleBunker || t == BldType::TankBunker);
        if (isLateTech && skipLateTech) continue;

        const BldDef& d = bldDef(rt);
        if (!w.modeAllowsBuilding(player, rt)) continue;
        if (!queueFree(rt)) continue;
        // 已建够数量则跳过；防御建筑允许 defWant 个，矿厂允许 refineryWant 个
        int want;
        if (rt == defT || rt == advDefT) want = defWant;
        else if (rt == BldType::OreRefinery) want = refineryWant;
        else want = 1;
        if (w.countBlds(player, rt) >= want) continue;
        if (!w.prereqMet(player, d)) continue;
        if (p.money < d.cost + 300) continue;  // 留点余钱
        w.startBldProd(player, rt);
        return;
    }
}

// ===================== 单位生产 =====================
void SkirmishAI::doProduction(World& w) {
    Player& p = w.players[player];

    // 攒钱建关键建筑：高科/超武造价高，建造序列未完成前暂停暴兵
    const AIBuildConfig& aiCfg = g_aiBuild[(int)p.faction];
    // 尤里超武1：心灵控制仪（替代盟军天气控制器/苏军核弹井）
    BldType swT = p.faction == Faction::Allies ? BldType::WeatherDevice
                : p.faction == Faction::Yuri   ? BldType::PsychicDominator
                : BldType::NukeSilo;
    // Turtler 攒钱更激进（为超武留更多余钱）
    int swSaveMargin = (personality == AIPersonality::Turtler) ? 1200 : 500;
    bool saveMoney = false;
    if (aiCfg.saveForSuperWeapon && w.hasBld(player, BldType::WarFactory) && !p.bldProd.active) {
        if (!w.hasBld(player, BldType::BattleLab) && w.prereqMet(player, bldDef(BldType::BattleLab))
            && p.money < bldDef(BldType::BattleLab).cost + 300) saveMoney = true;
        if (w.hasBld(player, BldType::BattleLab) && !w.hasBld(player, swT)
            && p.money < bldDef(swT).cost + swSaveMargin) saveMoney = true;
    }

    // ---- 车辆队列（类别1）：采矿车优先（经济命脉），其次主战坦克 ----
    if (w.hasBld(player, BldType::WarFactory) && w.unitQueuedCount(player, 1) < 2) {
        // 基地重建：建造厂被毁但有重工 → 补基地车
        if (!w.hasBld(player, BldType::ConYard)) {
            if (w.unitPrereqMet(player, unitDef(UnitType::MCV))) w.startUnitProd(player, UnitType::MCV);
            return;
        }
        int harvesters = w.countUnits(player, harvesterType(p.faction));
        // 采矿车目标按 econFocus 缩放
        int harvBase = aiCfg.enabled ? aiCfg.harvesterTarget : 3;
        int harvTarget = (int)(harvBase * pcfg.econFocus);
        if (harvTarget < 1) harvTarget = 1;
        if (w.skirmishMode != SkirmishMode::Megawealth
            && w.hasBld(player, BldType::OreRefinery) && harvesters < harvTarget) {
            w.startUnitProd(player, harvesterType(p.faction));
        } else if (!saveMoney) {
            // 混编部队：陆军 >6 坦克且 0 防空车辆时补 AA 护航
            UnitType aaT = (p.faction == Faction::Soviet) ? UnitType::FlakTrack
                        : (p.faction == Faction::Allies) ? UnitType::IFV
                        : (p.faction == Faction::Yuri)   ? UnitType::GatlingTank
                        : UnitType::COUNT;
            bool producedAA = false;
            if (aaT != UnitType::COUNT) {
                int tanks = 0, aa = 0;
                for (const World::Ent& e : w.ents) {
                    if (!e.alive || e.isBuilding || e.player != player) continue;
                    const UnitDef& ud = unitDef(e.utype);
                    if (ud.canHarvet() || ud.isAir() || ud.isNaval() || ud.isInfantry()) continue;
                    if (e.utype == aaT) aa++;
                    else tanks++;
                }
                if (tanks > 6 && aa == 0 && w.unitPrereqMet(player, unitDef(aaT))) {
                    w.startUnitProd(player, aaT);
                    producedAA = true;
                }
            }
            if (!producedAA) {
                bool late = w.hasBld(player, BldType::BattleLab);
                UnitType want;
                if (late) {
                    // 困难 AI 掺入特殊重单位（盟军光棱/苏军基洛夫/尤里主脑）
                    if ((difficulty == AIDiff::Hard || difficulty == AIDiff::Brutal) && attackWave % 3 == 2) {
                        UnitType sp = p.faction == Faction::Allies ? UnitType::PrismTank
                                    : p.faction == Faction::Soviet ? UnitType::Kirov
                                    : p.faction == Faction::Yuri ? UnitType::MasterMind
                                    : UnitType::Kirov;
                        if (w.unitPrereqMet(player, unitDef(sp))) { w.startUnitProd(player, sp); return; }
                    }
                    // 国家特色战车（RA2 原作：德国坦克杀手/苏俄磁能/利比亚自爆卡车；尤里无国家特色）
                    UnitType csp = UnitType::COUNT;
                    switch (p.country) {
                        case Country::Germany: csp = UnitType::TankDestroyer; break;
                        case Country::Russia:  csp = UnitType::TeslaTank; break;
                        case Country::Libya:   csp = UnitType::DemoTruck; break;
                        default: break;
                    }
                    if (csp != UnitType::COUNT && attackWave % 2 == 1 && w.countUnits(player, csp) < 6
                        && w.unitPrereqMet(player, unitDef(csp))) { w.startUnitProd(player, csp); return; }
                    // 阵营主战坦克：盟军光棱/苏军天启·磁能/中国99式/尤里狂风·磁电
                    if (p.faction == Faction::Allies) {
                        want = UnitType::PrismTank;
                    } else if (p.faction == Faction::Soviet) {
                        want = attackWave % 2 ? UnitType::Apocalypse : UnitType::TeslaTank;
                    } else if (p.faction == Faction::Yuri) {
                        // 尤里后期混编：磁电坦克（吊车辆）+ 狂风（输出）；高科后磁电优先
                        want = attackWave % 2 ? UnitType::Magnetron : UnitType::LasherTank;
                    } else {
                        want = UnitType::Type99;
                    }
                    // 前置不满足回退到基础主战坦克
                    if (!w.unitPrereqMet(player, unitDef(want))) {
                        if (p.faction == Faction::Allies) want = UnitType::Grizzly;
                        else if (p.faction == Faction::Soviet) want = UnitType::Rhino;
                        else if (p.faction == Faction::Yuri) want = UnitType::LasherTank;
                        else want = UnitType::Type99;
                    }
                } else {
                    // 中期国家特色（德国坦克杀手前置=雷达即可）
                    if (p.country == Country::Germany && attackWave % 2 == 1
                        && w.countUnits(player, UnitType::TankDestroyer) < 4
                        && w.unitPrereqMet(player, unitDef(UnitType::TankDestroyer))) {
                        w.startUnitProd(player, UnitType::TankDestroyer);
                        return;
                    }
                    // 阵营基础主战坦克
                    if (p.faction == Faction::Allies) want = UnitType::Grizzly;
                    else if (p.faction == Faction::Soviet) want = UnitType::Rhino;
                    else if (p.faction == Faction::Yuri) want = UnitType::LasherTank;
                    else want = UnitType::Type99;
                }
                w.startUnitProd(player, want);
            }
        }
    }
    // ---- 海军队列（类别3）：有船厂后维持舰队规模（战斗舰 4 + 运输船 1）----
    if (w.hasBld(player, BldType::NavalYard) && w.unitQueuedCount(player, 3) < 2) {
        // 阵营主战舰艇：盟军驱逐舰/苏军台风潜艇/中华神盾舰/尤里雷鸣潜艇
        UnitType shipT = UnitType::Destroyer;
        if (p.faction == Faction::Allies) shipT = UnitType::Destroyer;
        else if (p.faction == Faction::Soviet) shipT = UnitType::Typhoon;
        else if (p.faction == Faction::Yuri) shipT = UnitType::Boomer;
        else shipT = UnitType::Aegis;
        int trans = w.countUnits(player, UnitType::AmphTransport);
        if (trans < 1 && w.unitPrereqMet(player, unitDef(UnitType::AmphTransport))) {
            w.startUnitProd(player, UnitType::AmphTransport);
        } else if (w.countUnits(player, shipT) < 4 && w.unitPrereqMet(player, unitDef(shipT))) {
            w.startUnitProd(player, shipT);
        }
    }
    // ---- 空军队列（类别2）：有空指部后维持 2 架（韩国黑鹰/盟军入侵者/苏军米格/中国基洛夫/尤里飞碟）----
    if (w.hasBld(player, BldType::AirForceCmd) && w.unitQueuedCount(player, 2) < 2) {
        UnitType airT;
        if (p.country == Country::Korea) airT = UnitType::BlackEagle;
        else if (p.faction == Faction::Allies) airT = UnitType::Intruder;
        else if (p.faction == Faction::Soviet) airT = UnitType::MiG;
        else if (p.faction == Faction::Yuri) airT = UnitType::FloatingDisc;
        else airT = UnitType::Kirov;
        if (w.countUnits(player, airT) < 2 && w.unitPrereqMet(player, unitDef(airT)))
            w.startUnitProd(player, airT);
    }
    // ---- 步兵队列（类别0）：前期暴兵 + 适量工程师/特殊步兵 ----
    if (w.hasBld(player, BldType::Barracks) && w.unitQueuedCount(player, 0) < 2) {
        // 工程师：维持 1~2 名（占领中立建筑/修复用）
        int engs = w.countUnits(player, UnitType::Engineer);
        int engineerTarget = w.skirmishMode == SkirmishMode::Megawealth ? 3 : 1;
        if (engs < engineerTarget && p.money > 700) {
            w.startUnitProd(player, UnitType::Engineer);
        } else if (!saveMoney) {
            // 阵营基础步兵：盟军美国大兵/苏军动员兵/中国解放军/尤里新兵
            // Rusher 产更多步兵（ratio 15）；其他人格 ratio 10
            int infRatio = (personality == AIPersonality::Rusher) ? 15 : 10;
            UnitType inf;
            if (p.faction == Faction::Allies) inf = UnitType::GI;
            else if (p.faction == Faction::Soviet) inf = UnitType::Conscript;
            else if (p.faction == Faction::Yuri) inf = UnitType::Initiate;
            else inf = UnitType::PLA;
            if (w.countUnits(player, inf) < infRatio) w.startUnitProd(player, inf);
        }
        // 特殊步兵（高科后，困难 AI 更积极；国家特色步兵优先：英狙击/古巴恐怖分子/伊辐射工兵；尤里无国家特色）
        if (p.money > 1500 && w.unitQueuedCount(player, 0) < 1) {
            UnitType csp = UnitType::COUNT;
            switch (p.country) {
                case Country::UK:   csp = UnitType::Sniper; break;
                case Country::Cuba: csp = UnitType::Terrorist; break;
                case Country::Iraq: csp = UnitType::Desolator; break;
                default: break;
            }
            if (csp != UnitType::COUNT && w.countUnits(player, csp) < 3
                && w.unitPrereqMet(player, unitDef(csp))) {
                w.startUnitProd(player, csp);
            } else if (w.hasBld(player, BldType::BattleLab)) {
                int roll = (attackWave + (int)(w.tick / 900)) % ((difficulty == AIDiff::Hard || difficulty == AIDiff::Brutal) ? 3 : 5);
                UnitType sp = UnitType::COUNT;
                if (roll == 0) {
                    // 阵营英雄/特种步兵：盟军谭雅/苏军尤里/中国辐射工兵/尤里狂兽人
                    if (p.faction == Faction::Allies) sp = UnitType::Tanya;
                    else if (p.faction == Faction::Soviet) sp = UnitType::Yuri;
                    else if (p.faction == Faction::Yuri) sp = UnitType::Brute;
                    else sp = UnitType::Desolator;
                } else if (roll == 1 && p.faction == Faction::Allies) {
                    sp = UnitType::Spy;  // 盟军间谍：渗透偷钱/断电
                } else if (roll == 1 && p.faction == Faction::Yuri) {
                    sp = UnitType::Virus;  // 尤里病毒狙击手：远程反步兵
                } else if (roll == 2 && p.faction == Faction::Allies) {
                    sp = UnitType::NavySEAL;  // 海豹部队：两栖 C4 突击
                } else if (roll == 2 && p.faction == Faction::Soviet) {
                    sp = UnitType::CrazyIvan;
                } else if (roll == 2 && p.faction == Faction::Yuri) {
                    sp = UnitType::Brute;  // 狂兽人：近战重甲反车辆
                }
                if (sp != UnitType::COUNT && w.unitPrereqMet(player, unitDef(sp))
                    && w.countUnits(player, sp) < 2)
                    w.startUnitProd(player, sp);
            }
        }
    }
}

// ===================== 进攻（集结点 + 多路进攻） =====================
void SkirmishAI::doAttack(World& w) {
    Player& p = w.players[player];
    int army = countArmy(w);
    int threshold = attackThreshold();

    // 分离陆军/空军与海军
    std::vector<EID> armyIds, navyIds;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (!e.alive || e.isBuilding || e.player != player) continue;
        if (e.utype == UnitType::MCV || e.utype == UnitType::Engineer || e.utype == UnitType::Spy) continue;
        const UnitDef& ud = unitDef(e.utype);
        if (ud.canHarvet()) continue;
        if (ud.isNaval() && !ud.isAmphib()) navyIds.push_back((int)i);
        else armyIds.push_back((int)i);
    }

    Vec2i ac = findArmyCenter(w);

    // 找敌方（人类优先）建筑或单位
    EID targetB = INVALID_EID;
    float bd = 1e9f;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (!e.alive || !w.isEnemy(player, e.player)) continue;
        float ex = e.x, ey = e.y;
        if (e.isBuilding) { ex += 1.5f; ey += 1.5f; }
        int fx = (int)ex, fy = (int)ey;
        if (w.map.fogAt(player, fx, fy) == FOG_UNSEEN) continue; // no omniscience
        float d = distf((float)ac.x, (float)ac.y, ex, ey);
        if (d < bd) { bd = d; targetB = (int)i; }
    }

    // 海军分离攻击：找离舰队最近的水上/沿岸敌方目标（建筑沿岸 6 格内也算）
    if (!navyIds.empty() && targetB != INVALID_EID) {
        float nx = 0, ny = 0;
        for (EID id : navyIds) { nx += w.ents[id].x; ny += w.ents[id].y; }
        nx /= navyIds.size(); ny /= navyIds.size();
        EID seaT = INVALID_EID;
        float sd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& e = w.ents[i];
            if (!e.alive || !w.isEnemy(player, e.player)) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += 1.5f; ey += 1.5f; }
            // 水上目标或离岸 6 格内的沿岸目标
            int tx0 = std::max(0, (int)ex - 6), ty0 = std::max(0, (int)ey - 6);
            bool nearWater = false;
            for (int ty = ty0; ty <= (int)ey + 6 && ty < w.map.h && !nearWater; ty++)
                for (int tx = tx0; tx <= (int)ex + 6 && tx < w.map.w && !nearWater; tx++)
                    if (w.map.at(tx, ty).terrain == Terrain::Water) nearWater = true;
            if (!nearWater) continue;
            float d = distf(nx, ny, ex, ey);
            if (d < sd) { sd = d; seaT = (int)i; }
        }
        if (seaT != INVALID_EID) w.orderAttack(navyIds, seaT);
    }

    // 陆军不足阈值：不进攻，向集结点集结（防守）
    if (army < threshold || targetB == INVALID_EID) {
        // 设置集结点（=部队中心与敌方建造厂的中点）
        if (rallyPoint.x < 0 || rallyPoint.y < 0) {
            Vec2i enemyBase = ac;
            for (const World::Ent& e : w.ents)
                if (e.alive && e.isBuilding && e.player >= 0 && w.isEnemy(player, e.player)
                    && e.btype == BldType::ConYard) {
                    enemyBase = { (int)e.x, (int)e.y };
                    break;
                }
            rallyPoint = { ((int)ac.x + enemyBase.x) / 2, ((int)ac.y + enemyBase.y) / 2 };
            rallyCheckTimer = 15;
        }
        // 移动空闲陆军单位到集结点
        std::vector<EID> idleArmy;
        for (EID id : armyIds)
            if (w.ents[id].state == UState::Idle) idleArmy.push_back(id);
        if (!idleArmy.empty())
            w.orderMove(idleArmy, (float)rallyPoint.x + 0.5f, (float)rallyPoint.y + 0.5f, false);
        return;
    }

    // 陆军足够：在集结点集结后再进攻
    // 统计已在集结点附近的单位数
    int gathered = 0;
    if (rallyPoint.x >= 0 && rallyPoint.y >= 0) {
        for (EID id : armyIds) {
            const World::Ent& e = w.ents[id];
            if (distf(e.x, e.y, (float)rallyPoint.x, (float)rallyPoint.y) < 8.0f) gathered++;
        }
    }
    if (gathered < pcfg.rallySize) {
        // 集结点未设则设置
        if (rallyPoint.x < 0 || rallyPoint.y < 0) {
            Vec2i enemyBase = ac;
            for (const World::Ent& e : w.ents)
                if (e.alive && e.isBuilding && e.player >= 0 && w.isEnemy(player, e.player)
                    && e.btype == BldType::ConYard) {
                    enemyBase = { (int)e.x, (int)e.y };
                    break;
                }
            rallyPoint = { ((int)ac.x + enemyBase.x) / 2, ((int)ac.y + enemyBase.y) / 2 };
        }
        // 移动空闲陆军到集结点
        std::vector<EID> idleArmy;
        for (EID id : armyIds)
            if (w.ents[id].state == UState::Idle) idleArmy.push_back(id);
        if (!idleArmy.empty())
            w.orderMove(idleArmy, (float)rallyPoint.x + 0.5f, (float)rallyPoint.y + 0.5f, false);
        return;
    }

    // 集结完毕：发起进攻
    if (++attackTimer < 8) return;
    attackTimer = 0;

    // 困难/残暴：多路进攻（分两组从不同方向夹击）
    bool multiVector = (difficulty == AIDiff::Hard || difficulty == AIDiff::Brutal);
    if (multiVector && (int)armyIds.size() >= 12) {
        std::vector<EID> g1, g2;
        for (size_t i = 0; i < armyIds.size(); i++) {
            (i % 2 == 0 ? g1 : g2).push_back(armyIds[i]);
        }
        // 第二组从侧翼攻击一个更远的目标（不同方向）
        EID flankTarget = INVALID_EID;
        float fd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& e = w.ents[i];
            if (!e.alive || !w.isEnemy(player, e.player)) continue;
            if ((int)i == targetB) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += 1.5f; ey += 1.5f; }
            float d = distf((float)ac.x, (float)ac.y, ex, ey);
            if (d > 12.0f && d < fd) { fd = d; flankTarget = (int)i; }
        }
        if (flankTarget == INVALID_EID) flankTarget = targetB;
        w.orderAttack(g1, targetB);
        w.orderAttack(g2, flankTarget);
    } else {
        // 混编部队整体进攻（坦克在前、AA 护航跟随、步兵同组）
        w.orderAttack(armyIds, targetB);
    }

    attackWave++;
    rallyPoint = { -1, -1 };  // 进攻后重置集结点
}

// ===================== 战术微操 =====================
void SkirmishAI::doTactics(World& w) {
    Player& p = w.players[player];

    // 基地中心（建造厂位置）
    float bcx = -1, bcy = -1;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            bcx = e.x; bcy = e.y; break;
        }

    // 1. 集火：附近空闲单位协助正在交战的单位攻击同一目标
    if (pcfg.focusFire) {
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& atk = w.ents[i];
            if (!atk.alive || atk.isBuilding || atk.player != player) continue;
            if (atk.state != UState::Attacking) continue;
            EID tgt = atk.target;
            if (tgt == INVALID_EID || !w.valid(tgt)) continue;
            // 收集附近空闲 AI 单位协助攻击
            std::vector<EID> idle;
            for (size_t j = 0; j < w.ents.size(); j++) {
                World::Ent& u = w.ents[j];
                if (!u.alive || u.isBuilding || u.player != player) continue;
                if (u.state != UState::Idle) continue;
                if (distf(u.x, u.y, atk.x, atk.y) > 6.0f) continue;
                idle.push_back((int)j);
                if ((int)idle.size() >= 4) break;
            }
            if (!idle.empty()) w.orderAttack(idle, tgt);
        }
    }

    // 2. 残血撤退：HP < 30% 且在交战中的车辆撤回最近维修厂或基地中心
    if (pcfg.retreatDamaged) {
        EID depot = INVALID_EID;
        for (size_t i = 0; i < w.ents.size(); i++)
            if (w.ents[i].alive && w.ents[i].isBuilding && w.ents[i].player == player
                && w.ents[i].btype == BldType::ServiceDepot) { depot = (int)i; break; }
        std::vector<EID> hurt;
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || e.player != player) continue;
            const UnitDef& ud = unitDef(e.utype);
            if (ud.isInfantry() || ud.isAir() || ud.isNaval() || ud.canHarvet()) continue;
            if (e.hp >= ud.hp * 30 / 100) continue;
            if (e.state != UState::Attacking && e.state != UState::Chasing) continue;
            hurt.push_back((int)i);
            if ((int)hurt.size() >= 5) break;  // 每波最多撤 5 辆
        }
        if (!hurt.empty()) {
            if (depot != INVALID_EID) w.orderService(hurt, depot);
            else if (bcx >= 0) w.orderMove(hurt, bcx + 0.5f, bcy + 0.5f, false);  // 无维修厂退回基地
        }
    }

    // 3. 防御反应：敌方单位靠近基地（建造厂 10 格内）
    if (bcx >= 0 && defenseCheckTimer == 0) {
        EID threat = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || !w.isEnemy(player, e.player)) continue;
            float d = distf(e.x, e.y, bcx, bcy);
            if (d < 10.0f && d < bd) { bd = d; threat = (int)i; }
        }
        if (threat != INVALID_EID) {
            lastAttacker = threat;
            // 召集附近空闲单位防守
            std::vector<EID> defenders;
            for (size_t i = 0; i < w.ents.size(); i++) {
                World::Ent& e = w.ents[i];
                if (!e.alive || e.isBuilding || e.player != player) continue;
                if (e.state != UState::Idle) continue;
                if (distf(e.x, e.y, bcx, bcy) > 14.0f) continue;
                defenders.push_back((int)i);
                if ((int)defenders.size() >= 6) break;
            }
            if (!defenders.empty()) w.orderAttack(defenders, threat);
            defenseCheckTimer = 90;  // 节流：约 3 秒后再响应（逐帧递减）
        }
    }
}

// ===================== 分基地扩张 =====================
void SkirmishAI::doExpansion(World& w) {
    if (!pcfg.expandBase) return;     // 仅 Steamroller/Brutal 扩张
    if (expandTimer > 0) return;       // 冷却中（expandTimer 在 update() 中逐帧递减）

    Player& p = w.players[player];
    expandTimer = 900;                 // 默认 30 秒冷却（≈900 帧）

    // 基地中心（建造厂位置）
    float bcx = -1, bcy = -1;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            bcx = e.x; bcy = e.y; break;
        }

    // 已有 2 个建造厂则不再扩张
    int conYards = w.countBlds(player, BldType::ConYard);
    if (conYards >= 2) return;

    // 资金不足或无重工时短冷却重试
    if (p.money < 3000 || !w.hasBld(player, BldType::WarFactory)) {
        expandTimer = 300;
        return;
    }

    // 生产 MCV（队列空闲时）
    if (w.unitQueuedCount(player, 1) < 2 && w.unitPrereqMet(player, unitDef(UnitType::MCV))) {
        w.startUnitProd(player, UnitType::MCV);
    }

    // 移动/部署扩张用 MCV（仅当已有基地时；基地丢失的 MCV 由 update() 直接展开重建）
    if (conYards >= 1 && bcx >= 0) {
        // 寻找远离基地的矿场作为分基地目标
        int fx = -1, fy = -1;
        float bestDist = -1.0f;
        for (int y = 0; y < w.map.h; y++)
            for (int x = 0; x < w.map.w; x++) {
                const Cell& c = w.map.at(x, y);
                if (c.terrain != Terrain::Ore && c.terrain != Terrain::Gems) continue;
                float d = distf((float)x, (float)y, bcx, bcy);
                if (d > 20.0f && d > bestDist) { bestDist = d; fx = x; fy = y; }
            }
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || e.player != player || e.utype != UnitType::MCV) continue;
            if (e.state != UState::Idle) continue;
            // 空闲 MCV：远离基地（已到矿场）则展开；在基地则派往矿场
            if (distf(e.x, e.y, bcx, bcy) > 15.0f) {
                w.orderDeploy((int)i);  // 抵达矿场，展开
            } else if (fx >= 0) {
                w.orderMove({ (int)i }, (float)fx + 0.5f, (float)fy + 0.5f, false);
            }
        }
    }
}

// ===================== 工程师行为（沿用原实现 + 人格门控） =====================
void SkirmishAI::doEngineers(World& w) {
    if (!pcfg.useEngineers && w.skirmishMode != SkirmishMode::Megawealth) return;  // Megawealth 必须占领油井
    // 收集空闲工程师与间谍
    std::vector<EID> idleEngs, idleSpies;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (!e.alive || e.isBuilding || e.player != player) continue;
        if (e.utype == UnitType::Engineer && e.state == UState::Idle) idleEngs.push_back((int)i);
        if (e.utype == UnitType::Spy && e.state == UState::Idle) idleSpies.push_back((int)i);
    }
    // 工程师：优先占领中立科技建筑（player=-1），其次修复己方受损建筑
    for (EID eng : idleEngs) {
        EID best = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding) continue;
            if (b.player != -1) continue;  // 中立
            if (!bldDef(b.btype).capturable) continue;
            float d = distf(w.ents[eng].x, w.ents[eng].y, b.x, b.y);
            if (d < bd) { bd = d; best = (int)i; }
        }
        if (best != INVALID_EID) { w.orderCapture({ eng }, best); continue; }
        // 修复己方受损建筑（造价高优先）
        EID dmg = INVALID_EID;
        int bestCost = 0;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding || b.player != player) continue;
            const BldDef& bd2 = bldDef(b.btype);
            if (b.hp < bd2.hp * 2 / 3 && bd2.cost > bestCost) { bestCost = bd2.cost; dmg = (int)i; }
        }
        if (dmg != INVALID_EID) w.orderRepair({ eng }, dmg);
    }
    // 间谍：渗透敌方精炼厂（偷钱）> 电厂（断电）
    if (!pcfg.useSpies) return;
    for (EID spy : idleSpies) {
        EID best = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding || !w.isEnemy(player, b.player)) continue;
            if (b.btype != BldType::OreRefinery && b.btype != BldType::PowerPlant
                && b.btype != BldType::TeslaReactor) continue;
            float d = distf(w.ents[spy].x, w.ents[spy].y, b.x, b.y);
            if (d < bd) { bd = d; best = (int)i; }
        }
        if (best != INVALID_EID) w.orderAttack({ spy }, best);  // 无武器单位 orderAttack → 渗透目标（判定在 updateUnit）
    }
}

// ===================== 超武（沿用原实现 + 人格门控） =====================
void SkirmishAI::doSuperWeapon(World& w) {
    if (!pcfg.useSuperWeapon || !w.superweaponsEnabled) return;  // 人格/对局选项门控
    Player& p = w.players[player];
    for (int i = 0; i < (int)SWType::COUNT; i++) {
        if (!p.swReady[i]) continue;
        SWType t = (SWType)i;
        if (t == SWType::IronCurtain) {
            // 铁幕：套在己方部队中心（有部队时才用）
            if (countArmy(w) < 6) continue;
            Vec2i ac = findArmyCenter(w);
            w.launchSW(player, t, (float)ac.x, (float)ac.y);
        } else {
            // 核弹/闪电：炸敌方建筑密集区（优先建造厂/高科）
            float bestScore = -1, bx = 0, by = 0;
            for (size_t j = 0; j < w.ents.size(); j++) {
                const World::Ent& e = w.ents[j];
                if (!e.alive || !e.isBuilding || e.player < 0 || e.player == player) continue;
                const BldDef& bd = bldDef(e.btype);
                float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
                // 评分：高价值建筑 + 周围敌建筑密度
                float score = bd.cost / 500.0f;
                if (e.btype == BldType::ConYard) score += 6;
                if (e.btype == BldType::BattleLab) score += 3;
                for (const World::Ent& o : w.ents) {
                    if (!o.alive || !o.isBuilding || o.player != e.player) continue;
                    const BldDef& od = bldDef(o.btype);
                    if (distf(cx, cy, o.x + od.w / 2.0f, o.y + od.h / 2.0f) < 6.0f) score += 1.0f;
                }
                if (score > bestScore) { bestScore = score; bx = cx; by = cy; }
            }
            if (bestScore > 0) w.launchSW(player, t, bx, by);
        }
    }
}

// ===================== 支援技能（沿用原实现） =====================
void SkirmishAI::doSupport(World& w) {
    Player& p = w.players[player];
    // 尤里经济：优先让闲置新兵进入生化反应堆补电；把严重受损车辆送入回收炉，
    // 避免 AI 建出官方经济建筑却从不使用。
    if (p.faction == Faction::Yuri) {
        for (size_t bi = 0; bi < w.ents.size(); ++bi) {
            const World::Ent& b = w.ents[bi];
            if (!b.alive || !b.isBuilding || b.player != player || b.btype != BldType::BioReactor
                || (int)b.garrison.size() >= bldDef(b.btype).garrisonCap) continue;
            std::vector<EID> occupants;
            for (size_t ui = 0; ui < w.ents.size(); ++ui) {
                const World::Ent& u = w.ents[ui];
                if (!u.alive || u.isBuilding || u.player != player || u.state != UState::Idle
                    || u.utype != UnitType::Initiate) continue;
                occupants.push_back((int)ui);
                if ((int)occupants.size() >= bldDef(b.btype).garrisonCap - (int)b.garrison.size()) break;
            }
            if (!occupants.empty()) w.orderGarrison(occupants, (int)bi);
        }
        EID grinder = INVALID_EID;
        for (size_t i = 0; i < w.ents.size(); ++i)
            if (w.ents[i].alive && w.ents[i].isBuilding && w.ents[i].player == player
                && w.ents[i].btype == BldType::Grinder) { grinder = (int)i; break; }
        if (grinder != INVALID_EID) {
            std::vector<EID> recycle;
            for (size_t i = 0; i < w.ents.size(); ++i) {
                const World::Ent& u = w.ents[i];
                if (!u.alive || u.isBuilding || u.player != player || u.state != UState::Idle) continue;
                const UnitDef& ud = unitDef(u.utype);
                if (ud.isAir() || ud.isNaval() || ud.canHarvet() || isHero(u.utype)) continue;
                if (u.hp < ud.hp / 4) recycle.push_back((int)i);
                if (recycle.size() >= 2) break;
            }
            if (!recycle.empty()) w.orderGarrison(recycle, grinder);
        }
    }
    // 1. 伞兵：空投到敌方建造厂附近（骚扰敌后）
    if (p.paradropReady) {
        float bx = -1, by = -1;
        for (const World::Ent& e : w.ents)
            if (e.alive && e.isBuilding && e.player >= 0 && w.isEnemy(player, e.player)
                && e.btype == BldType::ConYard) { bx = e.x + 3; by = e.y + 5; break; }
        if (bx < 0)
            for (const World::Ent& e : w.ents)
                if (e.alive && e.isBuilding && e.player >= 0 && w.isEnemy(player, e.player)) { bx = e.x + 2; by = e.y + 3; break; }
        if (bx >= 0) {
            // 落点须为可站立陆地：向外找最近可通行格
            for (int r = 0; r < 8 && p.paradropReady; r++) {
                for (int dy = -r; dy <= r && p.paradropReady; dy++)
                    for (int dx = -r; dx <= r && p.paradropReady; dx++) {
                        int nx = (int)bx + dx, ny = (int)by + dy;
                        if (!w.map.inBounds(nx, ny) || !w.map.passable(nx, ny)) continue;
                        if (w.map.at(nx, ny).terrain == Terrain::Water) continue;
                        w.orderParadrop(player, nx + 0.5f, ny + 0.5f);
                    }
            }
        }
    }
    // 2. 维修厂：重伤（<55%）或被寄生的车辆自动回厂维修
    EID depot = INVALID_EID;
    for (size_t i = 0; i < w.ents.size(); i++)
        if (w.ents[i].alive && w.ents[i].isBuilding && w.ents[i].player == player
            && w.ents[i].btype == BldType::ServiceDepot) { depot = (int)i; break; }
    if (depot != INVALID_EID) {
        std::vector<EID> hurt;
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || e.player != player) continue;
            const UnitDef& ud = unitDef(e.utype);
            if (ud.isInfantry() || ud.isAir() || ud.pathDomain() != 0 || ud.canHarvet()) continue;
            bool needFix = e.hp < ud.hp * 55 / 100 || e.parasite != INVALID_EID;
            if (!needFix) continue;
            if (e.state == UState::Idle || (e.state == UState::Moving && e.target == INVALID_EID))
                hurt.push_back((int)i);
            if ((int)hurt.size() >= 3) break;  // 每波最多送修 3 辆，避免前线空虚
        }
        if (!hurt.empty()) w.orderService(hurt, depot);
    }
    // 3. 碉堡驻军：闲置步兵进驻战斗碉堡/中立民房，闲置坦克进驻坦克碉堡（加固基地防线）
    {
        // 基地中心（建造厂位置）
        float bcx = -1, bcy = -1;
        for (const World::Ent& e : w.ents)
            if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) { bcx = e.x; bcy = e.y; break; }
        if (bcx >= 0) {
            for (size_t i = 0; i < w.ents.size(); i++) {
                World::Ent& b = w.ents[i];
                if (!b.alive || !b.isBuilding) continue;
                int gdom = garrisonDomain(b.btype);
                if (gdom == 0 || (int)b.garrison.size() >= bldDef(b.btype).garrisonCap) continue;
                if (b.player >= 0 && b.player != player) continue;  // 只填己方或中立碉堡
                if (distf(b.x, b.y, bcx, bcy) > 24.0f) continue;   // 只填基地附近
                std::vector<EID> idle;
                for (size_t j = 0; j < w.ents.size(); j++) {
                    World::Ent& u = w.ents[j];
                    if (!u.alive || u.isBuilding || u.player != player || u.state != UState::Idle) continue;
                    const UnitDef& ud = unitDef(u.utype);
                    bool fit = (gdom == 1 && ud.isInfantry() && u.utype != UnitType::Engineer)
                            || (gdom == 2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain() == 0 && !ud.canHarvet());
                    if (fit) idle.push_back((int)j);
                    if ((int)idle.size() >= bldDef(b.btype).garrisonCap - (int)b.garrison.size()) break;
                }
                if (!idle.empty()) w.orderGarrison(idle, (int)i);
            }
        }
    }
}

// ===================== 查询（沿用原实现） =====================
int SkirmishAI::countArmy(World& w) {
    int n = 0;
    for (const World::Ent& e : w.ents)
        if (e.alive && !e.isBuilding && e.player == player &&
            !unitDef(e.utype).canHarvet() && e.utype != UnitType::MCV && e.utype != UnitType::Engineer)
            n++;
    return n;
}

Vec2i SkirmishAI::findArmyCenter(World& w) {
    int sx = 0, sy = 0, n = 0;
    for (const World::Ent& e : w.ents)
        if (e.alive && !e.isBuilding && e.player == player &&
            !unitDef(e.utype).canHarvet() && e.utype != UnitType::MCV) {
            sx += (int)e.x; sy += (int)e.y; n++;
        }
    if (n == 0) return { w.map.w / 2, w.map.h / 2 };
    return { sx / n, sy / n };
}
