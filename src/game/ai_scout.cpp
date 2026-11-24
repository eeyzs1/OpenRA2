#include "game/ai.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <limits>
#include <tuple>

bool SkirmishAI::buildingVisible(const World& world,const World::Ent& building) const {
    if(player<0 || player>=world.numPlayers || !building.alive || !building.isBuilding)return false;
    const auto& definition=bldDef(building);
    for(int y=0;y<definition.h;++y)for(int x=0;x<definition.w;++x)
        if(world.map.fogAt(player,(int)building.x+x,(int)building.y+y)==FOG_VISIBLE)return true;
    return false;
}

void SkirmishAI::observeBattlefield(const World& world) {
    if(scoutMapW!=world.map.w || scoutMapH!=world.map.h || scoutObserved.size()!=world.map.cells.size()) {
        scoutMapW=world.map.w;scoutMapH=world.map.h;
        scoutObserved.assign(world.map.cells.size(),0);knownBuildings.clear();
    }
    for(size_t cell=0;cell<scoutObserved.size();++cell)
        if(world.map.fog[player][cell]==FOG_VISIBLE)scoutObserved[cell]=world.tick+1;
    auto visible=[&](int x,int y,BldType type) {
        const auto& def=bldDef(type);
        for(int dy=0;dy<def.h;++dy)for(int dx=0;dx<def.w;++dx)
            if(world.map.fogAt(player,x+dx,y+dy)==FOG_VISIBLE)return true;
        return false;
    };
    std::vector<KnownBuilding> observed;
    for(const auto& ent:world.ents) {
        if(!ent.alive || !ent.isBuilding || !world.isEnemy(player,ent.player) || !buildingVisible(world,ent))continue;
        observed.push_back({(int)ent.x,(int)ent.y,ent.player,ent.btype,world.tick});
    }
    std::erase_if(knownBuildings,[&](const KnownBuilding& old) {
        if(!world.isEnemy(player,old.owner))return true;
        if(!visible(old.x,old.y,old.type))return false;
        return std::none_of(observed.begin(),observed.end(),[&](const KnownBuilding& now) {
            return old.x==now.x && old.y==now.y && old.type==now.type && old.owner==now.owner;
        });
    });
    for(const auto& now:observed) {
        auto old=std::find_if(knownBuildings.begin(),knownBuildings.end(),[&](const KnownBuilding& entry) {
            return entry.x==now.x && entry.y==now.y;
        });
        if(old==knownBuildings.end())knownBuildings.push_back(now);else *old=now;
    }
    std::sort(knownBuildings.begin(),knownBuildings.end(),[](const KnownBuilding& a,const KnownBuilding& b) {
        return std::tie(a.y,a.x,a.owner,a.type)<std::tie(b.y,b.x,b.owner,b.type);
    });
}

