#include "game/world.h"

void World::setNativeTeamScriptStep(int index,int step) {
    nativeConditions.unloading.erase(index);
    auto& team=nativeEvents.teams[index];team.step=step;
    team.lineStarted=team.focusFixed=team.advancePending=false;
    team.focus=team.focusCell=-1;team.focusLayer=-1;team.waitUntil=0;team.blockedScript=-1;
    // Success belongs to the lifetime of the team, not to a script line.
    // Changing the cursor must not cancel members' actual orders.
}

void World::updateNativeTeamControl(int index) {
    auto& team=nativeEvents.teams[index];
    const auto& script=nativeEventDefinition->scripts[nativeEventDefinition->teams[team.definition].script];
    const auto line=script.steps[team.step];
    if((line.x==39 || line.x==40) && nativeConditions.active) {
        if(line.y<0 || line.y>=50){team.blockedScript=line.x;return;}
        setNativeLocal(line.y,line.x==39);setNativeTeamScriptStep(index,team.step+1);
    } else if(line.x==49) {
        team.succeeded=true;
        setNativeTeamScriptStep(index,team.step+1);
    } else if(line.x==6) {
        // Parameters count occupied source slots from one. The parser keeps
        // that order. Off-end jumps retire the team on the following turn.
        const int target=line.y>0 && line.y<=(int)script.steps.size()?line.y-1:(int)script.steps.size();
        setNativeTeamScriptStep(index,target);
    }
    // Exactly one line per update, including a self-jump or a final Success.
}
