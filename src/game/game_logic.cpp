#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include "rlgl.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

void Game::logic() {
    if (netGame) {
        netAdvance(); // lockstep：远端命令就绪才 world.update()（本帧可能不推进）
    } else {
        world.update();
        for (auto& ai : ais) ai.update(world);
    }
    // 脚本引擎：每逻辑帧触发 OnTick（联机模式下跳过，避免非确定性）
    if (!netGame) g_script.onTick(world.tick);

    // 听者位置 = 视野中心瓦片
    {
        int ltx, lty;
        screenToTile(camX + (SCREEN_W - sidebarW) / 2.0f, camY + SCREEN_H / 2.0f, ltx, lty);
        g_sfx.setListener((float)ltx, (float)lty);
    }
    // 低电力警报（上升沿触发）
    {
        bool low = world.players[localPlayer].lowPower();
        if (low && !wasLowPower) {
            g_sfx.play(Sfx::Alarm, 0.8f);
            message(TR(S::MsgLowPower));
        }
        wasLowPower = low;
    }

    // EVA 播报：消费世界事件 → 字幕队列（仅本地玩家）
    while (!world.evaQueue.empty()) {
        const World::EvaEvent& ev = world.evaQueue.front();
        if (ev.player == localPlayer && evaLines.size() < 4) {
            evaLines.push_back(ev.text);
            g_sfx.play(Sfx::Eva, 0.7f);
        }
        world.evaQueue.pop_front();
    }
    // 逐条显示（不覆盖当前消息）
    if (msgTimer <= 0 && !evaLines.empty()) {
        msg = evaLines.front();
        msgTimer = 2.6f;
        evaLines.pop_front();
    }

    // 胜负判定
    if (!gameOver) {
        bool meDead = world.players[localPlayer].defeated;
        if (campaignMission >= 0) {
            // 战役：先刷波次与触发器再判定
            const MissionDef& md = missionTable()[campaignMission];
            if (nextWave < md.waves.size() && world.tick >= (uint64_t)md.waves[nextWave].atTick) {
                spawnCampaignWave();
                nextWave++;
            }
            updateTriggers(); // P7 触发器脚本（可在判定前直接 Win/Lose）
            if (gameOver) { /* 触发器已终结本局 */ }
            else if (meDead) { gameOver = true; victory = false; }
            else if (md.objective == 2) {
                // 触发器决定胜负：等待 Win/Lose 动作，无默认胜负
            } else if (md.objective == 1) {
                // 存活目标：坚守到指定帧数
                if (world.tick >= (uint64_t)md.objectiveTick) { gameOver = true; victory = true; }
            } else {
                // 歼灭目标：敌军全灭且脚本波次刷完
                bool allAIDead = true;
                for (int i = 1; i < world.numPlayers; i++)
                    if (!world.players[i].defeated) allAIDead = false;
                if (allAIDead && nextWave >= md.waves.size()) { gameOver = true; victory = true; }
            }
        } else {
            bool allAIDead = true;
            for (int i = 1; i < world.numPlayers; i++)
                if (!world.players[i].defeated) allAIDead = false;
            if (meDead) { gameOver = true; victory = false; }
            else if (allAIDead) { gameOver = true; victory = true; }
        }
    }
    if (msgTimer > 0) msgTimer -= 1.0f / LOGIC_FPS;
    // 联机异常结算：对手断线判胜（弃权），不同步判负并提示
    if (netGame && !gameOver) {
        if (netPeerLeft) { gameOver = true; victory = true; message(TR(S::PeerLeft)); }
        else if (netDesync) { gameOver = true; victory = false; message(TR(S::DesyncWarn)); }
    }
}

// ===================== 输入 =====================

