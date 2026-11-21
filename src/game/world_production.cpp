#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>

bool World::startUnitProd(int player, UnitType t) {
    if(productionStartRejection(player,true,(int)t)!=ProductionRejectReason::None) return false;
    if(usesRulesProfile()) {
        const int selector=productionSelector(player,true,(int)t),category=productionCategory(true,selector);
        auto& item=productionItem(player,true,selector);
        if(item.active)players[player].unitQueue[category].push_back(selector);else activateNativeProduction(player,item,selector);
        return true;
    }
    Player& p = players[player];
    const UnitDef& u = unitDef(t);
    int cat = u.prodCat();
    ProdItem& pr = p.unitProd[cat];
    if (!pr.active) {
        // RA2：缺钱也可开工，生产按 tick 扣款，钱不够则暂停进度
        pr.active = true;
        pr.isUnit = true;
        pr.typeIdx = (int)t;
        pr.progress = 0; pr.workRemainder = 0;
        pr.paid = 0;
        pr.held = false;
        pr.totalCost = unitProductionCost(player, t);
        pr.ready = false;
        return true;
    }
    // 排入队尾（RA2 约 30/类；含进行中）
    if ((int)p.unitQueue[cat].size() >= 29) return false;
    p.unitQueue[cat].push_back((int)t);
    return true;
}

void World::holdUnitProd(int player, UnitType t, bool hold) {
    if(usesRulesProfile()){Cmd command{};command.type=Cmd::HoldUnitProd;command.a=productionSelector(player,true,(int)t);command.b=hold;applyNativeProductionCommand(player,command);return;}
    Player& p = players[player];
    int cat = unitDef(t).prodCat();
    ProdItem& pr = p.unitProd[cat];
    if (pr.active && pr.typeIdx == (int)t && !pr.ready) pr.held = hold;
}

void World::holdBldProd(int player, BldType t, bool hold) {
    if(usesRulesProfile()){Cmd command{};command.type=Cmd::HoldBldProd;command.a=productionSelector(player,false,(int)t);command.b=hold;applyNativeProductionCommand(player,command);return;}
    Player& p = players[player];
    ProdItem& pr = isDefenseBld(t) ? p.defProd : p.bldProd;
    if (pr.active && pr.typeIdx == (int)t && !pr.ready) pr.held = hold;
}

int World::harvesterCapacity(UnitType t) {
    switch (t) {
        case UnitType::ChronoMiner: return 20; // CMIN Storage=20
        case UnitType::Harvester:
        case UnitType::WarMiner:   return 40; // HARV Storage=40
        case UnitType::Slave:      return 5;
        case UnitType::SlaveMiner: return 20;
        default: return 20;
    }
}

// 取消一个该类型：先取消队尾排队项，再取消进行中项（返还资金，队首递补）
void World::cancelUnitProd(int player, UnitType t) {
    if(usesRulesProfile()){Cmd command{};command.type=Cmd::CancelUnitProd;command.a=productionSelector(player,true,(int)t);applyNativeProductionCommand(player,command);return;}
    Player& p = players[player];
    int cat = unitDef(t).prodCat();
    auto& q = p.unitQueue[cat];
    for (auto it = q.rbegin(); it != q.rend(); ++it) {
        if (*it == (int)t) { q.erase(std::next(it).base()); return; }
    }
    ProdItem& pr = p.unitProd[cat];
    if (pr.active && pr.typeIdx == (int)t) {
        const int refund=pr.paid;
        pr = ProdItem{};
        changeMoney(player,refund);
        if (!q.empty()) {
            int nt = q.front(); q.pop_front();
            pr.active = true; pr.isUnit = true; pr.typeIdx = nt; pr.progress = 0; pr.workRemainder = 0;
            pr.paid = 0; pr.held = false;
            pr.totalCost = unitProductionCost(player, (UnitType)nt); pr.ready = false;
        }
    }
}

// 该类别排队总数（含进行中项）
int World::unitQueuedCount(int player, int cat) const {
    const Player& p = players[player];
    int n = (int)p.unitQueue[cat].size();
    if (p.unitProd[cat].active) n++;
    return n;
}

