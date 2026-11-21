#include "game/world.h"
#include <algorithm>
#include <cmath>

void World::startNativeAI() {
    auto centers=std::move(nativeAI.centers);nativeAI={};
    if(nativeAIRules.hasOriginalTeamControl())nativeAI.centers=std::move(centers);
    if(!nativeAIRules.available || !nativeEvents.active)return;
    nativeAI.active=true;nativeAI.recruit=nativeAIRules.originalRecruit;
    const int slot=2-nativeEvents.difficulty;
    for(int house=0;house<numPlayers;++house){NativeAIHouseState h;h.ratio=nativeAIRules.ratios[house];
        h.nextPass=tick+2ull*(175ull*house+nativeAIRules.delays[slot]);nativeAI.houses.push_back(h);}
    for(const auto& t:nativeAIRules.triggers)nativeAI.triggers.push_back({t.weight,0,0});
    for(const auto& t:nativeEventDefinition->teams)nativeAI.autocreate.push_back(t.autocreate);
    nativeAI.teamBorn.assign(nativeEvents.teams.size(),tick);
    if(nativeAIRules.hasOriginalTeamControl())nativeAI.underStrength.assign(nativeEvents.teams.size(),1);
}

void World::nativeAITeamCreated(int team) {
    if(nativeAI.active && team==(int)nativeAI.teamBorn.size())nativeAI.teamBorn.push_back(tick);
    if(nativeAIRules.hasOriginalTeamControl() && nativeAI.active && team==(int)nativeAI.underStrength.size())nativeAI.underStrength.push_back(1);
}

void World::nativeAITeamOutcome(int team) {
    if(!nativeAI.active)return;const auto& t=nativeEvents.teams[team];
    for(int i=0;i<(int)nativeAIRules.triggers.size();++i)if(nativeAIRules.triggers[i].first==t.definition){
        auto& state=nativeAI.triggers[i];const auto& d=nativeAIRules.triggers[i];
        const double history=(double)state.successes-(double)state.runs*.5;
        const double change=t.succeeded?std::max(0.,history)+nativeAIRules.successDelta:
            std::min(0.,history*nativeAIRules.historyCoefficient)+nativeAIRules.failureDelta;
        state.weight=std::clamp(state.weight+change,d.minimum,d.maximum);
        ++state.runs;if(t.succeeded)++state.successes;
    }
}

bool World::nativeAITypeBuildable(int house,int type) const {
    if(type<0 || type>=(int)nativeTypes.size() || nativeTypes[type]->isBuilding)return false;
    return nativeProductionRejection(house,true,NATIVE_PRODUCTION_BASE+type)==ProductionRejectReason::None;
}

bool World::nativeAITriggerEligible(int house,int index,bool defensiveOnly,bool skipDefense) const {
    if(!nativeAI.active || house<0 || house>=numPlayers || index<0 || index>=(int)nativeAIRules.triggers.size())return false;
    const auto& d=nativeAIRules.triggers[index];
    const bool defensive=nativeEventDefinition->teams[d.first].baseDefense
        && (d.second<0 || nativeEventDefinition->teams[d.second].baseDefense);
    const int difficulty=nativeAIRules.isGlobal()?nativeAI.houses[house].sourceDifficulty:nativeEvents.difficulty;
    if(!d.enabled || !d.difficulty[difficulty] || (!nativeAIRules.isGlobal() && d.house!=-2 && d.house!=house)
        || (defensiveOnly && !defensive) || (skipDefense && defensive))return false;
    // Imported campaign sources currently provide only map-local triggers.
    if(nativeAIRules.isGlobal()) {
        auto country=productionCountry(house);for(auto& c:country)if(c>='A' && c<='Z')c+='a'-'A';
        const auto side=nativeAIRules.countrySides.find(country);
        if(!d.skirmish || (!d.country.empty() && d.country!=country) || side==nativeAIRules.countrySides.end()
            || (d.side && d.side!=side->second) || !nativeAIGlobalCondition(house,index))return false;
    } else if(!nativeAIRules.ignoreGlobal || d.side!=0)return false;
    int enemy=nativeAIRules.isGlobal()?nativeAI.houses[house].sourceEnemy:nativeEventDefinition->playerHouse;if(!isEnemy(house,enemy))enemy=-1;
    if(!nativeAIRules.isGlobal() && d.condition!=-1){int value=0;bool direct=false;
        if(d.condition==0 || d.condition==1){const int owner=d.condition==1?house:enemy;if(owner<0)return false;
            for(const auto& e:ents)if(e.alive && e.player==owner && e.nativeIdentity.definition){
                auto id=e.nativeIdentity.definition->id;for(auto& c:id)if(c>='A' && c<='Z')c+='a'-'A';if(id==d.conditionObject)++value;}}
        else if(d.condition==2 || d.condition==3){if(enemy<0 || players[enemy].powerMade-players[enemy].powerUsed>=(d.condition==2?100:0))return false;direct=true;}
        else if(d.condition==4){if(enemy<0)return false;value=players[enemy].money;}
        else return false;
        const bool condition=d.comparison==0?value<d.operand:d.comparison==1?value<=d.operand:d.comparison==2?value==d.operand:
            d.comparison==3?value>=d.operand:d.comparison==4?value>d.operand:value!=d.operand;
        if(!direct && !condition)return false;
    }
    if(enemy<0 && !defensive)return false;
    for(int definition:{d.first,d.second})if(definition>=0){const auto& team=nativeEventDefinition->teams[definition];
        int count=0;for(const auto& t:nativeEvents.teams)if(t.alive && t.memberHouse==house && t.definition==definition)++count;
        if(team.max>=0 && count>=team.max)return false;
        if(nativeTeamBlocker(definition)!=-1)return false;
        for(const auto& [amount,id]:nativeEventDefinition->taskForces[team.taskForce].units){const int type=nativeUnitIndex(id);
            if(type<0)return false;const int level=nativeTypes[type]->production.techLevel;
            if((level<0?11:level)>nativeProductionRules.techLevels[house])return false;
            if(!nativeAITypeBuildable(house,type)){
                bool recruitable=false;for(const auto& e:ents)if(e.alive && !e.isBuilding && e.player==house && e.nativeIdentity.type==type){
                    auto permission=nativeAI.recruit.find(e.nativeIdentity.object);const NativeRecruitState defaults{};
                    const auto& p=permission==nativeAI.recruit.end()?defaults:permission->second;
                    if(nativeAI.autocreate[definition]?p.automatic:p.normal){recruitable=true;break;}}
                if(!recruitable)return false;
            }
        }
    }
    return true;
}

