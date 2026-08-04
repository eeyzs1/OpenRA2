#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void World::fireWeapon(Ent& e, EID id, EID targetId) {
    if (!valid(targetId)) return;
    (void)id;
    const Ent& t = ents[targetId];
    // 航空母舰不直接开火（舰载机空袭，见 updateUnit Attacking 分支）
    if (!e.isBuilding && e.utype == UnitType::AircraftCarrier) return;
    WeaponDef w = e.isBuilding ? bldDef(e.btype).weapon : effWeapon(e); // 含部署/IFV载兵/精英军衔
    // 防空/对地射界：非防空单位不得伤害飞行中的空军
    {
        bool airT = !t.isBuilding && unitDef(t.utype).isAir() && t.state != UState::Landed;
        if (airT && !w.antiAir) return;
        if (!airT && !w.antiGround) return;
    }
    if ((!e.isBuilding && e.utype == UnitType::GatlingTank)
        || (e.isBuilding && e.btype == BldType::GatlingCannon)) {
        e.gatlingHeat = std::min(180, e.gatlingHeat + 14);
        e.gatlingStage = e.gatlingHeat >= 120 ? 2 : (e.gatlingHeat >= 50 ? 1 : 0);
        if (e.isBuilding) {
            w.cooldown = std::max(3, w.cooldown * (e.gatlingStage == 0 ? 3 : (e.gatlingStage == 1 ? 2 : 1)) / 3);
            w.damage = w.damage * (e.gatlingStage == 0 ? 4 : (e.gatlingStage == 1 ? 5 : 7)) / 4;
        }
    }
    // 磁暴线圈充电加成：伤害 +50%
    if (e.isBuilding && e.btype == BldType::TeslaCoil && e.teslaCharge > 0)
        w.damage = (int)(w.damage * 1.5f);
    // 步兵开火动画（art.ini FireUp 序列）：播放期间渲染开火帧
    if (!e.isBuilding) {
        const UnitAnimInfo& fai = g_sprites.animInfo(e.utype);
        if (fai.fire > 0) e.fireAnim = fai.fire * 2; // 每相位 2 tick
    }
    float sx = e.x, sy = e.y;
    if (e.isBuilding) { sx += bldDef(e.btype).w / 2.0f; sy += bldDef(e.btype).h / 2.0f; }
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }

    // 疯狂伊文：攻击 = 在目标上安放定时炸弹（5 秒后爆炸，RA2 原作设定）
    if (!e.isBuilding && (e.utype == UnitType::CrazyIvan || e.utype == UnitType::ChronoIvan)) {
        TimedBomb b;
        b.x = tx; b.y = ty; b.timer = 30 * 5; b.player = e.player; b.attachedTo = targetId;
        timedBombs.push_back(b);
        Effect mz;
        mz.kind = 5; mz.x = tx; mz.y = ty; mz.maxAge = 6;
        effects.push_back(mz);
        g_sfx.playAt(Sfx::Click, tx, ty);
        return;
    }
    // 心灵波：控制效果在攻击状态机（mindControlTake）结算；对无效目标（建筑等）不发弹
    if (!e.isBuilding && unitDef(e.utype).isPsychic()) return;
    // 台风/雷鸣潜艇开火后暴露 3 秒（可被反潜单位索敌）
    if (!e.isBuilding && (e.utype == UnitType::Typhoon || e.utype == UnitType::Boomer)) e.subReveal = 90;
    const char* ps = w.projSprite;
    // 幻影坦克开火解除伪装
    if (!e.isBuilding && e.utype == UnitType::MirageTank) { e.camouflaged = false; e.camoTick = 0; }
    // 开火音效（Report 优先，否则按弹道类型）
    static auto sfxFromWeapon = [](const WeaponDef& w) -> Sfx {
        if (w.report && w.report[0]) {
            if (!strcmp(w.report, "MirageTankAttack")) return Sfx::MirageFire;
            if (!strcmp(w.report, "RhinoTankAttack")) return Sfx::RhinoFire;
            if (!strcmp(w.report, "ApocalypseAttackGround")) return Sfx::ApocFire;
            if (!strcmp(w.report, "GrizzlyTankAttack")) return Sfx::Cannon;
            if (!strcmp(w.report, "GIAttack") || !strcmp(w.report, "ConscriptAttack")) return Sfx::Shot;
        }
        const char* ps = w.projSprite;
        if (!strcmp(ps, "tesla") || !strcmp(ps, "chrono")) return Sfx::Tesla;
        if (!strcmp(ps, "prism")) return Sfx::Prism;
        if (!strcmp(ps, "bullet") || !strcmp(ps, "rad") || !strcmp(ps, "psi")) return Sfx::Shot;
        if (!strcmp(ps, "flak")) return Sfx::Flak;
        if (!strcmp(ps, "missile")) return Sfx::Missile;
        if (!strcmp(ps, "naval")) return Sfx::NavalCannon;
        if (!strcmp(ps, "torpedo")) return Sfx::Torpedo;
        return Sfx::Cannon;
    };
    g_sfx.playAt(sfxFromWeapon(w), sx, sy);
    // 开火口焰特效
    if (strcmp(ps, "tesla") != 0 && strcmp(ps, "prism") != 0) {
        Effect mz;
        mz.kind = 5; mz.x = sx; mz.y = sy; mz.maxAge = 4;
        effects.push_back(mz);
    }
    if (strcmp(ps, "tesla") == 0) {
        // 磁暴：瞬时电弧
        float mult = weaponMultiplier(w, t);
        damage(targetId, (int)(w.damage * mult), e.player, id);
        Effect ef;
        ef.kind = 2; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 8;
        effects.push_back(ef);
    } else if (strcmp(ps, "chrono") == 0) {
        // 超时空抹除枪：不造成伤害，叠加目标相位进度（冻结→抹除）
        Ent& te = ents[targetId];
        te.chrono += 20;
        Effect ef;
        ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 10;
        effects.push_back(ef);
    } else if (strcmp(ps, "prism") == 0) {
        float mult = weaponMultiplier(w, t);
        damage(targetId, (int)(w.damage * mult), e.player, id);
        Effect ef;
        ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 10;
        effects.push_back(ef);
        // YR 光棱坦克标志性折射：主目标附近最多两名敌人承受递减链伤害。
        if (!e.isBuilding && e.utype == UnitType::PrismTank) {
            int chained = 0;
            for (size_t i = 0; i < ents.size() && chained < 2; ++i) {
                if ((EID)i == targetId || !ents[i].alive || ents[i].player < 0
                    || !isEnemy(e.player, ents[i].player)) continue;
                Ent& o = ents[i];
                float ox = o.x, oy = o.y;
                if (o.isBuilding) { ox += bldDef(o.btype).w / 2.0f; oy += bldDef(o.btype).h / 2.0f; }
                if (distf(tx, ty, ox, oy) > 2.5f) continue;
                float chainScale = chained == 0 ? 0.5f : 0.25f;
                damage((EID)i, (int)(w.damage * weaponMultiplier(w, o) * chainScale), e.player, id);
                Effect ce; ce.kind = 3; ce.x = tx; ce.y = ty; ce.x2 = ox; ce.y2 = oy; ce.maxAge = 8;
                effects.push_back(ce);
                ++chained;
            }
        }
    } else if (strcmp(ps, "bullet") == 0 || strcmp(ps, "rad") == 0) {
        Projectile p;
        p.kind = ProjKind::Bullet; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    } else if (strcmp(ps, "flak") == 0) {
        Projectile p;
        p.kind = ProjKind::Flak; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    } else if (strcmp(ps, "missile") == 0) {
        Projectile p;
        p.kind = ProjKind::Missile; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        if (w.splash >= 1.0f) p.hp = 45; // 大型导弹（V3/无畏舰）：可被防空火力拦截（RA2 原作）
        projs.push_back(p);
    } else if (strcmp(ps, "naval") == 0 || strcmp(ps, "torpedo") == 0) {
        Projectile p;
        p.kind = ProjKind::Shell; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    } else { // shell
        Projectile p;
        p.kind = ProjKind::Shell; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    }
}