ProdItem& World::bldQueueFor(int player, BldType t) {
    if(usesRulesProfile()){const int selector=productionSelector(player,false,(int)t);if(selector>=0)return productionItem(player,false,selector);}
    return isDefenseBld(t) ? players[player].defProd : players[player].bldProd;
}
const ProdItem& World::bldQueueFor(int player, BldType t) const {
    if(usesRulesProfile()){const int selector=productionSelector(player,false,(int)t);if(selector>=0)return productionItem(player,false,selector);}
    return isDefenseBld(t) ? players[player].defProd : players[player].bldProd;
}

bool World::startBldProd(int player, BldType t) {
    if(productionStartRejection(player,false,(int)t)!=ProductionRejectReason::None) return false;
    if(usesRulesProfile()){const int selector=productionSelector(player,false,(int)t);activateNativeProduction(player,productionItem(player,false,selector),selector);return true;}
    ProdItem& pr = bldQueueFor(player, t);
    // RA2：建筑/防御双队列可并行；缺钱也可开工，按 tick 扣款
    pr.active = true;
    pr.isUnit = false;
    pr.typeIdx = (int)t;
    pr.progress = 0; pr.workRemainder = 0;
    pr.paid = 0;
    pr.held = false;
    pr.totalCost = buildingProductionCost(player,t);
    pr.ready = false;
    return true;
}

void World::cancelBldProd(int player, BldType t) {
    if(usesRulesProfile()){Cmd command{};command.type=Cmd::CancelBldProd;command.a=productionSelector(player,false,(int)t);applyNativeProductionCommand(player,command);return;}
    ProdItem& pr = bldQueueFor(player, t);
    if (!pr.active || pr.typeIdx != (int)t) return;
    const int refund=pr.paid;
    pr = ProdItem{};
    changeMoney(player,refund);
}

void World::cancelProd(int player, bool isUnit) {
    // 单位取消走 cancelUnitProd（分类队列）；此处取消活跃建筑队列（优先建筑栏）
    if (isUnit) return;
    Player& p = players[player];
    ProdItem* pr = nullptr;
    if (p.bldProd.active) pr = &p.bldProd;
    else if (p.defProd.active) pr = &p.defProd;
    if (!pr) return;
    const int refund=pr->paid;
    *pr = ProdItem{};
    changeMoney(player,refund);
}

int World::unitProductionCost(int player, UnitType t) const {
    if(usesRulesProfile()){const int type=rulesTypeIndex(player,true,(int)t);return type<0?0:nativeProductionCost(player,type);}
    const UnitDef& u = unitDef(t);
    bool vehicle = u.prodCat() == 1;
    bool plant = player >= 0 && player < numPlayers && hasBld(player, BldType::IndustrialPlant);
    return industrialPlantUnitCost(u.cost, vehicle, plant);
}

int World::buildingProductionCost(int player,BldType type) const {
    if(usesRulesProfile())return nativeProductionCost(player,rulesTypeIndex(player,false,(int)type));
    return bldDef(type).cost;
}

bool World::canPlace(BldType t, int bx, int by, int player) const {
    if(usesRulesProfile())return canPlaceNativeBuilding(player,productionSelector(player,false,(int)t),bx,by);
    const BldDef& d = bldDef(t);
    bool naval = (t == BldType::NavalYard); // 船厂必须全建于水面
    // 占地检查（地形 + 既有建筑 + 地面单位 —— RA2 不可压单位建造）
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int x = bx + dx, y = by + dy;
            if (naval) {
                if (!map.inBounds(x, y) || map.at(x, y).terrain != Terrain::Water) return false;
            } else {
                if (!map.passable(x, y)) return false;
                if (map.at(x,y).terrain==Terrain::Bridge || map.hasDeck(x,y))return false;
            }
            if (bldOcc[cellIdx(x, y)] > 0) return false;
            if (groundUnitBlocksCell(x, y, INVALID_EID)) return false;
        }
    // 必须靠近己方建筑（建造半径 6 格）
    bool nearBase = false;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player != player) continue;
        const BldDef& ed = bldDef(e);
        float cx = e.x + ed.w / 2.0f, cy = e.y + ed.h / 2.0f;
        float nx = bx + d.w / 2.0f, ny = by + d.h / 2.0f;
        if (distf(cx, cy, nx, ny) < 8.0f) { nearBase = true; break; }
    }
    return nearBase;
}

bool World::canDeployMcv(EID id) const {
    return valid(id) && canDeployMcvAt(id,(int)ents[id].x,(int)ents[id].y);
}

