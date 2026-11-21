#pragma once
#include <string>
#include <vector>
#include <utility>
#include <array>
#include <cstdint>
#include "core/ini.h"
#include "game/support_types.h"

constexpr int NATIVE_PRODUCTION_BASE=10000;
enum class NativeSpyEffect : uint8_t { None, Money, Power, Radar, Infantry, Vehicles, Technology, Superweapons };
struct NativeProductionProfile {
    bool available=false,baseNormal=true,naval=false,crateGoodie=true,startAllowed=true;
    int techLevel=-1,category=0,factory=-1,buildLimit=2147483647,stepTicks=2,baseFrames=0,adjacent=2,order=4095;
    int infantryExit=-1; // historical source: fixed project door; 0 generic, 1 GDI, 2 Nod
    std::vector<std::string> owners,required,forbidden,prerequisites;
    std::string freeUnit,cameo;
    int requiredStolenTech=0; // Source house technologies: allied=1, Soviet=2, third=4.
    bool factoryPlant=false,powered=false;
    unsigned superweaponMask=0,supportMask=0; // Source building SuperWeapon/SuperWeapon2 links.
    unsigned spyResetWeapons=0,spyResetSupport=0;
    std::array<float,5> costBonus{1,1,1,1,1}; // Infantry, vehicles/naval, aircraft, buildings, defenses.
    int soylent=-1,passengers=0,sizeLimit=0,extraPower=0,spyTechMask=0;
    bool grinding=false,infantryAbsorb=false,unitAbsorb=false,bunker=false,cloning=false,cloneable=true,spyable=false;
    NativeSpyEffect spyEffect=NativeSpyEffect::None;
    int rotationRate=0; // Production8: original ROT, in 256ths of a full 16-bit turn per frame.
};
struct NativeProductionRules {
    bool available=false,sourceModes=false,oreGrowth=true;int queueLimit=29,version=0;
    float buildSpeed=.7f,multipleFactory=.8f,minLowPower=.5f,maxLowPower=.8f,powerPenalty=1;
    std::vector<std::string> countries;
    std::vector<int> techLevels;
    std::vector<std::pair<std::string,std::vector<std::string>>> groups;
    std::vector<std::pair<std::string,int>> originalHouses;
    bool economy=false; // Version 2; old embedded definitions retain their original contracts.
    int oreValue=35,gemValue=70; // Version6 reads source prices; older saves retain project values.
    int harvesterLoadRate=0; // Version7: original frames per mining stage; earlier scenes retain20tick pickups.
    int harvesterDumpFrames=0; // Version8: ceil(HarvesterDumpRate * 900).
    float purifierBonus=0;
    unsigned superweaponMask=127;
    float spyMoneyFraction=.5f;int spyBlackoutTicks=2000;bool spyTechByOwner=false;
    std::array<SupportRule,SUPPORT_COUNT> support;
    SupportDeliveryRules delivery;
};
bool parseNativeProductionProfile(const Ini::Section* row,NativeProductionProfile& out,int version=1);
bool parseNativeProductionRules(const Ini& ini,int houses,NativeProductionRules& out);
bool prepareNativeSourceMode(Ini& ini,int types,int mode,NativeProductionRules& out);
