#include "game/native_rocket_types.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>

bool nativeSourceNumber(const char* text,float& value,float low,float high) {
    if(!text)return false;const auto end=text+strlen(text);const auto parsed=std::from_chars(text,end,value);
    return parsed.ec==std::errc{} && parsed.ptr==end && std::isfinite(value) && value>=low && value<=high;
}
namespace {
bool sourceVerses(const char* text,std::array<float,11>& values,float maximum) {
    if(!text)return false;std::string remaining=text;
    for(int i=0;i<11;++i) {
        const auto comma=remaining.find(',');auto part=remaining.substr(0,comma);
        const bool percent=!part.empty() && part.back()=='%';if(percent)part.pop_back();
        // The authored all01 V3WH has 50000% against special_1. Keep this
        // source value; a 1000% cap silently prevents the scene from loading.
        float value=0;if(!nativeSourceNumber(part.c_str(),value,0,maximum*(percent?100.f:1.f)))return false;values[i]=value/(percent?100.f:1.f);
        if((i==10)!=(comma==std::string::npos))return false;if(comma!=std::string::npos)remaining.erase(0,comma+1);
    }
    return true;
}
}
bool nativeSourceVerses(const char* text,std::array<float,11>& values) {return sourceVerses(text,values,10.f);}
bool parseNativeRocketDefinitions(const Ini& source,int types,std::vector<NativeRocketDefinition>& destination) {
    destination.clear();const auto* header=source.find("NativeRockets");if(!header)return true;
    const char* version=header->get("Version"),*countText=header->get("Count");
    if(!version || (strcmp(version,"1") && strcmp(version,"2")) || !countText)return false;
    int count=0;const auto parsed=std::from_chars(countText,countText+strlen(countText),count);
    if(parsed.ec!=std::errc{} || *parsed.ptr || count<1 || count>16 || (!strcmp(version,"1") && count!=1))return false;
    std::vector<NativeRocketDefinition> staged;
    for(int index=0;index<count;++index) {
        const auto section="NativeRocket."+std::to_string(index);
        const auto* row=source.find(section.c_str());if(!row)return false;NativeRocketDefinition d;
        const auto number=[&](const char* key,float& value,float low,float high){return nativeSourceNumber(row->get(key),value,low,high);};
        const auto integer=[&](const char* key,int& value,int low,int high){const char* text=row->get(key);if(!text)return false;
            const auto r=std::from_chars(text,text+strlen(text),value);return r.ec==std::errc{} && !*r.ptr && value>=low && value<=high;};
        int lazy=0,alternate=0;
        if(!integer("Launcher",d.launcher,0,types-1) || !integer("Missile",d.missile,0,types-1) || d.launcher==d.missile
            || !integer("Capacity",d.capacity,1,16) || !integer("Regenerate",d.regenerate,0,900000)
            || !integer("Reload",d.reload,0,900000) || !integer("LaunchDelay",d.launchDelay,0,900000) || !integer("ROF",d.rof,1,900000)
            || !integer("Pause",d.pause,0,900000) || !integer("Tilt",d.tilt,0,900000) || !integer("Altitude",d.altitude,1,100000)
            || !integer("Damage",d.damage,1,1000000) || !integer("EliteDamage",d.eliteDamage,1,1000000)
            || !integer("BodyLength",d.bodyLength,0,10000) || !integer("LazyCurve",lazy,0,1) || !integer("NoSpawnAlt",alternate,0,1)
            || !number("Range",d.range,0,128) || !number("MinimumRange",d.minimumRange,0,d.range)
            || !number("PitchInitial",d.pitchInitial,0,1) || !number("PitchFinal",d.pitchFinal,0,1)
            || !number("TurnRate",d.turnRate,0,1) || !number("Acceleration",d.acceleration,0,100)
            || !number("MaxSpeed",d.maxSpeed,0.01f,1000) || !number("YawRate",d.yawRate,0,256))return false;
        d.lazyCurve=lazy!=0;d.noSpawnAlt=alternate!=0;
        if(row->get("WeaponSlot") && !integer("WeaponSlot",d.weaponSlot,0,1))return false;
        const char* flh=row->get("FLH");if(!flh)return false;std::string values=flh;
        for(int i=0;i<3;++i){const auto comma=values.find(',');const auto value=values.substr(0,comma);
            if(!nativeSourceNumber(value.c_str(),d.flh[i],-10000,10000) || ((i==2)!=(comma==std::string::npos)))return false;
            if(comma!=std::string::npos)values.erase(0,comma+1);}
        for(auto [name,warhead]:{std::pair{"Normal",&d.normal},std::pair{"Elite",&d.elite}}) {
            const auto* w=source.find((section+"."+name).c_str());
            if(!w || !nativeSourceNumber(w->get("CellSpread"),warhead->spread,0,11)
                || !nativeSourceNumber(w->get("PercentAtMax"),warhead->percentAtMax,0,1)
                || !sourceVerses(w->get("Verses"),warhead->verses,1000.f))return false;
        }
        for(const auto& previous:staged)if(previous.launcher==d.launcher || previous.missile==d.missile
            || previous.launcher==d.missile || previous.missile==d.launcher)return false;
        staged.push_back(d);
    }
    destination=std::move(staged);return true;
}
