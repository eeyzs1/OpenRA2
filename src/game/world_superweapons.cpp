#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>

bool World::swAvailable(int player, SWType t) const {
    if ((unsigned)t>=(unsigned)SWType::COUNT || !superweaponsEnabled || player < 0 || player >= numPlayers || players[player].defeated) return false;
    return hasBld(player, swDef(t).fromBld);
}

SuperweaponRejectReason World::superweaponRejection(int player,int type,float x,float y) const {
    if(type<0 || type>=(int)SWType::COUNT) return SuperweaponRejectReason::InvalidType;
    if(!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=map.w || y>=map.h) return SuperweaponRejectReason::InvalidTarget;
    if(!swAvailable(player,(SWType)type)) return SuperweaponRejectReason::Unavailable;
    if(!players[player].swReady[type]) return SuperweaponRejectReason::NotReady;
    return SuperweaponRejectReason::None;
}
bool World::chronoSourceEligible(int player,EID id) const {
    if(!valid(id)) return false;
    const auto& e=ents[id];if(e.isBuilding || e.player!=player || e.immobilized() || e.parasiting || e.radDeployed) return false;
    const auto& u=unitDef(e.utype);return !u.isAir() && !u.isInfantry();
}

bool World::launchSW(int player, SWType t, float tx, float ty) {
    if(superweaponRejection(player,(int)t,tx,ty)!=SuperweaponRejectReason::None) return false;
    if(t==SWType::ChronoShift) {
        std::vector<EID> group;
        for(const auto& sphere:ents) if(sphere.alive && sphere.isBuilding && sphere.player==player && sphere.btype==BldType::ChronoSphere) {
            for(EID id=0;id<(EID)ents.size();++id) if(chronoSourceEligible(player,id)
                && distf(ents[id].x,ents[id].y,sphere.x+1.5f,sphere.y+1.0f)<=8.0f) group.push_back(id);
            break;
        }
        return launchChronoShift(player,group,tx,ty);
    }
    Player& p = players[player];
    p.swReady[(int)t] = false;
    p.swCharge[(int)t] = 0;
    switch (t) {
        case SWType::Nuke: {
            Nuke n;
            n.active = true; n.player = player;
            n.tx = tx; n.ty = ty;
            n.timer = 75; // 2.5 秒落地
            nukes.push_back(n);
            evaAll(TR(S::EvaNukeLaunched));
            emitSound(Sfx::NukeLaunch, 0.9f);
            break;
        }
        case SWType::Lightning: {
            p.stormTimer = 30 * 14; // 持续 14 秒
            p.stormX = tx; p.stormY = ty;
            p.stormBoltCd = 0;
            evaAll(TR(S::EvaStormComing));
            emitSoundAt(Sfx::Storm, tx, ty);
            break;
        }
        case SWType::IronCurtain: {
            // 目标点 3 格内己方单位/建筑无敌（官方 ~750@15fps → 1500@30fps）；步兵无法承受铁幕能量直接死亡
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& e = ents[i];
                if (!e.alive) continue;
                float ex = e.x, ey = e.y;
                if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
                if (distf(ex, ey, tx, ty) > 3.0f) continue;
                if (!e.isBuilding && unitDef(e.utype).isInfantry()) { kill((int)i); continue; }
                if (e.player == player) e.invuln = 1500;
            }
            emitSoundAt(Sfx::IronCurtain, tx, ty);
            // 铁幕扩散特效
            Effect ef;
            ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 40;
            effects.push_back(ef);
            break;
        }
        case SWType::GeneticMutator: {
            // 基因突变：范围内可变异步兵→己方狂兽人；英雄/犬/奴隶杀死不变异；跳过非步兵
            std::vector<std::pair<float, float>> positions;
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& e = ents[i];
                if (!e.alive || e.isBuilding) continue;
                if (!unitDef(e.utype).isInfantry()) continue;
                if (distf(e.x, e.y, tx, ty) > 5.0f) continue;
                UnitType ut = e.utype;
                bool killOnly = isHero(ut) || ut == UnitType::AttackDog || ut == UnitType::Slave
                             || ut == UnitType::Yuri || ut == UnitType::YuriPrime
                             || ut == UnitType::PsiCommando || ut == UnitType::ChronoIvan;
                float px = e.x, py = e.y;
                kill((int)i);
                if (!killOnly) positions.push_back({px, py});
            }
            for (auto& pos : positions)
                spawnUnit(player, UnitType::Brute, pos.first, pos.second);
            evaAll(formatText(TR(S::EvaMindGain)));
            emitSoundAt(Sfx::Tesla, tx, ty);
            Effect ef; ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 40;
            effects.push_back(ef);
            break;
        }
        case SWType::PsychicDominator: {
            // 心灵控制仪：目标点 5 格内所有敌方单位永久心灵控制 + 范围伤害
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& e = ents[i];
                if (!e.alive) continue;
                float ex = e.x, ey = e.y;
                if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
                if (distf(ex, ey, tx, ty) > 5.0f) continue;
                if (e.player != player && e.player >= 0 && isEnemy(player, e.player)
                    && !e.isBuilding && !psychicImmune(e.utype) && !e.permaControlled) {
                    if (e.mindBy != INVALID_EID && valid(e.mindBy)) {
                        Ent& controller = ents[e.mindBy];
                        if (controller.mindTarget == (EID)i) controller.mindTarget = INVALID_EID;
                        controller.mindTargets.erase(std::remove(controller.mindTargets.begin(), controller.mindTargets.end(), (EID)i),
                                                     controller.mindTargets.end());
                    }
                    e.player = player;
                    e.mindBy = INVALID_EID;
                    e.origPlayer = -1;
                    e.permaControlled = true;
                    e.state = UState::Idle; e.target = INVALID_EID; e.clearPath();
                }
                if (e.isBuilding) damage((int)i, 200, player);
            }
            evaAll(formatText(TR(S::EvaMindGain)));
            emitSoundAt(Sfx::Tesla, tx, ty);
            Effect ef; ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 50;
            effects.push_back(ef);
            break;
        }
        case SWType::ForceShield: {
            // YR Force Shield：半径 4 内友方建筑无敌（500@15fps → 1000@30fps），随后整基断电（1000@15 → 2000@30）
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& e = ents[i];
                if (!e.alive || !e.isBuilding || e.player < 0) continue;
                if (!isAllied(player, e.player)) continue;
                float ex = e.x + bldDef(e.btype).w / 2.0f;
                float ey = e.y + bldDef(e.btype).h / 2.0f;
                if (distf(ex, ey, tx, ty) > 4.0f) continue;
                e.invuln = 1000; // 覆盖已有铁幕保护
            }
            players[player].powerSabotage = 2000;
            emitSoundAt(Sfx::IronCurtain, tx, ty);
            Effect ef; ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 45;
            effects.push_back(ef);
            break;
        }
        default: break;
    }
    g_script.onSuperWeapon(player, t, tx, ty);
    return true;
}

