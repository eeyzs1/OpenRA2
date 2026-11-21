#pragma once
#include <vector>

struct NativeTeamLeaderCandidate {
    int actor=-1,rating=5,health=0;
    bool onMap=false,limbo=false,leader=false,aircraft=false;
};
inline int nativeSelectTeamLeader(const std::vector<NativeTeamLeaderCandidate>& headOrder) {
    if(headOrder.empty())return -1;
    int result=headOrder.front().actor,rating=-1;
    for(const auto& actor:headOrder)if(actor.onMap && actor.health!=0 && !actor.limbo
        && (actor.leader || actor.aircraft) && actor.rating>rating){result=actor.actor;rating=actor.rating;}
    return result;
}
