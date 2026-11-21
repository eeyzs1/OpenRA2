#include "game/native_crew_types.h"
#include <charconv>
#include <cstring>
#include <cmath>

namespace {
template<class T>bool field(const Ini::Section* row,const char* key,T& value,T low,T high) {
    const char* text=row?row->get(key):nullptr;if(!text)return false;
    const auto parsed=std::from_chars(text,text+std::strlen(text),value);
    return parsed.ec==std::errc{} && !*parsed.ptr && value>=low && value<=high;
}
}
bool parseNativeCrewRules(const Ini& ini,bool required,NativeCrewRules& result) {
    result={};const auto* row=ini.find("NativeCrew");
    if(!required)return !row;
    int version=0,mode=0;NativeCrewRules p;p.enabled=true;
    if(!field(row,"Version",version,1,2) || !field(row,"OccupyMode",mode,0,1) || !field(row,"OpenRange",p.openRange,0,128)
        || !field(row,"WarpDistance",p.warpDistance,0,128)
        || !field(row,"OpenDamage",p.openDamage,0.f,100.f)
        || !field(row,"OccupyDamage",p.occupyDamage,0.f,100.f)
        || !field(row,"OccupyROF",p.occupyROF,.01f,100.f)
        || !field(row,"OccupyRange",p.occupyRange,0,128))return false;
    p.version=version;p.passengerOccupy=mode!=0;result=p;return true;
}
bool parseNativeCrewProfile(const Ini::Section* row,int version,const NativeWeaponCatalog* catalog,NativeCrewProfile& result) {
    result={};
    if(!version) {
        if(row)for(const auto& [key,value]:row->kv)if(key.starts_with("Crew"))return false;
        return true;
    }
    if(version<1 || version>2)return false;
    if(row)for(const auto& [key,value]:row->kv)if(key.starts_with("Crew")
        && key!="CrewOpenTopped" && key!="CrewOccupier" && key!="CrewOccupyWeapon" && key!="CrewEliteOccupyWeapon"
        && (version<2 || (key!="CrewTransportWeapon" && key!="CrewTransportReference" && key!="CrewEliteTransportReference")))return false;
    int open=0,occupier=0;NativeCrewProfile p;p.available=true;
    if(!catalog || !field(row,"CrewOpenTopped",open,0,1) || !field(row,"CrewOccupier",occupier,0,1)
        || !field(row,"CrewOccupyWeapon",p.occupyWeapons[0],-1,(int)catalog->weapons.size()-1)
        || !field(row,"CrewEliteOccupyWeapon",p.occupyWeapons[1],-1,(int)catalog->weapons.size()-1))return false;
    p.openTopped=open!=0;p.occupier=occupier!=0;
    if(p.occupier && p.occupyWeapons[0]<0)return false;
    if(version>=2) {
        if(!field(row,"CrewTransportWeapon",p.transportWeapon,-1,31)
            || !field(row,"CrewTransportReference",p.transportWeapons[0],-1,(int)catalog->weapons.size()-1)
            || !field(row,"CrewEliteTransportReference",p.transportWeapons[1],-1,(int)catalog->weapons.size()-1))return false;
        if(p.transportWeapon<0 && (p.transportWeapons[0]>=0 || p.transportWeapons[1]>=0))return false;
    }
    result=p;return true;
}
