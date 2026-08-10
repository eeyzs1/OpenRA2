#include "game/world.h"
#include "game/lang.h"
#include <cmath>
#include <algorithm>

bool World::bldBlocked(int x, int y) const {
    if (!map.inBounds(x, y)) return true;
    return bldOcc[cellIdx(x, y)] > 0;
}

EID World::bldAt(int bx, int by) const {
    if (!map.inBounds(bx, by)) return INVALID_EID;
    int v = bldOcc[cellIdx(bx, by)];
    return v > 0 ? v - 1 : INVALID_EID;
}

EID World::unitAtCell(int x, int y) const {
    for (size_t i = 0; i < ents.size(); i++) {
        const Ent& e = ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (e.parasiting) continue; // 寄生中的机器人附着在宿主上，不占格
        if (unitDef(e.utype).isAir() && e.state != UState::Landed) continue; // 飞行中不占格
        if ((int)e.x == x && (int)e.y == y) return (int)i;
    }
    return INVALID_EID;
}

// 脚印格是否被「其他」地面单位占用（扫全表，避免 unitAtCell 先命中自身而漏掉同格单位）
bool World::groundUnitBlocksCell(int x, int y, EID ignore) const {
    for (size_t i = 0; i < ents.size(); i++) {
        if ((EID)i == ignore) continue;
        const Ent& e = ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (e.parasiting) continue;
        if (unitDef(e.utype).isAir() && e.state != UState::Landed) continue;
        if ((int)e.x == x && (int)e.y == y) return true;
    }
    return false;
}

int World::countInfantryAtCell(int x, int y, EID ignore) const {
    int n = 0;
    for (size_t i = 0; i < ents.size(); i++) {
        if ((EID)i == ignore) continue;
        const Ent& e = ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (e.parasiting) continue;
        if (!unitDef(e.utype).isInfantry()) continue;
        if ((int)e.x == x && (int)e.y == y) n++;
    }
    return n;
}

// RA2 格点占位（非连续物理引擎）：
// - 车/舰：同格至多 1（友军/敌军、静止/移动一律硬挡，避免永久重叠）
// - 步兵：同格至多 3
// - 车可进入有步兵的格（碾压/驶过另判）；步兵不可进入有车/舰的格
bool World::cellHardBlockedForMove(int x, int y, EID mover) const {
    if (!valid(mover) || !ents[mover].alive) return groundUnitBlocksCell(x, y, mover);
    const Ent& self = ents[mover];
    const UnitDef& sud = unitDef(self.utype);
    const bool moverInf = sud.isInfantry();
    constexpr int kInfStack = 3;
    for (size_t i = 0; i < ents.size(); i++) {
        if ((EID)i == mover) continue;
        const Ent& o = ents[i];
        if (!o.alive || o.isBuilding) continue;
        if (o.parasiting) continue;
        const UnitDef& oud = unitDef(o.utype);
        if (oud.isAir() && o.state != UState::Landed) continue;
        if ((int)o.x != x || (int)o.y != y) continue;
        const bool otherInf = oud.isInfantry();
        if (moverInf && otherInf) {
            if (countInfantryAtCell(x, y, mover) < kInfStack) continue;
            return true;
        }
        if (moverInf && !otherInf) return true;   // 步兵 vs 车/舰
        if (!moverInf && otherInf) continue;      // 车/舰 vs 步兵：可进
        return true; // 车/舰 vs 车/舰：硬挡
    }
    return false;
}

bool World::hasBld(int player, BldType t) const {
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == t) return true;
    return false;
}

int World::countUnits(int player, UnitType t) const {
    int n = 0;
    for (const Ent& e : ents)
        if (e.alive && !e.isBuilding && e.player == player && e.utype == t) n++;
    return n;
}

int World::countBlds(int player, BldType t) const {
    int n = 0;
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == t) n++;
    return n;
}

bool World::modeAllowsBuilding(int player, BldType t) const {
    if (player < 0 || player >= numPlayers || t < BldType::ConYard || t >= BldType::COUNT) return false;
    const BldDef& d = bldDef(t);
    if (d.factionMask == 0) return false;
    if (skirmishMode != SkirmishMode::UnholyAlliance
        && !(d.factionMask & (1 << (int)players[player].faction))) return false;
    if (!superweaponsEnabled && bldProvidesSW(t) != SWType::COUNT) return false;
    if (skirmishMode == SkirmishMode::Megawealth && t == BldType::OreRefinery) return false;
    if (skirmishMode == SkirmishMode::MeatGrinder
        && (t == BldType::AirForceCmd || t == BldType::NavalYard || bldProvidesSW(t) != SWType::COUNT))
        return false;
    return true;
}

