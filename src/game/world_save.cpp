#include "game/world.h"
#include "game/actor_contents.h"
#include "game/native_scene_contract.h"
#include "core/binary_state_io.h"
#include "game/world_save_format.h"
#include "game/lang.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdio>

// ===================== 存档/读档 =====================
namespace {
using Passenger=World::Ent::GarrisonedUnit;
constexpr uint32_t maxOwnerRecords=1000000;
bool passengerOwners(const World& w,uint32_t& count,bool& explicitOwners) {
    bool valid=true;uint64_t total=0;explicitOwners=false;
    visitStoredPassengers(w,[&](const auto& p) {
        ++total;explicitOwners |= p.player!=-2;
        valid &= p.player>=-2 && p.player<w.numPlayers;
    });
    count=total>maxOwnerRecords?maxOwnerRecords+1:static_cast<uint32_t>(total);
    return valid && (!explicitOwners || (total<=maxOwnerRecords && w.nativeRulesBaseVersion>=nativeCargoOwnerSceneVersion));
}
void writePassengerOwners(BinaryStateStream& s,const World& w,uint32_t count) {
    s.writeBytes("COW1",4);s.write(count);
    visitStoredPassengers(w,[&](const auto& p){s.write(p.player);});
    const uint32_t length=8+count*4;s.write(length);
}
void writeTransportUnloads(BinaryStateStream& s,const World& w,uint32_t count) {
    s.writeBytes("TUN1",4);s.write(count);
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.ents[id].nativeTransportUnload.active()) {
        const auto& e=w.ents[id];s.write(id);s.write(e.generation);
        s.write(static_cast<uint8_t>(e.nativeTransportUnload.phase));s.write(e.nativeTransportUnload.ticks);
    }
    s.write(uint32_t{8+17*count});
}
void writeBuildingSales(BinaryStateStream& s,const World& w) {
    uint32_t count=0;for(const auto& e:w.ents)if(e.alive && e.nativeBuildingSale.originalSide!=-2)++count;
    s.writeBytes("SLS1",4);s.write(count);
    for(EID id=0;id<(EID)w.ents.size();++id) {
        const auto& e=w.ents[id];const auto& state=e.nativeBuildingSale;
        if(!e.alive || state.originalSide==-2)continue;
        s.write(id);s.write(e.generation);s.write(state.originalSide);
        s.write(uint8_t{static_cast<uint8_t>((state.captured?1:0)|(state.pending?2:0))});
    }
    s.write(uint32_t{8+14*count});
}
bool readBuildingSales(BinaryStateStream& s,World& w) {
    char tag[4]{};uint32_t count=0,length=0;EID previous=INVALID_EID;
    s.readBytes(tag,4);s.read(count);
    if(!s.ok || std::memcmp(tag,"SLS1",4) || count>w.ents.size() || !w.nativeSaleSurvivorRules.enabled)return false;
    for(uint32_t i=0;i<count;++i) {
        EID id=INVALID_EID;uint64_t generation=0;int8_t side=-2;uint8_t flags=0;
        s.read(id);s.read(generation);s.read(side);s.read(flags);
        if(!s.ok || id<=previous || !w.valid(World::EntityRef{id,generation}) || side< -1 || side>2 || flags>3)return false;
        previous=id;w.ents[id].nativeBuildingSale={side,(flags&1)!=0,(flags&2)!=0};
    }
    s.read(length);return s.ok && length==8+14*count;
}
bool readTransportUnloads(BinaryStateStream& s,World& w) {
    char tag[4]{};uint32_t count=0,length=0;EID previous=INVALID_EID;
    s.readBytes(tag,4);s.read(count);
    if(!s.ok || std::memcmp(tag,"TUN1",4) || !count || count>w.ents.size()
        || w.nativeRulesBaseVersion<nativeTransportUnloadSceneVersion)return false;
    for(uint32_t i=0;i<count;++i) {
        EID id=INVALID_EID;uint64_t generation=0;uint8_t phase=0;int ticks=0;
        s.read(id);s.read(generation);s.read(phase);s.read(ticks);
        if(!s.ok || id<=previous || !w.valid(World::EntityRef{id,generation})
            || phase<static_cast<uint8_t>(NativeTransportUnloadPhase::Prepare)
            || phase>static_cast<uint8_t>(NativeTransportUnloadPhase::Finish))return false;
        previous=id;w.ents[id].nativeTransportUnload={static_cast<NativeTransportUnloadPhase>(phase),ticks};
    }
    s.read(length);return s.ok && length==8+17*count;
}
bool readPassengerOwners(BinaryStateStream& s,World& w) {
    char tag[4]{};uint32_t count=0,expected=0,length=0;bool explicitOwners=false;
    s.readBytes(tag,4);s.read(count);
    if(!s.ok || std::memcmp(tag,"COW1",4) || count>maxOwnerRecords
        || w.nativeRulesBaseVersion<nativeCargoOwnerSceneVersion
        || !passengerOwners(w,expected,explicitOwners) || count!=expected)return false;
    visitStoredPassengers(w,[&](auto& p){s.read(p.player);});
    s.read(length);
    return s.ok && length==8+count*4 && passengerOwners(w,expected,explicitOwners) && explicitOwners;
}
void serPassengerExtra(BinaryStateStream& s,const Passenger& p) {
    s.write(p.oreLoad);
    s.write(p.gemLoad);
    s.write(p.ammo);
    s.write(p.atkCd);
    s.write(p.invuln);
    s.write(p.crateDmgBoost);
    s.write(p.crateArmorBoost);
    s.write(p.crateSpeedBoost);
    s.write(p.airstrikeCd);
    s.write(p.confused);
    s.write(p.gatlingHeat);
    s.write(p.gatlingStage);
    s.write(p.autoHarvest);
    s.write(p.permaControlled);
    uint32_t count=(uint32_t)p.cargo.size();s.write(count);
    for(const auto& child:p.cargo) {
        uint8_t type=(uint8_t)child.type;s.write(type);
        s.write(child.hp);
        s.write(child.kills);
        s.write(child.veterancyValue);
        s.write(child.vetRank);
        serPassengerExtra(s,child);
    }
}
void deserPassengerExtra(BinaryStateStream& s,Passenger& p,int depth=0) {
    s.read(p.oreLoad);
    s.read(p.gemLoad);
    s.read(p.ammo);
    s.read(p.atkCd);
    s.read(p.invuln);
    s.read(p.crateDmgBoost);
    s.read(p.crateArmorBoost);
    s.read(p.crateSpeedBoost);
    s.read(p.airstrikeCd);
    s.read(p.confused);
    s.read(p.gatlingHeat);
    s.read(p.gatlingStage);
    s.read(p.autoHarvest);
    s.read(p.permaControlled);
    uint32_t count=0;s.read(count);
    if(!s.ok || count>64 || (depth>=2 && count)) {s.ok=false;return;}
    p.cargo.resize(count);
    for(auto& child:p.cargo) {
        uint8_t type=0;s.read(type);
        if(type>=(uint8_t)UnitType::COUNT){s.ok=false;return;}
        child.type=(UnitType)type;
        s.read(child.hp);
        s.read(child.kills);
        s.read(child.veterancyValue);
        s.read(child.vetRank);
        deserPassengerExtra(s,child,depth+1);if(!s.ok)return;
    }
}
bool validPassenger(const Passenger& p,int depth=0) {
    if(p.type==UnitType::NativeInfantry && (!p.nativeIdentity.definition || p.nativeIdentity.definition->unit.type!=p.type))return false;
    if((int)p.type<0 || p.type>=UnitType::COUNT || p.hp<0 || p.kills<0 || p.veterancyValue<0 || p.vetRank<0 || p.vetRank>2
        || p.oreLoad<0 || p.gemLoad<0 || p.gemLoad>p.oreLoad || p.ammo<0 || p.atkCd<0 || p.invuln<0
        || p.crateDmgBoost<0 || p.crateArmorBoost<0 || p.crateSpeedBoost<0 || p.airstrikeCd<0 || p.confused<0
        || p.gatlingHeat<0 || p.gatlingStage<0 || p.gatlingStage>2 || p.cargo.size()>64 || (depth>=2 && !p.cargo.empty()))return false;
    for(const auto& child:p.cargo)if(!validPassenger(child,depth+1))return false;
    const auto& def=unitDef(p);
    if(!p.cargo.empty() && World::cargoLoad(p.cargo)>def.cargoCap)return false;
    for(const auto& child:p.cargo)if(unitDef(child).transportSize()>def.cargoSizeLimit)return false;
    return true;
}

