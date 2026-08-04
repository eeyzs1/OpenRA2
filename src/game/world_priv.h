#pragma once
#include "game/world.h"
#include <cmath>
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
