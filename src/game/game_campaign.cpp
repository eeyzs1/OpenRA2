#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

void Game::newCampaignGame(int mission, bool prepareRender) {
    const MissionDef& md = missionTable()[mission];
    campaignMission = mission;
    nextWave = 0;
    missionTriggers = md.triggers; // 运行时副本（fired/armed 可变）
    objectiveText.clear();
    std::vector<Faction> factions;
    factions.push_back(md.playerFaction);
    for (Faction f : md.aiFactions) factions.push_back(f);
    // 固定种子：战役地图可复现；手工地图关卡从 maps/xxx.txt 加载地形与预置实体
    world.init(md.mapSize, md.mapSize, 20260723ull + mission * 977, 1, (int)md.aiFactions.size(), factions, md.mapType,
               md.mapFile.empty() ? nullptr : md.mapFile.c_str(), md.noStartForce);
    // 战役国家：按阵营取默认国（盟=美国 苏=苏俄 中=中国），使国家特色单位/支援可用
    for (int i = 0; i < world.numPlayers; i++)
        world.players[i].country = countriesOf(world.players[i].faction).front();
    int pool[MAX_PLAYERS], pn = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != cfgColor) pool[pn++] = i;
    world.players[0].colorId = cfgColor;
    for (int i = 1; i < world.numPlayers; i++) world.players[i].colorId = pool[(i - 1) % pn];
    for (int i = 0; i < world.numPlayers; i++) world.players[i].money = md.money;
    world.skirmishMode = SkirmishMode::Battle;
    world.cratesEnabled = cfgCrates;
    world.aiAlliance = cfgAlliance;
    world.sharedVision = false;
    world.shortGame = false;
    world.superweaponsEnabled = true;
    ais.assign((int)md.aiFactions.size(), SkirmishAI{});
    for (int i = 0; i < (int)ais.size(); i++) {
        ais[i].reset(i + 1);
        // 战役 AI 难度：固定普通；人格按任务类型自动选择
        // objective=1（坚守关）敌方为进攻型（Rusher），objective=0（歼灭关）敌方为龟缩型（Turtler）
        ais[i].difficulty = AIDiff::Normal;
        ais[i].personality = (md.objective == 1) ? AIPersonality::Rusher : AIPersonality::Turtler;
        ais[i].initPersonality();
    }
    sel.clear();
    selBuilding = INVALID_EID;
    placing = false;
    gameOver = victory = false;
    camZoom = 1.0f;
    for (auto& e : world.ents)
        if (e.alive && !e.isBuilding && e.player == 0) {
            int sx, sy;
            tileToScreen((int)e.x, (int)e.y, sx, sy);
            camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
            camY = (float)sy - SCREEN_H / 2.0f;
            break;
        }
    message(missionBrief(mission));
    phase = Phase::InGame;
    if (prepareRender) {
        bakeTerrain();
        g_sprites.preloadMatch(localPlayer); // 开局预载本地玩家素材，消除游戏中懒加载掉帧
        // 脚本引擎：加载 assets/scripts/*.lua 并触发 OnGameStart
        g_script.init(&world, 1, mission);
        g_script.onGameStart();
    }
}

// 战役波次：为敌方玩家1刷出脚本部队，攻击移动至玩家基地
void Game::spawnCampaignWave() {
    const MissionDef& md = missionTable()[campaignMission];
    const MissionWave& w = md.waves[nextWave];
    // 锚点：敌方玩家1的建造厂 > 任意建筑 > 任意单位
    float ax = -1, ay = -1;
    for (auto& e : world.ents)
        if (e.alive && e.player == 1 && e.isBuilding && e.btype == BldType::ConYard) { ax = e.x; ay = e.y; break; }
    if (ax < 0)
        for (auto& e : world.ents)
            if (e.alive && e.player == 1 && e.isBuilding) { ax = e.x; ay = e.y; break; }
    if (ax < 0)
        for (auto& e : world.ents)
            if (e.alive && e.player == 1) { ax = e.x; ay = e.y; break; }
    if (ax < 0) return; // 敌方已无依托，不刷
    // 目标：玩家任意建筑
    float px = ax, py = ay;
    for (auto& e : world.ents)
        if (e.alive && e.player == 0 && e.isBuilding) { px = e.x; py = e.y; break; }
    std::vector<EID> spawned;
    for (UnitType t : w.units) {
        const UnitDef& ud = unitDef(t);
        int dom = ud.pathDomain();
        int bx = -1, by = -1;
        for (int r = 3; r < 16 && bx < 0; r++)
            for (int dy = -r; dy <= r && bx < 0; dy++)
                for (int dx = -r; dx <= r && bx < 0; dx++) {
                    if (std::max(abs(dx), abs(dy)) != r) continue;
                    int nx = (int)ax + dx, ny = (int)ay + dy;
                    if (world.passableFor(nx, ny, dom) && !world.bldBlocked(nx, ny)
                        && world.unitAtCell(nx, ny) == INVALID_EID) { bx = nx; by = ny; }
                }
        if (bx < 0) continue;
        spawned.push_back(world.spawnUnit(1, t, bx + 0.5f, by + 0.5f));
    }
    if (!spawned.empty()) {
        world.orderMove(spawned, px, py, true);
        world.eva(0, TR(S::EvaWaveIncoming));
    }
}

// ===================== P7：战役触发器 =====================

