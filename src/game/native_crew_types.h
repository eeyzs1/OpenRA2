#pragma once
#include "game/native_weapon_types.h"

struct NativeCrewProfile {
    bool available=false,openTopped=false,occupier=false;
    std::array<int,2> occupyWeapons{-1,-1};
    int transportWeapon=-1; // Source OpenTransportWeapon: -1 automatic, otherwise a fixed source slot.
    std::array<int,2> transportWeapons{-1,-1}; // Normal / elite catalog references, not deployment state.
};
struct NativeCrewRules {
    bool enabled=false;
    int version=0;
    bool passengerOccupy=false; // RA2 building guns / YR individual occupy weapons
    int openRange=0,warpDistance=0;
    float openDamage=1,occupyDamage=1,occupyROF=1;
    int occupyRange=0;
};
bool parseNativeCrewRules(const Ini& ini,bool required,NativeCrewRules& result);
bool parseNativeCrewProfile(const Ini::Section* row,int version,const NativeWeaponCatalog* catalog,NativeCrewProfile& result);