bool World::canDeployMcvAt(EID id,int x,int y) const {
    if (!valid(id) || ents[id].isBuilding || ents[id].utype != UnitType::MCV) return false;
    const Ent& e = ents[id];
    if(e.immobilized() || !map.inBounds(x,y)) return false;
    int bx = x - 1, by = y - 1;
    const int type=usesRulesProfile()?rulesTransformIndex(e):-1;
    if(usesRulesProfile() && (type<0 || !nativeTypes[type]->isBuilding || nativeTypes[type]->building.type!=BldType::ConYard))return false;
    const BldDef& d = type>=0?nativeTypes[type]->building:bldDef(BldType::ConYard);
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int cx = bx + dx, cy = by + dy;
            if (!map.inBounds(cx, cy)) return false;
            if (!map.passable(cx,cy) || map.at(cx,cy).terrain==Terrain::Bridge || map.hasDeck(cx,cy))return false;
            if (bldOcc[cellIdx(cx, cy)] > 0) return false;
            if (groundUnitBlocksCell(cx, cy, id)) return false;
        }
    return true;
}

bool World::placeBuilding(int player, BldType t, int bx, int by) {
    if(usesRulesProfile())return placeNativeBuilding(player,productionSelector(player,false,(int)t),bx,by);
    Player& p = players[player];
    ProdItem& pr = bldQueueFor(player, t);
    if (!pr.active || !pr.ready || pr.typeIdx != (int)t) return false;
    if (!canPlace(t, bx, by, player)) return false;
    const BldDef& d = bldDef(t);
    spawnBuilding(player, t, bx, by, true); // 钱已在生产中扣除
    pr = ProdItem{};
    if (p.placingBld == t) p.placingBld = BldType::COUNT;
    emitSoundAt(Sfx::Place, (float)bx + d.w / 2.0f, (float)by + d.h / 2.0f);
    // 精炼厂附赠一辆采矿车（RA2 原作设定）：出生在厂房旁空格，立即自动寻矿
    // 矿车型号按阵营：盟军超时空矿车 / 苏军武装矿车 / 中国普通矿车
    if (t == BldType::OreRefinery) {
        for (int r = 1; r < 6; r++) {
            int sx = bx + d.w / 2, sy = by + d.h + r - 1;
            if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                EID mid = spawnUnit(player, harvesterType(players[player].faction), sx + 0.5f, sy + 0.5f);
                if (mid != INVALID_EID) {
                    ents[mid].autoHarvest = true;
                    Vec2i ore;
                    if (findNearestReachableOre(sx, sy, 48, ore))
                        orderHarvest({mid}, ore.x, ore.y);
                }
                break;
            }
        }
    }
    return true;
}

void World::setRally(EID factory, int x, int y) {
    if (!valid(factory) || !ents[factory].isBuilding) return;
    if (!isRallyBuilding(ents[factory].btype)) return;
    if(!((x==-1 && y==-1) || map.inBounds(x,y))) return;
    ents[factory].rallyX = x;
    ents[factory].rallyY = y;
}

void World::sellBuilding(EID id) {
    if (!valid(id) || !ents[id].isBuilding) return;
    Ent& e = ents[id];
    if (e.player < 0 || e.selling || e.nativeTemporalHead!=INVALID_EID) return; // 中立/已在出售中不可再卖
    e.repairing = false;
    e.selling = true;
    // 倒放建造动画（与起楼同帧率）；无 mk 序列时用固定时长
    e.constructAnim = g_gameRules.buildingAnimationTicks;
    changeMoney(e.player,bldDef(e).cost/2);
    emitSoundAt(Sfx::Sell, e.x, e.y);
}

bool World::repairBuilding(EID id) {
    if (!valid(id) || !ents[id].isBuilding) return false;
    Ent& e = ents[id];
    if (e.player < 0 || e.selling || e.nativeTemporalHead!=INVALID_EID) return false; // 中立/出售中不可维修
    const BldDef& d = bldDef(e);
    if (e.hp >= d.hp) { e.repairing = false; return false; }
    // RA2：切换持续维修（每 tick 扣款回血），再次点击可取消
    e.repairing = !e.repairing;
    if (e.repairing) emitSoundAt(Sfx::Click, e.x, e.y);
    return e.repairing;
}

// ===================== 超级武器 =====================

