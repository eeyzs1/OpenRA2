#include "game_regression.h"
#include "core/content.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>
#include <cstdlib>
#include "game/lang.h"
#include "gfx/sprites.h"
#include "gfx/assets.h"
#include "gfx/vxl.h"
#include "sfx/sound.h"

// Natural-start endurance is deliberately separate from fixture-based mechanisms.
int GameRegression::skirmishRegression(int ticks, int factionFilter, uint64_t seedFilter, bool requireOutcomes, int generatedMapType) {
    int failures = 0, completedMatches=0, testedMatches=0;
    for (int faction = 0; faction < 4; ++faction) {
        if(factionFilter>=0 && factionFilter!=faction) continue;
        for (uint64_t seed : {20260722ull, 20261031ull, 0x12345678ull}) {
            if(seedFilter && seedFilter!=seed) continue;
            ++testedMatches;
            World w;
            w.init(64, 64, seed, 0, 2, {(Faction)faction, (Faction)((faction+1)%4)},
                generatedMapType<0?0:generatedMapType, generatedMapType<0?"maps/skirmish-reference.txt":"");
            std::printf("MATCH_MAP generated=%d faction=%d seed=%llu\n",generatedMapType,faction,(unsigned long long)seed);
            SkirmishAI bots[2];
            bots[0].reset(0); bots[1].reset(1);
            bool built = false, harvested = false, produced = false, fought = false;
            uint64_t lastIncome[2]={},lastProduction[2]={};
            uint64_t lastDamage[2]={};int lastWaves[2]={};
            int dryWindows[2]={},idleWindows[2]={};
            bool healthy=true;
            World resumed;SkirmishAI resumedBots[2];
            int resumeTicks=0,snapshots=0;bool snapshotsEqual=true;
            uint64_t resolvedTick=0;
            std::vector<double> timings;
            timings.reserve(ticks);
            for (int frame = 0; frame < ticks; ++frame) {
                const auto start = std::chrono::steady_clock::now();
                w.update();
                for (auto& bot : bots) {
                    bot.update(w);
                }
                if(resumeTicks>0) {
                    resumed.update();for(auto& bot:resumedBots) bot.update(resumed);
                    bool equal=resumed.checksum()==w.checksum();
                    if(!equal) {
                        std::printf("NATURAL_RESUME FAIL faction=%d seed=%llu tick=%llu reason=checksum ai_equal=%d rng=%llu/%llu\n",faction,(unsigned long long)seed,(unsigned long long)w.tick,bots[0]==resumedBots[0] && bots[1]==resumedBots[1],(unsigned long long)w.rng.s,(unsigned long long)resumed.rng.s);
                        if(factionFilter>=0 && seedFilter) {
                            std::filesystem::create_directories("review_out");
                            if(FILE* file=fopen("review_out/natural_before.bin","wb")) { w.saveGame(file);fclose(file); }
                            if(FILE* file=fopen("review_out/natural_after.bin","wb")) { resumed.saveGame(file);fclose(file); }
                            for(size_t i=0;i<std::min(w.ents.size(),resumed.ents.size());++i) {
                                const auto& a=w.ents[i];const auto& b=resumed.ents[i];
                                if(a.alive!=b.alive || a.x!=b.x || a.y!=b.y || a.state!=b.state || a.target!=b.target || a.hp!=b.hp || a.pathIdx!=b.pathIdx || a.path.size()!=b.path.size())
                                    std::printf("RESUME_ENTITY id=%zu alive=%d/%d pos=%f,%f/%f,%f state=%d/%d target=%d/%d hp=%d/%d path=%d,%zu/%d,%zu\n",i,a.alive,b.alive,a.x,a.y,b.x,b.y,(int)a.state,(int)b.state,a.target,b.target,a.hp,b.hp,a.pathIdx,a.path.size(),b.pathIdx,b.path.size());
                            }
                            for(int pi=0;pi<2;++pi) std::printf("RESUME_PLAYER id=%d money=%lld/%lld power=%d,%d/%d,%d\n",pi,(long long)w.players[pi].money,(long long)resumed.players[pi].money,w.players[pi].powerMade,w.players[pi].powerUsed,resumed.players[pi].powerMade,resumed.players[pi].powerUsed);
                        }
                    }
                    snapshotsEqual &= equal;
                    resumeTicks=equal?resumeTicks-1:0;
                    resumed.presentationEvents.clear();resumed.evaQueue.clear();
                }
                if(!resolvedTick && w.matchOutcome(0)!=MatchOutcome::Running) resolvedTick=w.tick;
                timings.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count());
                built |= w.hasBld(0, BldType::ConYard) && w.hasBld(0, BldType::OreRefinery);
                harvested |= w.harvestedIncome[0] > 0;
                produced |= w.producedUnits[0] > 0;
                fought |= bots[0].attackWave > 0;
                if ((frame+1)%3600==0) for(int pi=0;pi<2;++pi) {
                    const auto& p=w.players[pi];
                    uint64_t income=w.harvestedIncome[pi]-lastIncome[pi];
                    uint64_t production=w.producedUnits[pi]-lastProduction[pi];
                    lastIncome[pi]=w.harvestedIncome[pi]; lastProduction[pi]=w.producedUnits[pi];
                    int miners=0,ore=0;bool receiver=w.hasBld(pi,BldType::OreRefinery),blocked=false;
                    for(const auto& e:w.ents) if(e.alive && !e.isBuilding && e.player==pi) {
                        if(unitDef(e.utype).canHarvet()) ++miners;
                        if(e.utype==UnitType::SlaveMiner && e.deployed) receiver=true;
                    }
                    for(const auto& c:w.map.cells) ore+=c.ore;
                    for(const auto& item:p.unitProd) blocked |= item.active && item.ready;
                    const bool factory=w.hasBld(pi,BldType::WarFactory);
                    const bool capable=p.active && w.matchOutcome(pi)==MatchOutcome::Running && frame>=3600;
                    dryWindows[pi]=capable && miners && receiver && ore && income==0 ? dryWindows[pi]+1:0;
                    idleWindows[pi]=capable && factory && p.money>=1000 && production==0 ? idleWindows[pi]+1:0;
                    healthy &= dryWindows[pi]<2 && idleWindows[pi]<2;
                    if(dryWindows[pi]==1 && factionFilter>=0 && seedFilter) {
                        Vec2i focus{w.map.w/2,w.map.h/2};
                        for(const auto& unit:w.ents)if(unit.alive && !unit.isBuilding && unit.player==pi && unitDef(unit.utype).canHarvet()){focus={(int)unit.x,(int)unit.y};break;}
                        const int left=std::clamp(focus.x-7,0,w.map.w-15),top=std::clamp(focus.y-7,0,w.map.h-15);
                        for(int y=top;y<top+15;++y) {
                            std::string row;
                            for(int x=left;x<left+15;++x) {
                                EID unit=w.unitAtCell(x,y);char tile=w.map.passable(x,y)?'.':'~';
                                if(w.bldBlocked(x,y))tile='#';
                                if(w.valid(unit))tile=unit<10?'0'+unit:'u';
                                row+=tile;
                            }
                            std::printf("AI_TRAFFIC_MAP player=%d x=%d y=%d %s\n",pi,left,y,row.c_str());
                        }
                    }
                    if(dryWindows[pi]>0) for(size_t i=0;i<w.ents.size();++i) {
                        const auto& miner=w.ents[i];
                        if(!miner.alive || miner.isBuilding || miner.player!=pi || !unitDef(miner.utype).canHarvet()) continue;
                        std::printf("AI_MINER faction=%d seed=%llu tick=%llu player=%d id=%zu pos=%.2f,%.2f state=%d ore=%d,%d cargo=%d auto=%d blocked=%d dock=%d\n",
                            faction,(unsigned long long)seed,(unsigned long long)w.tick,pi,i,miner.x,miner.y,(int)miner.state,
                            miner.oreCell.x,miner.oreCell.y,miner.oreLoad,miner.autoHarvest,miner.blockTick,miner.dockRefinery);
                        if(factionFilter>=0 && seedFilter && miner.pathIdx<(int)miner.path.size()) {
                            const auto next=miner.path[miner.pathIdx],goal=miner.path.back();
                            EID blocker=w.unitAtCell(next.x,next.y);
                            std::printf("AI_MINER_BLOCKER unit=%zu next=%d,%d goal=%d,%d building=%d blocker=%d player=%d type=%d state=%d guard=%d blocked=%d path=%d/%zu\n",i,next.x,next.y,goal.x,goal.y,w.bldAt(next.x,next.y),blocker,
                                w.valid(blocker)?w.ents[blocker].player:-1,w.valid(blocker)?(int)w.ents[blocker].utype:-1,w.valid(blocker)?(int)w.ents[blocker].state:-1,
                                w.valid(blocker)?w.ents[blocker].guard:0,w.valid(blocker)?w.ents[blocker].blockTick:0,w.valid(blocker)?w.ents[blocker].pathIdx:0,w.valid(blocker)?w.ents[blocker].path.size():0);
                            std::vector<EID> traced;
                            for(int depth=0;depth<12 && w.valid(blocker);++depth) {
                                if(std::find(traced.begin(),traced.end(),blocker)!=traced.end())break;
                                traced.push_back(blocker);const auto& unit=w.ents[blocker];
                                if(unit.pathIdx>=(int)unit.path.size())break;
                                const auto step=unit.path[unit.pathIdx],destination=unit.path.back();
                                EID occupant=w.unitAtCell(step.x,step.y);
                                const auto wait=std::adjacent_find(unit.path.begin()+unit.pathIdx,unit.path.end());
                                const bool dwell=wait!=unit.path.end();
                                std::printf("AI_TRAFFIC_CHAIN id=%d pos=%.2f,%.2f state=%d next=%d,%d goal=%d,%d unit=%d building=%d dwell=%d wait_offset=%d wait_cell=%d,%d\n",blocker,unit.x,unit.y,(int)unit.state,step.x,step.y,destination.x,destination.y,occupant,w.bldAt(step.x,step.y),dwell,dwell?(int)(wait-unit.path.begin())-unit.pathIdx:-1,dwell?wait->x:-1,dwell?wait->y:-1);
                                blocker=occupant;
                            }
                        }
                    }
                    if(idleWindows[pi]>0) for(size_t i=0;i<w.ents.size();++i) {
                        const auto& b=w.ents[i];
                        if(!b.alive || !b.isBuilding || b.player!=pi || b.btype!=BldType::WarFactory) continue;
                        const auto& def=bldDef(b.btype);
                        int x=(int)b.x+def.w/2,y=(int)b.y+def.h;
                        EID unit=w.unitAtCell(x,y),building=w.bldAt(x,y);
                        std::printf("AI_FACTORY faction=%d seed=%llu tick=%llu player=%d factory=%zu exit=%d,%d blocked_bld=%d btype=%d blocked_unit=%d utype=%d state=%d\n",
                            faction,(unsigned long long)seed,(unsigned long long)w.tick,pi,i,x,y,building,
                            w.valid(building)?(int)w.ents[building].btype:-1,unit,
                            w.valid(unit)?(int)w.ents[unit].utype:-1,w.valid(unit)?(int)w.ents[unit].state:-1);
                    }
                    int army=0,moving=0,chasing=0,attacking=0;
                    for(const auto& e:w.ents) if(e.alive && !e.isBuilding && e.player==pi && !unitDef(e.utype).canHarvet()
                        && e.utype!=UnitType::MCV && e.utype!=UnitType::Engineer && e.utype!=UnitType::Spy) {
                        ++army;moving+=e.state==UState::Moving || e.state==UState::AttackMoving;
                        chasing+=e.state==UState::Chasing;attacking+=e.state==UState::Attacking;
                    }
                    if(factionFilter>=0 && seedFilter)for(size_t target=0;target<w.ents.size();++target) {
                        const auto& building=w.ents[target];
                        if(!building.alive || !building.isBuilding || !w.isEnemy(pi,building.player))continue;
                        const int attackers=(int)std::count_if(w.ents.begin(),w.ents.end(),[&](const auto& actor) {
                            return actor.alive && !actor.isBuilding && actor.player==pi && actor.target==(EID)target;
                        });
                        if(attackers)std::printf("AI_TARGET tick=%llu attacker=%d id=%zu owner=%d type=%d hp=%d repairing=%d attackers=%d\n",
                            (unsigned long long)w.tick,pi,target,building.player,(int)building.btype,building.hp,building.repairing,attackers);
                    }
                    std::printf("AI_HEALTH faction=%d seed=%llu player=%d tick=%llu income=%llu produced=%llu damage=%llu money=%lld miners=%d receiver=%d factory=%d low_power=%d blocked=%d dry=%d idle=%d defeated=%d damage_delta=%llu waves=%d waves_delta=%d army=%d moving=%d chasing=%d attacking=%d rally=%d,%d\n",
                        faction,(unsigned long long)seed,pi,(unsigned long long)w.tick,(unsigned long long)income,
                        (unsigned long long)production,(unsigned long long)w.combatDamage[pi],(long long)p.money,miners,receiver,factory,
                        p.lowPower(),blocked,dryWindows[pi],idleWindows[pi],p.defeated,
                        (unsigned long long)(w.combatDamage[pi]-lastDamage[pi]),bots[pi].attackWave,bots[pi].attackWave-lastWaves[pi],
                        army,moving,chasing,attacking,bots[pi].rallyPoint.x,bots[pi].rallyPoint.y);
                    for(int category=0;category<PROD_CAT_N;++category) {
                        const auto& order=p.unitProd[category];
                        constexpr UnitType probes[]={UnitType::GI,UnitType::Grizzly,UnitType::Intruder,UnitType::AmphTransport};
                        const int facilityType=order.active?order.typeIdx:(int)probes[category];
                        auto facilities=w.productionFacilities(pi,true,facilityType);
                        std::printf("AI_PRODUCTION player=%d tick=%llu category=%d active=%d type=%d progress=%d cost=%d paid=%d held=%d ready=%d queued=%zu facility_type=%d facilities=%d operational=%d\n",pi,(unsigned long long)w.tick,category,
                            order.active,order.active?order.typeIdx:-1,order.progress,order.totalCost,order.paid,order.held,order.ready,p.unitQueue[category].size(),facilityType,facilities.present,facilities.operational);
                    }
                    for(size_t id=0;id<w.ents.size();++id) {
                        const auto& boat=w.ents[id];if(!boat.alive || boat.isBuilding || boat.player!=pi || boat.utype!=UnitType::AmphTransport)continue;
                        int incoming=0;for(const auto& actor:w.ents)incoming+=actor.alive && actor.player==pi && actor.state==UState::Boarding && actor.target==(EID)id;
                        const auto goal=boat.path.empty()?Vec2i{-1,-1}:boat.path.back();
                        std::printf("AI_TRANSPORT_STATE player=%d tick=%llu id=%zu pos=%.2f,%.2f state=%d load=%d passengers=%zu incoming=%d goal=%d,%d blocked=%d\n",pi,(unsigned long long)w.tick,id,boat.x,boat.y,(int)boat.state,World::cargoLoad(boat.cargo),boat.cargo.size(),incoming,goal.x,goal.y,boat.blockTick);
                    }
                    lastDamage[pi]=w.combatDamage[pi];lastWaves[pi]=bots[pi].attackWave;
                    std::fflush(stdout);
                }
                w.presentationEvents.clear(); w.evaQueue.clear();
                if((frame+1)%9000==0 && frame+301<ticks) {
                    FILE* file=tmpfile();bool written=file && w.saveGame(file),parsed=false;
                    if(written) { rewind(file);parsed=resumed.loadGame(file); }
                    if(file) fclose(file);
                    bool loaded=parsed && resumed.checksum()==w.checksum();
                    snapshotsEqual &= loaded;
                    if(loaded) { resumedBots[0]=bots[0];resumedBots[1]=bots[1];resumeTicks=300;++snapshots; }
                    std::printf("NATURAL_SNAPSHOT %s faction=%d seed=%llu tick=%llu written=%d parsed=%d original=%u restored=%u\n",loaded?"PASS":"FAIL",faction,(unsigned long long)seed,(unsigned long long)w.tick,written,parsed,w.checksum(),resumed.checksum());
                    if(!loaded && factionFilter>=0 && seedFilter) {
                        std::filesystem::create_directories("review_out");
                        if(FILE* diagnostic=fopen("review_out/natural_before.bin","wb")) { w.saveGame(diagnostic);fclose(diagnostic); }
                        if(parsed) if(FILE* diagnostic=fopen("review_out/natural_after.bin","wb")) { resumed.saveGame(diagnostic);fclose(diagnostic); }
                        for(int pi=0;pi<2;++pi) std::printf("NATURAL_POWER player=%d before=%d/%d after=%d/%d\n",pi,w.players[pi].powerMade,w.players[pi].powerUsed,resumed.players[pi].powerMade,resumed.players[pi].powerUsed);
                    }
                }
                // Full-match verification stops after natural resolution and
                // any pending checkpoint comparison; endurance keeps its window.
                if(requireOutcomes && resolvedTick && resumeTicks==0) break;
            }
            std::sort(timings.begin(), timings.end());
            bool ok = built && harvested && produced && fought && healthy && snapshotsEqual && w.combatDamage[0]>0
                && (!requireOutcomes || resolvedTick>0);
            std::printf("NATURAL_RESUME %s faction=%d seed=%llu checkpoints=%d replay_ticks_each=300\n",snapshotsEqual?"PASS":"FAIL",faction,(unsigned long long)seed,snapshots);
            completedMatches+=resolvedTick>0;
            if(!resolvedTick && requireOutcomes)if(const char* directory=std::getenv("OPENRA2_MATCH_DIAGNOSTICS")) {
                const auto output=std::filesystem::u8path(directory);std::filesystem::create_directories(output);
                const auto stem="faction-"+std::to_string(faction)+"-seed-"+std::to_string(seed)+"-map-"+std::to_string(generatedMapType);
                const auto state=(output/(stem+"-world60.sav")).string();FILE* file=std::fopen(state.c_str(),"wb");
                bool saved=file && w.saveGame(file);if(file)std::fclose(file);
                Game captured;captured.world=w;captured.world.map.bldOccRef=&captured.world.bldOcc;captured.ais={bots[0],bots[1]};
                const auto application=(output/(stem+"-app11.sav")).string();
                const bool app=captured.saveGameFile(application.c_str());
                std::printf("NATURAL_DIAGNOSTIC world=%d app=%d directory=%s\n",saved,app,directory);
            }
            if(!resolvedTick && factionFilter>=0 && seedFilter)for(int pi=0;pi<2;++pi) {
                std::printf("AI_STRATEGY player=%d knowledge=%zu rally_size=%d focus=%d timer=%d rally_timer=%d\n",pi,bots[pi].knownBuildings.size(),bots[pi].pcfg.rallySize,bots[pi].pcfg.focusFire,bots[pi].attackTimer,bots[pi].rallyCheckTimer);
                for(const auto& known:bots[pi].knownBuildings) {
                    bool visible=false;const auto& shape=bldDef(known.type);
                    for(int dy=0;dy<shape.h;++dy)for(int dx=0;dx<shape.w;++dx)visible |= w.map.fogAt(pi,known.x+dx,known.y+dy)==FOG_VISIBLE;
                    std::printf("AI_KNOWN player=%d type=%d owner=%d at=%d,%d seen=%llu visible=%d\n",pi,(int)known.type,known.owner,known.x,known.y,(unsigned long long)known.observedTick,visible);
                }
            }
            if(!resolvedTick && factionFilter>=0 && seedFilter) for(size_t index=0;index<w.ents.size();++index) {
                const auto& e=w.ents[index];if(!e.alive) continue;
                const auto goal=e.path.empty()?Vec2i{-1,-1}:e.path.back();
                std::printf("AI_FINAL id=%zu player=%d building=%d type=%d pos=%.2f,%.2f state=%d deployed=%d path=%d/%zu goal=%d,%d blocked=%d target=%d hp=%d repairing=%d intent=%d intent_owner=%d intent_goal=%.2f,%.2f guard=%d\n",
                    index,e.player,e.isBuilding,e.isBuilding?(int)e.btype:(int)e.utype,e.x,e.y,(int)e.state,e.deployed || e.radDeployed,
                    e.pathIdx,e.path.size(),goal.x,goal.y,e.blockTick,e.target,e.hp,e.repairing,e.attackMove.active,e.attackMove.owner,e.attackMove.x,e.attackMove.y,e.guard);
                if(!e.isBuilding && e.pathIdx<(int)e.path.size() && !unitDef(e.utype).isAir()) {
                    const auto next=e.path[e.pathIdx];const auto& def=unitDef(e.utype);
                    const EID peer=w.unitAtCell(next.x,next.y);
                    if(e.blockTick>1000 || (w.valid(peer) && w.ents[peer].blockTick>1000)) {
                        std::printf("AI_FINAL_ROUTE id=%zu guard=%d occ=%d nodes=",index,e.guard,e.occCell);
                        for(int node=e.pathIdx;node<(int)e.path.size();++node)std::printf(" %d,%d",e.path[node].x,e.path[node].y);
                        std::printf("\n");
                    }
                    int delay=std::max(1,(int)std::ceil(def.speed/g_gameRules.veteranSpeedBonus[std::clamp(e.vetRank,0,2)]));
                    if(e.crateSpeedBoost>0)delay=std::max(1,(int)(delay*0.8f));
                    const float dx=next.x+0.5f-e.x,dy=next.y+0.5f-e.y,length=distf(e.x,e.y,next.x+0.5f,next.y+0.5f);
                    const float fraction=length>0?std::min(1.f,1.f/(delay*length)):0;
                    const int mx=(int)(e.x+dx*fraction),my=(int)(e.y+dy*fraction);
                    std::printf("AI_FINAL_PATH id=%zu next=%d,%d reserved=%d next_unit=%d next_bld=%d here_bld=%d swept=%d,%d swept_unit=%d swept_bld=%d pass=%d climb=%d hard=%d\n",index,next.x,next.y,w.movementReservations[w.cellIdx(next.x,next.y)],w.unitAtCell(next.x,next.y),w.bldAt(next.x,next.y),w.bldAt((int)e.x,(int)e.y),mx,my,w.unitAtCell(mx,my),w.bldAt(mx,my),w.passableFor(mx,my,def.pathDomain()),w.passableStep((int)e.x,(int)e.y,mx,my,def.pathDomain()),w.cellHardBlockedForMove(mx,my,(EID)index));
                }
            }
            failures += !ok;
            std::printf("%s %s faction=%d seed=%llu ticks=%llu limit=%d built=%d income=%d production=%d attack=%d resolved_tick=%llu outcome=%d p95_ms=%.3f repaths=%llu failures=%llu\n",
                requireOutcomes?"FULL_MATCH":"ENDURANCE",ok ? "PASS":"FAIL", faction, (unsigned long long)seed, (unsigned long long)w.tick,ticks, built, harvested, produced, fought,(unsigned long long)resolvedTick,(int)w.matchOutcome(0),
                timings[(timings.size()-1)*95/100], (unsigned long long)w.repathRequests, (unsigned long long)w.repathFailures);
            std::fflush(stdout);
        }
    }
    std::printf("NATURAL_MATCHES completed=%d total=%d require_all=%d\n",completedMatches,testedMatches,requireOutcomes);
    return failures+(testedMatches==0)+((factionFilter<0 && !seedFilter && completedMatches==0)?1:0)
        +(requireOutcomes && completedMatches!=testedMatches);
}

