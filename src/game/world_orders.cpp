#include "game/world.h"
#include "game/actor_state_transfer.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <climits>
#include <limits>
#include <map>
#include <tuple>

namespace {
struct FormationGrid {
    int count,columns,rows;
    explicit FormationGrid(int actors)
        : count(actors),columns((int)ceilf(sqrtf((float)actors))),rows((actors+columns-1)/columns) {}

    Vec2f position(int slot,Vec2i forward,float x,float y) const {
        constexpr float formationSpacing=1.5f;
        const float spacing=forward.x && forward.y?formationSpacing*.707106781186548f:formationSpacing;
        const int rowWidth=std::min(columns,count-slot/columns*columns);
        const float side=(slot%columns-(rowWidth-1)*.5f)*spacing;
        const float depth=((rows-1)*.5f-slot/columns)*spacing;
        return {x+depth*forward.x-side*forward.y,y+depth*forward.y+side*forward.x};
    }
};

bool repeatsFormation(const World& world,const std::vector<EID>& units,const FormationGrid& grid,
                      Vec2f destination,bool attackMove,int requestedLayer) {
    const auto& map=world.map;
    const auto movingState=attackMove?UState::AttackMoving:UState::Moving;
    std::vector<int> assigned;
    assigned.reserve(units.size());
    for(EID id:units) {
        const auto& actor=world.ents[id];
        const auto& def=unitDef(actor);
        if(def.isAir() || actor.target!=INVALID_EID || actor.guard || !actor.wps.empty()
            || actor.nativeIdentity.sleeping || actor.engineerIntent!=EngineerIntent::None
            || (def.canHarvet() && actor.autoHarvest) || world.nativeTemporalBeams.contains(id))return false;
        if(!(actor.goalX>=0 && actor.goalX<map.w && actor.goalY>=0 && actor.goalY<map.h))return false;
        const Vec2i cell{(int)actor.goalX,(int)actor.goalY};
        const int domain=def.pathDomain();
        const int layer=domain==1?0:requestedLayer<0?map.preferredLayer(cell.x,cell.y,domain):requestedLayer;
        if(actor.goalX!=cell.x+.5f || actor.goalY!=cell.y+.5f || actor.goalLayer!=layer
            || !map.surfacePassable(cell.x,cell.y,domain,layer,true,def.fitsUnderBridge(),world.nativeGateAccess(actor))
            || world.cellHardBlockedForMove(cell.x,cell.y,id,layer))return false;
        if(actor.state==UState::Idle) {
            if(actor.attackMove.active || actor.navLayer!=layer || actor.x!=actor.goalX || actor.y!=actor.goalY)return false;
        } else {
            const World::Ent::AttackMoveOrder retained=attackMove
                ?World::Ent::AttackMoveOrder{true,actor.player,actor.goalX,actor.goalY,actor.goalLayer,0}
                :World::Ent::AttackMoveOrder{};
            if(actor.state!=movingState || actor.blockTick || actor.pathIdx>=(int)actor.path.size()
                || actor.path.back()!=cell || actor.attackMove!=retained)return false;
        }
        assigned.push_back(world.cellIdx(cell.x,cell.y));
    }
    std::sort(assigned.begin(),assigned.end());
    std::vector<int> slots(units.size());
    // The occupied or assigned slots encode the formation already in use.
    // Check each orientation because the approach vector changes on arrival.
    for(const auto forward:NativeDriveMath::directions) {
        bool fits=true;
        for(int n=0;n<grid.count;++n) {
            const auto slot=grid.position(n,forward,destination.x,destination.y);
            const int x=(int)slot.x,y=(int)slot.y;
            if(!map.inBounds(x,y)){fits=false;break;}
            slots[n]=world.cellIdx(x,y);
        }
        if(!fits)continue;
        std::sort(slots.begin(),slots.end());
        if(slots==assigned)return true;
    }
    return false;
}

Vec2i orientFormation(const std::deque<World::Ent>& ents,std::vector<EID>& units,float x,float y,size_t columns) {
    double centerX=0,centerY=0;
    for(EID id:units) {
        centerX+=ents[id].x;
        centerY+=ents[id].y;
    }
    const double dx=x-centerX/units.size(),dy=y-centerY/units.size();
    const double absX=std::abs(dx),absY=std::abs(dy);
    Vec2i forward{0,-1};
    if(std::max(absX,absY)>=.5) {
        constexpr double betweenAxes=.414213562373095; // tan(22.5 degrees)
        forward={absX>absY*betweenAxes?(dx>0?1:-1):0,
                 absY>absX*betweenAxes?(dy>0?1:-1):0};
    }
    const Vec2i side{-forward.y,forward.x};
    const auto project=[&](EID id,Vec2i axis){return ents[id].x*axis.x+ents[id].y*axis.y;};
    const auto depthKey=[&](EID id){return std::tuple{-project(id,forward),project(id,side),id};};
    const auto sideKey=[&](EID id){return std::tuple{project(id,side),-project(id,forward),id};};
    // Assign the front row first. Within each row keep units on their own side
    // of the group so the order in which they were selected cannot swap lanes.
    std::sort(units.begin(),units.end(),[&](EID a,EID b){return depthKey(a)<depthKey(b);});
    for(size_t first=0;first<units.size();first+=columns) {
        const size_t end=std::min(first+columns,units.size());
        std::sort(units.begin()+first,units.begin()+end,[&](EID a,EID b){return sideKey(a)<sideKey(b);});
    }
    return forward;
}

// A legal monotone route has the octile lower-bound cost, so no heap search
// can improve it when this command has no congestion penalties. Keep layered
// bridges on their existing surface-aware A* path. If any step needs a detour,
// leave the entire order to A* rather than committing a partial route.
bool directOrderPath(const Map& map,int sx,int sy,int tx,int ty,int domain,bool underBridge,uint8_t gateAccess,std::vector<Vec2i>& path) {
    path.clear();
    if(map.hasBridgeDecks() || !map.inBounds(sx,sy) || !map.inBounds(tx,ty))return false;
    int x=sx,y=sy;path.reserve((size_t)std::max(std::abs(tx-sx),std::abs(ty-sy)));
    while(x!=tx || y!=ty) {
        const int dx=tx-x,dy=ty-y,ax=std::abs(dx),ay=std::abs(dy);
        const int stepX=(dx>0)-(dx<0),stepY=(dy>0)-(dy<0);
        Vec2i best{-1,-1};int bestCost=-1;int64_t bestCross=INT64_MAX;
        const auto candidate=[&](int ox,int oy) {
            if(!ox && !oy)return;
            const int nx=x+ox,ny=y+oy;
            if(map.stepLayer(x,y,0,nx,ny,domain,true,underBridge,gateAccess)!=0)return;
            const int64_t cross=std::abs((int64_t)(nx-sx)*(ty-sy)-(int64_t)(ny-sy)*(tx-sx));
            const int cost=ox && oy?14142:10000;
            if(cross<bestCross || (cross==bestCross && (cost>bestCost
                || (cost==bestCost && (ny<best.y || (ny==best.y && nx<best.x)))))) {
                best={nx,ny};bestCross=cross;bestCost=cost;
            }
        };
        // These are exactly the steps that preserve the octile lower bound.
        if(ax>ay)candidate(stepX,0);
        if(ay>ax)candidate(0,stepY);
        if(ax && ay)candidate(stepX,stepY);
        if(best.x<0){path.clear();return false;}
        x=best.x;y=best.y;path.push_back(best);
    }
    return true;
}
}

