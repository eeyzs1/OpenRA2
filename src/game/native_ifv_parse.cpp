#include "game/native_ifv_types.h"
#include "game/native_weapon_types.h"
#include <charconv>
#include <cstring>
#include <algorithm>

namespace {
bool integer(const Ini::Section* row,const std::string& key,int& value,int low,int high) {
    const char* text=row?row->get(key.c_str()):nullptr;if(!text)return false;
    const auto parsed=std::from_chars(text,text+std::strlen(text),value);
    return parsed.ec==std::errc{} && !*parsed.ptr && value>=low && value<=high;
}
}
bool parseNativeIFV(const Ini::Section* row,bool required,const NativeWeaponCatalog* catalog,NativeIFVProfile& result) {
    result={};
    if(!required) {
        if(row)for(const auto& [key,value]:row->kv)if(key.starts_with("IFV") || key=="NativeIFV")return false;
        return true;
    }
    if(!catalog || catalog->version<3)return false;
    int version=0,gunner=0,weapons=0,modes=0;NativeIFVProfile profile;
    if(!integer(row,"NativeIFV",version,1,2) || !integer(row,"IFVPassengerMode",profile.passengerMode,0,31)
        || !integer(row,"IFVGunner",gunner,0,1) || !integer(row,"IFVWeapons",weapons,0,32)
        || !integer(row,"IFVModes",modes,0,32) || !integer(row,"IFVTurrets",profile.turretCount,0,32))return false;
    profile.available=true;profile.version=version;profile.gunner=gunner!=0;
    if(profile.gunner?(!weapons || !modes || !profile.turretCount):(weapons || modes || profile.turretCount))return false;
    if(profile.gunner) {
        if(version>=2 && (!integer(row,"IFVBurstDelayMin",profile.burstDelayMin,0,900)
            || !integer(row,"IFVBurstDelayMax",profile.burstDelayMax,profile.burstDelayMin,900)))return false;
        const char* image=row->get("IFVImage");if(!image)return false;profile.image=image;
        if(profile.image.empty() || profile.image.size()>64 || !std::all_of(profile.image.begin(),profile.image.end(),[](char c) {
            return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_';
        }))return false;
    } else if(row->get("IFVImage"))return false;
    if((!profile.gunner || version<2) && (row->get("IFVBurstDelayMin") || row->get("IFVBurstDelayMax")))return false;
    profile.weapons.resize(weapons);profile.modes.resize(modes);
    for(int i=0;i<weapons;++i)for(int rank=0;rank<2;++rank)
        if(!integer(row,"IFVWeapon"+std::to_string(i)+(rank?"EliteReference":"Reference"),profile.weapons[i][rank],0,(int)catalog->weapons.size()-1))return false;
    for(int i=0;i<modes;++i) {
        if(!integer(row,"IFVMode"+std::to_string(i)+"Weapon",profile.modes[i].weapon,0,weapons-1)
            || !integer(row,"IFVMode"+std::to_string(i)+"Turret",profile.modes[i].turret,0,profile.turretCount-1))return false;
    }
    // Reject out-of-domain fields instead of silently retaining an inactive
    // malformed indexed reference in the embedded snapshot.
    for(const auto& [key,value]:row->kv)if(key.starts_with("IFVWeapon") || key.starts_with("IFVMode")) {
        if(key=="IFVWeapons" || key=="IFVModes")continue;
        bool known=false;
        for(int i=0;i<weapons;++i)known |= key=="IFVWeapon"+std::to_string(i)+"Reference" || key=="IFVWeapon"+std::to_string(i)+"EliteReference";
        for(int i=0;i<modes;++i)known |= key=="IFVMode"+std::to_string(i)+"Weapon" || key=="IFVMode"+std::to_string(i)+"Turret";
        if(!known)return false;
    }
    result=std::move(profile);return true;
}
