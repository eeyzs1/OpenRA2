#pragma once
#include "game/world.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// Shared private helpers for World subsystem TUs (not part of public API).

inline int maxHpFor(const World::Ent& e, const UnitDef& ud) {
    (void)e;
    return ud.hp;
}
// 弹头倍率后至少 1 点伤害（避免 SmallArms×Concrete=0.8 截成 0）
inline int scaledWeaponDamage(int base, float mult) {
    if (base <= 0 || mult <= 0.0f) return 0;
    return std::max(1, (int)std::lround((double)base * (double)mult));
}
inline float weaponMultiplier(const WeaponDef& w, const World::Ent& target) {
    if(target.nativeTemporalHead!=INVALID_EID && !(w.nativeSource && w.nativeSource->warhead && w.nativeSource->warhead->temporal))return 0;
    if(w.nativeSource && w.nativeSource->warhead && target.nativeIdentity.definition) {
        const auto& wh=*w.nativeSource->warhead;const auto& type=*target.nativeIdentity.definition;
        if((wh.psychicDamage && type.immuneToPsionicWeapons) || (wh.radiation && type.immuneToRadiation))return 0;
    }
    if (target.isBuilding)
        return weaponVsArmor(w,target.nativeIdentity.definition?target.nativeIdentity.definition->armor:Armor::Concrete,false,true);
    const UnitDef& td = unitDef(target);
    return weaponVsArmor(w, td.armor, td.isInfantry(), false);
}
inline int rotStepDir(int cur, int want) {
    int d = (want - cur + 8) & 7;
    if (d == 0) return cur;
    return (cur + (d <= 4 ? 1 : 7)) & 7;
}
inline int rotDistDir(int a, int b) {
    int d = std::abs(a - b) & 7;
    return std::min(d, 8 - d);
}

// ---- YR ProjectileDef → 运行时弹丸 ----
inline int defaultProjSpeed(ProjKind k) {
    switch (k) {
        case ProjKind::Missile: return 25;
        case ProjKind::Bullet:  return 90;
        case ProjKind::Flak:    return 70;
        case ProjKind::Shell:
        default:                return 55;
    }
}
// leptons/帧 → 百分之一格/tick（OpenRA2 30fps ≈ 原作 15fps 墙钟）
inline int leptonsToCentiTiles(int leptons) {
    if (leptons <= 0) return defaultProjSpeed(ProjKind::Shell);
    return std::max(1, (leptons * 100 + 256) / 512);
}
inline int weaponSpeedCenti(const WeaponDef& w, const ProjectileDef& pd) {
    int lep = w.speedLeptons >= 0 ? w.speedLeptons : pd.speedLeptons;
    if (pd.arcing) lep = 50; // ModEnc: Arcing 强制 Speed=50
    return leptonsToCentiTiles(lep);
}
inline ProjKind kindFromProjectile(const ProjectileDef& pd, const char* projSprite) {
    if (pd.arcing) return ProjKind::Shell;
    if (pd.rot > 0) {
        if (projSprite && !strcmp(projSprite,"flak")) return ProjKind::Flak;
        return ProjKind::Missile;
    }
    const char* ps = projSprite ? projSprite : "shell";
    if (!strcmp(ps, "flak")) return ProjKind::Flak;
    if (!strcmp(ps, "missile") || !strcmp(ps, "naval") || !strcmp(ps, "torpedo")) return ProjKind::Missile;
    if (!strcmp(ps, "bullet") || !strcmp(ps, "rad")) return ProjKind::Bullet;
    return ProjKind::Shell;
}
inline float rotAimStep(int rot) {
    if (rot <= 0) return 0.f;
    return std::min(2.0f, rot / 100.0f);
}
inline void initProjectile(Projectile& p) {
    if (p.speed <= 0) p.speed = defaultProjSpeed(p.kind);
}
inline float projMoveSpeed(const Projectile& p) {
    int s = p.speed > 0 ? p.speed : defaultProjSpeed(p.kind);
    return s / 100.0f;
}

// 建筑贴边可站格：脚印外围最近可通行点（工程师/间谍贴边抵达，避免寻路进占地中心失败）
inline bool approachBuildingCell(const World& w, int sx, int sy, const World::Ent& b, int domain,
                                 int& ox, int& oy) {
    if (!b.isBuilding) return false;
    const BldDef& bd = bldDef(b);
    const int bx = (int)b.x, by = (int)b.y;
    auto walkable = [&](int x, int y) {
        if (!w.map.inBounds(x, y) || w.map.cellBlocked(x, y)) return false;
        const Cell& c = w.map.at(x, y);
        if (domain == 1) return c.terrain == Terrain::Water;
        if (domain == 2) return c.passable() || c.terrain == Terrain::Water;
        return c.passable();
    };
    float best = 1e30f;
    bool found = false;
    for (int y = by - 1; y <= by + bd.h; ++y) {
        for (int x = bx - 1; x <= bx + bd.w; ++x) {
            if (x >= bx && x < bx + bd.w && y >= by && y < by + bd.h) continue;
            if (!walkable(x, y)) continue;
            float d = distf((float)sx, (float)sy, (float)x + 0.5f, (float)y + 0.5f);
            if (d < best) { best = d; ox = x; oy = y; found = true; }
        }
    }
    return found;
}

