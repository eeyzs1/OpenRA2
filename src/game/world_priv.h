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
    if (target.isBuilding)
        return weaponVsArmor(w, Armor::Concrete, false, true);
    const UnitDef& td = unitDef(target.utype);
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
        if (pd.aa && !pd.ag) return ProjKind::Flak;
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
    const BldDef& bd = bldDef(b.btype);
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
