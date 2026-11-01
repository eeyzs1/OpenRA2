#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <climits>

MoveOrderResult World::orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove, bool append) {
    MoveOrderResult result;
    auto reject = [&](EID id, MoveRejectReason reason) {
        ++result.rejected;
        if (result.reason == MoveRejectReason::None) { result.reason = reason; result.failedEntity = id; }
    };
    // Validate before converting floats to tile coordinates or repacking buildings.
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= map.w || y >= map.h) {
        for (EID id : sel) if (valid(id)) reject(id, MoveRejectReason::InvalidTarget);
        return result;
    }
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
            EID mcv = packConYardToMcv(id);
            if (valid(mcv)) {
                units.push_back(mcv);
                DeployOrderResult packed;packed.accepted=packed.packed=1;
                packed.feedbackEntity=mcv;packed.x=bx;packed.y=by;
                packed.changes.push_back({id,mcv,false});
                emitDeployResult(player,packed);
            } else reject(id,MoveRejectReason::Unreachable);
            continue;
        }
        if (!e.isBuilding) units.push_back(id);
    }
    // Static connectivity is shared by the entire command, rather than trying
    // A* separately against many unreachable formation slots across a wall.
    // This is command-local: a newly placed/sold building cannot leave stale data.
    std::vector<int> components[3];
    auto componentAt = [&](int domain, int x, int y) -> int {
        if (!map.inBounds(x, y)) return -1;
        auto& labels = components[std::clamp(domain, 0, 2)];
        if (labels.empty()) {
            labels.assign(map.cells.size(), -1);
            std::vector<int> frontier;
            int component = 0;
            constexpr Vec2i steps[] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
            for (int sy=0; sy<map.h; ++sy) for (int sx=0; sx<map.w; ++sx) {
                const int start=cellIdx(sx,sy);
                if (labels[start]>=0 || !passableFor(sx,sy,domain) || bldBlocked(sx,sy)) continue;
                labels[start]=component;
                frontier.clear();frontier.push_back(start);
                for (size_t head=0; head<frontier.size(); ++head) {
                    const int cx=frontier[head]%map.w,cy=frontier[head]/map.w;
                    for (auto step:steps) {
                        const int nx=cx+step.x,ny=cy+step.y;
                        if (!passableStep(cx,cy,nx,ny,domain) || bldBlocked(nx,ny)) continue;
                        if (step.x && step.y &&
                            (!passableStep(cx,cy,nx,cy,domain) || !passableStep(cx,cy,cx,ny,domain)
                             || bldBlocked(nx,cy) || bldBlocked(cx,ny))) continue;
                        const int next=cellIdx(nx,ny);
                        if (labels[next]>=0) continue;
                        labels[next]=component;frontier.push_back(next);
                    }
                }
                ++component;
            }
        }
        return labels[cellIdx(x,y)];
    };
    // Reserve both current occupants and allied destinations across commands.
    // Replacing a group's orders releases its previous destinations atomically.
    std::vector<uint8_t> selected(ents.size(),0), landing(map.cells.size(),0);
    for(EID id:units) if(valid(id) && !ents[id].deployed && !ents[id].radDeployed
        && !(append && (ents[id].state==UState::Moving || ents[id].state==UState::AttackMoving))) selected[id]=1;
    auto reserve=[&](int x,int y,bool infantry) {
        if(!map.inBounds(x,y)) return;
        auto& use=landing[(size_t)y*map.w+x];
        use=infantry?(uint8_t)std::min(3,(int)use+1):3;
    };
    for(size_t id=0;id<ents.size();++id) {
        const auto& other=ents[id];
        if(!other.alive || other.isBuilding || other.airborne() || other.magneticHeight>0 || other.parasiting || selected[id]) continue;
        reserve((int)other.x,(int)other.y,unitDef(other.utype).isInfantry());
        if(other.pathIdx<(int)other.path.size()) {
            const auto goal=other.path.back();
            bool allied=false;
            for(EID mover:units) if(valid(mover) && isAllied(ents[mover].player,other.player)) { allied=true;break; }
            if(allied && (goal.x!=(int)other.x || goal.y!=(int)other.y))
                reserve(goal.x,goal.y,unitDef(other.utype).isInfantry());
        }
    }
    int n = 0;
    for (EID id : units) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        if (e.deployed || e.radDeployed) { reject(id, MoveRejectReason::Deployed); continue; }
        float lx = x, ly = y; // 本单元目标点（append 时可能被队首替换，不能污染共享参数）
        if (append) { // 路径点追加：入队；移动中则到位自动接续，空闲立即启程
            e.wps.push_back({lx, ly});
            if (e.state == UState::Moving || e.state == UState::AttackMoving) { ++result.queued; continue; }
            lx = e.wps.front().first; ly = e.wps.front().second;
            e.wps.pop_front();
        } else {
            e.wps.clear();
        }
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        if (ud.canHarvet()) e.autoHarvest = false; // 手动移动暂停自动寻矿
        e.goalX = lx; e.goalY = ly;
        // 目标点按单位散开（方阵）—— RA2 标准间距 1.5 格
        int cols = (int)ceilf(sqrtf((float)units.size()));
        const int rows = ((int)units.size() + cols - 1) / cols;
        const int rowWidth = std::min(cols, (int)units.size() - (n / cols) * cols);
        float ox = lx + (n % cols - (rowWidth - 1) * 0.5f) * 1.5f;
        float oy = ly + (n / cols - (rows - 1) * 0.5f) * 1.5f;
        n++;
        if (ud.isAir()) {
            // 战机：直线飞行，无视地形
            e.goalX = std::clamp(ox, 0.5f, map.w - 0.5f);
            e.goalY = std::clamp(oy, 0.5f, map.h - 0.5f);
            e.orbitA = (float)(id % 8) * 0.785f;
            e.state = attackMove ? UState::AttackMoving : UState::Moving;
            unitOccSync(id);airOccSync(id);
            ++result.accepted;
            continue;
        }
        if (e.utype == UnitType::Chrono || e.utype == UnitType::ChronoCommando
            || e.utype == UnitType::ChronoIvan) {
            // 超时空系：传送移动（RA2/YR：军团兵、突击队、超时空伊文）
            if (chronoJump(id, ox, oy)) { e.guard = attackMove; ++result.accepted; }
            else reject(id, MoveRejectReason::Unreachable);
            continue;
        }
        std::vector<Vec2i> path;
        const int nominalX=(int)ox,nominalY=(int)oy;
        const int preferredX=std::clamp(nominalX,0,map.w-1),preferredY=std::clamp(nominalY,0,map.h-1);
        const int domain=ud.pathDomain();
        int originComponent=components[domain].empty()?-2:componentAt(domain,(int)e.x,(int)e.y);
        auto available=[&](int tx,int ty) {
            return map.inBounds(tx,ty) && passableFor(tx,ty,domain) && !bldBlocked(tx,ty)
                && (originComponent==-2 || (originComponent>=0 && componentAt(domain,tx,ty)==originComponent))
                && landing[(size_t)ty*map.w+tx]<(ud.isInfantry()?3:1);
        };
        bool found=false;
        for(int radius=0;radius<=8 && !found;++radius)
            for(int dy=-radius;dy<=radius && !found;++dy) for(int dx=-radius;dx<=radius;++dx) {
                if(std::max(std::abs(dx),std::abs(dy))!=radius) continue;
                int tx=preferredX+dx,ty=preferredY+dy;
                if(!available(tx,ty)) continue;
                ox=(float)tx;oy=(float)ty;found=true;break;
            }
        bool pathFound=found && map.findPath((int)e.x,(int)e.y,(int)ox,(int)oy,path,20000,domain);
        if (!pathFound) {
            // Ordinary commands keep the cheap A* fast path. Build connectivity
            // only after a failure, then share it with the rest of this group.
            originComponent=componentAt(domain,(int)e.x,(int)e.y);
            // A distant or disconnected target still has one clear outcome:
            // the closest free tile on this unit's reachable side of the map.
            int best=-1;int bestDistance=INT_MAX;
            for(int ty=0;ty<map.h;++ty) for(int tx=0;tx<map.w;++tx) {
                if(!available(tx,ty))continue;
                const int dx=tx-preferredX,dy=ty-preferredY;
                const int distance=dx*dx+dy*dy;
                if(distance<bestDistance){best=cellIdx(tx,ty);bestDistance=distance;}
            }
            if(best>=0) {
                ox=(float)(best%map.w);oy=(float)(best/map.w);
                pathFound=map.findPath((int)e.x,(int)e.y,(int)ox,(int)oy,path,20000,domain);
            }
        }
        if (pathFound) {
            const auto endpoint=path.empty()?Vec2i{(int)e.x,(int)e.y}:path.back();
            if(endpoint.x!=nominalX || endpoint.y!=nominalY) ++result.adjusted;
            reserve(endpoint.x,endpoint.y,ud.isInfantry());
            e.path = std::move(path);
            e.pathIdx = 0;
            e.blockTick = 0;
            e.goalX = ox + 0.5f; e.goalY = oy + 0.5f;
            e.state = e.path.empty() ? UState::Idle : (attackMove ? UState::AttackMoving : UState::Moving);
            ++result.accepted;
        } else {
            e.clearPath(); e.pathIdx = 0; e.wps.clear(); e.blockTick = 0;
            e.state = UState::Idle;
            reject(id, MoveRejectReason::Unreachable);
        }
        if (unitDef(e.utype).canHarvet() && !attackMove) {
            // Move 将 autoHarvest=false；卸完后不自动再采，直到显式采矿令
            e.oreCell = {-1, -1};
        }
    }
    return result;
}

