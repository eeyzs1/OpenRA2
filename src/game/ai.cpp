#include "game/ai.h"

void SkirmishAI::update(World& w) {
    Player& p = w.players[player];
    if (!p.active || p.defeated) return;
    // 思考间隔按难度分级：简单 25 / 普通 15 / 困难 10 逻辑帧
    int interval = difficulty == 0 ? 25 : difficulty == 2 ? 10 : 15;
    if (++thinkTimer < interval) return;
    thinkTimer = 0;

    // 1. 基地车立即展开
    for (size_t i = 0; i < w.ents.size(); i++) {
        World::Ent& e = w.ents[i];
        if (e.alive && !e.isBuilding && e.player == player && e.utype == UnitType::MCV) {
            w.orderDeploy((int)i);
        }
    }

    doBuildOrder(w);
    doProduction(w);

    // 建筑生产就绪后直接放置
    if (p.bldProd.active && p.bldProd.ready) {
        BldType t = (BldType)p.bldProd.typeIdx;
        if (tryPlaceBld(w, t)) {
            navalFail = 0;
        } else if (t == BldType::NavalYard && ++navalFail > 40) {
            // 船厂就绪但长期放不下（水域被占/基地半径变化）：取消并放弃海军，解锁建造序列
            w.cancelProd(player, false);
            navalFail = 0;
            navalPlaceable = 0;
        }
    }

    doEngineers(w);
    doAttack(w);
    doSuperWeapon(w);
}

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
    navalCheckCd = 10; // 10 次思考（约 5 秒）后重查
    const BldDef& d = bldDef(BldType::NavalYard);
    navalPlaceable = 0;
    for (int y = 0; y + d.h <= w.map.h && !navalPlaceable; y++)
        for (int x = 0; x + d.w <= w.map.w && !navalPlaceable; x++) {
            if (w.map.at(x, y).terrain != Terrain::Water) continue;
            if (w.canPlace(BldType::NavalYard, x, y, player)) navalPlaceable = 1;
        }
    return navalPlaceable;
}

// 建造序列：电厂 -> 矿厂 -> 兵营 -> 重工 -> 雷达 -> 防御 -> 高科 -> 核电
void SkirmishAI::doBuildOrder(World& w) {
    Player& p = w.players[player];
    if (p.bldProd.active) return; // 正在建造
    bool naval = detectWater(w);
    static const BldType order[] = {
        BldType::TeslaReactor,  // 苏系电厂（中国也是）；盟军在下文替换
        BldType::OreRefinery,
        BldType::Barracks,
        BldType::WarFactory,
        BldType::OreRefinery,   // 第二矿厂
        BldType::Radar,
        BldType::NavalYard,     // 海军船厂（仅水域图，前置=重工）
        BldType::SentryGun,
        BldType::AirForceCmd,   // 空指部：解锁战机
        BldType::TeslaCoil,
        BldType::BattleLab,
        BldType::NuclearReactor,
        BldType::NukeSilo,      // 超武1：核弹井（盟替换为天气控制器）
        BldType::TeslaCoil,
        BldType::IronCurtain,   // 超武2：铁幕（盟军无，自动跳过）
    };
    BldType powerT = p.faction == Faction::Allies ? BldType::PowerPlant : BldType::TeslaReactor;
    BldType defT = p.faction == Faction::Allies ? BldType::Pillbox : BldType::SentryGun;
    BldType advDefT = p.faction == Faction::Allies ? BldType::PrismTower : BldType::TeslaCoil;
    BldType bigPowerT = p.faction == Faction::Allies ? BldType::PowerPlant : BldType::NuclearReactor;
    BldType swT = p.faction == Faction::Allies ? BldType::WeatherDevice : BldType::NukeSilo;

    // 电力不足优先补电
    if (p.powerMade - p.powerUsed < 30 && w.hasBld(player, BldType::ConYard)) {
        BldType want = w.hasBld(player, BldType::BattleLab) ? bigPowerT : powerT;
        if (w.prereqMet(player, bldDef(want)) && p.money >= bldDef(want).cost) {
            w.startBldProd(player, want);
            return;
        }
    }

    for (BldType t : order) {
        BldType rt = t;
        if (t == BldType::TeslaReactor) rt = powerT;
        if (t == BldType::SentryGun) rt = defT;
        if (t == BldType::TeslaCoil) rt = advDefT;
        if (t == BldType::NuclearReactor) rt = bigPowerT;
        if (t == BldType::NukeSilo) rt = swT;
        if (rt == BldType::NavalYard && (!naval || !navalSiteAvailable(w))) continue; // 无可建水域跳过船厂
        const BldDef& d = bldDef(rt);
        if (!(d.factionMask & (1 << (int)p.faction))) continue;
        // 已建够数量则跳过（防御建筑允许 2 个）
        int want = (rt == defT || rt == advDefT) ? 2 : (rt == BldType::OreRefinery ? 2 : 1);
        if (rt == BldType::OreRefinery && w.countBlds(player, BldType::OreRefinery) >= 2) continue;
        if (rt != BldType::OreRefinery && w.countBlds(player, rt) >= want) continue;
        if (!w.prereqMet(player, d)) continue;
        if (p.money < d.cost + 300) continue; // 留点余钱
        w.startBldProd(player, rt);
        return;
    }
}

