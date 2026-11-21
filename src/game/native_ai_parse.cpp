#include "game/native_ai_types.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <set>

namespace {
std::string lower(std::string s){for(auto& c:s)c=(char)std::tolower((unsigned char)c);return s;}
std::string field(const Ini::Section* row,const char* key,const char* fallback=""){const auto v=row?row->get(key):nullptr;return v?v:fallback;}
std::vector<std::string> split(const std::string& s){std::vector<std::string> v;size_t start=0;for(;;){auto end=s.find(',',start);v.push_back(s.substr(start,end-start));if(end==std::string::npos)return v;start=end+1;}}
bool integer(const std::string& s,int& n,int lo,int hi){const auto r=std::from_chars(s.data(),s.data()+s.size(),n);return r.ec==std::errc{} && r.ptr==s.data()+s.size() && n>=lo && n<=hi;}
bool real(const std::string& s,double& n){const auto r=std::from_chars(s.data(),s.data()+s.size(),n);return r.ec==std::errc{} && r.ptr==s.data()+s.size() && std::isfinite(n) && std::abs(n)<=1000000;}
bool triple(const Ini::Section* row,const char* key,std::array<int,3>& out,int low,int high){const auto v=split(field(row,key));if(v.size()!=3)return false;for(int i=0;i<3;++i)if(!integer(v[i],out[i],low,high))return false;return true;}
}