AttackRejectReason World::attackRejection(EID attacker,EID target) const {
    if(!valid(attacker) || !valid(target) || attacker==target) return AttackRejectReason::InvalidTarget;
    const auto& e=ents[attacker];const auto& t=ents[target];
    if(e.deploying())return AttackRejectReason::Busy;
    // Normal attack orders must stop after capture or mind control changes sides.
    // Neutral destructibles remain valid; force-fire is a separate, unimplemented intent.
    if(e.confused<=0 && t.player>=0 && !isEnemy(e.player,t.player)) return AttackRejectReason::InvalidTarget;
    if(e.isBuilding) return AttackRejectReason::NoWeapon;
    const auto& ud=unitDef(e.utype);
    if(e.utype==UnitType::Spy) {
        return isEnemy(e.player,t.player) && (t.isBuilding || unitDef(t.utype).isInfantry())
            ?AttackRejectReason::None:AttackRejectReason::Unsupported;
    }
    const auto ew=effWeapon(e,&t);
    if(ew.damage<=0) return AttackRejectReason::NoWeapon;
    bool air=t.airborne();
    if(air && !ew.antiAir) return AttackRejectReason::NoAntiAir;
    if(!air && !ew.antiGround) return AttackRejectReason::NoAntiGround;
    if(e.utype==UnitType::Magnetron && !t.isBuilding && unitDef(t.utype).isInfantry()) return AttackRejectReason::Unsupported;
    if(e.utype==UnitType::AttackDog && (t.isBuilding || !unitDef(t.utype).isInfantry())) return AttackRejectReason::Ineffective;
    float tx=t.x,ty=t.y;
    if(t.isBuilding) { tx+=bldDef(t.btype).w/2.0f;ty+=bldDef(t.btype).h/2.0f; }
    bool water=map.inBounds((int)tx,(int)ty) && map.at((int)tx,(int)ty).terrain==Terrain::Water;
    if(ew.navalOnly && !water) return AttackRejectReason::NavalOnly;
    const bool psychic=ud.isPsychic() || e.utype==UnitType::MasterMind;
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
    if(valid(target)) {
        const auto& t=ents[target];event.positional=true;event.x=t.x;event.y=t.y;
        if(t.isBuilding) { event.x+=bldDef(t.btype).w/2.0f;event.y+=bldDef(t.btype).h/2.0f; }
        captureVisualTarget(event,t);
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}

AttackOrderResult World::orderAttack(const std::vector<EID>& sel,EID target,bool feedback) {
    AttackOrderResult result;
    auto reject=[&](EID id,AttackRejectReason reason) {
        ++result.rejected;
        if(result.reason==AttackRejectReason::None) { result.reason=reason;result.failedEntity=id; }
    };
    for(EID id:sel) {
        if(!valid(id)) continue;
        auto reason=attackRejection(id,target);
        if(reason!=AttackRejectReason::None) { reject(id,reason);continue; }
        auto& e=ents[id];const auto& t=ents[target];const auto& ud=unitDef(e.utype);
        float tx=t.x,ty=t.y;
        if(t.isBuilding) { tx+=bldDef(t.btype).w/2.0f;ty+=bldDef(t.btype).h/2.0f; }
        if(e.utype==UnitType::Spy && !t.isBuilding) {
            e.camouflaged=true;
            int color=t.player>=0 && t.player<numPlayers?players[t.player].colorId:0;
            e.camoTick=(int)t.utype|((color&0xFF)<<16);
            e.target=INVALID_EID;e.clearPath();e.wps.clear();e.state=UState::Idle;
            e.feedbackAttackTarget=INVALID_EID;emitSoundAt(Sfx::Click,e.x,e.y);++result.accepted;continue;
        }
        const auto weapon=effWeapon(e,&t);
        float range=ud.hasC4() && t.isBuilding?2.5f:(float)weapon.range;
        bool inRange=e.utype!=UnitType::Spy && distf(e.x,e.y,tx,ty)<=range;
        std::vector<Vec2i> path;
        if(!inRange && !attackPath(*this,e,t,path)) { reject(id,AttackRejectReason::Unreachable);continue; }
        e.cancelBurst();
        e.wps.clear();e.target=target;e.guard=false;e.path=std::move(path);e.pathIdx=0;e.blockTick=0;
        e.state=inRange?UState::Attacking:UState::Chasing;
        e.feedbackAttackTarget=feedback?target:INVALID_EID;
        ++result.accepted;
    }
    return result;
}

void World::orderHarvest(const std::vector<EID>& sel, int x, int y) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (!unitDef(e.utype).canHarvet()) continue;
        e.wps.clear();
        e.oreCell = {x, y};
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        e.autoHarvest = true; // 显式采矿命令恢复自动寻矿
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, x, y, path);
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::HarvestGo;
    }
}

