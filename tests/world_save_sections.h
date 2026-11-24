#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include "game/world_save_format.h"

// App envelopes have a variable-length prefix. Locate a registered World
// header instead of binding independent corruption tests to one revision.
inline std::optional<size_t> embeddedWorldSaveOffset(std::span<const uint8_t> bytes) {
    if(bytes.size()<8)return std::nullopt;
    for(size_t at=0;at<=bytes.size()-8;++at) {
        const auto* magic=reinterpret_cast<const char*>(bytes.data()+at);
        if(magic[0]=='R' && magic[1]=='A' && magic[2]=='2' && WorldSaveFormat::identify(magic))return at;
    }
    return std::nullopt;
}

// A source building-sale envelope retains the exact selected travel layout.
inline bool worldSaveHeaderMatches(std::span<const uint8_t> bytes,const char* expected) {
    if(bytes.size()<8)return false;
    if(!std::memcmp(bytes.data(),expected,8))return true;
    if(bytes.size()<13 || std::memcmp(bytes.data(),"RA2WLD77",8))return false;
    if(!std::memcmp(expected,"RA2WLD75",8))return (bytes[12]&1)!=0;
    if(!std::memcmp(expected,"RA2WLD76",8))return (bytes[12]&2)!=0;
    int base=0;std::memcpy(&base,bytes.data()+8,4);
    const auto* format=WorldSaveFormat::forRevision(base);
    return format && std::equal(format->magic.begin(),format->magic.end(),expected);
}

// The v47+ append-only extensions have a uint32 payload length footer.
// Walk the versioned suffix instead of assuming a tested extension is last.
enum class WorldSaveSection : int { AttackMove=47, NativeMovement=48, EntityGenerations=49, Aircraft=50, ASW=51, NativeFlight=52, NativeJumpjet=53, AirNavigation=54, CrateLifetimes=55, Support=56, SupportDelivery=57, NativeCellOrder=58, NativeTemporal=59, NativeCrew=60, NativeGround=63, NativeTravel=64, NativeDockRedirect=WorldSaveFormat::dockRedirectRevision, CargoOwners=WorldSaveFormat::cargoOwnerRevision, TransportUnload=WorldSaveFormat::transportUnloadRevision, BuildingSale=WorldSaveFormat::buildingSaleRevision };
struct WorldSaveSectionRange { size_t offset=0,length=0,footer=0; };
inline std::optional<WorldSaveSectionRange> locateWorldSaveSection(std::span<const uint8_t> bytes,WorldSaveSection section) {
    if(bytes.size()<8 || std::memcmp(bytes.data(),"RA2WLD",6)
        || bytes[6]<'0' || bytes[6]>'9' || bytes[7]<'0' || bytes[7]>'9')return std::nullopt;
    int revision=(bytes[6]-'0')*10+bytes[7]-'0';
    size_t end=bytes.size();
    bool owners=revision==WorldSaveFormat::cargoOwnerRevision;
    bool unload=revision==WorldSaveFormat::transportUnloadRevision;
    const bool sale=revision==WorldSaveFormat::buildingSaleRevision;
    if(sale) {
        if(end<25 || bytes[12]>3)return std::nullopt;
        owners=(bytes[12]&1)!=0;unload=(bytes[12]&2)!=0;
        std::memcpy(&revision,bytes.data()+8,4);
        uint32_t length=0;std::memcpy(&length,bytes.data()+end-4,4);
        if(!WorldSaveFormat::cargoOwnerBaseRevision(revision) || length<8 || length>end-17)return std::nullopt;
        const WorldSaveSectionRange range{end-4-length,length,end-4};
        if(std::memcmp(bytes.data()+range.offset,"SLS1",4))return std::nullopt;
        if(section==WorldSaveSection::BuildingSale)return range;
        end=range.offset;
    }
    if(unload) {
        if(end<42 || (!sale && bytes[12]>1))return std::nullopt;
        if(!sale)owners=bytes[12]!=0;
        std::memcpy(&revision,bytes.data()+8,4);
        uint32_t length=0;std::memcpy(&length,bytes.data()+end-4,4);
        if(!WorldSaveFormat::cargoOwnerBaseRevision(revision) || length<25 || length>end-17)return std::nullopt;
        const WorldSaveSectionRange unload{end-4-length,length,end-4};
        if(std::memcmp(bytes.data()+unload.offset,"TUN1",4))return std::nullopt;
        if(section==WorldSaveSection::TransportUnload)return unload;
        end=unload.offset;
    }
    if(owners) {
        if(end<24)return std::nullopt;
        std::memcpy(&revision,bytes.data()+8,4);
        uint32_t length=0;std::memcpy(&length,bytes.data()+end-4,4);
        if(!WorldSaveFormat::cargoOwnerBaseRevision(revision) || length<8 || length>end-16)return std::nullopt;
        const WorldSaveSectionRange owners{end-4-length,length,end-4};
        if(std::memcmp(bytes.data()+owners.offset,"COW1",4))return std::nullopt;
        if(section==WorldSaveSection::CargoOwners)return owners;
        end=owners.offset;
    }
    const bool requiresTravel=WorldSaveFormat::requiresTravelAndDockRedirects(revision);
    const int version=(requiresTravel || revision==WorldSaveFormat::dockApproachRevision)?WorldSaveFormat::dockRedirectRevision:revision;
    if(version<(int)section || (version>60 && version!=63 && version!=64 && version!=WorldSaveFormat::dockRedirectRevision))
        return std::nullopt;
    for(int current=version;current>=(int)section;--current) {
        if(current==62 || current==61)continue; // Ordinary World63/64 append ground state directly after Crew60.
        if(end<12)return std::nullopt;
        uint32_t length=0;std::memcpy(&length,bytes.data()+end-4,4);
        if(length<4 || length>end-12)return std::nullopt;
        WorldSaveSectionRange range{end-4-length,length,end-4};
        if(current==WorldSaveFormat::dockRedirectRevision && std::memcmp(bytes.data()+range.offset,"DDR1",4))
            return std::nullopt;
        // World65 and World66 may omit TRV1. Later dock formats declare it required.
        if(version==WorldSaveFormat::dockRedirectRevision && current==64
            && !std::memcmp(bytes.data()+range.offset,"GND1",4)) {
            if(requiresTravel)return std::nullopt;
            continue;
        }
        if(current==(int)section)return range;
        end=range.offset;
    }
    return std::nullopt;
}
