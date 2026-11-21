#include "game/native_crate_types.h"
#include <charconv>
#include <cmath>
#include <vector>
#include <algorithm>
#include <set>

namespace {
std::vector<std::string> fields(const std::string& text){std::vector<std::string> result;size_t start=0;
    for(;;){const auto end=text.find(',',start);result.push_back(text.substr(start,end-start));if(end==std::string::npos)return result;start=end+1;}}
bool number(const char* text,float& value,float low,float high){if(!text)return false;const std::string s=text;
    const auto parsed=std::from_chars(s.data(),s.data()+s.size(),value);
    return parsed.ec==std::errc{} && parsed.ptr==s.data()+s.size() && std::isfinite(value) && value>=low && value<=high;}
}
bool parseNativeCrateRules(const Ini& source,NativeCrateRules& out){
    out={};const auto* header=source.find("NativeCrates");if(!header)return true;
    float version=0,money=0;
    if(!number(header->get("Version"),version,1,2) || version!=(int)version || !number(header->get("Radius"),out.radius,.01f,128)
        || !number(header->get("SoloMoney"),money,0,1000000) || money!=(int)money)return false;
    const auto* powerups=source.find("NativeCrates.Powerups");if(!powerups || powerups->kv.size()!=19)return false;
    const char* names[]={"Money","Unit","HealBase","Cloak","Explosion","Napalm","Squad","Darkness","Reveal",
        "Armor","Speed","Firepower","ICBM","Invulnerability","Veteran","IonStorm","Gas","Tiberium","Pod"};
    for(int i=0;i<19;++i){const char* value=powerups->get(names[i]);if(!value)return false;const auto row=fields(value);float share=0;
        if(row.size()<3 || row.size()>4 || !number(row[0].c_str(),share,0,1000000) || share!=(int)share || row[1].size()>64
            || (row[2]!="yes" && row[2]!="no") || (row.size()==4 && !number(row[3].c_str(),out.data[i],0,1000000)))return false;
        out.shares[i]=(int)share;out.water[i]=row[2]=="yes";}
    for(int i:{9,10,11,13})if(out.data[i]<=0 || out.data[i]>100)return false;
    if(version==2){
        float minimum=0,maximum=0,freeMCV=0;
        if(!number(header->get("Minimum"),minimum,0,255) || minimum!=(int)minimum
            || !number(header->get("Maximum"),maximum,minimum,255) || maximum!=(int)maximum
            || !number(header->get("RegenMinutes"),out.regenMinutes,.01f,60)
            || !number(header->get("FreeMCV"),freeMCV,0,1) || freeMCV!=(int)freeMCV)return false;
        const auto ids=[&](const char* key,std::vector<std::string>& values){const char* text=header->get(key);if(!text)return false;
            values=fields(text);std::set<std::string> seen;if(values.empty() || values.size()>1024)return false;
            for(const auto& id:values)if(id.empty() || id.size()>64 || !seen.insert(id).second
                || !std::all_of(id.begin(),id.end(),[](char c){return (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_';}))return false;return true;};
        if(!ids("Vehicles",out.vehicles) || !ids("BaseUnits",out.baseUnits) || !ids("ConstructionYards",out.constructionYards)
            || !ids("Harvesters",out.harvesters))return false;
        const char* fixed=header->get("UnitCrateType");if(!fixed || std::string(fixed).size()>64)return false;out.unitCrateType=fixed;
        if(out.unitCrateType=="none")out.unitCrateType.clear();
        else if(std::find(out.vehicles.begin(),out.vehicles.end(),out.unitCrateType)==out.vehicles.end())return false;
        int total=0;for(int i=0;i<19;++i){total+=out.shares[i];if(out.shares[i] && i!=0 && i!=1 && i!=2 && i!=8 && i!=9 && i!=10 && i!=11 && i!=14)return false;}
        if(!total || total>19000000 || out.data[0]>999100 || out.data[14]<=0 || out.data[14]>2)return false;
        out.minimum=(int)minimum;out.maximum=(int)maximum;out.freeMCV=freeMCV!=0;out.randomCrates=true;
    }
    out.soloMoney=(int)money;out.available=true;return true;
}