void World::orderReturnToRefinery(const std::vector<EID>& sel, EID preferredRef) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (!unitDef(e.utype).canHarvet()) continue;
        if (e.utype == UnitType::SlaveMiner && e.deployed) continue;
        e.wps.clear();
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        e.autoHarvest = true; // 显式回厂后卸完可再出发
        beginHarvesterReturn(e, id, preferredRef);
        unitOccSync(id);airOccSync(id);
    }
}

void World::orderStop(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if(e.deploying())continue;
        // 乌贼主动松开缠绕（RA2 原作：可命令乌贼撤退）
        if (e.parasiting && e.utype == UnitType::Squid) {
            if (valid(e.parasiteHost)) ents[e.parasiteHost].parasite = INVALID_EID;
            e.parasiting = false;
            e.parasiteHost = INVALID_EID;
        }
        e.clearPath();
        e.wps.clear(); // 停止清空路径点队列
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        if (unitDef(e.utype).canHarvet())
            e.autoHarvest = false; // Stop 后暂停自动寻矿，直到再次下达采矿命令
        if (unitDef(e.utype).isAir()) {
            // 战机：停机中的保持停机，飞行中的改为原地盘旋
            if (e.state != UState::Landed) {
                e.goalX = e.x; e.goalY = e.y;
                e.state = UState::Circling;
            }
        } else {
            e.state = UState::Idle;
        }
    }
}