std::vector<Vec2i> World::factoryExitCells(EID factory,const UnitDef& unit) const {
    std::vector<Vec2i> exits;
    if(!valid(factory) || !ents[factory].isBuilding)return exits;
    const auto& source=ents[factory];const auto& def=bldDef(source);
    const int x=(int)source.x,y=(int)source.y;
    const bool naval=source.btype==BldType::NavalYard;
    auto append=[&](int tx,int ty) {
        const int domain=naval?1:unit.pathDomain(),layer=naval?0:map.preferredLayer(tx,ty,domain);
        if(!map.surfacePassable(tx,ty,domain,layer,true,unit.fitsUnderBridge()))return;
        const Vec2i point{tx,ty};if(std::find(exits.begin(),exits.end(),point)==exits.end())exits.push_back(point);
    };
    const auto* native=source.nativeIdentity.definition.get();
    if(native && native->production.infantryExit>=0 && unit.isInfantry()) {
        if(native->production.infantryExit==1)append(x+1,y+2);
        else if(native->production.infantryExit==2)append(x+2,y+2);
        // Source barracks try their preferred cell, then their surrounding
        // cells. An authored barrel beside one door must not seal all exits.
        for(int dx=-1;dx<=def.w;++dx){append(x+dx,y-1);append(x+dx,y+def.h);}
        for(int dy=-1;dy<=def.h;++dy){append(x-1,y+dy);append(x+def.w,y+dy);}
        return exits;
    }
    append(x+def.w/2,y+def.h);
    if(naval) {
        // Every candidate shares an edge with the water-bound hull. Never
        // search beyond it or skip a blocking vessel to a remote water cell.
        for(int dx=0;dx<def.w;++dx)append(x+dx,y+def.h);
        for(int dy=def.h-1;dy>=0;--dy)append(x+def.w,y+dy);
        for(int dx=def.w-1;dx>=0;--dx)append(x+dx,y-1);
        for(int dy=0;dy<def.h;++dy)append(x-1,y+dy);
    } else if(unit.isAir())for(int distance=1;distance<7;++distance)append(x+def.w/2,y+def.h+distance);
    return exits;
}

