#include "game/game.h"
#include <algorithm>
#include <cstdio>

// Four opposed streams share a real junction. A second layout adds a loop;
// batches continue entering while earlier traffic is still in the network.
int Game::junctionRegression(int scenarioFilter) {
    if(scenarioFilter < -1 || scenarioFilter > 7)return 1;
    int failures=0;
    for(int scenario=0;scenario<(scenarioFilter<0?4:8);++scenario) {
        if(scenarioFilter>=0 && scenarioFilter!=scenario)continue;
        const bool loop=scenario==1 || scenario>=3,dynamic=scenario>=6;
        const int waves=scenario==2 || scenario==3 || dynamic?4:1;
        World w;w.init(96,96,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Water;}
        auto clear=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)w.map.at(x,y).terrain=Terrain::Clear;
        };
        clear(4,32,23,63);clear(72,32,91,63);clear(32,4,63,23);clear(32,72,63,91);
        clear(24,47,71,47);clear(47,24,47,71);clear(45,45,49,49);
        if(loop){clear(24,40,71,40);clear(24,55,71,55);clear(40,24,40,71);clear(55,24,55,71);}
        if(scenario==4)w.map.at(23,44).terrain=Terrain::Water; // A break in the west passing apron.
        if(scenario==5)std::reverse(w.map.cells.begin(),w.map.cells.end());
        w.cratesEnabled=false;w.aiAlliance=true;
        for(auto& player:w.players){player.isAI=true;player.money=0;player.defeated=false;}
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        std::vector<EID> units;std::vector<int> destinations;
        bool admitted=true,legal=true,smooth=true,equal=true,budget=true,goals=true,groups=true;int groupChecks=0;
        int spawned=0,snapshots=0,finished=-1,maxBlock=0;
        auto atBank=[](float x,float y,int side) {
            return side==0?x>=72:side==1?x<24:side==2?y>=72:y<24;
        };
        auto spawn=[&](int wave) {
            for(int side=0;side<4;++side) {
                std::vector<EID> group;const int count=side%2?12:13;
                for(int index=0;index<count;++index) {
                    float x=8.5f+index%3,y=34.5f+wave*6+index/3;
                    if(side==1)x+=76;
                    if(side>=2){std::swap(x,y);if(side==3)y+=76;}
                    if(scenario==5){x=96-x;y=96-y;}
                    if(w.unitAtCell((int)x,(int)y)!=INVALID_EID){admitted=false;continue;}
                    EID id=w.spawnUnit(side&1,UnitType::Grizzly,x,y);
                    EID other=replay.spawnUnit(side&1,UnitType::Grizzly,x,y);
                    equal &= id==other;units.push_back(id);destinations.push_back(scenario==5?side^1:side);group.push_back(id);
                }
                World::Cmd command{};command.type=World::Cmd::Move;command.ids=group;
                command.x=side==0?76:side==1?20:60-wave*6;
                command.y=side==2?76:side==3?20:60-wave*6;
                if(scenario==5){command.x=96-command.x;command.y=96-command.y;}
                w.applyCmd(side&1,command);replay.applyCmd(side&1,command);
                for(EID id:group){const auto& e=w.ents[id];
                    admitted &= !e.path.empty() && e.pathIdx<(int)e.path.size();
                    if(!e.path.empty()){const auto goal=e.path.back();goals &= atBank(goal.x+0.5f,goal.y+0.5f,scenario==5?side^1:side);}
                }
            }
        };
        int arrived=0;
        EID obstacle=INVALID_EID;int obstacleTick=-1,obstacleX=-1,obstacleY=-1,closedExit=-1;
        bool removed=false,dynamicLegal=true;int splitChecks=0,rejoinedChecks=0;
        uint64_t clearWait=0,occupiedWait=0;EID worst=INVALID_EID;
        for(int frame=0;frame<24000;++frame) {
            if(spawned<waves && frame==spawned*240)spawn(spawned++);
            if(dynamic && obstacleTick<0 && frame>=600 && frame<1200) {
                int x=23,y=44,exit=-1;
                if(scenario==7) {
                    x=y=-1;const auto hash=w.checksum();
                    for(const auto& gate:w.trafficDebug())if(gate.domain==0 && gate.cells>200) {
                        exit=gate.exit;x=exit%w.map.w;y=exit/w.map.w;
                        if(x==23)++x;else if(x==72)--x;else if(y==23)++y;else if(y==72)--y;else x=y=-1;
                        break;
                    }
                    equal &= hash==w.checksum();
                }
                if(w.map.inBounds(x,y) && w.unitAtCell(x,y)==INVALID_EID && replay.unitAtCell(x,y)==INVALID_EID) {
                    obstacle=w.spawnBuilding(-1,BldType::Wall,x,y,true);
                    const EID other=replay.spawnBuilding(-1,BldType::Wall,x,y,true);
                    dynamicLegal &= obstacle==other && w.valid(obstacle) && w.bldAt(x,y)==obstacle;
                    obstacleTick=frame;obstacleX=x;obstacleY=y;closedExit=exit;
                }
            }
            if(dynamic && obstacleTick>=0 && !removed && frame==obstacleTick+1000) {
                w.kill(obstacle,false);replay.kill(obstacle,false);removed=true;
                dynamicLegal &= !w.valid(obstacle) && !replay.valid(obstacle);
            }
            std::vector<Vec2f> before;
            for(EID id:units)before.push_back({w.ents[id].x,w.ents[id].y});
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            if(dynamic && obstacleTick>=0) {
                const auto hash=w.checksum();
                for(const auto& gate:w.trafficDebug())if(gate.cells>200) {
                    if(!removed){dynamicLegal &= gate.groups>4;++splitChecks;}
                    else {dynamicLegal &= gate.groups==4;++rejoinedChecks;}
                }
                equal &= hash==w.checksum();
            }
            std::vector<EID> occupied(w.map.cells.size(),INVALID_EID);arrived=0;
            for(size_t index=0;index<units.size();++index) {
                const auto& e=w.ents[units[index]];const int x=(int)e.x,y=(int)e.y;
                smooth &= distf(before[index].x,before[index].y,e.x,e.y)<=1.f/unitDef(e.utype).speed+0.0001f;
                legal &= w.valid(units[index]) && w.map.inBounds(x,y) && w.passableFor(x,y,0) && !w.bldBlocked(x,y);
                if(w.map.inBounds(x,y)){
                    const int cell=w.cellIdx(x,y);legal &= e.occCell==cell && occupied[cell]==INVALID_EID;occupied[cell]=units[index];
                }
                maxBlock=std::max(maxBlock,e.blockTick);
                if(e.blockTick>0 && e.pathIdx<(int)e.path.size() && distf(before[index].x,before[index].y,e.x,e.y)<0.00001f) {
                    const auto next=e.path[e.pathIdx];
                    if(w.unitAtCell(next.x,next.y)==INVALID_EID)++clearWait;else ++occupiedWait;
                }
                if(worst==INVALID_EID || e.blockTick>w.ents[worst].blockTick)worst=units[index];
                if(!e.path.empty()){const auto goal=e.path.back();goals &= atBank(goal.x+0.5f,goal.y+0.5f,destinations[index]);}
                arrived+=e.state==UState::Idle && !e.path.empty() && e.pathIdx==(int)e.path.size() && atBank(e.x,e.y,destinations[index]);
            }
            if(scenario==3 && (frame+1)%1000==0 && w.valid(worst)) {
                const auto& e=w.ents[worst];const auto next=e.pathIdx<(int)e.path.size()?e.path[e.pathIdx]:Vec2i{-1,-1};
                const EID peer=w.map.inBounds(next.x,next.y)?w.unitAtCell(next.x,next.y):INVALID_EID;
                std::printf("JUNCTION_QUEUE tick=%llu arrived=%d worst=%d pos=%.2f,%.2f blocked=%d next=%d,%d peer=%d clear_wait=%llu occupied_wait=%llu\n",
                    (unsigned long long)w.tick,arrived,worst,e.x,e.y,e.blockTick,next.x,next.y,peer,(unsigned long long)clearWait,(unsigned long long)occupiedWait);
                const auto checksum=w.checksum();
                for(const auto& gate:w.trafficDebug())std::printf("JUNCTION_GATE tick=%llu anchor=%d,%d exit=%d,%d cells=%d ports=%d groups=%d occupants=%d remaining=%llu draining=%d opposed=%d\n",
                    (unsigned long long)w.tick,gate.anchor%w.map.w,gate.anchor/w.map.w,gate.exit%w.map.w,gate.exit/w.map.w,gate.cells,gate.ports,gate.groups,gate.occupants,(unsigned long long)gate.remaining,gate.draining,gate.opposed);
                equal &= checksum==w.checksum();
                std::fflush(stdout);
            }
            if(frame==200 || frame==800 || (dynamic && frame==1800)) {
                if(loop) {
                    const auto hash=w.checksum();
                    for(const auto& gate:w.trafficDebug())if(gate.ports==12) {
                        groups &= gate.groups==((scenario==4 || (dynamic && obstacleTick>=0 && !removed))?5:4);++groupChecks;
                    }
                    equal &= hash==w.checksum();
                }
                FILE* file=tmpfile();bool saved=file && w.saveGame(file);
                if(saved){rewind(file);saved=replay.loadGame(file);}if(file)fclose(file);
                equal &= saved && w.checksum()==replay.checksum();snapshots+=saved;
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
            if(spawned==waves && arrived==(int)units.size() && snapshots==(dynamic?3:2)){finished=frame+1;break;}
        }
        // Arrival alone hid multi-minute starvation. Keep deterministic tick
        // and search budgets for the two 200-car workloads, including a guard
        // against improving the tree junction by regressing the loop network.
        bool timely=true;
        if(scenario==2)timely=finished>0 && finished<=12800 && maxBlock<=6000 && w.repathRequests<=8000;
        if(scenario==3)timely=finished>0 && finished<=12000 && maxBlock<=6000;
        const bool dynamicOk=!dynamic || (obstacleTick>=0 && removed && dynamicLegal && splitChecks>0 && rejoinedChecks>0);
        const bool ok=admitted && goals && legal && smooth && equal && budget && timely && groups && dynamicOk && (!loop || groupChecks>0) && snapshots==(dynamic?3:2) && finished>=0 && arrived==waves*50;
        failures+=!ok;
        std::printf("JUNCTION %s scenario=%d loop=%d waves=%d spawned=%zu arrived=%d finished=%d admitted=%d goals=%d legal=%d smooth=%d replay=%d budget=%d snapshots=%d max_block=%d repaths=%llu\n",ok?"PASS":"FAIL",scenario,loop,waves,units.size(),arrived,finished,admitted,goals,legal,smooth,equal,budget,snapshots,maxBlock,(unsigned long long)w.repathRequests);
        std::printf("JUNCTION_WAIT scenario=%d timely=%d\n",scenario,timely);
        std::printf("JUNCTION_GROUP scenario=%d legal=%d checks=%d\n",scenario,groups,groupChecks);
        if(dynamic)std::printf("JUNCTION_DYNAMIC scenario=%d legal=%d inserted=%d removed=%d split_checks=%d rejoined_checks=%d obstacle=%d,%d closed_exit=%d\n",scenario,dynamicLegal,obstacleTick,removed,splitChecks,rejoinedChecks,obstacleX,obstacleY,closedExit);
        if(!ok)for(EID id:units){const auto& e=w.ents[id];if(e.state==UState::Idle)continue;
            std::printf("JUNCTION_ROUTE id=%d pos=%.3f,%.3f state=%d blocked=%d nodes=",id,e.x,e.y,(int)e.state,e.blockTick);
            for(int node=e.pathIdx;node<(int)e.path.size();++node)std::printf(" %d,%d",e.path[node].x,e.path[node].y);
            std::printf("\n");
        }
        std::fflush(stdout);
    }
    return failures;
}