// X 散布：各自前往周围随机空格（规避范围伤害与碾压）
void World::orderScatter(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        if (ud.isAir()) continue; // 战机不散布
        int dom = ud.pathDomain();
        e.wps.clear();
        e.cancelBurst();
        e.target = INVALID_EID;
        e.guard = false;
        // 在 2~4 格内找随机可通行落点
        for (int tries = 0; tries < 8; tries++) {
            int dx = rng.range(-4, 4), dy = rng.range(-4, 4);
            if (abs(dx) < 2 && abs(dy) < 2) continue;
            int nx = (int)e.x + dx, ny = (int)e.y + dy;
            if (!passableStep((int)e.x, (int)e.y, nx, ny, dom) || bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
            std::vector<Vec2i> path;
            if (map.findPath((int)e.x, (int)e.y, nx, ny, path, 20000, dom)) {
                e.path = std::move(path);
                e.pathIdx = 0;
                e.state = UState::Moving;
                e.goalX = nx + 0.5f; e.goalY = ny + 0.5f;
                break;
            }
        }
    }
}

// G 警戒：驻守原地，按视野半径索敌（比普通 Idle 的射程+2 更远）
void World::orderGuard(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (unitDef(e.utype).weapon.damage == 0) continue;
        e.clearPath();
        e.wps.clear();
        e.target = INVALID_EID;
        e.guard = true;
        if (!unitDef(e.utype).isAir()) e.state = UState::Idle;
    }
}

