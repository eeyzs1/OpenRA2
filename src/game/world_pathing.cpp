#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
bool canCrushOccupant(const World& world,const World::Ent& actor,const World::Ent& occupant) {
    if(actor.isBuilding || occupant.isBuilding || world.isAllied(actor.player,occupant.player)) return false;
    const auto& mover=unitDef(actor);const auto& other=unitDef(occupant);
    return mover.move==MoveType::Vehicle && mover.armor==Armor::Heavy && other.isInfantry()
        && other.armor==Armor::None && occupant.invuln==0 && !occupant.immobilized() && !occupant.deployed && !occupant.radDeployed;
}
bool pendingMovement(const World::Ent& unit) {
    if(unit.pathIdx>=(int)unit.path.size()) return false;
    return unit.state==UState::Moving || unit.state==UState::AttackMoving || unit.state==UState::Chasing
        || unit.state==UState::HarvestGo || unit.state==UState::HarvestReturn || unit.state==UState::Boarding
        || unit.state==UState::Servicing;
}
}

bool World::repathUnit(EID id, int gx, int gy, std::vector<Vec2i>& path,EID fixedBlocker,int requestedGoalLayer) {
    ++repathsThisTick; ++repathRequests;
    const Ent& e = ents[id];
    const int domain=unitDef(e).pathDomain();
    const bool infantry=unitDef(e).isInfantry();
    std::vector<uint8_t> costs(movementSlotCount(), 0);
    std::vector<uint8_t> fixedInfantry(infantry?movementSlotCount():0,0);
    for (size_t i = 0; i < ents.size(); ++i) {
        const Ent& other = ents[i];
        // Use the physical ground index, not just a live entity's position:
        // parasites follow hosts without occupying a cell, landed air units do.
        if ((EID)i == id || other.occCell<0 || other.occCell>=(int)costs.size()) continue;
        int x=other.occCell%map.w,y=other.occCell/map.w;
        if (map.inBounds(x, y)) {
            const bool fixed=other.deployed || other.deploying() || other.radDeployed || other.immobilized() || (other.guard && !pendingMovement(other))
                || (!isAllied(e.player,other.player) && !pendingMovement(other));
            const size_t index=movementSlot(x,y,other.navLayer);
            auto& cell=costs[index];cell=std::max(cell,uint8_t{16});
            if(fixed && !canCrushOccupant(*this,e,other)) {
                // An infantry mover can share a cell with two stationary
                // infantry. Only fixed occupants exhausting all slots seal it.
                if(!infantry || !unitDef(other).isInfantry()
                    || ++fixedInfantry[index]>=3) cell=255;
            }
        }
    }
    if(valid(fixedBlocker) && ents[fixedBlocker].occCell>=0 && ents[fixedBlocker].occCell<(int)costs.size())
        costs[(size_t)ents[fixedBlocker].occCell+ents[fixedBlocker].navLayer*map.cells.size()]=255;
    std::vector<Vec2i> suffix;
    int exit=-1,lane=-1;
    if(!map.hasBridgeDecks() && !e.path.empty() && e.path.back().x==gx && e.path.back().y==gy && map.inBounds((int)e.x,(int)e.y)) {
        lane=movementCorridor((int)e.y*map.w+(int)e.x,domain);
        if(lane>=0) for(int step=e.pathIdx;step<(int)e.path.size();++step) {
            const auto node=e.path[step];const int cell=node.y*map.w+node.x;
            if(movementCorridorLabels[domain][cell]!=lane) {
                exit=cell;suffix.assign(e.path.begin()+step+1,e.path.end());gx=node.x;gy=node.y;break;
            }
        }
    }
    if(exit>=0) for(size_t cell=0;cell<costs.size();++cell)
        if(movementCorridorLabels[domain][cell]!=lane && (int)cell!=exit) costs[cell]=255;
    const bool retained=e.attackMove.active && e.state==UState::AttackMoving && gx==(int)e.attackMove.x && gy==(int)e.attackMove.y;
    const int goalLayer=requestedGoalLayer>=0?requestedGoalLayer:retained?e.attackMove.layer:movementLayerAt(e,gx,gy);
    bool ok = map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000,
                          domain, &costs,e.navLayer,goalLayer,unitDef(e).fitsUnderBridge(),nativeGateAccess(e));
    if(ok && exit>=0) {
        // Preserve an in-progress retreat/exit waypoint. Replanning straight to
        // the final goal here would reverse the car back into opposing traffic.
        ok=!path.empty() && path.back().x==gx && path.back().y==gy && path.size()+suffix.size()<=4096;
        if(ok) path.insert(path.end(),suffix.begin(),suffix.end());
    }
    if(ok && exit<0 && (e.state==UState::Moving || e.state==UState::AttackMoving)) {
        const auto endpoint=path.empty()?Vec2i{(int)e.x,(int)e.y}:path.back();
        // A zero-node search has reached the destination cell. Complete the
        // physical leg to its center through the ordinary movement step.
        if(path.empty())path.push_back(endpoint);
        if(endpoint.x!=gx || endpoint.y!=gy) {
            MoveOrderResult result;result.accepted=1;result.adjusted=1;
            emitMoveResult(e.player,id,endpoint.x+0.5f,endpoint.y+0.5f,result);
        }
    }
    if (!ok) ++repathFailures;
    return ok;
}