MoveOrderResult World::orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove, bool append,
                                 bool feedback,int layer,MoveFormation formation) {
    MoveOrderResult result;
    result.attackMove=attackMove;
    auto actor=[&](EID id,Ent& unit,bool preserveRoute=false){result.acceptedUnits.push_back(id);unit.nativeIdentity.sleeping=false;
        if(!preserveRoute)stopNativeTemporal(id);
        if(!preserveRoute)rememberAttackMove(id,unit.state==UState::AttackMoving);
        if(feedback)addOrderActor(result.actors,(int)unit.utype,(int)players[unit.player].faction);};
    auto reject = [&](EID id, MoveRejectReason reason) {
        ++result.rejected;
        if (result.reason == MoveRejectReason::None) { result.reason = reason; result.failedEntity = id; }
    };
    // Validate before converting floats to tile coordinates or repacking buildings.
    if (layer < -1 || layer>1 || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= map.w || y >= map.h) {
        for (EID id : sel) if (valid(id)) reject(id, MoveRejectReason::InvalidTarget);
        return result;
    }
    const bool directional=formation==MoveFormation::Directional && !append;
    // MCV Repacks：选中建造厂下达移动 → 先打包成基地车再寻路
    std::vector<EID> units;
    units.reserve(sel.size());
    std::vector<uint8_t> seenSelection(ents.size(), 0);
    for (EID id : sel) {
        if (!valid(id)) continue;
        if (seenSelection[id]) continue;
        seenSelection[id] = 1;
        Ent& e = ents[id];
        if(e.immobilized()) { reject(id,MoveRejectReason::Immobilized);continue; }
        if(e.deploying()) { reject(id,MoveRejectReason::Busy);continue; }
        if (e.isBuilding && e.btype == BldType::ConYard && mcvRepacks) {
            const int player=e.player;const float bx=e.x,by=e.y;
            const auto sourceGeneration=e.generation;
            EID mcv = packConYardToMcv(id);
            if (valid(mcv)) {
                units.push_back(mcv);
                DeployOrderResult packed;packed.accepted=packed.packed=1;
                packed.feedbackEntity=mcv;packed.x=bx;packed.y=by;
                packed.changes.push_back({id,mcv,false,sourceGeneration,ents[mcv].generation});
                emitDeployResult(player,packed);
            } else reject(id,MoveRejectReason::Unreachable);
            continue;
        }
        if (!e.isBuilding) {
            if(directional && (e.deployed || e.radDeployed))reject(id,MoveRejectReason::Deployed);
            else units.push_back(id);
        }
    }
    if(units.empty())return result;
    const FormationGrid grid((int)units.size());
    if(directional && units.size()>1 && repeatsFormation(*this,units,grid,{x,y},attackMove,layer)) {
        result.accepted=(int)units.size();
        for(EID id:units)actor(id,ents[id],true);
        return result;
    }
    const Vec2i forward=directional && units.size()>1?orientFormation(ents,units,x,y,grid.columns):Vec2i{0,-1};
    // Static connectivity is shared by the entire command, rather than trying
    // A* separately against many unreachable formation slots across a wall.
    // This is command-local: a newly placed/sold building cannot leave stale data.
    std::map<int,std::vector<int>> components;
    std::map<int,std::vector<std::vector<int>>> componentCells;
    struct Bounds {int minX=INT_MAX,minY=INT_MAX,maxX=-1,maxY=-1;};
    std::map<int,std::vector<Bounds>> componentBounds;
    const int area=(int)map.cells.size(),surfaceCount=map.hasBridgeDecks()?2:1;
    auto componentAt = [&](int domain,int x,int y,int surface,bool underBridge,uint8_t gateAccess) -> int {
        if(!map.inBounds(x,y) || surface<0 || surface>=surfaceCount)return -1;
        const int profile=domain+(underBridge?0:3)+(gateAccess<<3);auto& labels=components[profile];
        if(labels.empty()) {
            labels.assign((size_t)area*surfaceCount,-1);std::vector<int> frontier;frontier.reserve(labels.size());
            std::vector<size_t> regionSizes;int component=0;
            constexpr Vec2i steps[]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            for(int sl=0;sl<surfaceCount;++sl)for(int sy=0;sy<map.h;++sy)for(int sx=0;sx<map.w;++sx) {
                const int start=sl*area+cellIdx(sx,sy);
                if(labels[start]>=0 || !map.surfacePassable(sx,sy,domain,sl,true,underBridge,gateAccess))continue;
                labels[start]=component;frontier.clear();frontier.push_back(start);
                for(size_t head=0;head<frontier.size();++head) {
                    const int current=frontier[head],cl=current/area,cx=current%area%map.w,cy=current%area/map.w;
                    for(auto step:steps) {
                        const int nx=cx+step.x,ny=cy+step.y;
                        const int nextLayer=map.stepLayer(cx,cy,cl,nx,ny,domain,true,underBridge,gateAccess);if(nextLayer<0)continue;
                        const int next=nextLayer*area+cellIdx(nx,ny);if(labels[next]>=0)continue;
                        labels[next]=component;frontier.push_back(next);
                    }
                }
                regionSizes.push_back(frontier.size());++component;
            }
            // Build the same stable row index in linear order; sorting every
            // flood's discovery queue costs more on large disconnected maps.
            auto& regions=componentCells[profile];regions.resize(regionSizes.size());
            auto& bounds=componentBounds[profile];bounds.resize(regionSizes.size());
            for(size_t region=0;region<regions.size();++region)regions[region].reserve(regionSizes[region]);
            for(int node=0;node<(int)labels.size();++node)if(labels[node]>=0) {
                const int region=labels[node],x=node%map.w,y=node%area/map.w;regions[region].push_back(node);
                auto& box=bounds[region];box.minX=std::min(box.minX,x);box.maxX=std::max(box.maxX,x);
                box.minY=std::min(box.minY,y);box.maxY=std::max(box.maxY,y);
            }
        }
        return labels[surface*area+cellIdx(x,y)];
    };
    // Reserve both current occupants and allied destinations across commands.
    // Replacing a group's orders releases its previous destinations atomically.
    std::vector<uint8_t> selected(ents.size(),0), landing(movementSlotCount(),0);
    for(EID id:units) if(valid(id) && !ents[id].deployed && !ents[id].radDeployed
        && !(append && (ents[id].attackMove.active || ents[id].state==UState::Moving || ents[id].state==UState::AttackMoving))) selected[id]=1;
    auto reserve=[&](int x,int y,bool infantry,int surface) {
        if(!map.inBounds(x,y)) return;
        auto& use=landing[movementSlot(x,y,surface)];
        use=infantry?(uint8_t)std::min(3,(int)use+1):3;
    };
    for(size_t id=0;id<ents.size();++id) {
        const auto& other=ents[id];
        if(!other.alive || other.isBuilding || other.airborne() || other.magneticHeight>0 || other.parasiting || selected[id]) continue;
        reserve((int)other.x,(int)other.y,unitDef(other).isInfantry(),other.navLayer);
        if(other.pathIdx<(int)other.path.size()) {
            const auto goal=other.path.back();
            bool allied=false;
            for(EID mover:units) if(valid(mover) && isAllied(ents[mover].player,other.player)) { allied=true;break; }
            if(allied && (goal.x!=(int)other.x || goal.y!=(int)other.y))
                reserve(goal.x,goal.y,unitDef(other).isInfantry(),movementLayerAt(other,goal.x,goal.y));
        }
    }
    int n = 0;
    for (EID id : units) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e);
        if (e.deployed || e.radDeployed) { reject(id, MoveRejectReason::Deployed); continue; }
        float lx = x, ly = y;int requestedLayer=layer; // 本单元目标点（append 时可能被队首替换，不能污染共享参数）
        bool legAttack=attackMove;
        std::deque<Ent::Waypoint> pending;
        if (append) { // 路径点追加：入队；移动中则到位自动接续，空闲立即启程
            if(e.wps.size()>=4096){reject(id,MoveRejectReason::Busy);continue;}
            pending=e.wps;pending.emplace_back(lx,ly,requestedLayer,attackMove?1:0);
            if (e.attackMove.active || e.state == UState::Moving || e.state == UState::AttackMoving) {e.wps=std::move(pending);++result.queued;actor(id,e,true);continue;}
            const auto point=pending.front();pending.pop_front();
            lx=point.first;ly=point.second;requestedLayer=point.layer;if(point.attackMode>=0)legAttack=point.attackMode!=0;
        }
        const auto commit=[&](){e.driveArc={};e.cancelChronoReturn();e.nativeAirUnload=false;e.nativeAirLandingRetry=0;e.wps=std::move(pending);e.cancelBurst();e.target=INVALID_EID;
            e.engineerIntent=EngineerIntent::None;e.feedbackEngineerTarget=INVALID_EID;e.guard=false;
            if(ud.canHarvet()){e.autoHarvest=false;if(!legAttack)e.oreCell={-1,-1};}};
        const auto slot=grid.position(n++,forward,lx,ly);
        float ox=slot.x,oy=slot.y;
        if (ud.isAir()) {
            // 战机：直线飞行，无视地形
            commit();e.goalLayer=0;
            e.goalX = std::clamp(ox, 0.5f, map.w - 0.5f);
            e.goalY = std::clamp(oy, 0.5f, map.h - 0.5f);
            e.orbitA = (float)(id % 8) * 0.785f;
            e.state = legAttack ? UState::AttackMoving : UState::Moving;
            unitOccSync(id);airOccSync(id);
            ++result.accepted;
            actor(id,e);
            continue;
        }
        if (e.utype == UnitType::Chrono || e.utype == UnitType::ChronoCommando
            || e.utype == UnitType::ChronoIvan) {
            // 超时空系：传送移动（RA2/YR：军团兵、突击队、超时空伊文）
            if (chronoJump(id, ox, oy)) {commit();e.goalX=e.x;e.goalY=e.y;e.guard=legAttack;++result.accepted;actor(id,e);}
            else reject(id, MoveRejectReason::Unreachable);
            continue;
        }
        std::vector<Vec2i> path;
        const int nominalX=(int)ox,nominalY=(int)oy;
        const int preferredX=std::clamp(nominalX,0,map.w-1),preferredY=std::clamp(nominalY,0,map.h-1);
        const int domain=ud.pathDomain();
        const bool underBridge=ud.fitsUnderBridge();const uint8_t gateAccess=nativeGateAccess(e);
        const int profile=domain+(underBridge?0:3)+(gateAccess<<3);
        auto targetLayer=[&](int tx,int ty){return domain==1?0:requestedLayer<0?map.preferredLayer(tx,ty,domain):requestedLayer;};
        Vec2i connectivityStart{(int)e.x,(int)e.y};
        // A Chrono Miner can occupy the berth while still aligning.
        const auto firstBerthPhase=e.utype==UnitType::ChronoMiner?NativeDockPhase::Align:NativeDockPhase::Enter;
        if (nativeTravelEnabled() && e.nativeDock.phase >= firstBerthPhase
            && valid(e.nativeDock.receiver)) {
            const auto& refinery = ents[e.nativeDock.receiver.id];
            // The apron connects the occupied berth to the ordinary movement graph.
            if (e.navLayer == refinery.navLayer && connectivityStart == Vec2i{(int)refinery.x + 3, (int)refinery.y + 1})
                ++connectivityStart.x;
        }
        const auto findOriginComponent = [&] {
            return componentAt(domain, connectivityStart.x, connectivityStart.y, e.navLayer, underBridge, gateAccess);
        };
        int originComponent=components[profile].empty()?-2:findOriginComponent();
        auto available=[&](int tx,int ty) {
            return map.inBounds(tx,ty) && map.surfacePassable(tx,ty,domain,targetLayer(tx,ty),true,underBridge,gateAccess)
                && (originComponent==-2 || (originComponent>=0 && componentAt(domain,tx,ty,targetLayer(tx,ty),underBridge,gateAccess)==originComponent))
                && landing[movementSlot(tx,ty,targetLayer(tx,ty))]<(ud.isInfantry()?3:1);
        };
        bool found=false;
        bool localPossible=originComponent==-2;
        if(originComponent>=0) {
            const auto& bounds=componentBounds[profile][originComponent];
            localPossible=preferredX+8>=bounds.minX && preferredX-8<=bounds.maxX
                && preferredY+8>=bounds.minY && preferredY-8<=bounds.maxY;
        }
        // A disconnected component whose bounding box misses the whole local
        // search square cannot contribute a candidate in any of its rings.
        for(int radius=0;localPossible && radius<=8 && !found;++radius)
            for(int dy=-radius;dy<=radius && !found;++dy) for(int dx=-radius;dx<=radius;++dx) {
                if(std::max(std::abs(dx),std::abs(dy))!=radius) continue;
                int tx=preferredX+dx,ty=preferredY+dy;
                if(!available(tx,ty)) continue;
                ox=(float)tx;oy=(float)ty;found=true;break;
            }
        const auto route=[&](int tx,int ty) {
            if(e.navLayer==0 && targetLayer(tx,ty)==0 && directOrderPath(map,(int)e.x,(int)e.y,tx,ty,domain,underBridge,gateAccess,path))return true;
            // For a large group, prove reachability once before spending an
            // entire A* budget on the first disconnected destination. Clear
            // orders retain the linear route above without paying for a flood.
            if(units.size()>=64 && originComponent==-2)
                originComponent=findOriginComponent();
            if(originComponent>=0 && componentAt(domain,tx,ty,targetLayer(tx,ty),underBridge,gateAccess)!=originComponent)return false;
            return map.findPath((int)e.x,(int)e.y,tx,ty,path,20000,domain,nullptr,e.navLayer,targetLayer(tx,ty),underBridge,gateAccess);
        };
        bool pathFound=found && route((int)ox,(int)oy);
        if (!pathFound) {
            // Ordinary commands keep the cheap A* fast path. Build connectivity
            // only after a failure, then share it with the rest of this group.
            originComponent=findOriginComponent();
            // A distant or disconnected target still has one clear outcome:
            // the closest free tile on this unit's reachable side of the map.
            int best=-1;int bestDistance=INT_MAX;
            if(originComponent>=0) {
                // Shared row index: search the nearest free x in each relevant
                // row, instead of scanning every map cell for every soldier.
                // Distances and row-major ties match the original selection.
                const auto& cells=componentCells[profile][originComponent];
                auto searchRow=[&](int surface,int y) {
                    const int dy=y-preferredY;if(y<0 || y>=map.h || dy*dy>bestDistance)return;
                    const int row=surface*area+y*map.w;
                    const auto begin=std::lower_bound(cells.begin(),cells.end(),row);
                    const auto end=std::lower_bound(begin,cells.end(),row+map.w);
                    auto right=std::lower_bound(begin,end,row+preferredX),left=right;
                    while(left!=begin || right!=end) {
                        const bool useLeft=left!=begin && (right==end || preferredX-(*(left-1)%map.w)<=*right%map.w-preferredX);
                        const int node=useLeft?*--left:*right++,x=node%map.w,dx=x-preferredX;
                        const int distance=dx*dx+dy*dy;if(distance>bestDistance)break;
                        if(targetLayer(x,y)!=surface || !available(x,y))continue;
                        if(distance<bestDistance || (distance==bestDistance && (best<0 || node<best))) {best=node;bestDistance=distance;}
                        break;
                    }
                };
                for(int surface=0;surface<surfaceCount;++surface) {
                    const auto first=std::lower_bound(cells.begin(),cells.end(),surface*area);
                    const auto last=std::lower_bound(first,cells.end(),(surface+1)*area);if(first==last)continue;
                    const int firstRow=*first%area/map.w,lastRow=*(last-1)%area/map.w,startRow=std::clamp(preferredY,firstRow,lastRow);
                    searchRow(surface,startRow);
                    for(int offset=1;startRow-offset>=firstRow || startRow+offset<=lastRow;++offset) {
                        const int distance=offset-std::abs(startRow-preferredY);if(distance>0 && distance*distance>bestDistance)break;
                        if(startRow-offset>=firstRow)searchRow(surface,startRow-offset);
                        if(startRow+offset<=lastRow)searchRow(surface,startRow+offset);
                    }
                }
            }
            if(best>=0) {
                ox=(float)(best%map.w);oy=(float)(best%area/map.w);
                pathFound=route((int)ox,(int)oy);
            }
        }
        if (pathFound) {
            const auto curve=e.driveArc;
            const bool activeCurve=driveArcMatchesRoute(e);
            const int curveIndex=activeCurve ? prependDriveArcCorner(*this,e,curve,path) : -1;
            if(activeCurve) {
                if(curveIndex<0 && !path.empty() && reversesDriveArc(e,curve,path.front())) releaseReversingLane(id);
            } else if (directional && !path.empty() && usesDriveArc(e)
                    && (e.state == UState::Moving || e.state == UState::AttackMoving)
                    && (size_t)e.pathIdx < e.path.size()) {
                // The old waypoint can already be in the occupied cell, so
                // use its direction from the hull's actual sub-cell position.
                const auto ahead = e.path[e.pathIdx];
                const Vec2f oldLeg{ahead.x + .5f - e.x, ahead.y + .5f - e.y};
                const Vec2i newStep{path.front().x - (int)e.x, path.front().y - (int)e.y};
                const float projection = oldLeg.x * newStep.x + oldLeg.y * newStep.y;
                const float lateral = oldLeg.x * newStep.y - oldLeg.y * newStep.x;
                if (projection < 0.f && std::abs(lateral) <= -.001f * projection)
                    releaseReversingLane(id);
            }
            commit();
            if(curveIndex>=0)e.driveArc=curve;
            const auto endpoint=path.empty()?Vec2i{(int)e.x,(int)e.y}:path.back();
            if(endpoint.x!=nominalX || endpoint.y!=nominalY) ++result.adjusted;
            e.goalLayer=(uint8_t)targetLayer(endpoint.x,endpoint.y);
            reserve(endpoint.x,endpoint.y,ud.isInfantry(),e.goalLayer);
            e.path = std::move(path);
            e.pathIdx = std::max(0,curveIndex);
            e.blockTick = 0;
            e.goalX = ox + 0.5f; e.goalY = oy + 0.5f;
            // A cell route can be empty while the actor is still off-center.
            // Keep the final physical leg so same-cell commands and queued
            // waypoints use the ordinary speed, collision and arrival checks.
            if(e.path.empty() && e.navLayer==e.goalLayer && distf(e.x,e.y,e.goalX,e.goalY)>=.05f)
                e.path.push_back(endpoint);
            e.state = e.path.empty() ? UState::Idle : (legAttack ? UState::AttackMoving : UState::Moving);
            ++result.accepted;
            actor(id,e);
        } else {
            reject(id, MoveRejectReason::Unreachable);
        }
    }
    return result;
}