EntryRejectReason World::entryRejection(EID unit,EID target,EntryKind kind) const {
    if(!valid(target) || (kind==EntryKind::Garrison)!=ents[target].isBuilding) return EntryRejectReason::InvalidTarget;
    if(!valid(unit) || unit==target || ents[unit].isBuilding) return EntryRejectReason::Incompatible;
    const auto& actor=ents[unit];const auto& destination=ents[target];const auto& ud=unitDef(actor.utype);
    if(kind==EntryKind::Board) {
        const auto& td=unitDef(destination.utype);
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
        const auto& bd=bldDef(destination.btype);const int domain=garrisonDomain(destination.btype);
        if(bd.garrisonCap<=0 || !domain) return EntryRejectReason::Unsupported;
        if(destination.player>=0 && destination.player!=actor.player) return EntryRejectReason::WrongOwner;
        const bool fit=(domain==1 && ud.isInfantry())
            || (domain==2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain()==0 && !ud.canHarvet())
            || (domain==3 && destination.player==actor.player && !ud.isAir() && !isHero(actor.utype));
        if(!fit || (destination.btype==BldType::CivHouse && !canGarrisonCivHouse(actor.utype))) return EntryRejectReason::Incompatible;
        if(domain!=3 && (int)destination.garrison.size()>=bd.garrisonCap) return EntryRejectReason::Full;
    }
    if(actor.deployed || actor.radDeployed) return EntryRejectReason::Deployed;
    return EntryRejectReason::None;
}

float World::entryReach(const Ent& target) const {
    return target.isBuilding?std::max(bldDef(target.btype).w,bldDef(target.btype).h)/2.0f+1.5f:1.6f;
}

bool World::entryPath(const Ent& unit,const Ent& target,std::vector<Vec2i>& path) const {
    return interactionPath(unit,target,entryReach(target),path);
}

bool World::interactionPath(const Ent& unit,const Ent& target,float reach,std::vector<Vec2i>& path) const {
    float tx=target.x,ty=target.y;
    if(target.isBuilding) { tx+=bldDef(target.btype).w/2.0f;ty+=bldDef(target.btype).h/2.0f; }
    path.clear();
    if(distf(unit.x,unit.y,tx,ty)<reach) return true;
    std::vector<uint8_t> costs(map.cells.size(),0);
    std::vector<uint8_t> fixedInfantry(map.cells.size(),0);
    for(const auto& other:ents) if(&other!=&unit && other.alive && !other.isBuilding && other.occCell>=0 && map.inBounds((int)other.x,(int)other.y)) {
        const size_t cell=(size_t)(int)other.y*map.w+(int)other.x;
        costs[cell]=std::max<uint8_t>(costs[cell],16);
        if(other.deployed || other.radDeployed) {
            if(!unitDef(unit.utype).isInfantry() || !unitDef(other.utype).isInfantry() || ++fixedInfantry[cell]>=3) costs[cell]=255;
        }
    }
    bool found=map.findPathToRange((int)unit.x,(int)unit.y,tx,ty,reach-0.001f,path,20000,unitDef(unit.utype).pathDomain(),&costs);
    if(found && path.empty()) path.push_back({(int)unit.x,(int)unit.y});
    return found;
}

void World::emitEntryResult(int player,EID target,EntryKind kind,const EntryOrderResult& result) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::EntryOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:target;event.entry=result;event.entryKind=kind;
    if(valid(target)) {
        const auto& destination=ents[target];event.positional=true;event.x=destination.x;event.y=destination.y;
        if(destination.isBuilding) { event.x+=bldDef(destination.btype).w/2.0f;event.y+=bldDef(destination.btype).h/2.0f; }
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
        actor.wps.clear();actor.blockTick=0;actor.state=UState::Boarding;
        actor.engineerIntent=EngineerIntent::None;actor.feedbackEngineerTarget=INVALID_EID;actor.feedbackAttackTarget=INVALID_EID;
        actor.feedbackEntryTarget=feedback?target:INVALID_EID;actor.feedbackEntryKind=kind;++result.accepted;
    }
    return result;
}

EntryOrderResult World::orderBoard(const std::vector<EID>& sel,EID target,bool feedback) {
    return orderEntry(sel,target,EntryKind::Board,feedback);
}

