#include "game/world.h"
#include "core/binary_state_io.h"
#include "game/world_save_format.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
constexpr uint32_t dockRedirectMagic=0x31524444; // DDR1
constexpr uint16_t dockEast=0x4000;
constexpr uint32_t maxFacingDuration=128;
bool record(const World::Ent& e) {return e.alive && (e.nativeGround.initialized || e.nativeDock.phase!=NativeDockPhase::None);}
void facing(BinaryStateIO& io,NativeFacingState& s) {io.value(s.start);io.value(s.target);io.value(s.since);io.value(s.duration);}
void state(BinaryStateIO& io,World::Ent& e) {
    io.flag(e.nativeGround.initialized);facing(io,e.nativeGround.hull);facing(io,e.nativeGround.turret);
    io.value(e.nativeDock.phase);io.value(e.nativeDock.receiver.id);io.value(e.nativeDock.receiver.generation);
}
}
bool World::requiresForwardDockSave() const {
    return std::any_of(ents.begin(),ents.end(),[&](const Ent& e) {
        return e.alive && (e.nativeDock.phase==NativeDockPhase::ForwardEntry
            || (e.nativeDock.phase==NativeDockPhase::Exit && e.utype==UnitType::WarMiner
                && e.nativeGround.initialized && e.nativeGround.hull.duration<=maxFacingDuration
                && nativeHullHeading(e,tick)!=dockEast));
    });
}

bool World::requiresChronoBerthSave() const {
    return std::any_of(ents.begin(),ents.end(),[&](const Ent& e) {
        if(!e.alive || e.utype!=UnitType::ChronoMiner)return false;
        const auto& warp=e.chronoReturn;
        const bool pending=warp.directBerth
            && (warp.phase==ChronoReturnPhase::Waiting || warp.phase==ChronoReturnPhase::Charging);
        return pending || (e.nativeDock.phase==NativeDockPhase::Exit && e.nativeGround.initialized
            && e.nativeGround.hull.duration<=maxFacingDuration && nativeHullHeading(e,tick)!=dockEast);
    });
}

bool World::requiresUnloadStartupSave() const {
    return nativeTravelEnabled() && std::any_of(ents.begin(),ents.end(),[](const Ent& e) {
        return e.alive && e.nativeDock.phase==NativeDockPhase::Unload
            && e.digTimer>=-NativeUnloadTiming::startupTicks && e.digTimer<0;
    });
}

bool World::requiresDepartureWaitSave() const {
    return nativeTravelEnabled() && std::any_of(ents.begin(),ents.end(),[](const Ent& e) {
        return e.alive && e.nativeDock.phase==NativeDockPhase::Exit && e.waitingForNativeDeparture();
    });
}

bool World::requiresDriveExitSave() const {
    return nativeTravelEnabled() && std::any_of(ents.begin(),ents.end(),[](const Ent& e) {
        return e.alive && e.nativeDock.phase==NativeDockPhase::DriveExit;
    });
}

