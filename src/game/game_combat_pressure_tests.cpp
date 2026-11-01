#include "game/game.h"
#include "sfx/sound.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <set>

namespace {
using PressureClock=std::chrono::steady_clock;
double elapsedMs(PressureClock::time_point start) {
    return std::chrono::duration<double,std::milli>(PressureClock::now()-start).count();
}
double percentile(std::vector<double> values,int percent) {
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());return values[(values.size()-1)*percent/100];
}
int aliveUnits(const World& w,int player=-1) {
    return (int)std::count_if(w.ents.begin(),w.ents.end(),[&](const auto& e){return e.alive && !e.isBuilding && (player<0 || e.player==player);});
}
}

int Game::combatPressureRegression(int scenarioFilter,const char* outputDir) {
    if(scenarioFilter < -1 || scenarioFilter > 3)return 1;
    int failures=0;g_sfx.setMasterVol(0);
    std::filesystem::create_directories(outputDir);
#ifdef RA2_ASAN_ACTIVE
    constexpr bool enforcePerformance=false;
#else
    constexpr bool enforcePerformance=true;
#endif
    for(int scenario=0;scenario<4;++scenario) {
        if(scenarioFilter>=0 && scenarioFilter!=scenario)continue;
        const bool dense=scenario>=2;const int count=scenario%2?500:200;
        const int warm=dense?0:30,frames=dense?300:150;
        World w;w.init(160,160,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.cratesEnabled=false;w.crates.clear();w.aiAlliance=false;
        for(auto& player:w.players){player.defeated=false;player.isAI=false;}
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        constexpr UnitType pairTypes[]={UnitType::Grizzly,UnitType::Rhino,UnitType::Apocalypse,UnitType::IFV};
        constexpr UnitType denseTypes[]={UnitType::GI,UnitType::Conscript,UnitType::Grizzly,UnitType::Rhino,UnitType::IFV,UnitType::GatlingTank};
        std::vector<std::pair<EID,EID>> pairs;
        int sequence[2]={},replaySequence[2]={};
        bool admitted=true,commands=true,equal=true,legal=true,budget=true,saved=false,isolation=true;
        auto reinforce=[&](World& target,int (&cursor)[2]) {
            int added=0;
            for(int side=0;side<2;++side) {
                int need=count/2-aliveUnits(target,side);std::vector<EID> group;
                for(int item=0;item<need;++item) {
                    Vec2i spot{-1,-1};
                    for(int attempt=0;attempt<950;++attempt) {
                        // Exhaust the original staging bank before using its
                        // backup area; the unit sequence also chooses types.
                        const int slot=attempt<250?(cursor[side]+attempt)%250
                            :250+(cursor[side]+attempt-250)%700;
                        const int x=slot<250?(side?68:52)+slot%10:(side?120:10)+(slot-250)%20;
                        const int y=slot<250?50+slot/10:45+(slot-250)/20;
                        if(target.unitAtCell(x,y)==INVALID_EID){spot={x,y};break;}
                    }
                    if(spot.x<0){admitted=false;break;}
                    const auto type=denseTypes[cursor[side]++%std::size(denseTypes)];
                    group.push_back(target.spawnUnit(side,type,spot.x+0.5f,spot.y+0.5f));++added;
                }
                if(!group.empty()) {
                    World::Cmd command{};command.type=World::Cmd::Move;command.attackMove=true;command.ids=group;
                    command.x=side?56.5f:72.5f;command.y=62.5f;target.applyCmd(side,command);
                    const auto receipt=std::find_if(target.presentationEvents.rbegin(),target.presentationEvents.rend(),
                        [&](const auto& event){return event.kind==PresentationKind::MoveOrder && event.player==side;});
                    admitted &= receipt!=target.presentationEvents.rend();
                    if(receipt!=target.presentationEvents.rend())admitted &= receipt->move.rejected==0
                        && receipt->move.accepted+receipt->move.queued==(int)group.size();
                }
            }
            return added;
        };
        double initialInput=-1;
        if(dense) {
            const auto start=PressureClock::now();const int added=reinforce(w,sequence);initialInput=elapsedMs(start);
            const int other=reinforce(replay,replaySequence);
            commands &= added==count && other==count && sequence[0]==replaySequence[0] && sequence[1]==replaySequence[1];
        } else for(int pair=0;pair<count/2;++pair) {
            const float x=6.5f+pair%16*9,y=6.5f+pair/16*9;
            const auto type=pairTypes[pair%std::size(pairTypes)];
            const EID a=w.spawnUnit(0,type,x,y),b=w.spawnUnit(1,type,x+1,y);
            const EID ra=replay.spawnUnit(0,type,x,y),rb=replay.spawnUnit(1,type,x+1,y);
            commands &= a==ra && b==rb;pairs.push_back({a,b});
        }
        admitted &= aliveUnits(w)==count && aliveUnits(replay)==count;
        for(int side=0;side<2;++side){w.updateFog(side);replay.updateFog(side);}
        if(!dense)for(auto [a,b]:pairs)for(int side=0;side<2;++side) {
            World::Cmd command{};command.type=World::Cmd::Attack;command.ids={side?b:a};command.a=side?a:b;
            w.applyCmd(side,command);replay.applyCmd(side,command);
            commands &= w.ents[command.ids.front()].target==command.a;
        }
        commands &= w.checksum()==replay.checksum();
        std::vector<double> times,setups;std::set<EID> fired;
        uint64_t damageBefore[2]={},damage[2]={};int minBefore=count,maxBefore=0,minAfter=count,peakProjectiles=0,checks=0;
        int deaths=0,replacements=0,firstMismatch=-1;uint64_t shots=0;
        World renderSnapshot;bool renderCaptured=false;int renderLoad=-1;
        for(int frame=0;frame<warm+frames;++frame) {
            const auto setupStart=PressureClock::now();
            const int added=dense?reinforce(w,sequence):0;
            const double setup=elapsedMs(setupStart);
            const int otherAdded=dense?reinforce(replay,replaySequence):0;
            commands &= added==otherAdded && sequence[0]==replaySequence[0] && sequence[1]==replaySequence[1];
            const int before=aliveUnits(w);
            if(frame==warm){damageBefore[0]=w.combatDamage[0];damageBefore[1]=w.combatDamage[1];}
            const auto start=PressureClock::now();w.update();const double elapsed=elapsedMs(start);
            replay.update();const bool same=w.checksum()==replay.checksum();++checks;
            if(!same && firstMismatch<0)firstMismatch=frame+1;equal &= same;
            budget &= w.repathsThisTick<=8 && replay.repathsThisTick<=8;
            const int after=aliveUnits(w);
            const int projectiles=(int)std::count_if(w.projs.begin(),w.projs.end(),[](const auto& p){return p.alive;});
            if(frame>=warm) {
                times.push_back(elapsed);setups.push_back(setup);minBefore=std::min(minBefore,before);maxBefore=std::max(maxBefore,before);minAfter=std::min(minAfter,after);
                replacements+=added;deaths+=before-after;peakProjectiles=std::max(peakProjectiles,projectiles);
                for(const auto& event:w.presentationEvents)if(event.kind==PresentationKind::Muzzle){++shots;fired.insert(event.entity);}
            }
            const int activeEffects=(int)std::count_if(w.effects.begin(),w.effects.end(),[](const auto& effect){return effect.alive;});
            if(dense && after==count && projectiles>0 && projectiles+activeEffects>renderLoad) {
                renderSnapshot=w;renderSnapshot.map.bldOccRef=&renderSnapshot.bldOcc;renderCaptured=true;
                renderLoad=projectiles+activeEffects;
            }
            std::vector<int> foot(w.map.cells.size(),0),vehicles(w.map.cells.size(),0);
            for(const auto& e:w.ents)if(e.alive && !e.isBuilding) {
                const int x=(int)e.x,y=(int)e.y;
                if(!w.passableFor(x,y,unitDef(e.utype).pathDomain()) || w.bldBlocked(x,y)){legal=false;continue;}
                const int cell=w.cellIdx(x,y);const bool infantry=unitDef(e.utype).isInfantry();
                legal &= e.occCell==cell && (infantry?!vehicles[cell] && foot[cell]<3:!vehicles[cell] && !foot[cell]);
                if(infantry)++foot[cell];else ++vehicles[cell];
            }
            if(frame==warm+frames/2-1) {
                const auto hash=w.checksum();FILE* file=tmpfile();saved=file && w.saveGame(file);
                if(saved){rewind(file);saved=replay.loadGame(file);}if(file)fclose(file);
                replaySequence[0]=sequence[0];replaySequence[1]=sequence[1];
                isolation &= w.checksum()==hash;equal &= saved && replay.checksum()==hash;
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        damage[0]=w.combatDamage[0]-damageBefore[0];damage[1]=w.combatDamage[1]-damageBefore[1];
        const double p95=percentile(times,95),maximum=percentile(times,100);
        const bool battle=damage[0]>0 && damage[1]>0 && shots>0 && peakProjectiles>0 && fired.size()>=(size_t)count/2;
        const bool population=minBefore==count && maxBefore==count && (dense?deaths>0 && replacements>0:minAfter==count && fired.size()==(size_t)count);
        const bool ok=admitted && commands && equal && legal && budget && saved && isolation && battle && population
            && checks==warm+frames && (!enforcePerformance || p95<=33.3) && (!dense || renderCaptured);
        failures+=!ok;
        std::printf("COMBAT_PRESSURE %s scenario=%d mode=%s initial=%d macro_ai=0 frames=%zu warm=%d live_before_min=%d live_before_max=%d live_after_min=%d deaths=%d replacements=%d shots=%llu firing_slots=%zu damage0=%llu damage1=%llu peak_projectiles=%d commands=%d legal=%d replay=%d checks=%d first_mismatch=%d budget=%d saved=%d isolation=%d admitted=%d p95_ms=%.3f max_ms=%.3f setup_p95_ms=%.3f initial_input_ms=%.3f repaths=%llu repath_failures=%llu perf_enforced=%d render_captured=%d\n",ok?"PASS":"FAIL",scenario,dense?"reinforced":"paired",count,times.size(),warm,minBefore,maxBefore,minAfter,deaths,replacements,(unsigned long long)shots,fired.size(),(unsigned long long)damage[0],(unsigned long long)damage[1],peakProjectiles,commands,legal,equal,checks,firstMismatch,budget,saved,isolation,admitted,p95,maximum,percentile(setups,95),initialInput,(unsigned long long)w.repathRequests,(unsigned long long)w.repathFailures,enforcePerformance,renderCaptured);
        if(dense && renderCaptured) {
            const auto primaryHash=w.checksum(),replicaHash=replay.checksum();
            world=renderSnapshot;world.map.bldOccRef=&world.bldOcc;
            world.map.reveal(0,80,80,160);ais.clear();localPlayer=0;phase=Phase::InGame;
            gameOver=false;showMenu=false;paused=false;sel.clear();combatCues.clear();
            camZoom=0.5f;interpAlpha=1;camX=(65-62)*32-(SCREEN_W-sidebarW)/(2*camZoom);
            camY=(65+62)*16-(SCREEN_H-BOTTOM_BAR_H)/(2*camZoom);
            sim.active=true;sim.pos={-100,-100};sim.keysDown.clear();sim.keysPressed.clear();
            sim.pressedL=sim.pressedR=sim.downL=sim.downR=false;
            SetTargetFPS(0);shotAtDisplaySize=true;bakeTerrain();minimapTimer=0;
            consumePresentationEvents();const auto renderHash=world.checksum();
            int inView=0,visible=0;
            std::vector<Rectangle> regions;
            for(const auto& actor:world.ents)if(actor.alive && !actor.isBuilding) {
                const auto rect=unitScreenRect(actor);regions.push_back(rect);
                inView+=rect.x>=0 && rect.y>=0 && rect.x+rect.width<=(SCREEN_W-sidebarW)/camZoom
                    && rect.y+rect.height<=(SCREEN_H-BOTTOM_BAR_H)/camZoom;
                visible+=world.visibleTo(actor,localPlayer);
            }
            const int capturedProjectiles=(int)std::count_if(world.projs.begin(),world.projs.end(),[](const auto& p){return p.alive;});
            const int capturedEffects=(int)std::count_if(world.effects.begin(),world.effects.end(),[](const auto& effect){return effect.alive;});
            for(const Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                SetWindowSize(size.x,size.y);for(int i=0;i<30;++i)render();
                std::vector<double> rendering;
                for(int i=0;i<120;++i){const auto start=PressureClock::now();render();rendering.push_back(elapsedMs(start));}
                const auto path=std::filesystem::path(outputDir)/("combat_"+std::to_string(scenario)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png");
                shotFile=path.string();render();Image actual=LoadImage(path.string().c_str());
                World fullFrame=world;fullFrame.map.bldOccRef=&fullFrame.bldOcc;
                for(auto& actor:world.ents)if(!actor.isBuilding)actor.alive=false;
                world.projs.clear();world.effects.clear();
                const auto emptyPath=path.parent_path()/(path.stem().string()+"_empty.png");
                shotFile=emptyPath.string();render();Image empty=LoadImage(emptyPath.string().c_str());
                world=std::move(fullFrame);world.map.bldOccRef=&world.bldOcc;
                const bool dimensions=actual.data && empty.data && actual.width==size.x && actual.height==size.y
                    && empty.width==size.x && empty.height==size.y;
                Color* full=dimensions?LoadImageColors(actual):nullptr;
                Color* background=dimensions?LoadImageColors(empty):nullptr;
                int changedRegions=0;
                if(full && background)for(const auto& rect:regions) {
                    const float scaleX=camZoom*size.x/SCREEN_W,scaleY=camZoom*size.y/SCREEN_H;
                    const int x0=std::clamp((int)(rect.x*scaleX),0,size.x-1),y0=std::clamp((int)(rect.y*scaleY),0,size.y-1);
                    const int x1=std::clamp((int)((rect.x+rect.width)*scaleX)+1,0,size.x),y1=std::clamp((int)((rect.y+rect.height)*scaleY)+1,0,size.y);
                    int changed=0;
                    for(int y=y0;y<y1 && changed<3;++y)for(int x=x0;x<x1 && changed<3;++x) {
                        const auto a=full[y*size.x+x],b=background[y*size.x+x];
                        changed+=std::abs((int)a.r-b.r)+std::abs((int)a.g-b.g)+std::abs((int)a.b-b.b)>20;
                    }
                    changedRegions+=changed>=3;
                }
                if(full)UnloadImageColors(full);if(background)UnloadImageColors(background);
                if(actual.data)UnloadImage(actual);if(empty.data)UnloadImage(empty);
                const bool unchanged=renderHash==world.checksum() && primaryHash==w.checksum() && replicaHash==replay.checksum();
                const bool rendered=dimensions && inView==count && visible==count && changedRegions>=count*8/10 && capturedProjectiles>0 && unchanged;
                failures+=!rendered;
                std::printf("COMBAT_RENDER %s scenario=%d size=%dx%d spectator=1 actors=%zu in_view=%d visible=%d projectiles=%d effects=%d snapshot_tick=%llu zoom=%.2f changed_regions=%d dimensions=%d simulation_unchanged=%d render_p95_ms=%.3f\n",rendered?"PASS":"FAIL",scenario,size.x,size.y,regions.size(),inView,visible,capturedProjectiles,capturedEffects,(unsigned long long)world.tick,camZoom,changedRegions,dimensions,unchanged,percentile(rendering,95));
            }
            sim.active=false;shotAtDisplaySize=false;SetTargetFPS(60);
        }
        std::fflush(stdout);
    }
    return failures;
}
