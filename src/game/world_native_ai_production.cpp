#include "game/world.h"
#include "game/production_work.h"
#include <algorithm>
#include <limits>

void World::nativeAIClearFactoryExit(EID actor,EID factory) {
    if(!valid(actor) || !valid(factory) || unitDef(ents[actor]).isAir())return;
    const int x=(int)ents[actor].x,y=(int)ents[actor].y;std::vector<uint8_t> mouths(map.cells.size(),0);
    for(EID id=0;id<(EID)ents.size();++id)if(ents[id].alive && ents[id].isBuilding && ents[id].player==ents[actor].player
        && nativeFactorySupports(ents[id],ents[actor].nativeIdentity.type))
        for(auto p:factoryExitCells(id,unitDef(ents[actor])))mouths[cellIdx(p.x,p.y)]=1;
    for(int radius=2;radius<=5;++radius)for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
        if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;const int cx=x+dx,cy=y+dy;
        if(!map.inBounds(cx,cy) || mouths[cellIdx(cx,cy)] || unitAtCell(cx,cy)!=INVALID_EID || bldBlocked(cx,cy)
            || !map.surfacePassable(cx,cy,unitDef(ents[actor]).pathDomain(),map.preferredLayer(cx,cy,unitDef(ents[actor]).pathDomain()),true))continue;
        bool reserved=false;for(const auto& e:ents)if(e.alive && !e.isBuilding && e.player==ents[actor].player
            && e.state==UState::Moving && (int)e.goalX==cx && (int)e.goalY==cy){reserved=true;break;}
        if(!reserved && orderMove({actor},cx+.5f,cy+.5f,false).accepted)return;
    }
}

void World::updateNativeAIProduction() {
    if(!nativeAI.active)return;
    if(nativeAIRules.isGlobal()){updateNativeSkirmishProduction();return;}
    std::erase_if(nativeAI.jobs,[&](const auto& job){return !valid(job.actor) || !ents[job.actor].isBuilding
        || ents[job.actor].player!=job.house || ents[job.actor].nativeIdentity.object!=job.object;});
    const int slot=2-nativeEvents.difficulty;
    for(int house=0;house<numPlayers;++house){auto& h=nativeAI.houses[house];
        if(!players[house].isAI || !h.production || !players[house].active || players[house].defeated)continue;
        if(tick>=h.nextChoice){h.nextChoice=tick+LOGIC_FPS;
            std::vector<int> demand(nativeTypes.size(),0);std::vector<uint64_t> oldest(nativeTypes.size(),std::numeric_limits<uint64_t>::max());
            for(int i=0;i<(int)nativeEvents.teams.size();++i){const auto& t=nativeEvents.teams[i];
                if(!t.alive || t.reinforced || t.assembled || t.memberHouse!=house)continue;
                const auto& d=nativeEventDefinition->teams[t.definition];
                for(const auto& [count,name]:nativeEventDefinition->taskForces[d.taskForce].units){const int type=nativeUnitIndex(name);if(type<0)continue;
                    int have=0;for(EID id:t.members)if(valid(id) && ents[id].nativeIdentity.type==type)++have;
                    demand[type]+=std::max(0,count-have);if(count>have)oldest[type]=std::min(oldest[type],nativeAI.teamBorn[i]);}}
            for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];
                if(!e.alive || e.isBuilding || e.player!=house || e.nativeIdentity.type<0 || e.state!=UState::Idle)continue;
                bool assigned=false;for(const auto& t:nativeEvents.teams)if(t.alive && std::find(t.members.begin(),t.members.end(),id)!=t.members.end()){assigned=true;break;}
                const auto permission=nativeAI.recruit.find(e.nativeIdentity.object);
                if(!assigned && (permission==nativeAI.recruit.end() || permission->second.normal))--demand[e.nativeIdentity.type];}
            for(int category=0;category<4;++category)if(h.wanted[category]<0){std::vector<int> candidates;int maximum=-1,best=-1;
                for(int type=0;type<(int)nativeTypes.size();++type){const auto& d=*nativeTypes[type];
                    if(d.isBuilding || d.production.category!=category || demand[type]<=0 || d.unit.cost>players[house].money || !nativeAITypeBuildable(house,type))continue;
                    if(demand[type]>maximum){maximum=demand[type];candidates.clear();}candidates.push_back(type);
                    if(best<0 || oldest[type]<oldest[best])best=type;}
                if(best>=0){if(rng.range(1,100)<=nativeAIRules.fillEarliest[slot])h.wanted[category]=best;
                    else h.wanted[category]=candidates[rng.range(0,(int)candidates.size()-1)];}}
        }
        for(EID factory=0;factory<(EID)ents.size();++factory){const auto& e=ents[factory];
            if(!productionFacilityOperational(e) || e.player!=house || !e.nativeIdentity.definition)continue;
            if(std::any_of(nativeAI.jobs.begin(),nativeAI.jobs.end(),[&](const auto& j){return j.actor==factory;}))continue;
            for(int type:h.wanted)if(type>=0 && nativeFactorySupports(e,type)){
                NativeAIFactoryJob job;job.actor=factory;job.house=house;job.type=type;job.object=e.nativeIdentity.object;nativeAI.jobs.push_back(std::move(job));break;}
        }
    }
    // Entity creation may run callbacks. Iterate identities and reacquire a job
    // after releasing an object rather than holding vector/entity references.
    const auto jobs=nativeAI.jobs;
    for(auto job:jobs){if(!valid(job.actor) || ents[job.actor].player!=job.house || ents[job.actor].nativeIdentity.object!=job.object
        || !productionFacilityOperational(ents[job.actor]) || !nativeFactorySupports(ents[job.actor],job.type))continue;
        if(job.progress<54){const int interval=nativeProductionStepTicks(job.house,job.type);if(!interval)continue;
            advanceNativeProductionWork({job.work,job.progress,job.paid},players[job.house].money,nativeTypes[job.type]->unit.cost,interval);}
        const auto matches=[&](const auto& j){return j.actor==job.actor && j.object==job.object;};
        auto current=std::find_if(nativeAI.jobs.begin(),nativeAI.jobs.end(),matches);if(current==nativeAI.jobs.end())continue;*current=job;
        if(job.progress==54){EID produced=INVALID_EID;
            if(spawnFromFactory(job.house,nativeTypes[job.type]->unit,job.type,job.actor,&produced)){
                ++producedUnits[job.house];nativeAI.houses[job.house].wanted[nativeTypes[job.type]->production.category]=-1;
                std::erase_if(nativeAI.jobs,matches);nativeAIClearFactoryExit(produced,job.actor);
            }
        }
    }
}