// Reachability is local to the existing release search box. A free destination
// behind a closed wall is not an exit. Source footprint cells are virtual seeds.
std::vector<Vec2i> World::releaseCells(const Ent& source,UnitType passenger,int radius) const {
    int bx=(int)source.x,by=(int)source.y;
    int width=source.isBuilding?bldDef(source.btype).w:1,height=source.isBuilding?bldDef(source.btype).h:1;
    int cx=source.isBuilding?(int)(source.x+width/2.0f):bx,cy=source.isBuilding?(int)(source.y+height/2.0f):by;
    const int side=radius*2+1;std::vector<uint8_t> reached((size_t)side*side,0);std::vector<Vec2i> queue;
    auto inBox=[&](int x,int y) { return map.inBounds(x,y) && abs(x-cx)<=radius && abs(y-cy)<=radius; };
    auto inside=[&](int x,int y) { return x>=bx && x<bx+width && y>=by && y<by+height; };
    auto index=[&](int x,int y) { return (size_t)(y-cy+radius)*side+(size_t)(x-cx+radius); };
    const auto& ud=unitDef(passenger);
    auto occupied=[&](int x,int y) {
        if(ud.isInfantry()) return countInfantryAtCell(x,y)>=3 || [&]() {
            for(EID id=unitAtCell(x,y);id!=INVALID_EID;id=ents[id].occNext) if(!unitDef(ents[id].utype).isInfantry()) return true;
            return false;
        }();
        return groundUnitBlocksCell(x,y);
    };
    auto open=[&](int x,int y) {
        return inBox(x,y) && (inside(x,y) || (map.passable(x,y) && !bldBlocked(x,y) && !occupied(x,y)));
    };
    for(int y=by;y<by+height;++y) for(int x=bx;x<bx+width;++x) if(inBox(x,y)) { reached[index(x,y)]=1;queue.push_back({x,y}); }
    for(size_t n=0;n<queue.size();++n) for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy) {
        const auto from=queue[n];const int x=from.x+dx,y=from.y+dy;
        if(!open(x,y) || reached[index(x,y)]) continue;
        bool airSource=!source.isBuilding && unitDef(source.utype).isAir();
        if(!(inside(from.x,from.y) && airSource) && !map.climbOk(from.x,from.y,x,y,ud.pathDomain())) continue;
        if(dx && dy) {
            auto corner=[&](int sx,int sy) {
                if(!inBox(sx,sy)) return false;
                if(inside(sx,sy)) return true;
                // A shore can meet a boat diagonally, but a wall/vehicle is solid.
                if(inside(from.x,from.y) && (airSource || map.at(from.x,from.y).terrain==Terrain::Water))
                    return !bldBlocked(sx,sy) && !occupied(sx,sy);
                return open(sx,sy);
            };
            if(!corner(from.x+dx,from.y) || !corner(from.x,from.y+dy)) continue;
        }
        reached[index(x,y)]=1;queue.push_back({x,y});
    }
    std::vector<Vec2i> cells;
    for(int r=1;r<=radius;++r) for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx) {
        if(std::max(abs(dx),abs(dy))!=r) continue;
        int x=cx+dx,y=cy+dy;
        if(inBox(x,y) && !inside(x,y) && reached[index(x,y)] && !bldBlocked(x,y) && unitAtCell(x,y)==INVALID_EID) cells.push_back({x,y});
    }
    return cells;
}

ReleaseOrderResult World::releaseContainer(EID id,ReleaseKind kind) {
    ReleaseOrderResult result;
    auto reject=[&](ReleaseRejectReason reason) { result.rejected=1;result.failedEntity=id;result.reason=reason; };
    if(!valid(id)) { reject(ReleaseRejectReason::InvalidTarget);return result; }
    const bool building=kind==ReleaseKind::Ungarrison;
    if(ents[id].isBuilding!=building || (building?bldDef(ents[id].btype).garrisonCap<=0:
        (unitDef(ents[id].utype).cargoCap<=0 || ents[id].utype==UnitType::AircraftCarrier))) {
        reject(ReleaseRejectReason::Unsupported);return result;
    }
    auto out=building?ents[id].garrison:ents[id].cargo;
    if(out.empty()) { reject(ReleaseRejectReason::Empty);return result; }
    const int owner=ents[id].player;
    while(!out.empty()) {
        const auto saved=out.back();auto cells=releaseCells(ents[id],saved.type,building?3:2);
        if(cells.empty()) break;
        const auto cell=cells.front();EID unit=spawnUnit(owner,saved.type,cell.x+0.5f,cell.y+0.5f);
        if(!valid(unit)) break;
        auto& restored=ents[unit];
        restored.kills=saved.kills;restored.veterancyValue=saved.veterancyValue;restored.vetRank=std::clamp(saved.vetRank,0,2);
        restored.hp=std::clamp(saved.hp,1,maxHpFor(restored,unitDef(saved.type)));
        restored.oreLoad=saved.oreLoad;restored.gemLoad=saved.gemLoad;restored.ammo=saved.ammo;
        restored.atkCd=saved.atkCd;restored.invuln=saved.invuln;
        restored.crateDmgBoost=saved.crateDmgBoost;restored.crateArmorBoost=saved.crateArmorBoost;restored.crateSpeedBoost=saved.crateSpeedBoost;
        restored.airstrikeCd=saved.airstrikeCd;restored.confused=saved.confused;
        restored.gatlingHeat=saved.gatlingHeat;restored.gatlingStage=saved.gatlingStage;
        restored.autoHarvest=saved.autoHarvest;restored.permaControlled=saved.permaControlled;
        restored.cargo=saved.cargo;
        out.pop_back();++result.released;
    }
    result.remaining=(int)out.size();
    if(result.remaining) reject(ReleaseRejectReason::Blocked);
    if(building) {
        ents[id].garrison=std::move(out);
        if(result.released && !result.remaining && ents[id].btype==BldType::CivHouse) ents[id].player=-1;
        if(result.released) recomputePower();
    } else ents[id].cargo=std::move(out);
    return result;
}

