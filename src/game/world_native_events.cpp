#include "game/world.h"
#include <algorithm>
#include <cctype>

namespace {
bool same(const std::string& a,const std::string& b){return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](char l,char r){return std::tolower((unsigned char)l)==std::tolower((unsigned char)r);});}
}

void World::resetNativeEvents() {
    nativeConditions={};
    nativeAI={};nativeAIRules={};nativeCrateRules={};nativeCrateBonuses.clear();
    nativeEventDefinition.reset();nativeEvents={};nativeEventCues.clear();nativeEventDispatchTags.clear();nativeEventOperations=0;
    const auto clear=[&](auto&& self,Ent::GarrisonedUnit& p)->void{p.nativeEventTag=p.nativeLastEnteredCell=-1;for(auto& child:p.cargo)self(self,child);};
    for(auto& actor:ents){actor.nativeEventTag=actor.nativeLastEnteredCell=-1;for(auto& p:actor.cargo)clear(clear,p);for(auto& p:actor.garrison)clear(clear,p);}
}

int World::nativeHouseNumber(int number) const {
    if(nativeEventDefinition)for(auto [original,house]:nativeEventDefinition->houseNumbers)if(original==number)return house;
    return -1;
}
int World::nativeTriggerIndex(const std::string& id) const {
    if(nativeEventDefinition)for(int i=0;i<(int)nativeEventDefinition->triggers.size();++i)if(same(nativeEventDefinition->triggers[i].id,id))return i;
    return -1;
}
int World::nativeTeamIndex(const std::string& id) const {
    if(nativeEventDefinition)for(int i=0;i<(int)nativeEventDefinition->teams.size();++i)if(same(nativeEventDefinition->teams[i].id,id))return i;
    return -1;
}
int World::nativeUnitIndex(const std::string& id) const {
    for(int i=0;i<(int)nativeTypes.size();++i)if(!nativeTypes[i]->isBuilding && same(nativeTypes[i]->id,id))return i;
    return -1;
}
Vec2i World::nativeWaypoint(int id) const {
    if(nativeEventDefinition)for(auto [number,point]:nativeEventDefinition->waypoints)if(number==id)return point;
    return {-1,-1};
}

int World::nativeEventTagInstance(int definition,bool separate) {
    if(!nativeEventDefinition || definition<0 || definition>=(int)nativeEventDefinition->tags.size())return -1;
    if(!separate)for(int i=0;i<(int)nativeEvents.tags.size();++i)if(nativeEvents.tags[i].alive && nativeEvents.tags[i].definition==definition)return i;
    size_t count=0;for(int t=nativeEventDefinition->tags[definition].trigger;t>=0;t=nativeEventDefinition->triggers[t].linked)++count;
    if(nativeEvents.tags.size()>=8192 || nativeEvents.triggers.size()+count>16384)return -1;
    const int id=(int)nativeEvents.tags.size();nativeEvents.tags.push_back({definition,true});
    for(int index=nativeEventDefinition->tags[definition].trigger;index>=0;index=nativeEventDefinition->triggers[index].linked) {
        const auto& d=nativeEventDefinition->triggers[index];NativeTriggerState state;
        state.definition=index;state.tag=id;state.enabled=!d.disabled && d.difficulty[nativeEvents.difficulty];state.timerStart=tick;
        nativeEvents.triggers.push_back(state);
    }
    return id;
}

bool World::startNativeEvents(int difficulty) {
    if(!nativeEventDefinition || nativeEvents.active || difficulty<0 || difficulty>2 || updateInProgress || entityMutationDepth>0 || !nativeEventDispatchTags.empty())return false;
    size_t count=0;for(const auto& tag:nativeEventDefinition->tags)for(int t=tag.trigger;t>=0;t=nativeEventDefinition->triggers[t].linked)if(++count>16384)return false;
    nativeEvents={};nativeEvents.active=true;nativeEvents.difficulty=difficulty;nativeEventCues.clear();nativeEventOperations=0;
    for(auto& actor:ents)actor.nativeEventTag=-1;
    for(int i=0;i<(int)nativeEventDefinition->tags.size();++i)if(nativeEventTagInstance(i)!=i)return false;
    const auto bind=[&](auto& actor,int cell){actor.nativeLastEnteredCell=cell;if(!actor.nativeIdentity.tag.empty())
        for(int tag=0;tag<(int)nativeEventDefinition->tags.size();++tag)if(same(actor.nativeIdentity.tag,nativeEventDefinition->tags[tag].id)){actor.nativeEventTag=tag;break;}};
    const auto cargo=[&](auto&& self,Ent::GarrisonedUnit& p,int cell)->void{bind(p,cell);for(auto& child:p.cargo)self(self,child,cell);};
    for(auto& actor:ents)if(actor.alive){const int cell=!actor.isBuilding && map.inBounds((int)actor.x,(int)actor.y)?cellIdx((int)actor.x,(int)actor.y):-1;
        bind(actor,cell);for(auto& p:actor.cargo)cargo(cargo,p,cell);for(auto& p:actor.garrison)cargo(cargo,p,cell);}
    startNativeAI();startNativeConditions();return true;
}

