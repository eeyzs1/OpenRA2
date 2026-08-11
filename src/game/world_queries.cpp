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

static bool unitShouldOccupy(const World::Ent& e) {
    if (!e.alive || e.isBuilding || e.parasiting) return false;
    if (unitDef(e.utype).isAir() && e.state != UState::Landed) return false;
    return true;
}

static bool unitShouldAirBucket(const World::Ent& e) {
    if (!e.alive || e.isBuilding || e.parasiting) return false;
    return unitDef(e.utype).isAir() && e.state != UState::Landed;
}

void World::rebuildUnitOcc() {
    unitOccHead.assign((size_t)map.w * map.h, INVALID_EID);
    for (size_t i = 0; i < ents.size(); i++) {
        ents[i].occCell = -1;
        ents[i].occNext = INVALID_EID;
    }
    for (size_t i = 0; i < ents.size(); i++)
        unitOccSync((EID)i);
}

void World::unitOccSync(EID id) {
    if (id < 0 || id >= (EID)ents.size()) return;
    Ent& e = ents[id];
    int want = -1;
    if (unitShouldOccupy(e) && map.inBounds((int)e.x, (int)e.y))
        want = cellIdx((int)e.x, (int)e.y);
    if (e.occCell == want) return;

    if (e.occCell >= 0 && e.occCell < (int)unitOccHead.size()) {
        EID* link = &unitOccHead[(size_t)e.occCell];
        while (*link != INVALID_EID) {
            if (*link == id) {
                *link = e.occNext;
                break;
            }
            if (*link < 0 || *link >= (EID)ents.size()) break;
            link = &ents[*link].occNext;
        }
        e.occCell = -1;
        e.occNext = INVALID_EID;
    }

    if (want >= 0) {
        e.occNext = unitOccHead[(size_t)want];
        unitOccHead[(size_t)want] = id;
        e.occCell = want;
    }
}

void World::rebuildAirOcc() {
    airBucketW = (map.w + AIR_BUCKET - 1) / AIR_BUCKET;
    const int bh = (map.h + AIR_BUCKET - 1) / AIR_BUCKET;
    airOccHead.assign((size_t)airBucketW * (size_t)bh, INVALID_EID);
    for (size_t i = 0; i < ents.size(); i++) {
        ents[i].airBucket = -1;
        ents[i].airNext = INVALID_EID;
    }
    for (size_t i = 0; i < ents.size(); i++)
        airOccSync((EID)i);
}

void World::airOccSync(EID id) {
    if (id < 0 || id >= (EID)ents.size()) return;
    Ent& e = ents[id];
    int want = -1;
    if (unitShouldAirBucket(e) && map.inBounds((int)e.x, (int)e.y) && airBucketW > 0) {
        const int bx = (int)e.x / AIR_BUCKET;
        const int by = (int)e.y / AIR_BUCKET;
        want = by * airBucketW + bx;
    }
    if (e.airBucket == want) return;

    if (e.airBucket >= 0 && e.airBucket < (int)airOccHead.size()) {
        EID* link = &airOccHead[(size_t)e.airBucket];
        while (*link != INVALID_EID) {
            if (*link == id) {
                *link = e.airNext;
                break;
            }
            if (*link < 0 || *link >= (EID)ents.size()) break;
            link = &ents[*link].airNext;
        }
        e.airBucket = -1;
        e.airNext = INVALID_EID;
    }

    if (want >= 0 && want < (int)airOccHead.size()) {
        e.airNext = airOccHead[(size_t)want];
        airOccHead[(size_t)want] = id;
        e.airBucket = want;
    }
}

EID World::unitAtCell(int x, int y) const {
    if (!map.inBounds(x, y) || unitOccHead.empty()) return INVALID_EID;
    return unitOccHead[(size_t)cellIdx(x, y)];
}

