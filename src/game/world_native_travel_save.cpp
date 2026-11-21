#include "game/world.h"
#include "core/binary_state_io.h"
#include "game/world_save_format.h"
#include <algorithm>
#include <bit>

namespace {
constexpr uint32_t travelMagic=0x31565254; // TRV1
constexpr uint8_t driveRecord=1,chronoRecord=2,wideDriveRecord=4,chronoBerthRecord=5,cubicWideDriveRecord=6;
constexpr uint8_t legacyChargeTicks=12,legacySettleTicks=8;

uint8_t driveArcRecord(const NativeDriveArc& arc) {
    if(!arc.wide)return driveRecord;
    return arc.cubic?cubicWideDriveRecord:wideDriveRecord;
}
uint8_t chronoReturnRecord(const ChronoReturnState& warp) {
    return warp.directBerth?chronoBerthRecord:chronoRecord;
}
void driveArcFields(BinaryStateIO& io,NativeDriveArc& arc) {
    io.value(arc.corner.x);io.value(arc.corner.y);
    io.value(arc.outgoing.x);io.value(arc.outgoing.y);
    io.value(arc.incoming);io.value(arc.layer);io.value(arc.progress);
}
void chronoReturnFields(BinaryStateIO& io,ChronoReturnState& warp) {
    io.value(warp.phase);io.value(warp.ticksLeft);io.flag(warp.preferReceiver);
}
}

bool World::requiresChronoYieldSave() const {
    return std::any_of(ents.begin(),ents.end(),[](const Ent& e) {
        return e.alive && e.hasChronoYieldPath();
    });
}

bool World::validateChronoReturn(const Ent& e) const {
    const auto& warp=e.chronoReturn;
    if(warp.phase==ChronoReturnPhase::None)return warp==ChronoReturnState{};
    const bool yielding=e.hasChronoYieldPath();
    if(!usesNativeGround(e) || e.utype!=UnitType::ChronoMiner || e.state!=UState::HarvestReturn
        || e.player<0 || (!e.path.empty() && !yielding) || !warp.ticksLeft)return false;
    if(yielding && (e.pathIdx<0 || (size_t)e.pathIdx>=e.path.size()
        || e.nativeDock.phase!=NativeDockPhase::Approach))return false;
    switch(warp.phase) {
        case ChronoReturnPhase::Waiting:
            if(warp.ticksLeft>ChronoReturnTiming::retry)return false;
            if(e.nativeDock.phase==NativeDockPhase::None)
                return !warp.preferReceiver && e.dockRefinery==INVALID_EID;
            break;
        case ChronoReturnPhase::Charging:
            if(warp.ticksLeft>(warp.directBerth?ChronoReturnTiming::ready:legacyChargeTicks))return false;
            break;
        case ChronoReturnPhase::Settling:
            if(warp.directBerth || warp.ticksLeft>legacySettleTicks)return false;
            break;
        default:return false;
    }
    const auto ref=e.nativeDock.receiver;
    if(e.nativeDock.phase!=NativeDockPhase::Approach || !valid(ref) || e.dockRefinery!=ref.id)return false;
    const auto& refinery=ents[ref.id];
    if(!refinery.isBuilding || refinery.btype!=BldType::OreRefinery || refinery.player!=e.player)return false;
    return warp.phase!=ChronoReturnPhase::Settling
        || (e.x==refinery.x+4.5f && e.y==refinery.y+1.5f && e.navLayer==refinery.navLayer);
}

bool World::writeNativeTravelState(FILE* file) const {
    BinaryStateIO io(file);
    const long start=std::ftell(file);
    uint32_t count=0;
    for(const auto& e:ents)if(e.alive) {
        if(!validateChronoReturn(e))return false;
        if(driveArcMatchesRoute(e) || e.chronoReturn.phase!=ChronoReturnPhase::None)++count;
    }
    io.write(travelMagic);io.write(count);
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& e=ents[id];
        if(!e.alive)continue;
        if(driveArcMatchesRoute(e)) {
            io.write(id);io.write(driveArcRecord(e.driveArc));
            auto arc=e.driveArc;driveArcFields(io,arc);
        } else if(e.chronoReturn.phase!=ChronoReturnPhase::None) {
            io.write(id);io.write(chronoReturnRecord(e.chronoReturn));
            auto warp=e.chronoReturn;chronoReturnFields(io,warp);
        }
    }
    io.write((uint32_t)(std::ftell(file)-start));
    return io.ok;
}