// Retain the corner and exit in a replanned route, returning its first unconsumed waypoint.
inline int prependDriveArcCorner(const World& world, const World::Ent& unit, const NativeDriveArc& curve,
    std::vector<Vec2i>& path) {
    const Vec2i exit{curve.corner.x + curve.outgoing.x, curve.corner.y + curve.outgoing.y};
    const auto sameLevel = [&](Vec2i from, Vec2i to) {
        return world.movementStep(unit, from.x, from.y, curve.layer, to.x, to.y) == curve.layer
            && world.map.surfaceHeight(from.x, from.y, curve.layer) == world.map.surfaceHeight(to.x, to.y, curve.layer);
    };
    if (!curve.wide) {
        if (path.empty() || path.size() >= 4096 || path.front() != exit || !sameLevel(curve.corner, exit)) return -1;
        path.insert(path.begin(), curve.corner);
        return 0;
    }

    const auto incoming = NativeDriveMath::directions[curve.incoming];
    const Vec2i entry{curve.corner.x - incoming.x, curve.corner.y - incoming.y};
    const Vec2i occupied{(int)unit.x, (int)unit.y};
    const int index = curve.progress < NativeDriveMath::wideHandoff ? 0 : 1;
    if (occupied == exit) {
        if (index == 0 || !sameLevel(exit, exit) || world.bldBlocked(exit.x, exit.y, curve.layer)) return -1;
        if (!path.empty() && path.front() == exit) {
            if (path.size() >= 4096) return -1;
            path.insert(path.begin(), curve.corner);
            return index;
        }
        if (path.size() > 4094) return -1;
        if (!path.empty()) {
            const auto next = path.front();
            if (next == curve.corner || next == entry || std::abs(next.x - exit.x) > 1 || std::abs(next.y - exit.y) > 1
                || world.movementStep(unit, exit.x, exit.y, curve.layer, next.x, next.y) < 0) return -1;
        }
        // The pathfinder starts in the exit cell, but the wide arc still has a leg to its center.
        path.insert(path.begin(), {curve.corner, exit});
        return index;
    }

    if ((occupied != entry && occupied != curve.corner) || path.empty()
        || !sameLevel(curve.corner, exit) || world.bldBlocked(exit.x, exit.y, curve.layer)) return -1;
    if (occupied == entry && (index != 0 || !sameLevel(entry, curve.corner)
        || world.bldBlocked(curve.corner.x, curve.corner.y, curve.layer))) return -1;
    if (path.front() == curve.corner) return path.size() > 1 && path[1] == exit ? index : -1;
    if (path.size() >= 4096 || path.front() != exit) return -1;
    path.insert(path.begin(), curve.corner);
    return index;
}

inline bool reversesDriveArc(const World::Ent& unit, const NativeDriveArc& curve, Vec2i next) {
    const auto incoming = NativeDriveMath::directions[curve.incoming];
    const Vec2i entry{curve.corner.x - incoming.x, curve.corner.y - incoming.y};
    if (next == entry) return true;
    if (!curve.wide) return false;
    const Vec2i exit{curve.corner.x + curve.outgoing.x, curve.corner.y + curve.outgoing.y};
    const Vec2i occupied{(int)unit.x, (int)unit.y};
    return (occupied == exit && next == curve.corner)
        || (occupied == entry && next == Vec2i{entry.x - incoming.x, entry.y - incoming.y});
}

inline bool attackPath(const World& world,const World::Ent& unit,const World::Ent& target,std::vector<Vec2i>& path) {
    const auto& def=unitDef(unit);
    path.clear();
    if(def.isAir() || unit.utype==UnitType::Chrono || unit.utype==UnitType::ChronoCommando || unit.utype==UnitType::ChronoIvan) return true;
    float tx=target.x,ty=target.y;
    if(target.isBuilding) { tx+=bldDef(target).w/2.0f;ty+=bldDef(target).h/2.0f; }
    // Engineer capture/repair and spy infiltration also share the Chasing state.
    if((unit.utype==UnitType::Spy || unit.utype==UnitType::Engineer) && target.isBuilding) {
        if(distf(unit.x,unit.y,tx,ty)<std::max(bldDef(target).w,bldDef(target).h)/2.0f+2.5f) return true;
        int x=(int)tx,y=(int)ty;
        return approachBuildingCell(world,(int)unit.x,(int)unit.y,target,def.pathDomain(),x,y)
            && world.map.findPath((int)unit.x,(int)unit.y,x,y,path,20000,def.pathDomain(),nullptr,unit.navLayer,-1,def.fitsUnderBridge(),world.nativeGateAccess(unit));
    }
    world.attackTargetPoint(unit,target,tx,ty);
    float range=world.attackRange(unit,target);
    bool found=world.map.findPathToRange((int)unit.x,(int)unit.y,tx,ty,range,path,20000,def.pathDomain(),nullptr,unit.navLayer,-1,-1,def.fitsUnderBridge(),world.nativeGateAccess(unit));
    // While between tile centers, the current cell's center may itself be the firing point.
    if(found && path.empty() && distf(unit.x,unit.y,tx,ty)>range) path.push_back({(int)unit.x,(int)unit.y});
    return found;
}
