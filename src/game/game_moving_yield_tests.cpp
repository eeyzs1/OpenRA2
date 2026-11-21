#include "game_regression.h"
#include <algorithm>
#include <array>
#include <cstdio>

int GameRegression::movingYieldRegression() {
    int failures=0;
    for(int domain=0;domain<3;++domain)for(int rotated=0;rotated<2;++rotated)for(int offset=0;offset<2;++offset)for(int queued=0;queued<2;++queued)for(int waiting=0;waiting<3;++waiting) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.overlay=Overlay::Rock1;}
        auto point=[&](int x,int y){return rotated?Vec2i{63-y,x}:Vec2i{x,y};};
        auto clear=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){auto p=point(x,y);auto& cell=w.map.at(p.x,p.y);cell=Cell{};cell.terrain=domain==1?Terrain::Water:Terrain::Clear;}
        };
        clear(4,18,7,22);clear(8,20,20,20);clear(21,18,24,22);
        clear(12,16,12,25);clear(10,12,14,15);clear(10,26,14,29);
        w.cratesEnabled=false;w.aiAlliance=true;for(auto& p:w.players){p.money=0;p.defeated=false;p.isAI=true;}
        w.spawnBuilding(0,BldType::PowerPlant,10,18,true);
        const UnitType type=domain==0?UnitType::Grizzly:domain==1?UnitType::Destroyer:UnitType::AmphTransport;
        const auto startA=point(12,20),startB=point(13,20),goalA=point(13,20),goalB=point(waiting==2?22:12,waiting==2?20:21);
        if(waiting==2) {
            // Establish a real westbound gate, then leave a local parking
            // task in the route of an opposing through-traffic retreat.
            const auto from=point(14,20),to=point(7,20);
            EID leader=w.spawnUnit(0,type,from.x+0.5f,from.y+0.5f);
            w.map.findPath(from.x,from.y,to.x,to.y,w.ents[leader].path,20000,domain);
            w.ents[leader].state=UState::Moving;w.update();w.kill(leader,false);
        }
        EID a=w.spawnUnit(0,type,startA.x+0.5f+(rotated?0:offset*0.2f),startA.y+0.5f+(rotated?offset*0.2f:0));
        EID b=w.spawnUnit(rotated?1:0,type,startB.x+0.5f-(rotated?0:offset*0.2f),startB.y+0.5f-(rotated?offset*0.2f:0));
        // Recreate the already-admitted reciprocal paths observed in a natural
        // match, rather than asking admission to choose new occupied endpoints.
        const bool admitted=w.map.findPath(startA.x,startA.y,goalA.x,goalA.y,w.ents[a].path,20000,domain)
            && w.map.findPath(startB.x,startB.y,goalB.x,goalB.y,w.ents[b].path,20000,domain);
        w.ents[a].state=queued?UState::AttackMoving:UState::Moving;w.ents[b].state=UState::Moving;
        if(waiting==1)w.ents[b].path.insert(w.ents[b].path.begin(),30,startA);
        const auto finalA=queued?point(12,14):goalA;
        if(queued)w.ents[a].wps.push_back({finalA.x+0.5f,finalA.y+0.5f});
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool legal=true,smooth=true,equal=true,budget=true,saved=false,yielded=false,orders=true,reachedA=false;
        const float step=1.0f/(float)std::max(1,unitDef(type).speed)+0.0001f;
        int finished=-1;
        for(int tick=0;tick<3000;++tick) {
            Vec2f beforeA{w.ents[a].x,w.ents[a].y},beforeB{w.ents[b].x,w.ents[b].y};
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            reachedA |= distf(w.ents[a].x,w.ents[a].y,goalA.x+0.5f,goalA.y+0.5f)<0.05f;
            smooth &= distf(beforeA.x,beforeA.y,w.ents[a].x,w.ents[a].y)<=step
                && distf(beforeB.x,beforeB.y,w.ents[b].x,w.ents[b].y)<=step;
            legal &= w.ents[a].occCell!=w.ents[b].occCell;
            for(EID id:{a,b}) {
                const auto& unit=w.ents[id];
                legal &= w.passableFor((int)unit.x,(int)unit.y,domain) && !w.bldBlocked((int)unit.x,(int)unit.y)
                    && unit.occCell==(int)unit.y*w.map.w+(int)unit.x;
                if(!unit.path.empty())orders &= unit.path.back()==(id==a?(queued && reachedA?finalA:goalA):goalB);
                if(id==a && queued && !reachedA)orders &= unit.wps.size()==1 && unit.wps.front()==std::pair{finalA.x+0.5f,finalA.y+0.5f};
                for(size_t index=unit.pathIdx;index+1<unit.path.size();++index)yielded |= unit.path[index]==unit.path[index+1];
            }
            if(tick==40 && !saved) {
                FILE* file=tmpfile();equal &= file && w.saveGame(file);
                if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;
            }
            if(distf(w.ents[a].x,w.ents[a].y,finalA.x+0.5f,finalA.y+0.5f)<0.05f
                && distf(w.ents[b].x,w.ents[b].y,goalB.x+0.5f,goalB.y+0.5f)<0.05f){finished=tick;break;}
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        const bool ok=admitted && reachedA && finished>=0 && legal && smooth && equal && budget && orders && saved;
        failures+=!ok;
        std::printf("MOVING_YIELD %s domain=%d rotated=%d offset=%d queued=%d waiting=%d admitted=%d finished=%d legal=%d smooth=%d replay=%d budget=%d orders=%d yielded=%d saved=%d\n",ok?"PASS":"FAIL",domain,rotated,offset,queued,waiting,admitted,finished,legal,smooth,equal,budget,orders,yielded,saved);
    }
    for(int domain=0;domain<3;++domain)for(int rotated=0;rotated<2;++rotated)for(int terrainBlock=0;terrainBlock<2;++terrainBlock) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=domain==1?Terrain::Water:Terrain::Clear;}
        w.cratesEnabled=false;for(auto& player:w.players){player.money=0;player.defeated=false;}
        auto point=[&](int x,int y){return rotated?Vec2i{63-y,x}:Vec2i{x,y};};
        auto position=[&](float x,float y){return rotated?Vec2f{64-y,x}:Vec2f{x,y};};
        const auto type=domain==0?UnitType::Grizzly:domain==1?UnitType::Destroyer:UnitType::AmphTransport;
        EID first=w.spawnUnit(0,type,2.5f,2.5f),second=w.spawnUnit(0,type,3.5f,2.5f);
        const EID a=std::min(first,second),b=std::max(first,second);
        const auto startA=position(21.01f,17.62f),startB=terrainBlock?position(19.5f,16.5f):position(20.5f,17.83f);
        w.ents[a].x=startA.x;w.ents[a].y=startA.y;w.ents[b].x=startB.x;w.ents[b].y=startB.y;w.rebuildUnitOcc();
        const auto goalA=point(20,16),goalB=point(20,15);
        w.ents[a].path={goalA};w.ents[b].path={goalA,goalB};
        w.ents[a].state=w.ents[b].state=UState::Moving;
        if(terrainBlock){auto blocked=point(20,17);w.map.at(blocked.x,blocked.y).terrain=Terrain::Clear;w.map.at(blocked.x,blocked.y).overlay=Overlay::Rock1;}
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool advanced=false,legal=true,smooth=true,equal=true,budget=true,saved=false;int arrived=-1;
        const float speed=1.f/std::max(1,unitDef(type).speed)+0.0001f;
        for(int frame=0;frame<600;++frame) {
            const Vec2f oldA{w.ents[a].x,w.ents[a].y},oldB{w.ents[b].x,w.ents[b].y};
            w.update();replay.update();
            if(frame==0)advanced=distf(oldB.x,oldB.y,w.ents[b].x,w.ents[b].y)>0.001f;
            equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            smooth &= distf(oldA.x,oldA.y,w.ents[a].x,w.ents[a].y)<=speed && distf(oldB.x,oldB.y,w.ents[b].x,w.ents[b].y)<=speed;
            legal &= w.ents[a].occCell!=w.ents[b].occCell;
            for(EID id:{a,b})legal &= w.passableFor((int)w.ents[id].x,(int)w.ents[id].y,domain) && !w.bldBlocked((int)w.ents[id].x,(int)w.ents[id].y);
            if(frame==10){FILE* file=tmpfile();equal &= file && w.saveGame(file);if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;}
            if(distf(w.ents[a].x,w.ents[a].y,goalA.x+0.5f,goalA.y+0.5f)<0.05f && distf(w.ents[b].x,w.ents[b].y,goalB.x+0.5f,goalB.y+0.5f)<0.05f){arrived=frame;break;}
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        const bool ok=advanced && arrived>=0 && legal && smooth && equal && budget && saved;failures+=!ok;
        std::printf("SWEPT_RESERVATION %s domain=%d rotated=%d terrain=%d first_progress=%d arrived=%d legal=%d smooth=%d replay=%d budget=%d saved=%d\n",ok?"PASS":"FAIL",domain,rotated,terrainBlock,advanced,arrived,legal,smooth,equal,budget,saved);
    }
    // Actual archipelago coastline and reciprocal infantry/vehicle paths from
    // the unresolved seed-20261031 match. No AI, free displacement or new orders.
    for(int rotated=0;rotated<2;++rotated)for(int reversed=0;reversed<2;++reversed)for(int partial=0;partial<2;++partial) {
        World w;w.init(64,64,20261031,0,2,{Faction::Yuri,Faction::Allies},3,"");
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        if(rotated) {
            auto source=w.map.cells;
            for(int y=0;y<64;++y)for(int x=0;x<64;++x)w.map.at(63-y,x)=source[y*64+x];
        }
        auto point=[&](int x,int y){return rotated?Vec2i{63-y,x}:Vec2i{x,y};};
        auto position=[&](float x,float y){return rotated?Vec2f{64-y,x}:Vec2f{x,y};};
        w.cratesEnabled=false;w.aiAlliance=rotated;
        for(auto& player:w.players){player.money=0;player.defeated=false;player.isAI=rotated;}
        EID first=w.spawnUnit(0,reversed?UnitType::LasherTank:UnitType::Initiate,46.5f,3.5f);
        EID second=w.spawnUnit(0,reversed?UnitType::Initiate:UnitType::LasherTank,46.5f,2.5f);
        const EID a=reversed?second:first,b=reversed?first:second;
        w.ents[a].player=0;w.ents[b].player=rotated?1:0;
        const auto startA=position(46.5f,3.5f),startB=position(46.5f-partial*0.08f,2.5f);
        w.ents[a].x=startA.x;w.ents[a].y=startA.y;w.ents[b].x=startB.x;w.ents[b].y=startB.y;w.rebuildUnitOcc();
        const auto goalA=point(26,10),goalB=point(26,9);
        bool admitted=true;
        for(EID id:{a,b}) {
            const Vec2i next=id==a?point(46,2):point(46,3),goal=id==a?goalA:goalB;
            std::vector<Vec2i> tail;
            admitted &= w.map.findPath(next.x,next.y,goal.x,goal.y,tail,20000,0) && !tail.empty() && tail.back()==goal;
            w.ents[id].path={next};w.ents[id].path.insert(w.ents[id].path.end(),tail.begin(),tail.end());
            w.ents[id].state=UState::AttackMoving;
        }
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool legal=true,smooth=true,equal=true,budget=true,saved=false;int arrived=-1;
        for(int frame=0;frame<3000;++frame) {
            Vec2f oldA{w.ents[a].x,w.ents[a].y},oldB{w.ents[b].x,w.ents[b].y};w.update();replay.update();
            equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            smooth &= distf(oldA.x,oldA.y,w.ents[a].x,w.ents[a].y)<=1.f/unitDef(UnitType::Initiate).speed+0.0001f
                && distf(oldB.x,oldB.y,w.ents[b].x,w.ents[b].y)<=1.f/unitDef(UnitType::LasherTank).speed+0.0001f;
            legal &= w.ents[a].occCell!=w.ents[b].occCell;
            for(EID id:{a,b})legal &= w.map.passable((int)w.ents[id].x,(int)w.ents[id].y) && !w.bldBlocked((int)w.ents[id].x,(int)w.ents[id].y);
            if(frame==40){FILE* file=tmpfile();equal &= file && w.saveGame(file);if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;}
            if(distf(w.ents[a].x,w.ents[a].y,goalA.x+0.5f,goalA.y+0.5f)<0.05f && distf(w.ents[b].x,w.ents[b].y,goalB.x+0.5f,goalB.y+0.5f)<0.05f){arrived=frame;break;}
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        if(arrived<0 && !rotated && !reversed && !partial) {
            for(EID id:{a,b}) {
                const auto& e=w.ents[id];std::printf("COASTAL_PATH id=%d index=%d size=%zu nodes=",id,e.pathIdx,e.path.size());
                for(int i=e.pathIdx;i<std::min((int)e.path.size(),e.pathIdx+40);++i)std::printf(" %d,%d",e.path[i].x,e.path[i].y);
                std::printf("\n");
            }
            for(int y=0;y<7;++y){std::printf("COASTAL_MAP y=%d ",y);for(int x=42;x<50;++x)std::printf("%c",w.map.passable(x,y)?'.':'#');std::printf("\n");}
        }
        const bool ok=admitted && arrived>=0 && legal && smooth && equal && budget && saved;failures+=!ok;
        std::printf("COASTAL_MIXED_YIELD %s rotated=%d reversed=%d partial=%d admitted=%d arrived=%d legal=%d smooth=%d replay=%d budget=%d saved=%d a=%.2f,%.2f block=%d b=%.2f,%.2f block=%d\n",ok?"PASS":"FAIL",rotated,reversed,partial,admitted,arrived,legal,smooth,equal,budget,saved,w.ents[a].x,w.ents[a].y,w.ents[a].blockTick,w.ents[b].x,w.ents[b].y,w.ents[b].blockTick);
    }
    // Exact remaining routes observed at tick 72000 of archipelago seed
    // 305419896. A third tank's dwell prefix points into the west waiter's cell.
    for(int rotated=0;rotated<2;++rotated)for(int reversed=0;reversed<2;++reversed)for(int partial=0;partial<2;++partial)for(int protection=0;protection<3;++protection) {
        World w;w.init(64,64,305419896,0,2,{Faction::Yuri,Faction::Allies},3,"");
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive && (!w.ents[id].isBuilding || w.ents[id].player>=0))w.kill((EID)id,false);
        if(rotated) {
            const auto cells=w.map.cells;
            for(int y=0;y<64;++y)for(int x=0;x<64;++x)w.map.at(63-x,63-y)=cells[y*64+x];
            std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
            for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive && w.ents[id].isBuilding) {
                auto& e=w.ents[id];const auto& d=bldDef(e.btype);e.x=64-e.x-d.w;e.y=64-e.y-d.h;
                for(int y=0;y<d.h;++y)for(int x=0;x<d.w;++x)w.bldOcc[w.cellIdx((int)e.x+x,(int)e.y+y)]=(EID)id+1;
            }
        }
        auto point=[&](int x,int y){return rotated?Vec2i{63-x,63-y}:Vec2i{x,y};};
        auto position=[&](float x,float y){return rotated?Vec2f{64-x,64-y}:Vec2f{x,y};};
        w.cratesEnabled=false;w.aiAlliance=rotated;
        for(auto& player:w.players){player.money=0;player.defeated=false;player.isAI=rotated;}
        std::vector<EID> ids;
        for(int i=0;i<3;++i)ids.push_back(w.spawnUnit(0,UnitType::LasherTank,37.5f+i,7.5f));
        std::sort(ids.begin(),ids.end());if(reversed)std::reverse(ids.begin(),ids.end());
        const EID a=ids[0],b=ids[1],c=ids[2];
        w.ents[a].x=37.5f;w.ents[b].x=39.5f;w.ents[c].x=38.5f+partial/3.f;
        for(EID id:ids){w.ents[id].y=7.5f;w.ents[id].state=UState::AttackMoving;}
        w.ents[a].path={{38,7},{39,7},{39,8}};
        w.ents[b].path={{38,7},{37,7}};
        w.ents[c].path.assign(31,{37,7});
        for(Vec2i point:{Vec2i{38,7},Vec2i{39,7},Vec2i{40,8}})w.ents[c].path.push_back(point);
        for(EID id:ids) {
            auto& e=w.ents[id];const auto pos=position(e.x,e.y);e.x=pos.x;e.y=pos.y;
            for(auto& node:e.path)node=point(node.x,node.y);
        }
        w.ents[c].player=rotated?1:0;
        const EID protectedUnit=protection==1?c:a;
        if(protection)w.ents[protectedUnit].tpSick=10000;
        const Vec2f protectedPosition{w.ents[protectedUnit].x,w.ents[protectedUnit].y};
        const auto protectedPath=w.ents[protectedUnit].path;
        w.rebuildUnitOcc();World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool legal=true,smooth=true,equal=true,budget=true,saved=false,protectedStable=true,orders=true;int arrived=-1;
        const Vec2i goals[]={point(39,8),point(37,7),point(40,8)};
        for(int frame=0;frame<(protection?300:3000);++frame) {
            Vec2f old[3];for(int i=0;i<3;++i)old[i]={w.ents[ids[i]].x,w.ents[ids[i]].y};
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            bool reached=true;
            for(int i=0;i<3;++i){const auto& e=w.ents[ids[i]];
                smooth &= distf(old[i].x,old[i].y,e.x,e.y)<=1.f/unitDef(e.utype).speed+0.0001f;
                legal &= w.passableFor((int)e.x,(int)e.y,0) && !w.bldBlocked((int)e.x,(int)e.y) && e.occCell==(int)e.y*w.map.w+(int)e.x;
                reached &= distf(e.x,e.y,goals[i].x+0.5f,goals[i].y+0.5f)<0.05f;
                if(!protection && !e.path.empty())orders &= e.path.back()==goals[i];
                for(int j=0;j<i;++j)legal &= e.occCell!=w.ents[ids[j]].occCell;
            }
            if(protection){const auto& e=w.ents[protectedUnit];protectedStable &= e.x==protectedPosition.x && e.y==protectedPosition.y && e.path==protectedPath;}
            if(frame==40){FILE* file=tmpfile();equal &= file && w.saveGame(file);if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;}
            if(reached){arrived=frame;break;}
            w.presentationEvents.clear();replay.presentationEvents.clear();w.evaQueue.clear();replay.evaQueue.clear();
        }
        const bool ok=(protection?protectedStable:arrived>=0) && legal && smooth && equal && budget && saved && orders;failures+=!ok;
        std::printf("COASTAL_THREE_YIELD %s rotated=%d reversed=%d partial=%d protection=%d protected=%d orders=%d arrived=%d legal=%d smooth=%d replay=%d budget=%d saved=%d a=%.2f,%.2f block=%d b=%.2f,%.2f block=%d c=%.2f,%.2f block=%d\n",ok?"PASS":"FAIL",rotated,reversed,partial,protection,protectedStable,orders,arrived,legal,smooth,equal,budget,saved,w.ents[a].x,w.ents[a].y,w.ents[a].blockTick,w.ents[b].x,w.ents[b].y,w.ents[b].blockTick,w.ents[c].x,w.ents[c].y,w.ents[c].blockTick);
    }
    // Snow Valley at tick 180000 has three infantry on either side of the
    // bend at (155,140). Recreate the admitted retreat and the full dead-end
    // stack, with a room at the other port and no later orders or combat.
    for(bool masked:{false,true}) {
        World w;
        if(!w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true))return failures+1;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.overlay=Overlay::Rock1;}
        const auto clear=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)w.map.at(x,y)=Cell{};
        };
        const Vec2i laneCell{12,20},deadEnd{13,20},firstOpenStep{12,21};
        clear(12,20,13,20);clear(12,21,12,24);clear(9,25,15,29);
        clear(40,40,53,47);
        w.cratesEnabled=false;
        for(auto& player:w.players){player.money=0;player.defeated=false;player.isAI=false;}
        w.spawnBuilding(0,BldType::PowerPlant,41,41,true);
        w.spawnBuilding(1,BldType::TeslaReactor,50,41,true);
        std::array<EID,6> ids{};
        for(int n=0;n<(int)ids.size();++n) {
            ids[n]=w.spawnUnit(0,UnitType::GI,9.5f+n,29.5f);
            if(!w.valid(ids[n]))return failures+1;
        }
        std::sort(ids.begin(),ids.end());
        const EID requester=ids[1],dwelling=ids[masked?3:4],reciprocal=ids[masked?4:3];
        for(int n=0;n<(int)ids.size();++n) {
            auto& actor=w.ents[ids[n]];
            const auto cell=n<3?laneCell:deadEnd;
            actor.x=actor.px=cell.x+(ids[n]==reciprocal?.40234375f:.5f);
            actor.y=actor.py=cell.y+.5f;
        }
        w.orderGuard({ids[0],ids[2],ids[5]});
        w.rebuildUnitOcc();
        const Vec2i requestGoal{10,27},reciprocalGoal{12,28},dwellGoal{14,27};
        auto& approach=w.ents[requester];
        auto& peer=w.ents[reciprocal];
        auto& head=w.ents[dwelling];
        std::vector<Vec2i> retreatTail,alternate;
        const bool routes=w.map.findPath(deadEnd.x,deadEnd.y,requestGoal.x,requestGoal.y,retreatTail)
            && !retreatTail.empty() && retreatTail.front()==laneCell && retreatTail.back()==requestGoal
            && w.map.findPath(deadEnd.x,deadEnd.y,reciprocalGoal.x,reciprocalGoal.y,peer.path)
            && !peer.path.empty() && peer.path.front()==laneCell && peer.path.back()==reciprocalGoal
            && w.map.findPath(deadEnd.x,deadEnd.y,dwellGoal.x,dwellGoal.y,head.path)
            && !head.path.empty() && head.path.front()==laneCell && head.path.back()==dwellGoal
            && w.map.findPath(laneCell.x,laneCell.y,requestGoal.x,requestGoal.y,alternate)
            && !alternate.empty() && alternate.front()==firstOpenStep && alternate.back()==requestGoal;
        if(!routes)return failures+1;
        approach.path.assign(30,deadEnd);
        approach.path.insert(approach.path.end(),retreatTail.begin(),retreatTail.end());
        head.path.insert(head.path.begin(),30,deadEnd);
        for(EID id:{requester,reciprocal,dwelling})w.ents[id].state=UState::AttackMoving;
        approach.blockTick=30;peer.blockTick=head.blockTick=12;
        const auto atHead=w.unitAtCell(deadEnd.x,deadEnd.y,0);
        const bool full=w.countInfantryAtCell(deadEnd.x,deadEnd.y,INVALID_EID,0)==3
            && w.countInfantryAtCell(laneCell.x,laneCell.y,INVALID_EID,0)==3
            && w.cellHardBlockedForMove(deadEnd.x,deadEnd.y,requester,0)
            && w.cellHardBlockedForMove(laneCell.x,laneCell.y,reciprocal,0);
        const bool staged=routes && full && atHead==(masked?dwelling:reciprocal)
            && head.path.front()==deadEnd && peer.path.front()==laneCell;
        World replay;
        FILE* file=tmpfile();bool saved=file && w.saveGame(file);
        if(file){std::rewind(file);saved=saved && replay.loadGame(file);std::fclose(file);}
        bool exact=saved && w.checksum()==replay.checksum()
            && replay.unitAtCell(deadEnd.x,deadEnd.y,0)==atHead;
        bool legal=true,continuous=true,goals=true,budget=true;
        int rerouted=-1,finished=-1;
        const float speed=1.f/std::max(1,unitDef(UnitType::GI).speed)+.0001f;
        for(int frame=1;frame<=500;++frame) {
            std::array<Vec2f,6> before{};
            for(size_t n=0;n<ids.size();++n)before[n]={w.ents[ids[n]].x,w.ents[ids[n]].y};
            w.update();
            if(exact){replay.update();exact=w.checksum()==replay.checksum();}
            budget &= w.repathsThisTick<=8;
            if(rerouted<0 && !approach.path.empty() && approach.path.front()==firstOpenStep)rerouted=frame;
            for(size_t n=0;n<ids.size();++n) {
                const auto& actor=w.ents[ids[n]];
                const int x=(int)actor.x,y=(int)actor.y;
                legal &= w.valid(ids[n]) && actor.navLayer==0 && actor.occCell==w.cellIdx(x,y)
                    && w.passableFor(x,y,0,actor.navLayer) && !w.bldBlocked(x,y,actor.navLayer)
                    && w.countInfantryAtCell(x,y,INVALID_EID,actor.navLayer)<=3;
                continuous &= distf(before[n].x,before[n].y,actor.x,actor.y)<=speed;
            }
            const auto staysOnOrder=[](const World::Ent& actor,Vec2i goal) {
                return actor.path.empty() || actor.path.back()==goal;
            };
            goals &= staysOnOrder(approach,requestGoal) && staysOnOrder(peer,reciprocalGoal)
                && staysOnOrder(head,dwellGoal);
            const auto reached=[](const World::Ent& actor,Vec2i goal) {
                return distf(actor.x,actor.y,goal.x+.5f,goal.y+.5f)<.05f;
            };
            if(reached(approach,requestGoal) && reached(peer,reciprocalGoal) && reached(head,dwellGoal)) {
                finished=frame;
                break;
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();
            w.evaQueue.clear();replay.evaQueue.clear();
        }
        // The existing eight-tick reconsideration must run before the
        // separate ordinary 45-tick blockage threshold can choose a route.
        const bool relieved=rerouted>0 && rerouted<=8 && finished>0;
        const bool ok=staged && relieved && legal && continuous && goals && budget && exact;
        failures+=!ok;
        std::printf("STACKED_INFANTRY_YIELD %s masked=%d full=%d head=%d reciprocal=%d routes=%d "
            "rerouted=%d finished=%d legal=%d continuous=%d goals=%d budget=%d saved=%d replay=%d\n",
            ok?"PASS":"FAIL",masked,full,atHead,reciprocal,routes,rerouted,finished,legal,continuous,goals,budget,saved,exact);
    }
    for(int control=0;control<4;++control) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};if(control!=3)cell.overlay=Overlay::Rock1;}
        if(control!=3) {
            for(int x=4;x<25;++x)w.map.at(x,20)=Cell{};
            for(int y=12;y<30;++y)w.map.at(12,y)=Cell{};
            for(int y=12;y<16;++y)for(int x=10;x<15;++x)w.map.at(x,y)=Cell{};
        }
        w.cratesEnabled=false;w.aiAlliance=false;
        for(auto& p:w.players){p.money=0;p.defeated=false;}
        const auto type=control==3?UnitType::Grizzly:UnitType::MCV;
        EID a=w.spawnUnit(0,type,12.5f,20.5f),b=w.spawnUnit(control==0?1:control==1?-1:0,type,13.5f,20.5f);
        w.map.findPath(12,20,control==3?17:13,20,w.ents[a].path);w.ents[a].state=UState::Moving;
        if(control==3)w.orderGuard({b});
        else if(control==2){w.map.findPath(13,20,12,21,w.ents[b].path);w.ents[b].state=UState::Moving;}
        if(control==2)w.ents[b].tpSick=1000;
        const auto path=w.ents[b].path;
        bool protectedUnit=true;
        for(int tick=0;tick<300;++tick) {
            w.update();
            const bool stationary=w.ents[b].x==13.5f && w.ents[b].y==20.5f && w.ents[b].path==path;
            if(protectedUnit && !stationary)std::printf("MOVING_YIELD_CONTROL_DIAG scenario=%d tick=%d pos=%.2f,%.2f state=%d guard=%d path=%zu/%zu\n",control,tick,w.ents[b].x,w.ents[b].y,(int)w.ents[b].state,w.ents[b].guard,w.ents[b].path.size(),path.size());
            protectedUnit &= stationary;
        }
        const bool ok=protectedUnit && (control!=3 || (w.ents[b].guard && w.ents[a].state==UState::Idle && (int)w.ents[a].x==17));
        failures+=!ok;std::printf("MOVING_YIELD_CONTROL %s scenario=%d protected=%d\n",ok?"PASS":"FAIL",control,protectedUnit);
    }
    return failures;
}
