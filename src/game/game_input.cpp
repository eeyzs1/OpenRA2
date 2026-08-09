#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include "rlgl.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <unordered_set>

EID Game::pickUnit(int mx, int my) const {
    // 屏幕坐标 → 未缩放视口坐标（与 unitScreenRect / rlScalef 一致）
    float vx = (float)mx / camZoom, vy = (float)my / camZoom;
    EID best = INVALID_EID;
    float bestDist = 1e30f;
    float bestArea = 1e30f;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (e.player != localPlayer && world.map.fogAt(localPlayer, (int)e.x, (int)e.y) != FOG_VISIBLE) continue;
        Rectangle r = unitScreenRect(e);
        // 轻度放宽命中；过大 padding 会让步兵抢掉基地车等大贴图中心点击
        Rectangle rs{r.x * camZoom, r.y * camZoom, r.width * camZoom, r.height * camZoom};
        rs.x -= 8.0f; rs.y -= 8.0f; rs.width += 16.0f; rs.height += 16.0f;
        Rectangle rb = r;
        rb.x -= 8.0f; rb.y -= 8.0f; rb.width += 16.0f; rb.height += 16.0f;
        Vector2 p = unitScreenPos(e);
        const UnitDef& ud = unitDef(e.utype);
        if (ud.isAir() && e.state != UState::Landed) p.y -= AIR_ALT;
        bool inBox = (vx >= rb.x && vx < rb.x + rb.width && vy >= rb.y && vy < rb.y + rb.height)
                  || ((float)mx >= rs.x && (float)mx < rs.x + rs.width
                      && (float)my >= rs.y && (float)my < rs.y + rs.height);
        float footR = ud.isInfantry() ? 48.0f : 72.0f;
        float dFoot = distf(p.x, p.y, vx, vy);
        // 贴图中心（身子）也易点中
        float cx = r.x + r.width * 0.5f, cy = r.y + r.height * 0.5f;
        float dBody = distf(cx, cy, vx, vy);
        if (!inBox && dFoot > footR && dBody > footR) continue;
        // 框内命中优先，但仍按距脚/身排序，避免重叠时乱抢
        float d = std::min(dFoot, dBody);
        if (inBox) d *= 0.35f;
        float area = std::max(64.0f, r.width * r.height);
        if (d < bestDist - 0.5f || (fabsf(d - bestDist) <= 0.5f && area < bestArea)) {
            bestDist = d;
            bestArea = area;
            best = (int)i;
        }
    }
    if (best != INVALID_EID) return best;

    // 贴图命中失败：瓦片邻格己方单位（与 unitScreenPos 北尖约定一致）
    float wx, wy;
    screenToWorld(mx, my, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    bestDist = 1e30f;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
        int ex = (int)floorf(e.x), ey = (int)floorf(e.y);
        int dx = ex - tx, dy = ey - ty;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) continue;
        float d = distf((float)ex + 0.5f, (float)ey + 0.5f, wx, wy);
        float score = d + (dx == 0 && dy == 0 ? 0.f : 10.f);
        if (score < bestDist) {
            bestDist = score;
            best = (int)i;
        }
    }
    if (best != INVALID_EID) return best;

    // 最终回退：屏幕空间最近己方单位（阈值收紧，避免点空地误选远处单位）
    const float kMaxScreenDist = 56.0f;
    bestDist = kMaxScreenDist;
    best = INVALID_EID;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
        Vector2 p = unitScreenPos(e);
        float d = distf(p.x, p.y, vx, vy);
        if (d < bestDist) {
            bestDist = d;
            best = (int)i;
        }
    }
    return best;
}

EID Game::pickBuilding(int mx, int my) const {
    float vx = (float)mx / camZoom, vy = (float)my / camZoom;
    float wx, wy;
    screenToWorld(mx, my, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);

    EID best = INVALID_EID;
    float bestScore = 1e30f;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || !e.isBuilding) continue;
        if (e.player != localPlayer && world.map.fogAt(localPlayer, (int)e.x, (int)e.y) != FOG_VISIBLE) continue;
        const BldDef& d = bldDef(e.btype);
        int bx = (int)e.x, by = (int)e.y;
        bool footHit = (tx >= bx && tx < bx + d.w && ty >= by && ty < by + d.h);

        // 占领后用阵营色；中立科技保留原色（cid=-2 跳过 remap）
        int cid;
        if (e.player >= 0)
            cid = world.players[e.player].colorId;
        else if (e.btype == BldType::OilDerrick || e.btype == BldType::Hospital
                 || e.btype == BldType::TechAirport)
            cid = -2;
        else
            cid = -1;
        Vector2 p = bldScreenPos(e);
        const Sprite& s = g_sprites.building(e.btype, cid, false);
        float x0 = p.x - s.ox + 6, y0 = p.y - s.oy + 4;
        float x1 = x0 + (float)(s.tex.width - 14), y1 = y0 + (float)(s.tex.height - 10);
        bool sprHit = (vx >= x0 && vx <= x1 && vy >= y0 && vy <= y1);
        if (!footHit && !sprHit) continue;

        // 占地命中优先；同条件下选更小占地，再比距占地中心屏幕距离
        float area = (float)(std::max(1, d.w) * std::max(1, d.h));
        float scx = (x0 + x1) * 0.5f, scy = (y0 + y1) * 0.5f;
        float dist = distf(scx, scy, vx, vy);
        float score = area * 80.0f + dist + (footHit ? 0.0f : 400.0f);
        if (score < bestScore) {
            bestScore = score;
            best = (int)i;
        }
    }
    return best;
}