bool World::modeAllowsUnit(int player, UnitType t) const {
    if (player < 0 || player >= numPlayers || t < UnitType::MCV || t >= UnitType::COUNT) return false;
    const UnitDef& u = unitDef(t);
    if (skirmishMode != SkirmishMode::UnholyAlliance
        && !(u.factionMask & (1 << (int)players[player].faction))) return false;
    if (skirmishMode == SkirmishMode::Megawealth && u.canHarvet()) return false;
    if (skirmishMode == SkirmishMode::MeatGrinder && (u.isAir() || u.isNaval())) return false;
    return true;
}

void World::ensureMegawealthOilDerricks(int perPlayer) {
    int existing = 0;
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.btype == BldType::OilDerrick) existing++;
    const int wanted = numPlayers * std::max(1, perPlayer);
    const BldDef& d = bldDef(BldType::OilDerrick);
    for (int tries = 0; existing < wanted && tries < map.w * map.h * 2; ++tries) {
        int bx = 3 + rng.range(0, std::max(0, map.w - d.w - 7));
        int by = 3 + rng.range(0, std::max(0, map.h - d.h - 7));
        bool ok = true;
        for (int dy = 0; dy < d.h && ok; ++dy)
            for (int dx = 0; dx < d.w; ++dx) {
                int x = bx + dx, y = by + dy;
                if (!map.inBounds(x, y) || !map.at(x, y).passable() || map.at(x, y).ore > 0 || bldBlocked(x, y)) {
                    ok = false;
                    break;
                }
            }
        if (!ok) continue;
        for (const Ent& e : ents) {
            if (e.alive && distf(e.x, e.y, (float)bx, (float)by) < 6.0f) { ok = false; break; }
        }
        if (!ok) continue;
        spawnBuilding(-1, BldType::OilDerrick, bx, by, true);
        existing++;
    }
}

bool World::prereqMet(int player, const BldDef& d) const {
    if (!modeAllowsBuilding(player, d.type)) return false;
    // 国家限制（RA2 原作：如巨炮仅法国可建）；秘密实验室占领后可解锁（见 capture 处理）
    if (d.countryReq != Country::None && players[player].country != d.countryReq
        && players[player].secretLabUnlock != (int)d.countryReq) return false;
    if (skirmishMode == SkirmishMode::Megawealth && d.prereq == BldType::OreRefinery)
        return hasBld(player, BldType::PowerPlant) || hasBld(player, BldType::TeslaReactor)
            || hasBld(player, BldType::BioReactor);
    if (d.prereq == BldType::COUNT) return true;
    // 雷达科技等价：盟军空指部 / 苏中雷达 / 尤里心灵探测器
    if (d.prereq == BldType::Radar)
        return hasBld(player, BldType::Radar) || hasBld(player, BldType::AirForceCmd)
            || hasBld(player, BldType::PsychicSensor);
    return hasBld(player, d.prereq);
}

bool World::unitPrereqMet(int player, const UnitDef& u) const {
    if (!modeAllowsUnit(player, u.type)) return false;
    // 国家限制（RA2 原作：如狙击手仅英国、磁能坦克仅苏俄）；秘密实验室解锁亦放行
    // 融合：共和国之辉中国可造黑鹰（忽略韩国国别锁）
    if (u.countryReq != Country::None && players[player].country != u.countryReq
        && players[player].secretLabUnlock != (int)u.countryReq) {
        if (!(u.type == UnitType::BlackEagle && players[player].faction == Faction::China))
            return false;
    }
    // 韩国：黑鹰替代入侵者（Harrier）；中国融合保留入侵者+黑鹰
    if (u.type == UnitType::Intruder && players[player].country == Country::Korea) return false;
    // 偷科技单位（RA2 原作：间谍渗透敌作战实验室后解锁，见 applySpyEffect）
    int stBit = stolenTechBit(u.type);
    if (stBit && !(players[player].stolenTech & stBit)) return false;
    if (u.prereq == BldType::COUNT) return true;
    if (u.prereq == BldType::Radar)
        return hasBld(player, BldType::Radar) || hasBld(player, BldType::AirForceCmd)
            || hasBld(player, BldType::PsychicSensor);
    return hasBld(player, u.prereq);
}