void World::emitReleaseResult(int player,EID source,ReleaseKind kind,const ReleaseOrderResult& result) {
    if(!result.released && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::ReleaseOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:source;event.release=result;event.releaseKind=kind;
    EID marker=result.failedEntity>=0?result.failedEntity:source;
    if(valid(marker)) {
        const auto& container=ents[marker];event.positional=true;event.x=container.x;event.y=container.y;
        if(container.isBuilding) { event.x+=bldDef(container.btype).w/2.0f;event.y+=bldDef(container.btype).h/2.0f; }
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
        if(result.reason==ReleaseRejectReason::None && one.reason!=ReleaseRejectReason::None) { result.reason=one.reason;result.failedEntity=id; }
        if(!feedback && owner>=0) {
            if(one.released && !one.remaining) eva(owner,TR(S::EvaUnloadDone));
            else if(kind==ReleaseKind::Unload && one.remaining) eva(owner,TR(S::EvaUnloadFail));
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
    const auto& definition=unitDef(e.utype);
    int bx = -1, by = -1;
    for (int r = 0; r <= 3 && bx < 0; r++)
        for (int dy = -r; dy <= r && bx < 0; dy++)
            for (int dx = -r; dx <= r && bx < 0; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int nx = (int)gx + dx, ny = (int)gy + dy;
                if (passableFor(nx,ny,definition.pathDomain()) && !bldBlocked(nx,ny) && !cellHardBlockedForMove(nx,ny,id)) {
                    bx = nx; by = ny;
                }
            }
    if (bx < 0) return false;
    float dist = distf(e.x, e.y, bx + 0.5f, by + 0.5f);
    // 出发与到达的传送特效
    Effect w1; w1.kind = 9; w1.aux=(int)ChronoVisual::Departure; w1.x = e.x; w1.y = e.y; w1.maxAge = 20; effects.push_back(w1);
    e.x = e.px = bx + 0.5f; e.y = e.py = by + 0.5f;
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
            e.radDeployed = !e.radDeployed;
            if (e.radDeployed) {
                e.clearPath();
                e.target = INVALID_EID;
                e.guard = false;
                e.state = UState::Idle;
            }
        } else if (e.utype == UnitType::GuardianGI || e.utype == UnitType::GI) {
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
            if(!e.deployed && (!passableFor((int)e.x,(int)e.y,0) || bldBlocked((int)e.x,(int)e.y)
                || cellHardBlockedForMove((int)e.x,(int)e.y,id))) {
                eva(e.player,TR(S::MsgCannotPlace));
                continue;
            }
            const bool landing=!e.deployed;
            e.dir=(g_gameRules.chopperDeployFacing+7)&7;e.turretDir=e.dir;
            e.state=landing?UState::DeployLanding:UState::DeployFolding;
            e.deployAnim=landing?g_gameRules.chopperLandingTicks:g_gameRules.chopperDeployTicks;
            e.clearPath();e.target=INVALID_EID;e.feedbackAttackTarget=INVALID_EID;e.guard=false;
            e.goalX=e.x;e.goalY=e.y;e.wps.clear();e.blockTick=0;e.fireAnim=0;
            emitSoundAt(Sfx::Deploy,e.x,e.y);
            unitOccSync(id);airOccSync(id);
        } else if (e.utype == UnitType::V3Launcher) {
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
void World::eva(int player, const std::string& text) {
    if (player < 0 || player >= numPlayers || players[player].defeated) return;
    if (evaQueue.size() > 24) evaQueue.pop_front(); // 防积压
    evaQueue.push_back({player, text});
}

void World::evaAll(const std::string& text) {
    for (int p = 0; p < numPlayers; p++) eva(p, text);
}

EID World::orderDeploy(EID id) {
    if (!valid(id)) return INVALID_EID;
    Ent& e = ents[id];
    if(e.immobilized()) { eva(e.player,TR(S::MsgImmobilized));return INVALID_EID; }
    // YR 1.001：普通/永久心控下的 MCV/建造厂不可展开或打包
    if (e.mindBy != INVALID_EID || e.permaControlled) return INVALID_EID;
    // 奴隶矿车：部署后成为移动卸货点并自动产奴（YR 奴隶经济主路径）
    if (e.utype == UnitType::SlaveMiner && !e.isBuilding) {
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
    consumeEntity(id);
    EID replacement=spawnBuilding(pl, BldType::ConYard, bx, by, true);
    map.reveal(pl, bx + 1, by + 1, 8);
    emitSoundAt(Sfx::Deploy, (float)bx + 1, (float)by + 1);
    return replacement;
}

EID World::packConYardToMcv(EID id) {
    if (!valid(id)) return INVALID_EID;
    Ent& e = ents[id];
    if (!e.isBuilding || e.btype != BldType::ConYard) return INVALID_EID;
    if(e.immobilized()) return INVALID_EID;
    if (!mcvRepacks) return INVALID_EID;
    if (e.mindBy != INVALID_EID || e.permaControlled) return INVALID_EID;
    int pl = e.player;
    float sx = e.x + 1.5f, sy = e.y + 1.5f;
    const BldDef& d = bldDef(e.btype);
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int cx = (int)e.x + dx, cy = (int)e.y + dy;
            if (map.inBounds(cx, cy)) bldOcc[cellIdx(cx, cy)] = -1;
        }
    consumeEntity(id);
    recomputePower();
    EID mcv = spawnUnit(pl, UnitType::MCV, sx, sy);
    emitSoundAt(Sfx::Deploy, sx, sy);
    return mcv;
}

EngineerRejectReason World::engineerRejection(EID unit,EID target,EngineerIntent intent) const {
    if(!valid(target) || !ents[target].isBuilding) return EngineerRejectReason::InvalidTarget;
    if(!valid(unit) || ents[unit].isBuilding || ents[unit].utype!=UnitType::Engineer) return EngineerRejectReason::NoEngineer;
    const auto& actor=ents[unit];const auto& building=ents[target];
    if(intent==EngineerIntent::Capture) {
        if(building.player>=0 && !isEnemy(actor.player,building.player)) return EngineerRejectReason::CaptureOwner;
        if(!bldDef(building.btype).capturable) return EngineerRejectReason::NotCapturable;
    } else if(intent==EngineerIntent::Repair) {
        if(actor.player!=building.player) return EngineerRejectReason::RepairOwner;
        if(building.hp>=bldDef(building.btype).hp) return EngineerRejectReason::FullHealth;
    } else return EngineerRejectReason::InvalidTarget;
    return EngineerRejectReason::None;
}

void World::emitEngineerResult(int player,EID target,EngineerIntent intent,const EngineerOrderResult& result) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::EngineerOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:target;event.engineer=result;event.engineerIntent=intent;
    if(valid(target) && ents[target].isBuilding) {
        const auto& building=ents[target];event.positional=true;
        event.x=building.x+bldDef(building.btype).w/2.0f;event.y=building.y+bldDef(building.btype).h/2.0f;
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
        if(reason==EngineerRejectReason::None && !attackPath(*this,ents[id],ents[target],path)) reason=EngineerRejectReason::Unreachable;
        if(reason!=EngineerRejectReason::None) {
            ++result.rejected;
            if(result.reason==EngineerRejectReason::None) { result.reason=reason;result.failedEntity=id; }
            continue;
        }
        auto& actor=ents[id];actor.target=target;actor.engineerIntent=intent;
        actor.feedbackEngineerTarget=feedback?target:INVALID_EID;actor.feedbackAttackTarget=INVALID_EID;
        actor.path=std::move(path);actor.pathIdx=0;actor.wps.clear();actor.blockTick=0;actor.guard=false;actor.state=UState::Chasing;
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

// ===================== 生产 =====================
// 分类生产队列（RA2 原作）：步兵/车辆/空军/海军各自独立排队，当前项空则立即开工，否则排入队尾

