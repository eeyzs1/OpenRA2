#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void World::update() {
    tick++;
    for (Ent& e : ents) { e.px = e.x; e.py = e.y; } // 渲染插值：逻辑帧前位置快照
    bool dynamicPower = false;
    for (size_t i = 0; i < ents.size(); ++i) {
        Ent& b = ents[i];
        if (b.alive && b.isBuilding && b.btype == BldType::BioReactor && !b.garrison.empty())
            dynamicPower = true;
        if (!b.alive || !b.isBuilding || b.drainedBy == INVALID_EID) continue;
        dynamicPower = true;
        if (!valid(b.drainedBy) || ents[b.drainedBy].utype != UnitType::FloatingDisc
            || ents[b.drainedBy].target != (EID)i) {
            b.drainedBy = INVALID_EID;
        }
    }
    if (dynamicPower) recomputePower(); // 不覆盖测试/脚本显式电力缓存，只有动态机制存在时重算
    // 尤里奴隶经济：已部署奴隶矿车 / 精炼厂定时补充奴隶（上限 5）
    if (tick % 90 == 0) {
        for (int pi = 0; pi < numPlayers; pi++) {
            if (players[pi].faction != Faction::Yuri || !players[pi].active || players[pi].defeated) continue;
            int slaves = 0;
            for (const Ent& u : ents)
                if (u.alive && !u.isBuilding && u.player == pi && u.utype == UnitType::Slave) slaves++;
            if (slaves >= 5) continue;
            for (size_t i = 0; i < ents.size() && slaves < 5; i++) {
                Ent& b = ents[i];
                if (!b.alive || b.player != pi) continue;
                bool dock = (b.isBuilding && b.btype == BldType::OreRefinery)
                         || (!b.isBuilding && b.utype == UnitType::SlaveMiner && b.deployed);
                if (!dock) continue;
                float sx = b.isBuilding ? b.x + 1.5f : b.x;
                float sy = b.isBuilding ? b.y + (float)bldDef(b.btype).h + 1.0f : b.y + 1.0f;
                EID sid = spawnUnit(pi, UnitType::Slave, sx, sy);
                if (sid != INVALID_EID) {
                    slaves++;
                    Vec2i ore;
                    if (map.findNearestOre((int)sx, (int)sy, 40, ore))
                        orderHarvest({sid}, ore.x, ore.y);
                }
            }
        }
    }
    updateSW();
    // EVA 播报节流倒计时 + 间谍效果计时
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (p.evaBaseCd > 0) p.evaBaseCd--;
        if (p.evaMinerCd > 0) p.evaMinerCd--;
        if (p.evaUnitCd > 0) p.evaUnitCd--;
        if (p.powerSabotage > 0) p.powerSabotage--;
        if (p.revealTimer > 0) p.revealTimer--;
    }
    spawnCrateTick();    // 周期性生成补给箱
    // 建筑持续维修（RA2：RepairRate 周期到后回复一格血条；约 20 格血条）
    // 扳手模式逐步修，非工程师瞬间回满
    if (tick % 15 == 0) { // ~0.5s @ LOGIC_FPS=30
        for (Ent& e : ents) {
            if (!e.alive || !e.isBuilding || !e.repairing || e.selling || e.player < 0) continue;
            const BldDef& d = bldDef(e.btype);
            if (e.hp >= d.hp) { e.repairing = false; continue; }
            const int pips = 20;
            int pipHp = std::max(1, (d.hp + pips - 1) / pips); // 一格血
            int heal = std::min(pipHp, d.hp - e.hp);
            const int repairPercent = 15; // 满修约造价 15%
            int stepCost = std::max(1, (d.cost * repairPercent * heal + d.hp - 1) / (100 * std::max(1, d.hp)));
            if (players[e.player].money < stepCost) { e.repairing = false; continue; }
            players[e.player].money -= stepCost;
            e.hp = std::min(d.hp, e.hp + heal);
            if (e.hp >= d.hp) e.repairing = false;
        }
    }
    updateTimedBombs();  // 疯狂伊文炸弹倒计时
    regrowOre();         // 矿脉缓慢再生
    updateParadrop();    // 伞兵充能（美国空指部/科技机场）
    // 生产进度
    // 性能优化：预计算每个玩家的各类工厂数量缓存（避免每类生产都遍历所有实体）
    // 用数组大小 64（BldType 枚举值约 30+，留余量）
    int facCount[MAX_PLAYERS][64] = {}; // [player][BldType]
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player < 0 || e.player >= numPlayers) continue;
        facCount[e.player][(int)e.btype]++;
    }
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        float rate = p.lowPower() ? std::max(0.01f, g_gameRules.lowPowerSpeedFactor) : 1.0f;
        // 单位生产：步兵/车辆/空军/海军 4 类独立队列（RA2 原作）
        for (int cat = 0; cat < PROD_CAT_N; cat++) {
            ProdItem& pr = p.unitProd[cat];
            if (!pr.active) continue;
            if (pr.held) continue; // RA2 HOLD：暂停扣款与进度
            const UnitDef& u = unitDef((UnitType)pr.typeIdx);
            bool factoryDrained = false;
            for (const Ent& b : ents)
                if (b.alive && b.isBuilding && b.player == pi && b.drainedBy != INVALID_EID
                    && isFactoryFor(b.btype, u)) { factoryDrained = true; break; }
            if (factoryDrained) continue;
            // 多工厂加速：每个额外同类生产建筑 +50% 速度（上限 2.5x，RA2 原作设定）
            int fac = 0;
            for (int b = 0; b < 64; b++)
                if (facCount[pi][b] > 0 && isFactoryFor((BldType)b, u)) fac += facCount[pi][b];
            float speed = rate * std::min(2.5f, 1.0f + 0.5f * std::max(0, fac - 1));
            int totalCost = pr.totalCost;
            int totalTicks = std::max(1, (int)ceilf(u.buildTime / speed));
            int remTicks = std::max(1, totalTicks - pr.progress);
            int need = (remTicks <= 1) ? (totalCost - pr.paid)
                                       : (totalCost - pr.paid) / remTicks;
            if (need < 0) need = 0;
            if (p.money < need) continue; // 资金不足暂停（RA2：缺钱停产，不允许负值）
            p.money -= need;
            pr.paid += need;
            pr.progress = std::min(totalTicks, pr.progress + 1);
            if (pr.progress >= totalTicks && pr.paid >= totalCost && spawnFromFactory(pi, u)) {
                pr = ProdItem{};
                // 队首递补
                if (!p.unitQueue[cat].empty()) {
                    int nt = p.unitQueue[cat].front();
                    p.unitQueue[cat].pop_front();
                    pr.active = true; pr.isUnit = true; pr.typeIdx = nt; pr.progress = 0;
                    pr.paid = 0; pr.held = false;
                    pr.totalCost = unitProductionCost(pi, (UnitType)nt); pr.ready = false;
                }
                if (pi == 0) g_sfx.play(Sfx::Ready, 0.7f); // 本家单位就绪提示
            }
        }
        // 建筑 / 防御生产（RA2 双队列并行）
        auto tickBldQueue = [&](ProdItem& pr) {
            if (!pr.active || pr.ready || pr.held) return;
            const BldDef& d = bldDef((BldType)pr.typeIdx);
            bool yardDrained = false;
            for (const Ent& b : ents)
                if (b.alive && b.isBuilding && b.player == pi && b.btype == BldType::ConYard
                    && b.drainedBy != INVALID_EID) { yardDrained = true; break; }
            if (yardDrained) return;
            int totalTicks = std::max(1, (int)ceilf(d.buildTime / rate));
            int remTicks = std::max(1, totalTicks - pr.progress);
            int need = (remTicks <= 1) ? (pr.totalCost - pr.paid)
                                       : (pr.totalCost - pr.paid) / remTicks;
            if (need < 0) need = 0;
            if (p.money >= need) {
                p.money -= need;
                pr.paid += need;
                pr.progress++;
                if (pr.progress >= totalTicks && pr.paid >= pr.totalCost) {
                    pr.ready = true;
                    if (pi == 0) g_sfx.play(Sfx::Ready, 0.7f);
                }
            }
        };
        tickBldQueue(p.bldProd);
        tickBldQueue(p.defProd);
    }

    // 实体更新
    for (size_t i = 0; i < ents.size(); i++) {
        if (!ents[i].alive) continue;
        if (ents[i].isBuilding) updateBuilding(ents[i], (int)i);
        else updateUnit(ents[i], (int)i);
    }

    // 弹道
    for (auto& pr : projs) {
        if (!pr.alive) continue;
        // 大型导弹可被拦截（RA2 原作：爱国者/高炮/神盾在射程内击落 V3/无畏舰导弹）
        if (pr.hp > 0 && tick % 5 == 0) {
            for (size_t i = 0; i < ents.size(); i++) {
                const Ent& d = ents[i];
                if (!d.alive || d.player < 0 || !isEnemy(d.player, pr.player)) continue;
                WeaponDef dw = d.isBuilding ? bldDef(d.btype).weapon : unitDef(d.utype).weapon;
                if (!dw.antiAir || dw.damage <= 0) continue;
                if (d.isBuilding && players[d.player].lowPower()) continue; // 低电防御停摆
                float dx = d.x, dy = d.y;
                if (d.isBuilding) { dx += bldDef(d.btype).w / 2.0f; dy += bldDef(d.btype).h / 2.0f; }
                if (distf(dx, dy, pr.x, pr.y) > dw.range) continue;
                pr.hp -= 9;
                explodeAt(pr.x, pr.y, 0); // 拦截命中的小爆炸
                if (pr.hp <= 0) pr.alive = false; // 导弹被击落：凌空爆炸，不造成地面伤害
                break; // 每轮至多一个拦截火力
            }
            if (!pr.alive) continue;
        }
        float tx = pr.tx, ty = pr.ty;
        if (valid(pr.target)) {
            const Ent& t = ents[pr.target];
            tx = t.x; ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
        }
        float d = distf(pr.x, pr.y, tx, ty);
        float spd = pr.kind == ProjKind::Missile ? 0.25f : 0.6f;
        if (d < spd + 0.1f) {
            // 命中
            // 混乱无人机毒气：来源为 ChaosDrone，命中后施加混乱（自相残杀）
            bool isChaosGas = valid(pr.src) && !ents[pr.src].isBuilding
                              && ents[pr.src].utype == UnitType::ChaosDrone;
            if (valid(pr.target)) {
                Ent& t = ents[pr.target];
                float mult = weaponMultiplier(pr.w, t);
                damage(pr.target, (int)(pr.w.damage * mult), pr.player, pr.src, pr.srcGarrisonSlot);
                // 直接受击的地面单位同样陷入混乱（毒气主目标也受影响）
                if (isChaosGas && !t.isBuilding && !unitDef(t.utype).isAir()
                    && !psychicImmune(t.utype) && t.invuln == 0) {
                    t.confused = 180;
                    t.target = INVALID_EID;
                }
            }
            // 溅射伤害（V3 火箭等）：命中点范围内所有实体按距离衰减
            if (pr.w.splash > 0) {
                for (size_t i = 0; i < ents.size(); i++) {
                    Ent& o = ents[i];
                    if (!o.alive || (int)i == pr.target) continue;
                    // 溅射同样尊重防空/对地射界（RA2：溅射可伤己方）
                    bool oAir = !o.isBuilding && unitDef(o.utype).isAir() && o.state != UState::Landed;
                    if (oAir && !pr.w.antiAir) continue;
                    if (!oAir && !pr.w.antiGround) continue;
                    float ox = o.x, oy = o.y;
                    if (o.isBuilding) { ox += bldDef(o.btype).w / 2.0f; oy += bldDef(o.btype).h / 2.0f; }
                    float od = distf(tx, ty, ox, oy);
                    if (od > pr.w.splash) continue;
                    float mult = weaponMultiplier(pr.w, o);
                    float falloff = 1.0f - od / (pr.w.splash + 0.5f) * 0.6f; // 中心 100%，边缘 40%
                    damage((int)i, (int)(pr.w.damage * mult * falloff), pr.player, pr.src, pr.srcGarrisonSlot);
                    // 混乱毒气：地面单位（非建筑/非空中）陷入自相残杀 6 秒
                    if (isChaosGas && !o.isBuilding && !unitDef(o.utype).isAir()
                        && !psychicImmune(o.utype) && o.invuln == 0) {
                        o.confused = 180;
                        o.target = INVALID_EID; // 清除原目标，强制重选
                    }
                }
            }
            // 海豚音波命中：短暂声呐揭雾（反潜探测）
            if (valid(pr.src) && !ents[pr.src].isBuilding && ents[pr.src].utype == UnitType::Dolphin)
                map.reveal(pr.player, (int)tx, (int)ty, 4);
            if (pr.kind != ProjKind::Bullet) explodeAt(tx, ty, pr.kind == ProjKind::Missile ? 1 : 0);
            pr.alive = false;
        } else {
            pr.x += (tx - pr.x) / d * spd;
            pr.y += (ty - pr.y) / d * spd;
            if (++pr.trail > 600) pr.alive = false;
        }
    }
    projs.erase(std::remove_if(projs.begin(), projs.end(), [](const Projectile& p) { return !p.alive; }), projs.end());

    // 特效
    for (auto& ef : effects) if (ef.alive && ++ef.age >= ef.maxAge) ef.alive = false;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return !e.alive; }), effects.end());

    // 自动采矿（空闲采矿车找矿；Stop 后 autoHarvest=false 直到再下采矿令）
    for (size_t i = 0; i < ents.size(); i++) {
        Ent& e = ents[i];
        if (!e.alive || e.isBuilding || !unitDef(e.utype).canHarvet()) continue;
        if (e.utype == UnitType::SlaveMiner && e.deployed) continue; // 已部署只作卸货点
        if (e.state == UState::Idle && e.autoHarvest) {
            // 有货却 Idle（回厂失败/无精炼厂等）：优先回厂，勿去找矿空转
            if (e.oreLoad > 0) {
                e.state = UState::HarvestReturn;
                e.dockRefinery = INVALID_EID; // updateHarvester 内重寻停靠点
                continue;
            }
            Vec2i ore;
            if (map.findNearestOre((int)e.x, (int)e.y, 48, ore)) { // TiberiumFarScan
                e.oreCell = ore;
                std::vector<Vec2i> path;
                if (map.findPath((int)e.x, (int)e.y, ore.x, ore.y, path)) {
                    e.path = std::move(path); e.pathIdx = 0;
                    e.state = UState::HarvestGo;
                }
                // 寻路失败：保持 Idle，下轮再试（矿脉被挡时不卡在 HarvestGo）
            }
        }
    }

    // 迷雾
    for (int pi = 0; pi < numPlayers; pi++) updateFog(pi);
    if (sharedVision) {
        for (int a = 0; a < numPlayers; ++a)
            for (int b = a + 1; b < numPlayers; ++b) {
                if (!isAllied(a, b)) continue;
                for (size_t i = 0; i < map.fog[a].size(); ++i) {
                    uint8_t f = std::max(map.fog[a][i], map.fog[b][i]);
                    map.fog[a][i] = map.fog[b][i] = f;
                }
            }
    }
    applyGapShroud(); // 裂缝产生器黑幕最后覆盖（RA2 原作：间谍卫星也无法穿透）
}