bool World::launchChronoShift(int player,const std::vector<EID>& selected,float tx,float ty,int* transferred) {
    if(transferred) *transferred=0;
    if(superweaponRejection(player,(int)SWType::ChronoShift,tx,ty)!=SuperweaponRejectReason::None) return false;
    std::vector<EID> eligible;
    for(EID id:selected) if(chronoSourceEligible(player,id)) eligible.push_back(id);
    std::sort(eligible.begin(),eligible.end());eligible.erase(std::unique(eligible.begin(),eligible.end()),eligible.end());
    if(eligible.empty()) return false;
    const int moved=chronoShiftUnits(eligible,tx,ty);
    if(!moved) return false;
    if(transferred) *transferred=moved;
    Player& p=players[player];p.swReady[(int)SWType::ChronoShift]=false;p.swCharge[(int)SWType::ChronoShift]=0;
    eva(player,TR(S::EvaChronoStart));g_script.onSuperWeapon(player,SWType::ChronoShift,tx,ty);
    return true;
}

void World::updateSW() {
    if (!superweaponsEnabled) return;
    // 充能
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        for (int i = 0; i < (int)SWType::COUNT; i++) {
            if (p.swReady[i]) continue;
            if (!swAvailable(pi, (SWType)i)) { p.swCharge[i] = 0; continue; } // 建筑被毁清空
            if (p.lowPower()) continue; // 低电暂停充能
            if (++p.swCharge[i] >= swDef((SWType)i).chargeTime) {
                p.swReady[i] = true;
                eva(pi, formatText(TR(S::EvaSWReadyFmt), swName((SWType)i)));
                if (pi == 0) emitSound(Sfx::SWReady, 0.85f);
            }
        }
    }

    // 核弹飞行与爆炸
    for (auto& n : nukes) {
        if (!n.active) continue;
        if (--n.timer > 0) continue;
        n.active = false;
        // 大范围伤害：中心 1000，半径 6 格递减
        const float R = 6.0f;
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            float d = distf(ex, ey, n.tx, n.ty);
            if (d > R) continue;
            int dmg = (int)(1000 * (1.0f - d / (R + 1.0f)));
            // 核爆辐射尘：半径内步兵直接致死（RA2 原作设定）
            if (!e.isBuilding && unitDef(e.utype).isInfantry()) dmg = 10000;
            // 铁幕单位免疫
            if (e.invuln > 0) continue;
            damage((int)i, dmg, n.player);
        }
        // 蘑菇云 + 冲击波 + 余波小爆
        Effect ef;
        ef.kind = 6; ef.x = n.tx; ef.y = n.ty; ef.maxAge = 110;
        effects.push_back(ef);
        for (int k = 0; k < 8; k++) {
            float a = k * 0.785f;
            Effect ex2;
            ex2.kind = 0;
            ex2.x = n.tx + cosf(a) * (2.0f + k % 3);
            ex2.y = n.ty + sinf(a) * (2.0f + k % 3);
            ex2.maxAge = 18;
            effects.push_back(ex2);
        }
        emitSoundAt(Sfx::NukeBlast, n.tx, n.ty);
    }
    nukes.erase(std::remove_if(nukes.begin(), nukes.end(), [](const Nuke& n) { return !n.active; }), nukes.end());

    // 闪电风暴：周期性随机落雷
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (p.stormTimer <= 0) continue;
        p.stormTimer--;
        if (--p.stormBoltCd > 0) continue;
        p.stormBoltCd = 11; // 每 11 帧一道雷
        float a = rng.unit() * 6.2832f;
        float r = sqrtf(rng.unit()) * 5.5f;
        float bx = p.stormX + cosf(a) * r;
        float by = p.stormY + sinf(a) * r * 0.7f;
        // 落雷伤害（半径 1.6，对地）
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive) continue;
            if (e.airborne()) continue; // 打不到空中
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            if (distf(ex, ey, bx, by) > 1.6f) continue;
            if (e.invuln > 0) continue;
            damage((int)i, 130, pi);
        }
        // 天降闪电特效（从天上到地面）
        Effect ef;
        ef.kind = 7; ef.x = bx; ef.y = by; ef.maxAge = 9;
        effects.push_back(ef);
        explodeAt(bx, by, 0);
        emitSoundAt(Sfx::Lightning, bx, by);
    }
}

// ===================== 更新 =====================

int World::chronoShiftUnits(const std::vector<EID>& sel, float tx, float ty) {
    if(!std::isfinite(tx) || !std::isfinite(ty) || tx<0 || ty<0 || tx>=map.w || ty>=map.h) return 0;
    bool anyInf = false;int moved=0;
    for (EID id : sel) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        if (e.isBuilding) continue;
        const UnitDef& ud = unitDef(e.utype);
        if (ud.isAir()) continue;
        if (ud.isInfantry()) { anyInf = true; kill(id); continue; } // 原作设定：传送步兵即死
        if(chronoJump(id, tx, ty)) ++moved;
    }
    if (anyInf) eva(0, TR(S::EvaInfNoChrono));
    if(!moved && !anyInf) return 0;
    emitSoundAt(Sfx::IronCurtain, tx, ty);
    Effect ef;
    ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 40;
    effects.push_back(ef);
    return moved;
}

// ===================== P8 联机：命令执行与校验和 =====================