// projSprite 是 const char*：存档写字符串，读档映射回静态字面量（悬空指针防护）
static const char* kProjSprites[] = {
    "shell", "bullet", "flak", "tesla", "prism", "missile", "psi", "rad", "chrono", "naval", "torpedo"
};
void serWeapon(BinaryStateStream& s, const WeaponDef& wd) {
    s.write(wd.damage); s.write(wd.range); s.write(wd.cooldown);
    s.write(wd.antiAir); s.write(wd.antiGround);
    std::string ps = wd.projSprite ? wd.projSprite : "shell";
    s.writeText(ps);
    s.write(wd.vsInfantry); s.write(wd.vsVehicle); s.write(wd.vsBuilding);
    s.write(wd.navalOnly); s.write(wd.splash);
    uint8_t wh = (uint8_t)wd.warhead; s.write(wh);
    s.write(wd.cellSpread);s.write(wd.percentAtMax);
}
void deserWeapon(BinaryStateStream& s, WeaponDef& wd, bool schema7, bool fractionalRange,bool withArea) {
    wd.nativeVerses=nullptr;wd.nativeSource=nullptr;wd.nativeType=wd.nativeSlot=-1;
    s.read(wd.damage);
    if(fractionalRange)s.read(wd.range);else{int range=0;s.read(range);wd.range=(float)range;}
    s.read(wd.cooldown);
    s.read(wd.antiAir); s.read(wd.antiGround);
    std::string ps; s.readText(ps);
    wd.projSprite = "shell";
    for (const char* k : kProjSprites)
        if (ps == k) { wd.projSprite = k; break; }
    s.read(wd.vsInfantry); s.read(wd.vsVehicle); s.read(wd.vsBuilding);
    s.read(wd.navalOnly); s.read(wd.splash);
    if (schema7) {
        uint8_t wh = 0; s.read(wh); wd.warhead = (WeaponDef::Warhead)wh;
    } else {
        wd.warhead = WeaponDef::Warhead::Legacy;
    }
    if(withArea){s.read(wd.cellSpread);s.read(wd.percentAtMax);}
}
void serProd(BinaryStateStream& s, const ProdItem& p) {
    s.write(p.active); s.write(p.isUnit); s.write(p.typeIdx); s.write(p.progress);
    s.write(p.totalCost); s.write(p.paid); s.write(p.ready); s.write(p.held); s.write(p.workRemainder);
}
void deserProd(BinaryStateStream& s, ProdItem& p, bool withHeld, bool withWork) {
    s.read(p.active); s.read(p.isUnit); s.read(p.typeIdx); s.read(p.progress);
    s.read(p.totalCost); s.read(p.paid); s.read(p.ready);
    if (withHeld) s.read(p.held); else p.held = false;
    if (withWork) s.read(p.workRemainder); else p.workRemainder = 0;
}
} // namespace