bool World::validateNativeGroundState() const {
    std::set<EID> occupied;
    const int minUnloadTimer=nativeTravelEnabled()?-NativeUnloadTiming::startupTicks:0;
    for(const auto& e:ents) {
        if(!e.alive)continue;
        const auto& g=e.nativeGround;const auto& s=e.nativeDock;
        if(!usesNativeGround(e)) {if(g!=NativeGroundState{} || s!=Ent::DockState{})return false;continue;}
        if(!g.initialized) {if(g!=NativeGroundState{} || e.dir<0 || e.dir>7 || e.turretDir<0 || e.turretDir>7)return false;}
        else for(const auto* f:{&g.hull,&g.turret}) {
            if(f->since>tick/2 || f->duration>maxFacingDuration)return false;
            const int rate=e.nativeIdentity.definition->production.rotationRate;
            const int duration=rate?std::abs(NativeGroundMath::delta(f->start,f->target))/(rate*256):0;
            if(f->duration!=duration)return false;
        }
        if(s.phase==NativeDockPhase::None){if(s!=Ent::DockState{})return false;continue;}
        if(s.phase>NativeDockPhase::DriveExit || !unitDef(e).canHarvet() || !valid(s.receiver))return false;
        const auto& b=ents[s.receiver.id];
        if(!b.isBuilding || b.btype!=BldType::OreRefinery || e.dockRefinery!=s.receiver.id)return false;
        if(e.claimsNativeDock() && !occupied.insert(s.receiver.id).second)return false;
        if(s.phase>=NativeDockPhase::Align && (!g.initialized || e.navLayer!=b.navLayer
            || !std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(b.x) || !std::isfinite(b.y)
            || std::abs(e.y-(b.y+1.5f))>.03f || e.x<b.x+3.47f || e.x>b.x+4.53f))return false;
        const bool modernWarMiner=nativeTravelEnabled() && e.utype==UnitType::WarMiner;
        if(s.phase==NativeDockPhase::DriveExit) {
            const auto& profile=e.nativeIdentity.definition->movement;
            const float berthX=b.x+3.5f,apronX=b.x+4.5f,laneY=b.y+1.5f;
            if(!modernWarMiner || !nativeMovementRules.available || profile.locomotor!=NativeLocomotor::Drive
                || profile.maxSpeed<=0 || berthX<0 || apronX>=map.w || laneY<0 || laneY>=map.h
                || e.x<berthX || e.x>=apronX || e.y!=laneY || e.oreLoad!=0 || e.gemLoad!=0
                || g.hull.target!=dockEast || nativeHullHeading(e,tick)!=dockEast
                || e.digTimer<-NativeUnloadTiming::departureWaitTicks || e.digTimer>0)return false;
            const auto& movement=e.nativeMovement;
            if(e.digTimer<0) {
                if(e.x!=berthX || movement!=NativeMovementState{})return false;
            } else if(!movement.started || movement.nodeX!=(int)apronX || movement.nodeY!=(int)laneY
                || movement.nodeLayer!=e.navLayer)return false;
            continue;
        }
        if(s.phase==NativeDockPhase::ForwardEntry) {
            const bool inBay=std::trunc(e.x)==std::trunc(b.x+3.5f);
            if(!modernWarMiner || e.digTimer!=0 || (inBay && g.hull.target!=dockEast))return false;
        } else if(s.phase>=NativeDockPhase::Enter && nativeHullHeading(e,tick)!=dockEast) {
            const bool chronoAtBerth=nativeTravelEnabled() && e.utype==UnitType::ChronoMiner
                && e.x==b.x+3.5f && e.y==b.y+1.5f;
            if(s.phase!=NativeDockPhase::Exit || (!modernWarMiner && !chronoAtBerth))return false;
        }
        if(s.phase==NativeDockPhase::Unload && (std::abs(e.x-(b.x+3.5f))>.001f || std::abs(e.y-(b.y+1.5f))>.001f
            || e.digTimer<minUnloadTimer || e.digTimer>=2*nativeProductionRules.harvesterDumpFrames))return false;
        if(e.waitingForNativeDeparture() && (!modernWarMiner || e.digTimer<-NativeUnloadTiming::departureWaitTicks
            || e.oreLoad!=0 || e.gemLoad!=0 || e.x!=b.x+3.5f || e.y!=b.y+1.5f
            || g.hull.target!=dockEast || nativeHullHeading(e,tick)!=dockEast))return false;
    }
    return true;
}
bool World::writeNativeGroundState(FILE* file) const {
    if(!file || !nativeGroundEnabled() || !validateNativeGroundState())return false;
    const long start=std::ftell(file);BinaryStateIO io(file);uint32_t magic=0x31444e47,count=0;
    for(const auto& e:ents)if(record(e))++count;
    io.value(magic);io.value(count);
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])) {
        io.value(id);Ent e;e.nativeGround=ents[id].nativeGround;e.nativeDock=ents[id].nativeDock;state(io,e);
    }
    uint32_t bytes=(uint32_t)(std::ftell(file)-start);io.value(bytes);return io.ok;
}
bool World::readNativeGroundState(FILE* file,int saveRevision) {
    if(!file || !nativeGroundEnabled())return false;
    const long start=std::ftell(file);BinaryStateIO io(file,true);uint32_t magic=0,count=0;
    io.value(magic);io.value(count);if(!io.ok || magic!=0x31444e47 || count>ents.size())return false;
    EID previous=INVALID_EID;
    for(uint32_t i=0;i<count;++i) {
        EID id=INVALID_EID;io.value(id);if(!io.ok || id<=previous || !valid(id))return false;previous=id;
        auto& e=ents[id];state(io,e);if(!io.ok || !record(e))return false;
    }
    const uint32_t expected=(uint32_t)(std::ftell(file)-start);uint32_t bytes=0;io.value(bytes);
    if(!io.ok || bytes!=expected || !validateNativeGroundState())return false;
    // World74 identifies its required state in TRV1. Any validated dock state can accompany it.
    if(saveRevision!=WorldSaveFormat::cubicWideDriveRevision) {
        const bool driveExitSave=saveRevision==WorldSaveFormat::driveExitRevision;
        if(driveExitSave!=requiresDriveExitSave())return false;
        const bool departureWaitSave=saveRevision==WorldSaveFormat::departureWaitRevision;
        if(!driveExitSave && departureWaitSave!=requiresDepartureWaitSave())return false;
        const bool unloadStartupSave=saveRevision==WorldSaveFormat::unloadStartupRevision;
        if(!driveExitSave && !departureWaitSave && unloadStartupSave!=requiresUnloadStartupSave())return false;
        const bool allowChronoBerth=saveRevision==WorldSaveFormat::chronoBerthRevision
            || saveRevision==WorldSaveFormat::chronoYieldRevision || unloadStartupSave || departureWaitSave || driveExitSave;
        if(!allowChronoBerth && requiresChronoBerthSave())return false;
        if(!allowChronoBerth && saveRevision!=WorldSaveFormat::wideDriveRevision
            && requiresForwardDockSave()!=(saveRevision==WorldSaveFormat::forwardDockRevision))return false;
    }
    for(auto& e:ents)if(record(e)) {
        e.previousHullHeading=nativeHullHeading(e,tick);e.previousTurretHeading=nativeTurretHeading(e,tick);
    }
    return true;
}

