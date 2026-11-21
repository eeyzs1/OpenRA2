#include "game/world.h"

EID World::nativeWaypointObject(int cell) const {
    if(cell<0 || cell>=(int)map.cells.size())return INVALID_EID;
    const int x=cell%map.w,y=cell/map.w;
    // Include passable source buildings as well as occupied foundations.
    for(EID id=0;id<(EID)ents.size();++id)if(valid(id)) {
        const auto& e=ents[id];
        if(e.isBuilding) {
            const auto& b=bldDef(e);
            if(x>=(int)e.x && y>=(int)e.y && x<(int)e.x+b.w && y<(int)e.y+b.h)return id;
        } else if(!e.airborne() && !e.parasiting && e.magneticHeight<=0 && (int)e.x==x && (int)e.y==y)return id;
    }
    return INVALID_EID;
}

void World::finishNativeTeam(int index) {
    if(nativeAIRules.hasOriginalTeamControl())nativeTeamMemberCounts.clear();
    if(nativeAIRules.hasOriginalTeamControl())for(const auto& [key,team]:nativeAI.memberTeams)if(team==index)nativeAI.feet.erase(key);
    std::erase_if(nativeAI.memberTeams,[&](const auto& entry){return entry.second==index;});
    nativeConditions.unloading.erase(index);
    if(nativeEvents.teams[index].alive)nativeAITeamOutcome(index);
    auto& team=nativeEvents.teams[index];team.alive=false;team.members.clear();
    team.lineStarted=team.focusFixed=false;team.focus=team.focusCell=-1;team.waitUntil=0;team.blockedScript=-1;
    team.focusLayer=-1;
    team.advancePending=false;
    // Members remain alive with their current orders and source identities.
}

void World::advanceNativeTeamScript(int index) {
    setNativeTeamScriptStep(index,nativeEvents.teams[index].step+1);
    auto& team=nativeEvents.teams[index];
    if(team.step>=(int)nativeEventDefinition->scripts[nativeEventDefinition->teams[team.definition].script].steps.size())finishNativeTeam(index);
}

void World::updateNativeTeamScript(int index) {
    auto& team=nativeEvents.teams[index];
    const auto& script=nativeEventDefinition->scripts[nativeEventDefinition->teams[team.definition].script];
    // Each supported line waits at least one turn. There is no zero-time loop.
    if(team.step>=(int)script.steps.size()){finishNativeTeam(index);return;}
    const auto line=script.steps[team.step];
    if(nativeAIRules.isGlobal() && nativeEventDefinition->aiOnly && (line.x==53 || line.x==54)) {
        updateNativeTeamStaging(index);return;
    }
    if(nativeConditions.active && (line.x==39 || line.x==40)){updateNativeTeamControl(index);return;}
    if(nativeConditions.active && line.x==8){updateNativeTeamTransport(index);return;}
    if(line.x==6 || line.x==49){updateNativeTeamControl(index);return;}
    if(line.x==0){updateNativeTeamAttack(index);return;}
    if(line.x==3 || line.x==5 || line.x==20){updateNativeTeamOrders(index);return;}
    if(line.x!=1){team.blockedScript=line.x;return;}
    if(!team.lineStarted) {
        const auto point=nativeWaypoint(line.y);
        if(!map.inBounds(point.x,point.y)){team.blockedScript=1;return;}
        team.focusCell=cellIdx(point.x,point.y);team.focus=nativeWaypointObject(team.focusCell);
        team.focusFixed=valid(team.focus);team.lineStarted=true;
    } else if(team.focusFixed && !valid(team.focus)) {
        // The original object has gone. Do not select a replacement on its
        // cell or a later actor which happens to reuse its numeric entity ID.
        advanceNativeTeamScript(index);return;
    }
    if(!team.focusFixed && !valid(team.focus))team.focus=nativeWaypointObject(team.focusCell);
    if(!valid(team.focus)) {
        // Ground force-fire is not implemented by the shared combat orders.
        // Preserve the cell target and report the blocked line, without
        // substituting attack-move or pretending the mission is completed.
        team.blockedScript=1;return;
    }
    if(tick<team.waitUntil)return;
    bool accepted=false;
    for(EID actor:team.members)if(valid(actor)) {
        auto& e=ents[actor];
        if(e.target==team.focus && (e.state==UState::Attacking || e.state==UState::Chasing)){accepted=true;continue;}
        accepted |= orderAttack({actor},team.focus,false).accepted!=0;
    }
    team.blockedScript=accepted?-1:1;
    if(!accepted)team.waitUntil=tick+LOGIC_FPS;
}
