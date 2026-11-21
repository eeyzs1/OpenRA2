#include "game/native_target_types.h"
#include "game/native_rocket_types.h"
#include <cstring>
#include <string>

bool parseNativeTargetProfile(const Ini::Section* section,NativeTargetProfile& profile) {
    if(!section || !section->get("TargetProfile"))return false;
    const auto flag=[&](const char* key,bool& destination) {
        const char* text=section->get(key);if(!text || (strcmp(text,"0") && strcmp(text,"1")))return false;
        destination=*text=='1';return true;
    };
    if(!flag("TargetProfile",profile.available) || !profile.available
        || !flag("TargetLegal",profile.legal) || !flag("TargetInvisible",profile.invisible)
        || !flag("TargetStorage",profile.storage) || !flag("TargetFactory",profile.factory)
        || !flag("TargetUndeploys",profile.undeploys) || !flag("TargetDetectDisguise",profile.detectDisguise)
        || !flag("TargetAIDetectDisguise",profile.aiDetectDisguise)
        || !nativeSourceNumber(section->get("TargetSpecialThreat"),profile.specialThreat,-1000000,1000000))return false;
    const char* coefficients[]={"TargetMyEffectiveness","TargetEffectiveness","TargetSpecialCoefficient","TargetStrength","TargetDistance"};
    for(int i=0;i<5;++i)if(!nativeSourceNumber(section->get(coefficients[i]),profile.coefficients[i],-1000000,1000000))return false;
    for(int i=0;i<2;++i) {
        const std::string prefix="TargetWeapon"+std::to_string(i);auto& weapon=profile.weapons[i];
        if(!flag((prefix+"Present").c_str(),weapon.present))return false;
        if(!weapon.present)continue;
        if(!flag((prefix+"AA").c_str(),weapon.air) || !flag((prefix+"AG").c_str(),weapon.ground)
            || !nativeSourceNumber(section->get((prefix+"Range").c_str()),weapon.range,-2,512)
            || !nativeSourceVerses(section->get((prefix+"Verses").c_str()),weapon.verses))return false;
    }
    return true;
}