void World::updateUnit(Ent& e, EID id) {
    const UnitDef& ud = unitDef(e.utype);
    if (e.atkCd > 0) e.atkCd--;
    if (e.invuln > 0) e.invuln--;
    if (e.fireAnim > 0) e.fireAnim--; // 开火动画序列推进
    if (e.utype == UnitType::GatlingTank) {
        if (e.gatlingHeat > 0 && e.state != UState::Attacking) e.gatlingHeat = std::max(0, e.gatlingHeat - 2);
        e.gatlingStage = e.gatlingHeat >= 120 ? 2 : (e.gatlingHeat >= 50 ? 1 : 0);
    }
    // YR：遥控坦克依赖通电的机器人指挥中心；断电或无 RCC 则停摆
    if (e.utype == UnitType::RobotTank) {
        bool rccOnline = e.player >= 0 && hasBld(e.player, BldType::RobotControl) && !players[e.player].lowPower();
        if (!rccOnline) {
            e.path.clear();
            e.target = INVALID_EID;
            e.state = UState::Idle;
            return;
        }
    }
    // 心灵控制链接维护：被控单位消失（运输装载/进驻等消耗路径）则清空控制者链接
    if (e.mindTarget != INVALID_EID && !valid(e.mindTarget)) e.mindTarget = INVALID_EID;
    e.mindTargets.erase(std::remove_if(e.mindTargets.begin(), e.mindTargets.end(),
        [&](EID t) { return !valid(t) || ents[t].mindBy != id; }), e.mindTargets.end());
    if (e.utype == UnitType::MasterMind && e.mindTargets.size() > 3
        && tick % 30 == (uint64_t)(id % 30)) {
        damage(id, 20 * ((int)e.mindTargets.size() - 3), e.player);
        if (!valid(id)) return;
    }
    if (e.mindBy != INVALID_EID && !valid(e.mindBy)) { e.player = e.origPlayer; e.mindBy = INVALID_EID; e.origPlayer = -1; }
    // 磁电举升：束流存在时升高并被拖向磁电坦克；束流断开立即坠落受伤。
    if (e.magneticBy != INVALID_EID) {
        if (valid(e.magneticBy) && ents[e.magneticBy].utype == UnitType::Magnetron
            && ents[e.magneticBy].target == id && distf(e.x, e.y, ents[e.magneticBy].x, ents[e.magneticBy].y) <= 10.0f) {
            e.magneticHeight = std::min(12, e.magneticHeight + 1);
            e.x += (ents[e.magneticBy].x - e.x) * 0.025f;
            e.y += (ents[e.magneticBy].y - e.y) * 0.025f;
            return;
        }
        int fall = e.magneticHeight;
        e.magneticBy = INVALID_EID;
        e.magneticHeight = 0;
        if (fall > 0) {
            damage(id, fall * 12, -1);
            if (!valid(id)) return;
        }
    }
    if (ud.isAir()) { updateAircraft(e, id); return; }
    // 超时空传送后相位不适：完全冻结
    if (e.tpSick > 0) { e.tpSick--; return; }
    // 超时空抹除进度：>0 期间冻结且免疫伤害；累积超阈值即抹除，否则缓慢衰减
    if (e.chrono > 0) {
        int threshold = ud.hp / 3 + 20;
        if (e.chrono >= threshold) {
            Effect ef; ef.kind = 9; ef.x = e.x; ef.y = e.y; ef.maxAge = 26; effects.push_back(ef);
            g_sfx.playAt(Sfx::Tesla, e.x, e.y);
            if (e.player >= 0 && players[e.player].evaUnitCd <= 0) {
                eva(e.player, TR(S::EvaUnitLost));
                players[e.player].evaUnitCd = 150;
            }
            e.alive = false;
            freeList.push_back(id);
            checkDefeat();
            return;
        }
        if (tick % 2 == 0) e.chrono--; // 衰减：停止攻击后约 2 倍时间恢复
        return; // 冻结中不能行动
    }
    // 恐怖机器人寄生中：附着宿主持续啃噬（RA2 原作：仅维修厂可摘除）
    if (e.parasiting) {
        if (!valid(e.parasiteHost)) {
            // 宿主已毁（kill 中通常会脱离复位；此处兜底）
            e.parasiting = false;
            e.state = UState::Idle;
            return;
        }
        Ent& h = ents[e.parasiteHost];
        e.x = h.x; e.y = h.y; // 跟随宿主（绘制时隐藏）
        // 宿主边走边掀：左右交替（与基洛夫炸翻同动画）
        bool hostMoving = !h.path.empty() && h.pathIdx < (int)h.path.size();
        if (hostMoving || tick % 10 == 0)
            h.rockTilt = std::max(h.rockTilt, 14);
        if (tick % 18 == (uint64_t)(id % 18)) {
            damage(e.parasiteHost, 12, e.player, id);
            // 电火花特效提示宿主被寄生
            Effect sp; sp.kind = 2; sp.x = h.x; sp.y = h.y;
            sp.x2 = h.x + 0.4f; sp.y2 = h.y + 0.4f; sp.maxAge = 5;
            effects.push_back(sp);
        }
        return;
    }
    if (e.rockTilt > 0) e.rockTilt--;
    // 辐射工兵已部署：不能移动/普攻，周期性辐射范围伤害
    if (e.radDeployed) {
        if (tick % 12 == (uint64_t)(id % 12)) {
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& o = ents[i];
                if (!o.alive || o.isBuilding || (int)i == id) continue;
                if (unitDef(o.utype).isAir()) continue;
                if (o.utype == UnitType::Desolator) continue; // 辐射工兵免疫辐射
                if (distf(o.x, o.y, e.x, e.y) > 3.0f) continue;
                int dmg = unitDef(o.utype).isInfantry() ? 14 : 4;
                damage((int)i, dmg, e.player);
            }
            // 绿色辐射辉光
            Effect ef; ef.kind = 12;
            ef.x = e.x + (rng.unit() - 0.5f) * 4.0f; ef.y = e.y + (rng.unit() - 0.5f) * 4.0f;
            ef.maxAge = 22; effects.push_back(ef);
        }
        return;
    }
    // 幻影坦克：静止积累伪装成树（移动/开火解除，见 moveAlongPath/fireWeapon）
    if (e.utype == UnitType::MirageTank && !e.camouflaged && e.state == UState::Idle) {
        if (++e.camoTick >= 90) e.camouflaged = true;
    }
    // 老兵/精英按规则自愈。
    if (e.vetRank > 0 && e.hp < maxHpFor(e, ud) && tick % 45 == (uint64_t)(id % 45))
        e.hp = std::min(maxHpFor(e, ud), e.hp + g_gameRules.veteranSelfHeal[std::clamp(e.vetRank, 0, 2)]);
    // 航空母舰：舰载机整备补充（RA2 原作：损失后缓慢再造，上限 3 架）
    if (e.utype == UnitType::AircraftCarrier && (int)e.cargo.size() < ud.cargoCap
        && tick % 240 == (uint64_t)(id % 240)) {
        Ent::GarrisonedUnit slot{};
        slot.type = UnitType::Hornet;
        slot.hp = unitDef(UnitType::Hornet).hp;
        e.cargo.push_back(slot);
    }
    // IFV + 工程师：维修车模式，周期修复周围受损友军车辆（RA2 原作签名组合）
    if (e.utype == UnitType::IFV && !e.cargo.empty() && e.cargo[0].type == UnitType::Engineer
        && tick % 25 == (uint64_t)(id % 25)) {
        for (Ent& o : ents) {
            if (!o.alive || o.isBuilding || o.player != e.player) continue;
            const UnitDef& od = unitDef(o.utype);
            if (od.isInfantry() || od.isAir() || o.hp >= od.hp) continue;
            if (distf(o.x, o.y, e.x, e.y) > 4.0f) continue;
            o.hp = std::min(od.hp, o.hp + 20);
            Effect ef; ef.kind = 5; ef.x = o.x; ef.y = o.y; ef.maxAge = 6; effects.push_back(ef);
            break; // 每轮修一辆
        }
    }
    // 战斗要塞：全体载员可同时对外射击（每 8 帧一轮，射程+1）
    if (e.utype == UnitType::BattleFortress && !e.cargo.empty() && tick % 8 == (uint64_t)(id % 8)) {
        for (size_t slot = 0; slot < e.cargo.size(); ++slot) {
            WeaponDef pw = unitDef(e.cargo[slot].type).weapon;
            if (pw.damage <= 0) continue;
            pw.range += 1;
            pw.damage = (int)(pw.damage * 1.1f);
            EID tgt = findNearestEnemy(e.player, e.x, e.y, (float)pw.range, true, &pw, e.cargo[slot].type);
            if (tgt == INVALID_EID) continue;
            const Ent& t = ents[tgt];
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            Projectile p;
            p.kind = strcmp(pw.projSprite, "flak") == 0 ? ProjKind::Flak : ProjKind::Bullet;
            p.player = e.player; p.x = e.x; p.y = e.y; p.tx = tx; p.ty = ty;
            p.target = tgt; p.src = id; p.w = pw;
            projs.push_back(p);
        }
    }
    // 台风潜艇：开火暴露计时衰减
    if (e.subReveal > 0) e.subReveal--;
    if (e.crateDmgBoost > 0) e.crateDmgBoost--;
    if (e.crateArmorBoost > 0) e.crateArmorBoost--;
    if (e.crateSpeedBoost > 0) e.crateSpeedBoost--;
    // 鲍里斯：米格空袭冷却衰减
    if (e.airstrikeCd > 0) e.airstrikeCd--;
    // 混乱无人机毒气：混乱计时衰减
    if (e.confused > 0) e.confused--;
    // 重装大兵/美国大兵已部署：不能移动，以强化武器迎战（RA2 原作设定）
    if ((e.utype == UnitType::GuardianGI || e.utype == UnitType::GI) && e.deployed) {
        const WeaponDef& dw = e.utype == UnitType::GuardianGI ? ggiDeployedWeapon() : giDeployedWeapon();
        if (!valid(e.target)) {
            e.target = findNearestEnemy(e.player, e.x, e.y, (float)dw.range, true, &dw, e.utype);
            if (e.target == INVALID_EID) { e.state = UState::Idle; return; }
        }
        const Ent& t = ents[e.target];
        float tx = t.x, ty = t.y;
        if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
        if (distf(e.x, e.y, tx, ty) > dw.range) { e.target = INVALID_EID; e.state = UState::Idle; return; }
        e.dir = dirFromVec(tx - e.x, ty - e.y);
        e.turretDir = e.dir;
        if (e.atkCd <= 0) { fireWeapon(e, id, e.target); e.atkCd = dw.cooldown; }
        return;
    }
    // V3：部署后固定发射；最小射程 5（过近拒射）
    if (e.utype == UnitType::V3Launcher && e.deployed) {
        const WeaponDef& dw = ud.weapon;
        constexpr float V3_MIN = 5.0f;
        if (!valid(e.target)) {
            e.target = findNearestEnemy(e.player, e.x, e.y, (float)dw.range, true, &dw, e.utype);
            if (e.target == INVALID_EID) { e.state = UState::Idle; return; }
        }
        const Ent& t = ents[e.target];
        float tx = t.x, ty = t.y;
        if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
        float d = distf(e.x, e.y, tx, ty);
        if (d > dw.range + 1 || d < V3_MIN) {
            e.target = INVALID_EID;
            e.state = UState::Idle;
            return;
        }
        e.turretDir = dirFromVec(tx - e.x, ty - e.y);
        if (e.atkCd <= 0) { fireWeapon(e, id, e.target); e.atkCd = dw.cooldown; }
        return;
    }
    // 奴隶矿车已部署：固定为卸货点，不移动（产奴在 World::update）
    if (e.utype == UnitType::SlaveMiner && e.deployed) {
        e.path.clear();
        e.state = UState::Idle;
        return;
    }
    // 混乱毒气：被影响的单位强制攻击最近单位（含己方，自相残杀），持续至 confused 归零
    if (e.confused > 0 && !e.isBuilding) {
        const WeaponDef& cw = effWeapon(e);
        // 失去目标或目标无效/超出射程：重新索敌（任意阵营，含己方）
        if (!valid(e.target) || ents[e.target].player == e.player
            || distf(e.x, e.y, ents[e.target].x, ents[e.target].y) > cw.range + 1) {
            EID best = INVALID_EID; float bd = cw.range + 1;
            for (size_t i = 0; i < ents.size(); i++) {
                if ((int)i == id) continue;
                const Ent& o = ents[i];
                if (!o.alive || o.isBuilding) continue;
                if (o.invuln > 0) continue; // 铁幕无敌不攻击
                if (unitDef(o.utype).isAir()) continue; // 毒气仅影响地面单位
                float ox = o.x, oy = o.y;
                float d = distf(e.x, e.y, ox, oy);
                if (d < bd) { bd = d; best = (int)i; }
            }
            e.target = best;
        }
        if (valid(e.target)) {
            const Ent& t = ents[e.target];
            float tx = t.x, ty = t.y;
            float d = distf(e.x, e.y, tx, ty);
            if (d > cw.range) { e.state = UState::Chasing; } // 追击至射程内
            else {
                e.state = UState::Attacking;
                e.turretDir = dirFromVec(tx - e.x, ty - e.y);
                if (!unitHasTurret(e.utype)) e.dir = e.turretDir;
                if (e.atkCd <= 0) { fireWeapon(e, id, e.target); e.atkCd = cw.cooldown; }
            }
        } else {
            e.state = UState::Idle;
        }
        return;
    }
    // 重伤冒烟
    if (e.hp < maxHpFor(e, ud) / 2 && !ud.isInfantry() && tick % 25 == (uint64_t)(id % 25)) {
        Effect sm;
        sm.kind = 1; sm.x = e.x; sm.y = e.y; sm.maxAge = 30;
        effects.push_back(sm);
    }

    // 维修厂停靠维修（RA2 原作：车辆到位后持续扣钱回血，并摘除恐怖机器人寄生）
    if (!ud.isInfantry() && e.target != INVALID_EID && valid(e.target)
        && (e.state == UState::Idle || e.state == UState::Moving)) {
        Ent& dp = ents[e.target];
        if (dp.isBuilding && dp.btype == BldType::ServiceDepot && dp.player == e.player) {
            const BldDef& dd = bldDef(dp.btype);
            float cx = dp.x + dd.w / 2.0f, cy = dp.y + dd.h / 2.0f;
            if (distf(e.x, e.y, cx, cy) <= std::max(dd.w, dd.h) / 2.0f + 2.0f) {
                if (e.parasite != INVALID_EID) { // 摘除寄生
                    if (valid(e.parasite)) kill(e.parasite);
                    e.parasite = INVALID_EID;
                }
                if (e.hp >= maxHpFor(e, ud)) {
                    e.target = INVALID_EID; // 修满离开
                } else if (tick % 5 == 0) {
                    Player& p = players[e.player];
                    if (p.money >= 6) { p.money -= 6; e.hp = std::min(maxHpFor(e, ud), e.hp + 12); }
                }
                if (e.state == UState::Moving && e.pathIdx >= (int)e.path.size()) e.state = UState::Idle;
                return; // 维修期间不索敌不移动
            }
        }
    }

    // 工程师到达目标建筑：占领
    if (e.utype == UnitType::Engineer && e.target != INVALID_EID && valid(e.target)) {
        Ent& b = ents[e.target];
        if (b.isBuilding && b.player != e.player && bldDef(b.btype).capturable) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            // 贴边抵达：半径按脚印对角线放宽，避免大建筑占不到
            float reach = std::max(bd.w, bd.h) / 2.0f + 2.5f;
            if (distf(e.x, e.y, bx, by) < reach) {
                eva(b.player, TextFormat(TR(S::EvaBldCapturedFmt), bldName(b.btype)));
                eva(e.player, TextFormat(TR(S::EvaCapturedFmt), bldName(b.btype)));
                b.player = e.player;
                b.hp = bd.hp;
                applyCaptureEffect(b, e.player); // 科技机场/秘密实验室等特殊效果
                recomputePower();
                e.alive = false;
                freeList.push_back(id);
                return;
            }
        }
        // 己方受损建筑：进入修复（RA2 原作：瞬间回满，工程师消耗）
        if (b.isBuilding && b.player == e.player && b.hp < bldDef(b.btype).hp) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 2.5f) {
                b.hp = bd.hp;
                eva(e.player, TextFormat(TR(S::EvaEngRepairedFmt), bldName(b.btype)));
                g_sfx.playAt(Sfx::Place, bx, by);
                e.alive = false;
                freeList.push_back(id);
                return;
            }
        }
    }

    // 间谍到达目标建筑：渗透生效（RA2 原作：间谍消耗，按建筑类型产生效果）
    if (e.utype == UnitType::Spy && e.target != INVALID_EID && valid(e.target)) {
        Ent& b = ents[e.target];
        if (b.isBuilding && isEnemy(e.player, b.player)) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 2.5f) {
                applySpyEffect(e, b, id);
                return;
            }
        }
    }

    switch (e.state) {
        case UState::Idle: {
            // 自动索敌（警戒模式按视野半径，普通按射程+2）
            // 空载采矿车优先采矿：勿被武装矿车 Idle 索敌抢走
            const UnitDef& udIdle = unitDef(e.utype);
            if (!(udIdle.canHarvet() && e.autoHarvest && e.oreLoad == 0)) {
                const WeaponDef ew = effWeapon(e);
                if (ew.damage > 0) {
                    float scanR = e.guard ? (float)std::max(ud.sight, ew.range + 2)
                                          : (float)(ew.range + 2);
                    EID en = findNearestEnemy(e.player, e.x, e.y, scanR, true, &ew, e.utype);
                    if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; }
                }
            }
            break;
        }
        case UState::Moving:
        case UState::AttackMoving: {
            const WeaponDef ew = effWeapon(e);
            if (e.state == UState::AttackMoving && ew.damage > 0) {
                // 有炮塔：边走边打；无炮塔：进入追击停下开火
                if (unitHasTurret(e.utype)) {
                    EID en = findNearestEnemy(e.player, e.x, e.y, (float)ew.range, true, &ew, e.utype);
                    if (en != INVALID_EID) {
                        e.target = en;
                        const Ent& t = ents[en];
                        float tx = t.x, ty = t.y;
                        if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
                        int wantTur = dirFromVec(tx - e.x, ty - e.y);
                        if (e.turretDir != wantTur)
                            e.turretDir = rotStepDir(e.turretDir, wantTur);
                        if (rotDistDir(e.turretDir, wantTur) <= 1 && e.atkCd <= 0) {
                            fireWeapon(e, id, en);
                            e.atkCd = ew.cooldown;
                        }
                    }
                } else {
                    EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ew.range + 1), true, &ew, e.utype);
                    if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; break; }
                }
            }
            moveAlongPath(e, id);
            if (e.pathIdx >= (int)e.path.size()) {
                if (!e.wps.empty()) { // 路径点接续（RA2 原作 Z 键路径）
                    auto w = e.wps.front();
                    e.wps.pop_front();
                    orderMove({id}, w.first, w.second, e.state == UState::AttackMoving);
                } else {
                    e.state = UState::Idle;
                }
            }
            break;
        }
        case UState::Chasing: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            const Ent& t = ents[e.target];
            const WeaponDef ew = effWeapon(e);
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            float d = distf(e.x, e.y, tx, ty);
            // C4 爆破手攻击建筑需贴脸（2.5 格），而非武器射程
            float effR = (ud.hasC4() && t.isBuilding) ? 2.5f : (float)ew.range;
            if (d <= effR) {
                // 进入射程：停步转入开火（炮塔单位也不再贴脸推进）
                e.path.clear();
                e.state = UState::Attacking;
                bool hasTur = unitHasTurret(e.utype);
                if (hasTur) {
                    int wantTur = dirFromVec(tx - e.x, ty - e.y);
                    if (e.turretDir != wantTur)
                        e.turretDir = rotStepDir(e.turretDir, wantTur);
                    if (rotDistDir(e.turretDir, wantTur) <= 1 && e.atkCd <= 0) {
                        fireWeapon(e, id, e.target);
                        e.atkCd = ew.cooldown;
                    }
                }
            } else {
                // 超时空军团兵/超时空突击队追击：直接传送至目标射程边缘后开火
                if (e.utype == UnitType::Chrono || e.utype == UnitType::ChronoCommando
                    || e.utype == UnitType::ChronoIvan) {
                    float nx = tx - (tx - e.x) / d * (effR * 0.8f);
                    float ny = ty - (ty - e.y) / d * (effR * 0.8f);
                    EID keepT = e.target;
                    chronoJump(e, nx, ny);
                    e.target = keepT;
                    e.state = UState::Attacking; // 传送到位后立即进入开火（相位不适结束后才真正开枪）
                    break;
                }
                // 每 30 帧重寻路
                if (e.path.empty() || (tick % 30) == 0) {
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, (int)tx, (int)ty, path, 20000, ud.pathDomain())) {
                        e.path = std::move(path); e.pathIdx = 0;
                    }
                }
                moveAlongPath(e, id);
                // 追击中：车体朝移动方向，炮塔朝目标
                if (unitHasTurret(e.utype)) {
                    int wantTur = dirFromVec(tx - e.x, ty - e.y);
                    if (e.turretDir != wantTur)
                        e.turretDir = rotStepDir(e.turretDir, wantTur);
                }
            }
            break;
        }
        case UState::Attacking: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            const Ent& t = ents[e.target];
            const WeaponDef ew = effWeapon(e);
            // 射界：空中目标需防空武器（目标起飞后立即放弃）
            bool airT = !t.isBuilding && unitDef(t.utype).isAir() && t.state != UState::Landed;
            if (airT && !ew.antiAir) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            if (!airT && !ew.antiGround) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            float d = distf(e.x, e.y, tx, ty);
            if (d > ew.range + 1) { e.state = UState::Chasing; break; }
            // C4 爆破手：建筑目标超出贴脸距离 → 重新贴近
            if (ud.hasC4() && t.isBuilding && d > 2.5f) { e.state = UState::Chasing; break; }
            // 尤里无法控制建筑：放弃目标（尤里首脑除外）
            if (e.utype == UnitType::Yuri && t.isBuilding) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            // 尤里首脑：心控敌方建筑/防御（改归属）
            if (e.utype == UnitType::YuriPrime && t.isBuilding && e.atkCd <= 0) {
                if (t.player >= 0 && isEnemy(e.player, t.player) && t.btype != BldType::ConYard) {
                    evacuateGarrison(e.target);
                    Ent& b = ents[e.target];
                    b.player = e.player;
                    b.state = UState::Idle; b.target = INVALID_EID;
                    recomputePower();
                    Effect ef; ef.kind = 2; ef.x = e.x; ef.y = e.y; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 16;
                    effects.push_back(ef);
                    g_sfx.playAt(Sfx::Tesla, tx, ty);
                    if (e.player == 0) eva(0, TR(S::EvaMindGain));
                }
                e.atkCd = ew.cooldown;
                e.target = INVALID_EID; e.state = UState::Idle;
                break;
            }
            // 航空母舰：放飞舰载机空袭（RA2 原作：大黄蜂起飞→投弹→返航整备）
            if (e.utype == UnitType::AircraftCarrier) {
                if (e.atkCd <= 0 && !e.cargo.empty()) {
                    e.cargo.pop_back();
                    EID tgt0 = e.target;
                    int pl = e.player;
                    float sx0 = e.x, sy0 = e.y;
                    EID h = spawnUnit(pl, UnitType::Hornet, sx0, sy0); // 注意：spawnUnit 可能扩容 ents，e/t 引用失效
                    Ent& he = ents[h];
                    he.target = tgt0;
                    he.airbase = id; // 母舰（返航归舰）
                    he.state = UState::Chasing;
                    g_sfx.playAt(Sfx::Missile, sx0, sy0);
                    ents[id].atkCd = 70;
                }
                break;
            }
            // 磁电坦克：车辆/舰船被举升后不能行动；束流断开按高度坠落受伤。
            if (e.utype == UnitType::Magnetron && !t.isBuilding) {
                const UnitDef& td = unitDef(t.utype);
                if (!td.isInfantry() && !td.isAir() && e.atkCd <= 0) {
                    Ent& lifted = ents[e.target];
                    lifted.magneticBy = id;
                    e.atkCd = ew.cooldown;
                    Effect beam; beam.kind = 3; beam.x = e.x; beam.y = e.y;
                    beam.x2 = lifted.x; beam.y2 = lifted.y; beam.maxAge = 12; effects.push_back(beam);
                }
                break;
            }
            // 主脑：前三个目标稳定，多控目标继续归属主脑但触发周期过载自伤。
            if (e.utype == UnitType::MasterMind && !t.isBuilding) {
                if (!psychicImmune(t.utype) && !t.permaControlled && t.mindBy == INVALID_EID && e.atkCd <= 0) {
                    Ent& controlled = ents[e.target];
                    controlled.origPlayer = controlled.player;
                    controlled.player = e.player;
                    controlled.mindBy = id;
                    controlled.state = UState::Idle; controlled.target = INVALID_EID; controlled.path.clear();
                    e.mindTargets.push_back(e.target);
                    e.atkCd = ew.cooldown;
                }
                e.target = INVALID_EID;
                e.state = UState::Idle;
                break;
            }
            // 恐怖机器人：贴脸后附着宿主（RA2 原作：钻入车辆内部持续破坏）
            if (e.utype == UnitType::TerrorDrone && !t.isBuilding) {
                const UnitDef& td = unitDef(t.utype);
                if (!td.isInfantry() && !td.isAir() && !td.isNaval() && t.parasite == INVALID_EID && d <= 1.4f) {
                    Ent& host = ents[e.target];
                    host.parasite = id;
                    e.parasiting = true;
                    e.parasiteHost = e.target;
                    e.target = INVALID_EID;
                    e.path.clear();
                    Effect sp; sp.kind = 2; sp.x = host.x; sp.y = host.y;
                    sp.x2 = host.x + 0.4f; sp.y2 = host.y + 0.4f; sp.maxAge = 8;
                    effects.push_back(sp);
                    g_sfx.playAt(Sfx::Tesla, host.x, host.y);
                    break;
                }
            }
            // 巨型乌贼：贴身缠绕敌舰（RA2 原作：宿主定身+持续挤压，可被反潜火力攻击摘除）
            if (e.utype == UnitType::Squid && !t.isBuilding) {
                const UnitDef& td = unitDef(t.utype);
                if ((td.isNaval() || td.isAmphib()) && t.parasite == INVALID_EID && d <= 1.6f) {
                    Ent& host = ents[e.target];
                    host.parasite = id;
                    e.parasiting = true;
                    e.parasiteHost = e.target;
                    e.target = INVALID_EID;
                    e.path.clear();
                    Effect sp; sp.kind = 2; sp.x = host.x; sp.y = host.y;
                    sp.x2 = host.x - 0.4f; sp.y2 = host.y + 0.4f; sp.maxAge = 8;
                    effects.push_back(sp);
                    g_sfx.playAt(Sfx::Torpedo, host.x, host.y);
                    break;
                }
            }
            // 尤里/心灵突击队：心灵控制地面单位（RA2 原作：夺取敌方单位控制权，同一时刻仅一个）
            if (ud.isPsychic() && !t.isBuilding) {
                if (psychicImmune(t.utype) || t.permaControlled || t.mindBy != INVALID_EID) {
                    // 免疫/已被控制：放弃该目标，避免无效贴身
                    e.target = INVALID_EID; e.state = UState::Idle;
                    break;
                }
                if (e.atkCd <= 0) {
                    mindControlTake(e, id, e.target);
                    e.atkCd = ew.cooldown;
                }
                break;
            }
            // 鲍里斯：对建筑呼叫米格空袭（YR 原作：激光照射→2 架米格投弹，建筑目标专用）
            if (e.utype == UnitType::Boris && t.isBuilding && e.airstrikeCd <= 0 && e.atkCd <= 0) {
                // 米格投弹：2 枚延迟炸弹依次落地（复用 TimedBomb 机制，附着建筑中心）
                for (int i = 0; i < 2; i++) {
                    TimedBomb b;
                    b.x = tx; b.y = ty; b.player = e.player;
                    b.attachedTo = e.target; b.dmg = 300; b.radius = 1.8f;
                    b.timer = 50 + i * 15; // 第二枚稍后落地
                    timedBombs.push_back(b);
                }
                // 照射光束特效（指向建筑）
                Effect beam; beam.kind = 3; beam.x = e.x; beam.y = e.y;
                beam.x2 = tx; beam.y2 = ty; beam.maxAge = 12;
                effects.push_back(beam);
                g_sfx.playAt(Sfx::Missile, tx, ty);
                e.airstrikeCd = 600; // 20 秒后再呼叫
                e.atkCd = 90;
                break;
            }
            // 鲍里斯对建筑仅用空袭：不发射 AK（AK 反步兵，对建筑几乎无效）
            if (e.utype == UnitType::Boris && t.isBuilding) break;
            // V3 未部署：进入射程后自动升起发射架（不可未部署开火）
            if (e.utype == UnitType::V3Launcher && !e.deployed) {
                e.deployed = true;
                e.path.clear();
                e.state = UState::Idle;
                g_sfx.playAt(Sfx::Deploy, e.x, e.y);
                break;
            }
            // C4 爆破手（谭雅/海豹/超时空突击队/心灵突击队）：近身建筑安放 C4；会游泳的还可炸舰船
            if (ud.hasC4() && e.atkCd <= 0) {
                bool navalTgt = !t.isBuilding && (unitDef(t.utype).isNaval() || unitDef(t.utype).isAmphib());
                if ((t.isBuilding || (navalTgt && ud.canSwim())) && d <= 2.5f) {
                    TimedBomb b;
                    b.x = tx; b.y = ty; b.timer = 45; b.player = e.player;
                    b.attachedTo = e.target; b.dmg = 6000; b.radius = 0.6f;
                    timedBombs.push_back(b);
                    Effect mz; mz.kind = 5; mz.x = tx; mz.y = ty; mz.maxAge = 6;
                    effects.push_back(mz);
                    g_sfx.playAt(Sfx::Click, tx, ty);
                    e.atkCd = 60; // 撤离间隙
                    break;
                }
            }
            // C4 爆破手不对建筑开枪：等待下次爆破冷却（RA2 原作：谭雅/海豹对建筑仅用 C4）
            if (ud.hasC4() && t.isBuilding) break;
            // 直接攻击：射程内停步；仅攻击移动才边走边打
            bool hasTur = unitHasTurret(e.utype);
            // 超时空军团兵：持续抹除期间保持开火姿势（neutron rifle 持续照射）
            if (e.utype == UnitType::Chrono) {
                const UnitAnimInfo& fai = g_sprites.animInfo(e.utype);
                if (fai.fire > 0) e.fireAnim = fai.fire * 2;
            }
            // 面向目标：有炮塔则仅转炮塔（车体保持移动朝向）；无炮塔转车体
            int wantDir = dirFromVec(tx - e.x, ty - e.y);
            {
                int turPace = 1; // 炮塔：每 tick 最多转 45°
                int hullPace = ud.isInfantry() ? 1 : (ud.isNaval() ? 3 : 2);
                if (hasTur) {
                    if (e.turretDir != wantDir && (tick % turPace) == 0)
                        e.turretDir = rotStepDir(e.turretDir, wantDir);
                    // 车体不跟着目标转（RA2：炮塔独立朝向）
                } else {
                    if (e.dir != wantDir && (tick % hullPace) == 0)
                        e.dir = rotStepDir(e.dir, wantDir);
                    e.turretDir = e.dir;
                }
                int face = hasTur ? e.turretDir : e.dir;
                if (rotDistDir(face, wantDir) <= 1 && e.atkCd <= 0) {
                    fireWeapon(e, id, e.target);
                    e.atkCd = ew.cooldown;
                }
            }
            break;
        }
        case UState::HarvestGo: case UState::HarvestDig: case UState::HarvestReturn: case UState::HarvestUnload:
            updateHarvester(e, id);
            break;
        case UState::Boarding: {
            // 步兵登船/进驻建筑：接近目标后进入货舱或驻军
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            Ent& t = ents[e.target];
            // 进驻建筑（民房/战斗碉堡/坦克碉堡）
            if (t.isBuilding) {
                const BldDef& bd = bldDef(t.btype);
                int gdom = garrisonDomain(t.btype);
                bool fit = (gdom == 1 && ud.isInfantry())
                        || (gdom == 2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain() == 0 && !ud.canHarvet())
                        || (gdom == 3 && e.player == t.player && !ud.isAir() && !isHero(e.utype));
                if (bd.garrisonCap == 0 || !fit || (t.player >= 0 && t.player != e.player)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
                float bx = t.x + bd.w / 2.0f, by = t.y + bd.h / 2.0f;
                if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 1.5f) {
                    if (gdom == 3) {
                        int refund = (int)(unitDef(e.utype).cost * g_gameRules.grinderRefund);
                        players[e.player].money = std::min(g_gameRules.maxMoney, players[e.player].money + refund);
                        e.alive = false;
                        freeList.push_back(id);
                        break;
                    }
                    if ((int)t.garrison.size() < bd.garrisonCap) {
                        t.garrison.push_back({e.utype, e.hp, e.kills, e.veterancyValue, e.vetRank});
                        if (t.player < 0) t.player = e.player; // 进驻中立民房：归属占领方
                        e.alive = false;
                        freeList.push_back(id); // 已进驻（不触发爆炸）
                        if (t.btype == BldType::BioReactor) recomputePower();
                    } else {
                        e.target = INVALID_EID;
                        e.state = UState::Idle; // 驻满
                    }
                    break;
                }
                if (e.path.empty() || (tick % 30) == 0) {
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, (int)bx, (int)by, path, 20000, ud.pathDomain())) {
                        e.path = std::move(path); e.pathIdx = 0;
                    }
                }
                moveAlongPath(e, id);
                break;
            }
            // 登上运输载具
            {
                const UnitDef& td = unitDef(t.utype);
                if (t.player != e.player || td.cargoCap == 0) { e.target = INVALID_EID; e.state = UState::Idle; break; }
                if (distf(e.x, e.y, t.x, t.y) < 1.6f) {
                    if ((int)t.cargo.size() < td.cargoCap) {
                        Ent::GarrisonedUnit slot{};
                        slot.type = e.utype;
                        slot.hp = e.hp;
                        slot.kills = e.kills;
                        slot.veterancyValue = e.veterancyValue;
                        slot.vetRank = e.vetRank;
                        t.cargo.push_back(slot);
                        e.alive = false;
                        freeList.push_back(id); // 已登船（不触发爆炸）
                    } else {
                        e.target = INVALID_EID;
                        e.state = UState::Idle; // 舱满
                    }
                    break;
                }
                if (e.path.empty() || (tick % 30) == 0) {
                    int gx, gy;
                    if (boardGoal(t, ud.pathDomain(), gx, gy)) {
                        std::vector<Vec2i> path;
                        if (map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain())) {
                            e.path = std::move(path); e.pathIdx = 0;
                        }
                    }
                }
                moveAlongPath(e, id);
            }
            break;
        }
    }
}

