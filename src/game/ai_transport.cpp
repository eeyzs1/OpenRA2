#include "game/ai.h"
#include <algorithm>
#include <limits>

namespace {
constexpr float HomeRange=12.f;
Vec2i homePoint(const World& world,int player) {
    for(auto type:{BldType::Barracks,BldType::ConYard,BldType::OreRefinery})
        for(const auto& unit:world.ents)if(unit.alive && unit.isBuilding && unit.player==player
            && unit.btype==type && !unit.selling && unit.constructAnim<=0)
            return {(int)unit.x+bldDef(unit).w/2,(int)unit.y+bldDef(unit).h/2};
    return {-1,-1};
}

std::vector<uint8_t> region(const World& world,Vec2i start,int domain,bool terrainOnly,EID mover=INVALID_EID) {
    std::vector<uint8_t> seen(world.map.cells.size(),0);
    if(!world.map.inBounds(start.x,start.y))return seen;
    auto open=[&](int x,int y) {
        if(!world.passableFor(x,y,domain) || (!terrainOnly && world.bldBlocked(x,y)))return false;
        if(!terrainOnly) {
            EID id=world.unitAtCell(x,y);
            if(world.valid(id) && id!=mover) {
                const auto& unit=world.ents[id];
                if(unit.immobilized() || unit.deployed || unit.radDeployed || unit.guard
                    || (!world.isAllied(world.ents[mover].player,unit.player) && unit.state==UState::Idle))return false;
            }
        }
        return true;
    };
    constexpr Vec2i dirs[]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
    std::vector<Vec2i> queue{start};seen[world.cellIdx(start.x,start.y)]=1;
    for(size_t head=0;head<queue.size();++head) {
        auto from=queue[head];
        for(auto d:dirs) {
            const int x=from.x+d.x,y=from.y+d.y;
            if(!world.map.inBounds(x,y))continue;
            auto& mark=seen[world.cellIdx(x,y)];
            if(mark || !open(x,y) || !world.map.climbOk(from.x,from.y,x,y,domain))continue;
            if(d.x && d.y && (!open(from.x+d.x,from.y) || !open(from.x,from.y+d.y)
                || !world.map.climbOk(from.x,from.y,from.x+d.x,from.y,domain)
                || !world.map.climbOk(from.x,from.y,from.x,from.y+d.y,domain)))continue;
            mark=1;
            queue.push_back({x,y});
        }
    }
    return seen;
}

bool overseasLand(const World& world,const std::vector<uint8_t>& home) {
    for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x)
        if(world.map.passable(x,y) && !home[world.cellIdx(x,y)])return true;
    return false;
}

bool assaultPassenger(const World::Ent& unit,int player) {
    if(!unit.alive || unit.isBuilding || unit.player!=player || unit.immobilized() || unit.deployed || unit.radDeployed)return false;
    const auto& def=unitDef(unit);
    return !def.isAir() && !def.isNaval() && def.transportSize()<=6 && !def.canHarvet() && !def.isPsychic() && unit.utype!=UnitType::AttackDog
        && !unit.guard && unit.state!=UState::Servicing && unit.mindBy==INVALID_EID
        && unit.mindTarget==INVALID_EID && unit.mindTargets.empty() && unit.parasite==INVALID_EID && !unit.parasiting
        && def.weapon.damage>0 && def.weapon.antiGround;
}

