#include "game_regression.h"
#include "game/campaign.h"
#include "game/campaign_save.h"
#include "game/application_save_format.h"
#include "game/world_save_format.h"
#include <cstdlib>
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
#ifdef RA2_TESTING
#include "legacy_v7_fixture.h"
#endif
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

#ifdef RA2_TESTING
bool writeLegacyV18(const World& world, FILE* file);
bool writeLegacyV19(const World& world, FILE* file);
bool writeLegacyV20(const World& world, FILE* file);
bool writeLegacyV22(const World& world, FILE* file);
#endif

int GameRegression::statePersistenceTest() {
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
    check(bytes.size() > 8 && memcmp(bytes.data(), "RA2WLD60", 8) == 0,
          "current schema is explicit v60");
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
        World expected=sample;expected.aircraftSpawnersEnabled=false;expected.aswEnabled=false;expected.nativeFlightEnabled=false;expected.nativeJumpjetEnabled=false;expected.veterancyProgressEnabled=false;expected.independentSupportEnabled=false;expected.supportDeliveryEnabled=false;
        check(migrated && restored.checksum()==expected.checksum(),"frozen v22 binary migrates with empty traffic gates and historical aircraft behavior");
    }
    {
        FILE* file=tmpfile();World restored;
        bool migrated=file && writeLegacyV19(sample,file);
        if(migrated) { rewind(file);migrated=restored.loadGame(file); }
        if(file) fclose(file);
        World expected=sample;expected.aircraftSpawnersEnabled=false;expected.aswEnabled=false;expected.nativeFlightEnabled=false;expected.nativeJumpjetEnabled=false;expected.veterancyProgressEnabled=false;expected.independentSupportEnabled=false;expected.supportDeliveryEnabled=false;
        check(migrated && restored.checksum()==expected.checksum(),"frozen v19 binary migrates with empty pursuit failures, exact production work and historical aircraft behavior");
    }
    {
        World combat=sample;combat.map.bldOccRef=&combat.bldOcc;
        EID victim=combat.spawnUnit(1,UnitType::MCV,30.5f,30.5f);
        combat.damage(victim,100000,-1);
        FILE* file=tmpfile();World restored;
        bool migrated=file && combat.ents[victim].hp<0 && writeLegacyV19(combat,file);
        if(migrated) { rewind(file);migrated=restored.loadGame(file); }
        if(file) fclose(file);
        World expected=combat;expected.aircraftSpawnersEnabled=false;expected.aswEnabled=false;expected.nativeFlightEnabled=false;expected.nativeJumpjetEnabled=false;expected.veterancyProgressEnabled=false;expected.independentSupportEnabled=false;expected.supportDeliveryEnabled=false;
        check(migrated && restored.checksum()==expected.checksum(),"v19 overkill death state remains readable with the migrated historical simulation checksum");
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
        future[7] = '?';
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
        bool detached=false,sourceReused=false,replacementReused=false;
        if(mcv!=INVALID_EID) {
            float x=lifecycle.ents[mcv].x,y=lifecycle.ents[mcv].y;
            const auto generation=lifecycle.ents[mcv].generation;
            const EID yard=lifecycle.orderDeploy(mcv);
            detached=lifecycle.valid(yard) && lifecycle.ents[yard].isBuilding && yard!=mcv
                && !lifecycle.valid(mcv) && lifecycle.unitAtCell((int)x,(int)y)!=mcv;
            if(detached) {
                // Conversion quarantines the retired source through callbacks.
                // After the operation it must be reusable, with no stale links.
                Vec2i cell{-1,-1};
                for(int dy=-5;dy<=5 && cell.x<0;++dy)for(int dx=-5;dx<=5;++dx) {
                    const int cx=(int)x+dx,cy=(int)y+dy;
                    if(lifecycle.map.inBounds(cx,cy) && lifecycle.passableFor(cx,cy,0) && !lifecycle.bldBlocked(cx,cy)
                        && !lifecycle.cellHardBlockedForMove(cx,cy,INVALID_EID)){cell={cx,cy};break;}
                }
                if(cell.x>=0) {
                    const EID tank=lifecycle.spawnUnit(0,UnitType::Grizzly,cell.x+.5f,cell.y+.5f);
                    sourceReused=lifecycle.valid(tank) && tank==mcv && lifecycle.ents[tank].generation==generation+1
                        && lifecycle.ents[tank].occNext!=tank && lifecycle.unitAtCell(cell.x,cell.y)==tank;
                    lifecycle.kill(tank,false);
                }
                const auto yardGeneration=lifecycle.ents[yard].generation;
                lifecycle.kill(yard,false);
                const EID replacement=lifecycle.spawnUnit(0,UnitType::Grizzly,x,y);
                replacementReused=lifecycle.valid(replacement) && replacement==yard && lifecycle.ents[replacement].generation==yardGeneration+1
                    && lifecycle.ents[replacement].occNext!=replacement && lifecycle.unitAtCell((int)x,(int)y)==replacement;
            }
        }
        check(detached,"MCV conversion detaches occupancy and quarantines its source slot through callbacks");
        check(sourceReused && replacementReused,"both retired conversion slots can be reused with fresh generations and valid occupancy links");
    }
    TraceLog(failures == 0 ? LOG_INFO : LOG_ERROR, "STATE TEST SUMMARY: failures=%d", failures);
    return failures;
}


