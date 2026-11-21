#include "game/native_event_types.h"
#include "game/native_rocket_types.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <set>

namespace {
std::string lower(std::string value){for(auto& c:value)c=(char)std::tolower((unsigned char)c);return value;}
std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> result;size_t begin=0;
    for(;;){const auto end=text.find(',',begin);result.push_back(text.substr(begin,end-begin));if(end==std::string::npos)break;begin=end+1;}
    return result;
}
bool number(const std::string& text,int& value,int low=0,int high=1000000) {
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return result.ec==std::errc{} && result.ptr==text.data()+text.size() && value>=low && value<=high;
}
std::string get(const Ini::Section* row,const char* key,const char* fallback=""){const char* p=row?row->get(key):nullptr;return p?p:fallback;}
template<class T>int index(const std::vector<T>& rows,const std::string& id){const auto key=lower(id);for(int i=0;i<(int)rows.size();++i)if(lower(rows[i].id)==key)return i;return -1;}
bool coordinate(const std::string& text,int width,int height,Vec2i& point) {
    int encoded=0;if(!number(text,encoded,0,999999))return false;point={encoded%1000,encoded/1000};
    return point.x<width && point.y<height;
}
bool records(const Ini& source,const char* table,std::vector<std::string>& ids) {
    const auto* list=source.find(table);if(!list || list->kv.size()>4096)return false;
    std::set<std::string> unique;
    for(const auto& [key,value]:list->kv){int n=0;if(!number(key,n,0,4095) || value.empty() || value.size()>64 || !unique.insert(lower(value)).second)return false;ids.push_back(lower(value));}
    return true;
}
}