void World::damage(EID id, int dmg, int byPlayer, EID byEnt, int byGarrisonSlot) {
    if (!valid(id) || dmg <= 0) return;
    Ent& e = ents[id];
    if (e.invuln > 0) return; // 铁幕无敌
    if (!e.isBuilding && e.vetRank > 0)
        dmg = std::max(1, (int)(dmg * g_gameRules.veteranArmorBonus[std::clamp(e.vetRank, 0, 2)]));
    if (!e.isBuilding && e.crateArmorBoost > 0)
        dmg = std::max(1, (int)(dmg * 0.67f));
    e.hp -= dmg;
    // 民房重伤：驻军撤出继续战斗，房子留下燃烧破损
    if (e.isBuilding && e.btype == BldType::CivHouse && !e.garrison.empty()) {
        const int maxHp = bldDef(e.btype).hp;
        if (e.hp > 0 && e.hp * 2 <= maxHp)
            evacuateGarrison(id);
    }
    // 中立单位/建筑（player=-1）：无玩家状态，仅扣血与摧毁，跳过 EVA 与反击
    if (e.player < 0) {
        if (e.hp <= 0) { creditKill(byEnt, id, byGarrisonSlot); kill(id); }
        return;
    }
    Player& owner = players[e.player];
    // EVA 遇袭播报（节流，避免刷屏）
    if (byPlayer >= 0 && byPlayer != e.player) {
        if (e.isBuilding && owner.evaBaseCd <= 0) {
            eva(e.player, TR(S::EvaBaseAttack));
            owner.evaBaseCd = 480; // 16 秒
        } else if (!e.isBuilding && unitDef(e.utype).canHarvet() && owner.evaMinerCd <= 0) {
            eva(e.player, TR(S::EvaHarvAttack));
            owner.evaMinerCd = 480;
        }
    }
    if (e.hp <= 0) {
        if (e.isBuilding) eva(e.player, TextFormat(TR(S::EvaBldDestroyedFmt), bldName(e.btype)));
        else if (!unitDef(e.utype).isInfantry() && owner.evaUnitCd <= 0) {
            eva(e.player, TR(S::EvaUnitLost));
            owner.evaUnitCd = 150; // 5 秒
        }
        creditKill(byEnt, id, byGarrisonSlot);
        kill(id);
        return;
    }
    // 被打的单位反击
    if (!e.isBuilding && e.state == UState::Idle && byPlayer >= 0) {
        const UnitDef& ud = unitDef(e.utype);
        EID attacker = findNearestEnemy(e.player, e.x, e.y, 20, true, &ud.weapon);
        if (attacker != INVALID_EID) { e.target = attacker; e.state = UState::Chasing; }
    }
}

