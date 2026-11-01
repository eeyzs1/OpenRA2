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
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#define CloseWindow CloseWindowWin32
#define ShowCursor ShowCursorWin32
#include <windows.h>
#undef CloseWindow
#undef ShowCursor
#endif

bool Game::saveGameFile(const char* path) {
    syncFogKnowledge();
    MakeDirectory("saves");
    const auto destination=std::filesystem::u8path(path);
    auto staging=destination; staging += ".tmp";
#ifdef _WIN32
    FILE* f = _wfopen(staging.c_str(),L"wb");
#else
    FILE* f = fopen(staging.c_str(),"wb");
#endif
    if (!f) return false;
    bool ok = fwrite("RA2GAME5", 1, 8, f) == 8;
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
        w(a.rallyCheckTimer); w(a.expandTimer); w(a.defenseCheckTimer); w(a.lastAttacker);
        w(a.pcfg.aggression); w(a.pcfg.defensePriority); w(a.pcfg.techRush); w(a.pcfg.econFocus); w(a.pcfg.unitMix);
        w(a.pcfg.rallySize); w(a.pcfg.useEngineers); w(a.pcfg.useSpies); w(a.pcfg.useSuperWeapon);
        w(a.pcfg.expandBase); w(a.pcfg.retreatDamaged); w(a.pcfg.focusFire);
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
    if (ok) ok = writeFogKnowledge(f);
    if (ok) ok = world.saveGame(f);
    if (fflush(f)!=0) ok=false;
    if (fclose(f)!=0) ok=false;
    std::error_code error;
    if (ok) {
#ifdef _WIN32
        ok=MoveFileExW(staging.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        std::filesystem::rename(staging,destination,error); ok=!error;
#endif
    }
    if (!ok) std::filesystem::remove(staging,error);
    return ok;
}

bool Game::loadGameFile(const char* path) {
    const auto source=std::filesystem::u8path(path);
#ifdef _WIN32
    FILE* f=_wfopen(source.c_str(),L"rb");
#else
    FILE* f=fopen(source.c_str(),"rb");
#endif
    if (!f) return false;
    bool ok=true;
    char magic[8]{};
    ok=fread(magic,1,8,f)==8;
    const bool schema5=ok && memcmp(magic,"RA2GAME5",8)==0;
    ok=ok && schema5; // Older application saves lack scout memory; explicitly reject them.
    auto r=[&](auto& v) { if(ok && fread(&v,sizeof(v),1,f)!=1) ok=false; };
    int savedMission=-1,savedPlayer=0,savedSpeed=1;
    uint64_t savedWave=0;
    float savedX=0,savedY=0;
    r(savedMission);r(savedWave);r(savedPlayer);r(savedX);r(savedY);r(savedSpeed);
    if(savedMission < -1 || savedMission >= (int)missionTable().size() || savedWave>1000000
        || savedPlayer<0 || savedPlayer>=MAX_PLAYERS || !std::isfinite(savedX) || !std::isfinite(savedY)
        || savedSpeed<1 || savedSpeed>4) ok=false;
    uint32_t count=0;r(count);
    if(count>MAX_PLAYERS) ok=false;
    std::vector<SkirmishAI> savedAis;
    if(ok) savedAis.resize(count);
    for(auto& ai:savedAis) {
        r(ai.player);r(ai.thinkTimer);r(ai.attackWave);r(ai.attackTimer);r(ai.difficulty);
        r(ai.hasWater);r(ai.navalPlaceable);r(ai.navalCheckCd);r(ai.navalFail);
        r(ai.personality);r(ai.rallyPoint.x);r(ai.rallyPoint.y);
        {
            r(ai.rallyCheckTimer);r(ai.expandTimer);r(ai.defenseCheckTimer);r(ai.lastAttacker);
            r(ai.pcfg.aggression);r(ai.pcfg.defensePriority);r(ai.pcfg.techRush);r(ai.pcfg.econFocus);r(ai.pcfg.unitMix);
            r(ai.pcfg.rallySize);r(ai.pcfg.useEngineers);r(ai.pcfg.useSpies);r(ai.pcfg.useSuperWeapon);
            r(ai.pcfg.expandBase);r(ai.pcfg.retreatDamaged);r(ai.pcfg.focusFire);
        }
        const auto scalar=[](float v) { return std::isfinite(v) && v>=0 && v<=100; };
        if(ai.player<0 || ai.player>=MAX_PLAYERS || ai.thinkTimer<0 || ai.attackWave<0 || ai.attackTimer<0
            || ai.navalCheckCd<0 || ai.navalFail<0 || ai.rallyCheckTimer<0 || ai.expandTimer<0 || ai.defenseCheckTimer<0
            || ai.lastAttacker<INVALID_EID || ai.hasWater < -1 || ai.hasWater>1
            || ai.navalPlaceable < -1 || ai.navalPlaceable>1
            || (int)ai.difficulty>(int)AIDiff::Brutal || (int)ai.personality>(int)AIPersonality::Technician
            || !scalar(ai.pcfg.aggression) || ai.pcfg.aggression==0 || !scalar(ai.pcfg.defensePriority)
            || !scalar(ai.pcfg.techRush) || !scalar(ai.pcfg.econFocus) || ai.pcfg.econFocus==0
            || !scalar(ai.pcfg.unitMix) || ai.pcfg.rallySize<1 || ai.pcfg.rallySize>10000) ok=false;
    }
    uint32_t triggerCount=0;r(triggerCount);
    if(triggerCount>256) ok=false;
    std::vector<Trigger> savedTriggers;
    if(ok && savedMission>=0) savedTriggers=missionTable()[savedMission].triggers;
    if(ok) for(uint32_t i=0;i<triggerCount;++i) {
        uint8_t fired=0,armed=0;r(fired);r(armed);
        if(fired>1 || armed>1) ok=false;
        if(ok && i<savedTriggers.size()) { savedTriggers[i].fired=fired!=0;savedTriggers[i].armed=armed!=0; }
    }
    uint32_t length=0;r(length);
    if(length>1024) ok=false;
    std::string savedObjective;
    if(ok) {
        savedObjective.resize(length);
        if(length && fread(savedObjective.data(),1,length,f)!=length) ok=false;
    }
    FogKnowledge savedKnowledge;
    if(ok) ok=readFogKnowledge(f,savedKnowledge);
    World savedWorld;
    if(ok) ok=savedWorld.loadGame(f);
    if(ok) ok=validateFogKnowledge(savedKnowledge,savedWorld,savedPlayer);
    fclose(f);
    if(ok && savedPlayer>=savedWorld.numPlayers) ok=false;
    if(ok) {
        std::vector<bool> seenAi(savedWorld.numPlayers,false);
        for(const auto& ai:savedAis) {
            if(ai.player>=savedWorld.numPlayers || seenAi[ai.player]
                || ai.lastAttacker >= (EID)savedWorld.ents.size()) { ok=false;break; }
            seenAi[ai.player]=true;
        }
    }
    if(!ok) return false;
    // Commit only after the complete envelope and World have been validated.
    campaignMission=savedMission;nextWave=(size_t)savedWave;localPlayer=savedPlayer;
    camX=savedX;camY=savedY;gameSpeed=savedSpeed;
    ais=std::move(savedAis);missionTriggers=std::move(savedTriggers);objectiveText=std::move(savedObjective);
    world=std::move(savedWorld);world.map.bldOccRef=&world.bldOcc;
    fogKnowledge=std::move(savedKnowledge);fogKnowledge.epoch=world.presentationEpoch;minimapTimer=0;
    moveFeedbackTimer = 0;
    feedbackTerrainHeight=-1;
    combatCues.clear();
    // 界面状态复位（与开局一致）
    sel.clear();
    selBuilding = INVALID_EID;
    clearPlacement();
    targetingSW = SWType::COUNT;
    chronoSourceSel.clear();
    targetingParadrop = false;
    sideMode = 0;
    paused = false;
    showMenu = false;
    gameOver = victory = false;
    resetNotifications();
    phase = Phase::InGame;
    bakeTerrain(); // 读档后按载入地图重烘地表
    g_sprites.preloadMatch(localPlayer);
    return true;
}

#ifdef RA2_TESTING
bool writeLegacyV18(const World& world, FILE* file);
bool writeLegacyV19(const World& world, FILE* file);
bool writeLegacyV20(const World& world, FILE* file);
bool writeLegacyV22(const World& world, FILE* file);
#endif

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
    sample.players[0].unitProd[0].workRemainder=731;
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
    projectile.ox = 10.0f; projectile.oy = 10.0f;
    projectile.z = 0.4f; projectile.rot = 8;
    projectile.arcing = false; projectile.proximity = true;
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
    check(bytes.size() > 8 && memcmp(bytes.data(), "RA2WRLDT", 8) == 0,
          "current schema is explicit v29");
    auto loadBytes = [](const std::vector<uint8_t>& data, World& out) {
        FILE* f = tmpfile();
        if (!f) return false;
        bool ok = fwrite(data.data(), 1, data.size(), f) == data.size()
               && fseek(f, 0, SEEK_SET) == 0 && out.loadGame(f);
        fclose(f);
        return ok;
    };
    auto roundTripState=[](const World& source,World& restored) {
        FILE* file=tmpfile();bool ok=file && source.saveGame(file);
        if(ok) { rewind(file);ok=restored.loadGame(file); }
        if(file) fclose(file);return ok;
    };
    {
        World boxes=sample,restored;boxes.map.bldOccRef=&boxes.bldOcc;boxes.crates.clear();
        for(int kind=0;kind<World::Crate::KIND_COUNT;++kind) boxes.crates.push_back({true,8,9,kind});
        check(roundTripState(boxes,restored) && boxes.checksum()==restored.checksum(),"all eleven generated crate kinds round trip");
        boxes.crates.front().kind=World::Crate::KIND_COUNT;
        check(!roundTripState(boxes,restored),"crate kind outside generated range is rejected");
        World cursor=sample;cursor.map.bldOccRef=&cursor.bldOcc;
        cursor.ents[unit].path.clear();cursor.ents[unit].pathIdx=1;
        check(roundTripState(cursor,restored) && cursor.checksum()==restored.checksum(),"historical empty path cursor loads without changing state");
        cursor.ents[unit].path={{(int)cursor.ents[unit].x,(int)cursor.ents[unit].y}};cursor.ents[unit].pathIdx=2;
        check(!roundTripState(cursor,restored),"out of range cursor on nonempty path is rejected");
        cursor.ents[unit].clearPath();
        check(cursor.ents[unit].path.empty() && cursor.ents[unit].pathIdx==0,"path clearing resets its cursor atomically");
        World spatial=sample;spatial.map.bldOccRef=&spatial.bldOcc;
        EID first=spatial.spawnUnit(0,UnitType::GI,20.5f,20.5f);
        spatial.spawnUnit(0,UnitType::GI,20.5f,20.5f);spatial.spawnUnit(0,UnitType::GI,20.5f,20.5f);
        spatial.ents[first].x=21.5f;spatial.unitOccSync(first);
        spatial.ents[first].x=20.5f;spatial.unitOccSync(first);
        EID aircraft=spatial.spawnUnit(0,UnitType::Kirov,30.5f,30.5f);
        spatial.spawnUnit(0,UnitType::Kirov,30.5f,30.5f);spatial.spawnUnit(0,UnitType::Kirov,30.5f,30.5f);
        spatial.ents[aircraft].x=35.5f;spatial.airOccSync(aircraft);
        spatial.ents[aircraft].x=30.5f;spatial.airOccSync(aircraft);
        bool canonical=roundTripState(spatial,restored) && spatial.unitOccHead==restored.unitOccHead && spatial.airOccHead==restored.airOccHead;
        if(canonical) for(size_t i=0;i<spatial.ents.size();++i)
            canonical &= spatial.ents[i].occNext==restored.ents[i].occNext && spatial.ents[i].airNext==restored.ents[i].airNext;
        check(canonical && spatial.unitAtCell(20,20)==first,"ground and air index chain order survives exit reentry and save rebuild");
    }
#ifdef RA2_TESTING
    {
        FILE* file=tmpfile();World restored;
        bool migrated=file && writeLegacyV22(sample,file);
        if(migrated) { rewind(file);migrated=restored.loadGame(file); }
        if(file) fclose(file);
        check(migrated && restored.checksum()==sample.checksum(),"frozen v22 binary migrates with empty traffic gates");
    }
    {
        FILE* file=tmpfile();World restored;
        bool migrated=file && writeLegacyV19(sample,file);
        if(migrated) { rewind(file);migrated=restored.loadGame(file); }
        if(file) fclose(file);
        check(migrated && restored.checksum()==sample.checksum(),"frozen v19 binary migrates with empty pursuit failures and exact production work");
    }
    {
        World combat=sample;combat.map.bldOccRef=&combat.bldOcc;
        EID victim=combat.spawnUnit(1,UnitType::MCV,30.5f,30.5f);
        combat.damage(victim,100000,-1);
        FILE* file=tmpfile();World restored;
        bool migrated=file && combat.ents[victim].hp<0 && writeLegacyV19(combat,file);
        if(migrated) { rewind(file);migrated=restored.loadGame(file); }
        if(file) fclose(file);
        check(migrated && restored.checksum()==combat.checksum(),"v19 overkill death state remains readable without changing its checksum");
        combat.ents[unit].hp=-1;
        file=tmpfile();bool rejected=file && combat.saveGame(file);
        if(rejected) { rewind(file);rejected=!restored.loadGame(file); }
        if(file) fclose(file);
        check(rejected,"negative HP on a living entity is still rejected");
    }
    // Exercise the archived writer, not a patched v19 header masquerading as v18.
    {
        World legacy=sample;
        legacy.map.bldOccRef=&legacy.bldOcc;
        legacy.players[0].bldProd={true,false,(int)BldType::PowerPlant,999,800,800,true,false};
        legacy.players[0].unitProd[0].held=true;
        auto legacyBytes=[](const World& source) {
            std::vector<uint8_t> result;
            if(FILE* file=tmpfile()) {
                if(writeLegacyV18(source,file) && std::fseek(file,0,SEEK_END)==0) {
                    long size=std::ftell(file);
                    if(size>0 && std::fseek(file,0,SEEK_SET)==0) {
                        result.resize((size_t)size);
                        if(std::fread(result.data(),1,result.size(),file)!=result.size()) result.clear();
                    }
                }
                std::fclose(file);
            }
            return result;
        };
        const auto fixture=legacyBytes(legacy);
        World migrated;
        bool ok=!fixture.empty() && loadBytes(fixture,migrated);
        const int64_t expected=int64_t(unitDef(UnitType::GI).buildTime)*1024*50/200;
        check(ok && std::memcmp(fixture.data(),"RA2WRLDI",8)==0,
              "archived baseline v18 binary loads");
        check(ok && migrated.players[0].money==legacy.players[0].money
              && migrated.players[0].unitProd[0].paid==50 && migrated.players[0].unitProd[0].held
              && int64_t(migrated.players[0].unitProd[0].progress)*1024
                 +migrated.players[0].unitProd[0].workRemainder==expected,
              "v18 migration preserves funds and HOLD and converts paid fraction");
        check(ok && migrated.players[0].bldProd.ready
              && migrated.players[0].bldProd.progress==bldDef(BldType::PowerPlant).buildTime,
              "v18 ready building remains fully completed");
        if(ok) {
            const Credits money=migrated.players[0].money;
            migrated.cancelUnitProd(0,UnitType::GI);
            check(migrated.players[0].money==money+50,"migrated order refunds exact historic payment");
        }
        legacy.players[0].unitProd[0].progress=-1;
        World corrupted;
        check(!loadBytes(legacyBytes(legacy),corrupted),"v18 corrupt negative progress is rejected before conversion");
    }
#endif
    World loaded;
    bool loadedOk = !bytes.empty() && loadBytes(bytes, loaded);
    check(loadedOk, "complex v29 state loads");
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

    check(differs([](World& w) { ++w.players[0].unitProd[0].workRemainder; }),
          "checksum covers fractional production work");
    World silent = sample, consuming = sample;
    bool replayEqual = true;
    for (int frame = 0; frame < 120; ++frame) {
        silent.update(); consuming.update();
        consuming.presentationEvents.clear();
        replayEqual = replayEqual && silent.checksum() == consuming.checksum();
    }
    check(replayEqual, "event consumption does not alter tick-by-tick simulation");
    World eventSample = sample;
    const auto beforeEvent = eventSample.checksum();
    eventSample.emitSoundAt(Sfx::Cannon, 12.5f, 13.5f, unit);
    eventSample.emitSound(Sfx::Ready);
    check(eventSample.checksum() == beforeEvent
          && eventSample.presentationEvents.back().sequence > eventSample.presentationEvents.front().sequence,
          "presentation events are ordered and excluded from simulation checksum");
    if (loadedOk) {
        World uninterrupted = sample;
        bool equal = true;
        for (int frame = 0; frame < 120; ++frame) {
            uninterrupted.update(); loaded.update();
            equal = equal && uninterrupted.checksum() == loaded.checksum();
        }
        check(equal, "save restoration continues identical tick-by-tick simulation");
    }
    {
        World lifecycle;
        lifecycle.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0);
        EID mcv=INVALID_EID;
        for(size_t i=0;i<lifecycle.ents.size();++i)
            if(lifecycle.ents[i].alive && lifecycle.ents[i].player==0 && lifecycle.ents[i].utype==UnitType::MCV) { mcv=(EID)i;break; }
        bool detached=false,reused=false;
        if(mcv!=INVALID_EID) {
            float x=lifecycle.ents[mcv].x,y=lifecycle.ents[mcv].y;
            lifecycle.orderDeploy(mcv);
            detached=lifecycle.ents[mcv].isBuilding && lifecycle.unitAtCell((int)x,(int)y)!=mcv;
            lifecycle.kill(mcv,false);
            EID tank=lifecycle.spawnUnit(0,UnitType::Grizzly,x,y);
            reused=lifecycle.ents[tank].occNext!=tank && lifecycle.unitAtCell((int)x,(int)y)==tank;
        }
        check(detached && reused,"MCV consumption detaches occupancy before immediate slot reuse");
    }
    TraceLog(failures == 0 ? LOG_INFO : LOG_ERROR, "STATE TEST SUMMARY: failures=%d", failures);
    return failures;
}