// 脚印格是否被「其他」地面单位占用（格链表，O(同格人数)）
bool World::groundUnitBlocksCell(int x, int y, EID ignore) const {
    if (!map.inBounds(x, y) || unitOccHead.empty()) return false;
    for (EID id = unitOccHead[(size_t)cellIdx(x, y)]; id != INVALID_EID; id = ents[id].occNext) {
        if (id == ignore) continue;
        return true;
    }
    return false;
}

int World::countInfantryAtCell(int x, int y, EID ignore) const {
    if (!map.inBounds(x, y) || unitOccHead.empty()) return 0;
    int n = 0;
    for (EID id = unitOccHead[(size_t)cellIdx(x, y)]; id != INVALID_EID; id = ents[id].occNext) {
        if (id == ignore) continue;
        if (unitDef(ents[id].utype).isInfantry()) n++;
    }
    return n;
}

// RA2 格点占位（非连续物理引擎）：
// - 车/舰：同格至多 1（友军/敌军、静止/移动一律硬挡，避免永久重叠）
// - 步兵：同格至多 3
// - 车可进入有步兵的格（碾压/驶过另判）；步兵不可进入有车/舰的格
bool World::cellHardBlockedForMove(int x, int y, EID mover) const {
    if (!valid(mover) || !ents[mover].alive) return groundUnitBlocksCell(x, y, mover);
    if (!map.inBounds(x, y) || unitOccHead.empty()) return false;
    const Ent& self = ents[mover];
    const bool moverInf = unitDef(self.utype).isInfantry();
    constexpr int kInfStack = 3;
    int otherInf = 0;
    bool hasVehicle = false;
    for (EID id = unitOccHead[(size_t)cellIdx(x, y)]; id != INVALID_EID; id = ents[id].occNext) {
        if (id == mover) continue;
        if (unitDef(ents[id].utype).isInfantry()) otherInf++;
        else hasVehicle = true;
    }
    if (moverInf) {
        if (hasVehicle) return true;
        return otherInf >= kInfStack;
    }
    return hasVehicle; // 车/舰：仅被其他车/舰硬挡
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

bool World::campaignAllowsBuilding(BldType t) const {
    if (!campaignTechGate || campaignAllowedBlds.empty()) return true;
    for (BldType a : campaignAllowedBlds) if (a == t) return true;
    return false;
}

bool World::campaignAllowsUnit(UnitType t) const {
    if (!campaignTechGate || campaignAllowedUnits.empty()) return true;
    for (UnitType a : campaignAllowedUnits) if (a == t) return true;
    return false;
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
    if (!campaignAllowsBuilding(d.type)) return false;
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
    if (!campaignAllowsUnit(u.type)) return false;
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
    if (maxR <= 0.0f) return INVALID_EID;

    auto consider = [&](EID id) {
        if (id < 0 || id >= (EID)ents.size()) return;
        const Ent& e = ents[id];
        if (!e.alive || e.player < 0 || !isEnemy(player, e.player)) return;
        if (e.parasiting && e.utype == UnitType::TerrorDrone) return; // 寄生中的机器人不可被索敌（乌贼缠绕仍可被攻击摘除）
        if (e.isBuilding && !includeBlds) return;
        if (!e.isBuilding) {
            if (e.camouflaged && e.utype != UnitType::Spy) return; // 幻影伪装：无法被自动索敌
            // 间谍（含伪装）：除军犬外无法被自动索敌（RA2 原作：军犬嗅探）
            if (e.utype == UnitType::Spy && seeker != UnitType::AttackDog) return;
            // 台风/雷鸣潜艇下潜隐身：仅反潜探测单位在 7 格内、或任何单位贴脸（2.5 格）可发现
            if ((e.utype == UnitType::Typhoon || e.utype == UnitType::Boomer) && e.subReveal <= 0) {
                float sd = distf(x, y, e.x, e.y);
                if (!(isDetector(seeker) && sd <= 7.0f) && sd > 2.5f) return;
            }
            // 心灵控制者索敌：跳过免疫目标与已被控制单位（RA2 原作）
            if ((seeker == UnitType::Yuri || seeker == UnitType::PsiCommando || seeker == UnitType::YuriPrime)
                && (psychicImmune(e.utype) || e.permaControlled || e.mindBy != INVALID_EID)) return;
        }
        // 尤里无法控制建筑（尤里首脑可控；心灵突击队有 C4 可炸建筑，不在此过滤）
        if (e.isBuilding && seeker == UnitType::Yuri) return;
        // 武器射界过滤：空中目标需 antiAir，地面目标需 antiGround
        if (w) {
            bool airT = !e.isBuilding && unitDef(e.utype).isAir() && e.state != UState::Landed;
            if (airT && !w->antiAir) return;
            if (!airT && !w->antiGround) return;
            if (w->navalOnly) {
                // 鱼雷类：仅水上目标（舰船或水上建筑）
                bool onWater = e.isBuilding
                    ? map.at((int)e.x + bldDef(e.btype).w / 2, (int)e.y + bldDef(e.btype).h / 2).terrain == Terrain::Water
                    : map.at((int)e.x, (int)e.y).terrain == Terrain::Water;
                if (!onWater) return;
            }
        }
        float ex = e.x, ey = e.y;
        if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
        float d = distf(x, y, ex, ey);
        // 最近距离；平局取更小 EID（扫描顺序无关、确定性）
        if (d < bd || (d == bd && (best == INVALID_EID || id < best))) {
            bd = d;
            best = id;
        }
    };

    const int cx = (int)x, cy = (int)y;
    const int cellR = (int)std::ceil(maxR) + 1;
    const bool needGround = !w || w->antiGround;
    const bool needAir = !w || w->antiAir;

    // 地面单位：格占位环扫
    if (needGround && !unitOccHead.empty()) {
        for (int dy = -cellR; dy <= cellR; dy++) {
            for (int dx = -cellR; dx <= cellR; dx++) {
                const int gx = cx + dx, gy = cy + dy;
                if (!map.inBounds(gx, gy)) continue;
                for (EID id = unitOccHead[(size_t)cellIdx(gx, gy)]; id != INVALID_EID; id = ents[id].occNext)
                    consider(id);
            }
        }
    }

    // 建筑：脚印格；仅在左上角格计入，避免重复
    if (includeBlds && needGround && !bldOcc.empty()) {
        for (int dy = -cellR; dy <= cellR; dy++) {
            for (int dx = -cellR; dx <= cellR; dx++) {
                const int gx = cx + dx, gy = cy + dy;
                if (!map.inBounds(gx, gy)) continue;
                const int v = bldOcc[(size_t)cellIdx(gx, gy)];
                if (v <= 0) continue;
                const EID id = v - 1;
                if (id < 0 || id >= (EID)ents.size()) continue;
                const Ent& b = ents[id];
                if ((int)b.x != gx || (int)b.y != gy) continue;
                consider(id);
            }
        }
    }

    // 飞行单位：4×4 粗桶
    if (needAir && airBucketW > 0 && !airOccHead.empty()) {
        const int bh = (map.h + AIR_BUCKET - 1) / AIR_BUCKET;
        const int minBx = std::max(0, (cx - cellR) / AIR_BUCKET);
        const int maxBx = std::min(airBucketW - 1, (cx + cellR) / AIR_BUCKET);
        const int minBy = std::max(0, (cy - cellR) / AIR_BUCKET);
        const int maxBy = std::min(bh - 1, (cy + cellR) / AIR_BUCKET);
        for (int by = minBy; by <= maxBy; by++) {
            for (int bx = minBx; bx <= maxBx; bx++) {
                const int bi = by * airBucketW + bx;
                if (bi < 0 || bi >= (int)airOccHead.size()) continue;
                for (EID id = airOccHead[(size_t)bi]; id != INVALID_EID; id = ents[id].airNext)
                    consider(id);
            }
        }
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