void World::updateNativeAIScheduler() {
    if(!nativeAI.active)return;
    for(int house=0;house<numPlayers;++house){auto& h=nativeAI.houses[house];
        const int slot=2-(nativeAIRules.isGlobal()?h.sourceDifficulty:nativeEvents.difficulty);
        if(tick<h.nextPass)continue;h.nextPass=tick+2ull*nativeAIRules.delays[slot];
        if(!players[house].isAI || !h.enabled || !players[house].active || players[house].defeated || rng.range(1,100)>h.ratio)continue;
        int count=0,defense=0,oldest=-1;
        for(int i=0;i<(int)nativeEvents.teams.size();++i){const auto& t=nativeEvents.teams[i];if(!t.alive || t.memberHouse!=house)continue;
            ++count;if(nativeEventDefinition->teams[t.definition].baseDefense){++defense;if(oldest<0)oldest=i;}}
        bool skipDefense=defense>nativeAIRules.maximumDefense[slot];
        if(count>=nativeAIRules.cap[slot] && defense>=count/2 && oldest>=0){finishNativeTeam(oldest);--count;--defense;skipDefense=true;}
        if(count>=nativeAIRules.cap[slot])continue;
        const bool defensiveOnly=nativeAIRules.useMinDefense && defense<nativeAIRules.minimumDefense[slot];
        std::vector<std::pair<int,uint64_t>> eligible;uint64_t total=0;
        for(int i=0;i<(int)nativeAIRules.triggers.size();++i)if(nativeAITriggerEligible(house,i,defensiveOnly,skipDefense)){
            const auto weight=(uint64_t)std::max(0.,std::trunc(nativeAI.triggers[i].weight));if(weight){total+=weight;eligible.emplace_back(i,total);}}
        if(!total)continue;const uint64_t draw=rng.next()%total;
        int chosen=-1;for(auto [id,end]:eligible)if(draw<end){chosen=id;break;}if(chosen<0)continue;
        const auto& d=nativeAIRules.triggers[chosen];bool pending=false;
        for(const auto& t:nativeEvents.teams)if(t.alive && t.memberHouse==house && !t.assembled && (t.definition==d.first || t.definition==d.second)){pending=true;break;}
        if(pending)continue;
        for(int definition:{d.first,d.second})if(definition>=0){nativeAI.autocreate[definition]=1;int all=0;
            for(const auto& t:nativeEvents.teams)all+=t.alive && t.definition==definition && (!nativeAIRules.isGlobal() || t.memberHouse==house);
            const auto& team=nativeEventDefinition->teams[definition];if(team.max>=0 && all>=team.max)continue;
            const int created=createNativeTeam(definition,false);if(created>=0)nativeEvents.teams[created].memberHouse=house;
        }
    }
}