bool World::saveGame(FILE* f) const {
    if(!validateNativeTransportUnload() || !validateNativeBuildingSales())return false;
    if(!validateNativeProductionState() || !validateNativeAIState() || !validateNativeAIGlobalState() || !validateNativeCrateState() || !validateNativeConditionState()
        || !validateSupportState() || !validateSupportDelivery() || !validateNativeCellOrder() || !validateNativeTemporalState() || !validateNativeCrewState())return false;
    if(updateInProgress || entityMutationDepth>0 || areaDamageDepth>0 || !retiredDuringUpdate.empty()) return false;
    BinaryStateStream s{f};
    if (!f || numPlayers <= 0 || numPlayers > MAX_PLAYERS || players.size() != (size_t)numPlayers
        || map.w <= 0 || map.h <= 0 || map.cells.size() != (size_t)map.w * map.h
        || map.fog.size() != (size_t)numPlayers || bldOcc.size() != map.cells.size()
        || ents.size() > 65536 || freeList.size() > 65536 || projs.size() > 8192
        || effects.size() > 8192 || nukes.size() > 64 || crates.size() > 1024
        || timedBombs.size() > 1024 || evaQueue.size() > 256) return false;
    for (const auto& fog : map.fog)
        if (fog.size() != map.cells.size()) return false;
    for(int player=0;player<numPlayers;++player)
        if(players[player].money<0 || players[player].money>MAX_CREDIT_BALANCE ||
            reservedProductionFunds(player)>MAX_CREDIT_BALANCE-players[player].money) return false;
    for (const Player& p : players)
        for (int c = 0; c < PROD_CAT_N; ++c)
            if (p.unitQueue[c].size() > 64) return false;
    for (const Ent& e : ents)
        if (e.path.size() > 4096 || e.wps.size() > 4096 || e.cargo.size() > 64
            || e.garrison.size() > 64 || e.mindTargets.size() > 64 || e.failedPursuits.size()>8) return false;
    for(const auto& e:ents) {
        for(const auto& passenger:e.cargo)if(!validPassenger(passenger))return false;
        for(const auto& passenger:e.garrison)if(!validPassenger(passenger))return false;
    }
    if(!validateNativeMap(map))return false;
    if(!nativeEventDispatchTags.empty())return false;
    uint32_t redirectCount=0;
    bool dockApproach=false;
    for(const auto& e:ents)if(e.hasPendingDockReturn()) {
        ++redirectCount;
        dockApproach |= e.nativeDock.phase==NativeDockPhase::Approach;
    }
    const bool forwardDockSave=requiresForwardDockSave();
    int activeWideRevision=0;
    for(const auto& e:ents) {
        if(!e.alive || !e.driveArc.wide || !driveArcMatchesRoute(e))continue;
        activeWideRevision=e.driveArc.cubic?WorldSaveFormat::cubicWideDriveRevision:WorldSaveFormat::wideDriveRevision;
        if(e.driveArc.cubic)break;
    }
    int saveRevision=WorldSaveFormat::ordinaryRevision(nativeProductionRules.version);
    if(activeWideRevision==WorldSaveFormat::cubicWideDriveRevision)saveRevision=activeWideRevision;
    else if(requiresDriveExitSave())saveRevision=WorldSaveFormat::driveExitRevision;
    else if(requiresDepartureWaitSave())saveRevision=WorldSaveFormat::departureWaitRevision;
    else if(requiresUnloadStartupSave())saveRevision=WorldSaveFormat::unloadStartupRevision;
    else if(requiresChronoYieldSave())saveRevision=WorldSaveFormat::chronoYieldRevision;
    else if(requiresChronoBerthSave())saveRevision=WorldSaveFormat::chronoBerthRevision;
    else if(activeWideRevision)saveRevision=activeWideRevision;
    else if(forwardDockSave)saveRevision=WorldSaveFormat::forwardDockRevision;
    else if(redirectCount)saveRevision=dockApproach?WorldSaveFormat::dockApproachRevision:WorldSaveFormat::dockRedirectRevision;
    else if(nativeAIRules.isGlobal())saveRevision=nativeAIRules.globalContract->worldVersion;
    const bool requiresTravelTail=WorldSaveFormat::requiresTravelAndDockRedirects(saveRevision);
    if((redirectCount || requiresTravelTail) && (!nativeGroundEnabled() || nativeAIRules.isGlobal()))return false;
    if(requiresTravelTail && !nativeTravelEnabled())return false;
    uint32_t ownerRecords=0;bool hasCargoOwners=false;
    if(!passengerOwners(*this,ownerRecords,hasCargoOwners)
        || (hasCargoOwners && !WorldSaveFormat::cargoOwnerBaseRevision(saveRevision)))return false;
    uint32_t unloadCount=0;for(const auto& e:ents)if(e.nativeTransportUnload.active())++unloadCount;
    if(unloadCount && !WorldSaveFormat::cargoOwnerBaseRevision(saveRevision))return false;
    const bool buildingSaleSave=nativeSaleSurvivorRules.enabled;
    const auto* saveFormat=WorldSaveFormat::forRevision(buildingSaleSave?WorldSaveFormat::buildingSaleRevision:unloadCount?WorldSaveFormat::transportUnloadRevision:
        hasCargoOwners?WorldSaveFormat::cargoOwnerRevision:saveRevision);
    if(!saveFormat)return false;
    s.writeBytes(saveFormat->magic.data(),saveFormat->magic.size());
    if(buildingSaleSave || unloadCount || hasCargoOwners)s.write(saveRevision);
    if(buildingSaleSave)s.write(uint8_t{static_cast<uint8_t>((hasCargoOwners?1:0)|(unloadCount?2:0))});
    else if(unloadCount)s.write(static_cast<uint8_t>(hasCargoOwners));
    s.write(tick); s.write(numPlayers); s.write(rng.s);
    s.write(cratesEnabled); s.write(aiAlliance);
    uint8_t mode = (uint8_t)skirmishMode;
    s.write(mode); s.write(sharedVision); s.write(shortGame); s.write(superweaponsEnabled);
    s.write(mcvRepacks);
    // 地图（含矿石余量、格高度与迷雾）
    s.write(map.w); s.write(map.h);
    for (const Cell& c : map.cells) {
        uint8_t t = (uint8_t)c.terrain, o = (uint8_t)c.overlay;
        s.write(t); s.write(o); s.write(c.variant); s.write(c.ore); s.write(c.oreMax); s.write(c.height);
        const uint8_t flags=(c.present?1:0)|(c.blocked?2:0);s.write(flags);
    }
    if(s.ok)s.ok=writeNativeMapState(f,map);
    for (int p = 0; p < numPlayers; p++)
        s.writeBytes(map.fog[p].data(), map.fog[p].size());
    s.writeBytes(bldOcc.data(), bldOcc.size() * sizeof(int));
    // 玩家
    for (const Player& p : players) {
        s.write(p.active); s.write(p.isAI); s.write(p.defeated);
        uint8_t fac = (uint8_t)p.faction;
        s.write(fac);
        uint8_t ctry = (uint8_t)p.country;
        s.write(ctry);
        s.write(p.secretLabUnlock); s.write(p.paradropCharge); s.write(p.paradropReady);
        s.write(p.colorId); s.write(p.money); s.writeText(p.name);
        serProd(s, p.bldProd);
        serProd(s, p.defProd);
        for (int c = 0; c < PROD_CAT_N; c++) serProd(s, p.unitProd[c]);
        for (int c = 0; c < PROD_CAT_N; c++) {
            uint32_t n = (uint32_t)p.unitQueue[c].size();
            s.write(n);
            for (int t : p.unitQueue[c]) s.write(t);
        }
        uint8_t pb = (uint8_t)p.placingBld;
        s.write(pb);
        s.write(p.powerSabotage); s.write(p.revealTimer);
        for (int c = 0; c < PROD_CAT_N; c++) s.write(p.vetCat[c]);
        s.write(p.stolenTech);
        s.write(p.aiDifficulty);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.write(p.swCharge[i]);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.write(p.swReady[i]);
        s.write(p.stormTimer); s.write(p.stormX); s.write(p.stormY); s.write(p.stormBoltCd);
        s.write(p.evaBaseCd); s.write(p.evaMinerCd); s.write(p.evaUnitCd);
    }
    // 实体
    {
        uint32_t n = (uint32_t)ents.size();
        s.write(n);
        for (const Ent& e : ents) {
            s.write(e.alive); s.write(e.isBuilding); s.write(e.player);
            uint8_t ut = (uint8_t)e.utype, bt = (uint8_t)e.btype, st = (uint8_t)e.state;
            s.write(ut); s.write(bt);
            s.write(e.x); s.write(e.y); s.write(e.dir); s.write(e.turretDir); s.write(e.hp);
            uint32_t pn = (uint32_t)e.path.size();
            s.write(pn);
            for (const Vec2i& wp : e.path) { s.write(wp.x); s.write(wp.y); }
            uint32_t wn = (uint32_t)e.wps.size();
            s.write(wn);
            for (const auto& wp : e.wps) { s.write(wp.first); s.write(wp.second); }
            s.write(e.pathIdx); s.write(e.moveTick); s.write(e.blockTick);
            s.write(e.walkFrame); s.write(e.walkAnim);
            s.write(e.fireAnim); s.write(e.constructAnim); s.write(e.deployAnim);
            s.write(e.burstRemaining);s.write(e.burstDelay);s.write(e.burstOwner);s.write(e.burstTarget);s.write(e.burstAir);
            s.write(st); s.write(e.atkCd); s.write(e.target); s.write(e.goalX); s.write(e.goalY);
            uint32_t failures=(uint32_t)e.failedPursuits.size();s.write(failures);
            for(const auto& failure:e.failedPursuits) {
                s.write(failure.target);s.write(failure.sourceX);s.write(failure.sourceY);
                s.write(failure.targetX);s.write(failure.targetY);s.write(failure.retryTick);
            }
            uint8_t engineerIntent=(uint8_t)e.engineerIntent;s.write(engineerIntent);
            s.write(e.oreLoad); s.write(e.gemLoad); s.write(e.oreCell.x); s.write(e.oreCell.y); s.write(e.dockRefinery); s.write(e.digTimer);
            s.write(e.autoHarvest);
            s.write(e.invuln);
            s.write(e.ammo); s.write(e.rearmTimer); s.write(e.airbase); s.write(e.orbitA);
            s.write(e.rallyX); s.write(e.rallyY); s.write(e.bldAnim); s.write(e.undeploy); s.write(e.guard); s.write(e.repairing); s.write(e.selling);
            uint32_t cn = (uint32_t)e.cargo.size();
            s.write(cn);
            for (const Ent::GarrisonedUnit& cu : e.cargo) {
                uint8_t ct = (uint8_t)cu.type; s.write(ct);
                s.write(cu.hp); s.write(cu.kills); s.write(cu.veterancyValue); s.write(cu.vetRank);
                serPassengerExtra(s,cu);
            }
            s.write(e.chrono); s.write(e.tpSick); s.write(e.camouflaged); s.write(e.camoTick);
            s.write(e.radDeployed); s.write(e.deployed); s.write(e.subReveal);
            s.write(e.kills); s.write(e.veterancyValue); s.write(e.vetRank);
            s.write(e.crateDmgBoost); s.write(e.crateArmorBoost); s.write(e.crateSpeedBoost);
            // 驻军/寄生/磁暴充电
            uint32_t gn = (uint32_t)e.garrison.size();
            s.write(gn);
            for (const Ent::GarrisonedUnit& gu : e.garrison) {
                uint8_t gt = (uint8_t)gu.type; s.write(gt);
                s.write(gu.hp); s.write(gu.kills); s.write(gu.veterancyValue); s.write(gu.vetRank);
                serPassengerExtra(s,gu);
            }
            s.write(e.parasite); s.write(e.parasiteHost); s.write(e.parasiting); s.write(e.teslaCharge);
            // P6 心灵控制
            s.write(e.mindBy); s.write(e.mindTarget);
            uint32_t mn = (uint32_t)e.mindTargets.size(); s.write(mn);
            for (EID tid : e.mindTargets) s.write(tid);
            s.write(e.origPlayer); s.write(e.permaControlled);
            // 尤复补全：YR 新单位特殊机制
            s.write(e.airstrikeCd); s.write(e.confused);
            s.write(e.magneticBy); s.write(e.magneticHeight); s.write(e.drainedBy);
            s.write(e.gatlingHeat); s.write(e.gatlingStage);
        }
        uint32_t fn = (uint32_t)freeList.size();
        s.write(fn);
        for (int id : freeList) s.write(id);
    }
    // 投射物 / 特效 / 核弹 / 补给箱 / 定时炸弹 / EVA 队列
    {
        uint32_t n = (uint32_t)projs.size();
        s.write(n);
        for (const Projectile& p : projs) {
            s.write(p.alive);
            uint8_t k = (uint8_t)p.kind;
            s.write(k);
            s.write(p.player); s.write(p.x); s.write(p.y); s.write(p.tx); s.write(p.ty);
            s.write(p.target); s.write(p.src); s.write(p.srcGarrisonSlot);
            serWeapon(s, p.w);
            s.write(p.speed); s.write(p.trail); s.write(p.hp);
            s.write(p.ox); s.write(p.oy); s.write(p.z); s.write(p.rot); s.write(p.arcing); s.write(p.proximity);
        }
    }
    {
        uint32_t n = (uint32_t)effects.size();
        s.write(n);
        for (const Effect& e : effects) {
            s.write(e.alive); s.write(e.kind); s.write(e.x); s.write(e.y); s.write(e.x2); s.write(e.y2); s.write(e.age); s.write(e.maxAge);
            s.write(e.aux); s.write(e.aux2); s.write(e.aux3);
        }
    }
    {
        uint32_t n = (uint32_t)nukes.size();
        s.write(n);
        for (const Nuke& nk : nukes) { s.write(nk.active); s.write(nk.player); s.write(nk.tx); s.write(nk.ty); s.write(nk.timer); }
    }
    {
        uint32_t n = (uint32_t)crates.size();
        s.write(n);
        for (const Crate& c : crates) { s.write(c.alive); s.write(c.x); s.write(c.y); s.write(c.kind); }
    }
    {
        uint32_t n = (uint32_t)timedBombs.size();
        s.write(n);
        for (const TimedBomb& b : timedBombs) { s.write(b.x); s.write(b.y); s.write(b.timer); s.write(b.player); s.write(b.attachedTo); s.write(b.dmg); s.write(b.radius); s.write(b.rockVehicles); }
    }
    {
        uint32_t n = (uint32_t)evaQueue.size();
        s.write(n);
        for (const EvaEvent& ev : evaQueue) { s.write(ev.player); s.writeText(ev.text); }
    }
    if(trafficGates.size()>196608 || trafficGates.size()>map.cells.size()*3) return false;
    s.write((uint32_t)trafficGates.size());
    for(const auto& gate:trafficGates) { s.write(gate.domain);s.write(gate.anchor);s.write(gate.exit);s.write(gate.until);s.write((uint8_t)gate.draining); }
    if(s.ok)s.ok=writeNativeActors(f);
    if(s.ok)s.ok=writeBridgeState(f);
    if(s.ok)s.ok=writeNativeGateState(f);
    if(s.ok)s.ok=writeNativeEventState(f);
    if(s.ok)s.ok=writeNativeRocketState(f);
    if(s.ok)s.ok=writeNativeProductionState(f);
    if(s.ok)s.ok=writeNativeAIState(f);
    if(s.ok)s.ok=writeNativeCrateState(f);
    if(s.ok)s.ok=writeNativeConditionState(f);
    if(s.ok)s.ok=writeAttackMoveState(f);
    if(s.ok)s.ok=writeNativeMovementState(f);
    if(s.ok)s.ok=writeEntityGenerations(f);
    if(s.ok)s.ok=writeAircraftState(f);
    s.write(uint32_t{1});s.write((uint8_t)aswEnabled);s.write(uint32_t{5});
    if(s.ok)s.ok=writeNativeFlightState(f);
    if(s.ok)s.ok=writeNativeJumpjetState(f);
    if(s.ok)s.ok=writeNativeAirNavigation(f);
    if(s.ok)s.ok=writeSourceCrateLifetimes(f);
    if(s.ok)s.ok=writeSupportState(f);
    if(s.ok)s.ok=writeSupportDelivery(f);
    if(s.ok)s.ok=writeNativeCellOrder(f);
    if(s.ok)s.ok=writeNativeTemporalState(f);
    if(s.ok)s.ok=writeNativeCrewState(f);
    if(s.ok && nativeAIRules.isGlobal())s.ok=writeNativeAIGlobalState(f);
    if(s.ok && nativeGroundEnabled())s.ok=writeNativeGroundState(f);
    if(s.ok && nativeTravelEnabled())s.ok=writeNativeTravelState(f);
    if(s.ok && (redirectCount || requiresTravelTail))s.ok=writeNativeDockRedirects(f,redirectCount,saveRevision);
    if(s.ok && hasCargoOwners)writePassengerOwners(s,*this,ownerRecords);
    if(s.ok && unloadCount)writeTransportUnloads(s,*this,unloadCount);
    if(s.ok && buildingSaleSave)writeBuildingSales(s,*this);
    return s.ok;
}