bool World::spawnFromFactory(int player, const UnitDef& u,int nativeType,EID factory,EID* produced) {
    const bool source=nativeProductionRules.version>=3 && nativeType>=0;
    const int veteranCategory=source && u.prodCat()==3?1:u.prodCat();
    const bool veteran=players[player].vetCat[veteranCategory]
        && (!source || nativeTypes[nativeType]->trainable);
    // 找第一个对应工厂
    for (size_t i = 0; i < ents.size(); i++) {
        if(factory>=0 && (EID)i!=factory)continue;
        Ent& b = ents[i];
        if (!productionFacilityOperational(b) || b.player != player || (nativeType>=0?!nativeFactorySupports(b,nativeType):!isFactoryFor(b.btype,u))) continue;
        const BldDef& bd = bldDef(b);
        // Keep rally coordinates stable across entity creation.
        const int rallyX = b.rallyX, rallyY = b.rallyY;
        // 限弹药战机：直接落在空指部停机位（基洛夫/火箭飞行兵无限弹药，走地面出厂流程后升空）
        if (u.isAir() && u.ammo > 0) {
            if(!airfieldHasSpace((EID)i))continue;
            EID nu = spawnProductionActor(player,u.type,0,0,nativeType);if(!valid(nu))return false;
            Ent& ne = ents[nu];
            ne.airbase = (int)i;
            Vec2f pad = airPadPos(ents[i], nu);
            ne.x = ne.px = pad.x; ne.y = ne.py = pad.y;
            ne.goalX = pad.x; ne.goalY = pad.y;
            ne.state = UState::Landed;
            ne.ammo = u.ammo;
            initializeFactoryVeterancy(ne,veteran);
            unitOccSync(nu);airOccSync(nu);
            applyFactoryRally(nu,(EID)i,rallyX,rallyY);
            if(produced)*produced=nu;
            recordNativeBuilt(player,nativeType);
            return true;
        }
        // Land factories keep their fixed door; shipyards launch from any
        // adjacent water edge, including shores facing another direction.
        for(const auto exit:factoryExitCells((EID)i,u)) {
            const int sx=exit.x,sy=exit.y;
            const int layer=b.btype==BldType::NavalYard?0:map.preferredLayer(sx,sy,u.pathDomain());
            if (unitAtCell(sx,sy,layer) == INVALID_EID) {
                EID nu = spawnProductionActor(player,u.type,sx+.5f,sy+.5f,nativeType);if(!valid(nu))return false;
                recordNativeBuilt(player,nativeType);
                Ent& ne0 = ents[nu];
                if(!u.isAir()){ne0.navLayer=ne0.previousNavLayer=ne0.goalLayer=(uint8_t)layer;unitOccSync(nu);}
                initializeFactoryVeterancy(ne0,veteran);
                // 复制中心（RA2 原作）：兵营造步兵时，复制中心旁免费复制一个同类型
                if (u.isInfantry() && (nativeProductionRules.version<3 || nativeType<0 || nativeTypes[nativeType]->production.cloneable)) {
                    for (size_t ci = 0; ci < ents.size(); ci++) {
                        Ent& cv = ents[ci];
                        if (!productionFacilityOperational(cv) || cv.player != player)continue;
                        if(nativeProductionRules.version>=3 && cv.nativeIdentity.definition) {
                            const auto& profile=cv.nativeIdentity.definition->production;
                            if(!profile.cloning || (profile.powered && players[player].lowPower()))continue;
                        } else if(cv.btype!=BldType::CloningVat)continue;
                        const BldDef& cbd = bldDef(cv);
                        const int cloneRallyX=cv.rallyX,cloneRallyY=cv.rallyY;
                        for (int r = 1; r < 6; r++) {
                            int cx2 = (int)cv.x + cbd.w / 2, cy2 = (int)cv.y + cbd.h + r - 1;
                            if (map.passable(cx2, cy2) && !bldBlocked(cx2, cy2) && unitAtCell(cx2, cy2) == INVALID_EID) {
                                EID dup = spawnProductionActor(player,u.type,cx2+.5f,cy2+.5f,nativeType);if(!valid(dup))break;
                                initializeFactoryVeterancy(ents[dup],veteran);
                                applyFactoryRally(dup,(EID)ci,cloneRallyX,cloneRallyY);
                                r = 99; break;
                            }
                        }
                        break; // 一座复制中心即可生效
                    }
                }
                // 走向集结点（用缓存的 rally：spawnUnit 后 b 已悬空）
                // 采矿车：出厂即自动寻矿（勿只走集结点后空闲，武装矿车 Idle 还会优先索敌）
                Ent& ne = ents[nu];
                if (u.canHarvet()) {
                    if(produced)*produced=nu;
                    ne.autoHarvest = true;
                    Vec2i ore;
                    if (findNearestReachableOre(sx, sy, 48, ore))
                        orderHarvest({nu}, ore.x, ore.y);
                    return true;
                }
                applyFactoryRally(nu,(EID)i,rallyX,rallyY);
                if(produced)*produced=nu;
                return true;
            }
        }
    }
    return false; // 所有合法出口均堵塞：保留完成项，待下一帧重试
}

void World::recomputePower(bool syncAbilities) {
    updateNativeHouseControl();
    for (auto& p : players) { p.powerMade = 0; p.powerUsed = 0; }
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player < 0 || e.nativeTemporalHead!=INVALID_EID) continue;
        int pw = bldDef(e).power;
        const bool source=nativeProductionRules.version>=3 && e.nativeIdentity.definition;
        if (!source && pw > 0 && e.drainedBy != INVALID_EID) pw = 0;
        if (source
            ?e.nativeIdentity.definition->production.infantryAbsorb || e.nativeIdentity.definition->production.unitAbsorb
            :e.btype==BldType::BioReactor)
            pw += (int)e.garrison.size() * (source?e.nativeIdentity.definition->production.extraPower:g_gameRules.bioReactorPowerPerOccupant);
        if (source && pw > 0 && e.drainedBy != INVALID_EID) pw = 0;
        if (pw > 0) players[e.player].powerMade += pw;
        else players[e.player].powerUsed -= pw;
    }
    if(syncAbilities)syncSuperweaponSources();
}

void World::checkDefeat() {
    // Original campaign houses may begin empty or hand over their entire base.
    // Their result belongs to the authored mission events, not skirmish loss.
    if(!nativeScenarioSource.empty() && !usesRulesProfile())return;
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        bool hasAny = false;
        for (const Ent& e : ents) {
            if (!e.alive || e.player != pi) continue;
            if (!shortGame || e.isBuilding || e.utype == UnitType::MCV) { hasAny = true; break; }
        }
        if (!hasAny) {
            p.defeated = true;
            if (g_script.boundTo(this)) g_script.onPlayerDefeated(pi);
        }
    }
}