void SkirmishAI::doProduction(World& w) {
    Player& p = w.players[player];

    // 攒钱建关键建筑：高科/超武造价高，建造序列未完成前暂停暴兵
    BldType swT = p.faction == Faction::Allies ? BldType::WeatherDevice : BldType::NukeSilo;
    bool saveMoney = false;
    if (w.hasBld(player, BldType::WarFactory) && !p.bldProd.active) {
        if (!w.hasBld(player, BldType::BattleLab) && w.prereqMet(player, bldDef(BldType::BattleLab))
            && p.money < bldDef(BldType::BattleLab).cost + 300) saveMoney = true;
        if (w.hasBld(player, BldType::BattleLab) && !w.hasBld(player, swT)
            && p.money < bldDef(swT).cost + 500) saveMoney = true;
    }

    // ---- 车辆队列（类别1）：采矿车优先（经济命脉），其次主战坦克 ----
    if (w.hasBld(player, BldType::WarFactory) && w.unitQueuedCount(player, 1) < 2) {
        // 基地重建：建造厂被毁但有重工 → 补基地车
        if (!w.hasBld(player, BldType::ConYard)) {
            if (w.unitPrereqMet(player, unitDef(UnitType::MCV))) w.startUnitProd(player, UnitType::MCV);
            return;
        }
        int harvesters = w.countUnits(player, UnitType::Harvester);
        if (w.hasBld(player, BldType::OreRefinery) && harvesters < 3) {
            w.startUnitProd(player, UnitType::Harvester);
        } else if (!saveMoney) {
            bool late = w.hasBld(player, BldType::BattleLab);
            UnitType want;
            if (late) {
                // 困难 AI 掺入特殊重单位（基洛夫/特殊坦克）
                if (difficulty == 2 && attackWave % 3 == 2) {
                    UnitType sp = p.faction == Faction::Allies ? UnitType::PrismTank
                                : p.faction == Faction::Soviet ? UnitType::Kirov : UnitType::Kirov;
                    if (w.unitPrereqMet(player, unitDef(sp))) { w.startUnitProd(player, sp); return; }
                }
                want = p.faction == Faction::Allies ? UnitType::PrismTank
                     : p.faction == Faction::Soviet ? (attackWave % 2 ? UnitType::Apocalypse : UnitType::TeslaTank)
                     : UnitType::Type99;
                if (!w.unitPrereqMet(player, unitDef(want)))
                    want = p.faction == Faction::Allies ? UnitType::Grizzly
                         : p.faction == Faction::Soviet ? UnitType::Rhino : UnitType::Type99;
            } else {
                want = p.faction == Faction::Allies ? UnitType::Grizzly
                     : p.faction == Faction::Soviet ? UnitType::Rhino : UnitType::Type99;
            }
            w.startUnitProd(player, want);
        }
    }
    // ---- 海军队列（类别3）：有船厂后维持舰队规模（战斗舰 4 + 运输船 1）----
    if (w.hasBld(player, BldType::NavalYard) && w.unitQueuedCount(player, 3) < 2) {
        UnitType shipT = p.faction == Faction::Allies ? UnitType::Destroyer
                       : p.faction == Faction::Soviet ? UnitType::Typhoon : UnitType::Aegis;
        int trans = w.countUnits(player, UnitType::AmphTransport);
        if (trans < 1 && w.unitPrereqMet(player, unitDef(UnitType::AmphTransport))) {
            w.startUnitProd(player, UnitType::AmphTransport);
        } else if (w.countUnits(player, shipT) < 4 && w.unitPrereqMet(player, unitDef(shipT))) {
            w.startUnitProd(player, shipT);
        }
    }
    // ---- 空军队列（类别2）：有空指部后维持 2 架 ----
    if (w.hasBld(player, BldType::AirForceCmd) && w.unitQueuedCount(player, 2) < 2) {
        UnitType airT = p.faction == Faction::Allies ? UnitType::Intruder
                      : p.faction == Faction::Soviet ? UnitType::MiG : UnitType::BlackEagle;
        if (w.countUnits(player, airT) < 2 && w.unitPrereqMet(player, unitDef(airT)))
            w.startUnitProd(player, airT);
    }
    // ---- 步兵队列（类别0）：前期暴兵 + 适量工程师/特殊步兵 ----
    if (w.hasBld(player, BldType::Barracks) && w.unitQueuedCount(player, 0) < 2) {
        // 工程师：维持 1~2 名（占领中立建筑/修复用）
        int engs = w.countUnits(player, UnitType::Engineer);
        if (engs < 1 && p.money > 1200) {
            w.startUnitProd(player, UnitType::Engineer);
        } else if (!saveMoney) {
            UnitType inf = p.faction == Faction::Allies ? UnitType::GI
                         : p.faction == Faction::Soviet ? UnitType::Conscript : UnitType::PLA;
            if (w.countUnits(player, inf) < 10) w.startUnitProd(player, inf);
        }
        // 特殊步兵（高科后，困难 AI 更积极）
        if (w.hasBld(player, BldType::BattleLab) && p.money > 1500 && w.unitQueuedCount(player, 0) < 1) {
            int roll = (attackWave + (int)(w.tick / 900)) % (difficulty == 2 ? 3 : 5);
            UnitType sp = UnitType::COUNT;
            if (roll == 0) {
                sp = p.faction == Faction::Allies ? UnitType::Tanya
                   : p.faction == Faction::Soviet ? UnitType::Desolator : UnitType::Desolator;
            } else if (roll == 1 && p.faction == Faction::Allies) {
                sp = UnitType::Spy; // 盟军间谍：渗透偷钱/断电
            }
            if (sp != UnitType::COUNT && w.unitPrereqMet(player, unitDef(sp))
                && w.countUnits(player, sp) < 2)
                w.startUnitProd(player, sp);
        }
    }
}

