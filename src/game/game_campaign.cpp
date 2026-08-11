#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include "core/content.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

void Game::openMissionBrief(int mission) {
    if (mission < 0 || mission >= (int)missionTable().size()) return;
    pendingMission = mission;
    ensureBriefArt(missionTable()[mission]);
    phase = Phase::MissionBrief;
}

void Game::unloadBriefArt() {
    if (briefArtLoaded && briefArtTex.id) UnloadTexture(briefArtTex);
    briefArtTex = {};
    briefArtLoaded = false;
    briefArtPath.clear();
}

void Game::ensureBriefArt(const MissionDef& md) {
    if (md.briefArt.empty()) {
        unloadBriefArt();
        return;
    }
    if (briefArtLoaded && briefArtPath == md.briefArt) return;
    unloadBriefArt();
    std::string resolved = contentResolve(md.briefArt.c_str());
    const char* p = resolved.empty() ? md.briefArt.c_str() : resolved.c_str();
    if (!FileExists(p)) return;
    Image img = LoadImage(p);
    if (!img.data) return;
    briefArtTex = LoadTextureFromImage(img);
    SetTextureFilter(briefArtTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    briefArtLoaded = briefArtTex.id != 0;
    if (briefArtLoaded) briefArtPath = md.briefArt;
}

void Game::newCampaignGame(int mission, bool prepareRender) {
    const MissionDef& md = missionTable()[mission];
    campaignMission = mission;
    pendingMission = mission;
    nextWave = 0;
    missionTriggers = md.triggers; // 运行时副本（fired/armed 可变）
    objectiveText.clear();
    campaignObjDone.assign(md.objectives.size(), false);
    campRuntime.reset(md);
    campRuntime.setPhase(md.startPhase);
    if (!md.objectives.empty()) {
        const auto& o0 = md.objectives[0];
        objectiveText = (g_lang && !o0.textEn.empty()) ? o0.textEn : o0.text;
    }
    std::vector<Faction> factions;
    factions.push_back(md.playerFaction);
    for (Faction f : md.aiFactions) factions.push_back(f);
    // 固定种子：战役地图可复现；手工地图关卡从 maps/xxx.txt 加载地形与预置实体
    world.init(md.mapSize, md.mapSize, 20260723ull + mission * 977, 1, (int)md.aiFactions.size(), factions, md.mapType,
               md.mapFile.empty() ? nullptr : md.mapFile.c_str(), md.noStartForce);
    // 战役国家：显式 Country 优先，否则阵营默认首国
    for (int i = 0; i < world.numPlayers; i++) {
        if (i == 0 && md.playerCountry != Country::None)
            world.players[i].country = md.playerCountry;
        else
            world.players[i].country = countriesOf(world.players[i].faction).front();
    }
    // 科技门
    world.campaignTechGate = !md.allowedBuildings.empty() || !md.allowedUnits.empty();
    world.campaignAllowedBlds = md.allowedBuildings;
    world.campaignAllowedUnits = md.allowedUnits;

    int pool[MAX_PLAYERS], pn = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != cfgColor) pool[pn++] = i;
    world.players[0].colorId = cfgColor;
    for (int i = 1; i < world.numPlayers; i++) world.players[i].colorId = pool[(i - 1) % pn];

    // 难度：资金与 AI 强度
    float moneyMul = campaignDifficulty == 0 ? 1.25f : (campaignDifficulty == 2 ? 0.85f : 1.0f);
    int money = (int)(md.money * moneyMul);
    for (int i = 0; i < world.numPlayers; i++) world.players[i].money = money;
    if (campaignDifficulty == 2)
        for (int i = 1; i < world.numPlayers; i++) world.players[i].money = (int)(md.money * 1.15f);

    world.skirmishMode = SkirmishMode::Battle;
    world.cratesEnabled = cfgCrates;
    world.aiAlliance = cfgAlliance;
    world.sharedVision = false;
    world.shortGame = false;
    world.superweaponsEnabled = true;
    ais.assign((int)md.aiFactions.size(), SkirmishAI{});
    for (int i = 0; i < (int)ais.size(); i++) {
        ais[i].reset(i + 1);
        ais[i].difficulty = (AIDiff)std::clamp(campaignDifficulty, 0, 2);
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
        g_sprites.preloadMatch(localPlayer);
        g_script.init(&world, 1, mission);
        g_script.onGameStart();
    }
}

// 战役波次：为敌方玩家1刷出脚本部队，攻击移动至玩家基地
void Game::spawnCampaignWave() {
    const MissionDef& md = missionTable()[campaignMission];
    const MissionWave& w = md.waves[nextWave];
    // 难度：Hard 额外刷一单位（取首单位类型）
    float ax = -1, ay = -1;
    for (auto& e : world.ents)
        if (e.alive && e.player == 1 && e.isBuilding && e.btype == BldType::ConYard) { ax = e.x; ay = e.y; break; }
    if (ax < 0)
        for (auto& e : world.ents)
            if (e.alive && e.player == 1 && e.isBuilding) { ax = e.x; ay = e.y; break; }
    if (ax < 0)
        for (auto& e : world.ents)
            if (e.alive && e.player == 1) { ax = e.x; ay = e.y; break; }
    if (ax < 0) return;
    float px = ax, py = ay;
    for (auto& e : world.ents)
        if (e.alive && e.player == 0 && e.isBuilding) { px = e.x; py = e.y; break; }
    std::vector<UnitType> units = w.units;
    if (campaignDifficulty == 2 && !w.units.empty())
        units.push_back(w.units[0]);
    if (campaignDifficulty == 0 && units.size() > 1)
        units.pop_back();
    std::vector<EID> spawned;
    for (UnitType t : units) {
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