// ===================== 战机 =====================
Vec2f World::airPadPos(const Ent& af, int slot) const {
    const BldDef& d = bldDef(af.btype);
    static const float off[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}};
    return {af.x + d.w / 2.0f + off[slot & 3][0], af.y + d.h / 2.0f + off[slot & 3][1]};
}

bool World::flyToward(Ent& e, float tx, float ty) {
    const UnitDef& ud = unitDef(e.utype);
    float d = distf(e.x, e.y, tx, ty);
    float step = g_gameRules.veteranSpeedBonus[std::clamp(e.vetRank, 0, 2)] / ud.speed;
    if (d <= step) {
        e.x = tx; e.y = ty;
        return true;
    }
    e.dir = dirFromVec(tx - e.x, ty - e.y);
    e.turretDir = e.dir;
    e.x += (tx - e.x) / d * step;
    e.y += (ty - e.y) / d * step;
    return false;
}

void World::updateAircraft(Ent& e, EID id) {
    const UnitDef& ud = unitDef(e.utype);
    // 重伤冒烟
    if (e.hp < maxHpFor(e, ud) / 2 && e.state != UState::Landed && tick % 25 == (uint64_t)(id % 25)) {
        Effect sm;
        sm.kind = 1; sm.x = e.x; sm.y = e.y; sm.maxAge = 30;
        effects.push_back(sm);
    }

    // 攻城直升机部署后：降落转为固定远程炮台（不可移动，对地/对建筑强力，溅射）
    if (e.utype == UnitType::SiegeChopper && e.deployed) {
        const WeaponDef& dw = siegeChopperDeployedWeapon();
        if (!valid(e.target) || distf(e.x, e.y, ents[e.target].x, ents[e.target].y) > dw.range + 1) {
            e.target = findNearestEnemy(e.player, e.x, e.y, (float)dw.range, true, &dw, e.utype);
            if (e.target == INVALID_EID) { e.state = UState::Idle; return; }
        }
        const Ent& t = ents[e.target];
        float tx = t.x, ty = t.y;
        if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
        e.turretDir = dirFromVec(tx - e.x, ty - e.y);
        if (e.atkCd <= 0) { fireWeapon(e, id, e.target); e.atkCd = dw.cooldown; }
        return;
    }

    switch (e.state) {
        case UState::Idle: // 容错：空中单位不应 Idle
            e.goalX = e.x; e.goalY = e.y;
            e.state = UState::Circling;
            break;
        case UState::Moving:
        case UState::AttackMoving: {
            bool hasAmmo = ud.ammo == 0 || e.ammo > 0; // ammo=0 为无限弹药（基洛夫/火箭飞行兵）
            if (e.state == UState::AttackMoving && ud.weapon.damage > 0 && hasAmmo) {
                EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ud.weapon.range + 3), true, &ud.weapon, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; break; }
            }
            if (flyToward(e, e.goalX, e.goalY) || distf(e.x, e.y, e.goalX, e.goalY) < 0.4f) {
                if (e.state == UState::AttackMoving) e.guard = true; // 攻击移动到达后警戒索敌
                e.state = UState::Circling; // 到达后待命（空艇悬停 / 战机盘旋）
            }
            break;
        }
        case UState::Chasing: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Circling; break; }
            const Ent& t = ents[e.target];
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            if (ud.ammo > 0 && e.ammo <= 0) { e.state = UState::Returning; break; }
            if (e.utype == UnitType::FloatingDisc && t.isBuilding) {
                Ent& b = ents[e.target];
                b.drainedBy = id;
                flyToward(e, tx, ty);
                e.orbitA += 0.04f;
                return;
            }
            if (distf(e.x, e.y, tx, ty) <= ud.weapon.range) {
                e.state = UState::Attacking;
                e.orbitA = atan2f(e.y - ty, e.x - tx);
            } else {
                flyToward(e, tx, ty);
            }
            break;
        }
        case UState::Attacking: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = (ud.ammo == 0 || e.ammo > 0) ? UState::Circling : UState::Returning; break; }
            const Ent& t = ents[e.target];
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            if (ud.ammo > 0 && e.ammo <= 0) { e.state = UState::Returning; break; }
            if (e.utype == UnitType::FloatingDisc && t.isBuilding) {
                Ent& b = ents[e.target];
                if (b.drainedBy != id) { b.drainedBy = id; recomputePower(); }
                // 悬停精炼厂：周期性偷钱（YR 飞碟标志行为）
                if (b.btype == BldType::OreRefinery && b.player >= 0 && tick % 30 == 0) {
                    int steal = std::min(35, players[b.player].money);
                    if (steal > 0) {
                        players[b.player].money -= steal;
                        players[e.player].money = std::min(g_gameRules.maxMoney, players[e.player].money + steal);
                    }
                }
                flyToward(e, tx, ty);
                e.orbitA += 0.04f;
                return;
            }
            // 基洛夫：飞临目标上空，从艇身正下方投弹（非环绕平射）
            if (e.utype == UnitType::Kirov) {
                float d = distf(e.x, e.y, tx, ty);
                flyToward(e, tx, ty);
                e.dir = dirFromVec(tx - e.x, ty - e.y);
                e.turretDir = e.dir;
                if (d <= 1.35f && e.atkCd <= 0) {
                    TimedBomb b;
                    b.x = e.x; b.y = e.y; // 艇底正下方
                    b.player = e.player;
                    b.attachedTo = INVALID_EID;
                    b.dmg = ud.weapon.damage;
                    b.radius = std::max(1.5f, ud.weapon.splash);
                    b.timer = 18; // 下落时间
                    b.rockVehicles = true;
                    timedBombs.push_back(b);
                    Effect drop; drop.kind = 1; drop.x = e.x; drop.y = e.y; drop.maxAge = 16;
                    effects.push_back(drop);
                    g_sfx.playAt(Sfx::Missile, e.x, e.y);
                    e.atkCd = ud.weapon.cooldown;
                    if (ud.ammo > 0) {
                        e.ammo--;
                        if (e.ammo <= 0) { e.target = INVALID_EID; e.state = UState::Returning; }
                    }
                }
                break;
            }
            // 投弹型喷气机（入侵者/黑鹰/米格）：盘旋到射程内投弹后返航
            const bool bombOrbit = e.utype == UnitType::Intruder
                || e.utype == UnitType::BlackEagle
                || e.utype == UnitType::MiG
                || e.utype == UnitType::Hornet;
            if (bombOrbit) {
                e.orbitA += 0.10f;
                float r = ud.weapon.range * 0.75f;
                flyToward(e, tx + cosf(e.orbitA) * r, ty + sinf(e.orbitA) * r);
                e.dir = dirFromVec(tx - e.x, ty - e.y);
                e.turretDir = e.dir;
                if (distf(e.x, e.y, tx, ty) <= ud.weapon.range && e.atkCd <= 0) {
                    fireWeapon(e, id, e.target);
                    if (ud.ammo > 0) e.ammo--;
                    e.atkCd = ud.weapon.cooldown;
                    if (ud.ammo > 0 && e.ammo <= 0) { e.target = INVALID_EID; e.state = UState::Returning; }
                }
                break;
            }
            // 持续空中攻击（火箭飞行兵/夜鹰/攻城直升机等）：保持射程悬停射击，不盘旋
            {
                float d = distf(e.x, e.y, tx, ty);
                float holdR = std::max(1.0f, (float)ud.weapon.range * 0.85f);
                if (d > ud.weapon.range) {
                    flyToward(e, tx, ty);
                } else if (d < holdR * 0.55f) {
                    // 过近则略微拉开
                    float ang = atan2f(e.y - ty, e.x - tx);
                    flyToward(e, tx + cosf(ang) * holdR, ty + sinf(ang) * holdR);
                }
                e.dir = dirFromVec(tx - e.x, ty - e.y);
                e.turretDir = e.dir;
                if (d <= ud.weapon.range && e.atkCd <= 0) {
                    fireWeapon(e, id, e.target);
                    if (ud.ammo > 0) e.ammo--;
                    e.atkCd = ud.weapon.cooldown;
                    if (ud.ammo > 0 && e.ammo <= 0) { e.target = INVALID_EID; e.state = UState::Returning; }
                }
            }
            break;
        }
        case UState::Circling: {
            // 舰载机：不自主盘旋（完成攻击即返航归舰）
            if (e.utype == UnitType::Hornet) { e.state = UState::Returning; break; }
            // 空艇/直升机/飞碟：悬停待命（不绕圈）；喷气战机仍小半径盘旋
            const bool hoverIdle = e.utype == UnitType::Kirov
                || e.utype == UnitType::FloatingDisc
                || e.utype == UnitType::Nighthawk
                || e.utype == UnitType::SiegeChopper
                || e.utype == UnitType::Rocketeer
                || e.utype == UnitType::ChaosDrone;
            if (hoverIdle) {
                if (distf(e.x, e.y, e.goalX, e.goalY) > 0.15f)
                    flyToward(e, e.goalX, e.goalY);
                // 悬停时保持朝向，不每帧改 dir
            } else {
                e.orbitA += 0.10f;
                flyToward(e, e.goalX + cosf(e.orbitA) * 1.5f, e.goalY + sinf(e.orbitA) * 1.5f);
            }
            if (e.guard && ud.weapon.damage > 0 && (ud.ammo == 0 || e.ammo > 0)) {
                EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ud.weapon.range + 3), true, &ud.weapon, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; }
            }
            break;
        }
        case UState::Returning: {
            // 舰载机：返航归舰（母舰已毁则坠毁，RA2 原作设定）
            if (e.utype == UnitType::Hornet) {
                if (!valid(e.airbase) || ents[e.airbase].player != e.player
                    || ents[e.airbase].utype != UnitType::AircraftCarrier) {
                    kill(id); // 无家可归：坠毁
                    return;
                }
                Ent& cv = ents[e.airbase];
                if (flyToward(e, cv.x, cv.y) || distf(e.x, e.y, cv.x, cv.y) < 0.6f) {
                    if ((int)cv.cargo.size() < unitDef(cv.utype).cargoCap) {
                        Ent::GarrisonedUnit slot{};
                        slot.type = UnitType::Hornet;
                        slot.hp = unitDef(UnitType::Hornet).hp;
                        cv.cargo.push_back(slot);
                    }
                    e.alive = false; // 成功着舰：回收（不触发爆炸）
                    freeList.push_back(id);
                }
                return;
            }
            // 校验基地；被毁则找其他空指部
            if (!valid(e.airbase) || ents[e.airbase].player != e.player) {
                e.airbase = INVALID_EID;
                for (size_t i = 0; i < ents.size(); i++) {
                    const Ent& b = ents[i];
                    if (b.alive && b.isBuilding && b.player == e.player && b.btype == BldType::AirForceCmd) {
                        e.airbase = (int)i; break;
                    }
                }
                if (e.airbase == INVALID_EID) {
                    // 无家可归：无空指部落地 → 坠毁（RA2：战机需母港）
                    kill(id);
                    return;
                }
            }
            Vec2f pad = airPadPos(ents[e.airbase], id);
            if (flyToward(e, pad.x, pad.y) || distf(e.x, e.y, pad.x, pad.y) < 0.3f) {
                e.x = pad.x; e.y = pad.y;
                e.state = UState::Landed;
                e.rearmTimer = 0;
            }
            break;
        }
        case UState::Landed: {
            // 停机装填：每 20 帧补 1 发
            if (e.ammo < ud.ammo && ++e.rearmTimer >= 20) {
                e.rearmTimer = 0;
                e.ammo++;
            }
            break;
        }
        default:
            e.state = UState::Circling;
            break;
    }
}

