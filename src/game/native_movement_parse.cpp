#include "game/native_movement_types.h"
#include "game/native_movement_math.h"
#include "core/ini.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include <cctype>

namespace {
bool number(const Ini::Section* s,const char* key,double& result,double high=100) {
    const char* p=s?s->get(key):nullptr;if(!p)return false;
    const auto r=std::from_chars(p,p+strlen(p),result);
    return r.ec==std::errc{} && !*r.ptr && std::isfinite(result) && result>=0 && result<=high;
}
bool integer(const Ini::Section* s,const char* key,int& result,int high) {
    const char* p=s?s->get(key):nullptr;if(!p)return false;
    const auto r=std::from_chars(p,p+strlen(p),result);
    return r.ec==std::errc{} && !*r.ptr && result>=0 && result<=high;
}
const char* get(const Ini::Section* s,const char* key){const char* p=s?s->get(key):nullptr;return p?p:"";}
}

bool parseNativeMovementRules(const Ini& ini,int houses,bool required,NativeMovementRules& result) {
    result={};if(!required)return true;
    if(houses<1 || houses>(int)result.houseSpeed.size())return false;
    const auto* s=ini.find("NativeMovement");int version=0,lands=0;
    if(!integer(s,"Version",version,1) || version!=1 || !integer(s,"Lands",lands,12) || lands!=12)return false;
    if(!number(s,"TrackedUphill",result.trackedUphill) || !number(s,"TrackedDownhill",result.trackedDownhill)
       || !number(s,"WheeledUphill",result.wheeledUphill) || !number(s,"WheeledDownhill",result.wheeledDownhill)
       || !number(s,"ConditionYellow",result.conditionYellow,1) || !number(s,"HoverAcceleration",result.hoverAcceleration)
       || !number(s,"HoverBrake",result.hoverBrake) || !number(s,"HoverBoost",result.hoverBoost))return false;
    const char* names[]={"Clear","Road","Water","Rock","Wall","Tiberium","Beach","Rough","Ice","Railroad","Tunnel","Weeds"};
    const char* speeds[]={"Foot","Track","Wheel","Float","Winged","Hover","Amphibious","Creep","FloatBeach"};
    for(int land=0;land<12;++land) {
        const auto* row=ini.find(("NativeMovement.Land."+std::to_string(land)).c_str());
        if(strcmp(get(row,"Name"),names[land]))return false;
        for(int speed=0;speed<9;++speed) {
            if(speed==4)result.land[land][speed]=1;
            else if(!number(row,speeds[speed],result.land[land][speed]))return false;
            else result.land[land][speed]=NativeMovementMath::storedFloat(result.land[land][speed]);
        }
    }
    for(int house=0;house<houses;++house) {
        if(!number(ini.find(("NativeMovement.House."+std::to_string(house)).c_str()),"SpeedUnitsMult",result.houseSpeed[house]))return false;
        result.houseSpeed[house]=NativeMovementMath::storedFloat(result.houseSpeed[house]);
    }
    result.available=true;return true;
}

bool parseNativeMovementProfile(const Ini& ini,int type,NativeMovementProfile& result) {
    result={};const auto* s=ini.find(("Type."+std::to_string(type)).c_str());
    const std::string loco=get(s,"MovementLocomotor");
    if(loco=="Drive")result.locomotor=NativeLocomotor::Drive;
    else if(loco=="Ship")result.locomotor=NativeLocomotor::Ship;
    else if(loco=="Hover")result.locomotor=NativeLocomotor::Hover;
    else if(loco=="Teleport")result.locomotor=NativeLocomotor::Teleport;
    else return false;
    const char* ids[]={"","{4a582741-9839-11d1-b709-00a024ddafd1}","{2bea74e1-7cca-11d3-be14-00104b62a16c}",
        "{4a582742-9839-11d1-b709-00a024ddafd1}","{4a582747-9839-11d1-b709-00a024ddafd1}"};
    std::string original=get(s,"OriginalLocomotor");
    std::transform(original.begin(),original.end(),original.begin(),[](unsigned char c){return (char)std::tolower(c);});
    if(original!=ids[(size_t)result.locomotor])return false;
    const char* speeds[]={"Foot","Track","Wheel","Float","Winged","Hover","Amphibious","Creep","FloatBeach"};
    const auto found=std::find_if(std::begin(speeds),std::end(speeds),[&](const char* p){return !strcmp(p,get(s,"OriginalSpeedType"));});
    if(found==std::end(speeds))return false;
    result.speedType=(NativeSpeedType)(found-std::begin(speeds));
    int speed=0,accelerates=0,passive=0,veteran=0,elite=0;
    double veteranSpeed=1;
    if(!integer(s,"OriginalSpeed",speed,1000000) || !integer(s,"OriginalAccelerates",accelerates,1)
       || !integer(s,"OriginalPassive",passive,1) || !integer(s,"OriginalSlowdownDistance",result.slowdownDistance,1000000)
       || !number(s,"OriginalAccelerationFactor",result.acceleration) || !number(s,"OriginalDeaccelerationFactor",result.deacceleration)
       || !integer(s,"VeteranFaster",veteran,1) || !integer(s,"EliteFaster",elite,1) || !number(s,"MovementVeteranSpeed",veteranSpeed))return false;
    result.maxSpeed=std::min(255,std::min(speed,100)*256/100);
    result.accelerates=accelerates!=0;result.passive=passive!=0;
    if(veteran)result.veteranSpeed[1]=veteranSpeed;if(elite)result.veteranSpeed[2]=veteranSpeed;
    return true;
}