AttackRejectReason World::attackRejection(EID attacker,EID target) const {
    if(!valid(attacker) || !valid(target) || attacker==target) return AttackRejectReason::InvalidTarget;
    const auto& e=ents[attacker];const auto& t=ents[target];
    if(e.nativeTemporalHead!=INVALID_EID)return AttackRejectReason::Busy;
    if(e.supportAir || (e.nativeIdentity.definition && e.nativeIdentity.definition->nativeRocket))return AttackRejectReason::Unsupported;
    if(e.deploying())return AttackRejectReason::Busy;
    const auto healingWeapon=effWeapon(e,&t);
    if(healingWeapon.nativeSource && healingWeapon.damage<0)return nativeRepairRejection(attacker,target,healingWeapon);
    // Normal attack orders must stop after capture or mind control changes sides.
    // Neutral destructibles remain valid; force-fire is a separate, unimplemented intent.
    if(e.confused<=0 && t.player>=0 && !isEnemy(e.player,t.player)) return AttackRejectReason::InvalidTarget;
    if(e.isBuilding) return AttackRejectReason::NoWeapon;
    const auto& ud=unitDef(e);
    if(e.utype==UnitType::Spy) {
        if(t.isBuilding && nativeProductionRules.version>=3
            && (!t.nativeIdentity.definition || !t.nativeIdentity.definition->production.spyable))return AttackRejectReason::Unsupported;
        return isEnemy(e.player,t.player) && (t.isBuilding || unitDef(t).isInfantry())
            ?AttackRejectReason::None:AttackRejectReason::Unsupported;
    }
    const auto ew=effWeapon(e,&t);
    if(t.nativeTemporalHead!=INVALID_EID && !nativeTemporalWeapon(ew))return AttackRejectReason::Ineffective;
    if(nativeTemporalWeapon(ew) && !nativeTemporalCanTarget(attacker,target))return AttackRejectReason::Ineffective;
    if(ew.damage<=0) return AttackRejectReason::NoWeapon;
    bool air=t.airborne();
    if(air && !ew.antiAir) return AttackRejectReason::NoAntiAir;
    if(!air && !ew.antiGround) return AttackRejectReason::NoAntiGround;
    if(e.utype==UnitType::Magnetron && !t.isBuilding && unitDef(t).isInfantry()) return AttackRejectReason::Unsupported;
    if(e.utype==UnitType::AttackDog && (t.isBuilding || !unitDef(t).isInfantry())) return AttackRejectReason::Ineffective;
    float tx=t.x,ty=t.y;
    if(t.isBuilding) { tx+=bldDef(t).w/2.0f;ty+=bldDef(t).h/2.0f; }
    bool water=map.inBounds((int)tx,(int)ty) && map.at((int)tx,(int)ty).terrain==Terrain::Water;
    if(ew.navalOnly && !water) return AttackRejectReason::NavalOnly;
    const bool psychic=ud.isPsychic() || e.utype==UnitType::MasterMind;
    // Native Immune blocks damage, including C4. Effects that bypass damage
    // (control, erasure and lifting vehicles) still use their own eligibility.
    const bool effect=psychic || nativeTemporalWeapon(ew) || !strcmp(ew.projSprite?ew.projSprite:"","chrono")
        || (e.utype==UnitType::Magnetron && !t.isBuilding);
    if(t.nativeIdentity.definition && t.nativeIdentity.definition->immune && !effect) return AttackRejectReason::Ineffective;
    if(psychic) {
        if(!isEnemy(e.player,t.player)) return AttackRejectReason::Unsupported;
        if(t.isBuilding) {
            if(e.utype!=UnitType::YuriPrime || t.btype==BldType::ConYard) return AttackRejectReason::Unsupported;
        } else if(psychicImmune(t.utype) || t.permaControlled || t.mindBy!=INVALID_EID) return AttackRejectReason::Unsupported;
    } else if(!(ud.hasC4() && t.isBuilding) && e.utype!=UnitType::AircraftCarrier
        && strcmp(ew.projSprite?ew.projSprite:"","chrono") && weaponMultiplier(ew,t)<=0) return AttackRejectReason::Ineffective;
    if(e.radDeployed || (e.deployed && distf(e.x,e.y,tx,ty)>ew.range)) return AttackRejectReason::Deployed;
    return AttackRejectReason::None;
}

