#include "game/world.h"
#include <algorithm>
#include <tuple>

std::vector<World::TrafficDebug> World::trafficDebug() const {
    std::vector<TrafficDebug> result;
    for(const auto& gate:trafficGates) {
        TrafficDebug row;row.domain=gate.domain;row.anchor=gate.anchor;row.exit=gate.exit;
        row.remaining=gate.until>tick?gate.until-tick:0;row.draining=gate.draining;
        const auto& labels=movementCorridorLabels[gate.domain];
        if(gate.anchor>=0 && gate.anchor<(int)labels.size()) {
            const int region=labels[gate.anchor];
            if(region>=0 && region<(int)movementCorridors[gate.domain].size()) {
                const auto& corridor=movementCorridors[gate.domain][region];
                row.cells=(int)corridor.cells.size();row.ports=(int)corridor.ports.size();row.opposed=corridor.opposed;
                auto groups=corridor.portGroups;std::sort(groups.begin(),groups.end());
                row.groups=(int)(std::unique(groups.begin(),groups.end())-groups.begin());
                for(int cell:corridor.cells)for(EID id=unitOccHead[cell];id!=INVALID_EID;id=ents[id].occNext)
                    if(ents[id].alive && !ents[id].isBuilding && !unitDef(ents[id].utype).isAir()
                        && unitDef(ents[id].utype).pathDomain()==gate.domain)++row.occupants;
            }
        }
        result.push_back(row);
    }
    return result;
}

World::IdleYieldResult World::yieldIdleCorridor(EID requester,EID blocker) {
    if(!valid(requester) || !valid(blocker) || !isAllied(ents[requester].player,ents[blocker].player))
        return IdleYieldResult::NotApplicable;
    auto idle=[&](const Ent& unit) {
        const auto& definition=unitDef(unit.utype);
        return unit.alive && !unit.isBuilding && !definition.isAir() && !unit.parasiting
            && unit.state==UState::Idle && !unit.deployed && !unit.radDeployed && !unit.guard
            && unit.pathIdx>=(int)unit.path.size() && unit.wps.empty() && !valid(unit.target)
            && unit.engineerIntent==EngineerIntent::None && !(definition.canHarvet() && unit.autoHarvest)
            && !unit.immobilized()
            && !(valid(unit.parasite) && ents[unit.parasite].utype==UnitType::Squid);
    };
    auto moving=[&](const Ent& unit) {
        return unit.pathIdx<(int)unit.path.size() && !unit.deployed && !unit.radDeployed && !unit.parasiting
            && !unit.guard && !unit.immobilized()
            && !(valid(unit.parasite) && ents[unit.parasite].utype==UnitType::Squid)
            && (unit.state==UState::Moving || unit.state==UState::AttackMoving || unit.state==UState::Chasing
            || unit.state==UState::HarvestGo || unit.state==UState::HarvestReturn || unit.state==UState::Boarding
            || unit.state==UState::Servicing);
    };
    auto& parked=ents[blocker];const auto& lead=ents[requester];
    const int domain=unitDef(parked.utype).pathDomain(),start=parked.occCell;
    if(!idle(parked) || start<0)return IdleYieldResult::NotApplicable;
    if(repathsThisTick>=8) return IdleYieldResult::BudgetWait;
    std::vector<uint8_t> costs(map.cells.size(),0),landing(map.cells.size(),1),avoid(map.cells.size(),0);
    for(int step=lead.pathIdx;step<(int)lead.path.size();++step) {
        const auto point=lead.path[step];avoid[(size_t)point.y*map.w+point.x]=1;
    }
    for(size_t index=0;index<ents.size();++index) {
        if((EID)index==blocker) continue;
        const auto& other=ents[index];
        const bool allied=isAllied(parked.player,other.player);
        const bool active=other.alive && !other.isBuilding && moving(other);
        if(active && allied && !other.path.empty()) {
            const auto goal=other.path.back();landing[(size_t)goal.y*map.w+goal.x]=0;
        }
        const int cell=other.occCell;
        if(cell<0 || cell>=(int)costs.size()) continue;
        const bool cooperative=allied && (idle(other) || active);
        costs[cell]=std::max(costs[cell],uint8_t{16});
        if((EID)index==requester || (!cooperative && cellHardBlockedForMove(cell%map.w,cell/map.w,blocker)))
            costs[cell]=255;
        const bool vacating=active && allied && other.path.back()!=Vec2i{cell%map.w,cell/map.w};
        if(!vacating || (EID)index==requester) landing[cell]=0;
    }
    // Open parking fields need the same continuous, reserved-goal-aware
    // yielding as narrow lanes; a one-cell teleport can create swap cycles.
    // Find the nearest passing bay without teleporting the parked unit. Idle
    // allies on the route may be asked to yield in turn, forming a legal chain.
    std::vector<int> search{start};std::vector<uint8_t> seen(map.cells.size(),0);seen[start]=1;
    int goal=-1;
    for(size_t at=0;at<search.size() && at<4096;++at) {
        const int cell=search[at],x=cell%map.w,y=cell/map.w;
        if(landing[cell] && !avoid[cell] && !narrowMovementCell(x,y,domain)) { goal=cell;break; }
        for(Vec2i delta:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const int nx=x+delta.x,ny=y+delta.y;
            if(!passableStep(x,y,nx,ny,domain) || bldBlocked(nx,ny)) continue;
            const int next=ny*map.w+nx;
            if(!seen[next] && costs[next]!=255) { seen[next]=1;search.push_back(next); }
        }
    }
    if(goal<0) return IdleYieldResult::NoPullout;
    std::vector<Vec2i> path;++repathsThisTick;++repathRequests;
    if(!map.findPath((int)parked.x,(int)parked.y,goal%map.w,goal/map.w,path,20000,domain,&costs)
        || path.empty() || path.size()>4096 || path.back()!=Vec2i{goal%map.w,goal/map.w}) {
        ++repathFailures;return IdleYieldResult::NoPullout;
    }
    parked.path=std::move(path);parked.pathIdx=0;parked.blockTick=0;parked.state=UState::Moving;
    return IdleYieldResult::Started;
}

