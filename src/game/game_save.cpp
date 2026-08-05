#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

#include <cstdio>
#include <filesystem>

bool Game::saveGameFile(const char* path) {
    MakeDirectory("saves");
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite("RA2GAME3", 1, 8, f) == 8;
    auto w = [&](const auto& v) { if (ok && fwrite(&v, sizeof(v), 1, f) != 1) ok = false; };
    w(campaignMission);
    uint64_t nw = (uint64_t)nextWave;
    w(nw);
    w(localPlayer);
    w(camX); w(camY);
    w(gameSpeed);
    // AI 状态（思考计时/进攻波次/海军检测缓存，避免读档后行为跳变）
    uint32_t an = (uint32_t)ais.size();
    w(an);
    for (const SkirmishAI& a : ais) {
        w(a.player); w(a.thinkTimer); w(a.attackWave); w(a.attackTimer); w(a.difficulty);
        w(a.hasWater); w(a.navalPlaceable); w(a.navalCheckCd); w(a.navalFail);
        w(a.personality); w(a.rallyPoint.x); w(a.rallyPoint.y);
    }
    // P7 触发器运行时状态（fired/armed）与 HUD 目标文本
    uint32_t tn = (uint32_t)missionTriggers.size();
    w(tn);
    for (const Trigger& t : missionTriggers) {
        uint8_t fired = t.fired ? 1 : 0, armed = t.armed ? 1 : 0;
        w(fired); w(armed);
    }
    uint32_t ol = (uint32_t)objectiveText.size();
    w(ol);
    if (ok && ol > 0 && fwrite(objectiveText.data(), 1, ol, f) != ol) ok = false;
    if (ok) ok = world.saveGame(f);
    fclose(f);
    return ok;
}

bool Game::loadGameFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = true;
    char magic[8];
    ok = fread(magic, 1, 8, f) == 8
      && (memcmp(magic, "RA2GAME3", 8) == 0 || memcmp(magic, "RA2GAME2", 8) == 0);
    auto r = [&](auto& v) { if (ok && fread(&v, sizeof(v), 1, f) != 1) ok = false; };
    r(campaignMission);
    uint64_t nw = 0;
    r(nw); nextWave = (size_t)nw;
    r(localPlayer);
    r(camX); r(camY);
    r(gameSpeed);
    if (campaignMission < -1 || campaignMission >= (int)missionTable().size()
        || nw > 1000000 || localPlayer < 0 || localPlayer >= MAX_PLAYERS
        || !std::isfinite(camX) || !std::isfinite(camY) || gameSpeed < 1 || gameSpeed > 4) ok = false;
    uint32_t an = 0;
    r(an);
    if (an > MAX_PLAYERS) ok = false;
    if (ok) {
        ais.assign(an, SkirmishAI{});
        for (SkirmishAI& a : ais) {
            r(a.player); r(a.thinkTimer); r(a.attackWave); r(a.attackTimer); r(a.difficulty);
            r(a.hasWater); r(a.navalPlaceable); r(a.navalCheckCd); r(a.navalFail);
            r(a.personality); r(a.rallyPoint.x); r(a.rallyPoint.y);
            if (a.player < 0 || a.player >= MAX_PLAYERS
                || (int)a.difficulty < (int)AIDiff::Easy || (int)a.difficulty > (int)AIDiff::Brutal
                || a.thinkTimer < 0 || a.attackWave < 0 || a.attackTimer < 0
                || a.navalCheckCd < 0 || a.navalFail < 0
                || (int)a.personality < (int)AIPersonality::Balanced
                || (int)a.personality > (int)AIPersonality::Technician) ok = false;
            a.initPersonality(); // 从存档恢复后重建人格参数
        }
    }
    // P7 触发器状态：从任务表重建脚本，再覆盖 fired/armed
    uint32_t tn = 0;
    r(tn);
    if (tn > 256) ok = false;
    if (ok) {
        missionTriggers.clear();
        if (campaignMission >= 0 && campaignMission < (int)missionTable().size())
            missionTriggers = missionTable()[campaignMission].triggers;
        for (uint32_t i = 0; i < tn; i++) {
            uint8_t fired = 0, armed = 0;
            r(fired); r(armed);
            if (ok && i < missionTriggers.size()) {
                missionTriggers[i].fired = fired != 0;
                missionTriggers[i].armed = armed != 0;
            }
        }
        uint32_t ol = 0;
        r(ol);
        if (ol > 1024) ok = false;
        if (ok && ol > 0) {
            objectiveText.resize(ol);
            if (fread(objectiveText.data(), 1, ol, f) != ol) ok = false;
        } else {
            objectiveText.clear();
        }
        World loadedWorld;
        if (ok) ok = loadedWorld.loadGame(f);
        if (ok) {
            world = std::move(loadedWorld);
            world.map.bldOccRef = &world.bldOcc;
        }
    }
    fclose(f);
    if (!ok) return false;
    // 界面状态复位（与开局一致）
    sel.clear();
    selBuilding = INVALID_EID;
    placing = false;
    targetingSW = SWType::COUNT;
    chronoSourceSel.clear();
    targetingParadrop = false;
    sideMode = 0;
    paused = false;
    showMenu = false;
    gameOver = victory = false;
    evaLines.clear();
    phase = Phase::InGame;
    bakeTerrain(); // 读档后按载入地图重烘地表
    g_sprites.preloadMatch(localPlayer);
    return true;
}

