#include "game/native_production_types.h"
#include "game/native_rocket_types.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <limits>

namespace {
std::string upper(std::string s){for(auto& c:s)if(c>='a' && c<='z')c-=32;return s;}
bool number(const Ini::Section* row,const char* key,int& n,int low,int high) {
    const auto* text=row?row->get(key):nullptr;if(!text)return false;const auto end=text+strlen(text);
    const auto result=std::from_chars(text,end,n);return result.ec==std::errc{} && result.ptr==end && n>=low && n<=high;
}
bool words(const char* text,std::vector<std::string>& out) {
    if(!text)return false;std::string s=text;if(s.size()>4096)return false;if(s.empty())return true;
    for(size_t i=0;i<s.size();) {
        const auto end=s.find(',',i);auto word=s.substr(i,end==std::string::npos?end:end-i);
        const auto begin=word.find_first_not_of(" \t");if(begin==std::string::npos)return false;word=word.substr(begin,word.find_last_not_of(" \t")-begin+1);
        if(word.size()>64 || !std::all_of(word.begin(),word.end(),[](char c){return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_';}))return false;
        out.push_back(upper(word));if(out.size()>64)return false;if(end==std::string::npos)break;i=end+1;if(i==s.size())return false;
    }return true;
}
}

bool parseNativeProductionProfile(const Ini::Section* row,NativeProductionProfile& out,int version) {
    out={};int normal=0,naval=0;
    if(!number(row,"TechLevel",out.techLevel,-1,1000) || !number(row,"Category",out.category,0,5)
        || !number(row,"BuildLimit",out.buildLimit,std::numeric_limits<int>::min(),std::numeric_limits<int>::max())
        || !number(row,"StepTicks",out.stepTicks,2,510) || !number(row,"BaseBuildFrames",out.baseFrames,0,100000000)
        || !number(row,"Order",out.order,0,4095)
        || !number(row,"Adjacent",out.adjacent,0,64) || !number(row,"BaseNormal",normal,0,1) || !number(row,"Naval",naval,0,1))return false;
    if(out.stepTicks!=2*std::clamp(out.baseFrames/54,1,255))return false;
    if(row->get("InfantryExit") && !number(row,"InfantryExit",out.infantryExit,0,2))return false;
    int crate=1,start=1;
    if((row->has("CrateGoodie") && !number(row,"CrateGoodie",crate,0,1))
        || (row->has("StartAllowed") && !number(row,"StartAllowed",start,0,1)))return false;
    out.crateGoodie=crate!=0;out.startAllowed=start!=0;
    if(!words(row->get("Owner"),out.owners) || !words(row->get("RequiredHouses"),out.required)
        || !words(row->get("ForbiddenHouses"),out.forbidden) || !words(row->get("Prerequisite"),out.prerequisites))return false;
    const auto* factory=row->get("Factory");if(!factory)return false;const auto f=upper(factory);
    if(f=="NONE" || f.empty())out.factory=-1;
    else if(f=="INFANTRYTYPE")out.factory=0;else if(f=="UNITTYPE")out.factory=1;
    else if(f=="AIRCRAFTTYPE")out.factory=2;else if(f=="BUILDINGTYPE")out.factory=4;else return false;
    const auto* free=row->get("FreeUnit");const auto* cameo=row->get("Cameo");if(!free || !cameo)return false;
    std::vector<std::string> units,icons;if(!words(free,units) || units.size()>1 || !words(cameo,icons) || icons.size()>1)return false;
    out.freeUnit=units.empty()?"":units.front();out.cameo=icons.empty()?"":icons.front();
    if(version>=2) {
        int plant=0,powered=0;
        if(!number(row,"RequiredStolenTech",out.requiredStolenTech,0,7)
            || !number(row,"FactoryPlant",plant,0,1) || !number(row,"Powered",powered,0,1))return false;
        out.factoryPlant=plant!=0;out.powered=powered!=0;
        int weapons=0,support=0;
        if(!number(row,"SuperweaponMask",weapons,0,127) || !number(row,"SupportMask",support,0,version>=4?15:7))return false;
        out.superweaponMask=(unsigned)weapons;out.supportMask=(unsigned)support;
        constexpr const char* bonuses[]={"InfantryCostBonus","UnitsCostBonus","AircraftCostBonus","BuildingsCostBonus","DefensesCostBonus"};
        for(int i=0;i<5;++i)if(!nativeSourceNumber(row->get(bonuses[i]),out.costBonus[i],0,100))return false;
    }
    if(version>=3) {
        if(!number(row,"Soylent",out.soylent,-1,10000000) || !number(row,"Passengers",out.passengers,0,64)
            || !number(row,"SizeLimit",out.sizeLimit,0,64) || !number(row,"ExtraPower",out.extraPower,-10000000,10000000)
            || !number(row,"SpyTechMask",out.spyTechMask,0,7))return false;
        const char* names[]={"Grinding","InfantryAbsorb","UnitAbsorb","Bunker","Cloning","Cloneable","Spyable"};
        bool* values[]={&out.grinding,&out.infantryAbsorb,&out.unitAbsorb,&out.bunker,&out.cloning,&out.cloneable,&out.spyable};
        for(int i=0;i<7;++i){int value=0;if(!number(row,names[i],value,0,1))return false;*values[i]=value!=0;}
        int effect=0;if(!number(row,"SpyEffect",effect,0,(int)NativeSpyEffect::Superweapons))return false;
        out.spyEffect=(NativeSpyEffect)effect;
        if((out.spyEffect==NativeSpyEffect::Technology)!=(out.spyTechMask!=0))return false;
    }
    if(version>=4) {
        int weapons=0,support=0;
        if(!number(row,"SpyResetWeapons",weapons,0,127) || !number(row,"SpyResetSupport",support,0,15))return false;
        out.spyResetWeapons=(unsigned)weapons;out.spyResetSupport=(unsigned)support;
        if((out.spyResetWeapons&~out.superweaponMask) || (out.spyResetSupport&~out.supportMask))return false;
    }
    if(version>=8 && !number(row,"ROT",out.rotationRate,0,127))return false;
    if(version<8 && row->has("ROT"))return false;
    out.baseNormal=normal!=0;out.naval=naval!=0;out.available=true;return true;
}

bool parseNativeProductionRules(const Ini& ini,int houses,NativeProductionRules& out) {
    out={};const auto* header=ini.find("NativeProduction");if(!header)return true;int version=0;
    if(!number(header,"Version",version,1,9) || !number(header,"MaximumQueuedObjects",out.queueLimit,0,255)
        || !nativeSourceNumber(header->get("BuildSpeed"),out.buildSpeed,.001f,100)
        || !nativeSourceNumber(header->get("MultipleFactory"),out.multipleFactory,0,10)
        || !nativeSourceNumber(header->get("MinLowPowerProductionSpeed"),out.minLowPower,.01f,1)
        || !nativeSourceNumber(header->get("MaxLowPowerProductionSpeed"),out.maxLowPower,out.minLowPower,1)
        || !nativeSourceNumber(header->get("LowPowerPenaltyModifier"),out.powerPenalty,0,100))return false;
    out.version=version;out.economy=version>=2;
    if(version>=6) {
        if(!number(header,"OreValue",out.oreValue,0,1000000) || !number(header,"GemValue",out.gemValue,0,1000000))return false;
    } else if(header->has("OreValue") || header->has("GemValue"))return false;
    if(version>=7) {
        if(!number(header,"HarvesterLoadRate",out.harvesterLoadRate,1,1000))return false;
    } else if(header->has("HarvesterLoadRate"))return false;
    if(version>=8) {
        if(!number(header,"HarvesterDumpFrames",out.harvesterDumpFrames,1,1000000)
            || !nativeSourceNumber(header->get("PurifierBonus"),out.purifierBonus,0,100))return false;
    } else if(header->has("HarvesterDumpFrames") || header->has("PurifierBonus"))return false;
    if(version>=5) {
        const auto* row=ini.find("NativeProduction.Delivery");auto& d=out.delivery;
        if(!number(row,"FlightLevel",d.flightLevel,1,100000) || !number(row,"ParachuteMaxFallRate",d.parachuteFall,-1000,-1)
            || !number(row,"ParadropRadius",d.dropRadius,1,32768) || !number(row,"SpyRevealRadius",d.spyRadius,1,128)
            || !number(row,"SpyCameraRange",d.spyRange,1,128))return false;
    }
    if(out.economy){int mask=0;if(!number(header,"SuperweaponMask",mask,0,127))return false;out.superweaponMask=(unsigned)mask;}
    if(version>=3) {
        int byOwner=0;
        if(!nativeSourceNumber(header->get("SpyMoneyFraction"),out.spyMoneyFraction,0,1)
            || !number(header,"SpyBlackoutTicks",out.spyBlackoutTicks,0,1800000)
            || !number(header,"SpyTechByOwner",byOwner,0,1))return false;
        out.spyTechByOwner=byOwner!=0;
    }
    if(version>=4) {
        constexpr const char* sides[]={"Allied","Soviet","Yuri"};
        for(int i=0;i<SUPPORT_COUNT;++i) {
            const auto* row=ini.find(("NativeProduction.Support."+std::to_string(i)).c_str());
            auto& rule=out.support[i];int available=0,powered=0;
            if(!number(row,"Available",available,0,1) || !number(row,"IsPowered",powered,0,1)
                || !number(row,"RechargeTicks",rule.rechargeTicks,0,1800000))return false;
            rule.available=available!=0;rule.powered=powered!=0;
            if(rule.available?(rule.rechargeTicks<2 || rule.rechargeTicks%2):rule.rechargeTicks!=0 || rule.powered)return false;
            const char* icon=row->get("Icon");if(!icon)return false;rule.icon=icon;
            if(rule.available) {
                if(!rule.icon.starts_with("assets/rules/profiles/") || !rule.icon.ends_with(".png")
                    || rule.icon.size()>256 || rule.icon.find("..")!=std::string::npos
                    || !std::all_of(rule.icon.begin(),rule.icon.end(),[](char c){return (c>='a'&&c<='z') || (c>='A'&&c<='Z')
                        || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.' || c=='/';}))return false;
            } else if(!rule.icon.empty())return false;
            for(int side=0;side<3;++side) {
                std::vector<std::string> infantry;const std::string prefix=sides[side];
                if(!words(row->get((prefix+"Infantry").c_str()),infantry) || infantry.size()>1
                    || !number(row,(prefix+"Count").c_str(),rule.count[side],0,64))return false;
                rule.infantry[side]=infantry.empty()?"":infantry.front();
                const bool payload=rule.available && i<2;
                if(payload && side<2 && (rule.infantry[side].empty() || rule.count[side]==0))return false;
                if(rule.infantry[side].empty()!=(rule.count[side]==0))return false;
                if(!payload && rule.count[side]!=0)return false;
            }
        }
    }
    const auto* groups=ini.find("NativeProduction.Prerequisites");if(!groups)return false;
    for(const auto& [key,value]:groups->kv){std::vector<std::string> wordsInGroup;if(!words(value.c_str(),wordsInGroup))return false;out.groups.emplace_back(upper(key),std::move(wordsInGroup));}
    for(int i=0;i<houses;++i) {
        const auto* row=ini.find(("NativeProduction.House."+std::to_string(i)).c_str());int tech=-1;std::vector<std::string> country;
        if(!number(row,"TechLevel",tech,-1,1000) || !words(row->get("Country"),country) || country.size()!=1)return false;
        out.techLevels.push_back(tech);out.countries.push_back(country.front());
    }
    int actors=0;if(!number(ini.find("Scene"),"Actors",actors,0,65536))return false;
    for(int i=0;i<actors;++i){const auto* row=ini.find(("Actor."+std::to_string(i)).c_str());int house=-1;
        const auto* key=row?row->get("Key"):nullptr;
        if(!key || strlen(key)>128 || !number(row,"House",house,-1,houses-1))return false;
        out.originalHouses.emplace_back(key,house);}
    out.available=true;return true;
}

bool prepareNativeSourceMode(Ini& ini,int types,int mode,NativeProductionRules& out) {
    constexpr int modes=7;int version=0,count=0;
    if(mode<0 || mode>=modes || !number(ini.find("Rules.Modes"),"Version",version,1,1)
        || !number(ini.find("Rules.Modes"),"Count",count,modes,modes))return false;
    std::vector<Ini::Section> base;
    for(int i=0;i<types;++i){const auto* row=ini.find(("NativeProduction.Type."+std::to_string(i)).c_str());
        if(!row || !row->has("CrateGoodie") || !row->has("StartAllowed"))return false;base.push_back(*row);}
    auto active=base;
    for(int m=0;m<modes;++m){const std::string prefix="Rules.Mode."+std::to_string(m);const auto* row=ini.find(prefix.c_str());
        int original=0,growth=1,changes=0;
        if(!number(row,"Original",original,0,1) || !number(row,"OreGrowth",growth,0,1)
            || !number(row,"Overrides",changes,0,types) || original!=(m!=4))return false;
        const char* file=row->get("File"),*archive=row->get("Archive"),*hash=row->get("SHA256");std::vector<std::string> unmapped;
        if(!file || !archive || !hash || !words(row->get("Unmapped"),unmapped))return false;
        if(original){if(!*file || strlen(file)>64 || !*archive || strlen(archive)>256 || strlen(hash)!=64
            || !std::all_of(hash,hash+64,[](char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f');}))return false;}
        else if(*file || *archive || *hash || changes || !unmapped.empty())return false;
        std::vector<uint8_t> seen(types,0);
        for(int i=0;i<changes;++i){const auto* delta=ini.find((prefix+".Override."+std::to_string(i)).c_str());int type=-1;
            if(!number(delta,"Type",type,0,types-1) || seen[type]++ || delta->kv.size()<2)return false;
            auto merged=base[type];
            for(const auto& [key,value]:delta->kv){if(key=="Type")continue;
                if(key!="TechLevel" && key!="Prerequisite" && key!="CrateGoodie" && key!="StartAllowed")return false;
                auto field=std::find_if(merged.kv.begin(),merged.kv.end(),[&](const auto& p){return p.first==key;});
                if(field==merged.kv.end())return false;field->second=value;}
            NativeProductionProfile checked;if(!parseNativeProductionProfile(&merged,checked,out.version))return false;
            if(m==mode)active[type]=std::move(merged);
        }
        if(m==mode)out.oreGrowth=growth!=0;
    }
    for(auto& row:ini.sections)for(auto& selected:active)if(row.name==selected.name){row=std::move(selected);break;}
    out.sourceModes=true;return true;
}