int GameRegression::movementRegression() {
    int failures = 0;
    for (int scenario = 0; scenario < 8; ++scenario) {
        int count = scenario == 2 ? 200 : scenario == 3 ? 500 : 50;
        World w;
        w.init(96,96,20261031,2,0,{Faction::Allies,Faction::Soviet},0);
        for (size_t i=0;i<w.ents.size();++i) if (w.ents[i].alive) w.kill((EID)i,false);
        for (auto& cell : w.map.cells) cell = Cell{};
        w.cratesEnabled=false;
        for(auto& player:w.players) player.defeated=false;
        if (scenario == 1 || scenario == 4) {
            for (int y=0;y<96;++y) if (y!=48) w.map.at(40,y).terrain=Terrain::Water;
        }
        if (scenario==5) for(int x=30;x<=50;++x)
            for(int y=0;y<96;++y) if(y!=48) w.map.at(x,y).terrain=Terrain::Water;
        if (scenario==6) for(int y=30;y<=50;++y)
            for(int x=0;x<96;++x) if(x!=48) w.map.at(x,y).terrain=Terrain::Water;
        std::vector<EID> units;
        for (int i=0;i<count;++i) {
            float x=5.5f+i%20, y=5.5f+i/20;
            if (scenario>=4 && scenario<=6) { int n=i%25; x=(i<25?20.5f:56.5f)+n%5; y=42.5f+n/5; }
            if (scenario==6) std::swap(x,y);
            units.push_back(w.spawnUnit(0,scenario==7 && i%3==0?UnitType::Conscript:UnitType::Grizzly,x,y));
        }
        World replay=w;
        replay.map.bldOccRef=&replay.bldOcc;
        World::Cmd move{};
        move.type=World::Cmd::Move;
        move.ids=units; move.x=60.5f; move.y=60.5f;
        if (scenario>=4 && scenario<=6) {
            move.ids.assign(units.begin(),units.begin()+25);
            w.applyCmd(0,move);replay.applyCmd(0,move);
            move.ids.assign(units.begin()+25,units.end()); move.x=20.5f; move.y=20.5f;
        }
        w.applyCmd(0,move);replay.applyCmd(0,move);
        const bool commandsEqual=w.checksum()==replay.checksum();
        bool legal=true, deterministic=commandsEqual, budget=true,saved=false,saveIsolation=true;
        const int frames=count==50?5000:1000;
        int replayChecks=0,firstMismatch=-1;
        std::vector<double> times;
        for (int frame=0;frame<frames;++frame) {
            auto start=std::chrono::steady_clock::now();
            w.update();
            budget &= w.repathsThisTick<=8;
            times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            w.presentationEvents.clear();
            std::vector<EID> occupied(w.map.cells.size(),INVALID_EID);
            std::vector<int> infantry(w.map.cells.size(),0),vehicles(w.map.cells.size(),0);
            for (EID id:units) {
                const auto& e=w.ents[id]; int x=(int)e.x,y=(int)e.y;
                if (!w.map.passable(x,y) || w.bldBlocked(x,y)) { legal=false; continue; }
                size_t cell=(size_t)y*w.map.w+x;
                const bool foot=unitDef(e.utype).isInfantry();
                if ((foot && (vehicles[cell] || infantry[cell]>=3)) || (!foot && (vehicles[cell] || infantry[cell]))) {
                    if(legal) std::printf("COLLISION scenario=%d frame=%d a=%d b=%d cell=%d,%d pos=%.3f,%.3f other=%.3f,%.3f\n",scenario,frame,id,occupied[cell],x,y,e.x,e.y,w.ents[occupied[cell]].x,w.ents[occupied[cell]].y);
                    legal=false;
                }
                occupied[cell]=id;
                if(foot) ++infantry[cell];else ++vehicles[cell];
            }
            replay.update();replay.presentationEvents.clear();
            budget &= replay.repathsThisTick<=8;
            const bool equal=replay.checksum()==w.checksum();
            if(!equal && firstMismatch<0)firstMismatch=frame+1;
            deterministic &= equal;++replayChecks;
            if(frame==frames/2-1) {
                const auto before=w.checksum();
                FILE* file=tmpfile();saved=file && w.saveGame(file);
                if(saved){rewind(file);saved=replay.loadGame(file);}if(file)fclose(file);
                saveIsolation &= w.checksum()==before;
                deterministic &= saved && replay.checksum()==before;
            }
        }
        int arrived=0;
        for (size_t i=0;i<units.size();++i) {
            EID id=units[i];
            if (!w.valid(id)) continue;
            const auto& e=w.ents[id];
            if (scenario>=4 && scenario<=6 && i>=25) { if(e.x<30 && e.y<35) ++arrived; }
            else if(e.x>50 && e.y>50) ++arrived;
        }
        std::sort(times.begin(),times.end());
        double p95=times[(times.size()-1)*95/100];
        bool ok = legal && deterministic && budget && commandsEqual && saved && saveIsolation && replayChecks==frames && (count!=50 || arrived==count) && p95<=33.3;
        failures+=!ok;
        if(!ok && scenario>=4) for(EID id:units) {
            const auto& e=w.ents[id];auto next=e.pathIdx<(int)e.path.size()?e.path[e.pathIdx]:Vec2i{-1,-1};
            std::printf("LANE_UNIT id=%d pos=%.2f,%.2f state=%d blocked=%d next=%d,%d goal=%.2f,%.2f\n",id,e.x,e.y,(int)e.state,e.blockTick,next.x,next.y,e.goalX,e.goalY);
        }
        std::printf("MOVEMENT %s scenario=%d count=%d arrived=%d legal=%d replay=%d replay_checks=%d first_mismatch=%d commands=%d saved=%d save_isolation=%d budget=%d p95_ms=%.3f repaths=%llu failures=%llu\n",ok?"PASS":"FAIL",scenario,count,arrived,legal,deterministic,replayChecks,firstMismatch,commandsEqual,saved,saveIsolation,budget,p95,
            (unsigned long long)w.repathRequests,(unsigned long long)w.repathFailures);
    }
    for(int scenario=0;scenario<3;++scenario) {
        World w;
        w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0);
        for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) { cell=Cell{};if(scenario!=1) cell.terrain=Terrain::Water; }
        for(int x=2;x<60;++x) w.map.at(x,11).terrain=Terrain::Clear;
        w.cratesEnabled=false;
        EID unit=w.spawnUnit(0,UnitType::Grizzly,5.5f,11.5f);
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={unit};command.x=45;command.y=11;
        w.applyCmd(0,command);
        command.a=1;command.x=50;w.applyCmd(0,command);w.presentationEvents.clear();
        for(int i=0;i<10;++i) w.update();
        if(scenario==2) w.map.at(20,11).terrain=Terrain::Water;
        else w.spawnBuilding(0,BldType::TeslaReactor,20,10,true);
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool legal=true,deterministic=true;int reports=0;
        for(int i=0;i<1200;++i) {
            w.update();replay.update();
            deterministic &= w.checksum()==replay.checksum();
            for(const auto& event:w.presentationEvents)
                if(event.kind==PresentationKind::MoveOrder && event.entity==unit && event.move.rejected) ++reports;
            w.presentationEvents.clear();replay.presentationEvents.clear();
            const auto& e=w.ents[unit];
            legal &= w.map.passable((int)e.x,(int)e.y) && !w.bldBlocked((int)e.x,(int)e.y);
        }
        auto requests=w.repathRequests;
        for(int i=0;i<300;++i) w.update();
        const auto& e=w.ents[unit];
        bool outcome=scenario==1 ? (int)e.x==50 && reports==0
            : e.state==UState::Idle && e.path.empty() && e.wps.empty() && reports==1;
        bool ok=legal && deterministic && outcome && requests==w.repathRequests;
        failures+=!ok;
        std::printf("DYNAMIC_ROUTE %s scenario=%d legal=%d replay=%d outcome=%d reports=%d bounded=%d\n",
            ok?"PASS":"FAIL",scenario,legal,deterministic,outcome,reports,requests==w.repathRequests);
    }
    // Player commands must report their actual outcome, including failed routes.
    World& w=world;
    w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0);
    for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
    for(auto& cell:w.map.cells) cell=Cell{};
    for(int y=0;y<64;++y) w.map.at(30,y).terrain=Terrain::Water;
    EID tank=w.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    EID infantry=w.spawnUnit(0,UnitType::GI,12.5f,10.5f);
    EID enemy=w.spawnUnit(1,UnitType::Grizzly,40.5f,40.5f);
    w.ents[infantry].deployed=true;
    w.presentationEvents.clear(); localPlayer=0;
    World::Cmd command{}; command.type=World::Cmd::Move; command.ids={tank}; command.x=20;command.y=20;
    w.applyCmd(0,command);
    command.a=1;command.x=22;w.applyCmd(0,command);
    bool queued=!w.presentationEvents.empty() && w.presentationEvents.back().move.queued==1 && w.ents[tank].wps.size()==1;
    w.presentationEvents.clear();
    command.a=0;command.x=40;command.y=40;
    w.applyCmd(0,command);
    auto event=w.presentationEvents.back();
    bool fallback=event.kind==PresentationKind::MoveOrder && event.player==0 && event.entity==tank
        && event.tick==w.tick && event.move.accepted==1 && event.move.adjusted==1
        && !w.ents[tank].path.empty() && w.ents[tank].path.back()==Vec2i{29,40} && w.ents[tank].wps.empty();
    auto hash=w.checksum();consumePresentationEvents();
    bool feedback=msg==TR(S::MsgMoveAdjusted) && moveFeedbackTimer>0 && !moveFeedbackRejected && hash==w.checksum();
    uint64_t requests=w.repathRequests;
    for(int i=0;i<1000;++i) w.update();
    bool bounded=w.repathRequests==requests && w.ents[tank].state==UState::Idle
        && (int)w.ents[tank].x==29 && (int)w.ents[tank].y==40;
    w.presentationEvents.clear();
    // A blocked destination may resolve to a reachable adjacent cell.
    w.map.at(20,20).terrain=Terrain::Water;
    command.x=20;command.y=20;w.applyCmd(0,command);
    bool adjusted=w.presentationEvents.back().move.accepted==1 && w.presentationEvents.back().move.adjusted==1;
    consumePresentationEvents();adjusted &= msg==TR(S::MsgMoveAdjusted) && !moveFeedbackRejected;
    command.ids={tank,infantry};command.x=22;command.y=22;w.applyCmd(0,command);
    bool partial=w.presentationEvents.back().move.accepted==1 && w.presentationEvents.back().move.rejected==1;
    consumePresentationEvents();partial &= moveFeedbackPartial && !moveFeedbackRejected;
    command.ids={infantry};command.a=1;w.applyCmd(0,command);
    bool deployed=w.presentationEvents.back().move.reason==MoveRejectReason::Deployed && w.ents[infantry].wps.empty();
    consumePresentationEvents();deployed &= msg==TR(S::MsgMoveDeploy);
    command.ids={tank};command.x=std::numeric_limits<float>::infinity();
    hash=w.checksum();w.applyCmd(0,command);
    bool invalid=w.presentationEvents.back().move.reason==MoveRejectReason::InvalidTarget && w.checksum()==hash;
    consumePresentationEvents();invalid &= msg==TR(S::MsgMoveInvalid) && moveFeedbackTimer==0;
    command.x=42;command.y=42;command.a=0;command.ids={enemy};w.applyCmd(1,command);
    std::string previous=msg;consumePresentationEvents();
    bool filtered=previous==msg;
    w.applyCmd(0,command);filtered &= w.presentationEvents.empty();
    command.ids={tank};command.x=14;command.y=14;w.applyCmd(0,command);
    command.a=1;command.x=18;w.applyCmd(0,command);w.applyCmd(0,command);
    command.x=24;w.applyCmd(0,command);
    bool visitedMiddle=false;
    for(int i=0;i<800;++i) {
        w.update();w.presentationEvents.clear();
        visitedMiddle |= (int)w.ents[tank].x==18 && (int)w.ents[tank].y==14;
    }
    bool route=visitedMiddle && (int)w.ents[tank].x==24 && (int)w.ents[tank].y==14 && w.ents[tank].wps.empty();
    command.a=0;command.x=24;command.y=16;w.applyCmd(0,command);
    command.a=1;command.x=40;command.y=40;w.applyCmd(0,command);
    command.x=22;command.y=22;w.applyCmd(0,command);w.presentationEvents.clear();
    int deferredFailures=0,deferredAdjustments=0;
    const int routeBudget=4*w.map.w*std::max(1,unitDef(w.ents[tank].utype).speed);
    for(int i=0;i<routeBudget;++i) {
        w.update();
        for(const auto& ev:w.presentationEvents) if(ev.kind==PresentationKind::MoveOrder && ev.move.rejected==1) ++deferredFailures;
        for(const auto& ev:w.presentationEvents) if(ev.kind==PresentationKind::MoveOrder && ev.move.adjusted==1) ++deferredAdjustments;
        w.presentationEvents.clear();
        if(w.ents[tank].state==UState::Idle && w.ents[tank].wps.empty())break;
    }
    bool deferred=deferredFailures==0 && deferredAdjustments==1 && w.ents[tank].wps.empty()
        && w.ents[tank].pathIdx>=(int)w.ents[tank].path.size() && w.ents[tank].state==UState::Idle
        && (int)w.ents[tank].x==22 && (int)w.ents[tank].y==22;
    bool ok=queued && fallback && feedback && bounded && adjusted && partial && deployed && invalid && filtered && route && deferred;
    std::printf("MOVE_FEEDBACK %s queued=%d fallback=%d ui=%d bounded=%d adjusted=%d partial=%d deployed=%d invalid=%d player_filter=%d full_route=%d deferred_adjustment=%d\n",
        ok?"PASS":"FAIL",queued,fallback,feedback,bounded,adjusted,partial,deployed,invalid,filtered,route,deferred);
    failures+=!ok;
    return failures;
}

