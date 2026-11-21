#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool contains(const std::vector<std::string>& values,const std::string& value){return std::find(values.begin(),values.end(),value)!=values.end();}
}

bool World::createSourceRandomCrate(){
    if(!nativeCrateRules.randomCrates || crates.size()>=1024)return false;
    if(std::count_if(crates.begin(),crates.end(),[](const auto& c){return c.alive && c.kind==NATIVE_CRATE_BASE+19;})>=256)return false;
    for(int attempt=0;attempt<1000;++attempt){
        const int x=rng.range(0,map.w-1),y=rng.range(0,map.h-1);
        if((!map.passable(x,y) && map.at(x,y).terrain!=Terrain::Water) || bldBlocked(x,y) || unitAtCell(x,y)!=INVALID_EID)continue;
        if(std::any_of(crates.begin(),crates.end(),[&](const auto& c){return c.alive && c.x==x && c.y==y;}))continue;
        // Original CrateClass: [450,1800] source frames per Regen minute,
        // using one normalized 31-bit draw; the World runs at twice that rate.
        const double low=nativeCrateRules.regenMinutes*450.0,high=nativeCrateRules.regenMinutes*1800.0;
        const double fraction=rng.range(0,2147483646)/2147483646.0;
        Crate c;c.x=x;c.y=y;c.kind=NATIVE_CRATE_BASE+19;c.remainingTicks=std::max(2,(int)(low+(high-low)*fraction)*2);
        crates.push_back(c);return true;
    }
    return false;
}

void World::updateSourceRandomCrates(){
    if(!cratesEnabled)return;
    for(auto& c:crates)if(c.alive && c.kind==NATIVE_CRATE_BASE+19 && --c.remainingTicks<=0)c.alive=false;
    crates.erase(std::remove_if(crates.begin(),crates.end(),[](const auto& c){return !c.alive;}),crates.end());
    const int desired=std::min(nativeCrateRules.maximum,std::max(nativeCrateRules.minimum,numPlayers));
    int active=(int)std::count_if(crates.begin(),crates.end(),[](const auto& c){return c.alive && c.kind==NATIVE_CRATE_BASE+19;});
    while(active<desired){if(!createSourceRandomCrate())break;++active;}
}

