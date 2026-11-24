#include "game/world.h"
#include "game/native_scene_contract.h"
#include "game/lang.h"
#include "game/script.h"
#include "core/content.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <limits>

namespace {
bool landRushStarts(const World& world,std::vector<Vec2i>& starts) {
    constexpr Vec2i offsets[]={{4,0},{4,4},{0,6},{-4,4},{-6,0},{-4,-4},{0,-6},{4,-4}};
    std::vector<uint8_t> reserved(world.map.cells.size(),0);
    for(int player=0;player<world.numPlayers;++player) {
        const BldDef* foundation=&bldDef(BldType::ConYard);
        if(world.usesRulesProfile()) {
            const int type=world.rulesTypeIndex(player,true,(int)UnitType::MCV);if(type<0)return false;
            World::Ent vehicle;vehicle.nativeIdentity.definition=world.nativeTypes[type];
            const int yard=world.rulesTransformIndex(vehicle);if(yard<0)return false;
            foundation=&world.nativeTypes[yard]->building;
        }
        // Include the ordinary starting guards and the actual source foundation.
        // Reserve the whole patch so simultaneous MCV deployments cannot overlap.
        const int maxX=std::max(3,foundation->w-2),maxY=std::max(3,foundation->h-2);
        const Vec2i preferred{world.map.w/2+offsets[player].x,world.map.h/2+offsets[player].y};
        Vec2i best{-1,-1};int score=std::numeric_limits<int>::max();
        for(int y=3;y+maxY<world.map.h;++y)for(int x=3;x+maxX<world.map.w;++x) {
            const int dx=x-preferred.x,dy=y-preferred.y,distance=dx*dx+dy*dy;if(distance>=score)continue;
            bool open=true;
            for(int sy=-3;sy<=maxY && open;++sy)for(int sx=-3;sx<=maxX;++sx) {
                const int cx=x+sx,cy=y+sy;
                if(reserved[world.cellIdx(cx,cy)] || !world.map.passable(cx,cy)
                    || world.map.at(cx,cy).terrain==Terrain::Bridge || world.map.hasDeck(cx,cy)) {open=false;break;}
            }
            for(int sy=-1;sy<foundation->h-1 && open;++sy)for(int sx=-1;sx<foundation->w-1;++sx)
                if(world.map.at(x+sx,y+sy).height!=world.map.at(x,y).height){open=false;break;}
            if(open){best={x,y};score=distance;}
        }
        if(best.x<0)return false;
        starts[player]=best;
        for(int sy=-3;sy<=maxY;++sy)for(int sx=-3;sx<=maxX;++sx)reserved[world.cellIdx(best.x+sx,best.y+sy)]=1;
    }
    return true;
}
}