int GameRegression::economyRegression() {
    int failures=0;
    const int regrowth=g_gameRules.oreRegrowRate;
    g_gameRules.oreRegrowRate=0;
    for (int scenario=0;scenario<4;++scenario) {
        World w;
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0);
        for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        w.cratesEnabled=false;
        for(auto& player:w.players) player.defeated=false;
        EID refinery=w.spawnBuilding(0,BldType::OreRefinery,10,10,true);
        if(scenario==1) w.spawnBuilding(0,BldType::OreRefinery,30,10,true);
        EID miner=w.spawnUnit(0,UnitType::WarMiner,16.5f,16.5f);
        auto& first=w.map.at(18,16); first.terrain=Terrain::Ore; first.ore=first.oreMax=3;
        auto& second=w.map.at(24,16); second.terrain=Terrain::Ore; second.ore=second.oreMax=120;
        if(scenario==2) {
            w.spawnUnit(0,UnitType::Grizzly,14.5f,11.5f);
            w.spawnUnit(0,UnitType::Grizzly,14.5f,12.5f);
        }
        w.orderHarvest({miner},18,16);
        bool destroyed=false,rerouted=false,preserved=true;
        int rebuildAt=-1, cargoAtLoss=0;
        for(int frame=0;frame<6000;++frame) {
            w.update(); w.presentationEvents.clear();
            if((scenario==1 || scenario==3) && !destroyed && w.ents[miner].state==UState::HarvestReturn) {
                cargoAtLoss=w.ents[miner].oreLoad;
                w.kill(refinery,false); destroyed=true;
                if(scenario==3) rebuildAt=frame+300;
            }
            if(scenario==3 && frame==rebuildAt) {
                preserved=w.ents[miner].oreLoad==cargoAtLoss && w.harvestedIncome[0]==0;
                w.spawnBuilding(0,BldType::OreRefinery,30,10,true);
            }
            if(scenario==1 && destroyed && w.valid(w.ents[miner].dockRefinery)
                && w.ents[miner].dockRefinery!=refinery) rerouted=true;
        }
        bool ok=w.map.at(18,16).ore==0 && w.map.at(24,16).ore<120 && w.harvestedIncome[0]>0;
        if(scenario==1) ok &= destroyed && rerouted;
        if(scenario==3) ok &= destroyed && preserved && rebuildAt>=0;
        failures+=!ok;
        std::printf("ECONOMY %s scenario=%d first=%d second=%d income=%llu destroyed=%d rerouted=%d state=%d load=%d\n",
            ok?"PASS":"FAIL",scenario,w.map.at(18,16).ore,w.map.at(24,16).ore,
            (unsigned long long)w.harvestedIncome[0],destroyed,rerouted,(int)w.ents[miner].state,w.ents[miner].oreLoad);
    }
    // Factory mouth obstruction must not teleport finished vehicles beyond it.
    {
        World w;
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0);
        for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        w.cratesEnabled=false;
        for(auto& player:w.players) player.defeated=false;
        w.spawnBuilding(0,BldType::TeslaReactor,3,3,true);
        EID factory=w.spawnBuilding(0,BldType::WarFactory,10,10,true);
        const auto& def=bldDef(BldType::WarFactory);
        EID blocker=w.spawnUnit(0,UnitType::Grizzly,10+def.w/2+0.5f,10+def.h+0.5f);
        const Credits initial=w.players[0].money;
        bool started=w.startUnitProd(0,UnitType::Rhino);
        int quote=w.players[0].unitProd[1].totalCost;
        for(int i=0;i<1000;++i) w.update();
        bool blocked=started && w.producedUnits[0]==0 && w.players[0].unitProd[1].active
            && w.players[0].unitProd[1].ready && w.players[0].unitProd[1].paid==quote && w.players[0].money==initial-quote;
        w.kill(blocker,false);
        for(int i=0;i<50;++i) w.update();
        bool released=w.producedUnits[0]==1 && !w.players[0].unitProd[1].active
            && w.players[0].money==initial-quote && w.valid(factory);
        failures+=!(blocked && released);
        std::printf("PRODUCTION %s blocked=%d released=%d produced=%llu debit=%d quote=%d\n",
            blocked&&released?"PASS":"FAIL",blocked,released,(unsigned long long)w.producedUnits[0],(int)(initial-w.players[0].money),quote);
    }
    for(int scenario=0;scenario<3;++scenario) {
        World w;
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0);
        for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.terrain=Terrain::Water; }
        for(int y=2;y<8;++y) for(int x=2;x<8;++x) w.map.at(x,y).terrain=Terrain::Clear;
        for(auto& player:w.players) player.defeated=false;
        w.spawnBuilding(0,BldType::TeslaReactor,3,3,true);
        w.spawnBuilding(0,BldType::NavalYard,10,10,true);w.cratesEnabled=false;
        const auto& def=bldDef(BldType::NavalYard);int sx=10+def.w/2,sy=10+def.h;
        EID blocker=scenario==1 ? w.spawnBuilding(1,BldType::NavalYard,10,sy,true)
            : w.spawnUnit(0,UnitType::Typhoon,sx+0.5f,sy+0.5f);
        // A blocked shipyard means every physical launch edge is sealed.
        // In scenario 1 the opposing yard already seals the south edge.
        for(int dx=0;dx<def.w;++dx) {
            w.spawnBuilding(-1,BldType::Wall,10+dx,9,true);
            if(scenario!=1 && 10+dx!=sx)w.spawnBuilding(-1,BldType::Wall,10+dx,sy,true);
        }
        for(int dy=0;dy<def.h;++dy) {
            w.spawnBuilding(-1,BldType::Wall,9,10+dy,true);
            w.spawnBuilding(-1,BldType::Wall,10+def.w,10+dy,true);
        }
        Credits initial=w.players[0].money;
        bool started=w.startUnitProd(0,UnitType::Typhoon);
        int quote=w.players[0].unitProd[3].totalCost;
        for(int i=0;i<1000;++i) w.update();
        const auto& order=w.players[0].unitProd[3];
        bool held=started && order.active && order.ready && order.paid==quote
            && w.producedUnits[0]==0 && w.players[0].money==initial-quote;
        if(scenario==2) w.cancelUnitProd(0,UnitType::Typhoon);
        w.kill(blocker,false);
        for(int i=0;i<100;++i) w.update();
        bool released=scenario==2 ? w.producedUnits[0]==0 && w.players[0].money==initial
            : w.producedUnits[0]==1 && w.players[0].money==initial-quote && w.unitAtCell(sx,sy+1)!=INVALID_EID;
        bool ok=held && released && !w.players[0].unitProd[3].active;
        if(!ok) for(size_t id=0;id<w.ents.size();++id) {
            const auto& e=w.ents[id];if(e.alive && !e.isBuilding) std::printf("NAVAL_UNIT id=%zu pos=%.2f,%.2f state=%d player=%d\n",id,e.x,e.y,(int)e.state,e.player);
        }
        failures+=!ok;
        std::printf("NAVAL_PRODUCTION %s scenario=%d started=%d held=%d released=%d produced=%llu debit=%d quote=%d\n",
            ok?"PASS":"FAIL",scenario,started,held,released,(unsigned long long)w.producedUnits[0],(int)(initial-w.players[0].money),quote);
    }
    // Real combat drives defeat, then evaluate both player perspectives.
    {
        World w;
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0);
        for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        for(auto& player:w.players) player.defeated=false;
        w.cratesEnabled=false;
        EID tank=w.spawnUnit(0,UnitType::Rhino,12.5f,12.5f);
        EID infantry=w.spawnUnit(1,UnitType::GI,14.5f,12.5f);
        w.map.reveal(0,12,12,10); w.map.reveal(1,14,12,10);
        bool running=w.matchOutcome(0)==MatchOutcome::Running && w.matchOutcome(1)==MatchOutcome::Running;
        World::Cmd attack{}; attack.type=World::Cmd::Attack;
        attack.ids={tank}; attack.a=infantry; w.applyCmd(0,attack);
        attack.ids={infantry}; attack.a=tank; w.applyCmd(1,attack);
        for(int i=0;i<2000 && !w.players[1].defeated;++i) w.update();
        bool result=running && w.players[1].defeated && w.matchOutcome(0)==MatchOutcome::Victory
            && w.matchOutcome(1)==MatchOutcome::Defeat;
        // Reverse roles to catch the historical hard-coded index-0 winner assumption.
        World reverse;
        reverse.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0);
        for(size_t i=0;i<reverse.ents.size();++i)
            if(reverse.ents[i].alive && reverse.ents[i].player==0) reverse.kill((EID)i,false);
        result &= reverse.matchOutcome(1)==MatchOutcome::Victory && reverse.matchOutcome(0)==MatchOutcome::Defeat;
        failures+=!result;
        std::printf("SETTLEMENT %s running=%d loser_defeated=%d winner=%d loser=%d reverse=%d\n",
            result?"PASS":"FAIL",running,w.players[1].defeated,(int)w.matchOutcome(0),(int)w.matchOutcome(1),(int)reverse.matchOutcome(1));
    }
    g_gameRules.oreRegrowRate=regrowth;
    return failures;
}