bool World::hasFactoryFor(int player, const UnitDef& u) const {
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.player == player && isFactoryFor(e.btype, u)) return true;
    return false;
}

EID World::findNearestEnemy(int player, float x, float y, float maxR, bool includeBlds, const WeaponDef* w, UnitType seeker) {
    EID best = INVALID_EID;
    float bd = maxR;
    for (size_t i = 0; i < ents.size(); i++) {
        const Ent& e = ents[i];
        if (!e.alive || e.player < 0 || !isEnemy(player, e.player)) continue;
        if (e.parasiting && e.utype == UnitType::TerrorDrone) continue; // 寄生中的机器人不可被索敌（乌贼缠绕仍可被攻击摘除）
        if (e.isBuilding && !includeBlds) continue;
        if (!e.isBuilding) {
            if (e.camouflaged && e.utype != UnitType::Spy) continue; // 幻影伪装：无法被自动索敌
            // 间谍（含伪装）：除军犬外无法被自动索敌（RA2 原作：军犬嗅探）
            if (e.utype == UnitType::Spy && seeker != UnitType::AttackDog) continue;
            // 台风/雷鸣潜艇下潜隐身：仅反潜探测单位在 7 格内、或任何单位贴脸（2.5 格）可发现
            if ((e.utype == UnitType::Typhoon || e.utype == UnitType::Boomer) && e.subReveal <= 0) {
                float sd = distf(x, y, e.x, e.y);
                if (!(isDetector(seeker) && sd <= 7.0f) && sd > 2.5f) continue;
            }
            // 心灵控制者索敌：跳过免疫目标与已被控制单位（RA2 原作）
            if ((seeker == UnitType::Yuri || seeker == UnitType::PsiCommando || seeker == UnitType::YuriPrime)
                && (psychicImmune(e.utype) || e.permaControlled || e.mindBy != INVALID_EID)) continue;
        }
        // 尤里无法控制建筑（尤里首脑可控；心灵突击队有 C4 可炸建筑，不在此过滤）
        if (e.isBuilding && seeker == UnitType::Yuri) continue;
        // 武器射界过滤：空中目标需 antiAir，地面目标需 antiGround
        if (w) {
            bool airT = !e.isBuilding && unitDef(e.utype).isAir() && e.state != UState::Landed;
            if (airT && !w->antiAir) continue;
            if (!airT && !w->antiGround) continue;
            if (w->navalOnly) {
                // 鱼雷类：仅水上目标（舰船或水上建筑）
                bool onWater = e.isBuilding
                    ? map.at((int)e.x + bldDef(e.btype).w / 2, (int)e.y + bldDef(e.btype).h / 2).terrain == Terrain::Water
                    : map.at((int)e.x, (int)e.y).terrain == Terrain::Water;
                if (!onWater) continue;
            }
        }
        float ex = e.x, ey = e.y;
        if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
        float d = distf(x, y, ex, ey);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

// 反潜探测单位（驱逐舰/神盾舰/海蝎/海豚，RA2 原作为驱逐舰声呐/海豚）
bool World::isDetector(UnitType t) const {
    return t == UnitType::Destroyer || t == UnitType::Aegis || t == UnitType::SeaScorpion || t == UnitType::Dolphin;
}

// 单位可见性：潜艇隐身时仅本家/探测单位/贴脸可见
bool World::visibleTo(const Ent& e, int viewer) const {
    if (e.parasiting) return e.player == viewer || e.utype == UnitType::Squid; // 寄生机器人仅本家可见；乌贼缠绕可见（可被攻击摘除）
    if (e.player == viewer || viewer < 0) return true;
    if (e.isBuilding) return true;
    if ((e.utype == UnitType::Typhoon || e.utype == UnitType::Boomer) && e.subReveal <= 0) {
        for (const Ent& o : ents) {
            if (!o.alive || o.isBuilding || o.player != viewer) continue;
            if (isDetector(o.utype) && distf(o.x, o.y, e.x, e.y) <= 7.0f) return true;
            if (distf(o.x, o.y, e.x, e.y) <= 2.5f) return true;
        }
        return false;
    }
    return true;
}

// IFV 载兵武器（RA2 原作：多功能步兵车武器随乘员改变；工程师=维修，见 updateUnit）
static const WeaponDef& ifvWeapon(UnitType cargo) {
    static const WeaponDef wGun{25, 6, 16, false, true, "bullet", 1.2f, 0.8f, 0.5f};   // 默认机枪（GI/动员兵等）
    static const WeaponDef wSnp{60, 9, 60, false, true, "bullet", 1.0f, 0.05f, 0.05f}; // 狙击手
    static const WeaponDef wTsl{30, 6, 36, false, true, "tesla", 1.2f, 1.0f, 0.8f};    // 磁暴步兵
    static const WeaponDef wFlk{14, 7, 20, true, true, "flak", 1.0f, 0.7f, 0.5f};      // 高射炮兵
    static const WeaponDef wRad{50, 6, 30, false, true, "rad", 2.5f, 0.3f, 0.1f};      // 辐射工兵
    static const WeaponDef wMis{40, 7, 28, false, true, "missile", 0.3f, 1.6f, 0.5f};  // 重装大兵
    static const WeaponDef wTny{80, 7, 12, false, true, "bullet", 1.6f, 0.5f, 1.1f};   // 谭雅/海豹
    static const WeaponDef wChr{1, 7, 20, false, true, "chrono", 1.0f, 1.0f, 0.0f};    // 超时空军团兵
    static const WeaponDef wNoneW{0, 0, 999, false, false, "shell", 1, 1, 1};          // 工程师/间谍：无武器（工程师走维修）
    static const WeaponDef wPsi{0, 7, 40, false, true, "psychic", 1.0f, 0.0f, 0.0f};  // 尤里：心灵
    static const WeaponDef wAA{20, 7, 18, true, true, "missile", 0.8f, 1.0f, 0.8f};   // 军犬：空导弹（YR）
    static const WeaponDef wBomb{1, 1, 20, false, true, "shell", 1.0f, 1.0f, 1.0f};   // 伊文/恐怖分子：贴脸炸弹由战斗逻辑处理
    static const WeaponDef wMelee{40, 1, 25, false, true, "shell", 2.0f, 0.4f, 0.3f}; // 狂兽人近战
    switch (cargo) {
        case UnitType::Sniper: return wSnp;
        case UnitType::TeslaTrooper: return wTsl;
        case UnitType::FlakTrooper: return wFlk;
        case UnitType::Desolator: return wRad;
        case UnitType::GuardianGI: return wMis;
        case UnitType::Tanya: case UnitType::NavySEAL: return wTny;
        case UnitType::Chrono: case UnitType::ChronoCommando: case UnitType::ChronoIvan: return wChr;
        case UnitType::Engineer: case UnitType::Spy: return wNoneW;
        case UnitType::Yuri: case UnitType::PsiCommando: case UnitType::YuriPrime: return wPsi;
        case UnitType::AttackDog: return wAA;
        case UnitType::CrazyIvan: case UnitType::Terrorist: return wBomb;
        case UnitType::Brute: return wMelee;
        case UnitType::Initiate: return wGun; // 文档：新兵在 IFV 内为机枪
        default: return wGun;
    }
}

// 有效武器：部署形态 / IFV 载兵 / 精英军衔 综合
WeaponDef World::effWeapon(const Ent& e) const {
    if (e.isBuilding) return bldDef(e.btype).weapon;
    const UnitDef& ud = unitDef(e.utype);
    WeaponDef w = ud.weapon;
    if (e.deployed && e.utype == UnitType::GuardianGI) w = ggiDeployedWeapon();
    else if (e.deployed && e.utype == UnitType::GI) w = giDeployedWeapon();
    // 攻城直升机部署后：远程重炮（不可移动，对建筑/车辆强力，溅射）
    else if (e.deployed && e.utype == UnitType::SiegeChopper) w = siegeChopperDeployedWeapon();
    else if (e.utype == UnitType::IFV && !e.cargo.empty()) w = ifvWeapon(e.cargo[0].type);
    else if (e.vetRank >= 2 && ud.elite) w = *ud.elite;
    if (e.vetRank > 0) {
        int rank = std::clamp(e.vetRank, 0, 2);
        w.damage = (int)(w.damage * g_gameRules.veteranismDmgBonus[rank]);
        w.cooldown = std::max(1, (int)(w.cooldown * g_gameRules.veteranRofBonus[rank]));
    }
    if (e.crateDmgBoost > 0) w.damage = (int)(w.damage * 1.25f);
    if (e.utype == UnitType::GatlingTank && e.gatlingStage > 0) {
        w.cooldown = std::max(3, w.cooldown * (e.gatlingStage == 1 ? 2 : 1) / 3);
        w.damage = w.damage * (e.gatlingStage == 1 ? 5 : 7) / 4;
    }
    return w;
}

// ===================== 指令 =====================