void World::resetMovementCorridors() {
    for(int domain=0;domain<3;++domain) { movementCorridorLabels[domain].clear();movementCorridors[domain].clear(); }
}

bool World::narrowMovementCell(int x,int y,int domain) const {
    auto open=[&](int nx,int ny) { return passableFor(nx,ny,domain) && !bldBlocked(nx,ny); };
    if(!open(x,y)) return false;
    int neighbors=0;
    for(Vec2i step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}})
        neighbors+=open(x+step.x,y+step.y) && passableStep(x,y,x+step.x,y+step.y,domain);
    if(neighbors<2) return false;
    // A traversable 2x2 patch is a passing bay, not part of a single-file lane.
    // This keeps bends and narrow junctions together while separating open rooms.
    for(int dx:{-1,1}) for(int dy:{-1,1})
        if(open(x+dx,y) && open(x,y+dy) && open(x+dx,y+dy)
            && passableStep(x,y,x+dx,y,domain) && passableStep(x,y,x,y+dy,domain)
            && passableStep(x+dx,y,x+dx,y+dy,domain) && passableStep(x,y+dy,x+dx,y+dy,domain)) return false;
    return true;
}

int World::movementCorridor(int cell,int domain) {
    auto& labels=movementCorridorLabels[domain];auto& corridors=movementCorridors[domain];
    if(labels.size()!=map.cells.size()) labels.assign(map.cells.size(),-2);
    if(cell<0 || cell>=(int)labels.size()) return -1;
    if(labels[cell]!=-2) return labels[cell];
    if(!narrowMovementCell(cell%map.w,cell/map.w,domain)) return labels[cell]=-1;
    const int index=(int)corridors.size();corridors.push_back({});
    auto& corridor=corridors.back();corridor.cells.push_back(cell);labels[cell]=index;
    for(size_t current=0;current<corridor.cells.size();++current) {
        const int at=corridor.cells[current],x=at%map.w,y=at/map.w;
        for(Vec2i delta:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const int nx=x+delta.x,ny=y+delta.y;
            if(!map.inBounds(nx,ny) || bldBlocked(nx,ny) || !passableStep(x,y,nx,ny,domain)) continue;
            const int next=ny*map.w+nx;
            // -1 may be an open tile queried before its attached lane. It
            // still needs compact-junction classification; -3 is a patch
            // already proven to remain outside admission in this frame.
            if(labels[next]==-2 || labels[next]==-1) {
                if(narrowMovementCell(nx,ny,domain)) { labels[next]=index;corridor.cells.push_back(next); }
                else {
                    // A compact open junction between three or more lane
                    // mouths belongs to their shared admission region. If its
                    // arms are scheduled independently, incoming columns can
                    // fill the box while each blocks the other's outgoing arm.
                    std::vector<int> patch{next},mouths;
                    std::vector<uint8_t> seen(map.cells.size(),0);seen[next]=1;
                    for(size_t head=0;head<patch.size() && patch.size()<=64;++head) {
                        const int node=patch[head],px=node%map.w,py=node/map.w;
                        for(Vec2i step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
                            const int nx=px+step.x,ny=py+step.y;
                            if(!passableFor(nx,ny,domain) || bldBlocked(nx,ny) || !passableStep(px,py,nx,ny,domain))continue;
                            const int neighbor=cellIdx(nx,ny);
                            if(narrowMovementCell(nx,ny,domain)){mouths.push_back(neighbor);continue;}
                            if(!seen[neighbor]){seen[neighbor]=1;patch.push_back(neighbor);}
                        }
                    }
                    std::sort(mouths.begin(),mouths.end());mouths.erase(std::unique(mouths.begin(),mouths.end()),mouths.end());
                    if(patch.size()<=64 && mouths.size()>=3
                        && std::all_of(patch.begin(),patch.end(),[&](int node){return labels[node]<0 || labels[node]==index;})) {
                        for(int node:patch)if(labels[node]!=index){labels[node]=index;corridor.cells.push_back(node);}
                    } else {
                        for(int node:patch)if(labels[node]<0)labels[node]=-3;
                    }
                }
            }
            if(labels[next]!=index) corridor.ports.push_back(next);
        }
    }
    std::erase_if(corridor.ports,[&](int port){return labels[port]==index;});
    std::sort(corridor.ports.begin(),corridor.ports.end());
    corridor.ports.erase(std::unique(corridor.ports.begin(),corridor.ports.end()),corridor.ports.end());
    corridor.portGroups=corridor.ports;
    // Parallel mouths may discharge into the same passing apron. Co-admit
    // only equally facing, collinear mouths joined by an unobstructed strip
    // of non-narrow cells; facing ends and broken aprons stay independent.
    auto facing=[&](int port) {
        int face=-1;
        constexpr Vec2i directions[]={{1,0},{-1,0},{0,1},{0,-1}};
        for(int i=0;i<4;++i) {
            const int x=port%map.w+directions[i].x,y=port/map.w+directions[i].y;
            if(map.inBounds(x,y) && labels[y*map.w+x]==index && passableStep(x,y,port%map.w,port/map.w,domain)) {
                if(face>=0)return -1; // Ambiguous corner mouths retain separate admission.
                face=i;
            }
        }
        return face;
    };
    for(size_t i=0;i<corridor.ports.size();++i)for(size_t j=0;j<i;++j) {
        const int a=corridor.ports[i],b=corridor.ports[j],face=facing(a);
        if(face<0 || face!=facing(b) || (face<2?a%map.w!=b%map.w:a/map.w!=b/map.w))continue;
        const int step=face<2?map.w:1;bool clear=true;
        for(int cell=std::min(a,b);cell<=std::max(a,b);cell+=step) {
            const int x=cell%map.w,y=cell/map.w;
            if(labels[cell]==index || bldBlocked(x,y) || !passableFor(x,y,domain) || narrowMovementCell(x,y,domain)
                || (cell>std::min(a,b) && !passableStep((cell-step)%map.w,(cell-step)/map.w,x,y,domain))){clear=false;break;}
        }
        if(clear){corridor.portGroups[i]=corridor.portGroups[j];break;}
    }
    return index;
}