Vec2i SkirmishAI::scoutDestination(const World& world,const World::Ent& unit,const std::vector<Vec2i>& claimed) const {
    const int sx=(int)unit.x,sy=(int)unit.y;
    if(!world.map.inBounds(sx,sy))return {-1,-1};
    const auto& def=unitDef(unit);
    const bool air=unit.airborne() || def.isAir();
    // Re-shrouding can keep even the scout's own cell unseen. Choose a real
    // reconnaissance leg beyond its current sensor neighborhood, rather than
    // repeatedly issuing the same already-reached point. This uses own position
    // and remembered coordinates only; hidden enemy state is never inspected.
    const int leg=air && world.nativeSceneVersion>=36?std::max(1,def.sight-1):0;
    const float minimumDistance=(float)(leg*leg);
    std::vector<uint8_t> reachable;
    std::vector<Vec2i> cells;
    auto surveyLand=[&](std::vector<Vec2i>& frontier,int domain,bool checkBuildings) {
        auto open=[&](int x,int y){return world.passableFor(x,y,domain) && (!checkBuildings || !world.bldBlocked(x,y));};
        constexpr Vec2i directions[]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
        for(size_t head=0;head<frontier.size();++head) {
            const auto from=frontier[head];
            for(auto step:directions) {
                const int x=from.x+step.x,y=from.y+step.y;
                if(!open(x,y) || !world.map.climbOk(from.x,from.y,x,y,domain)
                    || (step.x && step.y && (!open(x,from.y) || !open(from.x,y)
                        || !world.map.climbOk(from.x,from.y,x,from.y,domain)
                        || !world.map.climbOk(from.x,from.y,from.x,y,domain))))continue;
                auto& visited=reachable[world.cellIdx(x,y)];if(visited)continue;
                visited=1;frontier.push_back({x,y});
            }
        }
    };
    if(air) {
        cells.reserve(world.map.cells.size());
        for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x)
            if(world.map.inBounds(x,y))cells.push_back({x,y});
    } else {
        reachable.assign(world.map.cells.size(),0);
        cells.push_back({sx,sy});reachable[world.cellIdx(sx,sy)]=1;
        surveyLand(cells,def.pathDomain(),true);
    }
    auto distance=[&](int x,int y) {
        const float dx=x+0.5f-unit.x,dy=y+0.5f-unit.y;return dx*dx+dy*dy;
    };
    Vec2i remembered{-1,-1};float closest=std::numeric_limits<float>::max();
    for(const auto& building:knownBuildings) {
        if(!world.isEnemy(player,building.owner))continue;
        const auto& target=bldDef(building.type);
        const int cx=building.x+target.w/2,cy=building.y+target.h/2;
        if(world.map.fogAt(player,cx,cy)==FOG_VISIBLE)continue;
        if(air) {
            const float d=distance(cx,cy);
            if(d>=minimumDistance && d<closest){closest=d;remembered={cx,cy};}
        } else {
            const int radius=std::max(1,def.sight-1);
            for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
                const int x=cx+dx,y=cy+dy;
                if(dx*dx+dy*dy>radius*radius || !world.map.inBounds(x,y)
                    || !reachable[world.cellIdx(x,y)])continue;
                const float d=distance(x,y);
                if(d<1 || d>=closest)continue;
                closest=d;remembered={x,y};
            }
        }
    }
    if(remembered.x>=0)return remembered;

    bool coverIsolatedLand=false;
    constexpr uint64_t revisitDelay=60*LOGIC_FPS;
    if(air && def.weapon.antiGround && world.nativeTravelEnabled() && world.tick>=revisitDelay) {
        reachable.assign(world.map.cells.size(),0);
        std::vector<Vec2i> frontier;
        for(const auto& ally:world.ents) {
            if(!ally.alive || ally.player!=player || ally.isBuilding || ally.airborne())continue;
            const auto& type=unitDef(ally);
            if(type.pathDomain()!=0 || !type.weapon.antiGround || type.weapon.damage<=0 || type.canHarvet())continue;
            const int x=(int)ally.x,y=(int)ally.y;
            if(!world.map.passable(x,y) || reachable[world.cellIdx(x,y)])continue;
            reachable[world.cellIdx(x,y)]=1;
            frontier.push_back({x,y});
        }
        coverIsolatedLand=!frontier.empty();
        // The air survey distinguishes cliffs and disconnected land using
        // terrain. Hidden enemy structures cannot affect this classification.
        if(coverIsolatedLand)surveyLand(frontier,0,false);
    }

    // Large surveys first avoid regions already assigned to our other scouts.
    // Prefer unexplored territory there, then the oldest observed area.
    Vec2i best{-1,-1};int bestRank=6;
    uint64_t oldest=std::numeric_limits<uint64_t>::max();closest=std::numeric_limits<float>::max();
    for(auto cell:cells) {
        const auto fog=world.map.fogAt(player,cell.x,cell.y);
        if(fog==FOG_VISIBLE)continue;
        const int radius=std::max(1,def.sight-1);
        const bool reserved=std::any_of(claimed.begin(),claimed.end(),[&](Vec2i goal){
            const int dx=goal.x-cell.x,dy=goal.y-cell.y;return dx*dx+dy*dy<radius*radius;});
        const size_t index=(size_t)world.cellIdx(cell.x,cell.y);
        uint64_t stamp=fog==FOG_UNSEEN?0:index<scoutObserved.size()?scoutObserved[index]:0;
        const bool retainAge=world.nativeRulesBaseVersion>=nativeIsolatedSurveyAgeSceneVersion;
        const bool isolated=coverIsolatedLand && (fog==FOG_SEEN || (retainAge && fog==FOG_UNSEEN)) && !reachable[index]
            && world.map.passable(cell.x,cell.y) && stamp<=world.tick-revisitDelay;
        const int rank=(reserved?3:0)+(isolated?0:fog==FOG_UNSEEN?1:2);
        // Never-observed isolated land must not wait behind repeated visits
        // to nearer stale land. Preserve age when comparing both classes.
        if(isolated && !retainAge)stamp=0;
        const float d=distance(cell.x,cell.y);if(d<minimumDistance)continue;
        if(rank<bestRank || (rank==bestRank && (stamp<oldest || (stamp==oldest && d<closest)))) {
            bestRank=rank;oldest=stamp;closest=d;best=cell;
        }
    }
    return best;
}