void World::emitAttackResult(int player,EID target,const AttackOrderResult& result) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.tick=tick;event.sequence=presentationSequence++;
    event.kind=PresentationKind::AttackOrder;event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:target;event.attack=result;
    if(valid(result.failedEntity) && nativeRepairMode(ents[result.failedEntity]))event.attack.healing=true;
    if(valid(target)) {
        const auto& t=ents[target];event.positional=true;event.x=t.x;event.y=t.y;
        if(t.isBuilding) { event.x+=bldDef(t).w/2.0f;event.y+=bldDef(t).h/2.0f; }
        captureVisualTarget(event,t);
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}

AttackOrderResult World::orderAttack(const std::vector<EID>& sel,EID target,bool feedback) {
    AttackOrderResult result;
    result.healing=nativeRepairIntent(sel,target);
    auto actor=[&](EID id,Ent& unit){
        unit.nativeAirUnload=false;unit.nativeAirLandingRetry=0;unit.attackMove={};result.acceptedUnits.push_back(id);
        // A parked aircraft becomes airborne as soon as this order changes its
        // state. AI issues orders after update(), so synchronize before a save
        // or another house queries the new cell-object/air-bucket membership.
        if(nativeCellOrderEnabled()){unitOccSync(id);airOccSync(id);}
        if(feedback)addOrderActor(result.actors,(int)unit.utype,(int)players[unit.player].faction);
    };
    auto reject=[&](EID id,AttackRejectReason reason) {
        ++result.rejected;
        if(result.reason==AttackRejectReason::None) { result.reason=reason;result.failedEntity=id; }
    };
    for(EID id:sel) {
        if(!valid(id)) continue;
        auto reason=attackRejection(id,target);
        if(reason!=AttackRejectReason::None) { reject(id,reason);continue; }
        auto& e=ents[id];const auto& t=ents[target];const auto& ud=unitDef(e);
        float tx=t.x,ty=t.y;
        if(t.isBuilding) { tx+=bldDef(t).w/2.0f;ty+=bldDef(t).h/2.0f; }
        if(e.utype==UnitType::Spy && !t.isBuilding) {
            e.camouflaged=true;
            int color=t.player>=0 && t.player<numPlayers?players[t.player].colorId:0;
            e.camoTick=(int)t.utype|((color&0xFF)<<16);
            e.target=INVALID_EID;e.clearPath();e.wps.clear();e.state=UState::Idle;
            e.feedbackAttackTarget=INVALID_EID;emitSoundAt(Sfx::Click,e.x,e.y);++result.accepted;actor(id,e);continue;
        }
        const auto weapon=effWeapon(e,&t);
        attackTargetPoint(e,t,tx,ty);
        float range=attackRange(e,t);
        bool inRange=e.utype!=UnitType::Spy && distf(e.x,e.y,tx,ty)<=range;
        std::vector<Vec2i> path;
        if(!inRange && !attackPath(*this,e,t,path)) { reject(id,AttackRejectReason::Unreachable);continue; }
        e.cancelBurst();
        if(e.target!=target){stopNativeTemporal(id);stopNativeCrewTemporal(id);
        std::erase_if(nativeCrewBursts,[&](const auto& node){return node.second.carrier.id==id;});}
        e.driveArc={};e.wps.clear();e.target=target;e.guard=false;e.path=std::move(path);e.pathIdx=0;e.blockTick=0;
        e.engineerIntent=EngineerIntent::None;e.feedbackEngineerTarget=INVALID_EID;
        e.state=inRange?UState::Attacking:UState::Chasing;
        e.feedbackAttackTarget=feedback?target:INVALID_EID;
        ++result.accepted;
        actor(id,e);
    }
    return result;
}