bool World::admitMovementCorridor(Ent& unit,EID id) {
    if(unitDef(unit.utype).isAir() || unit.pathIdx>=(int)unit.path.size()) return true;
    const int domain=unitDef(unit.utype).pathDomain();
    // Cooperative pullouts carry dwell waypoints before resuming the route.
    // Let that prefix finish even when it briefly points toward the portal;
    // otherwise admission can freeze a car midway through yielding.
    for(int step=unit.pathIdx;step+1<(int)unit.path.size();++step) {
        const auto point=unit.path[step],following=unit.path[step+1];
        if(narrowMovementCell(point.x,point.y,domain)) break;
        if(point.x==following.x && point.y==following.y) return true;
    }
    const int current=(int)unit.y*map.w+(int)unit.x;
    const auto next=unit.path[unit.pathIdx];
    int lane=map.inBounds(next.x,next.y)?movementCorridor(next.y*map.w+next.x,domain):-1;
    if(lane<0 && !narrowMovementCell((int)unit.x,(int)unit.y,domain)) {
        // Keep a clear apron on both banks so the exiting column can fan out.
        const int limit=std::min((int)unit.path.size(),unit.pathIdx+6);
        for(int step=unit.pathIdx+1;step<limit && lane<0;++step) {
            const auto point=unit.path[step];
            if(map.inBounds(point.x,point.y)) lane=movementCorridor(point.y*map.w+point.x,domain);
        }
    }
    if(lane<0) return true;
    auto& labels=movementCorridorLabels[domain];auto& corridor=movementCorridors[domain][lane];
    auto inside=[&](int cell) { return cell>=0 && cell<(int)labels.size() && labels[cell]==lane; };
    auto sameExit=[&](int a,int b) {
        if(a==b)return true;
        const auto first=std::lower_bound(corridor.ports.begin(),corridor.ports.end(),a);
        const auto second=std::lower_bound(corridor.ports.begin(),corridor.ports.end(),b);
        return first!=corridor.ports.end() && *first==a && second!=corridor.ports.end() && *second==b
            && corridor.portGroups[first-corridor.ports.begin()]==corridor.portGroups[second-corridor.ports.begin()];
    };
    auto moving=[&](const Ent& other) {
        return other.alive && !other.isBuilding && !other.deployed && !other.radDeployed
            && other.pathIdx<(int)other.path.size() && (other.state==UState::Moving || other.state==UState::AttackMoving
            || other.state==UState::Chasing || other.state==UState::HarvestGo || other.state==UState::HarvestReturn
            || other.state==UState::Boarding || other.state==UState::Servicing);
    };
    auto exitFor=[&](const Ent& other) {
        bool entered=inside((int)other.y*map.w+(int)other.x);
        for(int step=other.pathIdx;step<(int)other.path.size();++step) {
            const auto point=other.path[step];const int cell=point.y*map.w+point.x;
            if(inside(cell)) entered=true;
            else if(entered) return cell;
        }
        return -1; // Local docking/harvesting goals do not claim through traffic.
    };
    const int anchor=*std::min_element(corridor.cells.begin(),corridor.cells.end());
    auto gateAt=std::lower_bound(trafficGates.begin(),trafficGates.end(),std::pair{domain,anchor},[](const auto& gate,const auto& key) {
        return std::pair{gate.domain,gate.anchor}<key;
    });
    if(gateAt==trafficGates.end() || gateAt->domain!=domain || gateAt->anchor!=anchor)
        gateAt=trafficGates.insert(gateAt,TrafficGate{domain,anchor});
    auto& gate=*gateAt;gate.touched=true;
    if(gate.exit>=0 && !std::binary_search(corridor.ports.begin(),corridor.ports.end(),gate.exit)) {
        gate.exit=-1;gate.draining=false;gate.until=0;
    }
    auto phaseTicks=[&](EID leader) {
        const int delay=(int)std::clamp(unitDef(ents[leader].utype).speed,1,120);
        // Collapse each open junction into one graph vertex. In a tree every
        // exit has a unique crossing route; loops can redistribute traffic
        // during a phase, so retain their conservative component-size window.
        std::vector<int> vertices(map.cells.size(),-1);int vertexCount=0;
        for(int cell:corridor.cells) {
            if(vertices[cell]>=0)continue;
            const int vertex=vertexCount++;vertices[cell]=vertex;
            if(narrowMovementCell(cell%map.w,cell/map.w,domain))continue;
            std::vector<int> patch{cell};
            for(size_t at=0;at<patch.size();++at)for(Vec2i step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
                const int x=patch[at]%map.w,y=patch[at]/map.w,nx=x+step.x,ny=y+step.y;
                if(!map.inBounds(nx,ny) || !passableStep(x,y,nx,ny,domain))continue;
                const int next=ny*map.w+nx;
                if(inside(next) && vertices[next]<0 && !narrowMovementCell(nx,ny,domain)){vertices[next]=vertex;patch.push_back(next);}
            }
        }
        std::vector<std::pair<int,int>> edges;
        for(int cell:corridor.cells)for(Vec2i step:{Vec2i{1,0},Vec2i{0,1}}) {
            const int x=cell%map.w,y=cell/map.w,nx=x+step.x,ny=y+step.y;
            if(!map.inBounds(nx,ny) || !passableStep(x,y,nx,ny,domain))continue;
            const int next=ny*map.w+nx;
            if(inside(next) && vertices[cell]!=vertices[next])edges.emplace_back(std::min(vertices[cell],vertices[next]),std::max(vertices[cell],vertices[next]));
        }
        std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
        if(edges.size()>=(size_t)vertexCount)return std::clamp<uint64_t>(corridor.cells.size()*(uint64_t)delay,90,1000000);
        // A clearance window must cover every arm, not only the leader's
        // route. Use the furthest shortest crossing to this exit rather than
        // summing parallel branches and loops as if one car traversed them all.
        const int exit=exitFor(ents[leader]);
        std::vector<int> distance(map.cells.size(),-1),search;
        int crossing=0;
        if(exit>=0 && map.inBounds(exit%map.w,exit/map.w)){distance[exit]=0;search.push_back(exit);}
        for(size_t at=0;at<search.size();++at) {
            const int cell=search[at],x=cell%map.w,y=cell/map.w;
            for(Vec2i step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
                const int nx=x+step.x,ny=y+step.y;
                if(!map.inBounds(nx,ny) || !passableStep(x,y,nx,ny,domain))continue;
                const int next=ny*map.w+nx;
                if(inside(next) && distance[next]<0){distance[next]=distance[cell]+1;crossing=std::max(crossing,distance[next]);search.push_back(next);}
            }
        }
        return std::clamp<uint64_t>(std::max(1,crossing)*(uint64_t)delay,90,1000000);
    };
    if(!corridor.initialized) {
        corridor.initialized=true;
        std::vector<EID> occupants;
        for(int cell:corridor.cells) for(EID occupant=unitOccHead[cell];occupant!=INVALID_EID;occupant=ents[occupant].occNext)
            if(moving(ents[occupant]) && exitFor(ents[occupant])>=0) occupants.push_back(occupant);
        std::sort(occupants.begin(),occupants.end());
        corridor.occupied=!occupants.empty();
        if(gate.exit<0 && !occupants.empty()) {
            gate.exit=exitFor(ents[occupants.front()]);
            gate.until=tick+phaseTicks(occupants.front());
        }
        corridor.exit=gate.exit;
        // Old saves or units initially inside a lane may already oppose each
        // other. Retreat toward the chosen exit, then resume the original goal.
        for(EID occupant:occupants) {
            auto& other=ents[occupant];
            const auto next=other.path.begin()+other.pathIdx;
            const bool waiting=std::adjacent_find(next,other.path.end())!=other.path.end();
            // A retreat's chosen exit can become a dead end occupied by an
            // opposing waiter. Keeping that exit in every subsequent replan
            // strands the whole column, even with a free bay at the other port.
            // Only a stalled, reciprocal allied conflict may reconsider it;
            // preserve progressing retreats and protected/special tasks.
            if(waiting && other.blockTick>=30 && !unitDef(other.utype).isAir() && !other.guard && !other.immobilized()
                && (other.state==UState::Moving || other.state==UState::AttackMoving
                    || other.state==UState::HarvestGo || other.state==UState::HarvestReturn)
                && (tick+(uint64_t)occupant)%8==0 && repathsThisTick<8) {
                const EID peer=unitAtCell(next->x,next->y);
                if(valid(peer) && peer!=occupant && isAllied(other.player,ents[peer].player)
                    && moving(ents[peer]) && !ents[peer].guard && !ents[peer].immobilized()
                    && ents[peer].blockTick>=10
                    && cellIdx(ents[peer].path[ents[peer].pathIdx].x,ents[peer].path[ents[peer].pathIdx].y)==other.occCell) {
                    std::vector<uint8_t> costs(map.cells.size(),0);
                    for(size_t actor=0;actor<ents.size();++actor) {
                        const auto& obstacle=ents[actor];
                        if((EID)actor==occupant || obstacle.occCell<0)continue;
                        costs[obstacle.occCell]=std::max(costs[obstacle.occCell],uint8_t{16});
                        if(!moving(obstacle) || obstacle.guard || obstacle.immobilized()
                            || !isAllied(other.player,obstacle.player))costs[obstacle.occCell]=255;
                    }
                    const auto goal=other.path.back();std::vector<Vec2i> alternative;
                    ++repathsThisTick;++repathRequests;
                    if(map.findPath((int)other.x,(int)other.y,goal.x,goal.y,alternative,20000,domain,&costs)
                        && !alternative.empty() && alternative.size()<=4096 && alternative.back()==goal) {
                        int alternativeExit=-1;
                        for(auto point:alternative)if(!inside(cellIdx(point.x,point.y))){alternativeExit=cellIdx(point.x,point.y);break;}
                        if(alternativeExit>=0 && alternativeExit!=exitFor(other)
                            && std::binary_search(corridor.ports.begin(),corridor.ports.end(),alternativeExit)) {
                            other.path=std::move(alternative);other.pathIdx=0;other.blockTick=0;
                            gate.exit=alternativeExit;gate.draining=false;gate.until=tick+phaseTicks(occupant);
                            corridor.exit=alternativeExit;
                            continue;
                        }
                    } else ++repathFailures;
                }
            }
            if(sameExit(exitFor(other),corridor.exit)) continue;
            // A cooperative pullout may deliberately leave through another
            // port before returning. Replacing it each admission tick turns
            // fractional motion back toward its occupied original exit.
            if(waiting) {
                corridor.opposed=true;continue;
            }

            // A blocked retreat can otherwise retry two searches every tick,
            // consuming the shared budget before other units are updated.
            // Stagger by stable ID without adding replay-sensitive timers.
            if((tick+(uint64_t)occupant)%8!=0) { corridor.opposed=true;continue; }
            if(repathsThisTick+2>8) { corridor.opposed=true;continue; }
            const auto goal=other.path.back();std::vector<Vec2i> retreat,resume;
            std::vector<uint8_t> costs(map.cells.size(),255);
            for(int cell:corridor.cells) costs[cell]=0;
            costs[corridor.exit]=0;
            // Local parking tasks are not admitted as through traffic and
            // cannot be assumed to clear a retreat route in gate order.
            for(size_t local=0;local<ents.size();++local) {
                const auto& parked=ents[local];
                if((EID)local==occupant || !parked.alive || parked.isBuilding || parked.occCell<0)continue;
                if(inside(parked.occCell) && exitFor(parked)<0
                    && cellHardBlockedForMove(parked.occCell%map.w,parked.occCell/map.w,occupant))
                    costs[parked.occCell]=255;
            }
            repathsThisTick+=2;repathRequests+=2;
            if(!map.findPath((int)other.x,(int)other.y,corridor.exit%map.w,corridor.exit/map.w,retreat,20000,domain,&costs)
                || !map.findPath(corridor.exit%map.w,corridor.exit/map.w,goal.x,goal.y,resume,20000,domain)
                || retreat.size()+resume.size()+30>4096) { ++repathFailures;corridor.opposed=true;continue; }
            retreat.insert(retreat.end(),30,Vec2i{corridor.exit%map.w,corridor.exit/map.w});
            retreat.insert(retreat.end(),resume.begin(),resume.end());
            other.path=std::move(retreat);other.pathIdx=0;other.blockTick=0;
        }
    }
    if(inside(current)) { corridor.occupied=true;return true; } // Occupants must be able to reach the exit.
    const int wanted=exitFor(unit);
    if(wanted<0) return true;
    std::vector<EID> candidates;
    for(int port:corridor.ports) for(int dy=-6;dy<=6;++dy) for(int dx=-6;dx<=6;++dx) {
        const int x=port%map.w+dx,y=port/map.w+dy;
        if(!map.inBounds(x,y)) continue;
        for(EID other=unitOccHead[y*map.w+x];other!=INVALID_EID;other=ents[other].occNext) {
            const auto& actor=ents[other];
            if(!moving(actor) || inside((int)actor.y*map.w+(int)actor.x)) continue;
            const int limit=std::min((int)actor.path.size(),actor.pathIdx+6);
            bool near=false;
            for(int step=actor.pathIdx;step<limit;++step) near |= inside(actor.path[step].y*map.w+actor.path[step].x);
            if(near && exitFor(actor)>=0) candidates.push_back(other);
        }
    }
    candidates.push_back(id);std::sort(candidates.begin(),candidates.end());
    candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
    bool opposing=false,currentWaiting=false;
    for(EID other:candidates) {
        const int exit=exitFor(ents[other]);
        opposing |= gate.exit>=0 && !sameExit(exit,gate.exit);
        currentWaiting |= sameExit(exit,gate.exit);
    }
    // An empty network need not keep a time slice reserved for a direction
    // with no waiting traffic. Large branched regions otherwise waste entire
    // phases while the only remaining vehicle waits outside another port.
    if(opposing && (tick>=gate.until || (!corridor.occupied && !currentWaiting))) gate.draining=true;
    if(gate.exit<0 || (gate.draining && !corridor.occupied)) {
        EID first=INVALID_EID;
        // When draining finishes, serve the other direction before admitting a
        // fresh batch. This latch must survive saves and fractional movement.
        for(EID other:candidates) {
            if(gate.draining && opposing && sameExit(exitFor(ents[other]),gate.exit)) continue;
            if(first==INVALID_EID || ents[other].blockTick>ents[first].blockTick) first=other;
        }
        if(first!=INVALID_EID) {
            gate.exit=exitFor(ents[first]);gate.draining=false;
            gate.until=tick+phaseTicks(first);
        }
    }
    if(corridor.opposed || !sameExit(gate.exit,wanted) || gate.draining) {
        auto distanceToPort=[&](int cell) {
            int distance=map.w+map.h;
            for(int port:corridor.ports) distance=std::min(distance,
                std::abs(cell%map.w-port%map.w)+std::abs(cell/map.w-port/map.w));
            return distance;
        };
        // A waiting car may have been asked to clear the apron. Admission must
        // not freeze its outward pullout or its dwell waypoints.
        const int nextCell=next.y*map.w+next.x;
        if(!inside(nextCell) && (nextCell==current || distanceToPort(nextCell)>=distanceToPort(current))) return true;
        // Admission itself must not strand an opposing unit immediately in
        // front of this waiter. A proven reciprocal swap may leave the apron
        // through an ordinary pullout, then resume the original route.
        if(unit.blockTick>=10 && map.inBounds(next.x,next.y)) {
            const EID peer=unitAtCell(next.x,next.y);
            if(valid(peer) && peer!=id && ents[peer].blockTick>=10
                && (tick+(uint64_t)std::min(id,peer))%8==0
                && yieldMovingConflict(id,peer,true))return false;
        }
        if(distanceToPort(current)<5 && unit.blockTick>=10 && repathsThisTick+2<=8) {
            std::vector<uint8_t> costs(map.cells.size(),0);
            for(int cell:corridor.cells) costs[cell]=255;
            for(size_t actor=0;actor<ents.size();++actor) {
                const auto& other=ents[actor];
                if((EID)actor!=id && other.alive && !other.isBuilding && !unitDef(other.utype).isAir()
                    && map.inBounds((int)other.x,(int)other.y)) costs[(int)other.y*map.w+(int)other.x]=255;
            }
            int pullout=-1;
            std::vector<int> search{current};std::vector<uint8_t> seen(map.cells.size(),0);seen[current]=1;
            for(size_t at=0;at<search.size() && at<2048 && pullout<0;++at) {
                int cell=search[at];
                if(cell!=current && distanceToPort(cell)>=6) { pullout=cell;break; }
                for(Vec2i delta:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
                    int x=cell%map.w+delta.x,y=cell/map.w+delta.y;
                    if(!passableStep(cell%map.w,cell/map.w,x,y,domain) || bldBlocked(x,y)) continue;
                    int neighbor=y*map.w+x;
                    if(!seen[neighbor] && costs[neighbor]!=255) { seen[neighbor]=1;search.push_back(neighbor); }
                }
            }            if(pullout>=0) {
                std::vector<Vec2i> retreat,resume;const auto goal=unit.path.back();
                repathsThisTick+=2;repathRequests+=2;
                if(map.findPath((int)unit.x,(int)unit.y,pullout%map.w,pullout/map.w,retreat,20000,domain,&costs)
                    && !retreat.empty() && retreat.back().x==pullout%map.w && retreat.back().y==pullout/map.w
                    && map.findPath(pullout%map.w,pullout/map.w,goal.x,goal.y,resume,20000,domain)
                    && retreat.size()+resume.size()+30<=4096) {
                    retreat.insert(retreat.end(),30,Vec2i{pullout%map.w,pullout/map.w});
                    retreat.insert(retreat.end(),resume.begin(),resume.end());
                    unit.path=std::move(retreat);unit.pathIdx=0;unit.blockTick=0;return false;
                }
                ++repathFailures;
            }
        }
        unit.blockTick=std::min(unit.blockTick+1,1000000);return false;
    }
    if(inside(next.y*map.w+next.x)) corridor.occupied=true;
    return true;
}