bool parseNativeEventDefinitions(const Ini& source,int width,int height,int houses,
    std::shared_ptr<const NativeEventDefinition>& destination) {
    destination.reset();const auto* header=source.find("NativeEvents");if(!header)return true;
    auto d=std::make_shared<NativeEventDefinition>();int version=0,expectedTriggers=0,expectedTags=0;
    if(!number(get(header,"Version"),version,1,nativeGlobalAIContracts.back().eventVersion) || !number(get(header,"Triggers"),expectedTriggers,0,4096)
        || !number(get(header,"Tags"),expectedTags,0,4096) || !number(get(header,"RevealRadius"),d->revealRadius,0,128))return false;
    d->orderedActions=version>=3;
    d->extendedConditions=version>=4;
    int aiOnly=0;
    if(version>=5){if(!number(get(header,"AIOnly"),aiOnly,1,1) || expectedTriggers || expectedTags)return false;d->aiOnly=true;}
    else if(header->get("AIOnly"))return false;
    if(version>=2) {
        const auto* row=source.find("NativeEvent.Demolition");if(!row)return false;auto& blast=d->demolition;
        blast.warhead=get(row,"Warhead");
        if(blast.warhead.empty() || blast.warhead.size()>64 || !number(get(row,"BridgeStrength"),blast.bridgeStrength,1,1000000)
            || !number(get(row,"FlightLevel"),blast.flightLevel,0,100000)
            || !nativeSourceNumber(row->get("CellSpread"),blast.spread,0,11)
            || !nativeSourceNumber(row->get("PercentAtMax"),blast.percentAtMax,0,1)
            || !nativeSourceVerses(row->get("Verses"),blast.verses))return false;
        int wall=0,wood=0,tiberium=0;
        if(!number(get(row,"Wall"),wall,0,1) || !number(get(row,"Wood"),wood,0,1)
            || !number(get(row,"Tiberium"),tiberium,0,1))return false;
        blast.wall=wall!=0;blast.wood=wood!=0;blast.tiberium=tiberium!=0;
        blast.available=true;
    }
    const auto stray=get(header,"StrayDistance","2");const auto strayResult=std::from_chars(stray.data(),stray.data()+stray.size(),d->strayDistance);
    if(strayResult.ec!=std::errc{} || strayResult.ptr!=stray.data()+stray.size() || !std::isfinite(d->strayDistance)
        || d->strayDistance<0 || d->strayDistance>128)return false;
    d->globalContract=nativeGlobalAIContractForEvents(version);
    if(d->hasOriginalTeamControl()) {
        const auto relaxed=get(header,"RelaxedStrayDistance");
        const auto result=std::from_chars(relaxed.data(),relaxed.data()+relaxed.size(),d->relaxedStrayDistance);
        if(result.ec!=std::errc{} || result.ptr!=relaxed.data()+relaxed.size() || !std::isfinite(d->relaxedStrayDistance)
            || d->relaxedStrayDistance<0 || d->relaxedStrayDistance>128)return false;
    }
    int fog=0;if(!number(get(header,"PlayerHouse"),d->playerHouse,d->aiOnly?-1:0,d->aiOnly?-1:houses-1) || !number(get(header,"FogOfWar"),fog,0,1))return false;d->fogOfWar=fog!=0;
    const auto* houseNames=source.find("NativeEvent.houses");const auto* houseNumbers=source.find("NativeEvent.houseNumbers");
    if(!houseNames || !houseNumbers)return false;
    if(d->aiOnly && (!houseNames->kv.empty() || !houseNumbers->kv.empty()))return false;
    const auto house=[&](const std::string& name){for(const auto& [key,value]:houseNames->kv)if(lower(key)==lower(name)){int n=-1;if(number(value,n,0,houses-1))return n;}return -1;};
    for(const auto& [key,value]:houseNames->kv){int n=-1;if(!number(value,n,0,houses-1))return false;}
    for(const auto& [key,value]:houseNumbers->kv){int n=0,h=0;if(!number(key,n,0,1023) || !number(value,h,0,houses-1))return false;d->houseNumbers.emplace_back(n,h);}
    if(const auto* buildings=source.find("NativeEvent.buildingTypes"))for(const auto& [key,value]:buildings->kv){int n=0;if(!number(key,n,0,4095) || value.empty() || value.size()>64)return false;d->buildingTypes.emplace_back(n,value);}
    if(d->extendedConditions) {
        const char* tables[]={"NativeEvent.vehicleTypes","NativeEvent.infantryTypes","NativeEvent.aircraftTypes"};
        for(int category=0;category<3;++category) {
            const auto* table=source.find(tables[category]);if(!table || table->kv.size()>4096)return false;
            std::set<int> seen;
            for(const auto& [key,value]:table->kv){int n=0;if(!number(key,n,0,4095) || value.empty() || value.size()>64 || !seen.insert(n).second)return false;
                d->mobileTypes[category].emplace_back(n,value);}
        }
        const auto* variables=source.find("NativeEvent.variableNames");if(!variables || variables->kv.size()>50)return false;
        std::set<int> seen;
        for(const auto& [key,value]:variables->kv){int index=0,initial=0;const auto comma=value.rfind(',');
            if(!number(key,index,0,49) || !seen.insert(index).second || comma==std::string::npos || comma>256
                || !number(value.substr(comma+1),initial,0,1))return false;d->initialLocals[index]=(uint8_t)initial;}
    }
    const auto* triggers=source.find("NativeEvent.triggers");const auto* events=source.find("NativeEvent.events");
    const auto* actions=source.find("NativeEvent.actions");const auto* tags=source.find("NativeEvent.tags");
    const auto* cells=source.find("NativeEvent.celltags");const auto* waypoints=source.find("NativeEvent.waypoints");
    if(!triggers || !events || !actions || !tags || !cells || !waypoints || triggers->kv.size()!=expectedTriggers
        || tags->kv.size()!=expectedTags || events->kv.size()!=triggers->kv.size() || actions->kv.size()!=triggers->kv.size()
        || cells->kv.size()>65536 || waypoints->kv.size()>4096)return false;
    std::vector<std::string> links;
    for(const auto& [id,text]:triggers->kv) {
        if(id.empty() || id.size()>64 || index(d->triggers,id)>=0)return false;
        const auto row=split(text);if(row.size()!=8 || row[2].size()>256)return false;
        NativeTriggerDefinition trigger;trigger.id=lower(id);trigger.name=row[2];trigger.house=house(row[0]);
        if(trigger.house<0)return false;int disabled=0;
        if(!number(row[3],disabled,0,1))return false;trigger.disabled=disabled!=0;
        for(int i=0;i<3;++i){int enabled=0;if(!number(row[4+i],enabled,0,1))return false;trigger.difficulty[i]=enabled!=0;}
        int reserved=0;if(!number(row[7],reserved,0,1))return false;
        const char* eventText=events->get(id.c_str());const char* actionText=actions->get(id.c_str());if(!eventText || !actionText)return false;
        const auto erow=split(eventText),arow=split(actionText);int ec=0,ac=0;
        if(!number(erow[0],ec,0,64) || erow.size()!=1+(size_t)ec*3 || !number(arow[0],ac,0,64) || arow.size()!=1+(size_t)ac*8)return false;
        for(int i=0;i<ec;++i){NativeEventCondition e;if(!number(erow[1+i*3],e.type,0,255) || !number(erow[2+i*3],e.kind,0,255)
            || !number(erow[3+i*3],e.value,-1,1000000))return false;trigger.events.push_back(e);}
        for(int i=0;i<ac;++i){NativeEventAction a;if(!number(arow[1+i*8],a.type,0,255))return false;
            for(int j=0;j<7;++j){a.args[j]=arow[2+i*8+j];if(a.args[j].size()>256)return false;}trigger.actions.push_back(std::move(a));}
        links.push_back(lower(row[1]));d->triggers.push_back(std::move(trigger));
    }
    for(int i=0;i<(int)links.size();++i){if(links[i]!="<none>"){d->triggers[i].linked=index(d->triggers,links[i]);if(d->triggers[i].linked<0)return false;}
        std::set<int> seen;for(int n=i;n>=0;n=d->triggers[n].linked)if(!seen.insert(n).second)return false;}
    for(const auto& [id,text]:tags->kv) {
        const auto row=split(text);NativeTagDefinition tag;tag.id=lower(id);
        if(id.empty() || id.size()>64 || index(d->tags,id)>=0 || row.size()!=3 || row[1].size()>256
            || !number(row[0],tag.persistence,0,2))return false;
        tag.name=row[1];tag.trigger=index(d->triggers,row[2]);if(tag.trigger<0)return false;d->tags.push_back(std::move(tag));
    }
    for(const auto& [key,value]:cells->kv){Vec2i point;if(!coordinate(key,width,height,point))return false;const int tag=index(d->tags,value);if(tag<0)return false;d->cells.emplace_back(point,tag);}
    for(const auto& [key,value]:waypoints->kv){int id=0;Vec2i point;if(!number(key,id,0,4095) || !coordinate(value,width,height,point))return false;d->waypoints.emplace_back(id,point);}
    std::vector<std::string> ids;
    if(!records(source,"NativeEvent.scripttypes",ids))return false;
    for(const auto& id:ids){NativeScriptDefinition script;script.id=id;const auto* row=source.find(("NativeEvent.record."+id).c_str());if(!row)return false;
        for(int step=0;step<50;++step){const char* text=row->get(std::to_string(step).c_str());if(!text)continue;const auto fields=split(text);Vec2i action;
            if(fields.size()!=2 || !number(fields[0],action.x,0,255) || !number(fields[1],action.y,-1,100000000))return false;script.steps.push_back(action);}
        d->scripts.push_back(std::move(script));}
    ids.clear();if(!records(source,"NativeEvent.taskforces",ids))return false;
    for(const auto& id:ids){NativeTaskForceDefinition force;force.id=id;const auto* row=source.find(("NativeEvent.record."+id).c_str());if(!row)return false;
        if(!number(get(row,"group","-1"),force.group,-2,1000000))return false;
        for(int item=0;item<64;++item){const char* text=row->get(std::to_string(item).c_str());if(!text)break;const auto fields=split(text);int count=0;
            if(fields.size()!=2 || !number(fields[0],count,1,256) || fields[1].empty() || fields[1].size()>64)return false;force.units.emplace_back(count,fields[1]);}
        d->taskForces.push_back(std::move(force));}
    ids.clear();if(!records(source,"NativeEvent.teamtypes",ids))return false;
    for(const auto& id:ids){NativeTeamDefinition team;team.id=id;const auto* row=source.find(("NativeEvent.record."+id).c_str());if(!row)return false;
        team.name=get(row,"name");team.house=house(get(row,"house"));team.script=index(d->scripts,get(row,"script"));team.taskForce=index(d->taskForces,get(row,"taskforce"));
        if((team.house<0 && !(d->aiOnly && lower(get(row,"house"))=="<none>")) || team.script<0 || team.taskForce<0 || team.name.size()>256)return false;
        const auto tag=get(row,"tag");if(!tag.empty() && lower(tag)!="<none>"){team.tag=index(d->tags,tag);if(team.tag<0)return false;}
        const auto wp=get(row,"waypoint");if(!wp.empty()){
            team.waypoint=0;for(char c:wp){if(c<'A'||c>'Z')return false;team.waypoint=team.waypoint*26+c-'A'+1;}--team.waypoint;
        }
        if(!number(get(row,"max","0"),team.max,-1,1024) || !number(get(row,"veteranlevel","1"),team.veterancy,1,3))return false;
        auto yes=[&](const char* key){const auto v=lower(get(row,key));return v=="yes" || v=="true" || v=="1";};
        team.loadable=yes("loadable");team.recruitable=yes("areteammembersrecruitable");team.reinforce=yes("reinforce");team.prebuild=yes("prebuild");
        team.aggressive=yes("aggressive");
        team.onlyTargetEnemy=yes("onlytargethouseenemy");
        team.autocreate=yes("autocreate");team.recruiter=yes("recruiter");team.baseDefense=yes("isbasedefense");
        team.transportsReturn=yes("transportsreturnonunload");
        team.guardSlower=d->hasOriginalTeamControl() && yes("guardslower");
        if(!number(get(row,"priority","7"),team.priority,0,1000000) || !number(get(row,"group","-1"),team.group,-2,1000000))return false;
        d->teams.push_back(std::move(team));
    }
    destination=std::move(d);return true;
}