std::vector<OrderActor> World::orderHarvest(const std::vector<EID>& sel, int x, int y, bool feedback) {
    std::vector<OrderActor> actors;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (!unitDef(e).canHarvet()) continue;
        e.attackMove={};
        e.wps.clear();
        e.oreCell = {x, y};
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        e.autoHarvest = true; // 显式采矿命令恢复自动寻矿
        e.cancelChronoReturn();
        std::vector<Vec2i> path;
        map.findPath((int)e.x,(int)e.y,x,y,path,20000,0,nullptr,e.navLayer,-1,unitDef(e).fitsUnderBridge(),nativeGateAccess(e));
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::HarvestGo;
        if(feedback)addOrderActor(actors,(int)e.utype,(int)players[e.player].faction);
    }
    return actors;
}

void World::orderReturnToRefinery(const std::vector<EID>& sel, EID preferredRef) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (!unitDef(e).canHarvet()) continue;
        if (e.utype == UnitType::SlaveMiner && e.deployed) continue;
        e.attackMove={};
        e.wps.clear();
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        e.autoHarvest = true; // 显式回厂后卸完可再出发
        // A repeated click confirms either the queued destination or the current reservation.
        if (continuesNativeDock(e, preferredRef)) {
            if (e.chronoReturn.phase != ChronoReturnPhase::None) e.chronoReturn.preferReceiver = true;
            continue;
        }
        const auto requested = entityRef(preferredRef);
        e.pendingDockRefinery = e.utype == UnitType::WarMiner && usesNativeGround(e)
            && ownsNativeDockRefinery(e, requested) ? requested : EntityRef{};
        beginHarvesterReturn(e, id, preferredRef);
        unitOccSync(id);airOccSync(id);
    }
}

void World::orderStop(const std::vector<EID>& sel,bool cancelDriveFacing) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if(e.nativeTemporalHead!=INVALID_EID)continue;
        if(e.deploying())continue;
        stopNativeTemporal(id);stopNativeCrewTemporal(id);
        std::erase_if(nativeCrewBursts,[&](const auto& node){return node.second.carrier.id==id;});
        e.nativeAirUnload=false;e.nativeAirLandingRetry=0;
        stopAircraft(id);
        e.attackMove={};
        if(e.nativeDock.phase==NativeDockPhase::DriveExit)e.cancelNativeDockTiming();
        e.nativeMovement={};
        e.cancelChronoReturn();
        e.pendingDockRefinery = {};
        // 乌贼主动松开缠绕（RA2 原作：可命令乌贼撤退）
        if (e.parasiting && e.utype == UnitType::Squid) {
            if (valid(e.parasiteHost)) ents[e.parasiteHost].parasite = INVALID_EID;
            e.parasiting = false;
            e.parasiteHost = INVALID_EID;
        }
        e.clearPath();
        if (cancelDriveFacing && e.nativeGround.initialized && usesDriveArc(e)) {
            desireNativeHull(e, nativeHullHeading(e, tick));
            desireNativeTurret(e, nativeTurretHeading(e, tick));
        }
        e.wps.clear(); // 停止清空路径点队列
        e.cancelBurst();
        e.target = INVALID_EID;
        e.engineerIntent=EngineerIntent::None;e.feedbackEngineerTarget=INVALID_EID;
        e.guard = false;
        if (unitDef(e).canHarvet())
            e.autoHarvest = false; // Stop 后暂停自动寻矿，直到再次下达采矿命令
        if (unitDef(e).isAir()) {
            // 战机：停机中的保持停机，飞行中的改为原地盘旋
            if (e.state != UState::Landed) {
                e.goalX = e.x; e.goalY = e.y;
                e.state = UState::Circling;
            }
        } else {
            e.state = UState::Idle;
        }
        if(nativeCellOrderEnabled()){unitOccSync(id);airOccSync(id);}
    }
}

// X 散布：各自前往周围随机空格（规避范围伤害与碾压）
std::vector<int> World::orderScatter(const std::vector<EID>& sel) {
    std::vector<int> accepted;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e);
        if (ud.isAir()) continue; // 战机不散布
        int dom = ud.pathDomain();
        // 在 2~4 格内找随机可通行落点
        for (int tries = 0; tries < 8; tries++) {
            int dx = rng.range(-4, 4), dy = rng.range(-4, 4);
            if (abs(dx) < 2 && abs(dy) < 2) continue;
            int nx = (int)e.x + dx, ny = (int)e.y + dy;
            const int layer=map.preferredLayer(nx,ny,dom);
            if (!(map.hasBridgeDecks()?movementPassable(e,nx,ny,layer):passableStep((int)e.x,(int)e.y,nx,ny,dom))
                || bldBlocked(nx,ny,layer) || unitAtCell(nx,ny,layer)!=INVALID_EID)continue;
            std::vector<Vec2i> path;
            if (map.findPath((int)e.x, (int)e.y, nx, ny, path,20000,dom,nullptr,e.navLayer,layer,ud.fitsUnderBridge(),nativeGateAccess(e))) {
                e.wps.clear();e.cancelBurst();e.target=INVALID_EID;
                e.engineerIntent=EngineerIntent::None;e.feedbackEngineerTarget=INVALID_EID;e.guard=false;
                e.path = std::move(path);
                e.pathIdx = 0;
                e.state = UState::Moving;
                e.goalX = nx + 0.5f; e.goalY = ny + 0.5f;
                e.goalLayer=(uint8_t)layer;
                e.attackMove={};
                accepted.push_back(id);
                break;
            }
        }
    }
    return accepted;
}

// G 警戒：驻守原地，按视野半径索敌（比普通 Idle 的射程+2 更远）
void World::orderGuard(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (unitDef(e).weapon.damage == 0) continue;
        e.nativeAirUnload=false;e.nativeAirLandingRetry=0;
        e.clearPath();
        e.wps.clear();
        e.target = INVALID_EID;
        e.guard = true;
        e.attackMove={};
        if (!unitDef(e).isAir()) e.state = UState::Idle;
    }
}

int World::buildingEntryDomain(const Ent& building) const {
    if(!building.isBuilding)return 0;
    if(nativeProductionRules.version>=3 && building.nativeIdentity.definition) {
        const auto& profile=building.nativeIdentity.definition->production;
        if(profile.grinding)return 3;
        if(profile.bunker)return 2;
        if(profile.infantryAbsorb || profile.unitAbsorb)return profile.infantryAbsorb?(profile.unitAbsorb?4:1):2;
        return bldDef(building).garrisonCap>0?1:0;
    }
    return garrisonDomain(building.btype);
}

EntryRejectReason World::entryRejection(EID unit,EID target,EntryKind kind) const {
    if(!valid(target) || (kind==EntryKind::Garrison)!=ents[target].isBuilding) return EntryRejectReason::InvalidTarget;
    if(!valid(unit) || unit==target || ents[unit].isBuilding) return EntryRejectReason::Incompatible;
    const auto& actor=ents[unit];const auto& destination=ents[target];const auto& ud=unitDef(actor);
    if(actor.nativeTemporalHead!=INVALID_EID || destination.nativeTemporalHead!=INVALID_EID)return EntryRejectReason::Incompatible;
    if(kind==EntryKind::Board) {
        const auto& td=unitDef(destination);
        if(td.cargoCap<=0 || destination.utype==UnitType::AircraftCarrier) return EntryRejectReason::Unsupported;
        if(destination.player!=actor.player) return EntryRejectReason::WrongOwner;
        if(ud.isAir() || ud.isNaval() || ud.transportSize()>td.cargoSizeLimit
            || (!ud.isInfantry() && destination.utype!=UnitType::AmphTransport)) return EntryRejectReason::Incompatible;
        // Entity-linked effects cannot be converted to an anonymous cargo record.
        if(actor.mindBy!=INVALID_EID || actor.mindTarget!=INVALID_EID || !actor.mindTargets.empty()
            || actor.parasite!=INVALID_EID || actor.parasiteHost!=INVALID_EID || actor.parasiting)
            return EntryRejectReason::Incompatible;
        if(cargoLoad(destination.cargo)+ud.transportSize()>td.cargoCap) return EntryRejectReason::Full;
    } else {
        const auto& bd=bldDef(destination);const int domain=buildingEntryDomain(destination);
        const auto* profile=nativeProductionRules.version>=3 && destination.nativeIdentity.definition?&destination.nativeIdentity.definition->production:nullptr;
        if(!domain || (bd.garrisonCap<=0 && !(domain==3 && profile)))return EntryRejectReason::Unsupported;
        if(destination.player>=0 && destination.player!=actor.player) return EntryRejectReason::WrongOwner;
        const bool absorber=profile && (profile->infantryAbsorb || profile->unitAbsorb);
        if(nativeCrewRules.enabled && domain==1 && !absorber && (!actor.nativeIdentity.definition
            || !actor.nativeIdentity.definition->crew.occupier))return EntryRejectReason::Incompatible;
        if(absorber && destination.player!=actor.player)return EntryRejectReason::WrongOwner;
        const bool fit=absorber?((profile->infantryAbsorb && ud.isInfantry()) || (profile->unitAbsorb && !ud.isInfantry() && !ud.isAir())):
              (domain==1 && ud.isInfantry())
            || (domain==2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain()==0 && !ud.canHarvet())
            || (domain==3 && destination.player==actor.player && !ud.isAir() && (profile || !isHero(actor.utype)));
        if(!fit || (destination.btype==BldType::CivHouse && !canGarrisonCivHouse(actor.utype))) return EntryRejectReason::Incompatible;
        if(profile && (absorber || profile->bunker) && profile->sizeLimit>0 && ud.transportSize()>profile->sizeLimit)return EntryRejectReason::Incompatible;
        if(domain!=3 && (int)destination.garrison.size()>=bd.garrisonCap) return EntryRejectReason::Full;
    }
    if(actor.deployed || actor.radDeployed) return EntryRejectReason::Deployed;
    return EntryRejectReason::None;
}

