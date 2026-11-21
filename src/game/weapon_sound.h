#pragma once
#include "game/data.h"
#include "game/presentation_events.h"
#include <cstring>

inline Sfx weaponSoundCue(const WeaponDef& w) {
    if (w.report && w.report[0]) {
        if (!strcmp(w.report, "MirageTankAttack")) return Sfx::MirageFire;
        if (!strcmp(w.report, "RhinoTankAttack")) return Sfx::RhinoFire;
        if (!strcmp(w.report, "ApocalypseAttackGround")) return Sfx::ApocFire;
        if (!strcmp(w.report, "GrizzlyTankAttack")) return Sfx::Cannon;
        if (!strcmp(w.report, "GIAttack") || !strcmp(w.report, "ConscriptAttack")) return Sfx::Shot;
    }
    const char* ps = w.projSprite?w.projSprite:"shell";
    if (!strcmp(ps, "tesla") || !strcmp(ps, "chrono")) return Sfx::Tesla;
    if (!strcmp(ps, "prism")) return Sfx::Prism;
    if (!strcmp(ps, "bullet") || !strcmp(ps, "rad") || !strcmp(ps, "psi")) return Sfx::Shot;
    if (!strcmp(ps, "flak")) return Sfx::Flak;
    if (!strcmp(ps, "missile")) return Sfx::Missile;
    if (!strcmp(ps, "naval")) return Sfx::NavalCannon;
    if (!strcmp(ps, "torpedo")) return Sfx::Torpedo;
    return Sfx::Cannon;
}