// 工程师行为：占领中立科技建筑（油井优先），修复己方受损建筑；间谍渗透敌方经济建筑
void SkirmishAI::doEngineers(World& w) {
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
            if (b.player != -1) continue; // 中立
            if (!bldDef(b.btype).capturable) continue;
            float d = distf(w.ents[eng].x, w.ents[eng].y, b.x, b.y);
            if (d < bd) { bd = d; best = (int)i; }
        }
        if (best != INVALID_EID) { w.orderCapture({eng}, best); continue; }
        // 修复己方受损建筑（造价高优先）
        EID dmg = INVALID_EID;
        int bestCost = 0;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding || b.player != player) continue;
            const BldDef& bd2 = bldDef(b.btype);
            if (b.hp < bd2.hp * 2 / 3 && bd2.cost > bestCost) { bestCost = bd2.cost; dmg = (int)i; }
        }
        if (dmg != INVALID_EID) w.orderRepair({eng}, dmg);
    }
    // 间谍：渗透敌方精炼厂（偷钱）> 电厂（断电）
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
        if (best != INVALID_EID) w.orderAttack({spy}, best); // 无武器单位 orderAttack → 渗透目标（判定在 updateUnit）
    }
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
                if (w.map.at(x, y).terrain != Terrain::Water) continue; // 粗筛
                if (!w.canPlace(t, x, y, player)) continue;
                float dist = distf((float)x, (float)y, (float)ccx, (float)ccy);
                if (dist < best) { best = dist; bx = x; by = y; }
            }
        if (bx < 0) return false;
        return w.placeBuilding(player, t, bx, by);
    }
    // 围绕建造厂螺旋搜索可放置位置
    int ccx = -1, ccy = -1;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            ccx = (int)e.x; ccy = (int)e.y; break;
        }
    if (ccx < 0) return false;
    for (int r = 1; r <= 14; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int bx = ccx + dx, by = ccy + dy;
                if (w.canPlace(t, bx, by, player)) {
                    return w.placeBuilding(player, t, bx, by);
                }
            }
    return false;
}

