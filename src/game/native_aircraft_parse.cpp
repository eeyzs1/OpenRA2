#include "game/native_aircraft_types.h"
#include <charconv>
#include <cstring>

bool parseNativeAircraftDefinitions(const Ini& source,int types,std::vector<NativeAircraftDefinition>& destination) {
    destination.clear();const auto* header=source.find("NativeAircraft");if(!header)return true;
    if(!header->get("Version"))return false;
    const bool modern=!strcmp(header->get("Version"),"2");
    if(!modern && strcmp(header->get("Version"),"1"))return false;
    const auto integer=[](const Ini::Section* row,const char* key,int& value,int low,int high) {
        const char* text=row?row->get(key):nullptr;if(!text)return false;
        const auto r=std::from_chars(text,text+strlen(text),value);
        return r.ec==std::errc{} && !*r.ptr && value>=low && value<=high;
    };
    int count=0;if(!integer(header,"Count",count,1,16))return false;
    std::vector<NativeAircraftDefinition> staged;
    for(int i=0;i<count;++i) {
        const auto* row=source.find(("NativeAircraft."+std::to_string(i)).c_str());NativeAircraftDefinition d;
        if(!integer(row,"Launcher",d.launcher,0,types-1) || !integer(row,"Aircraft",d.aircraft,0,types-1) || d.launcher==d.aircraft
            || !integer(row,"Capacity",d.capacity,1,64) || !integer(row,"Regenerate",d.regenerate,0,900000)
            || !integer(row,"Reload",d.reload,0,900000) || !integer(row,"LaunchDelay",d.launchDelay,0,900000))return false;
        for(const auto& previous:staged)if(previous.launcher==d.launcher)return false;
        if(modern){int underwater=0,alternate=0;
            if(!integer(row,"WeaponSlot",d.weaponSlot,0,1) || !integer(row,"UnderwaterOnly",underwater,0,1)
                || !integer(row,"NoSpawnAlt",alternate,0,1))return false;
            d.underwaterOnly=underwater!=0;d.noSpawnAlt=alternate!=0;
        }
        staged.push_back(d);
    }
    destination=std::move(staged);
    return true;
}
