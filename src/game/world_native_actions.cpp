#include "game/world.h"
#include <algorithm>
#include <charconv>

namespace {
bool integer(const std::string& text,int& value) {
    const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
    return result.ec==std::errc{} && result.ptr==text.data()+text.size();
}
int waypoint(const std::string& text) {
    int result=0;if(text.empty() || text.size()>4)return -1;
    for(char c:text){if(c<'A'||c>'Z')return -1;result=result*26+c-'A'+1;}return result-1;
}
}

int World::nativeActionBlocker(const NativeEventAction& action,int tag) const {
    int value=0;
    switch(action.type) {
    case 0:case 46:case 47:return -1;
    case 3:case 74:case 75:case 76:
        return nativeAI.active && integer(action.args[1],value)
            && (action.type==76?(value>=0 && value<=100):nativeHouseNumber(value)>=0)?-1:action.type;
    case 1:case 2:
        // Current source missions target their scenario player. Propagation of
        // a foreign house's victory/defeat needs separate original-game rules.
        return nativeEventDefinition && integer(action.args[1],value) && nativeHouseNumber(value)==nativeEventDefinition->playerHouse?-1:action.type;
    case 12:case 53:case 54:return nativeTriggerIndex(action.args[1])>=0?-1:action.type;
    case 14:case 36: {
        if(!integer(action.args[1],value) || nativeHouseNumber(value)<0)return action.type;
        // Ownership inside an independently owned transport is not represented
        // by the legacy cargo model. Refuse this case before any action commits.
        for(const auto& e:ents) {
            if(!e.alive)continue;
            const auto tagged=[&](auto&& self,const Ent::GarrisonedUnit& p)->bool {
                if(action.type==14 && (p.nativeEventTag==tag || e.nativeEventTag==tag) && p.nativeEventTag!=e.nativeEventTag)return true;
                for(const auto& child:p.cargo)if(self(self,child))return true;return false;
            };
            for(const auto& p:e.cargo)if(tagged(tagged,p))return action.type;
            for(const auto& p:e.garrison)if(tagged(tagged,p))return action.type;
        }
        return -1;
    }
    case 17:return integer(action.args[1],value) && nativeWaypoint(value).x>=0?-1:action.type;
    case 56:case 57:return nativeConditions.active && integer(action.args[1],value) && value>=0 && value<50?-1:action.type;
    case 63:return nativeEventDefinition && nativeEventDefinition->demolition.available && integer(action.args[1],value)
        && nativeWaypoint(value).x>=0?-1:action.type;
    case 101:return nativeCrateRules.available && integer(action.args[1],value) && value>=0 && value<=512
        && nativeWaypoint(waypoint(action.args[6])).x>=0?-1:action.type;
    case 108:return nativeCrateRules.available && integer(action.args[1],value) && value>=0 && value<19
        && nativeCrateType(NATIVE_CRATE_BASE+value) && nativeWaypoint(waypoint(action.args[6])).x>=0?-1:action.type;
    case 4:case 5:case 7:case 80: {
        const int team=nativeTeamIndex(action.args[1]);if(team<0)return action.type;
        if(action.type==5)return -1;
        if(nativeTeamBlocker(team)!=-1)return action.type;
        if(action.type==7 || action.type==80) {
            const int wp=action.type==80?waypoint(action.args[6]):nativeEventDefinition->teams[team].waypoint;
            const auto point=nativeWaypoint(wp);if(!map.inBounds(point.x,point.y))return action.type;
        }
        return -1;
    }
    // These are exact source requests, not claims that a movie/speech has
    // played. The app consumes supported effects and retains unresolved cues.
    case 11:case 19:case 21:case 41:case 48:case 55:case 99:case 100:case 104:case 113:case 114:case 115:case 116:
        return -1;
    default:return action.type;
    }
}

void World::changeNativeHouse(int tag,int owner,bool all,int from) {
    if(owner<0 || owner>=numPlayers)return;
    for(EID id=0;id<(EID)ents.size();++id){auto& e=ents[id];if(!e.alive || !(all?e.player==from:e.nativeEventTag==tag))continue;
        if(e.player!=owner && hasAttackMoveState(e))orderStop({id});
        changeActorOwner(id,owner);
        if(e.isBuilding)e.repairing=false;
    }
    synchronizeBuildingOwners();
    std::erase_if(nativeAI.jobs,[&](const auto& job){return ents[job.actor].player!=job.house;});
}