// RA2/YR VeteranRatio：按被摧毁对象价值累计，每级默认需要 3× 自身价值。
void World::creditKill(EID byEnt, EID victim, int garrisonSlot) {
    if (!valid(byEnt)) return;
    Ent& a = ents[byEnt];
    if (!valid(victim)) return;
    const Ent& v = ents[victim];
    int value = v.isBuilding ? bldDef(v.btype).cost : unitDef(v.utype).cost;
    if (value <= 0) return;
    auto promote = [&](UnitType type, int& kills, int& xp, int& rank) {
        ++kills;
        xp += value;
        int ownValue = std::max(1, unitDef(type).cost);
        while (rank < 2 && xp >= (int)std::ceil(ownValue * g_gameRules.veteranRatio * (rank + 1))) {
            ++rank;
            if (a.player == 0) {
                eva(0, TextFormat(TR(rank == 1 ? S::EvaPromoteVetFmt : S::EvaPromoteEliteFmt), unitName(type)));
                g_sfx.play(Sfx::Ready, 0.6f);
            }
        }
    };
    if (a.isBuilding) {
        if (garrisonSlot < 0 || garrisonSlot >= (int)a.garrison.size()) return;
        Ent::GarrisonedUnit& gu = a.garrison[garrisonSlot];
        promote(gu.type, gu.kills, gu.veterancyValue, gu.vetRank);
    } else {
        promote(a.utype, a.kills, a.veterancyValue, a.vetRank);
    }
}

void World::explodeAt(float x, float y, int big) {
    Effect ef;
    ef.kind = big >= 2 ? 4 : 0;
    ef.x = x; ef.y = y;
    ef.maxAge = big >= 2 ? 30 : 18;
    effects.push_back(ef);
    g_sfx.playAt(big >= 2 ? Sfx::BigExplosion : Sfx::Explosion, x, y);
}