void Game::doSelect(int mx, int my, bool additive) {
    if (!additive) { sel.clear(); selBuilding = INVALID_EID; }
    EID u = pickUnit(mx, my);
    if (u != INVALID_EID) {
        if (world.ents[u].player == localPlayer) {
            sel.push_back(u);
        }
        return;
    }
    EID b = pickBuilding(mx, my);
    if (b != INVALID_EID && world.ents[b].player == localPlayer) {
        selBuilding = b;
        return;
    }
}

void Game::doBoxSelect(Rectangle r, bool additive) {
    if (!additive) { sel.clear(); selBuilding = INVALID_EID; }
    // 屏幕框 → 未缩放视口框
    Rectangle vr{r.x / camZoom, r.y / camZoom, r.width / camZoom, r.height / camZoom};
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
        Rectangle ur = unitScreenRect(e);
        if (CheckCollisionRecs(vr, ur))
            sel.push_back((int)i);
    }
}

void Game::cmdDeploySel() {
    // RA2：选中有驻军的建筑时，部署键撤出全部驻军
    if (sel.empty() && world.valid(selBuilding) && world.ents[selBuilding].isBuilding
        && !world.ents[selBuilding].garrison.empty()
        && world.ents[selBuilding].player == localPlayer) {
        World::Cmd c; c.type = World::Cmd::Ungarrison; c.ids.push_back(selBuilding);
        issueCmd(c);
        message(TR(S::MsgUngarrison));
        return;
    }
    // MCV Repacks：选中建造厂 → 打包回基地车
        if (sel.empty() && world.valid(selBuilding) && world.ents[selBuilding].isBuilding
        && world.ents[selBuilding].player == localPlayer
        && world.ents[selBuilding].btype == BldType::ConYard && world.mcvRepacks) {
        float sx = world.ents[selBuilding].x + 1.5f, sy = world.ents[selBuilding].y + 1.5f;
        World::Cmd c; c.type = World::Cmd::Deploy; c.ids.push_back(selBuilding);
        issueCmd(c);
        selBuilding = INVALID_EID;
        sel.clear();
        for (size_t i = 0; i < world.ents.size(); i++) {
            const World::Ent& e = world.ents[i];
            if (!e.alive || e.isBuilding || e.player != localPlayer || e.utype != UnitType::MCV) continue;
            if (distf(e.x, e.y, sx, sy) < 2.5f) sel.push_back((int)i);
        }
        message(TR(S::MsgDeployed));
        return;
    }
    for (EID id : sel) {
        if (!world.valid(id) || world.ents[id].utype != UnitType::MCV) continue;
        if (!world.canDeployMcv(id)) {
            message(TR(S::MsgCannotBuild)); // 脚印有单位/障碍，不可展开
            continue;
        }
        World::Cmd c; c.type = World::Cmd::Deploy; c.ids.push_back(id);
        issueCmd(c);
        sel.erase(std::remove(sel.begin(), sel.end(), id), sel.end());
        message(TR(S::MsgDeployed));
    }
    // 辐射工兵/重装大兵/美国大兵/攻城直升机：部署/收起（RA2 原作同键）
    bool anyDeploy = false;
    for (EID id : sel)
        if (world.valid(id) && !world.ents[id].isBuilding
            && (world.ents[id].utype == UnitType::Desolator || world.ents[id].utype == UnitType::GuardianGI
                || world.ents[id].utype == UnitType::GI || world.ents[id].utype == UnitType::SiegeChopper))
            anyDeploy = true;
    if (anyDeploy) {
        World::Cmd c; c.type = World::Cmd::RadDeploy; c.ids = sel;
        issueCmd(c);
        message(TR(S::MsgDeployToggled));
    }
}