void SkirmishAI::doSuperWeapon(World& w) {
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

int SkirmishAI::countArmy(World& w) {
    int n = 0;
    for (const World::Ent& e : w.ents)
        if (e.alive && !e.isBuilding && e.player == player &&
            e.utype != UnitType::Harvester && e.utype != UnitType::MCV && e.utype != UnitType::Engineer)
            n++;
    return n;
}

Vec2i SkirmishAI::findArmyCenter(World& w) {
    int sx = 0, sy = 0, n = 0;
    for (const World::Ent& e : w.ents)
        if (e.alive && !e.isBuilding && e.player == player &&
            e.utype != UnitType::Harvester && e.utype != UnitType::MCV) {
            sx += (int)e.x; sy += (int)e.y; n++;
        }
    if (n == 0) return {w.map.w / 2, w.map.h / 2};
    return {sx / n, sy / n};
}

void SkirmishAI::doAttack(World& w) {
    // 攒兵进攻：部队数量达标后攻击最近敌方目标（阈值按难度分级）
    int army = countArmy(w);
    int threshold = (difficulty == 0 ? 12 : difficulty == 2 ? 6 : 8) + attackWave * 2;
    if (army < threshold) return;
    if (++attackTimer < 8) return;
    attackTimer = 0;

    // 找敌方（人类优先）建筑或单位
    EID targetB = INVALID_EID;
    float bd = 1e9f;
    Vec2i ac = findArmyCenter(w);
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (!e.alive || !w.isEnemy(player, e.player)) continue;
        float ex = e.x, ey = e.y;
        if (e.isBuilding) { ex += 1.5f; ey += 1.5f; }
        float d = distf((float)ac.x, (float)ac.y, ex, ey);
        if (d < bd) { bd = d; targetB = (int)i; }
    }
    if (targetB == INVALID_EID) return;

    // 陆军/空军突击（海军分离：舰船只打水上/沿岸目标）
    std::vector<EID> armyIds, navyIds;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (e.alive && !e.isBuilding && e.player == player &&
            e.utype != UnitType::Harvester && e.utype != UnitType::MCV && e.utype != UnitType::Engineer
            && e.utype != UnitType::Spy) {
            const UnitDef& ud = unitDef(e.utype);
            if (ud.isNaval() && !ud.isAmphib()) navyIds.push_back((int)i);
            else armyIds.push_back((int)i);
        }
    }
    if (!armyIds.empty()) w.orderAttack(armyIds, targetB);
    // 海军分离攻击：找离舰队最近的水上/沿岸敌方目标（建筑沿岸 6 格内也算）
    if (!navyIds.empty()) {
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
    attackWave++;
}