bool World::nativeEventCondition(const NativeEventCondition& event,int occurrence,EID actor,int runtime) const {
    if(event.kind!=0)return false; // variable/expression operands need their own source evaluator
    const auto& state=nativeEvents.triggers[runtime];const auto& trigger=nativeEventDefinition->triggers[state.definition];
    switch(event.type) {
    case 1:return occurrence==1 && valid(actor) && ents[actor].player==nativeHouseNumber(event.value);
    case 13:return event.value>=0 && tick>=state.timerStart && (tick-state.timerStart)/(uint64_t)LOGIC_FPS>=(uint64_t)event.value;
    case 48:return occurrence==48;
    case 9:case 10:case 11:return nativeHouseEliminated(event.value,event.type);
    case 32:case 57: {
        std::string id;for(const auto& [number,name]:nativeEventDefinition->buildingTypes)if(number==event.value){id=name;break;}
        if(id.empty())return false;bool exists=false;
        for(const auto& e:ents)if(e.alive && e.isBuilding && e.player==trigger.house) {
            if(e.nativeIdentity.definition){if(same(e.nativeIdentity.definition->id,id)){exists=true;break;}}
            else for(const auto& native:nativeTypes)if(native->isBuilding && same(native->id,id)
                && native->building.type==e.btype && e.btype!=BldType::CivHouse && e.btype!=BldType::Wall) {
                const bool faction=(id[0]!='G' && id[0]!='N') || ((id[0]=='G')==(players[e.player].faction==Faction::Allies));
                if(faction){exists=true;break;}
            }
            if(exists)break;
        }
        return event.type==32?exists:!exists;
    }
    default:return nativeExtendedCondition(event,occurrence,actor,runtime);
    }
}

void World::offerNativeTagEvent(int tag,int occurrence,EID actor) {
    if(!nativeEvents.active || !nativeEventDefinition || tag<0 || tag>=(int)nativeEvents.tags.size()
        || !nativeEvents.tags[tag].alive || std::find(nativeEventDispatchTags.begin(),nativeEventDispatchTags.end(),tag)!=nativeEventDispatchTags.end())return;
    const int persistence=nativeEventDefinition->tags[nativeEvents.tags[tag].definition].persistence;
    if(nativeEventDispatchTags.empty() && !updateInProgress)nativeEventOperations=0;
    nativeEventDispatchTags.push_back(tag);bool fired=false;
    const int end=(int)nativeEvents.triggers.size();
    for(int runtime=0;runtime<end;++runtime) {
        if(nativeEvents.triggers[runtime].tag!=tag || !nativeEvents.triggers[runtime].enabled || nativeEvents.triggers[runtime].destroyed)continue;
        const int definition=nativeEvents.triggers[runtime].definition;const auto& d=nativeEventDefinition->triggers[definition];
        bool satisfied=true,remember=persistence==2;uint64_t mask=nativeEvents.triggers[runtime].eventMask;
        for(int i=(int)d.events.size()-1;i>=0;--i) {
            const auto& event=d.events[i];const bool old=(mask&(uint64_t{1}<<i))!=0;
            const bool extended=nativeEventDefinition->extendedConditions;
            const bool temporal=extended?(event.type==4 || event.type==33 || event.type==48 || (event.type>=19 && event.type<=22))
                :(event.type==13 || event.type==48);
            const bool match=(old && temporal) || nativeEventCondition(event,occurrence,actor,runtime);
            if(match && (event.type==1 || event.type==32 || (extended && event.type>=19 && event.type<=22)))remember=true;
            if(match && remember && temporal && (!extended || event.type!=6))mask|=uint64_t{1}<<i;
            if(!match)satisfied=false;
        }
        nativeEvents.triggers[runtime].eventMask=mask;
        if(!satisfied)continue;
        std::vector<std::vector<std::pair<int,Vec2i>>> placements;
        const bool ordered=nativeEventDefinition->orderedActions;
        int blocked=persistence==1?-3:ordered?-1:prepareNativeActions(d.actions,tag,placements);
        if(nativeEventOperations+(int)d.actions.size()>16384)blocked=-2;
        nativeEvents.triggers[runtime].blockedAction=blocked;if(blocked!=-1)continue;
        bool success=true;
        for(int i=0;i<(int)d.actions.size();++i){const auto& action=d.actions[i];
            if(nativeEventOperations>=16384){nativeEvents.triggers[runtime].blockedAction=-2;break;}++nativeEventOperations;
            if(ordered){std::vector<std::vector<std::pair<int,Vec2i>>> one;
                const int failure=prepareNativeActions({action},tag,one);
                if(failure!=-1 || !executeNativeAction(action,runtime,one.empty()?nullptr:&one[0])){
                    if(nativeEvents.triggers[runtime].blockedAction==-1)nativeEvents.triggers[runtime].blockedAction=failure==-1?action.type:failure;
                }
            } else if(!executeNativeAction(action,runtime,&placements[i])){success=false;nativeEvents.triggers[runtime].blockedAction=action.type;break;}}
        if(!success)continue;
        ++nativeEvents.triggers[runtime].firings;fired=true;
        if(remember) {
            nativeEvents.triggers[runtime].timerStart=tick;
            for(int i=0;i<(int)d.events.size();++i)if(d.events[i].type==13)nativeEvents.triggers[runtime].eventMask&=~(uint64_t{1}<<i);
        }
    }
    if(fired && persistence==0) {
        nativeEvents.tags[tag].alive=false;
        const auto clear=[&](auto&& self,Ent::GarrisonedUnit& p)->void{if(p.nativeEventTag==tag)p.nativeEventTag=-1;for(auto& child:p.cargo)self(self,child);};
        for(auto& e:ents){if(e.nativeEventTag==tag)e.nativeEventTag=-1;for(auto& p:e.cargo)clear(clear,p);for(auto& p:e.garrison)clear(clear,p);}
    }
    nativeEventDispatchTags.pop_back();
}

