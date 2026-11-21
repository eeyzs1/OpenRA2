#include "game/world.h"
#include "core/binary_state_io.h"
#include "game/native_nearby_clearance.h"
#include <algorithm>
#include <set>

namespace {
bool transfer(BinaryStateIO& io,NativeEventState& state,bool teamExecution=true,bool teamOrders=true,bool teamAttack=true,bool missionOutcome=true,bool teamControl=true) {
    io.flag(state.active);if(!io.ok || !state.active)return io.ok;
    io.value(state.difficulty);io.flag(state.inputLocked);
    if(!io.vector(state.tags,8192))return false;
    for(auto& tag:state.tags){io.value(tag.definition);io.flag(tag.alive);}
    if(!io.vector(state.triggers,16384))return false;
    for(auto& trigger:state.triggers) {
        io.value(trigger.definition);io.value(trigger.tag);io.flag(trigger.enabled);io.flag(trigger.destroyed);
        io.value(trigger.timerStart);io.value(trigger.eventMask);io.value(trigger.firings);io.value(trigger.blockedAction);
    }
    if(!io.vector(state.teams,4096))return false;
    for(auto& team:state.teams) {
        io.value(team.definition);io.value(team.tag);io.value(team.step);io.flag(team.alive);io.flag(team.reinforced);io.value(team.waitUntil);
        if(teamOrders){io.value(team.memberHouse);io.value(team.focusLayer);}
        if(teamAttack)io.flag(team.advancePending);
        if(teamControl)io.flag(team.succeeded);
        if(teamExecution) {
            io.flag(team.assembled);io.flag(team.lineStarted);io.flag(team.focusFixed);
            io.value(team.focus);io.value(team.focusCell);io.value(team.blockedScript);
        } else team.assembled=team.step>0;
        if(!io.vector(team.members,16384))return false;for(auto& member:team.members)io.value(member);
    }
    if(missionOutcome) {uint8_t outcome=(uint8_t)state.outcome;io.value(outcome);state.outcome=(NativeMissionOutcome)outcome;
        io.value(state.outcomeAt);io.value(state.outcomeTrigger);}
    return io.ok;
}
}

bool World::validateNativeEventState() const {
    if(!nativeEventDispatchTags.empty() || !validateNativeMissionOutcome())return false;
    if(!nativeEventDefinition || !nativeEvents.active) {
        if(nativeEvents.active || !nativeEvents.tags.empty() || !nativeEvents.triggers.empty() || !nativeEvents.teams.empty() || nativeEvents.inputLocked)return false;
        const auto empty=[&](auto&& self,const Ent::GarrisonedUnit& p)->bool{if(p.nativeEventTag!=-1 || p.nativeLastEnteredCell!=-1)return false;for(const auto& child:p.cargo)if(!self(self,child))return false;return true;};
        for(const auto& e:ents){if(e.nativeEventTag!=-1 || e.nativeLastEnteredCell!=-1)return false;for(const auto& p:e.cargo)if(!empty(empty,p))return false;for(const auto& p:e.garrison)if(!empty(empty,p))return false;}
        return true;
    }
    const auto& d=*nativeEventDefinition;
    if(nativeEvents.difficulty<0 || nativeEvents.difficulty>2 || nativeEvents.tags.size()<d.tags.size()
        || nativeEvents.tags.size()>8192 || nativeEvents.triggers.size()>16384 || nativeEvents.teams.size()>4096)return false;
    size_t runtime=0;
    for(int tag=0;tag<(int)nativeEvents.tags.size();++tag) {
        const auto& t=nativeEvents.tags[tag];if(t.definition<0 || t.definition>=(int)d.tags.size()
            || (tag<(int)d.tags.size() && t.definition!=tag))return false;
        for(int definition=d.tags[t.definition].trigger;definition>=0;definition=d.triggers[definition].linked) {
            if(runtime>=nativeEvents.triggers.size())return false;const auto& state=nativeEvents.triggers[runtime++];const auto& source=d.triggers[definition];
            if(state.definition!=definition || state.tag!=tag || state.timerStart>tick || (source.events.size()<64 && (state.eventMask>>source.events.size()))
                || (state.enabled && (state.destroyed || !source.difficulty[nativeEvents.difficulty])) || state.blockedAction < -3 || state.blockedAction>255)return false;
        }
    }
    if(runtime!=nativeEvents.triggers.size())return false;
    const auto binding=[&](int tag,int cell){return tag>=-1 && (tag<0 || (tag<(int)nativeEvents.tags.size() && nativeEvents.tags[tag].alive))
        && cell>=-1 && (cell<0 || (cell<(int)map.cells.size() && map.inBounds(cell%map.w,cell/map.w)));};
    const auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->bool{if(!binding(p.nativeEventTag,p.nativeLastEnteredCell))return false;
        for(const auto& child:p.cargo)if(!self(self,child))return false;return true;};
    for(const auto& e:ents) {
        if(!binding(e.nativeEventTag,e.nativeLastEnteredCell) || (!e.alive && e.nativeEventTag!=-1))return false;
        for(const auto& p:e.cargo)if(!passenger(passenger,p))return false;for(const auto& p:e.garrison)if(!passenger(passenger,p))return false;
    }
    std::set<EID> members;
    for(const auto& team:nativeEvents.teams) {
        if(team.definition<0 || team.definition>=(int)d.teams.size() || team.tag < -1 || team.tag>=(int)nativeEvents.tags.size()
            || team.step<0 || team.step>(int)d.scripts[d.teams[team.definition].script].steps.size() || team.members.size()>16384)return false;
        const auto& definition=d.teams[team.definition];const auto& script=d.scripts[definition.script];
        if(team.succeeded && (!team.assembled || std::none_of(script.steps.begin(),script.steps.end(),[](const auto& step){return step.x==49;})))return false;
        const Vec2i line=team.step<(int)script.steps.size()?script.steps[team.step]:Vec2i{-1,0};
        const bool staging=d.aiOnly && nativeAIRules.isGlobal() && (line.x==53 || line.x==54);
        const bool sourceCell=staging && nativeAIRules.hasOriginalNearbyGeometry();
        const auto packed=sourceCell?nativeNearbyUnpack(team.focusCell):Vec2i{};
        const bool validSourceCell=packed.x>=0 && packed.y>=0 && packed.x<map.w+32 && packed.y<map.h+32;
        if(team.lineStarted && ((line.x!=0 && line.x!=1 && line.x!=3 && line.x!=5 && line.x!=20 && !staging && !(d.extendedConditions && line.x==8))
            || (line.x==5 && (line.y<0 || line.y>86400)) || (line.x==20 && (line.y<0 || line.y>1023))))return false;
        const uint64_t maximumWait=team.lineStarted && line.x==5 && line.y>=0 && line.y<=86400?(uint64_t)line.y*LOGIC_FPS:LOGIC_FPS;
        if((team.tag<0)!=(definition.tag<0) || (team.tag>=0 && nativeEvents.tags[team.tag].definition!=definition.tag)
            || team.focus < -1 || (team.focus>=0 && !valid(team.focus)) || team.focusCell < -1
            || (team.focusCell>=0 && (sourceCell?!validSourceCell:team.focusCell>=(int)map.cells.size()))
            || team.memberHouse<0 || team.memberHouse>=numPlayers || team.focusLayer < -1 || team.focusLayer>1
            || team.blockedScript < -1 || team.blockedScript>255 || (team.waitUntil>tick && team.waitUntil-tick>maximumWait)
            || (!team.alive && (!team.members.empty() || team.lineStarted || team.focus!=-1 || team.focusCell!=-1 || team.focusLayer!=-1))
            || (!team.assembled && (team.lineStarted || team.step>0))
            || (team.lineStarted && team.step>=(int)script.steps.size()))return false;
        if(team.advancePending && (!team.alive || !team.lineStarted || line.x!=0 || team.focus!=-1 || team.waitUntil!=0))return false;
        if(team.lineStarted && line.x==0) {
            if(team.focusCell!=-1 || team.focusLayer!=-1 || team.focusFixed)return false;
        } else
        if(team.lineStarted && (line.x==1 || line.x==3 || staging || (line.x==5 && line.y>0))) {
            if((line.x==3 || staging) && team.focusCell==-1) {
                if(team.blockedScript!=line.x || team.focus!=-1 || team.focusLayer!=-1 || team.focusFixed)return false;
            } else {
                if(team.focusCell<0 || (sourceCell?!validSourceCell:!map.inBounds(team.focusCell%map.w,team.focusCell/map.w)))return false;
                if(line.x==1){if(team.focusLayer!=-1)return false;}
                else if(team.focusLayer<0 || team.focusFixed || ((line.x==3 || staging) && team.focus!=-1))return false;
            }
        } else if(team.focus!=-1 || team.focusCell!=-1 || team.focusFixed || team.focusLayer!=-1)return false;
        for(EID actor:team.members)if(!valid(actor) || ents[actor].isBuilding || !members.insert(actor).second)return false;
    }
    return true;
}