float World::entryReach(const Ent& target) const {
    return target.isBuilding?std::max(bldDef(target).w,bldDef(target).h)/2.0f+1.5f:1.6f;
}

bool World::entryPath(const Ent& unit,const Ent& target,std::vector<Vec2i>& path) const {
    return interactionPath(unit,target,entryReach(target),path);
}

bool World::interactionPath(const Ent& unit,const Ent& target,float reach,std::vector<Vec2i>& path) const {
    float tx=target.x,ty=target.y;
    if(target.isBuilding) { tx+=bldDef(target).w/2.0f;ty+=bldDef(target).h/2.0f; }
    path.clear();
    if(interactionWithinReach(unit,target,reach)) return true;
    std::vector<uint8_t> costs(movementSlotCount(),0);
    std::vector<uint8_t> fixedInfantry(movementSlotCount(),0);
    for(const auto& other:ents) if(&other!=&unit && other.alive && !other.isBuilding && other.occCell>=0 && map.inBounds((int)other.x,(int)other.y)) {
        const size_t cell=movementSlot((int)other.x,(int)other.y,other.navLayer);
        costs[cell]=std::max<uint8_t>(costs[cell],16);
        if(other.deployed || other.radDeployed) {
            if(!unitDef(unit).isInfantry() || !unitDef(other).isInfantry() || ++fixedInfantry[cell]>=3) costs[cell]=255;
        }
    }
    const int height=(map.hasBridgeDecks() || usesNativeAirNavigation(target)) && !target.airborne()?actorSurfaceHeight(target):-1;
    bool found=map.findPathToRange((int)unit.x,(int)unit.y,tx,ty,reach-0.001f,path,20000,unitDef(unit).pathDomain(),&costs,unit.navLayer,-1,height,unitDef(unit).fitsUnderBridge(),nativeGateAccess(unit));
    if(found && path.empty()) path.push_back({(int)unit.x,(int)unit.y});
    return found;
}

void World::emitEntryResult(int player,EID target,EntryKind kind,const EntryOrderResult& result) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::EntryOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:target;event.entry=result;event.entryKind=kind;
    if(valid(target)) {
        const auto& destination=ents[target];event.positional=true;event.x=destination.x;event.y=destination.y;
        if(destination.isBuilding) { event.x+=bldDef(destination).w/2.0f;event.y+=bldDef(destination).h/2.0f; }
        captureVisualTarget(event,destination);
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}

void World::endEntry(Ent& unit,EID id,EntryRejectReason reason) {
    if(unit.feedbackEntryTarget!=INVALID_EID && (unit.feedbackEntryTarget==unit.target || unit.target==INVALID_EID))
        emitEntryResult(unit.player,unit.target,unit.feedbackEntryKind,{0,1,id,reason});
    unit.feedbackEntryTarget=INVALID_EID;unit.target=INVALID_EID;unit.clearPath();unit.wps.clear();unit.blockTick=0;unit.state=UState::Idle;
}

EntryOrderResult World::orderEntry(const std::vector<EID>& sel,EID target,EntryKind kind,bool feedback) {
    EntryOrderResult result;
    for(EID id:sel) {
        if(!valid(id)) continue;
        auto reason=entryRejection(id,target,kind);std::vector<Vec2i> path;
        if(reason==EntryRejectReason::None && !entryPath(ents[id],ents[target],path)) reason=EntryRejectReason::Unreachable;
        if(reason!=EntryRejectReason::None) {
            ++result.rejected;
            if(result.reason==EntryRejectReason::None) { result.reason=reason;result.failedEntity=id; }
            continue;
        }
        auto& actor=ents[id];actor.target=target;actor.guard=false;actor.path=std::move(path);actor.pathIdx=0;
        actor.wps.clear();actor.attackMove={};actor.blockTick=0;actor.state=UState::Boarding;
        actor.engineerIntent=EngineerIntent::None;actor.feedbackEngineerTarget=INVALID_EID;actor.feedbackAttackTarget=INVALID_EID;
        actor.feedbackEntryTarget=feedback?target:INVALID_EID;actor.feedbackEntryKind=kind;++result.accepted;
    }
    if(result.accepted && kind==EntryKind::Board && valid(target) && usesNativeAirNavigation(ents[target]))orderStop({target});
    return result;
}

EntryOrderResult World::orderBoard(const std::vector<EID>& sel,EID target,bool feedback) {
    return orderEntry(sel,target,EntryKind::Board,feedback);
}

ReleaseOrderResult World::releaseContainer(EID id,ReleaseKind kind) {
    ReleaseOrderResult result;
    auto reject=[&](ReleaseRejectReason reason) { result.rejected=1;result.failedEntity=id;result.reason=reason; };
    if(!valid(id)) { reject(ReleaseRejectReason::InvalidTarget);return result; }
    const bool building=kind==ReleaseKind::Ungarrison;
    if(ents[id].isBuilding!=building || (building?bldDef(ents[id]).garrisonCap<=0:
        (unitDef(ents[id]).cargoCap<=0 || ents[id].utype==UnitType::AircraftCarrier))) {
        reject(ReleaseRejectReason::Unsupported);return result;
    }
    auto out=building?ents[id].garrison:ents[id].cargo;
    if(out.empty()) { reject(ReleaseRejectReason::Empty);return result; }
    if(ents[id].immobilized()) { result.remaining=(int)out.size();reject(ReleaseRejectReason::Immobilized);return result; }
    if(!building && usesNativeAirNavigation(ents[id])) {
        if(ents[id].nativeAirUnload){result.remaining=result.pending=(int)out.size();return result;}
        orderStop({id});
        if(ents[id].airborne() || ents[id].nativeJumpjet.phase!=NativeJumpjetPhase::Ground){
            ents[id].nativeAirUnload=true;result.remaining=result.pending=(int)out.size();return result;
        }
    }
    const int owner=ents[id].player;
    std::vector<EID> entered;
    while(!out.empty()) {
        const auto saved=out.back();auto cells=releaseCells(ents[id],unitDef(saved),building?3:2);
        if(cells.empty()) break;
        const auto cell=cells.front();
        EID unit=saved.nativeIdentity.definition?spawnNativeActor(saved.nativeIdentity.type,owner,cell.x+0.5f,cell.y+0.5f,256,0,
            saved.nativeIdentity.object,saved.nativeIdentity.tag,saved.nativeIdentity.sleeping?"Sleep":"Guard"):
            spawnUnit(owner,saved.type,cell.x+0.5f,cell.y+0.5f);
        if(!valid(unit)) break;
        auto& restored=ents[unit];
        restored.navLayer=restored.previousNavLayer=restored.goalLayer=(uint8_t)cell.layer;unitOccSync(unit);
        ActorStateTransfer::restore(restored,saved);
        restored.vetRank=std::clamp(saved.vetRank,0,2);
        restored.hp=std::clamp(saved.hp,1,maxHpFor(restored,unitDef(saved)));
        entered.push_back(unit);
        if(saved.combatActor){stopNativeTemporal(nativeCrewTemporalKey(saved.combatActor));nativeCrewBursts.erase(saved.combatActor);}
        out.pop_back();++result.released;
    }
    result.remaining=(int)out.size();
    if(result.remaining) reject(ReleaseRejectReason::Blocked);
    if(building) {
        ents[id].garrison=std::move(out);
        if(result.released && !result.remaining && ents[id].btype==BldType::CivHouse) changeActorOwner(id,-1);
        if(result.released) recomputePower();
    } else {ents[id].cargo=std::move(out);if(result.released)nativeIFVCrewChanged(id);}
    if(!building && result.remaining && usesNativeAirNavigation(ents[id])){
        ents[id].nativeAirUnload=true;ents[id].nativeAirLandingRetry=tick+30;
        result.pending=result.remaining;result.rejected=0;
    }
    for(EID unit:entered)if(valid(unit))offerNativeCellEntry(unit,(int)ents[unit].x,(int)ents[unit].y);
    return result;
}