bool World::pickupSourceCrate(Ent& collector,const Crate& crate){
    if(!nativeCrateRules.randomCrates || !nativeCrateType(crate.kind) || !collector.nativeIdentity.definition || collector.player<0 || collector.player>=numPlayers)return false;
    const int player=collector.player;int kind=crate.kind-NATIVE_CRATE_BASE;
    if(kind==19){int total=0;for(int share:nativeCrateRules.shares)total+=share;
        if(total<=0)return false;int draw=rng.range(1,total);kind=0;
        for(;kind<19;++kind){draw-=nativeCrateRules.shares[kind];if(draw<=0)break;}if(kind>=19)return false;}
    int buildings=0,vehicles=0;bool hasBaseUnit=false,hasYard=false,hasHarvester=false;
    for(const auto& e:ents)if(e.alive && e.player==player && e.nativeIdentity.definition && !e.nativeIdentity.definition->kind){
        const auto& d=*e.nativeIdentity.definition;buildings+=e.isBuilding;
        vehicles+=!e.isBuilding && (d.production.category==1 || d.production.category==3);
        hasBaseUnit |= contains(nativeCrateRules.baseUnits,d.id);hasYard |= contains(nativeCrateRules.constructionYards,d.id);
        hasHarvester |= contains(nativeCrateRules.harvesters,d.id);
    }
    const bool rescue=buildings==0 && players[player].money>1500 && !hasBaseUnit;
    if(rescue)kind=1;
    if(kind==1 && (vehicles>50 || map.at(crate.x,crate.y).terrain==Terrain::Water))kind=0;
    if(map.at(crate.x,crate.y).terrain==Terrain::Water && !nativeCrateRules.water[kind])kind=0;
    if((kind==9 || kind==10 || kind==11) && nativeCrateMultiplier(collector.nativeIdentity,kind)!=1)kind=0;
    if(kind==14 && (!collector.nativeIdentity.definition->trainable || collector.vetRank>=2))kind=0;
    if(kind==11 && !std::any_of(collector.nativeIdentity.definition->guns.slots.begin(),collector.nativeIdentity.definition->guns.slots.end(),[](const auto& gun){return gun.present;}))kind=0;
    // Original multiplayer removal replenishes before applying the reward.
    if(cratesEnabled)createSourceRandomCrate();
    float moneyBase=nativeCrateRules.data[0];
    if(kind==1){
        const auto forCountry=[&](const std::vector<std::string>& names){const auto country=productionCountry(player);
            for(const auto& id:names){const int type=nativeProductionCode(id)-NATIVE_PRODUCTION_BASE;
                if(type>=0 && contains(nativeTypes[type]->production.owners,country))return type;}return -1;};
        int type=rescue?forCountry(nativeCrateRules.baseUnits):-1;
        if(type<0 && hasYard && !hasHarvester)type=forCountry(nativeCrateRules.harvesters);
        if(!nativeCrateRules.unitCrateType.empty())type=nativeProductionCode(nativeCrateRules.unitCrateType)-NATIVE_PRODUCTION_BASE;
        if(type<0){std::vector<int> all,eligible;
            for(const auto& id:nativeCrateRules.vehicles){const int index=nativeProductionCode(id)-NATIVE_PRODUCTION_BASE;all.push_back(index);
                if(index>=0 && nativeTypes[index]->production.crateGoodie && (!contains(nativeCrateRules.baseUnits,id) || !players[player].isAI || rescue))eligible.push_back(index);}
            if(!eligible.empty()){
                // Match the original rejection draw over VehicleTypes. A bound
                // also protects corrupted/all-zero RNG state from hanging.
                for(int attempt=0;attempt<4096 && type<0;++attempt){const int index=all[rng.range(0,(int)all.size()-1)];
                    if(std::find(eligible.begin(),eligible.end(),index)!=eligible.end())type=index;}
                if(type<0)type=eligible.front();
            }
        }
        if(type>=0){
            for(int radius=0;radius<=4;++radius)for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
                if(std::max(std::abs(dx),std::abs(dy))!=radius)continue;const int x=crate.x+dx,y=crate.y+dy;
                if(!map.inBounds(x,y) || !map.passable(x,y) || bldBlocked(x,y) || unitAtCell(x,y)!=INVALID_EID)continue;
                const auto id=spawnUnit(player,nativeTypes[type]->unit.type,x+.5f,y+.5f,type);
                if(valid(id)){emitSoundAt(Sfx::Cash,collector.x,collector.y);eva(player,TR(S::CrateUnit));return true;}
            }
            moneyBase=nativeCrateRules.data[1];
        }
        kind=0;
    }
    if(kind==0){const auto before=players[player].money;changeMoney(player,(int)moneyBase+rng.range(0,900));
        emitSoundAt(Sfx::Cash,collector.x,collector.y);eva(player,formatText(TR(S::CrateMoney),(int)(players[player].money-before)));return true;}
    if(kind==2){for(auto& e:ents)if(e.alive && e.player==player && e.nativeIdentity.definition && !e.nativeIdentity.definition->kind)
            e.hp=e.isBuilding?bldDef(e).hp:unitDef(e).hp;eva(player,TR(S::CrateHeal));return true;}
    if(kind==8){for(int y=0;y<map.h;++y)for(int x=0;x<map.w;++x)map.reveal(player,x,y,0);eva(player,TR(S::CrateReveal));return true;}
    if(kind!=9 && kind!=10 && kind!=11 && kind!=14)return false;
    for(auto& e:ents){if(!e.alive || e.player<0 || !e.nativeIdentity.definition || e.nativeIdentity.definition->kind || e.airborne()
            || e.parasiting || e.magneticHeight>0 || e.nativeIdentity.object.empty())continue;
        const float x=e.x+(e.isBuilding?bldDef(e).w*.5f:0),y=e.y+(e.isBuilding?bldDef(e).h*.5f:0);
        if(distf(x,y,crate.x+.5f,crate.y+.5f)>=nativeCrateRules.radius)continue;
        if(kind==14){if(!e.isBuilding && e.nativeIdentity.definition->trainable){
                // Original VeterancyClass setters promote to the exact 1/2
                // experience boundary. Keep the value used by later kills in
                // sync with the displayed rank, including fractional Data.
                e.vetRank=std::min(2,e.vetRank+(int)std::ceil(nativeCrateRules.data[14]));
                const double boundary=std::ceil(std::max(1,unitDef(e).cost)*(double)g_gameRules.veteranRatio*e.vetRank);
                e.veterancyValue=(int)std::min((double)std::numeric_limits<int>::max(),std::max((double)e.veterancyValue,boundary));
            }continue;}
        if(kind==10 && (e.isBuilding || unitDef(e).isAir()))continue;
        auto& bonus=nativeCrateBonuses[e.nativeIdentity.object];float& value=kind==9?bonus.armor:kind==10?bonus.speed:bonus.firepower;
        if(value==1)value=nativeCrateRules.data[kind];
    }
    eva(player,TR(kind==9?S::CrateArmor:kind==10?S::CrateSpeed:kind==11?S::CratePower:S::CrateVet));return true;
}
