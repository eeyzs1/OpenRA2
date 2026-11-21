#include "game_regression.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>

namespace {
bool initFormationWorld(World& w) {
    if(!w.init(64,64,73,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,
        "assets/rules/profiles/ra2.ini"))return false;
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    w.cratesEnabled=false;
    return true;
}
}

int GameRegression::groupOrderRegression() {
    int failures=0;
    for(const Vec2i forward: {Vec2i{1,0},Vec2i{0,1},Vec2i{-1,0},Vec2i{0,-1},
                             Vec2i{1,1},Vec2i{-1,1},Vec2i{-1,-1},Vec2i{1,-1}}) {
        World w;
        if(!initFormationWorld(w))return failures+1;
        const Vec2i right{-forward.y,forward.x};
        constexpr float center=25.5f,advance=18;
        const float axisLength=std::hypot((float)forward.x,(float)forward.y);
        const float spacing=1.5f/axisLength;
        std::vector<EID> group;
        std::vector<Vec2f> starts;
        for(int depth=-1;depth<=1;++depth)for(int side=1;side>=-1;--side) {
            const float x=std::floor(center+spacing*(forward.x*depth+right.x*side))+.5f;
            const float y=std::floor(center+spacing*(forward.y*depth+right.y*side))+.5f;
            group.push_back(w.spawnUnit(0,UnitType::Grizzly,x,y));
            starts.push_back({x,y});
        }
        if(std::any_of(group.begin(),group.end(),[&](EID id){return !w.valid(id);}))return failures+1;
        World reversed=w;reversed.map.bldOccRef=&reversed.bldOcc;
        World::Cmd order;
        order.type=World::Cmd::Move;order.ids=group;
        order.x=center+forward.x*advance;order.y=center+forward.y*advance;
        order.attackMove=forward.x<0 || forward.y<0;order.b=1;
        w.bindCommand(order);w.applyCmd(0,order);
        const auto result=w.presentationEvents.back().move;
        std::reverse(order.ids.begin(),order.ids.end());
        reversed.bindCommand(order);reversed.applyCmd(0,order);
        const bool sameAssignment=w.checksum()==reversed.checksum();
        bool translated=true,arrived=false,exact=sameAssignment,restored=false;
        float crossLane=0;
        int detourTick=0;
        for(size_t i=0;i<group.size();++i) {
            const auto& actor=w.ents[group[i]];
            translated &= actor.goalX==starts[i].x+forward.x*advance && actor.goalY==starts[i].y+forward.y*advance;
        }
        for(int tick=0;tick<1800 && !arrived;++tick) {
            if(tick==120 && exact) {
                FILE* save=std::tmpfile();
                restored=save && w.saveGame(save);
                if(restored){std::rewind(save);restored=reversed.loadGame(save) && w.checksum()==reversed.checksum();}
                if(save)std::fclose(save);
                exact &= restored;
            }
            w.update();arrived=true;
            if(exact){reversed.update();exact=w.checksum()==reversed.checksum();}
            for(size_t i=0;i<group.size();++i) {
                const auto& actor=w.ents[group[i]];
                if(!detourTick && actor.pathIdx<(int)actor.path.size()) {
                    const auto waypoint=actor.path[actor.pathIdx];
                    if(std::abs((waypoint.x+.5f-starts[i].x)*right.x+(waypoint.y+.5f-starts[i].y)*right.y)>.01f)
                        detourTick=tick+1;
                }
                const float lateral=(actor.x-starts[i].x)*right.x+(actor.y-starts[i].y)*right.y;
                crossLane=std::max(crossLane,std::abs(lateral)/axisLength);
                arrived &= actor.alive && actor.state==UState::Idle
                    && distf(actor.x,actor.y,starts[i].x+forward.x*advance,starts[i].y+forward.y*advance)<.01f;
            }
        }
        const bool ok=result.accepted==(int)group.size() && !result.rejected && translated && exact && restored && arrived && crossLane<.01f;
        std::printf("GROUP_FORMATION %s forward=(%d,%d) attack=%d admitted=%d translated=%d same_assignment=%d restored=%d exact=%d arrived=%d lateral_cells=%.3f first_detour=%d\n",
            ok?"PASS":"FAIL",forward.x,forward.y,order.attackMove,result.accepted,translated,sameAssignment,restored,exact,arrived,crossLane,detourTick);
        failures+=!ok;
    }
    {
        World w;
        if(!initFormationWorld(w))return failures+1;
        const EID west=w.spawnUnit(0,UnitType::Grizzly,20.5f,25.5f);
        const EID east=w.spawnUnit(0,UnitType::Grizzly,30.5f,25.5f);
        if(!w.valid(west) || !w.valid(east))return failures+1;
        World::Cmd order;
        order.type=World::Cmd::Move;order.ids={west,east};order.x=order.y=25.5f;w.applyCmd(0,order);
        const bool ok=w.presentationEvents.back().move.accepted==2 && w.ents[west].state==UState::Moving
            && w.ents[east].state==UState::Moving && w.ents[west].goalX==24.5f && w.ents[east].goalX==26.5f;
        std::printf("GROUP_FORMATION_REGROUP %s: a dispersed group moves to the requested center\n",ok?"PASS":"FAIL");
        failures+=!ok;
    }
    {
        World w;
        if(!initFormationWorld(w))return failures+1;
        const EID mover=w.spawnUnit(0,UnitType::Grizzly,8.5f,8.5f);
        const EID guard=w.spawnUnit(0,UnitType::Grizzly,12.5f,8.5f);
        if(!w.valid(mover) || !w.valid(guard))return failures+1;
        World::Cmd order;
        order.type=World::Cmd::Guard;order.ids={guard};w.applyCmd(0,order);
        order.type=World::Cmd::Move;order.ids={mover};order.x=20.5f;order.y=8.5f;w.applyCmd(0,order);
        bool detoured=false,arrived=false;
        for(int tick=0;tick<1800 && !arrived;++tick) {
            w.update();
            const auto& unit=w.ents[mover];
            detoured |= std::abs(unit.y-8.5f)>.25f;
            arrived=unit.state==UState::Idle && distf(unit.x,unit.y,20.5f,8.5f)<.01f;
        }
        const bool ok=arrived && detoured && w.ents[guard].guard && w.ents[guard].x==12.5f && w.ents[guard].y==8.5f;
        std::printf("GROUP_FORMATION_BLOCKER %s: a parked guard stays in place and the moving tank routes around it\n",ok?"PASS":"FAIL");
        failures+=!ok;
    }
    {
        World w;w.init(48,48,73,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.blocked=true;}
        for(auto cell:{Vec2i{5,5},Vec2i{6,5},Vec2i{5,6},Vec2i{6,6}})w.map.at(cell.x,cell.y).blocked=false;
        w.map.at(5,5).height=1;w.map.at(6,5).height=w.map.at(5,6).height=2;w.map.at(6,6).height=0;
        const EID unit=w.spawnUnit(0,UnitType::Grizzly,5.5f,5.5f);
        const auto result=w.orderMove({unit},6.5f,6.5f,false);
        const bool legal=result.accepted==1 && (w.ents[unit].goalX!=6.5f || w.ents[unit].goalY!=6.5f)
            && !w.ents[unit].path.empty() && w.ents[unit].path.back()!=Vec2i{6,6};
        std::printf("GROUP_ORDER %s: both sides of a diagonal cliff corner must connect to the destination height\n",legal?"PASS":"FAIL");
        failures+=!legal;
    }
    int compared=0;bool shortest=true,legal=true;
    const auto cost=[](Vec2i start,const std::vector<Vec2i>& path) {
        int64_t total=0;for(const auto& cell:path){total+=cell.x!=start.x && cell.y!=start.y?14142:10000;start=cell;}return total;
    };
    for(int domain=0;domain<3;++domain)for(int scenario=0;scenario<32;++scenario) {
        World w;w.init(48,48,73+scenario,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        Rng random(0x70617468ull+scenario*17+domain);
        for(auto& cell:w.map.cells) {
            cell=Cell{};cell.terrain=domain==1?Terrain::Water:Terrain::Clear;
            if(scenario%4 && random.next()%8==0)cell.terrain=domain==1?Terrain::Clear:Terrain::Water;
            if(scenario%4==2)cell.blocked=random.next()%12==0;
            if(scenario%4==3)cell.height=(uint8_t)(random.next()%3);
        }
        Vec2i start{3+scenario%7,3+scenario%5},target{42-scenario%5,42-scenario%7};
        for(auto point:{start,target}){auto& cell=w.map.at(point.x,point.y);cell=Cell{};cell.terrain=domain==1?Terrain::Water:Terrain::Clear;}
        const UnitType type=domain==0?UnitType::Grizzly:domain==1?UnitType::Typhoon:UnitType::AmphTransport;
        const EID actor=w.spawnUnit(0,type,start.x+.5f,start.y+.5f);
        const auto result=w.orderMove({actor},target.x+.5f,target.y+.5f,false);
        if(!result.accepted)continue;
        const auto& path=w.ents[actor].path;const Vec2i endpoint=path.empty()?start:path.back();std::vector<Vec2i> reference;
        shortest &= w.map.findPath(start.x,start.y,endpoint.x,endpoint.y,reference,20000,domain)
            && cost(start,path)==cost(start,reference);
        Vec2i previous=start;for(auto point:path) {
            legal &= w.map.stepLayer(previous.x,previous.y,0,point.x,point.y,domain)==0;previous=point;
        }
        ++compared;
    }
    std::printf("GROUP_ORDER %s: %d varied ground/water/amphibious orders match independent A* costs and legal corner/height transitions\n",
        shortest && legal && compared>=90?"PASS":"FAIL",compared);
    failures+=!(shortest && legal && compared>=90);
    for(int domain=0;domain<2;++domain)for(int count:{200,500}) {
        World w;w.init(256,256,20261113,2,0,{Faction::Soviet,Faction::Soviet});
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=domain?Terrain::Water:Terrain::Clear;}
        for(int y=0;y<256;++y)w.map.at(128,y).terrain=domain?Terrain::Clear:Terrain::Water;
        w.cratesEnabled=false;
        std::vector<EID> group;
        for(int index=0;index<count;++index)group.push_back(w.spawnUnit(0,domain?UnitType::Typhoon:UnitType::Grizzly,
            4.5f+index%20,4.5f+index/20));
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        World::Cmd command;command.type=World::Cmd::Move;command.ids=group;command.x=command.y=230;
        const auto start=std::chrono::steady_clock::now();
        w.applyCmd(0,command);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        replay.applyCmd(0,command);
        const auto result=w.presentationEvents.back().move;
        std::set<int> destinations;bool legal=true;
        for(EID id:group) {
            const auto& unit=w.ents[id];
            if(unit.path.empty()){legal=false;continue;}
            const auto end=unit.path.back();
            legal &= end.x<128 && w.passableFor(end.x,end.y,domain) && destinations.insert(w.cellIdx(end.x,end.y)).second;
        }
        bool equal=w.checksum()==replay.checksum();
        for(int tick=0;tick<90;++tick) {
            w.update();replay.update();equal &= w.checksum()==replay.checksum();
            w.presentationEvents.clear();replay.presentationEvents.clear();
        }
#ifdef RA2_ASAN_ACTIVE
        const bool timely=true;
#else
        const bool timely=ms<100.0;
#endif
        const bool ok=result.accepted==count && result.adjusted==count && !result.rejected && legal && equal && timely;
        std::printf("GROUP_ORDER %s domain=%d count=%d map=256 command_ms=%.3f unique=%zu legal=%d replay=%d timely=%d\n",
            ok?"PASS":"FAIL",domain,count,ms,destinations.size(),legal,equal,timely);
        failures+=!ok;
    }
    return failures;
}