void World::emitReleaseResult(int player,EID source,ReleaseKind kind,const ReleaseOrderResult& result) {
    if(!result.released && !result.rejected && !result.pending) return;
    PresentationEvent event;event.kind=PresentationKind::ReleaseOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:source;event.release=result;event.releaseKind=kind;
    EID marker=result.failedEntity>=0?result.failedEntity:source;
    if(valid(marker)) {
        const auto& container=ents[marker];event.positional=true;event.x=container.x;event.y=container.y;
        if(container.isBuilding) { event.x+=bldDef(container).w/2.0f;event.y+=bldDef(container).h/2.0f; }
        captureVisualTarget(event,container);
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}

ReleaseOrderResult World::orderRelease(const std::vector<EID>& sel,ReleaseKind kind,bool feedback) {
    ReleaseOrderResult result;
    std::vector<uint8_t> seen(ents.size(),0);
    for(EID id:sel) {
        if(id<0 || id>=(EID)seen.size() || seen[id] || !valid(id)) continue;
        seen[id]=1;
        int owner=ents[id].player;auto one=releaseContainer(id,kind);
        result.released+=one.released;result.remaining+=one.remaining;result.rejected+=one.rejected;
        result.pending+=one.pending;
        if(result.reason==ReleaseRejectReason::None && one.reason!=ReleaseRejectReason::None) { result.reason=one.reason;result.failedEntity=id; }
        if(!feedback && owner>=0) {
            if(one.released && !one.remaining) eva(owner,TR(S::EvaUnloadDone));
            else if(kind==ReleaseKind::Unload && one.remaining && !one.pending) eva(owner,TR(S::EvaUnloadFail));
        }
    }
    return result;
}

ReleaseOrderResult World::orderUnload(const std::vector<EID>& sel,bool feedback) { return orderRelease(sel,ReleaseKind::Unload,feedback); }

// 步兵/车辆进驻建筑（RA2 原作：民房/战斗碉堡进驻步兵，坦克碉堡进驻车辆，驻军后从内部向外射击）
EntryOrderResult World::orderGarrison(const std::vector<EID>& sel,EID target,bool feedback) {
    return orderEntry(sel,target,EntryKind::Garrison,feedback);
}

ReleaseOrderResult World::evacuateGarrison(EID id) { return releaseContainer(id,ReleaseKind::Ungarrison); }
ReleaseOrderResult World::orderUngarrison(const std::vector<EID>& sel,bool feedback) { return orderRelease(sel,ReleaseKind::Ungarrison,feedback); }

// 超时空传送：瞬移到目标点附近最近可走空格；距离越远相位不适（冻结）越久
bool World::chronoJump(EID id, float gx, float gy) {
    if(!valid(id) || ents[id].isBuilding) return false;
    Ent& e=ents[id];
    if(e.immobilized() || !std::isfinite(gx) || !std::isfinite(gy) || gx<0 || gy<0 || gx>=map.w || gy>=map.h) return false;
    const auto& definition=unitDef(e);
    int bx = -1, by = -1,layer=0;
    for (int r = 0; r <= 3 && bx < 0; r++)
        for (int dy = -r; dy <= r && bx < 0; dy++)
            for (int dx = -r; dx <= r && bx < 0; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int nx = (int)gx + dx, ny = (int)gy + dy;
                const int surface=map.preferredLayer(nx,ny,definition.pathDomain());
                if (movementPassable(e,nx,ny,surface) && !bldBlocked(nx,ny,surface) && !cellHardBlockedForMove(nx,ny,id,surface)) {
                    bx = nx; by = ny;layer=surface;
                }
            }
    if (bx < 0) return false;
    float dist = distf(e.x, e.y, bx + 0.5f, by + 0.5f);
    // 出发与到达的传送特效
    Effect w1; w1.kind = 9; w1.aux=(int)ChronoVisual::Departure; w1.x = e.x; w1.y = e.y; w1.maxAge = 20; effects.push_back(w1);
    e.x = e.px = bx + 0.5f; e.y = e.py = by + 0.5f;
    if(nativeGroundEnabled()) {
        e.cancelNativeDockTiming();e.cancelChronoReturn();e.nativeDock={};e.dockRefinery=INVALID_EID;e.nativeMovement={};
    }
    e.navLayer=e.previousNavLayer=e.goalLayer=(uint8_t)layer;
    unitOccSync(id);airOccSync(id);
    e.clearPath();
    e.state = UState::Idle;
    e.tpSick = std::min(600, 30 + (int)(dist * 2.5f)); // RA2：跳跃越远冻结越久
    Effect w2; w2.kind = 9; w2.x = e.x; w2.y = e.y; w2.maxAge = 24; effects.push_back(w2);
    emitSoundAt(Sfx::Deploy, e.x, e.y);
    return true;
}

// 辐射工兵：部署/收起辐射区（部署后不能移动，持续范围伤害）
// 重装大兵：部署/收起反装甲炮（部署后不能移动、不可被碾压，射程与反甲伤害提升）
void World::orderRadDeploy(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if(e.immobilized()) { eva(e.player,TR(S::MsgImmobilized));continue; }
        if (e.utype == UnitType::Desolator) {
            e.attackMove={};e.wps.clear();
            e.radDeployed = !e.radDeployed;
            if (e.radDeployed) {
                e.clearPath();
                e.target = INVALID_EID;
                e.guard = false;
                e.state = UState::Idle;
            }
        } else if (e.utype == UnitType::GuardianGI || e.utype == UnitType::GI) {
            e.attackMove={};e.wps.clear();
            // 重装大兵反装甲炮 / 美国大兵沙袋工事：部署后不可移动，武器强化
            e.deployed = !e.deployed;
            if (e.deployed) {
                e.clearPath();
                e.target = INVALID_EID;
                e.guard = false;
                e.state = UState::Idle;
                emitSoundAt(Sfx::Deploy, e.x, e.y);
            }
        } else if (e.utype == UnitType::SiegeChopper) {
            if(e.deploying())continue;
            // 攻城直升机：部署=降落转入远程炮击模式 / 收起=重新升空（YR 原作设定）
            if(!e.deployed && (usesNativeAirNavigation(e)?nativeAirLandingLayer((int)e.x,(int)e.y,id)<0:
                (!passableFor((int)e.x,(int)e.y,0) || bldBlocked((int)e.x,(int)e.y)
                || cellHardBlockedForMove((int)e.x,(int)e.y,id)))) {
                eva(e.player,TR(S::MsgCannotPlace));
                continue;
            }
            const bool landing=!e.deployed;
            if(landing && usesNativeAirNavigation(e))e.navLayer=e.goalLayer=(uint8_t)nativeAirLandingLayer((int)e.x,(int)e.y,id);
            e.attackMove={};
            e.dir=(g_gameRules.chopperDeployFacing+7)&7;e.turretDir=e.dir;
            e.state=landing?UState::DeployLanding:UState::DeployFolding;
            e.deployAnim=landing?g_gameRules.chopperLandingTicks:g_gameRules.chopperDeployTicks;
            e.clearPath();e.target=INVALID_EID;e.feedbackAttackTarget=INVALID_EID;e.guard=false;
            e.goalX=e.x;e.goalY=e.y;e.wps.clear();e.blockTick=0;e.fireAnim=0;
            emitSoundAt(Sfx::Deploy,e.x,e.y);
            unitOccSync(id);airOccSync(id);
        } else if (e.utype == UnitType::V3Launcher && nativeLauncherDefinition(e)<0) {
            e.attackMove={};e.wps.clear();
            // V3 火箭发射车：须部署才可开火；部署后不可移动
            e.deployed = !e.deployed;
            if (e.deployed) {
                e.clearPath();
                e.target = INVALID_EID;
                e.guard = false;
                e.state = UState::Idle;
                emitSoundAt(Sfx::Deploy, e.x, e.y);
            }
        }
    }
}

