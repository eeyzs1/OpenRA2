#include "game/world.h"
#include "game/actor_contents.h"
#include <algorithm>
#include <limits>

bool World::nativeSkirmishAIActive(int house) const {
    return nativeAIRules.isGlobal() && nativeAI.active && house>=0 && house<(int)nativeAI.houses.size()
        && nativeAI.houses[house].sourceConfigured && nativeAI.houses[house].enabled;
}

bool World::configureNativeSkirmishAI(int house,int difficulty,bool enabled,int enemy) {
    if(!nativeAIRules.isGlobal() || !nativeEventDefinition || !nativeEventDefinition->aiOnly
        || house<0 || house>=numPlayers || difficulty<0 || difficulty>3 || enemy < -2 || enemy>=numPlayers
        || (enemy>=0 && !isEnemy(house,enemy)) || !players[house].isAI)return false;
    if(!nativeEvents.active && (!enabled || !startNativeEvents(1)))return false;
    if(!nativeAI.active)return false;
    auto& h=nativeAI.houses[house];const int sourceDifficulty=std::min(difficulty,2);
    if(!h.sourceConfigured || h.sourceDifficulty!=sourceDifficulty) {
        h.nextPass=tick+2ull*(175ull*house+nativeAIRules.delays[2-sourceDifficulty]);
        h.production=enabled;
    }
    h.sourceConfigured=true;h.sourceDifficulty=sourceDifficulty;h.enabled=enabled;
    if(!enabled)h.production=false;
    if(enemy>=-1)h.sourceEnemy=enemy;
    else if(h.sourceEnemy<0 || !isEnemy(house,h.sourceEnemy) || players[h.sourceEnemy].defeated || !players[h.sourceEnemy].active) {
        h.sourceEnemy=-1;
        // Match participant identities are known before reconnaissance. This
        // stable fallback is provisional; original enemy ranking is pending.
        for(int other=0;other<numPlayers;++other)if(players[other].active && !players[other].defeated && isEnemy(house,other)){h.sourceEnemy=other;break;}
    }
    return true;
}

bool World::nativeAIGlobalCondition(int house,int trigger) const {
    const auto& d=nativeAIRules.triggers[trigger];const int enemy=nativeAI.houses[house].sourceEnemy;
    if(d.condition==-1)return true;
    const bool enemyValid=enemy>=0 && enemy<numPlayers && isEnemy(house,enemy) && players[enemy].active && !players[enemy].defeated;
    if(d.condition==5 || d.condition==6) {
        if(!enemyValid)return false;
        const auto kind=d.condition==5?SWType::IronCurtain:SWType::ChronoShift;
        if(!swAvailable(house,kind,true))return false;
        const int full=swDef(kind).chargeTime;if(full<=0)return false;
        const auto& p=players[house];
        const int remaining=p.swReady[(int)kind]?0:std::max(0,full-p.swCharge[(int)kind]);
        return (double)remaining/full <= 1.-nativeAIRules.minorSuperReady;
    }
    // The project currently merges neutral map ownership into player=-1.
    // Do not claim the calibrated Civilian-side predicate is represented.
    if(d.condition==7)return false;
    int value=0;
    if(d.condition==0 || d.condition==1) {
        const int owner=d.condition==1?house:enemy;if(owner<0 || (d.condition==0 && !enemyValid))return false;
        const auto count=[&](const auto& actor){if(!actor.nativeIdentity.definition)return;
            auto id=actor.nativeIdentity.definition->id;for(char& c:id)if(c>='A' && c<='Z')c+='a'-'A';if(id==d.conditionObject)++value;};
        visitLivePlayerContents(*this,owner,count);
    } else if(d.condition==2 || d.condition==3) {
        return enemyValid && players[enemy].powerMade-players[enemy].powerUsed < (d.condition==2?100:0);
    } else if(d.condition==4) {if(!enemyValid)return false;value=players[enemy].money;}
    else return false;
    return d.comparison==0?value<d.operand:d.comparison==1?value<=d.operand:d.comparison==2?value==d.operand:
        d.comparison==3?value>=d.operand:d.comparison==4?value>d.operand:value!=d.operand;
}