void Game::issueSmartOrder(int mx, int my) {
    float wx, wy;
    screenToWorld(mx, my, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    if (!world.map.inBounds(tx, ty)) return;

    // 目标：敌人 → 攻击；矿 → 采矿；否则移动
    EID eu = pickUnit(mx, my);
    EID eb = pickBuilding(mx, my);
    EID enemy = INVALID_EID;
    if (eu != INVALID_EID && world.isEnemy(localPlayer, world.ents[eu].player)) enemy = eu;
    // 中立可进驻建筑不当「敌人」（优先进驻；见下方 Garrison）
    if (enemy == INVALID_EID && eb != INVALID_EID && world.isEnemy(localPlayer, world.ents[eb].player)) {
        enemy = eb;
    }

    bool hasEngineer = false, hasHarvester = false, hasSpy = false;
    for (EID id : sel) {
        if (!world.valid(id)) continue;
        if (world.ents[id].utype == UnitType::Engineer) hasEngineer = true;
        if (world.ents[id].utype == UnitType::Spy) hasSpy = true;
        if (unitDef(world.ents[id].utype).canHarvet()) hasHarvester = true;
    }

    // 右键可进驻建筑（己方/中立民房/战斗碉堡/坦克碉堡）→ 按进驻类型过滤（优先于攻击判定：中立建筑不算敌人）
    if (eb != INVALID_EID) {
        const World::Ent& b = world.ents[eb];
        const BldDef& bd = bldDef(b.btype);
        int gdom = garrisonDomain(b.btype);
        if (bd.garrisonCap > 0 && (b.player == localPlayer || b.player < 0)) {
            std::vector<EID> fit;
            for (EID id : sel) {
                if (!world.valid(id) || world.ents[id].isBuilding) continue;
                const UnitDef& ud = unitDef(world.ents[id].utype);
                bool ok = (gdom == 1 && ud.isInfantry())
                       || (gdom == 2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain() == 0 && !ud.canHarvet())
                       || (gdom == 3 && !ud.isAir() && !isHero(world.ents[id].utype));
                if (ok && b.btype == BldType::CivHouse && !canGarrisonCivHouse(world.ents[id].utype))
                    ok = false;
                if (ok) fit.push_back(id);
            }
            if (!fit.empty()) {
                World::Cmd c; c.type = World::Cmd::Garrison; c.ids = fit; c.a = eb;
                issueCmd(c);
                message(TR(S::MsgGarrison));
                return;
            }
        }
        // 工程师占领：敌方或中立可占领建筑（油井/机场等 player=-1 原先被 isEnemy 挡掉）
        if (hasEngineer && bd.capturable && b.player != localPlayer
            && (b.player < 0 || world.isEnemy(localPlayer, b.player))) {
            std::vector<EID> engs;
            for (EID id : sel) if (world.valid(id) && world.ents[id].utype == UnitType::Engineer) engs.push_back(id);
            World::Cmd c; c.type = World::Cmd::Capture; c.ids = engs; c.a = eb;
            issueCmd(c);
            message(TR(S::MsgEngCapture));
            return;
        }
        // 间谍渗透敌方建筑
        if (hasSpy && b.player >= 0 && world.isEnemy(localPlayer, b.player)) {
            std::vector<EID> spies;
            for (EID id : sel) if (world.valid(id) && world.ents[id].utype == UnitType::Spy) spies.push_back(id);
            if (!spies.empty()) {
                World::Cmd c; c.type = World::Cmd::Attack; c.ids = spies; c.a = eb;
                issueCmd(c);
                return;
            }
        }
        // 己方维修厂 / 已占领机械商店 / 科技前哨：受损/被寄生车辆右键 → 开往维修
        if (b.player == localPlayer
            && (b.btype == BldType::ServiceDepot || b.btype == BldType::MachineShop
                || b.btype == BldType::TechOutpost)) {
            std::vector<EID> veh;
            for (EID id : sel) {
                if (!world.valid(id) || world.ents[id].isBuilding) continue;
                const UnitDef& ud = unitDef(world.ents[id].utype);
                if (!ud.isInfantry() && !ud.isAir() && ud.pathDomain() == 0 && !ud.canHarvet()) {
                    const World::Ent& u = world.ents[id];
                    if (u.hp < ud.hp || u.parasite != INVALID_EID) veh.push_back(id);
                }
            }
            if (!veh.empty()) {
                World::Cmd c; c.type = World::Cmd::Service; c.ids = veh; c.a = eb;
                issueCmd(c);
                message(TR(S::MsgService));
                return;
            }
        }
    }

    if (enemy != INVALID_EID) {
        if (hasEngineer && world.ents[enemy].isBuilding && bldDef(world.ents[enemy].btype).capturable) {
            std::vector<EID> engs;
            for (EID id : sel) if (world.valid(id) && world.ents[id].utype == UnitType::Engineer) engs.push_back(id);
            World::Cmd c; c.type = World::Cmd::Capture; c.ids = engs; c.a = enemy;
            issueCmd(c);
            message(TR(S::MsgEngCapture));
        } else {
            World::Cmd c; c.type = World::Cmd::Attack; c.ids = sel; c.a = enemy;
            issueCmd(c);
        }
        return;
    }
    // 右键己方运输载具 → 步兵登船
    if (eu != INVALID_EID && world.ents[eu].player == localPlayer && !world.ents[eu].isBuilding
        && unitDef(world.ents[eu].utype).cargoCap > 0) {
        std::vector<EID> inf;
        for (EID id : sel)
            if (world.valid(id) && !world.ents[id].isBuilding && unitDef(world.ents[id].utype).isInfantry())
                inf.push_back(id);
        if (!inf.empty()) {
            World::Cmd c; c.type = World::Cmd::Board; c.ids = inf; c.a = eu;
            issueCmd(c);
            message(TR(S::MsgBoarding));
            return;
        }
    }
    // 友军建筑：工程师右键受损建筑 → 进入修复
    if (eb != INVALID_EID && world.ents[eb].player == localPlayer) {
        if (hasEngineer && world.ents[eb].hp < bldDef(world.ents[eb].btype).hp) {
            std::vector<EID> engs;
            for (EID id : sel) if (world.valid(id) && world.ents[id].utype == UnitType::Engineer) engs.push_back(id);
            World::Cmd c; c.type = World::Cmd::Repair; c.ids = engs; c.a = eb;
            issueCmd(c);
            message(TR(S::MsgEngRepair));
        }
        return;
    }
    const Cell& c = world.map.at(tx, ty);
    if (hasHarvester && c.ore > 0) {
        std::vector<EID> harv;
        std::vector<EID> rest;
        for (EID id : sel) {
            if (!world.valid(id)) continue;
            if (unitDef(world.ents[id].utype).canHarvet()) harv.push_back(id);
            else rest.push_back(id);
        }
        World::Cmd c; c.type = World::Cmd::Harvest; c.ids = harv; c.a = tx; c.b = ty;
        issueCmd(c);
        if (!rest.empty()) {
            World::Cmd mc; mc.type = World::Cmd::Move; mc.ids = rest;
            mc.x = (float)tx; mc.y = (float)ty; mc.attackMove = kDown(KEY_A);
            issueCmd(mc);
        }
        return;
    }
    World::Cmd mc; mc.type = World::Cmd::Move; mc.ids = sel;
    mc.x = (float)tx; mc.y = (float)ty; mc.attackMove = kDown(KEY_A);
    issueCmd(mc);
}

void Game::message(const std::string& m) {
    msg = m;
    msgTimer = 4;
}


void Game::handleInput() {
    updateCamera();
    Vector2 mouse = mousePos();
    bool overUI = mouse.x > SCREEN_W - sidebarW
               || mouse.y >= SCREEN_H - BOTTOM_BAR_H; // 底栏点击不可穿透到框选/取消选中
    updateHoverCursor((int)mouse.x, (int)mouse.y);
    if (showMenu || gameOver) {
        // 菜单点击在 render 中处理（立即模式）
        return;
    }

    // 超武目标选择模式
    if (targetingSW != SWType::COUNT) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) {
            targetingSW = SWType::COUNT;
            chronoSourceSel.clear();
            return;
        }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            float wx, wy;
            screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
            int tx, ty;
            screenToTile(wx, wy, tx, ty);
            if (world.map.inBounds(tx, ty)) {
                SWType t = targetingSW;
                // 本地预检（就绪与可用），联机下命令延迟执行，消息按预检结果提示
                if (world.swAvailable(localPlayer, t) && world.players[localPlayer].swReady[(int)t]) {
                    if (t == SWType::ChronoShift && chronoSourceSel.empty()) {
                        // 第一阶段：来源区域。实体 ID 按稳定索引排序写入最终 Cmd，
                        // 目标阶段才扣充能，取消或无有效车辆均不会浪费超武。
                        for (EID id = 0; id < (EID)world.ents.size(); id++) {
                            const auto& e = world.ents[id];
                            if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
                            const UnitDef& u = unitDef(e.utype);
                            if (u.isAir() || u.isInfantry()) continue;
                            if (distf(e.x, e.y, tx + 0.5f, ty + 0.5f) <= 3.0f)
                                chronoSourceSel.push_back(id);
                        }
                        if (chronoSourceSel.empty()) {
                            message(g_lang ? "No eligible vehicles in source area" : "来源区域内没有可传送车辆");
                        } else {
                            message(g_lang ? "Select Chrono Shift destination" : "请选择超时空传送目标区域");
                        }
                        return;
                    }
                    World::Cmd c; c.type = World::Cmd::LaunchSW; c.a = (int)t;
                    c.x = tx + 0.5f; c.y = ty + 0.5f;
                    if (t == SWType::ChronoShift) c.ids = chronoSourceSel;
                    issueCmd(c);
                    message(TextFormat(TR(S::MsgSWLaunchedFmt), swName(t)));
                }
                targetingSW = SWType::COUNT;
                chronoSourceSel.clear();
            }
        }
        return;
    }

    // 伞兵空降点选择模式（RA2 原作：美国空指部/科技机场支援技能）
    if (targetingParadrop) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) {
            targetingParadrop = false;
            return;
        }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            float wx, wy;
            screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
            int tx, ty;
            screenToTile(wx, wy, tx, ty);
            if (world.map.inBounds(tx, ty)) {
                if (world.players[localPlayer].paradropReady) {
                    World::Cmd c; c.type = World::Cmd::Paradrop;
                    c.x = tx + 0.5f; c.y = ty + 0.5f;
                    issueCmd(c);
                }
                targetingParadrop = false;
            }
        }
        return;
    }

    // 维修/出售模式：保持至右键/Esc 取消（RA2：可连续点多座建筑）
    if (sideMode != 0) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) { sideMode = 0; return; }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            EID b = pickBuilding((int)mouse.x, (int)mouse.y);
            if (b != INVALID_EID && world.ents[b].player == localPlayer && !world.ents[b].selling) {
                if (sideMode == 2) {
                    if (world.ents[b].btype != BldType::ConYard) {
                        World::Cmd c; c.type = World::Cmd::SellBuilding; c.ids.push_back(b);
                        issueCmd(c);
                        message(TR(S::MsgSold));
                    } else if (world.mcvRepacks) {
                        // 建造厂不可出售；开启 MCV Repacks 时出售光标改为打包回基地车
                        World::Cmd c; c.type = World::Cmd::Deploy; c.ids.push_back(b);
                        issueCmd(c);
                        if (selBuilding == b) selBuilding = INVALID_EID;
                        message(TR(S::MsgDeployed));
                    } else {
                        message(TR(S::MsgConYardNoSell));
                    }
                } else {
                    const BldDef& bd = bldDef(world.ents[b].btype);
                    // 持续维修开关：只要受损即可切换（费用在 tick 中按 RepairPercent 扣除）
                    if (world.ents[b].hp < bd.hp) {
                        World::Cmd c; c.type = World::Cmd::RepairBuilding; c.ids.push_back(b);
                        issueCmd(c);
                        message(TR(S::MsgRepaired));
                    }
                    else message(TR(S::MsgNoRepair));
                }
            }
        }
        return; // 该模式下屏蔽选择/框选
    }

    // 放置建筑模式
    if (placing) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) {
            placing = false;
            world.players[localPlayer].placingBld = BldType::COUNT;
            return;
        }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            float wx, wy;
            screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
            int tx, ty;
            screenToTile(wx, wy, tx, ty);
            BldType t = world.players[localPlayer].placingBld;
            const BldDef& d = bldDef(t);
            // 光标对准占地东南角格（与幽灵锚点一致，避免中心格导致大幅偏移）
            int bx = tx - (d.w - 1), by = ty - (d.h - 1);
            if (world.canPlace(t, bx, by, localPlayer)) { // 本地预检；真正放置在命令执行时（联机延迟）
                World::Cmd c; c.type = World::Cmd::PlaceBuilding; c.a = (int)t;
                c.x = (float)bx; c.y = (float)by;
                issueCmd(c);
                placing = false;
                if (!kDown(KEY_LEFT_SHIFT)) world.players[localPlayer].placingBld = BldType::COUNT;
                else { world.players[localPlayer].placingBld = t; placing = true; }
            } else {
                message(TR(S::MsgCannotPlace));
            }
        }
        return;
    }

    // 框选/点选：LMB 只负责选择（移动/攻击一律 RMB）
    bool cancelledBoxDrag = false;
    auto hitLocalAt = [&](int x, int y) -> bool {
        EID u = pickUnit(x, y);
        if (u != INVALID_EID && world.ents[u].player == localPlayer) return true;
        EID b = pickBuilding(x, y);
        return b != INVALID_EID && world.ents[b].player == localPlayer;
    };
    if (!overUI && mPressed(MOUSE_LEFT_BUTTON)) {
        // 已选 + Deploy 光标：点自身或空地 → 展开（RA2；勿被点选逻辑吃掉）
        if (!sel.empty() && cursorKind == CursorKind::Deploy) {
            int px = (int)mouse.x, py = (int)mouse.y;
            EID u = pickUnit(px, py);
            bool onSelected = (u != INVALID_EID
                && std::find(sel.begin(), sel.end(), u) != sel.end());
            if (onSelected || !hitLocalAt(px, py)) {
                cmdDeploySel();
                dragging = false;
                dragPressSelected = false;
                return;
            }
        }
        if (sel.empty() && cursorKind == CursorKind::Deploy) {
            EID eb = pickBuilding((int)mouse.x, (int)mouse.y);
            if (eb != INVALID_EID && world.ents[eb].player == localPlayer
                && !world.ents[eb].garrison.empty()) {
                World::Cmd c; c.type = World::Cmd::Ungarrison; c.ids.push_back(eb);
                issueCmd(c);
                message(TR(S::MsgUngarrison));
                dragging = false;
                dragPressSelected = false;
                return;
            }
        }
        dragging = true;
        dragStart = mouse;
        dragPressSelected = false;
        // 按下即点选：实机微抖/松手偏移时仍能选中
        int px = (int)mouse.x, py = (int)mouse.y;
        if (hitLocalAt(px, py)) {
            doSelect(px, py, kDown(KEY_LEFT_SHIFT));
            dragPressSelected = true;
        }
    }
    if (dragging && (mReleased(MOUSE_LEFT_BUTTON) || mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE))) {
        if (mReleased(MOUSE_LEFT_BUTTON)) {
            Rectangle r{
                std::min(dragStart.x, mouse.x), std::min(dragStart.y, mouse.y),
                fabsf(mouse.x - dragStart.x), fabsf(mouse.y - dragStart.y)};
            bool add = kDown(KEY_LEFT_SHIFT);
            // 任一边够长才算框选，避免人手微抖清掉按下时的点选
            bool realBox = (r.width >= 28.f && r.height >= 28.f);
            int ix = (int)(overUI ? dragStart.x : mouse.x);
            int iy = (int)(overUI ? dragStart.y : mouse.y);
            int sx0 = (int)dragStart.x, sy0 = (int)dragStart.y;

            if (realBox) {
                doBoxSelect(r, add);
            } else if (dragPressSelected) {
                // 已在按下时选中：保持，不触发 Deploy/清选
            } else {
                bool hit = hitLocalAt(sx0, sy0) || hitLocalAt(ix, iy);
                if (hit) {
                    if (hitLocalAt(sx0, sy0)) doSelect(sx0, sy0, add);
                    else doSelect(ix, iy, add);
                } else if (!sel.empty() && cursorKind == CursorKind::Deploy) {
                    // 空地 + 部署光标：展开（D 键仍可用）
                    cmdDeploySel();
                } else if (sel.empty() && cursorKind == CursorKind::Deploy) {
                    EID eb = pickBuilding(ix, iy);
                    if (eb != INVALID_EID && world.ents[eb].player == localPlayer
                        && !world.ents[eb].garrison.empty()) {
                        World::Cmd c; c.type = World::Cmd::Ungarrison; c.ids.push_back(eb);
                        issueCmd(c);
                        message(TR(S::MsgUngarrison));
                    }
                } else {
                    doSelect(ix, iy, add); // 点空地：清选（绝不 LMB 下令移动）
                }
            }
        } else {
            cancelledBoxDrag = true;
        }
        dragging = false;
        dragPressSelected = false;
    }
    if (!overUI) {
        // 右键指令
        if (mPressed(MOUSE_RIGHT_BUTTON) && !dragging && !cancelledBoxDrag) {
            if (!sel.empty()) {
                if (waypointLatch) { // 路径点模式：追加到队列而非替换目标
                    float wx, wy;
                    screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
                    int tx, ty;
                    screenToTile(wx, wy, tx, ty);
                    if (world.map.inBounds(tx, ty)) {
                        World::Cmd c; c.type = World::Cmd::Move; c.ids = sel;
                        c.x = (float)tx; c.y = (float)ty; c.a = 1; // a&1=路径点追加
                        issueCmd(c);
                    }
                } else {
                    issueSmartOrder((int)mouse.x, (int)mouse.y);
                    waypointLatch = false; // 普通指令自动退出路径点模式
                }
            } else if (world.valid(selBuilding) && world.ents[selBuilding].isBuilding
                       && world.ents[selBuilding].player == localPlayer
                       && world.ents[selBuilding].btype == BldType::ConYard && world.mcvRepacks) {
                // MCV Repacks：选中建造厂右键地面 → 打包成基地车并移动
                float wx, wy;
                screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
                int tx, ty;
                screenToTile(wx, wy, tx, ty);
                if (world.map.inBounds(tx, ty) && world.map.passable(tx, ty)) {
                    EID cy = selBuilding;
                    World::Cmd c; c.type = World::Cmd::Move; c.ids.push_back(cy);
                    c.x = (float)tx; c.y = (float)ty;
                    issueCmd(c);
                    selBuilding = INVALID_EID;
                    sel.clear();
                    // 单机命令已应用：选中新基地车
                    for (size_t i = 0; i < world.ents.size(); i++) {
                        const World::Ent& e = world.ents[i];
                        if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
                        if (e.utype != UnitType::MCV) continue;
                        if (e.state == UState::Moving || e.state == UState::AttackMoving)
                            sel.push_back((int)i);
                    }
                    message(TR(S::MsgDeployed));
                }
            } else if (world.valid(selBuilding) && world.ents[selBuilding].isBuilding
                       && world.ents[selBuilding].player == localPlayer
                       && isRallyBuilding(world.ents[selBuilding].btype)) {
                // 仅选中生产建筑时右键设集结点（RA2）
                float wx, wy;
                screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
                int tx, ty;
                screenToTile(wx, wy, tx, ty);
                if (world.map.inBounds(tx, ty)) {
                    World::Cmd c; c.type = World::Cmd::SetRally; c.ids.push_back(selBuilding);
                    c.x = (float)tx; c.y = (float)ty;
                    issueCmd(c);
                    message(TR(S::MsgRallySet));
                }
            } else { selBuilding = INVALID_EID; waypointLatch = false; }
        }
    }

    // 快捷键（设置页可重绑；0=未绑定。A/Shift/Ctrl/方向键等修饰与镜头键固定）
    auto ka = [&](int a) { return keyBind[a] > 0 && kPressed(keyBind[a]); };
    if (ka(KA_Stop)) { World::Cmd c; c.type = World::Cmd::Stop; c.ids = sel; issueCmd(c); }
    if (ka(KA_Unload)) {
        if (!sel.empty()) { // 运输船卸载
            World::Cmd c; c.type = World::Cmd::Unload; c.ids = sel; issueCmd(c);
        }
        // 选中建筑有驻军：撤出全部驻军（RA2 原作同键）
        if (world.valid(selBuilding) && world.ents[selBuilding].isBuilding
            && !world.ents[selBuilding].garrison.empty()) {
            World::Cmd c; c.type = World::Cmd::Ungarrison; c.ids.push_back(selBuilding);
            issueCmd(c);
            message(TR(S::MsgUngarrison));
        }
    }
    if (ka(KA_Deploy)) cmdDeploySel();
    // 路径点模式开关（RA2 原作 Z 键）：开启后右键追加路径点
    if (ka(KA_Waypoint) && !sel.empty()) {
        waypointLatch = !waypointLatch;
        message(waypointLatch ? TR(S::MsgWaypointOn) : TR(S::MsgWaypointOff));
        g_sfx.play(Sfx::Click, 0.6f);
    }
    // 散布（RA2 原作键位）
    if (ka(KA_Scatter) && !sel.empty()) {
        World::Cmd c; c.type = World::Cmd::Scatter; c.ids = sel;
        issueCmd(c);
        message(TR(S::MsgScatter));
    }
    // 警戒（RA2 原作键位）
    if (ka(KA_Guard) && !sel.empty()) {
        World::Cmd c; c.type = World::Cmd::Guard; c.ids = sel;
        issueCmd(c);
        message(TR(S::MsgGuard));
    }
    // 选择同类（RA2 原作键位）
    if (ka(KA_SameType) && !sel.empty()) {
        bool types[(int)UnitType::COUNT] = {};
        for (EID id : sel)
            if (world.valid(id) && !world.ents[id].isBuilding) types[(int)world.ents[id].utype] = true;
        sel.clear();
        for (size_t i = 0; i < world.ents.size(); i++) {
            const World::Ent& e = world.ents[i];
            if (e.alive && !e.isBuilding && e.player == localPlayer && types[(int)e.utype])
                sel.push_back((int)i);
        }
        message(TR(S::MsgSelSameType));
        g_sfx.play(Sfx::Click, 0.6f);
    }
    // 编队：Ctrl+数字设定 / 数字召回 / 双击数字跳转视角
    {
        bool ctrl = kDown(KEY_LEFT_CONTROL) || kDown(KEY_RIGHT_CONTROL);
        for (int n = 0; n <= 9; n++) {
            if (!kPressed(KEY_ZERO + n)) continue;
            if (ctrl) {
                groups[n] = sel;
                groups[n].erase(std::remove_if(groups[n].begin(), groups[n].end(), [&](EID id) {
                    return !world.valid(id) || world.ents[id].isBuilding;
                }), groups[n].end());
                message(TextFormat(TR(S::MsgGroupSetFmt), n, (int)groups[n].size()));
                g_sfx.play(Sfx::Click, 0.6f);
            } else {
                auto& g = groups[n];
                g.erase(std::remove_if(g.begin(), g.end(), [&](EID id) {
                    return !world.valid(id) || world.ents[id].isBuilding;
                }), g.end());
                if (g.empty()) continue;
                sel = g;
                selBuilding = INVALID_EID;
                double now = GetTime();
                if (lastGroupKey == n && now - lastGroupTap < 0.5) {
                    // 双击：视角跳到编队重心
                    float cx = 0, cy = 0;
                    for (EID id : g) {
                        Vector2 p = unitScreenPos(world.ents[id]);
                        cx += p.x + camX; cy += p.y + camY;
                    }
                    cx /= (float)g.size(); cy /= (float)g.size();
                    camX = cx - (SCREEN_W - sidebarW) / 2.0f;
                    camY = cy - SCREEN_H / 2.0f;
                }
                lastGroupKey = n;
                lastGroupTap = now;
            }
        }
    }
    // 音乐开关
    if (ka(KA_Music)) {
        g_sfx.toggleBgm();
        message(g_sfx.bgmEnabled() ? TR(S::MsgMusicOn) : TR(S::MsgMusicOff));
    }
    // 快速存档 / 快速读档（联机禁用：lockstep 下存读档无法同步）
    if (!netGame && ka(KA_QuickSave)) message(saveGameFile(QUICKSAVE_PATH) ? TR(S::MsgSaved) : TR(S::MsgSaveFail));
    if (!netGame && ka(KA_QuickLoad)) message(loadGameFile(QUICKSAVE_PATH) ? TR(S::MsgLoaded) : TR(S::MsgLoadFail));
    // 出售选中建筑（默认 Del）；建造厂在 MCV Repacks 开启时改为打包
    if (ka(KA_Sell) && world.valid(selBuilding) && world.ents[selBuilding].player == localPlayer) {
        if (world.ents[selBuilding].btype != BldType::ConYard) {
            World::Cmd c; c.type = World::Cmd::SellBuilding; c.ids.push_back(selBuilding);
            issueCmd(c);
            selBuilding = INVALID_EID;
            message(TR(S::MsgSold));
        } else if (world.mcvRepacks) {
            World::Cmd c; c.type = World::Cmd::Deploy; c.ids.push_back(selBuilding);
            issueCmd(c);
            selBuilding = INVALID_EID;
            message(TR(S::MsgDeployed));
        }
    }
    if (ka(KA_ViewBase)) {
        for (auto& e : world.ents)
            if (e.alive && e.isBuilding && e.player == localPlayer && e.btype == BldType::ConYard) {
                Vector2 p = bldScreenPos(e);
                camX += p.x - (SCREEN_W - sidebarW) / 2.0f;
                camY += p.y - SCREEN_H / 2.0f;
                break;
            }
    }
    if (ka(KA_Pause) && !netGame) paused = !paused; // 联机不可单方暂停（lockstep 步进对齐）
    // 页签快捷键（RA2 原作 QWER：建筑/防御/步兵/载具）
    if (kPressed(KEY_Q)) { uiTab = 0; uiScroll = 0; }
    if (kPressed(KEY_W)) { uiTab = 1; uiScroll = 0; }
    if (kPressed(KEY_E)) { uiTab = 2; uiScroll = 0; }
    if (kPressed(KEY_R)) { uiTab = 3; uiScroll = 0; }
    if (kPressed(KEY_F3)) showFps = !showFps; // 帧率/耗时显示（性能诊断）
    if (!netGame) {
        if (ka(KA_SpeedUp) || kPressed(KEY_KP_ADD)) gameSpeed = std::min(2, gameSpeed + 1);
        if (ka(KA_SpeedDown) || kPressed(KEY_KP_SUBTRACT)) gameSpeed = std::max(0, gameSpeed - 1);
    }
    if (kPressed(KEY_ESCAPE)) {
        if (!sel.empty() || world.valid(selBuilding)) { sel.clear(); selBuilding = INVALID_EID; }
        else showMenu = true;
    }
    // 设置集结点
    if (ka(KA_Rally) && world.valid(selBuilding)) {
        float wx, wy;
        screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
        int tx, ty;
        screenToTile(wx, wy, tx, ty);
        World::Cmd c; c.type = World::Cmd::SetRally; c.ids.push_back(selBuilding);
        c.x = (float)tx; c.y = (float)ty;
        issueCmd(c);
        message(TR(S::MsgRallySet));
    }

    // 清理失效选择
    sel.erase(std::remove_if(sel.begin(), sel.end(), [&](EID id) { return !world.valid(id); }), sel.end());
    if (!world.valid(selBuilding)) selBuilding = INVALID_EID;
}

// ===================== 渲染 =====================