bool World::writeNativeEventState(FILE* file) const {
    if(!validateNativeEventState())return false;
    if(!nativeEventDefinition)return true;
    BinaryStateIO io{file};auto state=nativeEvents;if(!transfer(io,state) || !state.active)return io.ok;
    uint32_t count=(uint32_t)ents.size();io.value(count);
    auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->void{
        int tag=p.nativeEventTag,cell=p.nativeLastEnteredCell;io.value(tag);io.value(cell);for(const auto& child:p.cargo)self(self,child);
    };
    for(const auto& e:ents){int tag=e.nativeEventTag,cell=e.nativeLastEnteredCell;io.value(tag);io.value(cell);
        for(const auto& p:e.cargo)passenger(passenger,p);for(const auto& p:e.garrison)passenger(passenger,p);}
    return io.ok;
}

bool World::readNativeEventState(FILE* file,bool teamExecution,bool teamOrders,bool teamAttack,bool missionOutcome,bool teamControl) {
    nativeEvents={};nativeEventCues.clear();nativeEventDispatchTags.clear();nativeEventOperations=0;
    if(!nativeEventDefinition)return validateNativeEventState();
    BinaryStateIO io{file,true};if(!transfer(io,nativeEvents,teamExecution,teamOrders,teamAttack,missionOutcome,teamControl))return false;
    if(!teamOrders)for(auto& team:nativeEvents.teams) {
        if(team.definition<0 || team.definition>=(int)nativeEventDefinition->teams.size())return false;
        team.memberHouse=nativeEventDefinition->teams[team.definition].house;team.focusLayer=-1;
    }
    if(nativeEvents.active) {
        uint32_t count=0;io.value(count);if(!io.ok || count!=ents.size())return false;
        auto passenger=[&](auto&& self,Ent::GarrisonedUnit& p)->void{io.value(p.nativeEventTag);io.value(p.nativeLastEnteredCell);for(auto& child:p.cargo)self(self,child);};
        for(auto& e:ents){io.value(e.nativeEventTag);io.value(e.nativeLastEnteredCell);for(auto& p:e.cargo)passenger(passenger,p);for(auto& p:e.garrison)passenger(passenger,p);}
    }
    return io.ok && validateNativeEventState();
}