void World::tryUnstackIdle(Ent& e, EID id) {
    if (!e.alive || e.isBuilding || e.state != UState::Idle) return;
    const UnitDef& ud = unitDef(e);
    if (e.airborne()) return;
    const int cx = (int)e.x, cy = (int)e.y;
    bool need = false;
    if (ud.isInfantry()) {
        need = countInfantryAtCell(cx, cy, id) >= 3; // 已有 ≥3 名其他步兵 → 超叠
    } else if (map.inBounds(cx, cy) && !unitOccHead.empty()) {
        for (EID oid = unitOccHead[(size_t)cellIdx(cx, cy)]; oid != INVALID_EID; oid = ents[oid].occNext) {
            if (oid == id) continue;
            if (ents[oid].navLayer==e.navLayer && !unitDef(ents[oid]).isInfantry()) { need = true; break; }
        }
    }
    if (!need) return;
    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const int start = (int)((uint32_t)id + tick) & 7;
    for (int k = 0; k < 8; k++) {
        const int d = (start + k) & 7;
        const int nx = cx + DX[d], ny = cy + DY[d];
        const int surface=movementStep(e,cx,cy,e.navLayer,nx,ny);
        if (surface<0 || bldBlocked(nx,ny,surface)) continue;
        if (cellHardBlockedForMove(nx, ny, id)) continue;
        e.x = nx + 0.5f;
        e.y = ny + 0.5f;
        e.navLayer=(uint8_t)surface;
        unitOccSync(id);
        airOccSync(id);
        return;
    }
}

