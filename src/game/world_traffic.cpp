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
                    if(ents[id].alive && !ents[id].isBuilding && !unitDef(ents[id]).isAir()
                        && unitDef(ents[id]).pathDomain()==gate.domain)++row.occupants;
            }
        }
        result.push_back(row);
    }
    return result;
}

World::IdleYieldResult World::yieldIdleCorridor(EID requester,EID blocker) {
    if(!valid(requester) || !valid(blocker) || !isAllied(ents[requester].player,ents[blocker].player))
        return IdleYieldResult::NotApplicable;
    auto idle=[&](const Ent& unit,bool waitingAtApron=false) {
        const auto& definition=unitDef(unit);
        return unit.alive && !unit.isBuilding && !definition.isAir() && !unit.parasiting
            && (unit.state==UState::Idle || waitingAtApron) && !unit.deployed && !unit.radDeployed && !unit.guard
            && unit.pathIdx>=(int)unit.path.size() && unit.wps.empty() && !valid(unit.target)
            && unit.engineerIntent==EngineerIntent::None
            && (waitingAtApron || !(definition.canHarvet() && unit.autoHarvest && (!nativeGroundEnabled() || unit.oreLoad>0)))
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
    const auto receiver=lead.nativeDock.receiver;
    bool waitingAtApron=false;
    if(lead.exitingNativeDock() && parked.player==lead.player
        && parked.utype==UnitType::ChronoMiner && parked.state==UState::HarvestReturn
        && parked.chronoReturn.phase==ChronoReturnPhase::Waiting && parked.chronoReturn.directBerth
        && parked.nativeDock.phase==NativeDockPhase::Approach && parked.nativeDock.receiver==receiver && valid(receiver)) {
        const auto& refinery=ents[receiver.id];
        waitingAtApron=refinery.player==parked.player && parked.navLayer==refinery.navLayer && lead.navLayer==refinery.navLayer
            && (int)parked.x==(int)refinery.x+4 && (int)parked.y==(int)refinery.y+1;
    }
    const int domain=unitDef(parked).pathDomain(),area=(int)map.cells.size();
    if(!idle(parked,waitingAtApron) || parked.occCell<0)return IdleYieldResult::NotApplicable;
    const int start=(int)movementSlot((int)parked.x,(int)parked.y,parked.navLayer);
    if(repathsThisTick>=8) return IdleYieldResult::BudgetWait;
    std::vector<uint8_t> costs(movementSlotCount(),0),landing(movementSlotCount(),1),avoid(movementSlotCount(),0);
    int leadX=(int)lead.x,leadY=(int)lead.y,leadLayer=lead.navLayer;
    for(int step=lead.pathIdx;step<(int)lead.path.size();++step) {
        const auto point=lead.path[step];
        if(map.hasBridgeDecks())leadLayer=movementStep(lead,leadX,leadY,leadLayer,point.x,point.y);
        if(leadLayer<0)break;
        avoid[movementSlot(point.x,point.y,leadLayer)]=1;leadX=point.x;leadY=point.y;
    }
    if((lead.exitingNativeDock() || lead.chronoReturn.phase==ChronoReturnPhase::Waiting)
        && valid(lead.nativeDock.receiver)) {
        const auto& receiver=ents[lead.nativeDock.receiver.id];
        const int x=(int)receiver.x+4,y=(int)receiver.y+1;
        if(map.inBounds(x,y))avoid[movementSlot(x,y,receiver.navLayer)]=1;
    }
    for(size_t index=0;index<ents.size();++index) {
        if((EID)index==blocker) continue;
        const auto& other=ents[index];
        const bool allied=isAllied(parked.player,other.player);
        const bool active=other.alive && !other.isBuilding && moving(other);
        if(active && allied && !other.path.empty()) {
            const auto goal=other.path.back();landing[movementSlot(goal.x,goal.y,movementLayerAt(other,goal.x,goal.y))]=0;
        }
        if(other.occCell<0 || other.occCell>=area)continue;
        const int cell=other.occCell+other.navLayer*area;
        const bool cooperative=allied && (idle(other) || active);
        costs[cell]=std::max(costs[cell],uint8_t{16});
        if((EID)index==requester || (!cooperative && cellHardBlockedForMove(cell%area%map.w,cell%area/map.w,blocker,other.navLayer)))
            costs[cell]=255;
        const bool vacating=active && allied && other.path.back()!=Vec2i{cell%area%map.w,cell%area/map.w};
        if(!vacating || (EID)index==requester) landing[cell]=0;
    }
    // Open parking fields need the same continuous, reserved-goal-aware
    // yielding as narrow lanes; a one-cell teleport can create swap cycles.
    // Find the nearest passing bay without teleporting the parked unit. Idle
    // allies on the route may be asked to yield in turn, forming a legal chain.
    std::vector<int> search{start};std::vector<uint8_t> seen(movementSlotCount(),0);seen[start]=1;
    int goal=-1;
    for(size_t at=0;at<search.size() && at<4096;++at) {
        const int cell=search[at],x=cell%area%map.w,y=cell%area/map.w,layer=cell/area;
        if(landing[cell] && !avoid[cell] && !narrowMovementCell(x,y,domain,layer,unitDef(parked).fitsUnderBridge())) { goal=cell;break; }
        for(Vec2i delta:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const int nx=x+delta.x,ny=y+delta.y;
            const int surface=movementStep(parked,x,y,layer,nx,ny);
            if(surface<0 || bldBlocked(nx,ny,surface))continue;
            const int next=(int)movementSlot(nx,ny,surface);
            if(!seen[next] && costs[next]!=255) { seen[next]=1;search.push_back(next); }
        }
    }
    if(goal<0) return IdleYieldResult::NoPullout;
    std::vector<Vec2i> path;++repathsThisTick;++repathRequests;
    if(!map.findPath((int)parked.x,(int)parked.y,goal%area%map.w,goal%area/map.w,path,20000,domain,&costs,parked.navLayer,goal/area,unitDef(parked).fitsUnderBridge(),nativeGateAccess(parked))
        || path.empty() || path.size()>4096 || path.back()!=Vec2i{goal%area%map.w,goal%area/map.w}) {
        ++repathFailures;return IdleYieldResult::NoPullout;
    }
    parked.path=std::move(path);parked.pathIdx=0;parked.blockTick=0;
    if(!waitingAtApron)parked.state=UState::Moving;
    return IdleYieldResult::Started;
}

void World::resetMovementCorridors() {
    for(int domain=0;domain<3;++domain) { movementCorridorLabels[domain].clear();movementCorridors[domain].clear(); }
}

bool World::narrowMovementCell(int x,int y,int domain,int layer,bool underBridge) const {
    if(map.hasBridgeDecks()) {
        if(layer<0)layer=map.preferredLayer(x,y,domain);
        if(!map.surfacePassable(x,y,domain,layer,true,underBridge))return false;
        int neighbors=0;
        for(Vec2i step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}})
            neighbors+=map.stepLayer(x,y,layer,x+step.x,y+step.y,domain,true,underBridge)>=0;
        if(neighbors<2)return false;
        for(int dx:{-1,1})for(int dy:{-1,1}) {
            const int a=map.stepLayer(x,y,layer,x+dx,y,domain,true,underBridge),b=map.stepLayer(x,y,layer,x,y+dy,domain,true,underBridge);
            if(a<0 || b<0)continue;
            const int c=map.stepLayer(x+dx,y,a,x+dx,y+dy,domain,true,underBridge),d=map.stepLayer(x,y+dy,b,x+dx,y+dy,domain,true,underBridge);
            if(c>=0 && c==d)return false;
        }
        return true;
    }
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