// ===================== EVA 播报 =====================
void World::eva(int player, const std::string& text, EvaCue cue) {
    if (player < 0 || player >= numPlayers || players[player].defeated) return;
    if (evaQueue.size() > 24) evaQueue.pop_front(); // 防积压
    evaQueue.push_back({player, text,cue});
}

void World::evaAll(const std::string& text, EvaCue cue) {
    for (int p = 0; p < numPlayers; p++) eva(p, text,cue);
}

EID World::orderDeploy(EID id) {
    EntityMutationScope mutation(*this);
    if (!valid(id)) return INVALID_EID;
    Ent& e = ents[id];
    if(e.immobilized()) { eva(e.player,TR(S::MsgImmobilized));return INVALID_EID; }
    // YR 1.001：普通/永久心控下的 MCV/建造厂不可展开或打包
    if (e.mindBy != INVALID_EID || e.permaControlled) return INVALID_EID;
    // 奴隶矿车：部署后成为移动卸货点并自动产奴（YR 奴隶经济主路径）
    if (e.utype == UnitType::SlaveMiner && !e.isBuilding) {
        e.attackMove={};e.wps.clear();
        e.deployed = !e.deployed;
        e.clearPath();
        e.state = UState::Idle;
        emitSoundAt(Sfx::Deploy, e.x, e.y);
        return id;
    }
    // MCV Repacks：建造厂打包回基地车
    if (e.isBuilding && e.btype == BldType::ConYard) {
        return packConYardToMcv(id);
    }
    if (e.utype != UnitType::MCV) return INVALID_EID;
    if (!canDeployMcv(id)) return INVALID_EID;
    int bx = (int)e.x - 1, by = (int)e.y - 1;
    int pl = e.player;
    const int source=usesRulesProfile()?rulesTransformIndex(e):-1;
    if(source>=0 && nativeProductionSerial==std::numeric_limits<uint64_t>::max())return INVALID_EID;
    const auto& destination=source>=0?nativeTypes[source]->building:bldDef(BldType::ConYard);
    const int health=std::max(1,(int)((int64_t)std::clamp(e.hp,1,unitDef(e).hp)*destination.hp/unitDef(e).hp));
    consumeEntity(id);
    EID replacement=spawnBuilding(pl, BldType::ConYard, bx, by, true,source,health);
    map.reveal(pl, bx + 1, by + 1, 8);
    emitSoundAt(Sfx::Deploy, (float)bx + 1, (float)by + 1);
    return replacement;
}

EID World::packConYardToMcv(EID id) {
    EntityMutationScope mutation(*this);
    if (!valid(id)) return INVALID_EID;
    Ent& e = ents[id];
    if (!e.isBuilding || e.btype != BldType::ConYard) return INVALID_EID;
    if(e.immobilized()) return INVALID_EID;
    if (!mcvRepacks) return INVALID_EID;
    if (e.mindBy != INVALID_EID || e.permaControlled) return INVALID_EID;
    int pl = e.player;
    const int source=usesRulesProfile()?rulesTransformIndex(e):-1;
    if(usesRulesProfile() && (source<0 || nativeTypes[source]->isBuilding || nativeTypes[source]->unit.type!=UnitType::MCV))return INVALID_EID;
    if(source>=0 && nativeProductionSerial==std::numeric_limits<uint64_t>::max())return INVALID_EID;
    float sx = e.x + 1.5f, sy = e.y + 1.5f;
    const BldDef& d = bldDef(e);
    const auto& destination=source>=0?nativeTypes[source]->unit:unitDef(UnitType::MCV);
    const int health=std::max(1,(int)((int64_t)std::clamp(e.hp,1,d.hp)*destination.hp/d.hp));
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int cx = (int)e.x + dx, cy = (int)e.y + dy;
            if (map.inBounds(cx, cy)) bldOcc[cellIdx(cx, cy)] = -1;
        }
    consumeEntity(id);
    recomputePower();
    EID mcv = spawnUnit(pl, UnitType::MCV, sx, sy,source);
    if(valid(mcv))ents[mcv].hp=health;
    emitSoundAt(Sfx::Deploy, sx, sy);
    return mcv;
}

EngineerRejectReason World::engineerRejection(EID unit,EID target,EngineerIntent intent) const {
    if(!valid(target) || !ents[target].isBuilding) return EngineerRejectReason::InvalidTarget;
    if(!valid(unit) || ents[unit].isBuilding || ents[unit].utype!=UnitType::Engineer) return EngineerRejectReason::NoEngineer;
    const auto& actor=ents[unit];const auto& building=ents[target];
    if(actor.nativeTemporalHead!=INVALID_EID || building.nativeTemporalHead!=INVALID_EID)return EngineerRejectReason::InvalidTarget;
    if(intent==EngineerIntent::RepairBridge) {
        const int group=bridgeForHut(target);
        if(group<0)return EngineerRejectReason::NoBridge;
        if(!bridgeNeedsRepair(group))return EngineerRejectReason::BridgeIntact;
        if(!canRestoreBridge(group))return EngineerRejectReason::BridgeBlocked;
    } else if(intent==EngineerIntent::Capture) {
        if(building.player>=0 && !isEnemy(actor.player,building.player)) return EngineerRejectReason::CaptureOwner;
        if(!bldDef(building).capturable) return EngineerRejectReason::NotCapturable;
    } else if(intent==EngineerIntent::Repair) {
        if(actor.player!=building.player) return EngineerRejectReason::RepairOwner;
        if(building.hp>=bldDef(building).hp) return EngineerRejectReason::FullHealth;
    } else return EngineerRejectReason::InvalidTarget;
    return EngineerRejectReason::None;
}

void World::emitEngineerResult(int player,EID target,EngineerIntent intent,const EngineerOrderResult& result) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::EngineerOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:target;event.engineer=result;event.engineerIntent=intent;
    if(valid(target) && ents[target].isBuilding) {
        const auto& building=ents[target];event.positional=true;
        event.x=building.x+bldDef(building).w/2.0f;event.y=building.y+bldDef(building).h/2.0f;
        event.visualTarget=VisualTarget::Building;
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}

EngineerOrderResult World::orderEngineer(const std::vector<EID>& sel,EID target,EngineerIntent intent,bool feedback) {
    EngineerOrderResult result;
    for(EID id:sel) {
        if(!valid(id)) continue;
        auto reason=engineerRejection(id,target,intent);
        std::vector<Vec2i> path;
        if(reason==EngineerRejectReason::None && !(intent==EngineerIntent::RepairBridge
            ?interactionPath(ents[id],ents[target],1.1f,path):attackPath(*this,ents[id],ents[target],path)))reason=EngineerRejectReason::Unreachable;
        if(reason!=EngineerRejectReason::None) {
            ++result.rejected;
            if(result.reason==EngineerRejectReason::None) { result.reason=reason;result.failedEntity=id; }
            continue;
        }
        auto& actor=ents[id];actor.attackMove={};actor.target=target;actor.engineerIntent=intent;
        actor.feedbackEngineerTarget=feedback?target:INVALID_EID;actor.feedbackAttackTarget=INVALID_EID;
        actor.path=std::move(path);actor.pathIdx=0;actor.wps.clear();actor.blockTick=0;actor.guard=false;actor.state=UState::Chasing;
        actor.nativeIdentity.sleeping=false;
        ++result.accepted;
    }
    return result;
}

EngineerOrderResult World::orderCapture(const std::vector<EID>& sel,EID target,bool feedback) {
    return orderEngineer(sel,target,EngineerIntent::Capture,feedback);
}
EngineerOrderResult World::orderRepair(const std::vector<EID>& sel,EID target,bool feedback) {
    return orderEngineer(sel,target,EngineerIntent::Repair,feedback);
}
EngineerOrderResult World::orderBridgeRepair(const std::vector<EID>& sel,EID target,bool feedback) {
    return orderEngineer(sel,target,EngineerIntent::RepairBridge,feedback);
}

// ===================== 生产 =====================
// 分类生产队列（RA2 原作）：步兵/车辆/空军/海军各自独立排队，当前项空则立即开工，否则排入队尾