int World::prepareNativeActions(const std::vector<NativeEventAction>& actions,int tag,std::vector<std::vector<std::pair<int,Vec2i>>>& placements) const {
    placements.clear();placements.resize(actions.size());std::vector<uint8_t> reserved;
    size_t futureTags=nativeEvents.tags.size(),futureTriggers=nativeEvents.triggers.size(),futureTeams=nativeEvents.teams.size();
    for(int i=0;i<(int)actions.size();++i) {
        const auto& action=actions[i];const int block=nativeActionBlocker(action,tag);if(block!=-1)return block;
        if(action.type!=4 && action.type!=5 && action.type!=7 && action.type!=80)continue;
        const int team=nativeTeamIndex(action.args[1]);const auto& definition=nativeEventDefinition->teams[team];
        if(action.type==5)continue;
        // Max limits automatic AI production, not explicit scenario actions.
        if(++futureTeams>4096)return action.type;
        if(action.type!=4) {
            if(reserved.empty())reserved.assign(movementSlotCount(),0);
            if(!nativeTeamPlacements(team,action.type==80?waypoint(action.args[6]):-1,placements[i],&reserved))return action.type;
            for(const auto& [type,point]:placements[i]){const auto& unit=nativeTypes[type]->unit;
                reserved[movementSlot(point.x,point.y,map.preferredLayer(point.x,point.y,unit.pathDomain()))]=1;}
        }
        if(definition.tag>=0) {
            if(++futureTags>8192)return action.type;
            for(int t=nativeEventDefinition->tags[definition.tag].trigger;t>=0;t=nativeEventDefinition->triggers[t].linked)if(++futureTriggers>16384)return action.type;
        }
    }
    return -1;
}

bool World::executeNativeAction(const NativeEventAction& action,int runtime,const std::vector<std::pair<int,Vec2i>>* placements) {
    const int tag=nativeEvents.triggers[runtime].tag;
    const auto& trigger=nativeEventDefinition->triggers[nativeEvents.triggers[runtime].definition];int value=0;
    switch(action.type) {
    case 0:return true;
    case 3:case 74:case 75:case 76:
        if(!nativeAI.active || !integer(action.args[1],value))return false;
        if(action.type==76){nativeAI.houses[trigger.house].ratio=value;return true;}
        {const int house=nativeHouseNumber(value);if(house<0)return false;
        if(action.type==3)nativeAI.houses[house].production=true;
        else nativeAI.houses[house].enabled=action.type==74;return true;}
    case 1:case 2:
        return integer(action.args[1],value) && nativeHouseNumber(value)==nativeEventDefinition->playerHouse
            && concludeNativeMission(action.type==1,runtime);
    case 12:case 53:case 54: {
        const int definition=nativeTriggerIndex(action.args[1]);if(definition<0)return false;
        for(auto& t:nativeEvents.triggers)if(t.definition==definition && !t.destroyed) {
            if(action.type==12){t.enabled=false;t.destroyed=true;}
            else if(action.type==54)t.enabled=false;
            else if(nativeEventDefinition->triggers[definition].difficulty[nativeEvents.difficulty]) {
                t.enabled=true;t.timerStart=tick;
                const auto& events=nativeEventDefinition->triggers[definition].events;
                for(int i=0;i<(int)events.size();++i)if(events[i].type==13)t.eventMask&=~(uint64_t{1}<<i);
            }
        }
        return true;
    }
    case 14:case 36:
        if(!integer(action.args[1],value) || nativeHouseNumber(value)<0)return false;
        changeNativeHouse(tag,nativeHouseNumber(value),action.type==36,trigger.house);return true;
    case 17: {
        if(!integer(action.args[1],value))return false;const auto point=nativeWaypoint(value);
        map.reveal(nativeEventDefinition->playerHouse,point.x,point.y,nativeEventDefinition->revealRadius);return true;
    }
    case 63:return integer(action.args[1],value) && nativeDemolitionAt(value);
    case 56:case 57:if(!integer(action.args[1],value) || value<0 || value>=50)return false;setNativeLocal(value,action.type==56);return true;
    case 101:return integer(action.args[1],value) && reshroudNativeWaypoint(value,nativeWaypoint(waypoint(action.args[6])));
    case 108:return integer(action.args[1],value) && createNativeCrate(value,nativeWaypoint(waypoint(action.args[6])));
    case 4:case 7:case 80:
        return createNativeTeam(nativeTeamIndex(action.args[1]),action.type!=4,action.type==80?waypoint(action.args[6]):-1,placements)>=0;
    case 5: {
        const int team=nativeTeamIndex(action.args[1]);
        for(int i=0;i<(int)nativeEvents.teams.size();++i)if(nativeEvents.teams[i].definition==team)finishNativeTeam(i);
        return true;
    }
    case 46:nativeEvents.inputLocked=true;break;
    case 47:nativeEvents.inputLocked=false;break;
    default:break;
    }
    NativeEventCue cue;cue.action=action.type;cue.house=nativeEventDefinition->playerHouse;cue.tick=tick;cue.key=action.args[1];cue.args=action.args;
    if(integer(action.args[1],value))cue.value=value;
    if(action.type==48 || action.type==55 || action.type==99 || action.type==41) {
        const auto point=nativeWaypoint(waypoint(action.args[6]));cue.x=point.x;cue.y=point.y;
    }
    // Presentation has the same bounded lifetime as other World event queues.
    if(nativeEventCues.size()<4096)nativeEventCues.push_back(std::move(cue));
    return true;
}