void World::releaseReversingLane(EID id) {
    const auto& unit=ents[id];
    const int domain=unitDef(unit).pathDomain();
    const int lane=movementCorridor(cellIdx((int)unit.x,(int)unit.y),domain);
    if(lane<0)return;
    auto& corridor=movementCorridors[domain][lane];
    const int anchor=*std::min_element(corridor.cells.begin(),corridor.cells.end());
    const auto gate=std::find_if(trafficGates.begin(),trafficGates.end(),[&](const auto& phase) {
        return phase.domain==domain && phase.anchor==anchor;
    });
    if(gate==trafficGates.end() || gate->draining)return;
    for(int cell:corridor.cells)
        for(EID other=unitOccHead[cell];other!=INVALID_EID;other=ents[other].occNext)
            if(other!=id && valid(other))return;
    // Normal admission will choose the exit from the newly ordered path.
    trafficGates.erase(gate);
    corridor.exit=-1;corridor.initialized=false;corridor.opposed=false;
}

bool World::admitMovementCorridor(Ent& unit,EID id) {
    if(unitDef(unit).isAir() || unit.pathIdx>=(int)unit.path.size()) return true;
    const int domain=unitDef(unit).pathDomain();
    // Let cooperative pullouts reach their waiting bay. Once there, retain
    // the dwell prefix until this direction is admitted; repeatedly returning
    // to a closed portal causes expensive pullout/replan loops and blocks exits.
    bool heldAtPullout=false;int waitingLane=-1;
    for(int step=unit.pathIdx;step+1<(int)unit.path.size();++step) {
        const auto point=unit.path[step],following=unit.path[step+1];
        if(narrowMovementCell(point.x,point.y,domain)) break;
        if(point.x!=following.x || point.y!=following.y)continue;
        const bool atBay=std::abs(unit.x-point.x-0.5f)<0.001f && std::abs(unit.y-point.y-0.5f)<0.001f;
        if(!atBay || unit.path[unit.pathIdx]!=point)return true;
        heldAtPullout=true;
        for(int after=step+2;after<(int)unit.path.size();++after) {
            const auto future=unit.path[after];
            if(!narrowMovementCell(future.x,future.y,domain))continue;
            waitingLane=movementCorridor(future.y*map.w+future.x,domain);break;
        }
        break;
    }
    const int current=(int)unit.y*map.w+(int)unit.x;
    const auto next=unit.path[unit.pathIdx];
    int lane=waitingLane>=0?waitingLane:map.inBounds(next.x,next.y)?movementCorridor(next.y*map.w+next.x,domain):-1;
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
        const int delay=(int)std::clamp(unitDef(ents[leader]).speed,1,120);
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
        // Admission is first requested from the six-cell passing apron, not
        // from the first lane cell. Include that approach in the green window.
        return std::clamp<uint64_t>((std::max(1,crossing)+6)*(uint64_t)delay,90,1000000);
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
            if(waiting && other.blockTick>=30 && !unitDef(other).isAir() && !other.guard && !other.immobilized()
                && (other.state==UState::Moving || other.state==UState::AttackMoving
                    || other.state==UState::HarvestGo || other.state==UState::HarvestReturn)
                && (tick+(uint64_t)occupant)%8==0 && repathsThisTick<8) {
                const Vec2i origin{other.occCell%map.w,other.occCell/map.w};
                const int nextLayer=movementStep(other,origin.x,origin.y,other.navLayer,next->x,next->y);
                bool reciprocal=false;
                if(nextLayer>=0 && cellHardBlockedForMove(next->x,next->y,occupant,nextLayer)) {
                    // A full infantry cell can contain a dwelling head and
                    // active waiters which want to step back into this cell.
                    for(EID peer=unitAtCell(next->x,next->y,nextLayer);peer!=INVALID_EID;peer=ents[peer].occNext) {
                        const auto& blocker=ents[peer];
                        if(peer==occupant || blocker.navLayer!=nextLayer || !isAllied(other.player,blocker.player)
                            || !moving(blocker) || blocker.guard || blocker.immobilized() || blocker.blockTick<10
                            || blocker.path[blocker.pathIdx]!=origin)continue;
                        if(movementStep(blocker,next->x,next->y,nextLayer,origin.x,origin.y)==other.navLayer
                            && cellHardBlockedForMove(origin.x,origin.y,peer,other.navLayer)) {
                            reciprocal=true;
                            break;
                        }
                    }
                }
                if(reciprocal) {
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
                    if(map.findPath((int)other.x,(int)other.y,goal.x,goal.y,alternative,20000,domain,&costs,other.navLayer,movementLayerAt(other,goal.x,goal.y),unitDef(other).fitsUnderBridge(),nativeGateAccess(other))
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
            if(!map.findPath((int)other.x,(int)other.y,corridor.exit%map.w,corridor.exit/map.w,retreat,20000,domain,&costs,other.navLayer,-1,unitDef(other).fitsUnderBridge(),nativeGateAccess(other))
                || !map.findPath(corridor.exit%map.w,corridor.exit/map.w,goal.x,goal.y,resume,20000,domain,nullptr,routeEndLayer(other,retreat),movementLayerAt(other,goal.x,goal.y),unitDef(other).fitsUnderBridge(),nativeGateAccess(other))
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
            // Count physical steps, not repeated dwell points, so a waiting
            // bay remains represented when its direction becomes eligible.
            bool near=false;int advances=0;Vec2i previous{(int)actor.x,(int)actor.y};
            for(int step=actor.pathIdx;step<(int)actor.path.size() && advances<8;++step) {
                const auto point=actor.path[step];
                if(point!=previous){++advances;previous=point;}
                near |= inside(point.y*map.w+point.x);
            }
            if(near && exitFor(actor)>=0) candidates.push_back(other);
        }
    }
    candidates.push_back(id);std::sort(candidates.begin(),candidates.end());
    candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
    auto canApproach=[&](EID other) {
        const auto& actor=ents[other];
        if(actor.immobilized() || actor.deploying() || actor.guard || actor.pathIdx>=(int)actor.path.size())return false;
        const auto point=actor.path[actor.pathIdx];
        return !bldBlocked(point.x,point.y) && !cellHardBlockedForMove(point.x,point.y,other);
    };
    bool opposing=false,currentWaiting=false;
    for(EID other:candidates) {
        const int exit=exitFor(ents[other]);
        opposing |= gate.exit>=0 && !sameExit(exit,gate.exit);
        currentWaiting |= sameExit(exit,gate.exit) && canApproach(other);
    }
    // An empty network need not keep a time slice reserved for a direction
    // with no waiting traffic. Large branched regions otherwise waste entire
    // phases while the only remaining vehicle waits outside another port.
    if(opposing && (tick>=gate.until || (!corridor.occupied && !currentWaiting))) gate.draining=true;
    if(gate.exit<0 || (gate.draining && !corridor.occupied)) {
        EID first=INVALID_EID;
        // When draining finishes, serve the other direction before admitting a
        // fresh batch. This latch must survive saves and fractional movement.
        for(bool readyOnly:{true,false}) {
            for(EID other:candidates) {
                if((readyOnly && !canApproach(other)) || (gate.draining && opposing && sameExit(exitFor(ents[other]),gate.exit)))continue;
                if(first==INVALID_EID || ents[other].blockTick>ents[first].blockTick)first=other;
            }
            if(first!=INVALID_EID)break;
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
        if(!heldAtPullout && !inside(nextCell) && (nextCell==current || distanceToPort(nextCell)>=distanceToPort(current))) return true;
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
                if((EID)actor!=id && other.alive && !other.isBuilding && !unitDef(other).isAir()
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
                if(map.findPath((int)unit.x,(int)unit.y,pullout%map.w,pullout/map.w,retreat,20000,domain,&costs,unit.navLayer,-1,unitDef(unit).fitsUnderBridge(),nativeGateAccess(unit))
                    && !retreat.empty() && retreat.back().x==pullout%map.w && retreat.back().y==pullout/map.w
                    && map.findPath(pullout%map.w,pullout/map.w,goal.x,goal.y,resume,20000,domain,nullptr,routeEndLayer(unit,retreat),movementLayerAt(unit,goal.x,goal.y),unitDef(unit).fitsUnderBridge(),nativeGateAccess(unit))
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
    if(heldAtPullout) {
        // Waiting for permission has already provided the clearance interval.
        // Do not spend another 30 stationary ticks after the lane turns green.
        const auto bay=unit.path[unit.pathIdx];
        while(unit.pathIdx<(int)unit.path.size() && unit.path[unit.pathIdx]==bay)++unit.pathIdx;
        unit.blockTick=0;
        return false; // moveAlongPath must fetch the new next node next tick.
    }
    if(inside(next.y*map.w+next.x)) corridor.occupied=true;
    return true;
}
