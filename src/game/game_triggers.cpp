#include "game/game.h"
#include "game/campaign.h"
#include "game/campaign_runtime.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

static void spawnUnitsNear(World& world, int player, int cx, int cy, const std::vector<UnitType>& units,
                           int moveX, int moveY, std::vector<EID>* outSpawned) {
    std::vector<EID> spawned;
    for (UnitType ut : units) {
        int dom = unitDef(ut).pathDomain();
        int bx = -1, by = -1;
        for (int r = 0; r < 16 && bx < 0; r++)
            for (int dy = -r; dy <= r && bx < 0; dy++)
                for (int dx = -r; dx <= r && bx < 0; dx++) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    int nx = cx + dx, ny = cy + dy;
                    if (world.passableFor(nx, ny, dom) && !world.bldBlocked(nx, ny)
                        && world.unitAtCell(nx, ny) == INVALID_EID) { bx = nx; by = ny; }
                }
        if (bx < 0) continue;
        spawned.push_back(world.spawnUnit(player, ut, bx + 0.5f, by + 0.5f));
    }
    if (!spawned.empty() && moveX >= 0)
        world.orderMove(spawned, moveX + 0.5f, moveY + 0.5f, true);
    if (outSpawned) *outSpawned = std::move(spawned);
}

void Game::updateTriggers() {
    if (campaignMission < 0) return;
    const MissionDef& md = missionTable()[campaignMission];

    // 限时失败（TimerStart）
    if (!gameOver && campRuntime.timerExpired(world.tick)) {
        gameOver = true;
        victory = false;
        message("Mission timer expired.");
        return;
    }

    for (Trigger& t : missionTriggers) {
        if (!t.enabled) continue;
        if (t.once && t.fired) continue;
        if (t.requiresPhase >= 0 && campRuntime.phase < t.requiresPhase) continue;
        bool cond = false;
        switch (t.cond) {
            case TrigCond::Always: cond = true; break;
            case TrigCond::Time: cond = world.tick >= (uint64_t)t.c[0]; break;
            case TrigCond::PlayerBldLost: {
                int p = t.c[0];
                if (p < 0 || p >= world.numPlayers) break;
                int n = world.countBlds(p, (BldType)t.c[1]);
                if (n > 0) t.armed = true;
                cond = t.armed && n == 0;
                break;
            }
            case TrigCond::PlayerAllDead: {
                int p = t.c[0];
                cond = p >= 0 && p < world.numPlayers && world.players[p].defeated;
                break;
            }
            case TrigCond::UnitInRect: {
                int p = t.c[0];
                if (p < 0 || p >= world.numPlayers) break;
                int x1 = std::min(t.c[1], t.c[3]), x2 = std::max(t.c[1], t.c[3]);
                int y1 = std::min(t.c[2], t.c[4]), y2 = std::max(t.c[2], t.c[4]);
                for (auto& e : world.ents)
                    if (e.alive && !e.isBuilding && e.player == p
                        && e.x >= (float)x1 && e.x <= (float)x2 && e.y >= (float)y1 && e.y <= (float)y2) {
                        cond = true;
                        break;
                    }
                break;
            }
            case TrigCond::MoneyBelow: {
                int p = t.c[0];
                cond = p >= 0 && p < world.numPlayers && world.players[p].money < t.c[1];
                break;
            }
            case TrigCond::UnitLost: {
                int p = t.c[0];
                UnitType ut = (UnitType)t.c[1];
                if (p < 0 || p >= world.numPlayers) break;
                int n = 0;
                for (const auto& e : world.ents)
                    if (e.alive && !e.isBuilding && e.player == p && e.utype == ut) n++;
                for (const auto& e : world.ents) {
                    if (!e.alive || e.player != p) continue;
                    for (const auto& g : e.garrison) if (g.type == ut) n++;
                    for (const auto& g : e.cargo) if (g.type == ut) n++;
                }
                if (n > 0) t.armed = true;
                cond = t.armed && n == 0;
                break;
            }
            case TrigCond::BldCaptured: {
                int p = t.c[0];
                if (p < 0 || p >= world.numPlayers) break;
                int need = t.c[2] > 0 ? t.c[2] : 1;
                cond = world.countBlds(p, (BldType)t.c[1]) >= need;
                break;
            }
            case TrigCond::ObjAllPrimary:
                cond = campRuntime.allGateWinDone(md, campaignObjDone);
                break;
            case TrigCond::UnitCountBelow: {
                int p = t.c[0];
                UnitType ut = (UnitType)t.c[1];
                int lim = t.c[2];
                if (p < 0 || p >= world.numPlayers) break;
                int n = 0;
                for (const auto& e : world.ents)
                    if (e.alive && !e.isBuilding && e.player == p && e.utype == ut) n++;
                cond = n < lim;
                break;
            }
            case TrigCond::PhaseAt:
                cond = campRuntime.phase >= t.c[0];
                break;
            case TrigCond::Script:
                cond = g_script.onTriggerCond(t.tag);
                break;
        }
        if (!cond) continue;
        t.fired = true;
        const std::string& ts = (g_lang && !t.msgEn.empty()) ? t.msgEn : t.msg;
        const char* txt = ts.empty() ? nullptr : ts.c_str();
        switch (t.act) {
            case TrigAct::SpawnAt: {
                int p = t.a[0];
                if (p < 0 || p >= world.numPlayers) break;
                spawnUnitsNear(world, p, t.a[1], t.a[2], t.units, t.a[3], t.a[4], nullptr);
                if (txt) world.eva(0, txt);
                break;
            }
            case TrigAct::Reinforce: {
                int p = t.a[0];
                if (p < 0 || p >= world.numPlayers) break;
                int edge = t.a[1] & 3;
                int cx = world.map.w / 2, cy = 2;
                if (edge == 1) { cx = world.map.w - 3; cy = world.map.h / 2; }
                else if (edge == 2) { cx = world.map.w / 2; cy = world.map.h - 3; }
                else if (edge == 3) { cx = 2; cy = world.map.h / 2; }
                int mx = t.a[2], my = t.a[3];
                spawnUnitsNear(world, p, cx, cy, t.units, mx, my, nullptr);
                if (txt) world.eva(0, txt);
                break;
            }
            case TrigAct::Eva:
                if (txt) world.eva(0, txt);
                break;
            case TrigAct::GiveMoney: {
                int p = t.a[0];
                if (p >= 0 && p < world.numPlayers) world.players[p].money += t.a[1];
                if (txt) world.eva(0, txt);
                break;
            }
            case TrigAct::RevealMap: {
                int p = t.a[0];
                if (p >= 0 && p < world.numPlayers) world.map.reveal(p, t.a[1], t.a[2], t.a[3]);
                break;
            }
            case TrigAct::Win:
                gameOver = true;
                victory = true;
                if (!md.lineId.empty())
                    campaignSetProgress(md.lineId, md.lineIndex + 1);
                break;
            case TrigAct::Lose: gameOver = true; victory = false; break;
            case TrigAct::Objective:
                if (txt) {
                    objectiveText = txt;
                    message(txt);
                }
                break;
            case TrigAct::CompleteObj:
                if (t.a[0] >= 0 && t.a[0] < (int)campaignObjDone.size())
                    campaignObjDone[t.a[0]] = true;
                if (txt) { objectiveText = txt; message(txt); }
                break;
            case TrigAct::SetPhase:
                campRuntime.setPhase(t.a[0]);
                if (txt) { objectiveText = txt; message(txt); }
                break;
            case TrigAct::EnableTag:
                if (!t.tag.empty()) {
                    for (Trigger& u : missionTriggers)
                        if (u.tag == t.tag) u.enabled = true;
                }
                if (txt) world.eva(0, txt);
                break;
            case TrigAct::TimerStart: {
                int dur = t.a[0] > 0 ? t.a[0] : 1;
                bool vis = t.a[1] != 0 || md.timerVisibleDefault;
                campRuntime.startTimer((int)world.tick + dur, vis);
                if (txt) world.eva(0, txt);
                break;
            }
            case TrigAct::TimerAbort:
                campRuntime.abortTimer();
                if (txt) world.eva(0, txt);
                break;
            case TrigAct::Script:
                g_script.onTrigger(t.tag);
                break;
        }
    }

    // 主目标门闩胜利（显式 Win 已处理则跳过）
    if (!gameOver && md.objective == 2 && campRuntime.winOnAllPrimary
        && campRuntime.allGateWinDone(md, campaignObjDone)) {
        gameOver = true;
        victory = true;
        if (!md.lineId.empty())
            campaignSetProgress(md.lineId, md.lineIndex + 1);
    }
}

// ===================== 存档/读档 =====================