bool World::validateNativeDockRedirect(const Ent& e, bool allowApproach) const {
    if (!e.hasPendingDockReturn() || !usesNativeGround(e) || !unitDef(e).canHarvet()
        || e.chronoReturn.phase != ChronoReturnPhase::None || !ownsNativeDockRefinery(e, e.pendingDockRefinery)) return false;
    if (e.nativeDock.phase == NativeDockPhase::Approach)
        return allowApproach && e.utype == UnitType::WarMiner && e.nativeDock.receiver == e.pendingDockRefinery;
    const bool awaitingRoute = e.nativeDock.phase == NativeDockPhase::None && e.dockRefinery == INVALID_EID;
    return e.nativeGround.initialized && e.path.empty() && (e.nativeDock.phase == NativeDockPhase::Exit || awaitingRoute);
}

bool World::writeNativeDockRedirects(FILE* file, uint32_t count, int saveRevision) const {
    const bool modernRedirects=WorldSaveFormat::requiresTravelAndDockRedirects(saveRevision);
    const bool allowApproach=modernRedirects || saveRevision==WorldSaveFormat::dockApproachRevision;
    if(!count && !modernRedirects)return false;
    const long start = std::ftell(file);
    BinaryStateStream io(file);
    io.write(dockRedirectMagic);
    io.write(count);
    for (EID id = 0; id < (EID)ents.size(); ++id) {
        const auto& e = ents[id];
        if (!e.hasPendingDockReturn()) continue;
        if (!validateNativeDockRedirect(e, allowApproach)) return false;
        io.write(id);
        io.write(e.pendingDockRefinery.id);
        io.write(e.pendingDockRefinery.generation);
    }
    io.write(static_cast<uint32_t>(std::ftell(file) - start));
    return io.ok;
}

bool World::readNativeDockRedirects(FILE* file, int saveRevision) {
    const bool modernRedirects=WorldSaveFormat::requiresTravelAndDockRedirects(saveRevision);
    const bool expectApproach=saveRevision==WorldSaveFormat::dockApproachRevision;
    const long start = std::ftell(file);
    BinaryStateStream io(file);
    uint32_t magic = 0, count = 0;
    io.read(magic);
    io.read(count);
    if (!io.ok || magic != dockRedirectMagic || (!count && !modernRedirects) || count > ents.size()) return false;
    EID previous = INVALID_EID;
    bool foundApproach = false;
    for (uint32_t i = 0; i < count; ++i) {
        EID id = INVALID_EID;
        io.read(id);
        if (!io.ok || id <= previous || !valid(id)) return false;
        previous = id;
        auto& e = ents[id];
        io.read(e.pendingDockRefinery.id);
        io.read(e.pendingDockRefinery.generation);
        if (!io.ok || !validateNativeDockRedirect(e, modernRedirects || expectApproach)) return false;
        foundApproach |= e.nativeDock.phase == NativeDockPhase::Approach;
    }
    const auto length = static_cast<uint32_t>(std::ftell(file) - start);
    uint32_t footer = 0;
    io.read(footer);
    return io.ok && footer == length && (modernRedirects || foundApproach == expectApproach);
}

uint32_t World::nativeGroundChecksum() const {
    uint32_t hash=2166136261u;
    auto number=[&](uint64_t v,int size){for(int i=0;i<size;++i){hash=(hash^uint8_t(v))*16777619u;v>>=8;}};
    auto heading=[&](const NativeFacingState& f){number(f.start,2);number(f.target,2);number(f.since,8);number(f.duration,4);};
    for(EID id=0;id<(EID)ents.size();++id)if(record(ents[id])) {
        const auto& e=ents[id];number(id,4);number(e.nativeGround.initialized,1);heading(e.nativeGround.hull);heading(e.nativeGround.turret);
        number((uint8_t)e.nativeDock.phase,1);number((uint32_t)e.nativeDock.receiver.id,4);number(e.nativeDock.receiver.generation,8);
        if (e.hasPendingDockReturn()) {
            number(dockRedirectMagic,4);
            number((uint32_t)e.pendingDockRefinery.id,4);
            number(e.pendingDockRefinery.generation,8);
        }
    }
    return hash;
}