int GameRegression::appPersistenceTest() {
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
    // Optional local acceptance uses a copy of an actual user save. Outputs
    // go only to the explicit test directory; the input is never overwritten.
    if(const char* input=std::getenv("OPENRA2_LEGACY_APP_TEST_INPUT")) {
        const char* output=std::getenv("OPENRA2_LEGACY_APP_TEST_OUTPUT");
        check(output && *output,"actual legacy import has an explicit output directory");
        if(!output || !*output)return failures;
        const auto directory=std::filesystem::u8path(output);
        std::filesystem::create_directories(directory);
        const auto converted=utf8(directory/"imported-app11.sav");
        const auto roundtrip=utf8(directory/"roundtrip-app11.sav");
        const auto source=readFile(input);
        check(source.size()>8 && !memcmp(source.data(),"RA2GAME6",8),"actual input retains its GAME6 envelope");
        const auto sourcePath=std::filesystem::weakly_canonical(std::filesystem::u8path(input));
        check(sourcePath!=std::filesystem::weakly_canonical(std::filesystem::u8path(converted))
              && sourcePath!=std::filesystem::weakly_canonical(std::filesystem::u8path(roundtrip)),
              "conversion output differs from its source");
        if(failures)return failures;
        const bool loaded=loadGameFile(input);check(loaded,"actual GAME6/World29 skirmish restores");
        if(!loaded)return failures;
        check(saveGameFile(converted.c_str()),"actual legacy state writes a separate current snapshot");
        check(loadGameFile(converted.c_str()) && saveGameFile(roundtrip.c_str()) && readFile(converted)==readFile(roundtrip),
              "converted application state roundtrips byte-for-byte");
        std::vector<uint32_t> hashes;std::vector<std::vector<SkirmishAI>> decisions;
        for(int frame=0;frame<300;++frame){logic();hashes.push_back(world.checksum());decisions.push_back(ais);}
        bool same=loadGameFile(converted.c_str());
        if(same)for(int frame=0;frame<300;++frame){logic();same &= world.checksum()==hashes[frame] && ais==decisions[frame];}
        check(same,"actual converted state repeats 300 ticks with identical simulation and AI state");
        check(readFile(input)==source,"actual input remains byte-for-byte unchanged");
        return failures;
    }
    selectRulesChoice(RulesChoice::Fusion); // World29 migration fixtures predate source profiles.
    cfgAI=2;cfgCountry=(int)Country::America;cfgMapSize=64;cfgMapType=0;
    cfgGameMode=(int)SkirmishMode::Battle;
    msg="previous match";msgTimer=4;evaLines={{"old warning",true}};wasLowPower=true;
    newGame(20261031);
    check(evaLines.empty() && !wasLowPower && msg!="previous match" && msgTimer>0,
          "new match discards old subtitles and edge state while keeping its introduction");
    {
        const auto previousInput=sim;
        const float previousX=camX,previousY=camY;
        const int previousLock=camEdgeLock;
        sim={};sim.active=true;sim.pos={500,400};sim.keysDown.insert(KEY_RIGHT);
        auto pan=[&](int speed,int frames) {
            gameSpeed=speed;camX=-200;camY=400;camEdgeLock=frames+1;
            for(int frame=0;frame<frames;++frame)updateCamera(1.f/frames);
            return camX+200;
        };
        const float slow=pan(0,30),normal=pan(1,60),fast=pan(2,120);
        check(slow>0 && std::abs(slow-normal)<0.1f && std::abs(normal-fast)<0.1f,
              "camera pans at the same real-time speed at slow/normal/fast and 30/60/120 FPS");
        sim.keysDown.clear();sim.pos={-100,-100};camX=-200;camY=400;camEdgeLock=0;
        updateCamera(1.f/60);const bool outside=camX==-200 && camY==400;
        sim.pos={1,400};updateCamera(1.f/60);
        check(outside && camX<-200,"only an in-window pointer can trigger edge scrolling");
        sim=previousInput;camX=previousX;camY=previousY;camEdgeLock=previousLock;gameSpeed=1;
    }
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
    check(bytes.size()>8 && memcmp(bytes.data(),"RA2GAM11",8)==0,"application v11 preserves match setup, native actors and frozen scenery frames");
#ifdef RA2_TESTING
    {
        FILE* oldFog=tmpfile();const bool written=oldFog && writeFogKnowledge(oldFog);
        const auto earliest=written?makeLegacyV6Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto legacy=written?makeLegacyV7Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto previous=written?makeLegacyV8Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto nativePrevious=written?makeLegacyV9Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto layeredPrevious=written?makeLegacyV10Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto bridgePrevious=written?makeLegacyWorld32Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto repairPrevious=written?makeLegacyWorld33Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto gatePrevious=written?makeLegacyWorld34Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto eventPrevious=written?makeLegacyWorld35Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto teamPrevious=written?makeLegacyWorld36Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto rocketPrevious=written?makeLegacyWorld37Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto ordersPrevious=written?makeLegacyWorld38Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto attackPrevious=written?makeLegacyWorld39Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto weaponsPrevious=written?makeLegacyWorld40Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto missionPrevious=written?makeLegacyWorld41Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto controlPrevious=written?makeLegacyWorld42Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        const auto productionPrevious=written?makeLegacyWorld43Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        World migratedBaseline=world;migratedBaseline.map.bldOccRef=&migratedBaseline.bldOcc;
        migratedBaseline.aircraftSpawnersEnabled=false;migratedBaseline.aswEnabled=false;migratedBaseline.nativeFlightEnabled=false;migratedBaseline.nativeJumpjetEnabled=false;migratedBaseline.veterancyProgressEnabled=false;migratedBaseline.independentSupportEnabled=false;migratedBaseline.supportDeliveryEnabled=false;
        for(auto& entity:migratedBaseline.ents)entity.generation=1;
        const auto migratedChecksum=migratedBaseline.checksum();
        const auto conditionsPrevious=written?makeLegacyWorld45Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        check(!conditionsPrevious.empty() && writeFile(bad,conditionsPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous World45 restores without original condition extension");
        const auto aiPrevious=written?makeLegacyWorld44Fixture(bytes,oldFog,world.map.cells.size()):std::vector<uint8_t>{};
        check(!aiPrevious.empty() && writeFile(bad,aiPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v44 restore without native crate bonuses");
        if(oldFog)fclose(oldFog);
        check(!earliest.empty() && writeFile(bad,earliest) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "GAME6 restores its script-free skirmish envelope and World29 state");
        if(!earliest.empty()) {
            const auto reject=[&](std::vector<uint8_t> damaged,const char* label) {
                check(writeFile(bad,damaged) && !loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,label);
            };
            auto campaign=earliest;const int mission=0;memcpy(campaign.data()+8,&mission,sizeof(mission));
            reject(std::move(campaign),"GAME6 campaign envelope is rejected atomically");
            auto tuple=earliest;const std::string worldMagic="RA2WRLDT";
            const auto found=std::search(tuple.begin(),tuple.end(),worldMagic.begin(),worldMagic.end());
            check(found!=tuple.end(),"GAME6 fixture contains its independently generated World29 header");
            if(found!=tuple.end()){found[7]='U';reject(std::move(tuple),"unverified GAME6/World30 combination is rejected atomically");}
            auto truncated=earliest;truncated.pop_back();reject(std::move(truncated),"truncated GAME6 is rejected atomically");
            auto trailing=earliest;trailing.push_back(0);reject(std::move(trailing),"trailing GAME6 bytes are rejected atomically");
        }
        check(!legacy.empty() && writeFile(bad,legacy) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v7 and nested World v29 restore without native-mask fields");
        check(!previous.empty() && writeFile(bad,previous) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v8 and nested World v30 restore without native building identities");
        check(!nativePrevious.empty() && writeFile(bad,nativePrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v9 and nested World v31 restore without frozen scenery frames");
        check(!layeredPrevious.empty() && writeFile(bad,layeredPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v31 restore without bridge surface fields");
        check(!bridgePrevious.empty() && writeFile(bad,bridgePrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v32 restore without the new engineer intent");
        check(!repairPrevious.empty() && writeFile(bad,repairPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v33 restore without native gate state");
        check(!gatePrevious.empty() && writeFile(bad,gatePrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v34 restore without native event instances");
        check(!eventPrevious.empty() && writeFile(bad,eventPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v35 restore without team script focus");
        check(!teamPrevious.empty() && writeFile(bad,teamPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v36 restore without native rocket state");
        check(!rocketPrevious.empty() && writeFile(bad,rocketPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v37 restore without native team-order state");
        check(!ordersPrevious.empty() && writeFile(bad,ordersPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v38 restore without deferred team attack advance");
        check(!attackPrevious.empty() && writeFile(bad,attackPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v39 restore without conventional native weapon slot references");
        check(!weaponsPrevious.empty() && writeFile(bad,weaponsPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v40 restore without native mission terminal state");
        check(!missionPrevious.empty() && writeFile(bad,missionPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v41 restore without native team success state");
        check(!controlPrevious.empty() && writeFile(bad,controlPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v42 restore without native production selectors and identity sequence");
        check(!productionPrevious.empty() && writeFile(bad,productionPrevious) && loadGameFile(bad.c_str()) && world.checksum()==migratedChecksum && ais==savedAis,
              "previous application v10 and nested World v43 restore without native AI scheduler and factory state");
    }
#endif
    check(loadGameFile(path.c_str()) && world.checksum()==savedChecksum && ais==savedAis,
          "the current snapshot restores its original identities after legacy-migration checks");
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
    for(int speed=0;speed<3;++speed) {
        gameSpeed=speed;
        const auto state=world.checksum();
        const bool written=saveGameFile(path.c_str());gameSpeed=(speed+1)%3;
        check(written && loadGameFile(path.c_str()) && gameSpeed==speed && world.checksum()==state,
              "each selectable simulation speed survives application save/load");
    }
    gameSpeed=1;
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
        const std::string magic="RA2WLD60";
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