int GameRegression::presentationReplay(bool sound) {
    if(sound) {
        g_sfx.init(); g_sfx.setMasterVol(0);
        if(!IsAudioDeviceReady() || !g_sfx.assetsOk()) {
            std::fprintf(stderr,"PRESENTATION REPLAY: real audio device/assets unavailable\n");
            return 1;
        }
    }
    const auto& body=g_sprites.unitBody(UnitType::GI,0,0,0);
    Image image=LoadImageFromTexture(body.tex);
    Color* pixels=LoadImageColors(image);
    uint32_t spriteHash=2166136261u;
    for(int i=0;pixels && i<image.width*image.height;++i)
        for(unsigned char channel:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a}) spriteHash=(spriteHash^channel)*16777619u;
    const bool spriteLoaded=pixels!=nullptr;
    if(pixels) UnloadImageColors(pixels);
    UnloadImage(image);
    std::printf("PRESENTATION walk=%d fire=%d mk=%d sprite_hash=%08x audio=%d\n",
        g_sprites.animInfo(UnitType::GI).walk,g_sprites.animInfo(UnitType::GI).fire,
        g_sprites.bldMkFrames(BldType::Barracks),spriteHash,sound);
    std::printf("PRESENTATION_DEP depfire=%d\n",g_sprites.animInfo(UnitType::GI).depFire);
    const auto& cameo=g_sprites.iconUnit(UnitType::Type99,0);
    Image cameoImage=LoadImageFromTexture(cameo.tex);Color* cameoPixels=LoadImageColors(cameoImage);
    uint32_t cameoHash=2166136261u;
    for(int i=0;cameoPixels && i<cameoImage.width*cameoImage.height;++i)
        for(unsigned char value:{cameoPixels[i].r,cameoPixels[i].g,cameoPixels[i].b,cameoPixels[i].a}) cameoHash=(cameoHash^value)*16777619u;
    const bool cameoLoaded=cameoPixels!=nullptr;
    if(cameoPixels) UnloadImageColors(cameoPixels);
    UnloadImage(cameoImage);
    std::printf("PRESENTATION_CAMEO type99=%08x\n",cameoHash);
    const auto& warpSprite=g_sprites.warpEffect(0,0);
    Image warpImage=LoadImageFromTexture(warpSprite.tex);Color* warpPixels=LoadImageColors(warpImage);uint32_t warpHash=2166136261u;
    for(int i=0;warpPixels && i<warpImage.width*warpImage.height;++i)
        for(unsigned char value:{warpPixels[i].r,warpPixels[i].g,warpPixels[i].b,warpPixels[i].a}) warpHash=(warpHash^value)*16777619u;
    const bool warpLoaded=warpPixels!=nullptr;if(warpPixels) UnloadImageColors(warpPixels);UnloadImage(warpImage);
    const auto& siegePose=g_sprites.unitDeployed(UnitType::SiegeChopper,7,2,0);
    Image siegeImage=LoadImageFromTexture(siegePose.tex);Color* siegePixels=LoadImageColors(siegeImage);
    uint32_t siegeHash=2166136261u;const bool siegeLoaded=siegePixels!=nullptr;
    for(int i=0;siegePixels && i<siegeImage.width*siegeImage.height;++i)
        for(uint8_t value:{siegePixels[i].r,siegePixels[i].g,siegePixels[i].b,siegePixels[i].a})siegeHash=(siegeHash^value)*16777619u;
    if(siegePixels)UnloadImageColors(siegePixels);UnloadImage(siegeImage);
    World w;
    w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0);
    for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
    for(auto& cell:w.map.cells) cell=Cell{};
    for(auto& player:w.players) player.defeated=false;
    w.cratesEnabled=false;
    w.spawnBuilding(0,BldType::PowerPlant,5,5,false);
    EID barracks=w.spawnBuilding(0,BldType::Barracks,10,10,false);
    EID soldier=w.spawnUnit(0,UnitType::GI,14.5f,18.5f);
    EID enemy=w.spawnUnit(1,UnitType::Conscript,23.5f,18.5f);
    EID mcv=w.spawnUnit(0,UnitType::MCV,32.5f,32.5f);
    EID passenger=w.spawnUnit(0,UnitType::GI,45.5f,45.5f);
    EID transport=w.spawnUnit(0,UnitType::IFV,47.5f,45.5f);
    EID crew=w.spawnUnit(0,UnitType::GI,40.5f,50.5f);
    EID bunker=w.spawnBuilding(0,BldType::BattleBunker,42,50,true);
    EID repairTank=w.spawnUnit(0,UnitType::Grizzly,51.5f,15.5f);
    EID depot=w.spawnBuilding(0,BldType::ServiceDepot,53,15,true);w.ents[repairTank].hp=100;
    EID magnet=w.spawnUnit(0,UnitType::Magnetron,6.5f,40.5f);
    EID magneticTarget=w.spawnBuilding(1,BldType::ConYard,14,40,true);
    EID chrono=w.spawnUnit(0,UnitType::Chrono,60.5f,50.5f);bool warpEmitted=false;
    EID siege=w.spawnUnit(0,UnitType::SiegeChopper,10.5f,58.5f);
    EID siegeTarget=w.spawnUnit(1,UnitType::MCV,14.5f,58.5f);w.ents[siegeTarget].hp=10000;
    bool siegeIsolated=true;
    EID nighthawk=w.spawnUnit(0,UnitType::Nighthawk,36.5f,58.5f);
    EID gatling=w.spawnUnit(0,UnitType::GatlingTank,25.5f,58.5f);
    EID gatlingTarget=w.spawnUnit(1,UnitType::MCV,29.5f,58.5f);w.ents[gatlingTarget].hp=10000;
    World::Cmd gatlingAttack{};gatlingAttack.type=World::Cmd::Attack;gatlingAttack.ids={gatling};gatlingAttack.a=gatlingTarget;w.applyCmd(0,gatlingAttack);
    bool voxelIsolated=true,voxelFired=false;
    auto poseHash=[](const Sprite& sprite) {
        Image image=LoadImageFromTexture(sprite.tex);Color* pixels=LoadImageColors(image);uint32_t result=2166136261u;
        for(int i=0;pixels && i<image.width*image.height;++i)for(uint8_t c:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a})result=(result^c)*16777619u;
        if(pixels)UnloadImageColors(pixels);UnloadImage(image);return result;
    };
    const uint32_t rotorHash=poseHash(g_sprites.unitBody(UnitType::Nighthawk,0,1,0));
    const uint32_t barrelHash=poseHash(g_sprites.unitTurret(UnitType::GatlingTank,0,0,1));
    const uint32_t apocHash=poseHash(g_sprites.unitTurret(UnitType::Apocalypse,0,0,1));
    EID apoc[3],apocTarget[3],apocNeighbor[3];
    int apocShots[3]={},apocProjectiles[3]={};
    std::vector<int> apocTicks[3];bool apocIsolated=true,apocPhysical=true;
    for(int mode=0;mode<3;++mode) {
        const float x=5.5f+mode*18;
        apoc[mode]=w.spawnUnit(0,UnitType::Apocalypse,x,28.5f);
        const auto victim=mode==2?UnitType::Kirov:UnitType::Conscript;
        apocTarget[mode]=w.spawnUnit(1,victim,x+4,28.5f);
        apocNeighbor[mode]=w.spawnUnit(1,victim,x+4+38/256.f,28.5f);
        for(EID id:{apocTarget[mode],apocNeighbor[mode]}){w.ents[id].hp=10000;w.ents[id].tpSick=1000;}
        w.ents[apoc[mode]].vetRank=mode==1?2:0;
        w.ents[apoc[mode]].turretDir=dirFromVec(4,0);
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={apoc[mode]};attack.a=apocTarget[mode];w.applyCmd(0,attack);
    }
    const uint32_t transformHash=poseHash(g_sprites.chopperTransform(1,0));bool transformed=false,transformIsolated=true;
    const int magneticHp=w.ents[magneticTarget].hp;bool magneticBeam=false;
    World::Cmd magnetic{};magnetic.type=World::Cmd::Attack;magnetic.ids={magnet};magnetic.a=magneticTarget;w.applyCmd(0,magnetic);
    bool serviced=false;
    bool boarded=false,garrisoned=false,unloaded=false,evacuated=false;
    w.map.reveal(0,20,20,25); w.map.reveal(1,20,20,25);
    for(int frame=0;frame<600;++frame) {
        // One complete volley, then hold fire while all launched rounds land.
        if(frame==20)for(EID id:apoc) {
            World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={id};w.applyCmd(0,stop);w.ents[id].tpSick=1000;
        }
        World::Cmd c{};
        if(frame==0) { c.type=World::Cmd::Move;c.ids={soldier};c.x=18.5f;c.y=18.5f; }
        if(frame==10) { c.type=World::Cmd::Deploy;c.ids={siege}; }
        if(frame==15) { c.type=World::Cmd::Attack;c.ids={siege};c.a=siegeTarget; }
        if(frame==500) { c.type=World::Cmd::Deploy;c.ids={siege}; }
        if(frame==20) { c.type=World::Cmd::Board;c.ids={passenger};c.a=transport; }
        if(frame==40) boarded=w.ents[transport].cargo.size()==1;
        if(frame==50) { c.type=World::Cmd::Unload;c.ids={transport}; }
        if(frame==70) unloaded=w.ents[transport].cargo.empty();
        if(frame==110) { c.type=World::Cmd::Garrison;c.ids={crew};c.a=bunker; }
        if(frame==130) garrisoned=w.ents[bunker].garrison.size()==1;
        if(frame==140) { c.type=World::Cmd::Ungarrison;c.ids={bunker}; }
        if(frame==160) evacuated=w.ents[bunker].garrison.empty();
        if(frame==30) { c.type=World::Cmd::RadDeploy;c.ids={soldier}; }
        if(frame==60) { c.type=World::Cmd::Attack;c.ids={soldier};c.a=enemy; }
        if(frame==90) { c.type=World::Cmd::StartUnitProd;c.a=(int)UnitType::GI; }
        if(frame==120) { c.type=World::Cmd::RadDeploy;c.ids={soldier}; }
        if(frame==150) { c.type=World::Cmd::SellBuilding;c.ids={barracks}; }
        if(frame==200) { c.type=World::Cmd::Deploy;c.ids={mcv}; }
        if(frame==220) { c.type=World::Cmd::Service;c.ids={repairTank};c.a=depot; }
        if(frame==240) { c.type=World::Cmd::Move;c.ids={chrono};c.x=55.5f;c.y=55.5f; }
        if(frame==590) serviced=w.ents[repairTank].hp==unitDef(UnitType::Grizzly).hp && w.ents[repairTank].state!=UState::Servicing;
        if(c.type!=World::Cmd::None) w.applyCmd(0,c);
        w.update();
        for(int mode=0;mode<3;++mode) {
            for(const auto& projectile:w.projs)if(projectile.src==apoc[mode] && projectile.trail==1) {
                ++apocProjectiles[mode];apocPhysical &= projectile.kind==(mode==2?ProjKind::Missile:ProjKind::Shell)
                    && projectile.w.cellSpread==(mode==2?.5f:.3f) && projectile.w.percentAtMax==.5f;
            }
            const auto before=w.checksum();const auto& actor=w.ents[apoc[mode]];
            g_sprites.unitTurret(actor.utype,actor.turretDir,0,actor.fireAnim>0?(int)((w.tick/2)%std::max(1,g_sprites.unitTurretFrames(actor.utype))):0);
            apocIsolated &= before==w.checksum();
        }
        if(const int phase=chopperTransformFrame(w.ents[siege]);phase>=0) {
            const auto before=w.checksum();g_sprites.chopperTransform(phase,0);transformed=true;transformIsolated &= before==w.checksum();
        }
        {
            const auto before=w.checksum();
            for(EID id:{nighthawk,siege}) {
                const auto& actor=w.ents[id];
                if(actor.airborne())g_sprites.unitBody(actor.utype,actor.dir,(int)((w.tick/2)%std::max(1,g_sprites.unitBodyFrames(actor.utype))),0);
            }
            const auto& actor=w.ents[gatling];voxelFired |= actor.fireAnim>0;
            g_sprites.unitTurret(actor.utype,actor.turretDir,0,actor.fireAnim>0?(int)((w.tick/2)%std::max(1,g_sprites.unitTurretFrames(actor.utype))):0);
            voxelIsolated &= before==w.checksum();
        }
        if(w.ents[siege].deployed) {
            const auto before=w.checksum();const auto& actor=w.ents[siege];
            g_sprites.unitDeployed(actor.utype,actor.dir,actor.turretDir,0);siegeIsolated &= before==w.checksum();
        }
        for(const auto& effect:w.effects) { magneticBeam |= effect.kind==3 && effect.aux==1;warpEmitted |= effect.kind==9 && effect.aux==(int)ChronoVisual::Departure; }
        while(!w.presentationEvents.empty()) {
            auto event=w.presentationEvents.front();w.presentationEvents.pop_front();
            if(event.kind==PresentationKind::Muzzle)for(int mode=0;mode<3;++mode)if(event.entity==apoc[mode]) {
                ++apocShots[mode];apocTicks[mode].push_back((int)w.tick);
            }
            if(sound && event.kind == PresentationKind::Sound) {
                if(event.positional) g_sfx.playAt(event.sound,event.x,event.y);
                else g_sfx.play(event.sound,event.volume);
            }
        }
        std::printf("TRACE %llu %08x\n",(unsigned long long)w.tick,w.checksum());
    }
    std::printf("PRESENTATION_ENTRY board=%d garrison=%d\n",boarded,garrisoned);
    bool apocOk=apocPhysical && apocIsolated;
    for(int mode=0;mode<3;++mode) {
        const int direct=10000-w.ents[apocTarget[mode]].hp,edge=10000-w.ents[apocNeighbor[mode]].hp;
        const int expected=mode==1?4:2;bool cadence=apocTicks[mode].size()==(size_t)expected;
        for(size_t i=1;i<apocTicks[mode].size();++i){const int gap=apocTicks[mode][i]-apocTicks[mode][i-1];cadence &= gap>=3 && gap<=5;}
        apocOk &= cadence && apocShots[mode]==expected && apocProjectiles[mode]==expected && direct>edge && edge>0;
        if(mode==0)apocOk &= direct==50 && edge==36;
        std::printf("PRESENTATION_APOC mode=%d shots=%d projectiles=%d direct=%d edge=%d cadence=%d physical=%d isolated=%d\n",
            mode,apocShots[mode],apocProjectiles[mode],direct,edge,cadence,apocPhysical,apocIsolated);
    }
    std::printf("PRESENTATION_APOC_ART frames=%d hash=%08x\n",g_sprites.unitTurretFrames(UnitType::Apocalypse),apocHash);
    std::printf("PRESENTATION_RELEASE unload=%d evacuate=%d\n",unloaded,evacuated);
    std::printf("PRESENTATION_SERVICE completed=%d\n",serviced);
    const int magneticDamage=magneticHp-w.ents[magneticTarget].hp;
    std::printf("PRESENTATION_MAGNETRON damage=%d beam=%d\n",magneticDamage,magneticBeam);
    std::printf("PRESENTATION_WARP frames=%d hash=%08x emitted=%d\n",g_sprites.warpFrames(0),warpHash,warpEmitted);
    const int siegeDamage=10000-w.ents[siegeTarget].hp;
    std::printf("PRESENTATION_SIEGE hash=%08x damage=%d isolated=%d\n",siegeHash,siegeDamage,siegeIsolated);
    std::printf("PRESENTATION_VOXEL body_frames=%d turret_frames=%d rotor=%08x barrel=%08x fired=%d isolated=%d\n",
        g_sprites.unitBodyFrames(UnitType::Nighthawk),g_sprites.unitTurretFrames(UnitType::GatlingTank),rotorHash,barrelHash,voxelFired,voxelIsolated);
    std::printf("PRESENTATION_TRANSFORM frames=%d hash=%08x seen=%d isolated=%d\n",g_sprites.chopperTransformFrames(),transformHash,transformed,transformIsolated);
    return apocOk && transformed && transformIsolated && g_sprites.assetsOk() && voxelFired && voxelIsolated && siegeLoaded && siegeDamage>0 && siegeIsolated && warpLoaded && warpEmitted && spriteLoaded && cameoLoaded && boarded && garrisoned && unloaded && evacuated && serviced && magneticDamage>0 && magneticBeam?0:1;
}