void World::offerNativeCellEntry(EID actor,int x,int y) {
    if(!nativeEvents.active || !valid(actor) || ents[actor].airborne() || ents[actor].parasiting || ents[actor].magneticHeight>0)return;
    const int cell=cellIdx(x,y);if(ents[actor].nativeLastEnteredCell==cell)return;ents[actor].nativeLastEnteredCell=cell;
    for(const auto& [point,tag]:nativeEventDefinition->cells)if(point==Vec2i{x,y}){offerNativeTagEvent(tag,1,actor);break;}
}

void World::retireNativeEventActor(EID actor,bool destroyed) {
    std::erase_if(nativeAI.jobs,[&](const auto& job){return job.actor==actor;});
    if(!nativeEvents.active || actor<0 || actor>=(int)ents.size())return;
    const int tag=ents[actor].nativeEventTag;if(destroyed)offerNativeTagEvent(tag,48,actor);
    if(destroyed) {
        nativeConditions.discoveredHumans.erase(ents[actor].nativeIdentity.object);
        const auto clear=[&](auto&& self,const Ent::GarrisonedUnit& p)->void{nativeConditions.discoveredHumans.erase(p.nativeIdentity.object);for(const auto& c:p.cargo)self(self,c);};
        for(const auto& p:ents[actor].cargo)clear(clear,p);for(const auto& p:ents[actor].garrison)clear(clear,p);
    }
    ents[actor].nativeEventTag=-1;
    for(auto& team:nativeEvents.teams){std::erase(team.members,actor);if(team.focus==actor)team.focus=-1;}
}

void World::updateNativeEvents() {
    nativeEventOperations=0;if(!nativeEvents.active || !nativeEventDefinition)return;
    const int count=(int)nativeEvents.tags.size();
    for(int tag=0;tag<count;++tag) {
        if(!nativeEvents.tags[tag].alive)continue;bool general=false;
        for(int index=nativeEventDefinition->tags[nativeEvents.tags[tag].definition].trigger;index>=0;index=nativeEventDefinition->triggers[index].linked)
            for(const auto& event:nativeEventDefinition->triggers[index].events)
                if(event.type==13 || event.type==32 || event.type==57 || event.type==9 || event.type==10 || event.type==11
                    || (nativeConditions.active && ((event.type>=19 && event.type<=22) || event.type==36 || event.type==37)))general=true;
        if(general)offerNativeTagEvent(tag,13);
    }
    updateNativeAIScheduler();
    updateNativeTeams();
    updateNativeAIProduction();
}