void guideLandingParties(World& world,int player,const std::vector<uint8_t>& home) {
    std::vector<EID> remaining;
    for(size_t id=0;id<world.ents.size();++id) {
        const auto& unit=world.ents[id];
        if(!assaultPassenger(unit,player) || home[world.cellIdx((int)unit.x,(int)unit.y)] || unit.state==UState::Boarding)continue;
        if((unit.state==UState::Chasing || unit.state==UState::Attacking) && world.valid(unit.target))continue;
        if(unit.state==UState::AttackMoving && unit.pathIdx<(int)unit.path.size())continue;
        remaining.push_back((EID)id);
    }
    while(!remaining.empty()) {
        const EID leadId=remaining.front();const auto& lead=world.ents[leadId];
        const auto land=region(world,{(int)lead.x,(int)lead.y},0,false,leadId);
        std::vector<EID> party;
        std::erase_if(remaining,[&](EID id) {
            const auto& unit=world.ents[id];
            if(!land[world.cellIdx((int)unit.x,(int)unit.y)])return false;
            party.push_back(id);return true;
        });
        EID enemy=INVALID_EID;float nearest=std::numeric_limits<float>::max();
        for(size_t id=0;id<world.ents.size();++id) {
            const auto& unit=world.ents[id];if(!unit.alive || !world.isEnemy(player,unit.player))continue;
            float x=unit.x+(unit.isBuilding?bldDef(unit).w/2.0f:0),y=unit.y+(unit.isBuilding?bldDef(unit).h/2.0f:0);
            if(!world.map.inBounds((int)x,(int)y) || home[world.cellIdx((int)x,(int)y)]
                || world.map.fogAt(player,(int)x,(int)y)!=FOG_VISIBLE || world.attackRejection(leadId,(EID)id)!=AttackRejectReason::None)continue;
            float distance=distf(lead.x,lead.y,x,y);
            if(distance<nearest){nearest=distance;enemy=(EID)id;}
        }
        if(world.valid(enemy) && world.orderAttack(party,enemy).accepted>0)continue;
        Vec2i scout{-1,-1},patrol{-1,-1};nearest=std::numeric_limits<float>::max();float farthest=0;
        for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x) {
            if(!land[world.cellIdx(x,y)])continue;
            float distance=distf(lead.x,lead.y,x+0.5f,y+0.5f);
            if(world.map.fogAt(player,x,y)==FOG_UNSEEN && distance<nearest){nearest=distance;scout={x,y};}
            if(distance>farthest){farthest=distance;patrol={x,y};}
        }
        if(scout.x<0)scout=patrol;
        if(scout.x>=0)world.orderMove(party,scout.x+0.5f,scout.y+0.5f,true);
    }
}
}

bool SkirmishAI::needsTransport(const World& world) const {
    const auto home=homePoint(world,player);if(home.x<0)return false;
    return overseasLand(world,region(world,home,0,true));
}

