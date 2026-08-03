#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void World::updateBuilding(Ent& e, EID id) {
    const BldDef& bd = bldDef(e.btype);
    if (e.atkCd > 0) e.atkCd--;
    if (e.btype == BldType::GatlingCannon) {
        if (e.gatlingHeat > 0 && !valid(e.target)) e.gatlingHeat = std::max(0, e.gatlingHeat - 2);
        e.gatlingStage = e.gatlingHeat >= 120 ? 2 : (e.gatlingHeat >= 50 ? 1 : 0);
    }
    if (e.invuln > 0) e.invuln--;
    if (e.constructAnim > 0) e.constructAnim--; // 建造动画推进
    // 建筑被超时空武器照射：冻结且累积抹除（中立建筑同样可被抹除）
    if (e.chrono > 0) {
        int threshold = bd.hp / 3 + 20;
        if (e.chrono >= threshold) {
            Effect ef; ef.kind = 9; ef.x = e.x + bd.w / 2.0f; ef.y = e.y + bd.h / 2.0f; ef.maxAge = 30; effects.push_back(ef);
            g_sfx.playAt(Sfx::Tesla, ef.x, ef.y);
            kill(id); // 抹除（含占地清理）
            return;
        }
        if (tick % 2 == 0) e.chrono--;
        return; // 冻结：停产停火（生产进度由玩家队列管理，不受影响）
    }
    // 中立科技建筑效果（被占领后 player>=0 生效；bldAnim 兼作计时器）
    if (e.player >= 0) {
        e.bldAnim++;
        if (e.btype == BldType::OilDerrick && e.bldAnim % 100 == 0) {
            players[e.player].money += 25; // 油井持续资金
            if (e.player == 0) g_sfx.play(Sfx::Cash, 0.35f);
        } else if (e.btype == BldType::Hospital && e.bldAnim % 60 == 0) {
            // 医院：全体己方步兵持续回血
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player && unitDef(o.utype).isInfantry())
                    o.hp = std::min(unitDef(o.utype).hp, o.hp + 3);
        } else if (e.btype == BldType::MachineShop && e.bldAnim % 60 == 0) {
            // 机械商店：全体己方车辆/舰船持续维修
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player && !unitDef(o.utype).isInfantry())
                    o.hp = std::min(unitDef(o.utype).hp, o.hp + 4);
        } else if (e.btype == BldType::TechOutpost && e.bldAnim % 60 == 0) {
            // 科技前哨站：全体己方单位持续维修+回血（医院+机械商店合体）
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player)
                    o.hp = std::min(unitDef(o.utype).hp, o.hp + 3);
        }
    }
    // 磁暴线圈：附近磁暴步兵为其充电（RA2 原作：充电后伤害+50%，低电仍可开火）
    if (e.btype == BldType::TeslaCoil && e.player >= 0) {
        e.teslaCharge = 0;
        float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
        float tpx = 0, tpy = 0;
        for (const Ent& o : ents)
            if (o.alive && !o.isBuilding && o.player == e.player && o.utype == UnitType::TeslaTrooper
                && distf(o.x, o.y, cx, cy) <= 2.6f) {
                if (e.teslaCharge == 0) { tpx = o.x; tpy = o.y; }
                e.teslaCharge++;
            }
        if (e.teslaCharge > 0 && tick % 10 == (uint64_t)(id % 10)) {
            Effect arc; arc.kind = 2; arc.x = tpx; arc.y = tpy; arc.x2 = cx; arc.y2 = cy; arc.maxAge = 7;
            effects.push_back(arc);
        }
    }
    bool bldCanFire = bd.weapon.damage > 0 && e.player >= 0 && e.drainedBy == INVALID_EID
        && (!players[e.player].lowPower() || (e.btype == BldType::TeslaCoil && e.teslaCharge > 0));
    if (bldCanFire) {
        float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
        auto bldTargetOk = [&](EID tid) {
            if (!valid(tid)) return false;
            const Ent& t = ents[tid];
            bool airT = !t.isBuilding && unitDef(t.utype).isAir() && t.state != UState::Landed;
            if (airT && !bd.weapon.antiAir) return false;
            if (!airT && !bd.weapon.antiGround) return false;
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            return distf(cx, cy, tx, ty) <= bd.weapon.range + 1;
        };
        if (!bldTargetOk(e.target)) {
            e.target = findNearestEnemy(e.player, cx, cy, (float)bd.weapon.range, false, &bd.weapon);
            if (e.target == INVALID_EID) e.target = findNearestEnemy(e.player, cx, cy, (float)bd.weapon.range, true, &bd.weapon);
        }
        if (valid(e.target) && e.atkCd <= 0) {
            // 心灵控制塔：直接心灵控制目标（不发射弹道，与尤里步兵同机制）
            if (e.btype == BldType::PsychicTower) {
                Ent& t = ents[e.target];
                if (!t.isBuilding && !psychicImmune(t.utype) && !t.permaControlled && t.mindBy == INVALID_EID
                    && t.player >= 0 && isEnemy(e.player, t.player)) {
                    mindControlTake(e, id, e.target);
                    Effect ef; ef.kind = 2; ef.x = t.x; ef.y = t.y; ef.x2 = cx; ef.y2 = cy; ef.maxAge = 15;
                    effects.push_back(ef);
                    g_sfx.playAt(Sfx::Tesla, cx, cy);
                }
                e.atkCd = bd.weapon.cooldown;
            } else if (e.btype == BldType::PrismTower) {
                int sup = 0;
                float supPos[8][2];
                for (size_t i = 0; i < ents.size() && sup < 8; i++) {
                    Ent& o = ents[i];
                    if (!o.alive || !o.isBuilding || (int)i == id) continue;
                    if (o.btype != BldType::PrismTower || o.player != e.player) continue;
                    float ox = o.x + bd.w / 2.0f, oy = o.y + bd.h / 2.0f;
                    if (distf(ox, oy, cx, cy) > 8.0f) continue;
                    supPos[sup][0] = ox; supPos[sup][1] = oy; sup++;
                }
                const Ent& t = ents[e.target];
                float tx = t.x, ty = t.y;
                if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
                float mult = weaponMultiplier(bd.weapon, t);
                int dmg = (int)(bd.weapon.damage * mult * (1.0f + 0.75f * sup));
                damage(e.target, dmg, e.player, id);
                // 主光束 + 各支援塔汇聚光束
                Effect ef; ef.kind = 3; ef.x = cx; ef.y = cy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 12;
                effects.push_back(ef);
                for (int k = 0; k < sup; k++) {
                    Effect sb; sb.kind = 3; sb.x = supPos[k][0]; sb.y = supPos[k][1];
                    sb.x2 = cx; sb.y2 = cy; sb.maxAge = 10;
                    effects.push_back(sb);
                }
                g_sfx.playAt(Sfx::Prism, cx, cy);
                e.atkCd = bd.weapon.cooldown;
            } else {
                fireWeapon(e, id, e.target);
                e.atkCd = e.btype == BldType::GatlingCannon
                    ? std::max(3, bd.weapon.cooldown * (e.gatlingStage == 0 ? 3 : (e.gatlingStage == 1 ? 2 : 1)) / 3)
                    : bd.weapon.cooldown;
            }
        }
    }
    // 驻军轮流出击（RA2 原作：进驻步兵从建筑内对外射击，射程+1）
    if (!e.garrison.empty() && e.player >= 0) garrisonFire(e, id);
}