bool parseNativeAIRules(const Ini& source,const NativeEventDefinition* events,int houses,NativeAIRules& out) {
    out={};const auto* header=source.find("NativeAI");if(!header)return true;
    int version=0,ignore=0,defense=0;
    if(!events || !integer(field(header,"Version"),version,1,nativeGlobalAIContracts.back().aiVersion)
        || !integer(field(header,"IgnoreGlobalAITriggers"),ignore,0,1) || !integer(field(header,"UseMinDefenseRule"),defense,0,1)
        || !triple(header,"TeamDelays",out.delays,1,1000000) || !triple(header,"TotalAITeamCap",out.cap,0,4096)
        || !triple(header,"MinimumAIDefensiveTeams",out.minimumDefense,0,4096) || !triple(header,"MaximumAIDefensiveTeams",out.maximumDefense,0,4096)
        || !triple(header,"FillEarliestTeamProbability",out.fillEarliest,0,100)
        || !real(field(header,"AITriggerSuccessWeightDelta"),out.successDelta) || !real(field(header,"AITriggerFailureWeightDelta"),out.failureDelta)
        || !real(field(header,"AITriggerTrackRecordCoefficient"),out.historyCoefficient))return false;
    out.ignoreGlobal=ignore!=0;out.useMinDefense=defense!=0;
    out.globalContract=nativeGlobalAIContractForRules(version);
    if(out.globalContract!=events->globalContract)return false;
    if(out.hasOriginalSourceCoordinates()) {int enabled=0;if(!integer(field(header,"OriginalSourceCoordinates"),enabled,1,1))return false;}
    if(out.hasOriginalNearbyGeometry()) {
        int enabled=0;
        if(!integer(field(header,"OriginalNearbyGeometry"),enabled,1,1))return false;
        const auto* speeds=source.find("NativeAI.NearbySpeeds");const auto* walls=source.find("NativeAI.NearbyWalls");
        if(!speeds || speeds->kv.empty() || speeds->kv.size()>4096 || !walls || walls->kv.size()>255)return false;
        for(const auto& [name,text]:speeds->kv){int speed=0;
            if(name.empty() || name.size()>64 || !integer(text,speed,0,8) || !out.nearbySpeeds.emplace(lower(name),speed).second)return false;}
        for(const auto& [name,text]:walls->kv){int overlay=0,crushable=0;
            if(!integer(name,overlay,0,254) || !integer(text,crushable,0,1) || !out.nearbyWalls.emplace(overlay,crushable!=0).second)return false;}
    }
    if(out.hasOriginalTeamControl()) {
        int enabled=0;
        if(!integer(field(header,"OriginalTeamControl"),enabled,1,1))return false;
        out.baseBuildings=split(field(header,"BaseBuildings"));
        if(out.baseBuildings.empty() || out.baseBuildings.size()>64)return false;
        for(auto& name:out.baseBuildings){name=lower(name);if(name.empty() || name.size()>64)return false;}
        const auto* ratings=source.find("NativeAI.Leadership");if(!ratings || ratings->kv.empty() || ratings->kv.size()>4096)return false;
        for(const auto& [name,text]:ratings->kv){int rating=0;
            if(name.empty() || name.size()>64 || !integer(text,rating,-1000000,1000000) || !out.leadership.emplace(lower(name),rating).second)return false;}
    }
    if(out.isGlobal()){int global=0;double ready=0;
        if(!events->aiOnly || out.ignoreGlobal || !integer(field(header,"Global"),global,1,1)
            || !real(field(header,"AIMinorSuperReadyPercent"),ready) || ready<0 || ready>1)return false;
        out.minorSuperReady=(float)ready;
        // Existing experimental World61 saves predate this exported key.
        if(!integer(field(header,"AISafeDistance","20"),out.safeDistance,0,1024))return false;
        const auto* countries=source.find("NativeAI.CountrySides");if(!countries || countries->kv.empty() || countries->kv.size()>64)return false;
        for(const auto& [country,text]:countries->kv){int side=0;if(country.empty() || country.size()>64 || !integer(text,side,1,32)
            || !out.countrySides.emplace(lower(country),side).second)return false;}
        out.countries.assign(houses,"");out.ratios.assign(houses,100);
    } else if(events->aiOnly || header->get("Global") || source.find("NativeAI.CountrySides"))return false;
    for(int i=0;!out.isGlobal() && i<houses;++i){const auto* row=source.find(("NativeAI.House."+std::to_string(i)).c_str());int ratio=0;
        auto country=lower(field(row,"Country"));if(country.empty() || country.size()>64 || !integer(field(row,"RatioAITriggerTeam"),ratio,0,100))return false;
        out.countries.push_back(country);out.ratios.push_back(ratio);}
    const auto team=[&](const std::string& id){for(int i=0;i<(int)events->teams.size();++i)if(lower(events->teams[i].id)==lower(id))return i;return -1;};
    const auto* rows=source.find("NativeAI.Triggers");const auto* enabled=source.find("NativeAI.Enabled");
    if(!rows || !enabled || rows->kv.size()>4096 || enabled->kv.size()>8192)return false;
    std::set<std::string> ids;
    for(const auto& [key,text]:rows->kv){NativeAITriggerDefinition t;t.id=lower(key);const auto f=split(text);
        if(key.empty() || key.size()>64 || !ids.insert(t.id).second || f.size()!=18 || f[0].size()>256)return false;
        t.name=f[0];t.first=team(f[1]);t.second=team(f[14]);if(t.first<0 || (lower(f[14])!="<none>" && t.second<0))return false;
        const auto owner=lower(f[2]);t.house=owner=="<all>"?-2:-1;
        if(out.isGlobal() && owner!="<all>"){if(!out.countrySides.contains(owner))return false;t.country=owner;}
        else for(int i=0;i<houses;++i)if(out.countries[i]==owner)t.house=i;
        int ignored=0,skirmish=0,base=0;
        if(!integer(f[3],t.techLevel,-1,100) || !integer(f[4],t.condition,-1,7) || f[5].size()>64 || f[6].size()!=64
            || !real(f[7],t.weight) || !real(f[8],t.minimum) || !real(f[9],t.maximum) || t.minimum<0 || t.maximum<t.minimum
            || !integer(f[10],skirmish,0,1) || !integer(f[11],ignored,0,100) || !integer(f[12],t.side,0,32) || !integer(f[13],base,0,1))return false;
        uint8_t bytes[32]{};for(int i=0;i<32;++i){unsigned n=0;const auto r=std::from_chars(f[6].data()+i*2,f[6].data()+i*2+2,n,16);if(r.ec!=std::errc{} || r.ptr!=f[6].data()+i*2+2)return false;bytes[i]=(uint8_t)n;}
        const auto word=[&](int offset){uint32_t n=0;for(int j=0;j<4;++j)n|=(uint32_t)bytes[offset+j]<<(8*j);return (int32_t)n;};
        t.operand=word(0);t.comparison=word(4);if(t.comparison<0 || t.comparison>5)return false;
        t.conditionObject=lower(f[5]);t.skirmish=skirmish!=0;t.baseDefense=base!=0;
        for(int i=0;i<3;++i){int flag=0;if(!integer(f[15+i],flag,0,1))return false;t.difficulty[i]=flag!=0;}
        t.enabled=out.isGlobal() || lower(field(enabled,key.c_str(),"no"))=="yes";out.triggers.push_back(std::move(t));
    }
    for(const auto& row:source.sections)if(row.name.rfind("NativeAI.Recruit.",0)==0){const auto object=field(&row,"Object");int a=0,b=0,g=0;
        if(object.empty() || object.size()>128 || !integer(field(&row,"Recruitable"),a,0,1) || !integer(field(&row,"AutocreateRecruitable"),b,0,1)
            || !integer(field(&row,"Group"),g,-2,1000000) || !out.originalRecruit.emplace(object,NativeRecruitState{a!=0,b!=0,g}).second)return false;}
    out.available=true;return true;
}
