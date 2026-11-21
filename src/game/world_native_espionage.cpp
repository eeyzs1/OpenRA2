#include "game/world.h"
#include "game/lang.h"
#include <cmath>
#include <cstdint>

namespace {
// Exact floor(balance * the source float), including the 64-bit ledger edge.
// Split its 24-bit significand product so the multiplication cannot overflow.
Credits stolenFraction(Credits balance,float fraction) {
    if(balance<=0 || fraction<=0)return 0;
    int exponent=0;const auto numerator=(uint32_t)std::ldexp(std::frexp((double)fraction,&exponent),24);
    const uint64_t amount=(uint64_t)balance,low=(amount&UINT32_MAX)*numerator;
    const uint64_t high=(amount>>32)*numerator+(low>>32);const int shift=24-exponent;
    if(shift>=32)return shift>=96?0:(Credits)(high>>(shift-32));
    return (Credits)((high<<(32-shift))|((low&UINT32_MAX)>>shift));
}
}

void World::applyNativeSpyEffect(Ent& spy,Ent& building,EID spyId) {
    const int player=spy.player,victim=building.player;
    if(player<0 || player>=numPlayers || victim<0 || victim>=numPlayers || !building.nativeIdentity.definition)return;
    auto& house=players[player];const auto& profile=building.nativeIdentity.definition->production;
    const auto notice=[&](S text,EvaCue cue=EvaCue::BuildingInfiltrated){eva(player,TR(text),cue);};
    switch(profile.spyEffect) {
    case NativeSpyEffect::Money: {
        const auto amount=transferMoney(victim,player,stolenFraction(players[victim].money,nativeProductionRules.spyMoneyFraction));
        eva(player,formatText(TR(S::SpyStealMoneyFmt),(long long)amount),EvaCue::BuildingInfiltrated);
        eva(victim,TR(S::SpyMoneyVictim));break;
    }
    case NativeSpyEffect::Power:
        players[victim].powerSabotage=nativeProductionRules.spyBlackoutTicks;
        notice(S::SpySourcePower);eva(victim,TR(S::SpyPowerVictim));break;
    case NativeSpyEffect::Radar:
        for(auto& cell:map.fog[victim])if(cell==FOG_SEEN)cell=FOG_UNSEEN;
        notice(S::SpySourceRadar);eva(victim,TR(S::SpyRadarVictim));break;
    case NativeSpyEffect::Infantry:house.vetCat[0]=true;notice(S::SpyBarracks);break;
    case NativeSpyEffect::Vehicles:house.vetCat[1]=true;notice(S::SpySourceVehicles);break;
    case NativeSpyEffect::Technology: {
        // RA2's original routine tests the current victim house's side
        // (GAME.EXE 0x4525E8), including a captured enemy laboratory.
        const int mask=nativeProductionRules.spyTechByOwner?
            (players[victim].faction==Faction::Allies?1:2):profile.spyTechMask;
        if((house.stolenTech&mask)!=mask) {
            house.stolenTech|=mask;notice(S::SpySourceTechnology,EvaCue::NewTechnology);
        } else notice(S::SpySourceTechnology);
        eva(victim,TR(S::SpyLabVictim));break;
    }
    case NativeSpyEffect::Superweapons:
        for(int i=0;i<(int)SWType::COUNT;++i)if((nativeProductionRules.version>=4?profile.spyResetWeapons:profile.superweaponMask)&(1u<<i)) {
            players[victim].swCharge[i]=0;players[victim].swReady[i]=false;
        }
        if(independentSupportEnabled) {
            for(int i=0;i<SUPPORT_COUNT;++i)if(profile.spyResetSupport&(1u<<i))players[victim].support[i]={};
        } else if(profile.supportMask){players[victim].paradropCharge=0;players[victim].paradropReady=false;}
        notice(S::SpySWReset);eva(victim,TR(S::SpySWVictim));break;
    default:eva(player,formatText(TR(S::SpyGenericFmt),bldName(building.btype)),EvaCue::BuildingInfiltrated);break;
    }
    emitSoundAt(Sfx::Eva,spy.x,spy.y);consumeEntity(spyId);
}