// 驻军火力：每 8 帧一名驻军射击一次（轮换），享受射程与伤害加成
void World::garrisonFire(Ent& b, EID id) {
    if (tick % 8 != (uint64_t)(id % 8)) return;
    const BldDef& bd = bldDef(b.btype);
    float cx = b.x + bd.w / 2.0f, cy = b.y + bd.h / 2.0f;
    int slot = (int)((tick / 8 + id) % b.garrison.size());
    Ent::GarrisonedUnit& gu = b.garrison[slot];
    UnitType gt = gu.type;
    WeaponDef w = unitDef(gt).weapon;
    if (gu.vetRank >= 2 && unitDef(gt).elite) w = *unitDef(gt).elite;
    int rank = std::clamp(gu.vetRank, 0, 2);
    w.damage = (int)(w.damage * g_gameRules.veteranismDmgBonus[rank]);
    w.cooldown = std::max(1, (int)(w.cooldown * g_gameRules.veteranRofBonus[rank]));
    if (w.damage <= 0) return;
    w.range += 1;                          // 驻军射程加成
    w.damage = (int)(w.damage * 1.2f);     // 驻军伤害加成
    EID tgt = findNearestEnemy(b.player, cx, cy, (float)w.range, true, &w, gt);
    if (tgt == INVALID_EID) return;
    const Ent& t = ents[tgt];
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
    Projectile p;
    p.kind = strcmp(w.projSprite, "flak") == 0 ? ProjKind::Flak : ProjKind::Bullet;
    p.player = b.player;
    p.x = cx; p.y = cy; p.tx = tx; p.ty = ty; p.target = tgt; p.src = id; p.srcGarrisonSlot = slot; p.w = w;
    projs.push_back(p);
    Effect mz; mz.kind = 5; mz.x = cx; mz.y = cy; mz.maxAge = 4;
    effects.push_back(mz);
    g_sfx.playAt(Sfx::Shot, cx, cy);
}