bool World::init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType,
                 const char* mapFile, bool noStartForce, bool requireMap,const char* rulesFile,const std::vector<Country>& countries,const RulesSnapshot* rulesSnapshot) {
    if(updateInProgress || entityMutationDepth>0 || areaDamageDepth>0)return false;
    presentationEpoch=nextPresentationEpoch();
    nativeScenarioSource.clear();nativeTypes.clear();nativeWeaponCatalog.reset();nativeProductionRules={};nativeMovementRules={};nativeProductionSerial=0;std::fill_n(nativeAllies,MAX_PLAYERS,0);
    nativeCrushWarningRules={};nativeTransportUnloadRules={};
    rulesProfileId.clear();rulesProfileName.clear();rulesCountryMask=0;rulesCountryKeys={};rulesCountrySpeed.fill(1);
    resetNativeBridgeScenario();
    map.gateCells.clear();
    resetNativeEvents();
    resetNativeRockets();
    nativeAircraftDefinitions.clear();aircraftSpawners.clear();aircraftSpawnersEnabled=true;aswEnabled=true;nativeFlightEnabled=true;nativeJumpjetEnabled=true;
    veterancyProgressEnabled=independentSupportEnabled=true;
    supportDeliveryEnabled=true;supportMissions.clear();
    std::fill_n(nativeColors,MAX_PLAYERS,0);
    presentationEvents.clear(); presentationSequence = 0;
    repathsThisTick = 0; repathRequests = repathFailures = 0; movementReservations.clear(); resetMovementCorridors();
    std::fill_n(producedUnits, MAX_PLAYERS, 0); std::fill_n(harvestedIncome, MAX_PLAYERS, 0); std::fill_n(combatDamage, MAX_PLAYERS, 0);
    // 全局状态复位：支持局内"重新开始"不留残局
    ents.clear();
    nativeCellSerial=0;
    nativeTemporalEnabled=false;nativeTemporalBeams.clear();nativeCrewRules={};nativeSceneVersion=0;nextCombatActor=1;nativeCrewBursts.clear();
    freeList.clear();
    updateInProgress=false;entityMutationDepth=areaDamageDepth=0;retiredDuringUpdate.clear();trafficGates.clear();
    projs.clear();
    effects.clear();
    nukes.clear();
    crates.clear();
    timedBombs.clear();
    evaQueue.clear();
    tick = 0;
    rng = Rng(seed);
    numPlayers = numHumans + numAI;
    if(!countries.empty() && countries.size()!=(size_t)numPlayers)return false;
    // P7：手工地图（地形/实体/出生点来自 maps/xxx.txt）；加载失败回退程序生成
    std::vector<Vec2i> spawns;
    std::vector<PendingEnt> pend;
    bool hand = false;
    std::string resolvedMap;
    if (mapFile && mapFile[0]) {
        resolvedMap = contentResolve(mapFile);
        if (resolvedMap.empty()) resolvedMap = mapFile;
        const bool nativeMap=resolvedMap.ends_with(".nmap");
        requireMap=requireMap || nativeMap;
        // A native ordinary opening requires an explicit map-bound rules contract.
        hand = nativeMap ? ((noStartForce || rulesSnapshot || rulesFile) && loadNativeMap(resolvedMap.c_str(),map,numPlayers,spawns))
                         : loadHandMap(resolvedMap.c_str(), numPlayers, spawns, pend);
        if (!hand) std::fprintf(stderr, "RA2 hand map failed%s: %s\n", requireMap ? "" : ", fallback to generate", resolvedMap.c_str());
    }
    if (requireMap && !hand) return false;
    if (!hand) map.generate(w, h, seed, numPlayers, spawns, mapType);
    map.initFog(numPlayers);
    bldOcc.assign((size_t)map.w * map.h, -1);
    unitOccHead.assign((size_t)map.w * map.h, INVALID_EID);
    airBucketW = (map.w + AIR_BUCKET - 1) / AIR_BUCKET;
    airOccHead.assign((size_t)airBucketW * ((map.h + AIR_BUCKET - 1) / AIR_BUCKET), INVALID_EID);
    map.bldOccRef = &bldOcc; // 寻路避开建筑占用
    players.assign(numPlayers, Player{});

    for (int i = 0; i < numPlayers; i++) {
        Player& p = players[i];
        p.active = true;
        p.isAI = i >= numHumans;
        p.faction = factions[i % factions.size()];
        if(!countries.empty()) {
            const auto country=countries[i];
            if(country<=Country::None || country>=Country::COUNT || countryFaction(country)!=p.faction)return false;
            p.country=country;
        }
        p.colorId = i;
        p.money = 10000;
        p.name = p.isAI ? ("AI-" + std::to_string(i)) : (g_lang ? "Commander" : "指挥官");
    }
    if(rulesSnapshot ? !loadRulesSnapshot(*rulesSnapshot) : (rulesFile && !loadRulesProfile(rulesFile)))return false;
    if(nativeSceneVersion==nativeSkirmishMapSceneVersion) {
        if(noStartForce || !initializeRulesMapStarts(spawns))return false;
    } else if(map.native.active() && !noStartForce)return false;
    if(!hand && skirmishMode==SkirmishMode::LandRush && !landRushStarts(*this,spawns))return false;
    for(int i=0;i<numPlayers;++i) {
        auto& p=players[i];
        if (hand && noStartForce) continue; // 手工突击队地图：开局部队全部由地图文件放置
        // 出生点：一辆基地车 + 护卫
        Vec2i sp = spawns[i];
        EID mcv = spawnUnit(i, UnitType::MCV, (float)sp.x + 0.5f, (float)sp.y + 0.5f);
        (void)mcv;
        // 初始护卫：必须在 MCV 展开 4×4 脚印外（脚印= (sp-1)..(sp+2)），否则无法展开建造厂
        UnitType tankT = p.faction == Faction::Allies ? UnitType::Grizzly
                       : p.faction == Faction::Soviet ? UnitType::Rhino
                       : p.faction == Faction::Yuri ? UnitType::LasherTank : UnitType::Type99;
        if(!usesRulesProfile() || !nativeProductionRules.sourceModes || modeAllowsUnit(i,tankT)) {
            spawnUnit(i, tankT, sp.x + 3.5f, sp.y + 1.5f);
            spawnUnit(i, tankT, sp.x - 2.5f, sp.y + 1.5f);
        }
        UnitType infT = p.faction == Faction::Allies ? UnitType::GI
                      : p.faction == Faction::Soviet ? UnitType::Conscript
                      : p.faction == Faction::Yuri ? UnitType::Initiate : UnitType::PLA;
        spawnUnit(i, infT, sp.x + 1.5f, sp.y - 2.5f);
        spawnUnit(i, infT, sp.x + 1.5f, sp.y + 3.5f);
        map.reveal(i, sp.x, sp.y, 10);
    }
    if (hand) {
        // 地图文件预置实体（建筑/单位；精炼厂附赠矿车与生产放置一致，保障 AI 经济启动）
        for (const PendingEnt& pe : pend) {
            if (pe.isBld) {
                BldType bt = (BldType)pe.typeIdx;
                EID bid = spawnBuilding(pe.player, bt, pe.x, pe.y, true);
                if (pe.hpOverride > 0 && valid(bid)) ents[bid].hp = pe.hpOverride;
                if (bt == BldType::OreRefinery && pe.player >= 0) {
                    const BldDef& d = bldDef(bt);
                    for (int r = 1; r < 6; r++) {
                        int sx = pe.x + d.w / 2, sy = pe.y + d.h + r - 1;
                        if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                            spawnUnit(pe.player, harvesterType(players[pe.player].faction), sx + 0.5f, sy + 0.5f);
                            break;
                        }
                    }
                }
            } else {
                EID uid = spawnUnit(pe.player, (UnitType)pe.typeIdx, pe.x + 0.5f, pe.y + 0.5f);
                if (pe.hpOverride > 0 && valid(uid)) ents[uid].hp = pe.hpOverride;
                if (pe.guard && valid(uid) && unitDef(ents[uid]).weapon.damage > 0)
                    ents[uid].guard = true;
            }
        }
        // 开局视野：每个玩家以其首个实体为中心揭示（无基地车部队的突击队关卡）
        for (int i = 0; i < numPlayers; i++)
            for (auto& e : ents)
                if (e.alive && e.player == i) { map.reveal(i, (int)e.x, (int)e.y, 10); break; }
    } else {
        placeNeutralTechs();
    }
    initializeNativeHouseControl();
    return true;
}