void World::moveAlongPath(Ent& e, EID id,int nativeSegments) {
    if(!e.alive) return;
    if (e.pathIdx >= (int)e.path.size()) return;
    // 乌贼缠绕：宿主舰船被定身无法移动（RA2 原作签名机制）
    if (e.parasite != INVALID_EID && valid(e.parasite) && ents[e.parasite].utype == UnitType::Squid) return;
    const UnitDef& ud = unitDef(e);
    Vec2i next = e.path[e.pathIdx];
    auto corridorCell = [&](int x,int y,int domain) { return narrowMovementCell(x,y,domain); };
    auto abandonPath = [&]() {
        e.nativeMovement={};
        if(e.state==UState::Servicing) { endService(e,id,ServiceRejectReason::Unreachable);return; }
        if (e.state == UState::Moving || e.state == UState::AttackMoving) {
            const auto goal=e.path.back();
            MoveOrderResult result; result.rejected=1;
            result.reason=MoveRejectReason::Unreachable; result.failedEntity=id;
            if(!e.attackMove.active){emitMoveResult(e.player,id,goal.x+0.5f,goal.y+0.5f,result);e.wps.clear();}
            e.target=INVALID_EID; e.state=UState::Idle;
        }
        e.clearPath(); e.pathIdx=0; e.blockTick=0;
    };

    const auto followingTank = [&](EID blocker) {
        if(!valid(blocker) || !usesDriveArc(e))return false;
        const auto& lead=ents[blocker];
        const auto hasMoveOrder=[](const Ent& actor) {
            return actor.state==UState::Moving || actor.state==UState::AttackMoving;
        };
        if(lead.player!=e.player || lead.navLayer!=e.navLayer || !usesDriveArc(lead) || lead.immobilized()
            || !hasMoveOrder(e) || !hasMoveOrder(lead) || valid(lead.target) || !pendingMovement(lead))return false;
        const Vec2i current{(int)e.x,(int)e.y},ahead{(int)lead.x,(int)lead.y};
        if(ahead==current || e.path[e.pathIdx]!=ahead)return false;
        const int leadNext=lead.pathIdx+(lead.path[lead.pathIdx]==ahead?1:0);
        return leadNext<(int)lead.path.size() && lead.path[leadNext]==Vec2i{ahead.x+(ahead.x-current.x),ahead.y+(ahead.y-current.y)};
    };
    auto waitOrRepath = [&](EID blocker) -> bool {
        e.nativeMovement={};
        // A tank moving out along this same lane can take longer than the
        // ordinary blockage threshold to turn and clear a whole cell.
        constexpr int followingWait=3*LOGIC_FPS;
        if(e.blockTick<followingWait && followingTank(blocker)) {
            const auto cell=e.path[e.pathIdx];
            const auto heading=NativeGroundMath::vector(cell.x+.5f-e.x,cell.y+.5f-e.y);
            desireNativeHull(e,heading);
            if(!unitHasTurret(e.utype) || e.state!=UState::AttackMoving || !valid(e.target))desireNativeTurret(e,heading);
            ++e.blockTick;
            return true;
        }
        bool fixedIdle=false;
        // returns true if caller should abort this tick (always, after wait/repath)
        if (blocker != INVALID_EID && blocker != id) {
            if(e.blockTick==10 || e.blockTick>=45) {
                if(yieldMovingConflict(id,blocker)){e.blockTick=0;return true;}
                const auto yield=yieldIdleCorridor(id,blocker);
                if(yield==IdleYieldResult::Started) { e.blockTick=0;return true; }
                fixedIdle=yield==IdleYieldResult::NoPullout;
            }
            Ent& o = ents[blocker];
            bool exitingCorridor=corridorCell((int)e.x,(int)e.y,ud.pathDomain());
            for(Vec2i step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}})
                exitingCorridor |= corridorCell((int)e.x+step.x,(int)e.y+step.y,ud.pathDomain());
            // A blocked column may have no free adjacent pullout. Search a
            // short chain of friendly vehicles to an empty cell, then let the
            // tail move first. All displacement follows ordinary legal paths.
            if (isAllied(o.player,e.player) && (id < blocker || exitingCorridor) && e.blockTick >= 10
                && o.blockTick >= 10 && repathsThisTick < 8) {
                struct YieldNode { EID unit; int x,y,parent; };
                std::vector<YieldNode> nodes{{blocker,(int)o.x,(int)o.y,-1}};
                std::vector<uint8_t> seen(ents.size(),0); seen[blocker]=1;
                int end=-1, freeX=0, freeY=0,freeLayer=0;
                static constexpr int DX[4]={0,0,-1,1}, DY[4]={-1,1,0,0};
                for (size_t n=0;n<nodes.size() && n<128 && end<0;++n) {
                    const auto current=nodes[n];
                    const auto& pushed=ents[current.unit];
                    if (pushed.deployed || pushed.radDeployed || pushed.path.empty()
                        || (pushed.state!=UState::Moving && pushed.state!=UState::AttackMoving
                            && pushed.state!=UState::HarvestGo && pushed.state!=UState::HarvestReturn && pushed.state!=UState::Servicing)) continue;
                    for (int k=0;k<4;++k) {
                        int nx=current.x+DX[k], ny=current.y+DY[k];
                        const int layer=movementStep(pushed,current.x,current.y,pushed.navLayer,nx,ny);
                        if(layer<0 || bldBlocked(nx,ny,layer))continue;
                        if(narrowMovementCell(nx,ny,unitDef(pushed).pathDomain(),layer))continue;
                        EID occupant=unitAtCell(nx,ny,layer);
                        if (!cellHardBlockedForMove(nx,ny,current.unit,layer)) {
                            end=(int)n; freeX=nx; freeY=ny;freeLayer=layer;break;
                        }
                        if (occupant==INVALID_EID || occupant==id || (!exitingCorridor && occupant<=id) || seen[occupant]
                            || !isAllied(ents[occupant].player,e.player)) continue;
                        seen[occupant]=1;
                        nodes.push_back({occupant,nx,ny,(int)n});
                    }
                }
                std::vector<std::pair<EID,std::vector<Vec2i>>> plans;
                int available=8-repathsThisTick;
                for (int node=end;node>=0 && available>0;node=nodes[node].parent,--available) {
                    const auto current=nodes[node];
                    const auto goal=ents[current.unit].path.back();
                    std::vector<Vec2i> path;
                    ++repathsThisTick; ++repathRequests;
                    if (!map.findPath(freeX,freeY,goal.x,goal.y,path,20000,
                                      unitDef(ents[current.unit]).pathDomain(),nullptr,freeLayer,movementLayerAt(ents[current.unit],goal.x,goal.y),unitDef(ents[current.unit]).fitsUnderBridge(),nativeGateAccess(ents[current.unit])) || path.size()+30>4096) {
                        ++repathFailures; plans.clear(); end=-1; break;
                    }
                    // Dwell waypoints keep the pullout clear while the lead passes.
                    path.insert(path.begin(),30,Vec2i{freeX,freeY});
                    plans.emplace_back(current.unit,std::move(path));
                    freeX=current.x; freeY=current.y;freeLayer=ents[current.unit].navLayer;
                    if (current.parent<0) end=-1;
                }
                if (end<0) for (auto& plan:plans) {
                    auto& pushed=ents[plan.first];
                    pushed.path=std::move(plan.second); pushed.pathIdx=0; pushed.blockTick=0;
                }
            }

        }
        if (++e.blockTick > 45) {
            if (repathsThisTick >= 8) return true; // 堵约 1.5 秒：重寻路
            e.blockTick = 0;
            int gx = e.path.back().x, gy = e.path.back().y;
            // 终点被敌军硬挡才放弃；友军占用仍尝试靠近
            if (cellHardBlockedForMove(gx, gy, id)) {
                EID gOcc=unitAtCell(gx,gy,movementLayerAt(e,gx,gy));
                if (gOcc != INVALID_EID && !isAllied(ents[gOcc].player,e.player)) {
                    abandonPath();
                    return true;
                }
            }
            std::vector<Vec2i> path;
            if (repathUnit(id, gx, gy, path,fixedIdle?blocker:INVALID_EID)) {
                e.path = std::move(path); e.pathIdx = 0;
            } else abandonPath();
        }
        return true;
    };

    if(waitingForNativeGate(e,next)){e.blockTick=0;return;}
    if(!admitMovementCorridor(e,id)) return;
    // Admission may replace an occupant's route with a retreat prefix.
    next=e.path[e.pathIdx];
    const int nextLayer=map.hasBridgeDecks() || e.driveArc.wide
        ?movementStep(e,(int)e.x,(int)e.y,e.navLayer,next.x,next.y):0;
    if(nextLayer<0) {
        if(repathsThisTick>=8)return;
        const auto goal=e.path.back();std::vector<Vec2i> path;
        if(repathUnit(id,goal.x,goal.y,path)){e.path=std::move(path);e.pathIdx=0;}else abandonPath();
        return;
    }

    // 目标格占用：车互斥硬挡；步兵可叠≤3；碾压优先清格
    EID occ = unitAtCell(next.x,next.y,nextLayer);
    if (occ != INVALID_EID && occ != id) {
        Ent& o = ents[occ];
        // 坦克碾压（RA2 原作）：重甲车辆直接碾死敌方无甲步兵
        if (canCrushOccupant(*this,e,o)) {
            emitSoundAt(Sfx::Crush, o.x, o.y);
            if(o.player>=0 && o.player<numPlayers) {
                Player& vp = players[o.player];
                if (vp.evaUnitCd <= 0) { eva(o.player, TR(S::EvaUnitLost),EvaCue::UnitLost); vp.evaUnitCd = 150; }
            }
            kill(occ);
            if(!valid(id)) return; // death callbacks can also retire the moving vehicle
            // 格子已空，继续走正常移动流程
        } else if (cellHardBlockedForMove(next.x, next.y, id)) {
            EID yielding=occ;
            if(ud.isInfantry()) for(EID other=unitOccHead[(size_t)next.y*map.w+next.x];other!=INVALID_EID;other=ents[other].occNext) {
                const auto& candidate=ents[other];
                if(other!=id && candidate.navLayer==nextLayer && candidate.state==UState::Idle && isAllied(candidate.player,e.player)
                    && unitDef(candidate).isInfantry() && !candidate.deployed && !candidate.radDeployed) {
                    yielding=other;break;
                }
            }
            waitOrRepath(yielding);
            return;
        }
    }
    if (bldBlocked(next.x,next.y,nextLayer)) {
        if (repathsThisTick >= 8) return;
        // 建筑硬挡：重新寻路绕开
        std::vector<Vec2i> path;
        int gx = e.path.back().x, gy = e.path.back().y;
        if (repathUnit(id, gx, gy, path)) { e.path = std::move(path); e.pathIdx = 0; }
        else abandonPath();
        return;
    }
    if (map.inBounds(next.x,next.y) && movementReservations.size()==movementSlotCount()) {
        EID& reservation=movementReservations[movementSlot(next.x,next.y,nextLayer)];
        if (reservation != INVALID_EID && reservation != id) {
            waitOrRepath(reservation);
            return;
        }
        reservation = id;
    }
    auto releaseReservation=[&]() {
        if(movementReservations.size()==movementSlotCount()) {
            auto& reservation=movementReservations[movementSlot(next.x,next.y,nextLayer)];
            if(reservation==id)reservation=INVALID_EID;
        }
    };
    // Speed = 每逻辑帧移动 1/Speed 格（越大越慢）；连续滑动 + 渲染插值，避免「停一停再瞬移一格」
    int moveDelay = std::max(1, (int)std::ceil(ud.speed / g_gameRules.veteranSpeedBonus[std::clamp(e.vetRank, 0, 2)]));
    if (e.crateSpeedBoost > 0) moveDelay = std::max(1, (int)(moveDelay * 0.8f));
    float step = nativeCrateMultiplier(e.nativeIdentity,10) / (float)moveDelay;
    const bool sourceMovement=nativeMovementRules.available && e.nativeIdentity.definition && e.nativeIdentity.definition->movement.available();
    if(sourceMovement && e.x==next.x+.5f && e.y==next.y+.5f && e.navLayer==nextLayer) {
        // Traffic dwell nodes consume time, not distance. Their advancement
        // cannot depend on a nonzero locomotor velocity or accrue travel credit.
        releaseReservation();e.nativeMovement={};++e.pathIdx;e.moveTick=0;return;
    }
    const bool realTurning=usesNativeGround(e);
    const bool aimTurret=unitHasTurret(e.utype) && (e.state==UState::Attacking || e.state==UState::Chasing
        || (e.state==UState::AttackMoving && valid(e.target)));
    auto finishNode=[&](float x,float y,bool moved,bool keepCurve=false) {
        if(cellHardBlockedForMove(next.x,next.y,id,nextLayer) || bldBlocked(next.x,next.y,nextLayer)) {
            releaseReservation();
            waitOrRepath(unitAtCell(next.x,next.y,nextLayer));
            return;
        }
        e.blockTick=0;
        if(moved)e.walkAnim=e.walkAnim>=999999999?0:e.walkAnim+1;
        e.x=x;e.y=y;
        e.navLayer=(uint8_t)nextLayer;
        if(!keepCurve)e.driveArc={};
        ++e.pathIdx;
        e.moveTick=0;
        if(e.camouflaged && e.utype==UnitType::MirageTank){e.camouflaged=false;e.camoTick=0;}
        const auto identity=entityRef(id);
        pickupCrates(e);
        if(!valid(identity))return;
        unitOccSync(id);airOccSync(id);
        offerNativeCellEntry(id,next.x,next.y);
        if(sourceMovement && valid(identity)) {
            auto& moving=ents[id];
            if(moving.pathIdx>=(int)moving.path.size())moving.nativeMovement={};
            else if(nativeSegments>1 && moving.nativeMovement.credit>1e-8)moveAlongPath(moving,id,nativeSegments-1);
        }
    };
    if(e.driveArc.active() && (!driveArcMatchesRoute(e) || (e.driveArc.wide && (e.driveArc.layer!=nextLayer
        || actorSurfaceHeight(e)!=map.surfaceHeight(next.x,next.y,nextLayer)))))e.driveArc={};
    const auto arc=e.driveArc.active()?e.driveArc:planDriveArc(e,next,nextLayer,id);
    const auto entry=arc.active()?NativeDriveMath::position(arc,0):Vec2f{};
    if(arc.active() && (e.driveArc.active() || distf(e.x,e.y,entry.x,entry.y)<.00001f)) {
        // A wide arc crosses into its next cell before completing the node.
        // Check virtual occupancy as well as units before advancing the pose.
        if(arc.wide && cellHardBlockedForMove(next.x,next.y,id,nextLayer)) {
            releaseReservation();
            waitOrRepath(unitAtCell(next.x,next.y,nextLayer));
            if(e.driveArc.active() && !driveArcMatchesRoute(e))e.driveArc={};
            return;
        }
        e.driveArc=arc;
        if(!advanceDriveArc(e,aimTurret)){releaseReservation();return;}
        const auto point=NativeDriveMath::position(e.driveArc,e.driveArc.progress);
        if(e.driveArc.progress>=1)finishNode(point.x,point.y,true);
        else if(arc.wide && arc.progress<NativeDriveMath::wideHandoff
                && e.driveArc.progress==NativeDriveMath::wideHandoff)finishNode(point.x,point.y,true,true);
        else {
            e.x=point.x;e.y=point.y;e.blockTick=0;
            e.walkAnim=e.walkAnim>=999999999?0:e.walkAnim+1;
            unitOccSync(id);airOccSync(id);
        }
        return;
    }
    if(realTurning && std::hypot(next.x+.5f-e.x,next.y+.5f-e.y)>1e-5f) {
        const auto heading=NativeGroundMath::vector(next.x+.5f-e.x,next.y+.5f-e.y);
        desireNativeHull(e,heading);
        if(!aimTurret)desireNativeTurret(e,heading);
        constexpr int ordinaryAllowance = 8192;
        // A straight segment has no arc to follow during a turn. Let the
        // tracked hull align within one logic tick of steering before rolling.
        const int allowance = usesDriveArc(e) ? e.nativeIdentity.definition->production.rotationRate * 128 : ordinaryAllowance;
        if(std::abs(NativeGroundMath::delta(nativeHullHeading(e,tick),heading))>allowance) {
            e.nativeMovement={};releaseReservation();return;
        }
    }
    const float stopBefore=arc.active()?distf(entry.x,entry.y,next.x+.5f,next.y+.5f):0;
    if(sourceMovement)step=nativeMovementStep(e,next.x,next.y,nextLayer,e.path.back(),stopBefore);
    else if(e.nativeIdentity.definition && e.nativeIdentity.definition->walkSpeedLeptons>=0) {
        // Original Speed is percent -> 0..255 leptons per 15-Hz frame,
        // never the prototype's inverted integer ticks-per-cell value.
        const auto& profile=*e.nativeIdentity.definition;
        float leptons=profile.walkSpeedLeptons*profile.walkVeteranSpeed[std::clamp(e.vetRank,0,2)]*nativeCrateMultiplier(e.nativeIdentity,10);
        if(profile.rulesOnly)leptons*=(float)movementSpeedMultiplier(e.player);
        if(e.crateSpeedBoost>0)leptons*=1.25f;
        step=(int)leptons*(15.f/(256.f*LOGIC_FPS));
    }
    if(step<=0){releaseReservation();return;}
    float tx = next.x + 0.5f, ty = next.y + 0.5f;
    float dx = tx - e.x, dy = ty - e.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 1e-5f && !realTurning) {
        e.dir = dirFromVec(dx, dy);
        // 有炮塔：追击/攻击/攻击移动时炮塔由状态机对准目标；其余移动炮塔跟车头
        bool turAim = e.state == UState::Attacking || e.state == UState::Chasing
            || (e.state == UState::AttackMoving && e.target != INVALID_EID);
        if (!unitHasTurret(e.utype) || !turAim)
            e.turretDir = e.dir;
    }
    // 行走动画相位与移动同步（RA2 原作步态）
    if (ud.isInfantry() || e.utype == UnitType::TerrorDrone) {
        e.moveTick++;
        e.walkFrame = e.moveTick;
    } else {
        e.moveTick++;
    }
    // 落格前再检一次：防止两车同时滑入同格
    if (dist <= step + 1e-4f) {
        finishNode(tx,ty,dist>1e-4f);
    } else {
        const bool reachingArc=arc.active() && dist-stopBefore<=step+1e-5f;
        const float mx=reachingArc?entry.x:e.x+(dx/dist)*step;
        const float my=reachingArc?entry.y:e.y+(dy/dist)*step;
        const int sweptLayer=map.hasBridgeDecks()?movementStep(e,(int)e.x,(int)e.y,e.navLayer,(int)mx,(int)my):0;
        // Diagonal interpolation can enter an orthogonal cell before the path
        // node's cell. Reserve/check the actual swept cell as well as the node.
        if (sweptLayer>=0 && cellHardBlockedForMove((int)mx,(int)my,id,sweptLayer)) {
            releaseReservation();
            waitOrRepath(unitAtCell((int)mx,(int)my,sweptLayer));
            return;
        }
        if (sweptLayer<0 || !passableStep((int)e.x,(int)e.y,(int)mx,(int)my,ud.pathDomain(),e.navLayer)
            || bldBlocked((int)mx,(int)my,sweptLayer)) {
            releaseReservation();
            waitOrRepath(INVALID_EID);
            return;
        }
        e.blockTick = 0;
        e.walkAnim=e.walkAnim>=999999999?0:e.walkAnim+1;
        e.x = mx;
        e.y = my;
        e.navLayer=(uint8_t)sweptLayer;
        unitOccSync(id);
        airOccSync(id);
        if(reachingArc) {
            e.driveArc=arc;
            if(nativeSegments>1 && e.nativeMovement.credit>1e-8)moveAlongPath(e,id,nativeSegments-1);
        }
    }
}

