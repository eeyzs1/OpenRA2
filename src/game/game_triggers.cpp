#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

void Game::updateTriggers() {
    if (campaignMission < 0) return;
    for (Trigger& t : missionTriggers) {
        if (t.once && t.fired) continue;
        bool cond = false;
        switch (t.cond) {
            case TrigCond::Always: cond = true; break;
            case TrigCond::Time: cond = world.tick >= (uint64_t)t.c[0]; break;
            case TrigCond::PlayerBldLost: {
                int p = t.c[0];
                if (p < 0 || p >= world.numPlayers) break;
                int n = world.countBlds(p, (BldType)t.c[1]);
                if (n > 0) t.armed = true;              // 目标曾存在过才允许"全灭"成立
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
                // 货舱/驻军中的英雄也算存活
                for (const auto& e : world.ents) {
                    if (!e.alive || e.player != p) continue;
                    for (const auto& g : e.garrison) if (g.type == ut) n++;
                    for (const auto& g : e.cargo) if (g.type == ut) n++;
                }
                if (n > 0) t.armed = true;
                cond = t.armed && n == 0;
                break;
            }
            case TrigCond::Script:
                cond = g_script.onTriggerCond(t.tag);
                break;
        }
        if (!cond) continue;
        t.fired = true;
        // 双语：英文缺省回退中文（msg 可空，如 RevealMap）
        const std::string& ts = (g_lang && !t.msgEn.empty()) ? t.msgEn : t.msg;
        const char* txt = ts.empty() ? nullptr : ts.c_str();
        switch (t.act) {
            case TrigAct::SpawnAt: {
                int p = t.a[0];
                if (p < 0 || p >= world.numPlayers) break;
                // 在 (a1,a2) 附近按寻路域找空格刷兵；a3>=0 时攻击移动至 (a3,a4)
                std::vector<EID> spawned;
                for (UnitType ut : t.units) {
                    int dom = unitDef(ut).pathDomain();
                    int bx = -1, by = -1;
                    for (int r = 0; r < 16 && bx < 0; r++)
                        for (int dy = -r; dy <= r && bx < 0; dy++)
                            for (int dx = -r; dx <= r && bx < 0; dx++) {
                                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                                int nx = t.a[1] + dx, ny = t.a[2] + dy;
                                if (world.passableFor(nx, ny, dom) && !world.bldBlocked(nx, ny)
                                    && world.unitAtCell(nx, ny) == INVALID_EID) { bx = nx; by = ny; }
                            }
                    if (bx < 0) continue;
                    spawned.push_back(world.spawnUnit(p, ut, bx + 0.5f, by + 0.5f));
                }
                if (!spawned.empty() && t.a[3] >= 0)
                    world.orderMove(spawned, t.a[3] + 0.5f, t.a[4] + 0.5f, true);
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
            case TrigAct::Win:  gameOver = true; victory = true; break;
            case TrigAct::Lose: gameOver = true; victory = false; break;
            case TrigAct::Objective:
                if (txt) { objectiveText = txt; message(txt); }
                break;
            case TrigAct::Script:
                g_script.onTrigger(t.tag);
                break;
        }
    }
}

// ===================== 存档/读档 =====================