bool World::loadGame(FILE* f) {
    if(updateInProgress || entityMutationDepth>0 || areaDamageDepth>0 || !f) return false;
    nativeTemporalEnabled=false;nativeTemporalBeams.clear();nativeCrewRules={};nativeSceneVersion=nativeRulesBaseVersion=0;nextCombatActor=1;nativeCrewBursts.clear();
    for(EID id=0;id<(EID)ents.size();++id)retireCombatIdentity(id);
    for(auto& projectile:projs)projectile.sourceActor=0;
    retiredDuringUpdate.clear();
    presentationEpoch=nextPresentationEpoch();
    presentationEvents.clear(); presentationSequence = 0;
    BinaryStateStream s{f};
    char magic[8];
    s.readBytes(magic, 8);
    const auto* saveFormat=s.ok?WorldSaveFormat::identify(magic):nullptr;
    veterancyProgressEnabled=independentSupportEnabled=false;
    supportDeliveryEnabled=false;supportMissions.clear();
    if(!saveFormat)return false;
    const bool buildingSaleSave=saveFormat->revision==WorldSaveFormat::buildingSaleRevision;
    bool transportUnloadSave=saveFormat->revision==WorldSaveFormat::transportUnloadRevision;
    bool cargoOwnerSave=saveFormat->revision==WorldSaveFormat::cargoOwnerRevision;
    int saveRevision=saveFormat->revision;
    if(buildingSaleSave || cargoOwnerSave || transportUnloadSave) {
        s.read(saveRevision);
        if(!s.ok || !WorldSaveFormat::cargoOwnerBaseRevision(saveRevision))return false;
    }
    if(buildingSaleSave) {
        uint8_t flags=0;s.read(flags);if(!s.ok || flags>3)return false;
        cargoOwnerSave=(flags&1)!=0;transportUnloadSave=(flags&2)!=0;
    } else if(transportUnloadSave) {
        uint8_t owners=0;s.read(owners);if(!s.ok || owners>1)return false;cargoOwnerSave=owners!=0;
    }
    const bool schemaAtLeast20=saveRevision>=20;
    const bool schemaAtLeast19=saveRevision>=19;
    const bool schemaAtLeast18=saveRevision>=18;
    const bool schemaAtLeast17=saveRevision>=17;
    const bool schemaAtLeast16=saveRevision>=16;
    const bool schemaAtLeast15=saveRevision>=15;
    const bool schemaAtLeast14=saveRevision>=14;
    const bool schemaAtLeast13=saveRevision>=13;
    const bool schemaAtLeast12=saveRevision>=12;
    const bool schemaAtLeast11=saveRevision>=11;
    const bool schemaAtLeast9=saveRevision>=9;
    const bool schemaAtLeast8=saveRevision>=8;
    const bool schemaAtLeast7=saveRevision>=7;
    const bool requiresTravelTail=WorldSaveFormat::requiresTravelAndDockRedirects(saveRevision);
    const bool dockRedirectSave=requiresTravelTail || saveRevision==WorldSaveFormat::dockApproachRevision
        || saveRevision==WorldSaveFormat::dockRedirectRevision;
    const bool schema64=saveRevision==WorldSaveFormat::travelRevision;
    const bool schema63=dockRedirectSave || schema64 || saveRevision==WorldSaveFormat::groundRevision;
    const bool schema62=saveRevision==62;
    const bool schema61=saveRevision==61 || schema62;
    const bool schema60=saveRevision>=60;
    const bool schema59=saveRevision>=59;
    const bool schema58=saveRevision>=58;
    const bool schema57=saveRevision>=57;
    const bool schema56=saveRevision>=56;
    const bool schema55=saveRevision>=55;
    const bool schema54=saveRevision>=54;
    const bool schema53=saveRevision>=53;
    const bool schema52=saveRevision>=52;
    const bool schema51=saveRevision>=51;
    const bool schema50=saveRevision>=50;
    const bool schema49=saveRevision>=49;
    const bool schema48=saveRevision>=48;
    const bool schema47=saveRevision>=47;
    const bool schema46=saveRevision>=46;
    const bool schema45=saveRevision>=45;
    const bool schema44=saveRevision>=44;
    const bool schema43=saveRevision>=43;
    const bool schema42=saveRevision>=42;
    const bool schema41=saveRevision>=41;
    const bool schema40=saveRevision>=40;
    const bool schema39=saveRevision>=39;
    const bool schema38=saveRevision>=38;
    const bool schema37=saveRevision>=37;
    const bool schema36=saveRevision>=36;
    const bool schema35=saveRevision>=35;
    const bool schema34=saveRevision>=34;
    const bool schema33=saveRevision>=33;
    const bool schema32=saveRevision>=32;
    const bool schema31=saveRevision>=31;
    const bool schema30=saveRevision>=30;
    const bool schema29=saveRevision>=29;
    const bool schema28=saveRevision>=28;
    const bool schema27=saveRevision>=27;
    const bool schema26=saveRevision>=26;
    const bool schema25=saveRevision>=25;
    const bool schema24=saveRevision>=24;
    const bool schema23=saveRevision==23;
    const bool schema22=saveRevision==22;
    const bool schema21=saveRevision==21;
    const bool schema10=saveRevision==10;
    s.read(tick); s.read(numPlayers); s.read(rng.s);
    s.read(cratesEnabled); s.read(aiAlliance);
    if (!s.ok || numPlayers <= 0 || numPlayers > MAX_PLAYERS || rng.s == 0) return false;
    if (schemaAtLeast8) {
        uint8_t mode = 0;
        s.read(mode);
        if (mode >= (uint8_t)SkirmishMode::COUNT) return false;
        skirmishMode = (SkirmishMode)mode;
        s.read(sharedVision); s.read(shortGame); s.read(superweaponsEnabled);
        if (schema10 || schemaAtLeast11) s.read(mcvRepacks);
        else mcvRepacks = false;
    } else {
        skirmishMode = SkirmishMode::Battle;
        sharedVision = false;
        shortGame = false;
        superweaponsEnabled = true;
        mcvRepacks = false;
    }
    // 地图
    s.read(map.w); s.read(map.h);
    if (!s.ok || map.w <= 0 || map.h <= 0 || map.w > 512 || map.h > 512) return false;
    map.cells.resize((size_t)map.w * map.h);
    for (Cell& c : map.cells) {
        uint8_t t = 0, o = 0;
        s.read(t); s.read(o); s.read(c.variant); s.read(c.ore); s.read(c.oreMax);
        if (schemaAtLeast17) s.read(c.height); else c.height = 0;
        uint8_t flags=1;if(schema30)s.read(flags);
        if(flags>3)return false;c.present=(flags&1)!=0;c.blocked=(flags&2)!=0;
        if (t > (uint8_t)Terrain::Bridge || o > (uint8_t)Overlay::Rock2
            || c.ore < 0 || c.oreMax < 0 || c.ore > c.oreMax) return false;
        c.terrain = (Terrain)t; c.overlay = (Overlay)o;
    }
    map.native={};if(schema30 && !readNativeMapState(f,map))return false;
    map.fog.assign(numPlayers, std::vector<uint8_t>((size_t)map.w * map.h));
    for (int p = 0; p < numPlayers; p++)
        s.readBytes(map.fog[p].data(), map.fog[p].size());
    for (const auto& fog : map.fog)
        for (uint8_t value : fog)
            if (value > FOG_VISIBLE) return false;
    bldOcc.assign((size_t)map.w * map.h, -1);
    s.readBytes(bldOcc.data(), bldOcc.size() * sizeof(int));
    map.bldOccRef = &bldOcc; // 重新挂载寻路占用表
    // 玩家
    players.assign(numPlayers, Player{});
    for (Player& p : players) {
        s.read(p.active); s.read(p.isAI); s.read(p.defeated);
        uint8_t fac = 0;
        s.read(fac); p.faction = (Faction)fac;
        uint8_t ctry = 0;
        s.read(ctry); p.country = (Country)ctry;
        if (fac > (uint8_t)Faction::Yuri || ctry >= (uint8_t)Country::COUNT) return false;
        s.read(p.secretLabUnlock); s.read(p.paradropCharge); s.read(p.paradropReady);
        s.read(p.colorId);
        if(schema24) s.read(p.money);
        else { int32_t oldMoney=0;s.read(oldMoney);p.money=oldMoney; }
        s.readText(p.name);
        deserProd(s, p.bldProd, schemaAtLeast13, schemaAtLeast19);
        if (schemaAtLeast14) deserProd(s, p.defProd, true, schemaAtLeast19);
        else p.defProd = ProdItem{};
        for (int c = 0; c < PROD_CAT_N; c++) deserProd(s, p.unitProd[c], schemaAtLeast13, schemaAtLeast19);
        for (int c = 0; c < PROD_CAT_N; c++) {
            uint32_t n = 0;
            s.read(n);
            if (n > 64) { s.ok = false; break; }
            p.unitQueue[c].clear();
            for (uint32_t i = 0; i < n; i++) {
                int t = 0;
                s.read(t);
                if (t < 0 || (!buildingSaleSave && t==(int)UnitType::NativeInfantry)
                    || (t >= (int)UnitType::COUNT && (!schema43 || t<NATIVE_PRODUCTION_BASE || t>=NATIVE_PRODUCTION_BASE+512))) { s.ok = false; break; }
                p.unitQueue[c].push_back(t);
            }
        }
        uint8_t pb = 0;
        s.read(pb); p.placingBld = (BldType)pb;
        if (pb > (uint8_t)BldType::COUNT) return false;
        s.read(p.powerSabotage); s.read(p.revealTimer);
        for (int c = 0; c < PROD_CAT_N; c++) s.read(p.vetCat[c]);
        s.read(p.stolenTech);
        s.read(p.aiDifficulty);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.read(p.swCharge[i]);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.read(p.swReady[i]);
        s.read(p.stormTimer); s.read(p.stormX); s.read(p.stormY); s.read(p.stormBoltCd);
        s.read(p.evaBaseCd); s.read(p.evaMinerCd); s.read(p.evaUnitCd);
    }
    // 实体
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 65536) return false;
        ents.assign(n, Ent{});
        for (Ent& e : ents) {
            s.read(e.alive); s.read(e.isBuilding); s.read(e.player);
            uint8_t ut = 0, bt = 0, st = 0;
            s.read(ut); s.read(bt);
            if (ut >= (uint8_t)(buildingSaleSave?UnitType::COUNT:UnitType::NativeInfantry) || bt >= (uint8_t)BldType::COUNT) return false;
            e.utype = (UnitType)ut; e.btype = (BldType)bt;
            s.read(e.x); s.read(e.y); s.read(e.dir); s.read(e.turretDir); s.read(e.hp);
            uint32_t pn = 0;
            s.read(pn);
            if (pn > 4096) { s.ok = false; break; }
            e.path.resize(pn);
            for (Vec2i& wp : e.path) { s.read(wp.x); s.read(wp.y); }
            if (schemaAtLeast9) {
                uint32_t wn = 0;
                s.read(wn);
                if (wn > 4096) { s.ok = false; break; }
                for (uint32_t i = 0; i < wn; ++i) {
                    float x = 0, y = 0;
                    s.read(x); s.read(y);
                    e.wps.emplace_back(x, y);
                }
            }
            s.read(e.pathIdx); s.read(e.moveTick); s.read(e.blockTick);
            s.read(e.walkFrame); s.read(e.walkAnim);
            s.read(e.fireAnim); s.read(e.constructAnim); s.read(e.deployAnim);
            if(schema28){s.read(e.burstRemaining);s.read(e.burstDelay);s.read(e.burstOwner);s.read(e.burstTarget);s.read(e.burstAir);}
            s.read(st); e.state = (UState)st;
            if (st > (uint8_t)(schema26?UState::DeployTakeoff:(schema24 || schema23 || schema22)?UState::Servicing:UState::Boarding)) return false;
            s.read(e.atkCd); s.read(e.target); s.read(e.goalX); s.read(e.goalY);
            if(schemaAtLeast20) {
                uint32_t failures=0;s.read(failures);
                if(!s.ok || failures>8) return false;
                e.failedPursuits.resize(failures);
                for(auto& failure:e.failedPursuits) {
                    s.read(failure.target);s.read(failure.sourceX);s.read(failure.sourceY);
                    s.read(failure.targetX);s.read(failure.targetY);s.read(failure.retryTick);
                }
            }
            if(schema24 || schema23 || schema22 || schema21) {
                uint8_t intent=0;s.read(intent);
                if(!s.ok || intent>(uint8_t)(schema33?EngineerIntent::RepairBridge:EngineerIntent::Repair)) return false;
                e.engineerIntent=(EngineerIntent)intent;
            }
            s.read(e.oreLoad); s.read(e.gemLoad); s.read(e.oreCell.x); s.read(e.oreCell.y); s.read(e.dockRefinery); s.read(e.digTimer);
            if (schemaAtLeast15) s.read(e.autoHarvest);
            else e.autoHarvest = true;
            s.read(e.invuln);
            s.read(e.ammo); s.read(e.rearmTimer); s.read(e.airbase); s.read(e.orbitA);
            s.read(e.rallyX); s.read(e.rallyY); s.read(e.bldAnim); s.read(e.undeploy); s.read(e.guard);
            if (schemaAtLeast11) s.read(e.repairing);
            else e.repairing = false;
            if (schemaAtLeast16) s.read(e.selling);
            else e.selling = false;
            uint32_t cn = 0;
            s.read(cn);
            if (cn > 64) { s.ok = false; break; }
            e.cargo.resize(cn);
            for (Ent::GarrisonedUnit& cu : e.cargo) {
                uint8_t ct = 0;
                s.read(ct);
                if (ct >= (uint8_t)(buildingSaleSave?UnitType::COUNT:UnitType::NativeInfantry)) { s.ok = false; break; }
                cu.type = (UnitType)ct;
                if (schemaAtLeast12) {
                    s.read(cu.hp); s.read(cu.kills); s.read(cu.veterancyValue); s.read(cu.vetRank);
                    if(schema25)deserPassengerExtra(s,cu);
                } else {
                    cu.hp = unitDef(cu.type).hp;
                    cu.kills = 0; cu.veterancyValue = 0; cu.vetRank = 0;
                }
            }
            s.read(e.chrono); s.read(e.tpSick); s.read(e.camouflaged); s.read(e.camoTick);
            s.read(e.radDeployed); s.read(e.deployed); s.read(e.subReveal);
            s.read(e.kills);
            if (schemaAtLeast7) {
                s.read(e.veterancyValue); s.read(e.vetRank);
            } else {
                s.read(e.vetRank);
                e.veterancyValue = e.isBuilding ? 0
                    : (int)std::ceil(unitDef(e).cost * g_gameRules.veteranRatio * e.vetRank);
            }
            if (schemaAtLeast12) {
                s.read(e.crateDmgBoost); s.read(e.crateArmorBoost); s.read(e.crateSpeedBoost);
            } else {
                e.crateDmgBoost = e.crateArmorBoost = e.crateSpeedBoost = 0;
            }
            // 驻军/寄生/磁暴充电
            uint32_t gn = 0;
            s.read(gn);
            if (gn > 64) { s.ok = false; break; }
            e.garrison.resize(gn);
            for (Ent::GarrisonedUnit& gu : e.garrison) {
                uint8_t gt = 0; s.read(gt); gu.type = (UnitType)gt;
                if (gt >= (uint8_t)(buildingSaleSave?UnitType::COUNT:UnitType::NativeInfantry)) { s.ok = false; break; }
                if (schemaAtLeast7) {
                    s.read(gu.hp); s.read(gu.kills); s.read(gu.veterancyValue); s.read(gu.vetRank);
                    if(schema25)deserPassengerExtra(s,gu);
                } else {
                    gu.hp = unitDef(gu.type).hp;
                }
            }
            s.read(e.parasite); s.read(e.parasiteHost); s.read(e.parasiting); s.read(e.teslaCharge);
            // P6 心灵控制
            s.read(e.mindBy); s.read(e.mindTarget);
            if (schemaAtLeast7) {
                uint32_t mn = 0; s.read(mn);
                if (mn > 64) { s.ok = false; break; }
                e.mindTargets.resize(mn);
                for (EID& tid : e.mindTargets) s.read(tid);
                s.read(e.origPlayer); s.read(e.permaControlled);
            } else {
                s.read(e.origPlayer);
            }
            // 尤复补全：YR 新单位特殊机制
            s.read(e.airstrikeCd); s.read(e.confused);
            if (schemaAtLeast7) {
                s.read(e.magneticBy); s.read(e.magneticHeight); s.read(e.drainedBy);
                s.read(e.gatlingHeat); s.read(e.gatlingStage);
            }
        }
        uint32_t fn = 0;
        s.read(fn);
        if (fn > 65536) s.ok = false;
        freeList.clear();
        for (uint32_t i = 0; s.ok && i < fn; i++) { int id = 0; s.read(id); freeList.push_back(id); }
    }
    // 投射物 / 特效 / 核弹 / 补给箱 / 定时炸弹 / EVA 队列
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 8192) return false;
        projs.assign(n, Projectile{});
        for (Projectile& p : projs) {
            s.read(p.alive);
            uint8_t k = 0;
            s.read(k); p.kind = (ProjKind)k;
            if (k > (uint8_t)ProjKind::Missile) return false;
            s.read(p.player); s.read(p.x); s.read(p.y); s.read(p.tx); s.read(p.ty);
            s.read(p.target); s.read(p.src);
            if (schemaAtLeast7) s.read(p.srcGarrisonSlot);
            deserWeapon(s, p.w, schemaAtLeast7,schema27,schema29);
            s.read(p.speed); s.read(p.trail); s.read(p.hp);
            if (schemaAtLeast18) {
                s.read(p.ox); s.read(p.oy); s.read(p.z); s.read(p.rot); s.read(p.arcing); s.read(p.proximity);
            } else {
                p.ox = p.x; p.oy = p.y; p.z = 0; p.rot = 0; p.arcing = false; p.proximity = false;
                if (p.kind == ProjKind::Missile) p.rot = 8;
                if (p.kind == ProjKind::Shell) p.arcing = true;
            }
        }
    }
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 8192) return false;
        effects.assign(n, Effect{});
        for (Effect& e : effects) {
            s.read(e.alive); s.read(e.kind); s.read(e.x); s.read(e.y); s.read(e.x2); s.read(e.y2); s.read(e.age); s.read(e.maxAge);
            s.read(e.aux); s.read(e.aux2); s.read(e.aux3);
        }
    }
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 64) return false;
        nukes.assign(n, Nuke{});
        for (Nuke& nk : nukes) { s.read(nk.active); s.read(nk.player); s.read(nk.tx); s.read(nk.ty); s.read(nk.timer); }
    }
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 1024) return false;
        crates.assign(n, Crate{});
        for (Crate& c : crates) { s.read(c.alive); s.read(c.x); s.read(c.y); s.read(c.kind); }
    }
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 1024) return false;
        timedBombs.assign(n, TimedBomb{});
        for (TimedBomb& b : timedBombs) {
            s.read(b.x); s.read(b.y); s.read(b.timer); s.read(b.player); s.read(b.attachedTo); s.read(b.dmg); s.read(b.radius);
            if (schemaAtLeast15) s.read(b.rockVehicles); else b.rockVehicles = false;
        }
    }
    {
        uint32_t n = 0;
        s.read(n);
        if (!s.ok || n > 256) return false;
        evaQueue.clear();
        for (uint32_t i = 0; i < n; i++) {
            EvaEvent ev;
            s.read(ev.player); s.readText(ev.text);
            evaQueue.push_back(std::move(ev));
        }
    }
    if (!s.ok) return false;

    if (!schemaAtLeast19) {
        auto migrate = [](ProdItem& p, bool unit) {
            if (!p.active) return true;
            const int limit = unit ? (int)UnitType::COUNT : (int)BldType::COUNT;
            if (p.typeIdx < 0 || p.typeIdx >= limit || p.progress < 0
                || p.totalCost < 0 || p.paid < 0 || p.paid > p.totalCost) return false;
            const int time = std::max(1, unit ? unitDef((UnitType)p.typeIdx).buildTime : bldDef((BldType)p.typeIdx).buildTime);
            // Legacy elapsed ticks used changing durations. Paid fraction is stable.
            const int64_t work = p.ready ? int64_t(time) * 1024
                : (p.totalCost ? int64_t(time) * 1024 * p.paid / p.totalCost : 0);
            p.progress = int(work / 1024); p.workRemainder = int(work % 1024);
            return true;
        };
        for (Player& p : players) {
            if (!migrate(p.bldProd, false) || !migrate(p.defProd, false)) return false;
            for (ProdItem& item : p.unitProd) if (!migrate(item, true)) return false;
        }
    }
    if(!schema24 && !schema23 && !schema22 && !schema21) for(auto& e:ents) {
        if(e.alive && !e.isBuilding && e.utype==UnitType::Engineer && (e.state==UState::Chasing || e.state==UState::Attacking)
            && valid(e.target) && ents[e.target].isBuilding)
            e.engineerIntent=ents[e.target].player==e.player?EngineerIntent::Repair:EngineerIntent::Capture;
    }
    trafficGates.clear();resetMovementCorridors();
    if(schema24 || schema23) {
        uint32_t count=0;s.read(count);
        if(!s.ok || count>196608 || count>map.cells.size()*3) return false;
        trafficGates.resize(count);
        for(size_t index=0;index<count;++index) {
            auto& gate=trafficGates[index];s.read(gate.domain);s.read(gate.anchor);s.read(gate.exit);s.read(gate.until);
            uint8_t draining=0;s.read(draining);gate.draining=draining!=0;
            if(!s.ok || draining>1 || gate.domain<0 || gate.domain>2 || gate.anchor<0 || gate.anchor>=(int)map.cells.size()
                || gate.exit<0 || gate.exit>=(int)map.cells.size() || (gate.until>tick && gate.until-tick>1000000)) return false;
            if(index && (trafficGates[index-1].domain>gate.domain
                || (trafficGates[index-1].domain==gate.domain && trafficGates[index-1].anchor>=gate.anchor))) return false;
        }
    } // v6-v22 have no historical phase: the first update derives a fresh admission.
    nativeScenarioSource.clear();nativeTypes.clear();nativeWeaponCatalog.reset();nativeProductionRules={};nativeMovementRules={};nativeProductionSerial=0;std::fill_n(nativeAllies,MAX_PLAYERS,0);
    nativeAircraftDefinitions.clear();aircraftSpawners.clear();aircraftSpawnersEnabled=false;aswEnabled=schema51;nativeFlightEnabled=schema52;nativeJumpjetEnabled=schema53;
    rulesProfileId.clear();rulesProfileName.clear();rulesCountryMask=0;rulesCountryKeys={};rulesCountrySpeed.fill(1);
    resetNativeBridgeScenario();
    if(schema31 && !readNativeActors(f))return false;
    // The derived bridge cache projects actor positions before the complete
    // entity validation below. Reject unconvertible coordinates first.
    for(const auto& actor:ents)if(!std::isfinite(actor.x) || !std::isfinite(actor.y) || actor.x < -16.f || actor.y < -16.f
        || actor.x>map.w+16.f || actor.y>map.h+16.f)return false;
    rebuildNativeBridges();
    for(auto& actor:ents){actor.navLayer=actor.previousNavLayer=actor.goalLayer=0;for(auto& point:actor.wps)point.layer=-1;}
    if(schema32 && !readBridgeState(f,schema33))return false;
    if(schema34 && !readNativeGateState(f))return false;
    if(schema35 && !readNativeEventState(f,schema36,schema38,schema39,schema41,schema42))return false;
    if(schema37){if(!readNativeRocketState(f,schema40))return false;}
    else if(!nativeRocketDefinitions.empty())return false;
    if(schema43){if(!readNativeProductionState(f))return false;}
    else if(!validateNativeProductionState())return false;
    if(schema44){if(!readNativeAIState(f))return false;}
    else if(!validateNativeAIState())return false;
    nativeCrateBonuses.clear();
    if(schema45){if(!readNativeCrateState(f))return false;}
    else if(nativeCrateRules.available || !validateNativeCrateState())return false;
    nativeConditions={};
    if(schema46){if(!readNativeConditionState(f))return false;}
    else if(!validateNativeConditionState())return false;
    for(auto& e:ents){e.attackMove={};for(auto& p:e.wps)p.attackMode=-1;}
    if(schema47){if(!readAttackMoveState(f))return false;}
    else if(!validateAttackMoveState())return false;
    for(auto& e:ents)e.nativeMovement={};
    if(schema48){if(!readNativeMovementState(f))return false;}
    else if(nativeMovementRules.available || !validateNativeMovementState())return false;
    if(schema49){if(!readEntityGenerations(f))return false;}
    else for(auto& entity:ents)entity.generation=1; // Legacy files have no deferred external commands.
    if(schema50){if(!readAircraftState(f))return false;}
    else if(!validateAircraftState())return false;
    if(schema51){uint32_t version=0,footer=0;uint8_t enabled=0;s.read(version);s.read(enabled);s.read(footer);
        if(!s.ok || version!=1 || enabled>1 || footer!=5)return false;
        aswEnabled=enabled!=0;if(!validateAircraftState())return false;
    }
    if(schema52){if(!readNativeFlightState(f))return false;}
    else if(!validateNativeFlightState())return false;
    if(schema53){if(!readNativeJumpjetState(f))return false;}
    else if(!validateNativeJumpjetState())return false;
    if(schema54){if(!readNativeAirNavigation(f))return false;}
    else if(!validateNativeAirNavigation())return false;
    if(schema55){if(!readSourceCrateLifetimes(f))return false;}
    else if(!validateNativeCrateState())return false;
    if(schema56){if(!readSupportState(f))return false;}
    else if(!validateSupportState())return false;
    if(schema57){if(!readSupportDelivery(f))return false;}
    else if(!validateSupportDelivery())return false;
    nativeCellSerial=0;for(auto& e:ents){e.nativeCellOrder=0;e.nativeOrderCell=e.nativeOrderLayer=-1;}
    if(schema58){if(!readNativeCellOrder(f))return false;}
    else if(nativeCellOrderEnabled())return false;
    nativeTemporalBeams.clear();for(auto& actor:ents)actor.nativeTemporalHead=INVALID_EID;
    if(schema59){if(!readNativeTemporalState(f))return false;}
    else if(nativeTemporalEnabled)return false;
    if(schema60){if(!readNativeCrewState(f) || !validateNativeTemporalState())return false;}
    else if(nativeCrewRules.enabled)return false;
    if(schema61){if(schema62!=nativeAIRules.hasOriginalTeamControl() || !readNativeAIGlobalState(f))return false;}
    else if(nativeAIRules.isGlobal() || !validateNativeAIGlobalState())return false;
    if(requiresTravelTail && !nativeTravelEnabled())return false;
    if(schema63){if(!readNativeGroundState(f,saveRevision))return false;}
    else if(nativeGroundEnabled())return false;
    if(schema64 || (dockRedirectSave && nativeTravelEnabled())){if(!readNativeTravelState(f,saveRevision))return false;}
    else if(nativeTravelEnabled())return false;
    if(dockRedirectSave && !readNativeDockRedirects(f,saveRevision))return false;
    if(cargoOwnerSave && !readPassengerOwners(s,*this))return false;
    if(transportUnloadSave && !readTransportUnloads(s,*this))return false;
    if(buildingSaleSave && !readBuildingSales(s,*this))return false;
    if(buildingSaleSave!=nativeSaleSurvivorRules.enabled || !validateNativeBuildingSales())return false;
    if(!validateNativeTransportUnload())return false;
    rebuildNativeGates();
    if (fgetc(f) != EOF || ferror(f)) return false;
    // Legacy servicing was encoded as a normal move/idle targeting a depot.
    if(!schema24 && !schema23 && !schema22) for(auto& e:ents) {
        if(e.alive && !e.isBuilding && (e.state==UState::Moving || e.state==UState::Idle)
            && valid(e.target) && ents[e.target].isBuilding && ents[e.target].btype==BldType::ServiceDepot
            && ents[e.target].player==e.player && !unitDef(e).isInfantry() && !unitDef(e).isAir()
            && unitDef(e).pathDomain()==0 && !unitDef(e).canHarvet()) e.state=UState::Servicing;
    }
    const auto finite = [](float value) { return std::isfinite(value); };
    const auto validRef = [&](EID id) {
        return id == INVALID_EID || (id >= 0 && id < (EID)ents.size());
    };
    const auto validProd = [&](const ProdItem& item, bool unit) {
        return validProductionType(unit,item.typeIdx)
            && item.workRemainder >= 0 && item.workRemainder < 1024
            && item.progress >= 0 && item.totalCost >= 0 && item.paid >= 0
            && item.paid <= item.totalCost;
    };
    for (const Player& p : players) {
        if (p.colorId < 0 || p.colorId >= MAX_PLAYERS || p.money < 0 || p.money>MAX_CREDIT_BALANCE
            || reservedProductionFunds((int)(&p-players.data()))>MAX_CREDIT_BALANCE-p.money
            || p.aiDifficulty < 0 || p.aiDifficulty > 2
            || p.secretLabUnlock < 0 || p.secretLabUnlock >= (int)Country::COUNT
            || p.paradropCharge < 0 || p.powerSabotage < 0 || p.revealTimer < 0
            || p.stolenTech < 0 || !validProd(p.bldProd, false) || !validProd(p.defProd, false)) return false;
        for (int c = 0; c < PROD_CAT_N; ++c)
            if (!validProd(p.unitProd[c], true)) return false;
        for (int i = 0; i < (int)SWType::COUNT; ++i)
            if (p.swCharge[i] < 0) return false;
        if (p.stormTimer < 0 || p.stormBoltCd < 0 || !finite(p.stormX) || !finite(p.stormY)
            || p.evaBaseCd < 0 || p.evaMinerCd < 0 || p.evaUnitCd < 0) return false;
    }
    std::vector<uint8_t> freeSeen(ents.size(), 0);
    for (int id : freeList) {
        if (id < 0 || id >= (int)ents.size() || freeSeen[id] || ents[id].alive) return false;
        freeSeen[id] = 1;
    }
    for (size_t i = 0; i < ents.size(); ++i) {
        const Ent& e = ents[i];
        if(e.alive && !e.isBuilding && e.utype==UnitType::NativeVehicle && !e.nativeIdentity.definition)return false;
        if(!e.isBuilding && e.utype==UnitType::NativeInfantry
            && (!buildingSaleSave || !e.nativeIdentity.definition || !unitDef(e).isInfantry()))return false;
        if (e.player < -1 || e.player >= numPlayers || !finite(e.x) || !finite(e.y)
            || (e.state>=UState::DeployLanding && (!e.deploying() || e.deployAnim<1 || e.deployAnim>3600
                || e.deployed!=(e.state==UState::DeployUnfolding || e.state==UState::DeployFolding)))
            || !finite(e.goalX) || !finite(e.goalY) || !finite(e.orbitA)
            || e.x < -16.0f || e.y < -16.0f || e.x > map.w + 16.0f || e.y > map.h + 16.0f
            || (e.alive && e.state==UState::Servicing && (e.isBuilding || unitDef(e).isInfantry()
                || unitDef(e).isAir() || unitDef(e).pathDomain()!=0 || unitDef(e).canHarvet()
                || !valid(e.target) || !ents[e.target].isBuilding || !serviceFacility(ents[e.target].btype)))
            || (e.engineerIntent!=EngineerIntent::None && (e.isBuilding || e.utype!=UnitType::Engineer))
            || (e.engineerIntent==EngineerIntent::RepairBridge && (e.state!=UState::Chasing || bridgeForHut(e.target)<0))
            || e.pathIdx < 0 || (!e.path.empty() && e.pathIdx > (int)e.path.size()) || e.walkAnim < 0
            // Overkill leaves negative HP in dead slots until reuse; it is valid
            // historical state, whereas a living entity must not have negative HP.
            || (e.alive && e.hp < 0) || e.dir < 0 || e.dir > 7 || e.turretDir < 0 || e.turretDir > 7
            || e.vetRank < 0 || e.vetRank > 2 || e.veterancyValue < 0 || e.kills < 0
            || e.gatlingStage < 0 || e.gatlingStage > 2 || e.gatlingHeat < 0
            || e.magneticHeight<0 || e.magneticHeight>12
            || (e.alive && e.isBuilding && isRallyBuilding(e.btype)
                && !((e.rallyX==-1 && e.rallyY==-1) || map.inBounds(e.rallyX,e.rallyY)))
            || e.burstRemaining<0 || e.burstRemaining>15 || e.burstDelay<0 || e.burstDelay>600
            // Historical writers left inert volleys on dead actors. Their
            // target slot may also be dead, but must still be an in-range ID.
            || (e.burstRemaining>0 && (e.burstTarget<0 || !validRef(e.burstTarget) || (e.alive && !valid(e.burstTarget))
                || e.burstOwner<0 || e.burstOwner>=numPlayers || (e.isBuilding && !hasNativeGatling(e))))
            || (e.burstRemaining==0 && (e.burstDelay!=0 || e.burstOwner!=-1 || e.burstTarget!=INVALID_EID || e.burstAir))
            || !validRef(e.target) || !validRef(e.dockRefinery) || !validRef(e.airbase)
            || !validRef(e.parasite) || !validRef(e.parasiteHost) || !validRef(e.mindBy)
            || !validRef(e.mindTarget) || !validRef(e.magneticBy) || !validRef(e.drainedBy)) return false;
        for (const Vec2i& wp : e.path)
            if (!map.inBounds(wp.x, wp.y)) return false;
        for(size_t j=0;j<e.failedPursuits.size();++j) {
            const auto& failure=e.failedPursuits[j];
            if(failure.target<0 || !validRef(failure.target)
                || !map.inBounds(failure.sourceX,failure.sourceY) || !map.inBounds(failure.targetX,failure.targetY)
                || (failure.retryTick>tick && failure.retryTick-tick>180)) return false;
            for(size_t k=0;k<j;++k) if(e.failedPursuits[k].target==failure.target) return false;
        }
        for (const auto& wp : e.wps)
            if (!finite(wp.first) || !finite(wp.second)
                || wp.first < -1.0f || wp.second < -1.0f
                || wp.first > map.w + 1.0f || wp.second > map.h + 1.0f) return false;
        for (EID id : e.mindTargets)
            if (!validRef(id)) return false;
        for(const auto& passenger:e.cargo)if(!validPassenger(passenger))return false;
        for(const auto& passenger:e.garrison)if(!validPassenger(passenger))return false;
    }
    for (int occ : bldOcc)
        if (occ < -1 || occ > (int)ents.size()) return false;
    for (const Projectile& p : projs) {
        if (p.player < 0 || p.player >= numPlayers || !finite(p.x) || !finite(p.y)
            || !finite(p.tx) || !finite(p.ty) || !finite(p.w.vsInfantry)
            || !finite(p.w.vsVehicle) || !finite(p.w.vsBuilding) || !finite(p.w.splash)
            || !validRef(p.target) || !validRef(p.src) || p.srcGarrisonSlot < -1
            || p.speed < 0 || p.hp < 0 || p.w.damage < 0 || (!std::isfinite(p.w.range) || p.w.range < 0)
            || !finite(p.w.cellSpread) || p.w.cellSpread < -1 || p.w.cellSpread>11
            || !finite(p.w.percentAtMax) || p.w.percentAtMax<0 || p.w.percentAtMax>1
            || p.w.cooldown < 0 || p.w.warhead >= WeaponDef::Warhead::COUNT) return false;
    }
    for (const Effect& e : effects)
        if (!finite(e.x) || !finite(e.y) || !finite(e.x2) || !finite(e.y2)
            || e.age < 0 || e.maxAge < 0
            || (e.kind==9 && (e.aux<0 || e.aux>2))
            || (e.kind==3 && e.aux==1 && (e.aux2<0 || e.aux2>12))) return false;
    for (const Nuke& n : nukes)
        if (n.player < 0 || n.player >= numPlayers || !finite(n.tx) || !finite(n.ty) || n.timer < 0) return false;
    for (const Crate& c : crates)
        if (!map.inBounds(c.x, c.y) || c.kind < 0 || (c.kind >= Crate::KIND_COUNT && !nativeCrateType(c.kind))
            || c.remainingTicks<0 || c.remainingTicks>216000 || (c.remainingTicks && c.kind!=NATIVE_CRATE_BASE+19)
            || (c.kind==NATIVE_CRATE_BASE+19 && (!schema55 || !c.remainingTicks))) return false;
    for (const TimedBomb& b : timedBombs)
        if (b.player < 0 || b.player >= numPlayers || !finite(b.x) || !finite(b.y)
            || !finite(b.radius) || b.timer < 0 || b.dmg < 0 || b.radius < 0
            || !validRef(b.attachedTo)) return false;
    for (const EvaEvent& ev : evaQueue)
        if (ev.player < 0 || ev.player >= numPlayers) return false;
    // Rebuild derived power caches without rewriting saved simulation state.
    // Normal updates/mutations subsequently validate current ability sources.
    recomputePower(false);
    rebuildUnitOcc(); // 占位链表为派生数据，读档后重建
    rebuildAirOcc();
    for(auto& actor:ents){actor.previousDir=actor.dir;actor.previousTurretDir=actor.turretDir;}
    return true;
}