int World::nativeAITeamCount(int team,int type) const {
    if(!nativeAIRules.isGlobal()) {int n=0;for(EID id:nativeEvents.teams[team].members)if(valid(id) && ents[id].nativeIdentity.type==type)++n;return n;}
    int n=0;const int house=nativeEvents.teams[team].memberHouse;
    const auto count=[&](const auto& actor){if(actor.nativeIdentity.type!=type)return;
        const auto found=nativeAI.memberTeams.find(actor.nativeIdentity.object);
        if(found!=nativeAI.memberTeams.end() && found->second==team)++n;};
    visitLivePlayerContents(*this,house,count);
    return n;
}

void World::updateNativeSkirmishProduction() {
    for(int house=0;house<numPlayers;++house) {
        auto& h=nativeAI.houses[house];
        if(!nativeSkirmishAIActive(house) || !h.production || !players[house].active || players[house].defeated || tick<h.nextChoice)continue;
        h.nextChoice=tick+LOGIC_FPS;
        std::vector<int> demand(nativeTypes.size(),0);
        std::vector<uint64_t> oldest(nativeTypes.size(),std::numeric_limits<uint64_t>::max());
        for(int i=0;i<(int)nativeEvents.teams.size();++i) {
            const auto& t=nativeEvents.teams[i];if(!t.alive || t.reinforced || t.memberHouse!=house)continue;
            const auto& d=nativeEventDefinition->teams[t.definition];if(t.assembled && !d.reinforce)continue;
            std::map<int,int> wanted;for(const auto& [count,name]:nativeEventDefinition->taskForces[d.taskForce].units){const int type=nativeUnitIndex(name);if(type>=0)wanted[type]+=count;}
            for(auto [type,count]:wanted){const int missing=std::max(0,count-nativeAITeamCount(i,type));demand[type]+=missing;if(missing)oldest[type]=std::min(oldest[type],nativeAI.teamBorn[i]);}
        }
        // Factory products may still be walking off the apron before they can
        // be recruited. They already satisfy demand; excluding moving actors
        // buys an extra unit which can block the sole exit behind the team.
        for(const auto& e:ents)if(e.alive && !e.isBuilding && e.player==house && e.nativeIdentity.type>=0
            && !nativeAI.memberTeams.contains(e.nativeIdentity.object)) {
            const auto permission=nativeAI.recruit.find(e.nativeIdentity.object);
            if(permission==nativeAI.recruit.end() || permission->second.automatic)--demand[e.nativeIdentity.type];
        }
        for(int category=0;category<4;++category) {
            const auto pending=[&](int selector){if(nativeProductionType(selector))--demand[selector-NATIVE_PRODUCTION_BASE];};
            const auto& item=players[house].unitProd[category];if(item.active)pending(item.typeIdx);
            for(int selector:players[house].unitQueue[category])pending(selector);
            if(unitQueuedCount(house,category)>=2)continue;
            std::vector<int> candidates;int maximum=-1,best=-1;
            for(int type=0;type<(int)nativeTypes.size();++type) {
                const auto& d=*nativeTypes[type];if(d.isBuilding || d.production.category!=category || demand[type]<=0 || !nativeAITypeBuildable(house,type))continue;
                if(demand[type]>maximum){maximum=demand[type];candidates.clear();}if(demand[type]==maximum)candidates.push_back(type);
                if(best<0 || oldest[type]<oldest[best])best=type;
            }
            if(best<0)continue;
            const int chosen=rng.range(1,100)<=nativeAIRules.fillEarliest[2-h.sourceDifficulty]?best:candidates[rng.range(0,(int)candidates.size()-1)];
            Cmd command{};command.type=Cmd::StartUnitProd;command.a=NATIVE_PRODUCTION_BASE+chosen;
            applyNativeProductionCommand(house,command);
        }
    }
}