int Game::statePersistenceTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "STATE %s: %s", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    World sample;
    sample.init(64, 64, 0x9e3779b97f4a7c15ull, 2, 0,
                {Faction::Allies, Faction::Soviet}, 0);
    sample.skirmishMode = SkirmishMode::UnholyAlliance;
    sample.sharedVision = true;
    sample.shortGame = true;
    sample.players[0].money = 23456;
    sample.players[0].unitProd[0] = {true, true, (int)UnitType::GI, 17, 200, 50, false};
    sample.players[0].unitQueue[0].push_back((int)UnitType::Engineer);
    sample.players[0].swCharge[(int)SWType::ChronoShift] = 321;
    sample.players[0].stormTimer = 12;
    sample.players[0].stormX = 11.25f;
    sample.players[0].stormY = 13.5f;
    sample.map.at(3, 4).terrain = Terrain::Ore;
    sample.map.at(3, 4).ore = 80;
    sample.map.at(3, 4).oreMax = 120;

    EID unit = INVALID_EID;
    for (size_t i = 0; i < sample.ents.size(); ++i)
        if (sample.ents[i].alive && !sample.ents[i].isBuilding && sample.ents[i].player == 0) {
            unit = (EID)i;
            break;
        }
    check(unit != INVALID_EID, "fixture contains a player unit");
    if (unit != INVALID_EID) {
        World::Ent& e = sample.ents[unit];
        e.path = {{(int)e.x, (int)e.y}, {std::min((int)e.x + 1, sample.map.w - 1), (int)e.y}};
        e.pathIdx = 1;
        e.wps.emplace_back(std::min(e.x + 2.0f, (float)sample.map.w - 1), e.y);
        e.cargo.push_back({UnitType::GI, unitDef(UnitType::GI).hp, 0, 0, 0});
        e.garrison.push_back({UnitType::GI, 75, 2, 350, 1});
        e.mindTargets.push_back(unit);
        e.gatlingHeat = 20;
        e.gatlingStage = 1;
    }
    Projectile projectile{};
    projectile.kind = ProjKind::Missile;
    projectile.player = 0;
    projectile.x = 10.0f; projectile.y = 10.0f;
    projectile.tx = 12.5f; projectile.ty = 14.0f;
    projectile.target = unit; projectile.src = unit;
    projectile.speed = 5; projectile.hp = 25;
    projectile.w.damage = 90;
    projectile.w.warhead = WeaponDef::Warhead::AP;
    sample.projs.push_back(projectile);
    sample.nukes.push_back({true, 0, 20.0f, 21.0f, 45});
    sample.crates.push_back({true, 8, 9, 2});
    sample.timedBombs.push_back({15.0f, 16.0f, 90, 0, unit, 400, 2.5f});
    sample.effects.push_back({true, 2, 1.0f, 2.0f, 3.0f, 4.0f, 1, 12, 0, 0, 0});
    sample.evaQueue.push_back({0, "state-roundtrip"});

    std::vector<uint8_t> bytes;
    if (FILE* f = tmpfile()) {
        if (sample.saveGame(f) && fseek(f, 0, SEEK_END) == 0) {
            long length = ftell(f);
            if (length > 0 && fseek(f, 0, SEEK_SET) == 0) {
                bytes.resize((size_t)length);
                if (fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
            }
        }
        fclose(f);
    }
    check(bytes.size() > 8 && memcmp(bytes.data(), "RA2WRLDH", 8) == 0,
          "current schema is explicit v17");
    auto loadBytes = [](const std::vector<uint8_t>& data, World& out) {
        FILE* f = tmpfile();
        if (!f) return false;
        bool ok = fwrite(data.data(), 1, data.size(), f) == data.size()
               && fseek(f, 0, SEEK_SET) == 0 && out.loadGame(f);
        fclose(f);
        return ok;
    };
    World loaded;
    bool loadedOk = !bytes.empty() && loadBytes(bytes, loaded);
    check(loadedOk, "complex v14 state loads");
    check(loadedOk && loaded.checksum() == sample.checksum(), "complex state checksum round-trips");
    check(loadedOk && unit != INVALID_EID && loaded.ents[unit].wps.size() == 1
          && loaded.ents[unit].garrison.size() == 1 && loaded.projs.size() == sample.projs.size()
          && loaded.effects.size() == sample.effects.size() && loaded.evaQueue.size() == 1,
          "paths cargo projectiles and presentation queues round-trip");
    if (loadedOk) {
        World continuedSample = sample;
        World continuedLoaded = loaded;
        for (int i = 0; i < 30; ++i) { continuedSample.update(); continuedLoaded.update(); }
        check(continuedLoaded.checksum() == continuedSample.checksum(),
              "round-tripped simulation continues deterministically");
    }

    if (bytes.size() > 1) {
        std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 1);
        World rejected;
        check(!loadBytes(truncated, rejected), "truncated save is rejected");
    }
    if (bytes.size() > 8) {
        std::vector<uint8_t> future = bytes;
        future[7] = 'X';
        World rejected;
        check(!loadBytes(future, rejected), "unsupported schema is rejected");
    }
    const size_t firstCell = 8 + sizeof(uint64_t) + sizeof(int) + sizeof(uint64_t)
                           + 2 * sizeof(bool) + sizeof(uint8_t) + 4 * sizeof(bool)
                           + 2 * sizeof(int);
    if (bytes.size() > firstCell) {
        std::vector<uint8_t> invalidEnum = bytes;
        invalidEnum[firstCell] = 0xff;
        World rejected;
        check(!loadBytes(invalidEnum, rejected), "invalid terrain enum is rejected");
    }

    const uint32_t baseline = sample.checksum();
    auto differs = [&](auto mutate) {
        World changed = sample;
        mutate(changed);
        return changed.checksum() != baseline;
    };
    check(differs([](World& w) { ++w.players[0].money; }), "checksum covers player economy");
    check(differs([](World& w) { w.players[0].unitQueue[0].push_back((int)UnitType::Spy); }),
          "checksum covers production queues");
    check(unit != INVALID_EID && differs([&](World& w) { w.ents[unit].goalX += 0.25f; }),
          "checksum covers entity targets and paths");
    check(differs([](World& w) { ++w.projs[0].w.damage; }), "checksum covers projectiles");
    check(differs([](World& w) { ++w.players[0].swCharge[0]; }), "checksum covers superweapons");
    check(differs([](World& w) { ++w.map.at(3, 4).ore; }), "checksum covers map resources");
    check(differs([](World& w) { ++w.map.at(3, 4).height; }), "checksum covers map cell height");
    check(differs([](World& w) { w.skirmishMode = SkirmishMode::Megawealth; }), "checksum covers game mode");
    check(differs([](World& w) { w.mcvRepacks = !w.mcvRepacks; }), "checksum covers MCV Repacks option");
    check(differs([](World& w) { w.rng.next(); }), "checksum covers full RNG state");
    check(differs([](World& w) { ++w.timedBombs[0].timer; }), "checksum covers timed simulation objects");
    check(differs([](World& w) { w.ents[0].autoHarvest = !w.ents[0].autoHarvest; }), "checksum covers autoHarvest");

    TraceLog(failures == 0 ? LOG_INFO : LOG_ERROR, "STATE TEST SUMMARY: failures=%d", failures);
    return failures;
}