bool World::readNativeTravelState(FILE* file,int saveRevision) {
    if(!nativeTravelEnabled())return false;
    const bool cubicWideDriveSave=saveRevision==WorldSaveFormat::cubicWideDriveRevision;
    const bool chronoYieldSave=saveRevision==WorldSaveFormat::chronoYieldRevision;
    const bool allowChronoYield=chronoYieldSave || saveRevision==WorldSaveFormat::unloadStartupRevision
        || saveRevision==WorldSaveFormat::departureWaitRevision || saveRevision==WorldSaveFormat::driveExitRevision
        || cubicWideDriveSave;
    const bool chronoBerthSave=saveRevision==WorldSaveFormat::chronoBerthRevision;
    const bool allowChronoBerth=chronoBerthSave || allowChronoYield;
    const bool requiresWide=saveRevision==WorldSaveFormat::wideDriveRevision;
    const bool allowWide=requiresWide || allowChronoBerth;
    BinaryStateIO io(file,true);
    const long start=std::ftell(file);
    uint32_t magic=0,count=0;
    io.read(magic);io.read(count);
    if(!io.ok || magic!=travelMagic || count>ents.size())return false;
    EID previous=INVALID_EID;
    bool foundWide=false,foundCubic=false;
    for(uint32_t i=0;i<count;++i) {
        EID id=INVALID_EID;
        uint8_t record=0;
        io.read(id);io.read(record);
        const bool cubic=record==cubicWideDriveRecord;
        const bool wide=record==wideDriveRecord || cubic;
        const bool drive=record==driveRecord || wide;
        const bool directBerth=record==chronoBerthRecord;
        if(!io.ok || id<=previous || !valid(id) || (!drive && !directBerth && record!=chronoRecord)
            || (wide && !allowWide) || (cubic && !cubicWideDriveSave) || (directBerth && !allowChronoBerth))return false;
        previous=id;
        auto& e=ents[id];
        if(drive) {
            e.driveArc.wide=wide;
            e.driveArc.cubic=cubic;
            driveArcFields(io,e.driveArc);
            if(!io.ok || !driveArcMatchesRoute(e))return false;
            foundWide |= record==wideDriveRecord;
            foundCubic |= cubic;
        } else {
            e.chronoReturn.directBerth=directBerth;
            chronoReturnFields(io,e.chronoReturn);
            if(!io.ok || e.chronoReturn.phase==ChronoReturnPhase::None
                || (e.hasChronoYieldPath() && !allowChronoYield) || !validateChronoReturn(e))return false;
        }
    }
    const auto length=(uint32_t)(std::ftell(file)-start);
    uint32_t footer=0;
    io.read(footer);
    if(!io.ok || footer!=length || (requiresWide && !foundWide) || (cubicWideDriveSave && !foundCubic)
        || (chronoBerthSave && !requiresChronoBerthSave())
        || (chronoYieldSave && !requiresChronoYieldSave()))return false;
    return validateNativeGroundState();
}

uint32_t World::nativeTravelChecksum() const {
    uint32_t hash=2166136261u;
    auto add=[&](uint32_t value){
        for(int i=0;i<4;++i){hash=(hash^uint8_t(value))*16777619u;value>>=8;}
    };
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& e=ents[id];
        if(!e.alive)continue;
        if(driveArcMatchesRoute(e)) {
            const auto& arc=e.driveArc;
            add(id);add(driveArcRecord(arc));add(arc.corner.x);add(arc.corner.y);add(arc.outgoing.x);add(arc.outgoing.y);
            add(arc.incoming);add(arc.layer);add(std::bit_cast<uint32_t>(arc.progress));
        }
        if(e.chronoReturn.phase!=ChronoReturnPhase::None) {
            add(id);add(chronoReturnRecord(e.chronoReturn));add((uint8_t)e.chronoReturn.phase);
            add(e.chronoReturn.ticksLeft);add(e.chronoReturn.preferReceiver);
        }
    }
    return hash;
}
