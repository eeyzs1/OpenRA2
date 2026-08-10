#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void World::tryUnstackIdle(Ent& e, EID id) {
    if (!e.alive || e.isBuilding || e.state != UState::Idle) return;
    const UnitDef& ud = unitDef(e.utype);
    if (ud.isAir() && e.state != UState::Landed) return;
    const int cx = (int)e.x, cy = (int)e.y;
    bool need = false;
    if (ud.isInfantry()) {
        need = countInfantryAtCell(cx, cy, id) >= 3; // 已有 ≥3 名其他步兵 → 超叠
    } else {
        for (size_t i = 0; i < ents.size(); i++) {
            if ((EID)i == id) continue;
            const Ent& o = ents[i];
            if (!o.alive || o.isBuilding || o.parasiting) continue;
            const UnitDef& oud = unitDef(o.utype);
            if (oud.isAir() && o.state != UState::Landed) continue;
            if (oud.isInfantry()) continue;
            if ((int)o.x == cx && (int)o.y == cy) { need = true; break; }
        }
    }
    if (!need) return;
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int start = (int)((uint32_t)id + tick) & 7;
    for (int k = 0; k < 8; k++) {
        const int d = (start + k) & 7;
        const int nx = cx + DX[d], ny = cy + DY[d];
        if (!passableStep(cx, cy, nx, ny, ud.pathDomain()) || bldBlocked(nx, ny)) continue;
        if (cellHardBlockedForMove(nx, ny, id)) continue;
        e.x = nx + 0.5f;
        e.y = ny + 0.5f;
        return;
    }
}

void World::moveAlongPath(Ent& e, EID id) {
    if (e.pathIdx >= (int)e.path.size()) return;
    // 乌贼缠绕：宿主舰船被定身无法移动（RA2 原作签名机制）
    if (e.parasite != INVALID_EID && valid(e.parasite) && ents[e.parasite].utype == UnitType::Squid) return;
    const UnitDef& ud = unitDef(e.utype);
    Vec2i next = e.path[e.pathIdx];

    auto waitOrRepath = [&](EID blocker) -> bool {
        // returns true if caller should abort this tick (always, after wait/repath)
        if (blocker != INVALID_EID && blocker != id) {
            Ent& o = ents[blocker];
            const UnitDef& od = unitDef(o.utype);
            // 同阵营挡路：尽量轻推到旁格，疏通车流
            if (o.player == e.player && o.state == UState::Idle && rng.chance(0.35f)) {
                int dx = rng.range(-1, 1), dy = rng.range(-1, 1);
                int nx = (int)o.x + dx, ny = (int)o.y + dy;
                if ((dx || dy) && passableStep((int)o.x, (int)o.y, nx, ny, od.pathDomain()) && !bldBlocked(nx, ny)
                    && !cellHardBlockedForMove(nx, ny, blocker)) {
                    o.x = nx + 0.5f; o.y = ny + 0.5f;
                }
            }
        }
        if (++e.blockTick > 45) { // 堵约 1.5 秒：重寻路
            e.blockTick = 0;
            int gx = e.path.back().x, gy = e.path.back().y;
            // 终点被敌军硬挡才放弃；友军占用仍尝试靠近
            if (cellHardBlockedForMove(gx, gy, id)) {
                EID gOcc = unitAtCell(gx, gy);
                if (gOcc != INVALID_EID && ents[gOcc].player != e.player) {
                    e.path.clear();
                    return true;
                }
            }
            std::vector<Vec2i> path;
            if (map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain())) {
                e.path = std::move(path); e.pathIdx = 0;
            } else e.path.clear();
        }
        return true;
    };

    // 目标格占用：车互斥硬挡；步兵可叠≤3；碾压优先清格
    EID occ = unitAtCell(next.x, next.y);
    if (occ != INVALID_EID && occ != id) {
        Ent& o = ents[occ];
        const UnitDef& od = unitDef(o.utype);
        // 坦克碾压（RA2 原作）：重甲车辆直接碾死敌方无甲步兵
        if (o.player != e.player && ud.move == MoveType::Vehicle && ud.armor == Armor::Heavy
            && od.isInfantry() && od.armor == Armor::None && o.invuln == 0 && !o.deployed) {
            g_sfx.playAt(Sfx::Crush, o.x, o.y);
            Player& vp = players[o.player];
            if (vp.evaUnitCd <= 0) { eva(o.player, TR(S::EvaUnitLost)); vp.evaUnitCd = 150; }
            kill(occ);
            // 格子已空，继续走正常移动流程
        } else if (cellHardBlockedForMove(next.x, next.y, id)) {
            waitOrRepath(occ);
            return;
        }
    }
    if (bldBlocked(next.x, next.y)) {
        // 建筑硬挡：重新寻路绕开
        std::vector<Vec2i> path;
        int gx = e.path.back().x, gy = e.path.back().y;
        if (map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain())) { e.path = std::move(path); e.pathIdx = 0; }
        else e.path.clear();
        return;
    }
    // Speed = 每逻辑帧移动 1/Speed 格（越大越慢）；连续滑动 + 渲染插值，避免「停一停再瞬移一格」
    int moveDelay = std::max(1, (int)std::ceil(ud.speed / g_gameRules.veteranSpeedBonus[std::clamp(e.vetRank, 0, 2)]));
    if (e.crateSpeedBoost > 0) moveDelay = std::max(1, (int)(moveDelay * 0.8f));
    float step = 1.0f / (float)moveDelay;
    float tx = next.x + 0.5f, ty = next.y + 0.5f;
    float dx = tx - e.x, dy = ty - e.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 1e-5f) {
        e.dir = dirFromVec(dx, dy);
        // 有炮塔：追击/攻击/攻击移动时炮塔由状态机对准目标；其余移动炮塔跟车头
        bool turAim = e.state == UState::Attacking || e.state == UState::Chasing
            || (e.state == UState::AttackMoving && e.target != INVALID_EID);
        if (!unitHasTurret(e.utype) || !turAim)
            e.turretDir = e.dir;
    }
    // 行走动画相位与移动同步（RA2 原作步态）
    if (ud.isInfantry() || e.utype == UnitType::TerrorDrone) {
        const UnitAnimInfo& ai = g_sprites.animInfo(e.utype);
        e.moveTick++;
        if (ai.walk > 0) e.walkFrame = (e.moveTick * ai.walk / moveDelay) % ai.walk;
        else if (++e.walkAnim % 4 == 0) e.walkFrame ^= 1;
    } else {
        e.moveTick++;
    }
    // 落格前再检一次：防止两车同时滑入同格
    if (dist <= step + 1e-4f) {
        if (cellHardBlockedForMove(next.x, next.y, id) || bldBlocked(next.x, next.y)) {
            waitOrRepath(unitAtCell(next.x, next.y));
            return;
        }
        e.blockTick = 0;
        e.x = tx; e.y = ty;
        e.pathIdx++;
        e.moveTick = 0;
        if (e.camouflaged && e.utype == UnitType::MirageTank) {
            e.camouflaged = false; e.camoTick = 0; // 幻影移动解除伪装（间谍伪装不因移动解除）
        }
        pickupCrates(e); // 驶入补给箱：拾取
    } else {
        e.blockTick = 0;
        e.x += (dx / dist) * step;
        e.y += (dy / dist) * step;
    }
}