int GameRegression::visualRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"maps/skirmish-reference.txt",true);
    world.players[0].country=Country::Russia;world.players[1].country=Country::America;
    world.spawnBuilding(0,BldType::ConYard,8,8,true);
    world.spawnBuilding(0,BldType::TeslaReactor,11,5,true);
    world.spawnBuilding(0,BldType::Barracks,5,10,true);
    world.spawnBuilding(0,BldType::WarFactory,13,10,true);
    world.spawnBuilding(0,BldType::OreRefinery,14,17,true);
    world.spawnBuilding(0,BldType::Radar,8,17,true);
    EID tank=world.spawnUnit(0,UnitType::Rhino,21.5f,21.5f);
    world.spawnUnit(0,UnitType::WarMiner,20.5f,13.5f);
    world.spawnUnit(0,UnitType::Conscript,18.5f,18.5f);
    world.spawnUnit(0,UnitType::Conscript,19.5f,18.5f);
    world.spawnBuilding(1,BldType::ConYard,50,50,true);
    world.map.reveal(0,16,16,18);
    for(int i=0;i<60;++i) world.update();
    ais.clear();phase=Phase::InGame;localPlayer=0;gameOver=false;showMenu=false;paused=false;
    camZoom=1;interpAlpha=1;uiTab=0;sel={tank};
    camX=64-(SCREEN_W-sidebarW)/2.0f;camY=80;
    const uint32_t visualChecksum=world.checksum();
    bakeTerrain();minimapTimer=0;
    sim.active=true;sim.pos={-100,-100};sim.keysDown.clear();sim.keysPressed.clear();
    sim.pressedL=sim.pressedR=sim.downL=sim.downR=false;
    SetTargetFPS(0);shotAtDisplaySize=true;
    int failures=0;
    int darkSource=0,darkFixed=0,armor=0;bool silhouette=true;
    for(int direction=0;direction<8;++direction) {
        PixBuf source,gold,blue;
        bool ok=VxlRt::renderBody(UnitType::Rhino,direction,0,source)
            && VxlRt::renderBody(UnitType::Rhino,direction,0,gold,&HOUSE_COLORS[0])
            && VxlRt::renderBody(UnitType::Rhino,direction,0,blue,&HOUSE_COLORS[2]);
        if(!ok || source.w!=gold.w || source.h!=gold.h || source.w!=blue.w || source.h!=blue.h) { silhouette=false;continue; }
        for(size_t i=0;i<source.px.size();++i) {
            const auto a=source.px[i],g=gold.px[i],b=blue.px[i];
            silhouette &= a.a==g.a && a.a==b.a;
            if(a.a && a.r>=140 && a.r<=150 && a.g==0 && a.b==0) {
                ++darkSource;darkFixed += g.g>g.b && b.b>b.r;
            }
            if(a.a && a.r==g.r && a.g==g.g && a.b==g.b && a.r==b.r && a.g==b.g && a.b==b.b) ++armor;
        }
    }
    const bool remap=darkSource>0 && darkFixed==darkSource && armor>100 && silhouette;
    failures+=!remap;
    std::printf("VISUAL_REMAP pass=%d source_dark=%d recolored_dark=%d neutral_pixels=%d silhouette=%d\n",
        remap,darkSource,darkFixed,armor,silhouette);
    bool pngRemap=true;int pngHouse=0,pngNeutral=0;
    for(int direction=0;direction<8;++direction) for(bool turret:{false,true}) {
        const std::string stem=turret?"turret_type99_d"+std::to_string(direction):"unit_type99_d"+std::to_string(direction)+"_f0";
        Image mask=LoadImage(contentResolve(("assets/sprites/"+stem+"_remap.png").c_str()).c_str());
        Image raw=LoadImage(contentResolve(("assets/sprites/"+stem+".png").c_str()).c_str());
        if(!mask.data || !raw.data) { pngRemap=false;if(mask.data) UnloadImage(mask);if(raw.data) UnloadImage(raw);continue; }
        const auto& gold=turret?g_sprites.unitTurret(UnitType::Type99,direction,0):g_sprites.unitBody(UnitType::Type99,direction,0,0);
        const auto& blue=turret?g_sprites.unitTurret(UnitType::Type99,direction,2):g_sprites.unitBody(UnitType::Type99,direction,0,2);
        Image gi=LoadImageFromTexture(gold.tex),bi=LoadImageFromTexture(blue.tex);
        Color *m=LoadImageColors(mask),*r=LoadImageColors(raw),*g=LoadImageColors(gi),*b=LoadImageColors(bi);
        const int ox=turret?0:6,oy=turret?0:4;
        bool dimensions=gi.width==mask.width+2*ox && gi.height==mask.height+2*oy && gi.width==bi.width && gi.height==bi.height && raw.width==mask.width && raw.height==mask.height;
        pngRemap &= dimensions;
        if(dimensions) for(int y=0;y<mask.height;++y) for(int x=0;x<mask.width;++x) {
            int at=y*mask.width+x,dest=(y+oy)*gi.width+x+ox;
            if(!r[at].a) continue;
            if(m[at].a) {
                float shade=m[at].r/255.0f;
                pngRemap &= g[dest].r==(uint8_t)(HOUSE_COLORS[0].r*shade) && g[dest].g==(uint8_t)(HOUSE_COLORS[0].g*shade)
                    && b[dest].r==(uint8_t)(HOUSE_COLORS[2].r*shade) && b[dest].b==(uint8_t)(HOUSE_COLORS[2].b*shade);
                ++pngHouse;
            } else {
                pngRemap &= g[dest].r==b[dest].r && g[dest].g==b[dest].g && g[dest].b==b[dest].b;++pngNeutral;
            }
        }
        UnloadImageColors(m);UnloadImageColors(r);UnloadImageColors(g);UnloadImageColors(b);
        UnloadImage(mask);UnloadImage(raw);UnloadImage(gi);UnloadImage(bi);
    }
    pngRemap &= pngHouse>0 && pngNeutral>0;failures+=!pngRemap;
    std::printf("VISUAL_PNG_REMAP pass=%d house=%d neutral=%d\n",pngRemap,pngHouse,pngNeutral);
    for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);
        for(int i=0;i<3;++i) render();
        bool dimensions=GetRenderWidth()==size.x && GetRenderHeight()==size.y;
        const Rectangle unit=unitScreenRect(world.ents[tank]);
        const Rectangle dst=letterboxDst();
        Vector2 logical{(unit.x+unit.width/2)*camZoom,(unit.y+unit.height/2)*camZoom};
        SetMousePosition((int)(dst.x+logical.x*dst.width/SCREEN_W),(int)(dst.y+logical.y*dst.height/SCREEN_H));
        sim.active=false;
        const Vector2 mouse=mousePos();
        const bool picked=pickUnit((int)mouse.x,(int)mouse.y)==tank;
        sim.active=true;
        const float savedX=camX,savedY=camY;
        camZoom=1.4f;
        sim.pos={1340,105};sim.downL=true;
        BeginTextureMode(canvas);drawMinimap(1260,45,160,120);EndTextureMode();
        const bool centered=std::abs(camX+(SCREEN_W-sidebarW)/(2*camZoom))<1
            && std::abs(camY+(SCREEN_H-BOTTOM_BAR_H)/(2*camZoom)-1024)<1;
        sim.downL=false;sim.pos={-100,-100};
        camZoom=1;camX=savedX;camY=savedY;
        failures+=!centered;
        std::printf("VISUAL_MINIMAP size=%dx%d zoom=1.4 centered=%d\n",size.x,size.y,centered);
        shotFile=(std::filesystem::path(outputDir)/(std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
        render();
        std::vector<double> times;
        for(int i=0;i<120;++i) { double start=GetTime();render();times.push_back((GetTime()-start)*1000); }
        std::sort(times.begin(),times.end());
        failures+=!dimensions || !picked;
        std::printf("VISUAL %s size=%dx%d actual=%dx%d pick=%d render_p95_ms=%.3f\n",
            dimensions&&picked?"PASS":"FAIL",size.x,size.y,GetRenderWidth(),GetRenderHeight(),picked,times[113]);
    }
    failures+=world.checksum()!=visualChecksum;
    std::printf("VISUAL simulation_unchanged=%d\n",world.checksum()==visualChecksum);
    // Exercise the actual command -> result event -> subtitle/marker path.
    EID deployed=world.spawnUnit(0,UnitType::GI,20.5f,24.5f);
    world.orderRadDeploy({deployed});
    World::Cmd rejectedMove{};rejectedMove.type=World::Cmd::Move;rejectedMove.ids={deployed};rejectedMove.x=22;rejectedMove.y=22;
    world.applyCmd(0,rejectedMove);
    const auto feedbackHash=world.checksum();consumePresentationEvents();
    bool commandFeedback=msg==TR(S::MsgMoveDeploy) && moveFeedbackRejected && moveFeedbackTimer>0;
    for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);render();
        shotFile=(std::filesystem::path(outputDir)/("feedback_rejected_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
        render();
    }
    failures+=!commandFeedback || feedbackHash!=world.checksum();
    std::printf("VISUAL_COMMAND rejected=%d simulation_unchanged=%d\n",commandFeedback,feedbackHash==world.checksum());
    moveFeedbackTimer=0;msgTimer=0;
    EID airTarget=world.spawnUnit(1,UnitType::Kirov,24.5f,21.5f);
    EID antiAir=world.spawnUnit(0,UnitType::FlakTrack,19.5f,24.5f);
    World::Cmd airAttack{};airAttack.type=World::Cmd::Attack;airAttack.a=airTarget;
    bool attackUI=true;
    for(bool partial:{false,true}) {
        airAttack.ids=partial?std::vector<EID>{tank,antiAir}:std::vector<EID>{tank};world.applyCmd(0,airAttack);
        const auto hash=world.checksum();consumePresentationEvents();
        attackUI &= attackFeedback && feedbackLift==AIR_ALT && (partial?moveFeedbackPartial:moveFeedbackRejected);
        for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();
            const std::string stem=partial?"attack_partial_":"attack_rejected_";
            shotFile=(std::filesystem::path(outputDir)/(stem+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
            float timer=moveFeedbackTimer;moveFeedbackTimer=0;
            shotFile=(std::filesystem::path(outputDir)/(stem+"without_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();moveFeedbackTimer=timer;
        }
        attackUI &= hash==world.checksum();
    }
    failures+=!attackUI;
    std::printf("VISUAL_ATTACK rejected_partial=%d simulation_unchanged=%d\n",attackUI,attackUI);
    moveFeedbackTimer=0;msgTimer=0;
    world.ents[airTarget].x=world.ents[airTarget].px=50.5f;world.ents[airTarget].y=world.ents[airTarget].py=50.5f;
    world.unitOccSync(airTarget);world.airOccSync(airTarget);
    EID engineer=world.spawnUnit(0,UnitType::Engineer,20.5f,23.5f);
    EID repairTarget=world.spawnBuilding(0,BldType::OilDerrick,24,21,true);
    World::Cmd repair{};repair.type=World::Cmd::Repair;repair.a=repairTarget;
    bool engineerUI=true;
    for(bool partial:{false,true}) {
        if(partial) world.damage(repairTarget,100,-1);
        repair.ids=partial?std::vector<EID>{engineer,tank}:std::vector<EID>{engineer};
        sel=repair.ids;world.applyCmd(0,repair);
        const auto hash=world.checksum();consumePresentationEvents();
        engineerUI &= !attackFeedback && feedbackLift==0 && (partial?moveFeedbackPartial:moveFeedbackRejected);
        engineerUI &= msg==(partial?TextFormat(TR(S::MsgEngineerPartial),1,TR(S::MsgEngineerRequired)):TR(S::MsgRepairFullHealth));
        for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();
            const std::string stem=partial?"engineer_partial_":"engineer_rejected_";
            shotFile=(std::filesystem::path(outputDir)/(stem+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
            float timer=moveFeedbackTimer;moveFeedbackTimer=0;
            shotFile=(std::filesystem::path(outputDir)/(stem+"without_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();moveFeedbackTimer=timer;
        }
        engineerUI &= hash==world.checksum();
    }
    failures+=!engineerUI;
    std::printf("VISUAL_ENGINEER rejected_partial=%d simulation_unchanged=%d\n",engineerUI,engineerUI);
    moveFeedbackTimer=0;msgTimer=0;
    EID passenger=world.spawnUnit(0,UnitType::GI,18.5f,24.5f);
    EID transport=world.spawnUnit(0,UnitType::IFV,24.5f,25.5f);
    world.ents[transport].cargo.push_back({UnitType::Engineer,100,0,0,0});
    World::Cmd board{};board.type=World::Cmd::Board;board.a=transport;
    bool entryUI=true;
    for(bool partial:{false,true}) {
        if(partial) world.ents[transport].cargo.clear();
        board.ids=partial?std::vector<EID>{passenger,tank}:std::vector<EID>{passenger};sel=board.ids;world.applyCmd(0,board);
        const auto hash=world.checksum();consumePresentationEvents();
        entryUI &= !attackFeedback && feedbackLift==0 && (partial?moveFeedbackPartial:moveFeedbackRejected);
        entryUI &= msg==(partial?TextFormat(TR(S::MsgEntryPartial),1,TR(S::MsgEntryIncompatible)):TR(S::MsgEntryFull));
        for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();
            const std::string stem=partial?"entry_partial_":"entry_rejected_";
            shotFile=(std::filesystem::path(outputDir)/(stem+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
            float timer=moveFeedbackTimer;moveFeedbackTimer=0;
            shotFile=(std::filesystem::path(outputDir)/(stem+"without_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();moveFeedbackTimer=timer;
        }
        entryUI &= hash==world.checksum();
    }
    failures+=!entryUI;
    std::printf("VISUAL_ENTRY rejected_partial=%d simulation_unchanged=%d\n",entryUI,entryUI);
    moveFeedbackTimer=0;msgTimer=0;
    world.ents[transport].x=world.ents[transport].px=50.5f;world.ents[transport].y=world.ents[transport].py=45.5f;
    world.unitOccSync(transport);world.airOccSync(transport);
    EID unloading=world.spawnUnit(0,UnitType::AmphTransport,24.5f,25.5f);
    world.ents[unloading].cargo={{UnitType::GI,67,2,400,1},{UnitType::GI,68,3,500,1}};
    EID gate=INVALID_EID;
    for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) if(dx || dy) {
        EID wall=world.spawnBuilding(0,BldType::Wall,24+dx,25+dy,true);
        if(dx==1 && dy==0) gate=wall;
    }
    World::Cmd unload{};unload.type=World::Cmd::Unload;unload.ids={unloading};sel=unload.ids;
    bool releaseUI=true;
    for(bool partial:{false,true}) {
        if(partial) world.kill(gate,false);
        world.applyCmd(0,unload);const auto hash=world.checksum();consumePresentationEvents();
        releaseUI &= !attackFeedback && feedbackLift==0 && (partial?moveFeedbackPartial:moveFeedbackRejected);
        releaseUI &= msg==(partial?TextFormat(TR(S::MsgReleasePartial),1,1):TextFormat(TR(S::MsgReleaseBlocked),2));
        for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();const std::string stem=partial?"release_partial_":"release_blocked_";
            shotFile=(std::filesystem::path(outputDir)/(stem+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
            float timer=moveFeedbackTimer;moveFeedbackTimer=0;
            shotFile=(std::filesystem::path(outputDir)/(stem+"without_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();moveFeedbackTimer=timer;
        }
        releaseUI &= hash==world.checksum();
    }
    failures+=!releaseUI;std::printf("VISUAL_RELEASE blocked_partial=%d simulation_unchanged=%d\n",releaseUI,releaseUI);
    moveFeedbackTimer=0;msgTimer=0;
    world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    for(auto& cell:world.map.cells) cell=Cell{};
    world.cratesEnabled=false;combatCues.clear();
    EID serviceTank=world.spawnUnit(0,UnitType::Grizzly,22.5f,24.5f);
    EID serviceInfantry=world.spawnUnit(0,UnitType::GI,20.5f,24.5f);
    EID serviceDepot=world.spawnBuilding(0,BldType::ServiceDepot,23,24,true);
    world.spawnBuilding(0,BldType::PowerPlant,40,40,true);world.ents[serviceTank].hp=100;
    world.map.reveal(0,24,25,20);camZoom=1;
    camX=(24.5f-25.f)*(TILE_W/2.f)-532;camY=(24.5f+25.f)*(TILE_H/2.f)-500;
    bakeTerrain();minimapTimer=0;
    bool serviceUI=true;
    for(int stage=0;stage<4;++stage) {
        const char* stems[]={"service_rejected_","service_partial_","service_money_","service_power_"};
        if(stage<2) {
            World::Cmd service{};service.type=World::Cmd::Service;service.a=serviceDepot;
            service.ids=stage==0?std::vector<EID>{serviceInfantry}:std::vector<EID>{serviceTank,serviceInfantry};
            sel=service.ids;world.applyCmd(0,service);
        } else {
            world.players[0].money=stage==2?0:1000;
            if(stage==3) { world.players[0].powerMade=0;world.players[0].powerUsed=25; }
            world.update();
        }
        const auto hash=world.checksum();consumePresentationEvents();
        serviceUI &= !attackFeedback && feedbackLift==0 && moveFeedbackTimer>0;
        serviceUI &= stage==0?moveFeedbackRejected && msg==TR(S::MsgServiceIncompatible):
            stage==1?moveFeedbackPartial:msg==TR(stage==2?S::MsgServiceMoney:S::MsgServicePower);
        for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();const std::string stem=stems[stage];
            shotFile=(std::filesystem::path(outputDir)/(stem+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
            float timer=moveFeedbackTimer;moveFeedbackTimer=0;
            shotFile=(std::filesystem::path(outputDir)/(stem+"without_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();moveFeedbackTimer=timer;
        }
        serviceUI &= hash==world.checksum();
    }
    failures+=!serviceUI;std::printf("VISUAL_SERVICE rejected_partial_paused=%d simulation_unchanged=%d\n",serviceUI,serviceUI);
    moveFeedbackTimer=0;msgTimer=0;
    const UnitType combatTanks[]={UnitType::Grizzly,UnitType::Rhino,UnitType::Type99,UnitType::LasherTank};
    const UnitType combatInfantry[]={UnitType::GI,UnitType::Conscript,UnitType::PLA,UnitType::Initiate};
    for(int faction=0;faction<4;++faction) {
        world.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        combatCues.clear();
        EID shooter=world.spawnUnit(0,combatTanks[faction],21.5f,21.5f);
        EID target=world.spawnUnit(1,UnitType::MCV,26.5f,21.5f);
        EID walker=world.spawnUnit(0,combatInfantry[faction],18.5f,22.5f);
        world.map.reveal(0,24,22,20);sel={shooter};
        camZoom=1;camX=-(SCREEN_W-sidebarW)/2.0f;camY=200;
        bakeTerrain();minimapTimer=0;
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={shooter};attack.a=target;world.applyCmd(0,attack);
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={walker};move.x=20;move.y=23;world.applyCmd(0,move);
        World replay=world;replay.map.bldOccRef=&replay.bldOcc;
        bool fired=false,hit=false,dead=false,walked=false,isolated=true;
        auto capture=[&](const char* stage) {
            const auto hash=world.checksum();
            for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                SetWindowSize(size.x,size.y);render();
                std::string stem="combat_f"+std::to_string(faction)+"_"+stage+"_";
                shotFile=(std::filesystem::path(outputDir)/(stem+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
                if(std::string(stage)=="impact") {
                    auto saved=std::move(combatCues);combatCues.clear();
                    shotFile=(std::filesystem::path(outputDir)/("combat_f"+std::to_string(faction)+"_impact_without_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
                    combatCues=std::move(saved);
                }
            }
            isolated &= hash==world.checksum();
        };
        for(int frame=0;frame<3000 && !dead;++frame) {
            world.update();replay.update();
            walked |= world.ents[walker].moveTick>0;
            bool muzzle=false,impact=false;
            for(const auto& event:world.presentationEvents) {
                muzzle |= event.kind==PresentationKind::Muzzle && event.entity==shooter;
                impact |= event.kind==PresentationKind::Impact && event.entity==target && event.damage>0;
            }
            advancePresentation(1.0f/LOGIC_FPS);
            const auto hash=world.checksum();consumePresentationEvents();
            isolated &= hash==world.checksum() && hash==replay.checksum();
            replay.presentationEvents.clear();
            if(muzzle && !fired) { capture("fire");fired=true; }
            if(impact && !hit) { capture("impact");hit=true; }
            if(!world.valid(target)) { capture("death");dead=true; }
        }
        advancePresentation(1);
        bool expired=combatCues.empty();
        bool ok=fired && hit && dead && walked && isolated && expired;
        failures+=!ok;
        std::printf("VISUAL_COMBAT faction=%d pass=%d fire=%d impact=%d death=%d walk=%d isolated=%d expired=%d\n",faction,ok,fired,hit,dead,walked,isolated,expired);
    }
    {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        combatCues.clear();
        EID armored=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
        world.ents[armored].invuln=20;
        int hp=world.ents[armored].hp;world.presentationEvents.clear();
        world.damage(armored,100,1);
        bool protectedHit=world.presentationEvents.empty() && world.ents[armored].hp==hp;
        world.ents[armored].invuln=0;world.ents[armored].vetRank=1;
        world.damage(armored,100,1);
        auto hit=world.presentationEvents.front();
        bool actual=hit.kind==PresentationKind::Impact && hit.damage==hp-world.ents[armored].hp && !hit.lethal;
        world.presentationEvents.clear();
        EID hidden=world.spawnUnit(1,UnitType::MCV,40.5f,40.5f);
        world.damage(hidden,5,0);consumePresentationEvents();
        bool fog=combatCues.empty();
        world.map.reveal(0,40,40,3);world.damage(hidden,5,0);consumePresentationEvents();
        fog &= combatCues.size()==1;
        EID aircraft=world.spawnUnit(0,UnitType::Kirov,12.5f,14.5f);world.ents[aircraft].state=UState::Circling;
        world.damage(aircraft,3,-1);
        bool metadata=world.presentationEvents.front().visualTarget==VisualTarget::Aircraft;
        world.presentationEvents.clear();
        EID building=world.spawnBuilding(0,BldType::TeslaReactor,15,15,true);
        world.damage(building,2,-1);
        const auto buildingHit=world.presentationEvents.front();
        metadata &= buildingHit.visualTarget==VisualTarget::Building
            && buildingHit.x==15+bldDef(BldType::TeslaReactor).w/2.0f;
        world.presentationEvents.clear();
        EID victim=world.spawnUnit(0,UnitType::GI,20.5f,20.5f);int victimHp=world.ents[victim].hp;
        world.damage(victim,100000,-1);
        auto death=world.presentationEvents.front();
        EID replacement=world.spawnUnit(0,UnitType::Rhino,30.5f,30.5f);
        const auto hash=world.checksum();consumePresentationEvents();
        bool snapshot=replacement==victim && death.lethal && death.damage==victimHp
            && death.visualTarget==VisualTarget::Infantry && combatCues.back().event.x==20.5f && hash==world.checksum();
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        combatCues.clear();
        for(int i=0;i<150;++i) {
            EID unit=world.spawnUnit(0,UnitType::MCV,2.5f+i%30,2.5f+i/30);
            world.damage(unit,1,-1);
        }
        auto before=world.checksum();consumePresentationEvents();
        bool bounded=combatCues.size()==128;
        advancePresentation(1);bounded &= combatCues.empty() && before==world.checksum();
        bool ok=protectedHit && actual && fog && metadata && snapshot && bounded;
        failures+=!ok;
        std::printf("VISUAL_CUE_RULES pass=%d invulnerable=%d actual_loss=%d fog=%d metadata=%d snapshot=%d bounded=%d\n",ok,protectedHit,actual,fog,metadata,snapshot,bounded);
    }
    for(int count:{200,500}) {
        world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"maps/skirmish-reference.txt",true);
        world.spawnBuilding(0,BldType::TeslaReactor,3,3,true);
        world.spawnBuilding(0,BldType::Radar,8,3,true);
        for(int i=0;i<count;++i) world.spawnUnit(0,
            i%4==0?UnitType::Conscript:i%4==1?UnitType::Grizzly:i%4==2?UnitType::Rhino:UnitType::Apocalypse,
            16.5f+i%20,16.5f+i/20);
        world.map.reveal(0,32,32,64);sel.clear();camZoom=0.8f;
        camX=-(SCREEN_W-sidebarW)/(2*camZoom);camY=800-(SCREEN_H-BOTTOM_BAR_H)/(2*camZoom);
        bakeTerrain();minimapTimer=0;
        const auto checksum=world.checksum();
        for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);
            for(int i=0;i<30;++i) render();
            std::vector<double> times;
            for(int i=0;i<120;++i) { double start=GetTime();render();times.push_back((GetTime()-start)*1000); }
            std::sort(times.begin(),times.end());
            std::printf("VISUAL_BENCH units=%d size=%dx%d render_p95_ms=%.3f simulation_unchanged=%d\n",
                count,size.x,size.y,times[113],checksum==world.checksum());
            failures+=checksum!=world.checksum();
            if(count==500) {
                shotFile=(std::filesystem::path(outputDir)/("crowd_500_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
            }
        }
    }
    sim.active=false;shotAtDisplaySize=false;SetTargetFPS(60);
    return failures;
}

int GameRegression::animationRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    const UnitType types[]={UnitType::GI,UnitType::Conscript,UnitType::PLA,UnitType::Initiate,UnitType::AttackDog};
    const char* stages[]={"walk","fire","death"};
    Image sheets[3];for(auto& sheet:sheets) sheet=GenImageColor(1024,400,Color{16,24,24,255});
    int failures=0;localPlayer=0;sel.clear();camZoom=1;interpAlpha=1;
    for(int row=0;row<5;++row) {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;
        EID unit=world.spawnUnit(0,types[row],10.5f,10.5f);
        EID target=world.spawnUnit(1,UnitType::MCV,50.5f,50.5f);
        const auto& info=g_sprites.animInfo(types[row]);
        std::set<int> phases[3];std::set<uint32_t> variants[3];bool isolated=true,continuous=true;
        auto capture=[&](int stage,int phase) {
            if(!phases[stage].insert(phase).second) return;
            // Pose isolation requires a visible scene even after its last observer dies.
            // Visibility filtering is independently checked by fog_feedback.
            std::fill(world.map.fog[localPlayer].begin(),world.map.fog[localPlayer].end(),FOG_VISIBLE);
            const auto& actor=world.ents[unit];
            camX=(actor.x-actor.y)*TILE_W/2.0f-200;camY=(actor.x+actor.y)*TILE_H/2.0f-200;
            const auto hash=world.checksum();
            BeginTextureMode(canvas);ClearBackground(Color{16,24,24,255});
            drawEntities();if(stage==2) drawEffectsLayer();EndTextureMode();
            Image full=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&full);
            Image crop=ImageFromImage(full,{168,160,64,64});UnloadImage(full);
            Color* pixels=LoadImageColors(crop);uint32_t fingerprint=2166136261u;
            for(int i=0;i<64*64;++i) { fingerprint=(fingerprint^pixels[i].r)*16777619u;fingerprint=(fingerprint^pixels[i].g)*16777619u;fingerprint=(fingerprint^pixels[i].b)*16777619u; }
            variants[stage].insert(fingerprint);UnloadImageColors(pixels);
            ImageDraw(&sheets[stage],crop,{0,0,64,64},{(float)(phase*64),(float)(row*80+14),64,64},WHITE);
            ImageDrawText(&sheets[stage],unitAssetName(types[row]),2,row*80+2,10,WHITE);
            UnloadImage(crop);isolated &= hash==world.checksum();
        };
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={unit};command.x=35;command.y=10;world.applyCmd(0,command);
        for(int frame=0;frame<48;++frame) {
            int previous=world.ents[unit].walkAnim;world.update();
            const auto& actor=world.ents[unit];
            bool moved=actor.x!=actor.px || actor.y!=actor.py;
            continuous &= actor.walkAnim==previous+(moved?1:0);
            if(moved) capture(0,(actor.walkAnim/std::max(1,info.walkRate))%info.walk);
        }
        world.orderStop({unit});world.kill(target,false);
        const bool dog=types[row]==UnitType::AttackDog;
        target=world.spawnUnit(1,dog?UnitType::GI:UnitType::MCV,world.ents[unit].x-(dog?1.0f:1.5f),world.ents[unit].y-(dog?0.0f:1.5f));
        command.type=World::Cmd::Attack;command.a=target;world.applyCmd(0,command);
        for(int frame=0;frame<100 && world.ents[unit].fireAnim==0;++frame) world.update();
        for(int frame=0;frame<g_gameRules.fireAnimationTicks;++frame) {
            int remaining=world.ents[unit].fireAnim;
            if(remaining>0) capture(1,(g_gameRules.fireAnimationTicks-remaining)*info.fire/std::max(1,g_gameRules.fireAnimationTicks));
            world.update();
        }
        world.damage(unit,100000,-1);
        for(int frame=0;frame<g_gameRules.deathAnimationTicks;++frame) {
            for(const auto& effect:world.effects) if(effect.kind==10 && effect.aux==(int)types[row]) {
                capture(2,effect.age*info.die/std::max(1,effect.maxAge));break;
            }
            world.update();
        }
        bool ok=continuous && isolated && phases[0].size()==(size_t)info.walk && phases[1].size()==(size_t)info.fire && phases[2].size()==(size_t)info.die;
        ok &= variants[0].size()==(size_t)info.walk && variants[1].size()>=(size_t)std::max(2,info.fire-1) && variants[2].size()==(size_t)info.die;
        failures+=!ok;
        std::printf("ANIMATION %s unit=%s walk=%zu fire=%zu death=%zu variants=%zu,%zu,%zu continuous=%d isolated=%d\n",ok?"PASS":"FAIL",unitAssetName(types[row]),phases[0].size(),phases[1].size(),phases[2].size(),variants[0].size(),variants[1].size(),variants[2].size(),continuous,isolated);
    }
    for(int stage=0;stage<3;++stage) {
        ExportImage(sheets[stage],(std::filesystem::path(outputDir)/(std::string("animation_")+stages[stage]+".png")).string().c_str());UnloadImage(sheets[stage]);
    }
    world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    for(auto& cell:world.map.cells) cell=Cell{};
    EID walker=world.spawnUnit(0,UnitType::GI,10.5f,10.5f);
    EID blocker=world.spawnUnit(1,UnitType::MCV,11.5f,10.5f);
    world.ents[walker].walkAnim=15;
    World::Cmd move{};move.type=World::Cmd::Move;move.ids={walker};move.x=35;move.y=10;world.applyCmd(0,move);
    for(int i=0;i<24;++i) world.update();
    bool blocked=world.ents[walker].walkAnim==15 && world.ents[walker].x==10.5f && world.ents[walker].pathIdx==0;
    world.kill(blocker,false);world.update();
    FILE* file=tmpfile();World restored;
    bool saved=file && world.saveGame(file);
    if(saved) { rewind(file);saved=restored.loadGame(file); }
    if(file) fclose(file);
    bool continued=saved && restored.ents[walker].walkAnim==world.ents[walker].walkAnim;
    if(saved) for(int i=0;i<120;++i) { world.update();restored.update();continued &= world.checksum()==restored.checksum(); }
    failures+=!blocked || !continued;
    std::printf("ANIMATION_TIMING blocked=%d saved=%d continued=%d\n",blocked,saved,continued);
    const UnitType deployTypes[]={UnitType::GI,UnitType::GuardianGI,UnitType::Desolator};
    Image idleSheet=GenImageColor(512,240,Color{16,24,24,255});
    Image fireSheet=GenImageColor(512,240,Color{16,24,24,255});
    Image restoredSheet=GenImageColor(64,240,Color{16,24,24,255});
    for(int row=0;row<3;++row) {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;
        EID actor=world.spawnUnit(0,deployTypes[row],10.5f,10.5f);
        World::Cmd deploy{};deploy.type=World::Cmd::RadDeploy;deploy.ids={actor};world.applyCmd(0,deploy);
        const auto& info=g_sprites.animInfo(deployTypes[row]);
        bool poses=true,isolated=true;int maxDifference=0;
        auto checkPose=[&](const Sprite& expected,const Sprite& wrong,Image& sheet,int column,bool projectiles) {
            const auto hash=world.checksum();
            camX=-200;camY=136;
            auto crop=[&](const Sprite* reference) {
                BeginTextureMode(canvas);ClearBackground(Color{16,24,24,255});
                if(reference) DrawTexture(reference->tex,200-reference->ox,200-reference->oy,WHITE);
                else drawEntities();
                EndTextureMode();
                Image full=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&full);
                Image part=ImageFromImage(full,{168,160,64,64});UnloadImage(full);return part;
            };
            Image actual=crop(nullptr),reference=crop(&expected),oldPose=crop(&wrong);
            Color *a=LoadImageColors(actual),*b=LoadImageColors(reference),*c=LoadImageColors(oldPose);
            int right=0,incorrect=0;
            for(int i=0;i<64*64;++i) {
                right+=a[i].r!=b[i].r || a[i].g!=b[i].g || a[i].b!=b[i].b;
                incorrect+=a[i].r!=c[i].r || a[i].g!=c[i].g || a[i].b!=c[i].b;
            }
            // Live projectiles can overlap a small part of the actor. The correct
            // deployed reference must still beat the old standing reference.
            bool match=right<incorrect && right<=(projectiles?128:0);
            maxDifference=std::max(maxDifference,right);
            ImageDraw(&sheet,actual,{0,0,64,64},{(float)(column*64),(float)(row*80+14),64,64},WHITE);
            ImageDrawText(&sheet,unitAssetName(deployTypes[row]),2,row*80+2,10,WHITE);
            UnloadImageColors(a);UnloadImageColors(b);UnloadImageColors(c);
            UnloadImage(actual);UnloadImage(reference);UnloadImage(oldPose);
            isolated &= hash==world.checksum();return match;
        };
        for(int direction=0;direction<8;++direction) {
            world.ents[actor].dir=direction;
            const auto& expected=g_sprites.unitAnim(deployTypes[row],row==2?UAnim::DepFire:UAnim::Dep,direction,0,0);
            poses &= checkPose(expected,g_sprites.unitBody(deployTypes[row],direction,0,0),idleSheet,direction,false);
        }
        EID enemy=world.spawnUnit(1,UnitType::MCV,10.5f,13.5f);
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={actor};attack.a=enemy;world.applyCmd(0,attack);
        World replay=world;replay.map.bldOccRef=&replay.bldOcc;
        bool replayEqual=true;std::set<int> captured;
        for(int tick=0;tick<28;++tick) {
            world.update();replay.update();replayEqual &= world.checksum()==replay.checksum();
            const auto& e=world.ents[actor];
            if(row!=2 && !e.fireAnim) continue;
            int phase=row==2?(int)((world.tick/std::max(1,info.fireRate))%info.depFire)
                :(g_gameRules.fireAnimationTicks-e.fireAnim)*info.depFire/std::max(1,g_gameRules.fireAnimationTicks);
            if(!captured.insert(phase).second) continue;
            const auto& expected=g_sprites.unitAnim(deployTypes[row],UAnim::DepFire,e.dir,phase,0);
            const auto& wrong=g_sprites.unitAnim(deployTypes[row],UAnim::Fire,e.dir,std::min(phase,info.fire-1),0);
            poses &= checkPose(expected,wrong,fireSheet,phase,true);
        }
        world.kill(enemy,false);world.orderStop({actor});
        EID friendly=world.spawnUnit(0,UnitType::Grizzly,6.5f,10.5f);
        move.ids={friendly};move.x=20;move.y=10;world.applyCmd(0,move);
        bool immobile=true,separate=true;
        for(int tick=0;tick<400;++tick) {
            world.update();immobile &= world.ents[actor].x==10.5f && world.ents[actor].y==10.5f;
            separate &= (int)world.ents[friendly].x!=10 || (int)world.ents[friendly].y!=10;
        }
        bool passed=world.ents[friendly].x>18;
        world.applyCmd(0,deploy);world.orderStop({actor});
        bool restored=!world.ents[actor].deployed && !world.ents[actor].radDeployed;
        restored &= checkPose(g_sprites.unitBody(deployTypes[row],world.ents[actor].dir,0,0),
            g_sprites.unitAnim(deployTypes[row],UAnim::Dep,world.ents[actor].dir,0,0),restoredSheet,0,false);
        bool ok=poses && isolated && replayEqual && immobile && separate && passed && restored && captured.size()==(size_t)info.depFire;
        failures+=!ok;
        std::printf("DEPLOYED_ANIMATION %s unit=%s phases=%zu pose=%d max_diff=%d isolated=%d replay=%d immobile=%d separate=%d passed=%d restored=%d\n",
            ok?"PASS":"FAIL",unitAssetName(deployTypes[row]),captured.size(),poses,maxDifference,isolated,replayEqual,immobile,separate,passed,restored);
    }
    ExportImage(idleSheet,(std::filesystem::path(outputDir)/"animation_deploy_idle.png").string().c_str());UnloadImage(idleSheet);
    ExportImage(fireSheet,(std::filesystem::path(outputDir)/"animation_deploy_fire.png").string().c_str());UnloadImage(fireSheet);
    ExportImage(restoredSheet,(std::filesystem::path(outputDir)/"animation_undeploy.png").string().c_str());UnloadImage(restoredSheet);
    return failures;
}

int GameRegression::attackRegression() {
    int failures=0;localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;camZoom=1;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("ATTACK %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;world.map.reveal(0,32,32,64);moveFeedbackTimer=0;
    };
    reset();
    EID tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    EID plane=world.spawnUnit(1,UnitType::Kirov,14.5f,10.5f);
    EID aa=world.spawnUnit(0,UnitType::FlakTrack,8.5f,16.5f);
    World::Cmd command{};command.type=World::Cmd::Move;command.ids={tank};command.x=20;command.y=10;world.applyCmd(0,command);
    command.a=1;command.x=25;world.applyCmd(0,command);world.presentationEvents.clear();
    auto before=world.checksum();command.type=World::Cmd::Attack;command.a=plane;world.applyCmd(0,command);
    bool rejected=world.presentationEvents.back().attack.reason==AttackRejectReason::NoAntiAir && before==world.checksum() && world.ents[tank].wps.size()==1;
    consumePresentationEvents();
    check(rejected && attackFeedback && moveFeedbackRejected && feedbackLift==AIR_ALT && msg==TR(S::MsgAttackNoAA),"anti-air rejection preserves previous orders and explains reason");
    command.ids={tank,aa};world.applyCmd(0,command);
    bool partial=world.presentationEvents.back().attack.accepted==1 && world.presentationEvents.back().attack.rejected==1 && world.ents[aa].target==plane;
    consumePresentationEvents();check(partial && moveFeedbackPartial,"mixed selection reports partial success");
    camX=-500;camY=0;sel={tank};
    auto rect=unitScreenRect(world.ents[plane]);int mx=(int)(rect.x+rect.width/2),my=(int)(rect.y+rect.height/2);
    updateHoverCursor(mx,my);bool cursor=cursorKind==CursorKind::NoMove;sel={tank,aa};updateHoverCursor(mx,my);
    check(cursor && cursorKind==CursorKind::Attack,"hover cursor shares weapon eligibility");
    EID ground=world.spawnUnit(1,UnitType::MCV,30.5f,16.5f);
    auto reject=[&](UnitType type,AttackRejectReason reason,const char* label) {
        EID actor=world.spawnUnit(0,type,5.5f+(int)type%10,25.5f);
        command.ids={actor};command.a=ground;auto hash=world.checksum();world.applyCmd(0,command);
        bool ok=world.presentationEvents.back().attack.reason==reason && hash==world.checksum();
        world.presentationEvents.clear();check(ok,label);return actor;
    };
    reject(UnitType::Engineer,AttackRejectReason::NoWeapon,"unarmed unit rejected");
    reject(UnitType::AttackDog,AttackRejectReason::Ineffective,"infantry-only dog rejects vehicle target");
    reject(UnitType::Typhoon,AttackRejectReason::NavalOnly,"torpedo rejects land target");
    reject(UnitType::Aegis,AttackRejectReason::NoAntiGround,"anti-air only weapon rejects ground target");
    reject(UnitType::Spy,AttackRejectReason::Unsupported,"spy rejects vehicle disguise");
    auto weapon=world.effWeapon(world.ents[tank]);
    auto& versus=g_gameRules.warheadVerses[(int)weapon.warhead][(int)unitDef(UnitType::MCV).armor];
    float savedVersus=versus;versus=0;
    command.ids={tank};command.a=ground;world.applyCmd(0,command);
    check(world.presentationEvents.back().attack.reason==AttackRejectReason::Ineffective,"rules with zero armor multiplier reject ineffective weapon");
    versus=savedVersus;world.presentationEvents.clear();
    EID ifv=world.spawnUnit(0,UnitType::IFV,7.5f,28.5f);
    World::Ent::GarrisonedUnit passenger{};passenger.type=UnitType::Engineer;world.ents[ifv].cargo.push_back(passenger);
    command.ids={ifv};command.a=ground;world.applyCmd(0,command);
    check(world.presentationEvents.back().attack.reason==AttackRejectReason::NoWeapon,"IFV eligibility uses passenger weapon");world.presentationEvents.clear();
    command.ids={tank};command.a=INVALID_EID;before=world.checksum();world.applyCmd(0,command);consumePresentationEvents();
    check(before==world.checksum() && msg==TR(S::MsgAttackInvalid) && moveFeedbackTimer==0,"stale target rejected without fake position");
    EID spy=world.spawnUnit(0,UnitType::Spy,8.5f,30.5f),infantry=world.spawnUnit(1,UnitType::Conscript,30.5f,30.5f);
    command.ids={spy};command.a=infantry;world.applyCmd(0,command);
    check(world.presentationEvents.back().attack.accepted==1 && world.ents[spy].camouflaged,"spy disguise remains a valid ability");world.presentationEvents.clear();
    EID yuri=world.spawnUnit(0,UnitType::Yuri,10.5f,30.5f);
    EID immune=world.spawnUnit(1,UnitType::RobotTank,35.5f,30.5f);
    command.ids={yuri};command.a=immune;world.applyCmd(0,command);
    check(world.presentationEvents.back().attack.reason==AttackRejectReason::Unsupported,"psychic immune target rejected");
    for(int scenario=0;scenario<3;++scenario) {
        reset();
        tank=world.spawnUnit(0,UnitType::Grizzly,8.5f,12.5f);
        EID target=world.spawnUnit(1,UnitType::MCV,scenario==0?24.5f:34.5f,12.5f);
        if(scenario<2) for(int x=20;x<=(scenario==0?22:31);++x) for(int y=0;y<64;++y) world.map.at(x,y).terrain=Terrain::Water;
        command.type=World::Cmd::Attack;command.ids={tank};command.a=target;world.applyCmd(0,command);
        if(scenario==1) {
            check(world.presentationEvents.back().attack.reason==AttackRejectReason::Unreachable && world.ents[tank].state==UState::Idle,"sealed region rejects impossible firing position");
            continue;
        }
        bool accepted=world.presentationEvents.back().attack.accepted==1;
        if(scenario==2) for(int x=20;x<=31;++x) for(int y=0;y<64;++y) world.map.at(x,y).terrain=Terrain::Water;
        World replay=world;replay.map.bldOccRef=&replay.bldOcc;bool deterministic=true;int reports=0;
        world.presentationEvents.clear();
        for(int frame=0;frame<1200;++frame) {
            world.update();replay.update();deterministic &= world.checksum()==replay.checksum();
            for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::AttackOrder && event.attack.reason==AttackRejectReason::Unreachable) ++reports;
            world.presentationEvents.clear();replay.presentationEvents.clear();
        }
        if(scenario==0) check(accepted && deterministic && world.combatDamage[0]>0 && world.ents[tank].x<20,"ranged unit reaches shore and damages inaccessible target");
        else {
            auto requests=world.repathRequests;
            for(int frame=0;frame<300;++frame) world.update();
            check(accepted && deterministic && reports==1 && world.ents[tank].state==UState::Idle && world.repathRequests==requests,"dynamic sealed route ends once without repeated pursuit");
        }
    }
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    plane=world.spawnUnit(1,UnitType::Intruder,14.5f,10.5f);world.ents[plane].state=UState::Landed;
    command.type=World::Cmd::Attack;command.ids={tank};command.a=plane;world.applyCmd(0,command);world.presentationEvents.clear();
    world.ents[plane].state=UState::Circling;world.airOccSync(plane);world.unitOccSync(plane);world.update();
    bool transition=false;
    for(const auto& event:world.presentationEvents) transition |= event.kind==PresentationKind::AttackOrder && event.attack.reason==AttackRejectReason::NoAntiAir;
    check(transition && world.ents[tank].target==INVALID_EID,"target takeoff invalidates ground-only pursuit with feedback");
    auto hash=world.checksum();world.ents[tank].feedbackAttackTarget=plane;
    check(hash==world.checksum(),"command feedback provenance is excluded from simulation hash");
    world.presentationEvents.clear();std::string previousMessage=msg;
    command.ids={plane};command.a=tank;world.applyCmd(1,command);consumePresentationEvents();
    bool filtered=msg==previousMessage;
    hash=world.checksum();world.applyCmd(0,command);
    check(filtered && world.presentationEvents.empty() && world.checksum()==hash,"other player feedback and foreign selection are filtered");
    reset();EID engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    EID oil=world.spawnBuilding(-1,BldType::OilDerrick,15,10,true);
    command.type=World::Cmd::Capture;command.ids={engineer};command.a=oil;world.applyCmd(0,command);
    for(int frame=0;frame<600 && world.ents[oil].player!=0;++frame) world.update();
    check(world.ents[oil].player==0 && !world.valid(engineer),"capture chase remains valid for unarmed engineer");
    world.damage(oil,bldDef(BldType::OilDerrick).hp/2,-1);
    engineer=world.spawnUnit(0,UnitType::Engineer,4.5f,10.5f);
    command.type=World::Cmd::Repair;command.ids={engineer};command.a=oil;world.applyCmd(0,command);
    for(int frame=0;frame<600 && world.valid(engineer);++frame) world.update();
    check(world.ents[oil].hp==bldDef(BldType::OilDerrick).hp && !world.valid(engineer),"repair chase remains valid for unarmed engineer");
    reset();oil=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);
    world.damage(oil,bldDef(BldType::OilDerrick).hp/2,-1);
    engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    command.type=World::Cmd::Repair;command.ids={engineer};command.a=oil;world.applyCmd(0,command);
    world.ents[oil].hp=bldDef(BldType::OilDerrick).hp; // Another repair finishes first.
    FILE* interactionFile=tmpfile();World interactionRestored;
    bool interactionReplay=interactionFile && world.saveGame(interactionFile);
    if(interactionReplay) { rewind(interactionFile);interactionReplay=interactionRestored.loadGame(interactionFile); }
    if(interactionFile) fclose(interactionFile);
    world.update();
    if(interactionReplay) { interactionRestored.update();interactionReplay=world.checksum()==interactionRestored.checksum(); }
    bool released=world.valid(engineer) && world.ents[engineer].target==INVALID_EID
        && world.ents[engineer].state==UState::Idle && world.ents[engineer].path.empty();
    auto repaths=world.repathRequests;
    for(int frame=0;frame<300;++frame) {
        world.update();
        if(interactionReplay) { interactionRestored.update();interactionReplay=world.checksum()==interactionRestored.checksum(); }
    }
    check(released && world.repathRequests==repaths && world.valid(engineer),"already repaired target releases engineer without consumption or repeated paths");
    check(interactionReplay,"pending completed repair resumes identically from saved simulation state");
    reset();oil=world.spawnBuilding(-1,BldType::OilDerrick,25,10,true);
    engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    command.type=World::Cmd::Capture;command.ids={engineer};command.a=oil;world.applyCmd(0,command);
    world.ents[oil].player=0; // A second friendly engineer captures it first.
    world.update();
    check(world.valid(engineer) && world.ents[engineer].state==UState::Idle && world.ents[engineer].target==INVALID_EID
        && world.ents[engineer].path.empty(),"already captured healthy target releases remaining engineer");
    reset();oil=world.spawnBuilding(-1,BldType::OilDerrick,25,10,true);
    engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    command.type=World::Cmd::Capture;command.ids={engineer};command.a=oil;world.applyCmd(0,command);
    world.damage(oil,100000,-1);world.update();
    check(world.valid(engineer) && world.ents[engineer].state==UState::Idle && world.ents[engineer].target==INVALID_EID
        && world.ents[engineer].path.empty(),"destroyed interaction target clears pending approach path");
    reset();
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    world.map.at(10,10).terrain=Terrain::Clear;
    tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    const int autoRange=world.effWeapon(world.ents[tank]).range;
    EID isolated=world.spawnUnit(1,UnitType::MCV,10.5f+autoRange+1,10.5f);
    world.map.at((int)world.ents[isolated].x,10).terrain=Terrain::Clear;
    EID isolatedSecond=world.spawnUnit(1,UnitType::MCV,10.5f,10.5f+autoRange+1);
    world.map.at(10,(int)world.ents[isolatedSecond].y).terrain=Terrain::Clear;
    auto requests=world.repathRequests;
    for(int frame=0;frame<120;++frame) world.update();
    check(world.repathRequests-requests<=2 && world.repathFailures>0,
        "automatic pursuit backs off an unreachable island target");
    check(world.ents[tank].failedPursuits.size()==2,"multiple unreachable targets are remembered without alternating paths");
    FILE* pursuitFile=tmpfile();World pursuitRestored;
    bool pursuitReplay=pursuitFile && world.saveGame(pursuitFile);
    if(pursuitReplay) { rewind(pursuitFile);pursuitReplay=pursuitRestored.loadGame(pursuitFile); }
    if(pursuitFile) fclose(pursuitFile);
    for(int frame=0;frame<300;++frame) {
        world.update();
        if(pursuitReplay) { pursuitRestored.update();pursuitReplay=world.checksum()==pursuitRestored.checksum(); }
    }
    check(pursuitReplay,"failed pursuit retry state survives save and 300 tick replay");
    auto beforeFailureChange=world.checksum();
    ++world.ents[tank].failedPursuits.front().retryTick;
    check(beforeFailureChange!=world.checksum(),"failed pursuit retry deadline participates in simulation checksum");
    --world.ents[tank].failedPursuits.front().retryTick;
    EID nearby=world.spawnUnit(1,UnitType::MCV,10.5f,12.5f);
    for(int frame=0;frame<100;++frame) world.update();
    check(world.ents[nearby].hp<unitDef(UnitType::MCV).hp,
        "unreachable target backoff does not suppress in-range defense");
    world.damage(nearby,100000,-1);world.orderStop({tank});
    for(int x=4;x<=10;++x) world.map.at(x,10).terrain=Terrain::Clear;
    EID reachable=world.spawnUnit(1,UnitType::MCV,4.5f,10.5f);
    for(int frame=0;frame<160;++frame) world.update();
    check(world.ents[reachable].hp<unitDef(UnitType::MCV).hp,
        "failed nearest targets allow another reachable out-of-range enemy");
    world.damage(reachable,100000,-1);world.orderStop({tank});
    for(auto& cell:world.map.cells) cell.terrain=Terrain::Clear;
    for(int frame=0;frame<400;++frame) world.update();
    check(world.ents[isolated].hp<unitDef(UnitType::MCV).hp || world.ents[isolatedSecond].hp<unitDef(UnitType::MCV).hp,
        "automatic pursuit recovers after a blocked route reopens");
    world.rememberPursuitFailure(tank,isolated);
    world.damage(isolated,100000,-1);
    EID reused=world.spawnUnit(1,UnitType::MCV,10.5f+autoRange+1,10.5f);
    bool oldFailure=false;for(const auto& failure:world.ents[tank].failedPursuits) oldFailure|=failure.target==reused;
    check(reused==isolated && !oldFailure,"reused entity slot does not inherit a previous target's pursuit failure");
    reset();for(auto& cell:world.map.cells) cell.terrain=Terrain::Water;
    world.map.at(10,10).terrain=Terrain::Clear;
    tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    std::vector<EID> islands;
    for(Vec2i offset:std::vector<Vec2i>{{6,0},{6,1},{6,-1},{0,6},{1,6},{-1,6},{-6,0},{-6,-1},{-6,1}})
        islands.push_back(world.spawnUnit(1,UnitType::MCV,10.5f+offset.x,10.5f+offset.y));
    requests=world.repathRequests;bool withinBudget=true;
    for(int frame=0;frame<120;++frame) { world.update();withinBudget &= world.repathsThisTick<=8; }
    check(world.ents[tank].failedPursuits.size()==8 && world.repathRequests-requests<=8 && withinBudget,
        "full failure memory defers new pursuits instead of cycling evicted targets");
    world.ents[islands.front()].x=15.5f;world.unitOccSync(islands.front());
    auto autoWeapon=world.effWeapon(world.ents[tank]);
    bool movedTarget=world.findNearestEnemy(0,10.5f,10.5f,7,true,&autoWeapon,UnitType::Grizzly,tank)==islands.front()
        && !world.activePursuitFailure(world.ents[tank],world.ents[tank].failedPursuits.front());
    world.ents[islands.front()].x=16.5f;world.unitOccSync(islands.front());
    world.ents[tank].x=9.5f;world.unitOccSync(tank);
    bool movedSeeker=!world.activePursuitFailure(world.ents[tank],world.ents[tank].failedPursuits.front());
    world.ents[tank].x=10.5f;world.unitOccSync(tank);
    check(movedTarget && movedSeeker,"moving target or seeker invalidates the old failed approach");
    const auto savedFailures=world.ents[tank].failedPursuits;
    auto acceptsPursuitSave=[&]() {
        FILE* file=tmpfile();World restored;
        bool ok=file && world.saveGame(file);
        if(ok) { rewind(file);ok=restored.loadGame(file); }
        if(file) fclose(file);return ok;
    };
    world.ents[tank].failedPursuits.front().retryTick=world.tick+181;
    bool rejectsDeadline=!acceptsPursuitSave();world.ents[tank].failedPursuits=savedFailures;
    world.ents[tank].failedPursuits.front().sourceX=-1;
    bool rejectsCell=!acceptsPursuitSave();world.ents[tank].failedPursuits=savedFailures;
    world.ents[tank].failedPursuits[1]=world.ents[tank].failedPursuits[0];
    bool rejectsDuplicate=!acceptsPursuitSave();world.ents[tank].failedPursuits=savedFailures;
    world.ents[tank].failedPursuits.push_back(world.ents[tank].failedPursuits.front());
    bool rejectsCount=!acceptsPursuitSave();world.ents[tank].failedPursuits=savedFailures;
    check(rejectsDeadline && rejectsCell && rejectsDuplicate && rejectsCount,
        "pursuit save validation rejects invalid deadline cells duplicates and oversized memory");
    for(int x=10;x<=16;++x) world.map.at(x,10).terrain=Terrain::Clear;
    command.type=World::Cmd::Attack;command.ids={tank};command.a=islands.front();world.applyCmd(0,command);
    bool explicitAccepted=world.presentationEvents.back().attack.accepted==1;
    for(int frame=0;frame<100;++frame) world.update();
    check(explicitAccepted && world.ents[islands.front()].hp<unitDef(UnitType::MCV).hp,
        "explicit attack bypasses automatic failure backoff when route is repaired");
    reset();EID helicopter=world.spawnUnit(0,UnitType::Nighthawk,10.5f,10.5f);
    plane=world.spawnUnit(1,UnitType::Intruder,14.5f,10.5f);world.ents[plane].state=UState::Landed;
    command.type=World::Cmd::Attack;command.ids={helicopter};command.a=plane;world.applyCmd(0,command);
    world.presentationEvents.clear();world.ents[plane].state=UState::Circling;world.airOccSync(plane);world.unitOccSync(plane);
    world.update();bool airRejected=false;
    for(const auto& event:world.presentationEvents) airRejected|=event.kind==PresentationKind::AttackOrder && event.attack.reason==AttackRejectReason::NoAntiAir;
    check(airRejected && world.ents[helicopter].target==INVALID_EID,
        "airborne ground-only attacker stops when its landed target takes off");
    reset();EID bomber=world.spawnUnit(0,UnitType::Kirov,10.5f,10.5f);
    EID captured=world.spawnUnit(1,UnitType::MCV,15.5f,10.5f);
    command.ids={bomber};command.a=captured;world.applyCmd(0,command);
    world.ents[captured].player=0;world.presentationEvents.clear();
    for(int frame=0;frame<160;++frame) world.update();
    check(world.ents[bomber].target==INVALID_EID && world.ents[captured].hp==unitDef(UnitType::MCV).hp && world.timedBombs.empty(),
        "aircraft drops attack when target becomes friendly without bombing the captured unit");
    reset();EID airfield=world.spawnBuilding(0,BldType::AirForceCmd,2,2,true);
    bomber=world.spawnUnit(0,UnitType::Intruder,25.5f,25.5f);
    captured=world.spawnUnit(1,UnitType::MCV,35.5f,25.5f);
    world.ents[bomber].airbase=airfield;world.ents[bomber].ammo=0;
    command.ids={bomber};command.a=captured;world.applyCmd(0,command);
    world.damage(captured,100000,-1);world.update();
    bool returningEmpty=world.valid(bomber) && world.ents[bomber].state==UState::Returning && world.ents[bomber].target==INVALID_EID;
    for(int frame=0;frame<500 && world.valid(bomber);++frame) world.update();
    check(returningEmpty && world.valid(bomber) && world.ents[bomber].state==UState::Landed && world.ents[bomber].ammo==unitDef(UnitType::Intruder).ammo,
        "empty aircraft with destroyed target returns lands and rearms");
    reset();EID carrier=world.spawnUnit(0,UnitType::AircraftCarrier,3.5f,3.5f);
    captured=world.spawnUnit(1,UnitType::MCV,12.5f,3.5f);
    command.ids={carrier};command.a=captured;world.applyCmd(0,command);
    EID hornet=INVALID_EID;
    for(int frame=0;frame<100 && hornet<0;++frame){world.update();for(const auto& node:world.aircraftSpawners.front().nodes)if(world.valid(node.aircraft))hornet=node.aircraft.id;}
    world.damage(captured,100000,-1);
    bool hornetReturning=world.valid(hornet) && world.ents[hornet].state==UState::Returning;
    for(int frame=0;frame<400 && world.valid(hornet);++frame) world.update();
    check(hornetReturning && !world.valid(hornet) && world.ents[carrier].cargo.size()==3,
        "hornet returns to carrier when target is lost with ammunition remaining");
    reset();helicopter=world.spawnUnit(0,UnitType::Rocketeer,10.5f,10.5f);
    captured=world.spawnUnit(1,UnitType::MCV,30.5f,10.5f);
    command.ids={helicopter};command.a=captured;world.applyCmd(0,command);
    for(int frame=0;frame<25;++frame) world.update();
    float hoverX=world.ents[helicopter].x,hoverY=world.ents[helicopter].y;
    world.damage(captured,100000,-1);world.update();
    FILE* hoverFile=tmpfile();World hoverReplay;
    bool hoverEqual=hoverFile && world.saveGame(hoverFile);
    if(hoverEqual) { rewind(hoverFile);hoverEqual=hoverReplay.loadGame(hoverFile); }
    if(hoverFile) fclose(hoverFile);
    for(int frame=0;frame<100;++frame) {
        world.update();
        if(hoverEqual) { hoverReplay.update();hoverEqual=world.checksum()==hoverReplay.checksum(); }
    }
    check(hoverEqual && world.ents[helicopter].target==INVALID_EID && world.ents[helicopter].x==hoverX && world.ents[helicopter].y==hoverY,
        "hover aircraft stays at pursuit endpoint after target loss with deterministic replay");
    reset();EID disc=world.spawnUnit(0,UnitType::FloatingDisc,18.5f,18.5f);
    EID plant=world.spawnBuilding(1,BldType::PowerPlant,20,20,true);
    command.ids={disc};command.a=plant;world.applyCmd(0,command);world.update();
    bool drained=world.ents[plant].drainedBy==disc;
    world.ents[plant].player=0;world.update();
    check(drained && world.ents[plant].drainedBy==INVALID_EID && world.ents[disc].target==INVALID_EID
        && world.players[0].powerMade>=bldDef(BldType::PowerPlant).power,
        "disc releases drain and restores power immediately after target capture");
    reset();helicopter=world.spawnUnit(0,UnitType::SiegeChopper,10.5f,10.5f);
    World::Cmd deploy{};deploy.type=World::Cmd::RadDeploy;deploy.ids={helicopter};world.applyCmd(0,deploy);
    for(int tick=0;tick<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks;++tick)world.update();
    plane=world.spawnUnit(1,UnitType::Intruder,14.5f,10.5f);world.ents[plane].state=UState::Landed;
    command.ids={helicopter};command.a=plane;world.applyCmd(0,command);world.presentationEvents.clear();
    world.ents[plane].state=UState::Circling;world.airOccSync(plane);world.unitOccSync(plane);world.update();
    airRejected=false;
    for(const auto& event:world.presentationEvents) airRejected|=event.kind==PresentationKind::AttackOrder && event.attack.reason==AttackRejectReason::NoAntiAir;
    check(airRejected && world.ents[helicopter].target==INVALID_EID && world.ents[helicopter].deployed
        && world.ents[helicopter].x==10.5f && world.ents[helicopter].y==10.5f,
        "deployed siege chopper rejects an airborne target while remaining deployed");
    reset();helicopter=world.spawnUnit(0,UnitType::Rocketeer,10.5f,10.5f);
    world.ents[helicopter].vetRank=1;
    captured=world.spawnUnit(1,UnitType::MCV,13.5f,10.5f);
    int effectiveCooldown=world.effWeapon(world.ents[helicopter]).cooldown;
    command.ids={helicopter};command.a=captured;world.applyCmd(0,command);world.update();
    check(effectiveCooldown<unitDef(UnitType::Rocketeer).weapon.cooldown && world.ents[helicopter].atkCd==effectiveCooldown,
        "aircraft uses effective veteran weapon cooldown");
    reset();tank=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    captured=world.spawnUnit(1,UnitType::MCV,14.5f,10.5f);
    command.ids={tank};command.a=captured;world.applyCmd(0,command);world.ents[captured].player=0;world.update();
    check(world.ents[tank].target==INVALID_EID && world.ents[captured].hp==unitDef(UnitType::MCV).hp,
        "ground attacker also releases a target captured by its side");
    world.ents[tank].x=40.5f;world.ents[tank].y=40.5f;world.unitOccSync(tank);
    world.ents[captured].x=41.5f;world.ents[captured].y=40.5f;world.unitOccSync(captured);
    world.ents[tank].confused=30;
    for(int frame=0;frame<20;++frame) world.update();
    check(world.ents[captured].hp<unitDef(UnitType::MCV).hp,
        "chaos confusion retains its intentional friendly-fire behavior");
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    oil=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);world.damage(oil,100,-1);
    command.type=World::Cmd::Repair;command.ids={engineer};command.a=oil;world.applyCmd(0,command);
    world.ents[oil].player=1;
    for(int frame=0;frame<600;++frame) world.update();
    check(world.ents[oil].player==1 && world.valid(engineer) && world.ents[engineer].target==INVALID_EID,
        "repair intent never turns into capture after the building changes owner");
    return failures;
}

int GameRegression::engineerRegression() {
    int failures=0;localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;camZoom=1;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("ENGINEER %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;world.map.reveal(0,32,32,64);moveFeedbackTimer=0;world.presentationEvents.clear();
    };
    auto issue=[&](EID actor,EID target,EngineerIntent intent) {
        World::Cmd cmd{};cmd.type=intent==EngineerIntent::Capture?World::Cmd::Capture:World::Cmd::Repair;
        cmd.ids={actor};cmd.a=target;world.applyCmd(0,cmd);
    };
    auto rejected=[&](EID actor,EID target,EngineerIntent intent,EngineerRejectReason reason,S text) {
        world.presentationEvents.clear();auto hash=world.checksum();issue(actor,target,intent);
        bool result=!world.presentationEvents.empty() && world.presentationEvents.back().kind==PresentationKind::EngineerOrder
            && world.presentationEvents.back().engineer.reason==reason && world.presentationEvents.back().engineer.rejected==1;
        consumePresentationEvents();return result && world.checksum()==hash && msg==TR(text) && moveFeedbackRejected;
    };
    reset();EID engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    EID tank=world.spawnUnit(0,UnitType::Grizzly,7.5f,10.5f);
    EID oil=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);
    World::Cmd move{};move.type=World::Cmd::Move;move.ids={engineer};move.x=5;move.y=20;world.applyCmd(0,move);
    move.a=1;move.y=25;world.applyCmd(0,move);
    check(rejected(engineer,oil,EngineerIntent::Repair,EngineerRejectReason::FullHealth,S::MsgRepairFullHealth)
        && world.ents[engineer].wps.size()==1,"full-health repair rejection preserves route and waypoint queue");
    check(rejected(engineer,oil,EngineerIntent::Capture,EngineerRejectReason::CaptureOwner,S::MsgCaptureOwner),"owned capture target explains ownership restriction");
    check(rejected(tank,oil,EngineerIntent::Repair,EngineerRejectReason::NoEngineer,S::MsgEngineerRequired),"non-engineer selection gets explicit reason");
    check(rejected(engineer,tank,EngineerIntent::Repair,EngineerRejectReason::InvalidTarget,S::MsgEngineerInvalid),"non-building target is rejected atomically");
    check(rejected(engineer,99999,EngineerIntent::Capture,EngineerRejectReason::InvalidTarget,S::MsgEngineerInvalid)
        && moveFeedbackTimer==0,"stale target has no fabricated marker");
    world.ents[oil].player=1;
    check(rejected(engineer,oil,EngineerIntent::Repair,EngineerRejectReason::RepairOwner,S::MsgRepairOwner),"enemy building cannot receive repair intent");
    EID wall=world.spawnBuilding(1,BldType::Wall,25,20,true);
    check(rejected(engineer,wall,EngineerIntent::Capture,EngineerRejectReason::NotCapturable,S::MsgNotCapturable),"uncapturable building explains rejection");
    camX=-500;camY=0;sel={engineer};sideMode=0;placing=false;dragging=false;
    auto anchor=bldScreenPos(world.ents[oil]);
    auto cursorHash=world.checksum();updateHoverCursor((int)anchor.x,(int)anchor.y-20);
    bool captureCursor=cursorKind==CursorKind::Enter && world.checksum()==cursorHash;
    world.ents[oil].player=0;world.ents[oil].hp-=100;
    cursorHash=world.checksum();updateHoverCursor((int)anchor.x,(int)anchor.y-20);
    bool repairCursor=cursorKind==CursorKind::Repair && world.checksum()==cursorHash;
    world.ents[oil].player=1;world.ents[oil].hp+=100;
    check(captureCursor && repairCursor,"hover cursor identifies capture and damaged friendly repair without mutating simulation");
    World::Cmd partial{};partial.type=World::Cmd::Capture;partial.ids={engineer,tank};partial.a=oil;world.applyCmd(0,partial);
    bool accepted=world.presentationEvents.back().engineer.accepted==1 && world.presentationEvents.back().engineer.rejected==1;
    auto hash=world.checksum();consumePresentationEvents();
    check(accepted && moveFeedbackPartial && msg==TextFormat(TR(S::MsgEngineerPartial),1,TR(S::MsgEngineerRequired))
        && hash==world.checksum() && world.ents[engineer].wps.empty(),"mixed selection reports partial capture and clears accepted engineer queue");
    hash=world.checksum();world.ents[engineer].feedbackEngineerTarget=INVALID_EID;
    check(hash==world.checksum(),"engineer feedback provenance is excluded from simulation checksum");
    world.ents[engineer].feedbackEngineerTarget=oil;
    world.ents[oil].player=0;world.damage(oil,100,-1);world.presentationEvents.clear();world.update();
    consumePresentationEvents();
    check(world.valid(engineer) && world.ents[engineer].state==UState::Idle && world.ents[engineer].target==INVALID_EID
        && world.ents[oil].hp==bldDef(BldType::OilDerrick).hp-100 && msg==TR(S::MsgCaptureOwner),
        "capture race never changes intent to repairing a newly friendly damaged building");
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);oil=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);world.damage(oil,100,-1);
    issue(engineer,oil,EngineerIntent::Repair);world.presentationEvents.clear();world.ents[oil].player=1;
    FILE* file=tmpfile();World restored;
    bool resumed=file && world.saveGame(file);
    if(resumed) { rewind(file);resumed=restored.loadGame(file); }
    if(file) fclose(file);
    resumed=resumed && restored.checksum()==world.checksum() && restored.ents[engineer].engineerIntent==EngineerIntent::Repair;
    for(int frame=0;frame<600;++frame) {
        world.update();if(resumed) { restored.update();resumed=world.checksum()==restored.checksum(); }
    }
    int reports=0;for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::EngineerOrder && event.engineer.reason==EngineerRejectReason::RepairOwner) ++reports;
    consumePresentationEvents();
    check(resumed && reports==1 && world.ents[oil].player==1 && world.valid(engineer) && msg==TR(S::MsgRepairOwner),
        "saved repair intent stops after capture with one local failure and identical resumed simulation");
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);oil=world.spawnBuilding(-1,BldType::OilDerrick,25,10,true);
    for(int y=0;y<64;++y) for(int x=12;x<=19;++x) world.map.at(x,y).terrain=Terrain::Water;
    check(rejected(engineer,oil,EngineerIntent::Capture,EngineerRejectReason::Unreachable,S::MsgEngineerUnreachable),"initial unreachable building is rejected before changing orders");
    for(auto& cell:world.map.cells) cell=Cell{};
    issue(engineer,oil,EngineerIntent::Capture);world.presentationEvents.clear();
    for(int y=0;y<64;++y) for(int x=12;x<=19;++x) world.map.at(x,y).terrain=Terrain::Water;
    for(int frame=0;frame<150;++frame) world.update();
    reports=0;for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::EngineerOrder && event.engineer.reason==EngineerRejectReason::Unreachable) ++reports;
    auto paths=world.repathRequests;
    for(int frame=0;frame<300;++frame) world.update();
    check(reports==1 && paths==world.repathRequests && world.valid(engineer) && world.ents[engineer].target==INVALID_EID,
        "dynamic sealed approach stops and reports once without repeating paths");
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);oil=world.spawnBuilding(-1,BldType::OilDerrick,25,10,true);
    issue(engineer,oil,EngineerIntent::Capture);world.presentationEvents.clear();world.damage(oil,100000,-1);world.update();consumePresentationEvents();
    check(msg==TR(S::MsgEngineerInvalid) && moveFeedbackTimer==0 && world.valid(engineer),"destroyed building releases engineer and reports without stale marker");
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);oil=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);world.damage(oil,100,-1);
    issue(engineer,oil,EngineerIntent::Repair);world.presentationEvents.clear();world.ents[oil].hp=bldDef(BldType::OilDerrick).hp;world.update();consumePresentationEvents();
    check(msg==TR(S::MsgRepairFullHealth) && world.valid(engineer) && world.ents[engineer].engineerIntent==EngineerIntent::None,
        "competing repair completion explains release without consuming engineer");
    // A cancelled order must not later emit an interaction failure.
    world.damage(oil,100,-1);issue(engineer,oil,EngineerIntent::Repair);world.presentationEvents.clear();
    World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={engineer};world.applyCmd(0,stop);world.ents[oil].player=1;world.update();
    bool quiet=true;for(const auto& event:world.presentationEvents) quiet &= event.kind!=PresentationKind::EngineerOrder;
    check(quiet && world.ents[engineer].engineerIntent==EngineerIntent::None,"stop cancels engineer intent without delayed failure");
    EID foreign=world.spawnUnit(1,UnitType::Engineer,5.5f,12.5f);
    World::Cmd foreignCmd{};foreignCmd.type=World::Cmd::Repair;foreignCmd.ids={foreign};foreignCmd.a=oil;
    std::string previousMessage=msg;world.applyCmd(1,foreignCmd);auto foreignHash=world.checksum();consumePresentationEvents();
    world.applyCmd(0,foreignCmd);
    check(msg==previousMessage && world.checksum()==foreignHash && world.presentationEvents.empty(),"other player engineer feedback and foreign selections are filtered");
    world.ents[engineer].engineerIntent=(EngineerIntent)255;
    file=tmpfile();bool badIntent=file && world.saveGame(file);
    if(badIntent) { rewind(file);badIntent=!restored.loadGame(file); }
    if(file) fclose(file);
    world.ents[engineer].engineerIntent=EngineerIntent::None;
    check(badIntent,"save reader rejects unknown engineer intent enum");
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    EID bunker=world.spawnBuilding(0,BldType::BattleBunker,25,10,true);world.damage(bunker,100,-1);
    camX=-500;camY=0;sel={engineer};sideMode=0;placing=false;dragging=false;
    anchor=bldScreenPos(world.ents[bunker]);updateHoverCursor((int)anchor.x,(int)anchor.y-20);
    issueSmartOrder((int)anchor.x,(int)anchor.y-20);
    check(cursorKind==CursorKind::Repair && world.ents[engineer].engineerIntent==EngineerIntent::Repair,
        "smart right-click repair matches cursor instead of garrisoning engineer in damaged bunker");
    world.orderRepair({engineer},bunker);world.presentationEvents.clear();
    world.ents[engineer].target=INVALID_EID;world.ents[engineer].confused=30;world.update();
    quiet=true;for(const auto& event:world.presentationEvents) quiet &= event.kind!=PresentationKind::EngineerOrder;
    check(quiet,"internal engineer order interrupted by confusion does not invent player feedback");
#ifdef RA2_TESTING
    extern bool writeLegacyV20(const World&,FILE*);
    reset();engineer=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);oil=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);world.damage(oil,100,-1);
    issue(engineer,oil,EngineerIntent::Repair);
    file=tmpfile();bool migrated=file && writeLegacyV20(world,file);
    if(migrated) { rewind(file);migrated=restored.loadGame(file); }
    if(file) fclose(file);
    World legacyExpected=world;legacyExpected.map.bldOccRef=&legacyExpected.bldOcc;legacyExpected.aircraftSpawnersEnabled=false;legacyExpected.aswEnabled=false;legacyExpected.nativeFlightEnabled=false;legacyExpected.nativeJumpjetEnabled=false;
    legacyExpected.veterancyProgressEnabled=false;legacyExpected.independentSupportEnabled=false;legacyExpected.supportDeliveryEnabled=false;
    check(migrated && restored.ents[engineer].engineerIntent==EngineerIntent::Repair && legacyExpected.checksum()==restored.checksum(),
        "frozen v20 binary explicitly infers pending repair intent");
    world.ents[oil].player=-1;issue(engineer,oil,EngineerIntent::Capture);
    file=tmpfile();migrated=file && writeLegacyV20(world,file);
    if(migrated) { rewind(file);migrated=restored.loadGame(file); }
    if(file) fclose(file);
    legacyExpected=world;legacyExpected.map.bldOccRef=&legacyExpected.bldOcc;legacyExpected.aircraftSpawnersEnabled=false;legacyExpected.aswEnabled=false;legacyExpected.nativeFlightEnabled=false;legacyExpected.nativeJumpjetEnabled=false;
    legacyExpected.veterancyProgressEnabled=false;legacyExpected.independentSupportEnabled=false;legacyExpected.supportDeliveryEnabled=false;
    check(migrated && restored.ents[engineer].engineerIntent==EngineerIntent::Capture && legacyExpected.checksum()==restored.checksum(),
        "frozen v20 binary explicitly infers pending capture intent");
#endif
    return failures;
}