int Game::appPersistenceTest() {
    int failures=0;
    auto check=[&](bool ok,const char* name) {
        TraceLog(ok?LOG_INFO:LOG_ERROR,"APP_STATE %s: %s",ok?"PASS":"FAIL",name);
        failures+=!ok;
    };
    const auto stem=std::filesystem::temp_directory_path()/
        ("openra2-app-state-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto utf8=[](const std::filesystem::path& path) { auto text=path.u8string();return std::string(text.begin(),text.end()); };
    const std::string path=utf8(stem)+".sav",bad=path+".bad",legacyPath=path+".v3";
    auto readFile=[](const std::string& file) {
        std::vector<uint8_t> data;
        if(FILE* f=fopen(file.c_str(),"rb")) {
            if(fseek(f,0,SEEK_END)==0) {
                long size=ftell(f);
                if(size>0 && fseek(f,0,SEEK_SET)==0) {
                    data.resize((size_t)size);
                    if(fread(data.data(),1,data.size(),f)!=data.size()) data.clear();
                }
            }
            fclose(f);
        }
        return data;
    };
    auto writeFile=[](const std::string& file,const std::vector<uint8_t>& data) {
        FILE* f=fopen(file.c_str(),"wb");if(!f) return false;
        bool ok=fwrite(data.data(),1,data.size(),f)==data.size();
        return fclose(f)==0 && ok;
    };
    cfgAI=2;cfgCountry=(int)Country::America;cfgMapSize=64;cfgMapType=0;
    cfgGameMode=(int)SkirmishMode::Battle;
    msg="previous match";msgTimer=4;evaLines={{"old warning",true}};wasLowPower=true;
    newGame(20261031);
    check(evaLines.empty() && !wasLowPower && msg!="previous match" && msgTimer>0,
          "new match discards old subtitles and edge state while keeping its introduction");
    for(int i=0;i<600;++i) logic();
    for(auto& ai:ais) {
        ai.rallyCheckTimer=13;ai.expandTimer=197;ai.defenseCheckTimer=29;ai.lastAttacker=0;
        ai.pcfg.aggression=1.25f;ai.pcfg.econFocus=1.5f;ai.pcfg.rallySize=5;
        ai.pcfg.retreatDamaged=true;
    }
    const auto savedAis=ais;
    const auto savedChecksum=world.checksum();
    check(saveGameFile(path.c_str()),"complete application snapshot writes");
    const auto bytes=readFile(path);
    check(bytes.size()>8 && memcmp(bytes.data(),"RA2GAME5",8)==0,"application format is explicit v5");
    World expected=world;expected.map.bldOccRef=&expected.bldOcc;
    auto expectedAis=ais;
    for(int i=0;i<17;++i) logic();
    msg="before load";msgTimer=4;evaLines={{"old warning",true}};wasLowPower=true;
    bool loaded=loadGameFile(path.c_str());
    check(loaded && msg.empty() && msgTimer==0 && evaLines.empty() && !wasLowPower,
          "successful load resets transient notifications and re-arms low-power detection");
    check(loaded && world.checksum()==savedChecksum && ais==savedAis,
          "all AI cooldowns and runtime personality parameters restore exactly");
    bool same=loaded;
    if(loaded) for(int i=0;i<1000;++i) {
        expected.update();for(auto& ai:expectedAis) ai.update(expected);
        logic();
        same &= world.checksum()==expected.checksum() && ais==expectedAis;
    }
    check(same,"application save resumes identical simulation and AI for 1000 ticks");
    const auto beforeWorld=world.checksum();const auto beforeAis=ais;
    const auto beforeObjective=objectiveText;const auto beforePhase=phase;
    const auto beforeMission=campaignMission;const auto beforeWave=nextWave;
    const auto beforePlayer=localPlayer;const auto beforeX=camX,beforeY=camY;
    msg="keep on failed load";msgTimer=3;evaLines={{"keep queued",true}};wasLowPower=true;
    auto unchanged=[&]() {
        return world.checksum()==beforeWorld && ais==beforeAis && objectiveText==beforeObjective
            && phase==beforePhase && campaignMission==beforeMission && nextWave==beforeWave
            && localPlayer==beforePlayer && camX==beforeX && camY==beforeY
            && msg=="keep on failed load" && msgTimer==3 && wasLowPower
            && evaLines.size()==1 && evaLines.front().text=="keep queued" && evaLines.front().powerWarning;
    };
    if(bytes.size()>32) {
        auto truncated=bytes;truncated.pop_back();
        check(writeFile(bad,truncated) && !loadGameFile(bad.c_str()) && unchanged(),
              "truncated application snapshot leaves live match untouched");
        auto invalidPlayer=bytes;const int player=MAX_PLAYERS-1;
        memcpy(invalidPlayer.data()+8+sizeof(int)+sizeof(uint64_t),&player,sizeof(player));
        check(writeFile(bad,invalidPlayer) && !loadGameFile(bad.c_str()) && unchanged(),
              "local player outside loaded world is rejected transactionally");
        const std::string magic="RA2WRLDT";
        auto invalidWorld=bytes;
        auto pos=std::search(invalidWorld.begin(),invalidWorld.end(),magic.begin(),magic.end());
        bool rejected=false;
        if(pos!=invalidWorld.end()) {
            const int invalidCount=-1;
            size_t offset=(size_t)(pos-invalidWorld.begin())+8+sizeof(uint64_t);
            memcpy(invalidWorld.data()+offset,&invalidCount,sizeof(invalidCount));
            rejected=writeFile(bad,invalidWorld) && !loadGameFile(bad.c_str()) && unchanged();
        }
        check(rejected,"invalid nested world count is rejected without allocating invalid sizes");
    }
    const auto validFile=readFile(path);
    const auto effects=world.effects;
    world.effects.resize(8193); // Force the World writer to reject the snapshot.
    bool preserved=!saveGameFile(path.c_str()) && readFile(path)==validFile;
    world.effects=effects;
    check(preserved,"failed save preserves previous complete save file");
#ifdef RA2_TESTING
    // Outer v3 layout copied from baseline, with its independently frozen v18 body.
    bool legacyOk=false;
    if(FILE* f=fopen(legacyPath.c_str(),"wb")) {
        legacyOk=fwrite("RA2GAME3",1,8,f)==8;
        auto put=[&](const auto& value) { if(legacyOk && fwrite(&value,sizeof(value),1,f)!=1) legacyOk=false; };
        put(campaignMission);uint64_t wave=nextWave;put(wave);put(localPlayer);put(camX);put(camY);put(gameSpeed);
        uint32_t count=(uint32_t)ais.size();put(count);
        for(const auto& ai:ais) {
            put(ai.player);put(ai.thinkTimer);put(ai.attackWave);put(ai.attackTimer);put(ai.difficulty);
            put(ai.hasWater);put(ai.navalPlaceable);put(ai.navalCheckCd);put(ai.navalFail);
            put(ai.personality);put(ai.rallyPoint.x);put(ai.rallyPoint.y);
        }
        count=0;put(count);put(count); // no skirmish triggers or objective text
        if(legacyOk) legacyOk=writeLegacyV18(world,f);
        if(fclose(f)!=0) legacyOk=false;
    }
    check(legacyOk && !loadGameFile(legacyPath.c_str()),
          "older application saves without scout memory are explicitly rejected");
#endif
    std::error_code error;
    for(const auto& file:{path,bad,legacyPath,path+".tmp"}) std::filesystem::remove(std::filesystem::u8path(file),error);
    TraceLog(failures?LOG_ERROR:LOG_INFO,"APP STATE SUMMARY: failures=%d",failures);
    return failures;
}
