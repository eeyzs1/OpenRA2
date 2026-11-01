#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void World::fireWeapon(Ent& e, EID id, EID targetId, bool continuation) {
    if(!continuation && e.burstRemaining>0)return;
    if (!e.alive || !valid(targetId)) return;
    (void)id;
    const Ent& t = ents[targetId];
    // 航空母舰不直接开火（舰载机空袭，见 updateUnit Attacking 分支）
    if (!e.isBuilding && e.utype == UnitType::AircraftCarrier) return;
    WeaponDef w = e.isBuilding ? bldDef(e.btype).weapon : effWeapon(e,&t); // 含目标、部署/IFV载兵/精英军衔
    // 防空/对地射界：非防空单位不得伤害飞行中的空军
    {
        bool airT = t.airborne();
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
    // 步兵开火动画（art.ini FireUp）：按 firerate 推进相位
    if (!e.isBuilding) {
        e.fireAnim = g_gameRules.fireAnimationTicks;
    }
    float sx = e.x, sy = e.y;
    if (e.isBuilding) { sx += bldDef(e.btype).w / 2.0f; sy += bldDef(e.btype).h / 2.0f; }
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }

    if(!e.isBuilding && e.utype==UnitType::Magnetron) {
        // Both state transitions and sustained attacks use the same weapon
        // dispatch; the lifting beam must never emit a placeholder shell.
        const int owner=e.player;
        if(!t.isBuilding) {
            const auto& definition=unitDef(t.utype);
            if(definition.isInfantry() || definition.isAir()) return;
            ents[targetId].magneticBy=id;
            ents[targetId].magneticHeight=std::max(1,ents[targetId].magneticHeight);
            unitOccSync(targetId);airOccSync(targetId);
        }
        emitSoundAt(Sfx::Tesla,sx,sy,id);
        if(t.isBuilding) {
            const int amount=(int)(w.damage*weaponMultiplier(w,t));
            damage(targetId,amount,owner,id);
        }
        // Existing aux2 stores the beam endpoint's lift-height snapshot.
        // Historical magnetic beams have aux2=0 and retain their old anchor.
        Effect beam;beam.kind=3;beam.aux=1;beam.aux2=t.magneticHeight;beam.x=sx;beam.y=sy;beam.x2=tx;beam.y2=ty;beam.maxAge=12;
        effects.push_back(beam);
        return;
    }
    // 疯狂伊文：攻击 = 在目标上安放定时炸弹（5 秒后爆炸，RA2 原作设定）
    if (!e.isBuilding && (e.utype == UnitType::CrazyIvan || e.utype == UnitType::ChronoIvan)) {
        TimedBomb b;
        b.x = tx; b.y = ty; b.timer = 30 * 5; b.player = e.player; b.attachedTo = targetId;
        timedBombs.push_back(b);
        Effect mz;
        mz.kind = 5; mz.x = tx; mz.y = ty; mz.maxAge = 6;
        effects.push_back(mz);
        emitSoundAt(Sfx::Click, tx, ty);
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
    emitSoundAt(sfxFromWeapon(w), sx, sy, id);
    // 开火口焰特效
    if (strcmp(ps, "tesla") != 0 && strcmp(ps, "prism") != 0 && strcmp(ps, "chrono") != 0) {
        PresentationEvent event;
        event.kind=PresentationKind::Muzzle;event.tick=tick;event.sequence=presentationSequence++;
        event.entity=id;event.player=e.player;event.x=sx;event.y=sy;event.targetX=tx;event.targetY=ty;
        captureVisualTarget(event,e);
        if(presentationEvents.size()>=4096) presentationEvents.pop_front();
        presentationEvents.push_back(event);
    }
    const ProjectileDef& pd = projectileForWeapon(w);

    if (strcmp(ps, "tesla") == 0) {
        float mult = weaponMultiplier(w, t);
        damage(targetId, (int)(w.damage * mult), e.player, id);
        Effect ef;
        ef.kind = 2; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 8;
        effects.push_back(ef);
        return;
    }
    if (strcmp(ps, "chrono") == 0) {
        Ent& te = ents[targetId];
        te.chrono += 20;
        Effect ef;
        ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 10;
        effects.push_back(ef);
        return;
    }
    if (strcmp(ps, "prism") == 0) {
        float mult = weaponMultiplier(w, t);
        damage(targetId, (int)(w.damage * mult), e.player, id);
        Effect ef;
        ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 10;
        effects.push_back(ef);
        if (e.alive && !e.isBuilding && e.utype == UnitType::PrismTank) {
            int chained = 0;
            for (size_t i = 0; e.alive && i < ents.size() && chained < 2; ++i) {
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
        return;
    }

    // 心灵弹道：瞬时命中 + 光束表现；其它 Inviso 走 emitWeaponProjectile
    if (pd.inviso && (!strcmp(ps, "psi") || !strcmp(pd.name, "Psychic"))) {
        emitWeaponProjectile(e.player, sx, sy, targetId, id, -1, w);
        Effect ef; ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 6;
        effects.push_back(ef);
        return;
    }
    if(!continuation && !e.isBuilding && !unitDef(e.utype).isAir() && unitDef(e.utype).ammo==0 && w.burst>1) {
        e.burstRemaining=w.burst-1;e.burstDelay=rng.range(w.burstDelayMin,w.burstDelayMax);
        e.burstTarget=targetId;e.burstOwner=e.player;e.burstAir=t.airborne();
    }
    emitWeaponProjectile(e.player, sx, sy, targetId, id, -1, w);
}

void World::emitWeaponProjectile(int player, float sx, float sy, EID tgt, EID src, int srcGarrisonSlot, const WeaponDef& weapon) {
    WeaponDef w=weapon;
    if(w.cellSpread<0 && w.warhead!=WeaponDef::Warhead::Legacy) {
        const auto& area=g_gameRules.warheadArea[(int)w.warhead];
        w.cellSpread=area.cellSpread;w.percentAtMax=area.percentAtMax;
    }
    if (!valid(tgt)) return;
    const Ent& t = ents[tgt];
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
    const ProjectileDef& pd = projectileForWeapon(w);
    const char* ps = w.projSprite ? w.projSprite : "shell";

    // YR Inviso：无飞行体，瞬时结算（InvisibleLow / Psychic 等）
    if(pd.inviso && w.cellSpread>=0) {
        const bool gas=valid(src) && !ents[src].isBuilding && ents[src].utype==UnitType::ChaosDrone;
        applyCellSpreadDamage(w,player,tx,ty,src,srcGarrisonSlot,gas);return;
    }
    if (pd.inviso) {
        float mult = weaponMultiplier(w, t);
        damage(tgt, (int)(w.damage * mult), player, src, srcGarrisonSlot);
        return;
    }

    // 飞行弹丸：Arcing 瞄落点；ROT>0 制导；Inaccurate 散布
    float aimX = tx, aimY = ty;
    if (pd.inaccurate) {
        float scatter = 0.35f + distf(sx, sy, tx, ty) * 0.06f;
        aimX += (rng.unit() - 0.5f) * 2.f * scatter;
        aimY += (rng.unit() - 0.5f) * 2.f * scatter;
    }
    Projectile p;
    p.kind = kindFromProjectile(pd, ps);
    p.player = player;
    p.x = sx; p.y = sy; p.ox = sx; p.oy = sy;
    p.tx = aimX; p.ty = aimY;
    p.target = tgt; p.src = src; p.srcGarrisonSlot = srcGarrisonSlot; p.w = w;
    p.rot = pd.rot;
    p.arcing = pd.arcing;
    p.proximity = pd.proximity;
    p.speed = weaponSpeedCenti(w, pd);
    if (pd.vertical) p.z = 0.8f;
    if (w.splash >= 1.0f && p.kind == ProjKind::Missile) p.hp = 45; // V3/无畏可拦截
    initProjectile(p);
    projs.push_back(p);
}

void World::damage(EID id, int dmg, int byPlayer, EID byEnt, int byGarrisonSlot) {
    if (!valid(id) || dmg <= 0) return;
    Ent& e = ents[id];
    if (e.invuln > 0) return; // 铁幕无敌
    if (!e.isBuilding && e.vetRank > 0)
        dmg = std::max(1, (int)(dmg * g_gameRules.veteranArmorBonus[std::clamp(e.vetRank, 0, 2)]));
    if (!e.isBuilding && e.crateArmorBoost > 0)
        dmg = std::max(1, (int)(dmg * 0.67f));
    if (byPlayer>=0 && byPlayer<numPlayers && isEnemy(byPlayer,e.player))
        combatDamage[byPlayer] += std::min(std::max(0,e.hp),dmg);
    PresentationEvent impact;
    impact.kind=PresentationKind::Impact;impact.tick=tick;impact.sequence=presentationSequence++;
    impact.entity=id;impact.player=e.player;impact.x=e.x;impact.y=e.y;
    impact.damage=std::min(std::max(0,e.hp),dmg);impact.lethal=dmg>=e.hp;
    if(e.isBuilding) { impact.x+=bldDef(e.btype).w/2.0f;impact.y+=bldDef(e.btype).h/2.0f; }
    captureVisualTarget(impact,e);
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(impact);
    e.hp -= dmg;
    // 民房重伤：驻军撤出继续战斗，房子留下燃烧破损
    if (e.isBuilding && e.btype == BldType::CivHouse && !e.garrison.empty()) {
        const int maxHp = bldDef(e.btype).hp;
        if (e.hp > 0 && e.hp * 2 <= maxHp)
            evacuateGarrison(id);
    }
    Ent& current=ents[id]; // evacuation may have reallocated ents
    // 中立单位/建筑（player=-1）：无玩家状态，仅扣血与摧毁，跳过 EVA 与反击
    if (current.player < 0) {
        if (current.hp <= 0) { creditKill(byEnt, id, byGarrisonSlot); kill(id); }
        return;
    }
    Player& owner = players[current.player];
    // EVA 遇袭播报（节流，避免刷屏）
    if (byPlayer >= 0 && byPlayer != current.player) {
        if (current.isBuilding && owner.evaBaseCd <= 0) {
            eva(current.player, TR(S::EvaBaseAttack));
            owner.evaBaseCd = 480; // 16 秒
        } else if (!current.isBuilding && unitDef(current.utype).canHarvet() && owner.evaMinerCd <= 0) {
            eva(current.player, TR(S::EvaHarvAttack));
            owner.evaMinerCd = 480;
        }
    }
    if (current.hp <= 0) {
        if (current.isBuilding) eva(current.player, formatText(TR(S::EvaBldDestroyedFmt), bldName(current.btype)));
        else if (!unitDef(current.utype).isInfantry() && owner.evaUnitCd <= 0) {
            eva(current.player, TR(S::EvaUnitLost));
            owner.evaUnitCd = 150; // 5 秒
        }
        creditKill(byEnt, id, byGarrisonSlot);
        kill(id);
        return;
    }
    // 被打的单位反击
    if (!current.isBuilding && current.state == UState::Idle && byPlayer >= 0) {
        const UnitDef& ud = unitDef(current.utype);
        EID attacker = findNearestEnemy(current.player, current.x, current.y, 20, true, &ud.weapon);
        if (attacker != INVALID_EID) { current.target = attacker; current.state = UState::Chasing; }
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
                eva(0, formatText(TR(rank == 1 ? S::EvaPromoteVetFmt : S::EvaPromoteEliteFmt), unitName(type)));
                emitSound(Sfx::Ready, 0.6f);
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
    emitSoundAt(big >= 2 ? Sfx::BigExplosion : Sfx::Explosion, x, y);
}


void World::updateWeaponBurst(EID id) {
    auto& e=ents[id];if(e.burstRemaining<=0)return;
    if(e.isBuilding || e.player!=e.burstOwner || !valid(e.burstTarget) || e.target!=e.burstTarget
        || (e.state!=UState::Attacking && e.state!=UState::Chasing && e.state!=UState::AttackMoving)) {
        e.cancelBurst();return;
    }
    const auto& t=ents[e.burstTarget];
    if(!isEnemy(e.player,t.player) || t.airborne()!=e.burstAir || attackRejection(id,e.burstTarget)!=AttackRejectReason::None) {
        // Immobilization pauses, rather than consumes or restarts, the pending delay.
        if(e.immobilized())return;
        e.cancelBurst();return;
    }
    if(e.immobilized())return;
    const auto w=effWeapon(e,&t);float tx=t.x,ty=t.y;
    if(t.isBuilding){tx+=bldDef(t.btype).w/2.f;ty+=bldDef(t.btype).h/2.f;}
    if(distf(e.x,e.y,tx,ty)>w.range){e.cancelBurst();return;}
    if(e.burstDelay>0)--e.burstDelay;
    if(e.burstDelay>0)return;
    const int face=unitHasTurret(e.utype)?e.turretDir:e.dir;
    if(rotDistDir(face,dirFromVec(tx-e.x,ty-e.y))>1)return;
    const EID target=e.burstTarget;
    --e.burstRemaining;
    if(e.burstRemaining)e.burstDelay=rng.range(w.burstDelayMin,w.burstDelayMax);
    else e.cancelBurst();
    e.atkCd=std::max(2,e.atkCd); // No new volley in the same tick as its last shot.
    fireWeapon(e,id,target,true);
}
