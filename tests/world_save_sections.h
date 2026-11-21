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

// The v47+ append-only extensions have a uint32 payload length footer.
// Walk the versioned suffix instead of assuming a tested extension is last.
enum class WorldSaveSection : int { AttackMove=47, NativeMovement=48, EntityGenerations=49, Aircraft=50, ASW=51, NativeFlight=52, NativeJumpjet=53, AirNavigation=54, CrateLifetimes=55, Support=56, SupportDelivery=57, NativeCellOrder=58, NativeTemporal=59, NativeCrew=60, NativeGround=63, NativeTravel=64, NativeDockRedirect=WorldSaveFormat::dockRedirectRevision };
struct WorldSaveSectionRange { size_t offset=0,length=0,footer=0; };
inline std::optional<WorldSaveSectionRange> locateWorldSaveSection(std::span<const uint8_t> bytes,WorldSaveSection section) {
    if(bytes.size()<8 || std::memcmp(bytes.data(),"RA2WLD",6)
        || bytes[6]<'0' || bytes[6]>'9' || bytes[7]<'0' || bytes[7]>'9')return std::nullopt;
    const int revision=(bytes[6]-'0')*10+bytes[7]-'0';
    const bool requiresTravel=WorldSaveFormat::requiresTravelAndDockRedirects(revision);
    const int version=(requiresTravel || revision==WorldSaveFormat::dockApproachRevision)?WorldSaveFormat::dockRedirectRevision:revision;
    if(version<(int)section || (version>60 && version!=63 && version!=64 && version!=WorldSaveFormat::dockRedirectRevision))
        return std::nullopt;
    size_t end=bytes.size();
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