void SkirmishAI::doTransports(World& world) {
    std::vector<EID> boats;
    for(size_t id=0;id<world.ents.size();++id) {
        const auto& unit=world.ents[id];
        if(unit.alive && !unit.isBuilding && unit.player==player && (unit.utype==UnitType::AmphTransport
            || (unit.utype==UnitType::Nighthawk && world.usesNativeAirNavigation(unit))))boats.push_back((EID)id);
    }
    if(boats.empty() && std::none_of(world.ents.begin(),world.ents.end(),[&](const auto& unit){return assaultPassenger(unit,player);}))return;
    const auto home=homePoint(world,player);if(home.x<0)return;
    const auto homeLand=region(world,home,0,true);
    if(!overseasLand(world,homeLand)) {
        for(EID id:boats)if(!world.ents[id].immobilized() && !world.ents[id].cargo.empty()
            && (world.ents[id].state==UState::Idle || world.usesNativeAirNavigation(world.ents[id])))world.orderUnload({id});
        return;
    }
    guideLandingParties(world,player,homeLand);
    for(EID id:boats) {
        auto& boat=world.ents[id];
        const bool air=world.usesNativeAirNavigation(boat);
        const bool available=boat.state==UState::Idle || (air && (boat.state==UState::Landed || boat.state==UState::Circling
            || boat.state==UState::Chasing || boat.state==UState::Attacking));
        if(boat.immobilized() || !available || (air && (boat.nativeAirUnload || boat.guard)))continue;
        const auto reachable=air?std::vector<uint8_t>{}:region(world,{(int)boat.x,(int)boat.y},2,false,id);
        const auto occupied=[&](int x,int y){
            const int layer=air?world.map.preferredLayer(x,y,0):0;const EID unit=world.unitAtCell(x,y,layer);
            return world.valid(unit) && (!air || (unit!=id && (world.ents[unit].player==player || world.map.fogAt(player,x,y)==FOG_VISIBLE)));
        };
        const auto canReach=[&](int x,int y){
            if(!air)return reachable[world.cellIdx(x,y)]!=0;
            const int layer=world.map.preferredLayer(x,y,0);
            if(!world.map.surfacePassable(x,y,0,layer,false))return false;
            // Plan from terrain, own actors and current observations. The
            // physical landing controller rechecks actual occupancy on arrival.
            const EID building=world.bldAt(x,y);
            const bool known=world.map.fogAt(player,x,y)==FOG_VISIBLE || (world.valid(building) && world.ents[building].player==player);
            return !(known && world.bldBlocked(x,y,layer)) && !occupied(x,y);
        };
        const bool atHome=homeLand[world.cellIdx((int)boat.x,(int)boat.y)] && distf(boat.x,boat.y,(float)home.x,(float)home.y)<HomeRange;
        auto pickup=[&]() {
            Vec2i goal{-1,-1};float best=std::numeric_limits<float>::max();
            for(int y=std::max(0,home.y-8);y<std::min(world.map.h,home.y+9);++y)
                for(int x=std::max(0,home.x-8);x<std::min(world.map.w,home.x+9);++x) {
                    if(!homeLand[world.cellIdx(x,y)] || !canReach(x,y) || (!air && world.bldBlocked(x,y)) || occupied(x,y))continue;
                    // Include the cell-center offset and arrival tolerance, so
                    // the selected staging point actually enters home range.
                    if(distf(x+.5f,y+.5f,(float)home.x,(float)home.y)>=HomeRange-.5f)continue;
                    if(distf((float)x,(float)y,(float)home.x,(float)home.y)<3)continue;
                    bool exit=false;
                    for(const auto& b:world.ents)if(b.alive && b.isBuilding && b.player==player) {
                        const auto& def=bldDef(b);
                        if(b.btype==BldType::WarFactory || b.btype==BldType::Barracks)
                            exit |= distf(x+0.5f,y+0.5f,b.x+def.w/2.0f+0.5f,b.y+def.h+0.5f)<2.5f;
                        if(b.btype==BldType::OreRefinery)exit |= distf(x+0.5f,y+0.5f,b.x+3.5f,b.y+1.5f)<3;
                    }
                    if(exit)continue;
                    const float score=distf(boat.x,boat.y,x+0.5f,y+0.5f);
                    if(score<best){best=score;goal={x,y};}
                }
            if(goal.x>=0)world.orderMove({id},goal.x+0.5f,goal.y+0.5f,false);
        };
        if(boat.cargo.empty() && !atHome){pickup();continue;}
        if(atHome && World::cargoLoad(boat.cargo)<unitDef(boat).cargoCap) {
            int incoming=0;
            for(const auto& unit:world.ents)if(unit.alive && unit.player==player && unit.state==UState::Boarding && unit.target==id)incoming+=unitDef(unit).transportSize();
            std::vector<EID> candidates;
            for(size_t index=0;index<world.ents.size();++index) {
                const auto& unit=world.ents[index];
                if(!assaultPassenger(unit,player) || unit.state==UState::Boarding || unit.state==UState::Attacking
                    || unit.state==UState::Chasing || !homeLand[world.cellIdx((int)unit.x,(int)unit.y)]
                    || distf(unit.x,unit.y,boat.x,boat.y)>20)continue;
                if(air && world.entryRejection((EID)index,id,EntryKind::Board)!=EntryRejectReason::None)continue;
                candidates.push_back((EID)index);
            }
            std::stable_sort(candidates.begin(),candidates.end(),[&](EID a,EID b){
                const auto& x=world.ents[a];const auto& y=world.ents[b];
                return distf(x.x,x.y,boat.x,boat.y)<distf(y.x,y.y,boat.x,boat.y);
            });
            int slots=unitDef(boat).cargoCap-World::cargoLoad(boat.cargo)-incoming;
            for(EID troop:candidates) {
                const int size=unitDef(world.ents[troop]).transportSize();
                if(slots>=size && world.orderBoard({troop},id).accepted){slots-=size;incoming+=size;}
            }
            if(incoming>0 || boat.cargo.empty())continue;
        }
        // Only observed overseas enemies guide landing; otherwise explore the
        // nearest reachable, unseen foreign shore using terrain and own fog.
        EID enemy=INVALID_EID;float bestEnemy=std::numeric_limits<float>::max();
        for(size_t index=0;index<world.ents.size();++index) {
            const auto& unit=world.ents[index];if(!unit.alive || !world.isEnemy(player,unit.player) || unit.airborne())continue;
            float x=unit.x+(unit.isBuilding?bldDef(unit).w/2.0f:0),y=unit.y+(unit.isBuilding?bldDef(unit).h/2.0f:0);
            if(!world.map.inBounds((int)x,(int)y) || !world.map.passable((int)x,(int)y)
                || homeLand[world.cellIdx((int)x,(int)y)] || world.map.fogAt(player,(int)x,(int)y)!=FOG_VISIBLE)continue;
            float score=distf(boat.x,boat.y,x,y)+(unit.isBuilding?0:10);
            if(score<bestEnemy){bestEnemy=score;enemy=(EID)index;}
        }
        const bool onForeignLand=!homeLand[world.cellIdx((int)boat.x,(int)boat.y)] && world.map.passable((int)boat.x,(int)boat.y);
        bool nearEnemy=false;
        if(world.valid(enemy)) {
            const auto& target=world.ents[enemy];
            nearEnemy=distf(boat.x,boat.y,target.x+(target.isBuilding?bldDef(target).w/2.0f:0),
                target.y+(target.isBuilding?bldDef(target).h/2.0f:0))<=10;
        }
        if(!atHome && (onForeignLand || nearEnemy) && !boat.cargo.empty()) {
            const auto release=world.orderUnload({id});
            if(release.pending>0)continue;
            if(release.released>0) {
                std::vector<EID> party;
                for(size_t index=0;index<world.ents.size();++index) {
                    const auto& unit=world.ents[index];
                    if(assaultPassenger(unit,player) && !homeLand[world.cellIdx((int)unit.x,(int)unit.y)]
                        && distf(unit.x,unit.y,boat.x,boat.y)<6)party.push_back((EID)index);
                }
                if(world.valid(enemy))world.orderAttack(party,enemy);
                else if(!party.empty()) {
                    const auto& lead=world.ents[party.front()];
                    const auto land=region(world,{(int)lead.x,(int)lead.y},0,false,party.front());
                    Vec2i scout{-1,-1};float nearest=std::numeric_limits<float>::max();
                    for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x) {
                        if(!land[world.cellIdx(x,y)] || world.map.fogAt(player,x,y)!=FOG_UNSEEN)continue;
                        float distance=distf(lead.x,lead.y,x+0.5f,y+0.5f);
                        if(distance<nearest){nearest=distance;scout={x,y};}
                    }
                    if(scout.x>=0)world.orderMove(party,scout.x+0.5f,scout.y+0.5f,true);
                }
                continue;
            }
        }
        Vec2i goal{-1,-1},patrol{-1,-1};float best=std::numeric_limits<float>::max(),farthest=0;
        for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x) {
            int cell=world.cellIdx(x,y);
            if(homeLand[cell] || !canReach(x,y) || !world.map.passable(x,y) || occupied(x,y))continue;
            float score=distf(boat.x,boat.y,x+0.5f,y+0.5f);
            if(world.valid(enemy)) {
                const auto& target=world.ents[enemy];
                float tx=target.x+(target.isBuilding?bldDef(target).w/2.0f:0),ty=target.y+(target.isBuilding?bldDef(target).h/2.0f:0);
                float distance=distf(x+0.5f,y+0.5f,tx,ty);if(distance>8)continue;
                score+=std::abs(distance-6)*64;
            } else if(world.map.fogAt(player,x,y)!=FOG_UNSEEN) {
                if(score>farthest){farthest=score;patrol={x,y};}
                continue;
            }
            if(score<best){best=score;goal={x,y};}
        }
        if(goal.x<0)goal=patrol;
        if(goal.x>=0)world.orderMove({id},goal.x+0.5f,goal.y+0.5f,false);
    }
}
