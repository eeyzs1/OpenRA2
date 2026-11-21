#include "game/world.h"
#include <algorithm>
#include <limits>

void World::rememberAttackMove(EID id,bool attack) {
    if(!valid(id))return;
    auto& e=ents[id];e.attackMove={};
    // Teleport movement has already completed synchronously.
    if(!attack || e.isBuilding || e.player<0 || e.player>=numPlayers || e.utype==UnitType::Chrono || e.utype==UnitType::ChronoCommando || e.utype==UnitType::ChronoIvan)return;
    if(distf(e.x,e.y,e.goalX,e.goalY)<.05f && e.navLayer==e.goalLayer)return;
    e.attackMove={true,e.player,e.goalX,e.goalY,e.goalLayer,0};
}

void World::completeMoveLeg(EID id,bool attack) {
    if(!valid(id))return;
    auto& e=ents[id];
    // Air movement calls this after its physical arrival test, which accepts
    // cell/tolerance arrival. Requiring an exact center here would resume the
    // same completed flight forever. A shortened ground leg still retains its
    // original destination until the actor physically reaches it.
    const bool airArrival=((usesNativeJumpjet(e) && usesNativeAirNavigation(e))
        || (nativeProductionRules.version>=8 && unitDef(e).isAir()))
        && e.goalX==e.attackMove.x && e.goalY==e.attackMove.y;
    if(e.attackMove.active && !airArrival && (distf(e.x,e.y,e.attackMove.x,e.attackMove.y)>=.05f
        || (!unitDef(e).isAir() && e.navLayer!=e.attackMove.layer))) {
        e.state=unitDef(e).isAir()?UState::Circling:UState::Idle;return;
    }
    e.attackMove={};
    if(e.wps.empty()) {
        if(unitDef(e).isAir()){e.guard=attack;e.state=UState::Circling;}
        else e.state=UState::Idle;
        return;
    }
    auto pending=std::move(e.wps);const auto waypoint=pending.front();pending.pop_front();
    const bool mode=waypoint.attackMode<0?attack:waypoint.attackMode!=0;
    const auto result=orderMove({id},waypoint.first,waypoint.second,mode,false,false,waypoint.layer);
    if(result.accepted) {
        e.wps=std::move(pending);
        if(!e.wps.empty() && e.state==UState::Idle)e.state=mode?UState::AttackMoving:UState::Moving;
        if(result.adjusted)emitMoveResult(e.player,id,waypoint.first,waypoint.second,result);
    } else {
        e.wps.clear();e.state=unitDef(e).isAir()?UState::Circling:UState::Idle;
        emitMoveResult(e.player,id,waypoint.first,waypoint.second,result);
    }
}

void World::updateAttackMoveOrders() {
    const size_t count=ents.size();if(!count)return;
    // Rotate from the saved tick so low IDs retrying every30 ticks cannot
    // monopolize all eight slots and indefinitely defer a later open route.
    const size_t first=tick%count;
    for(size_t offset=0;offset<count;++offset) {
        const EID id=(EID)((first+offset)%count);
        auto& e=ents[id];if(!e.attackMove.active)continue;
        if(!e.alive || e.isBuilding || e.player!=e.attackMove.owner || e.deployed || e.radDeployed || e.deploying()
            || e.state==UState::Boarding || e.state==UState::Servicing || e.engineerIntent!=EngineerIntent::None) {e.attackMove={};e.wps.clear();continue;}
        if(e.immobilized() || e.parasiting || e.nativeIdentity.sleeping)continue;
        const auto& ud=unitDef(e);
        if(e.utype==UnitType::RobotTank && (!hasBld(e.player,BldType::RobotControl) || players[e.player].lowPower()))continue;
        if(e.state!=UState::Idle && e.state!=UState::Circling && e.state!=UState::Landed)continue;
        if(ud.isAir() && ud.ammo>0) {
            if(e.state==UState::Landed && e.ammo<ud.ammo)continue;
            if(e.ammo<=0){e.state=UState::Returning;continue;}
        }
        const auto intent=e.attackMove;
        if(distf(e.x,e.y,intent.x,intent.y)<.05f && (ud.isAir() || e.navLayer==intent.layer)) {completeMoveLeg(id,true);continue;}
        // Air resumption does not request an A* path or consume its budget.
        if(tick<intent.retryAt || (!ud.isAir() && repathsThisTick>=8))continue;
        e.goalX=intent.x;e.goalY=intent.y;e.goalLayer=intent.layer;e.target=INVALID_EID;e.guard=false;e.clearPath();e.blockTick=0;
        if(ud.isAir()){e.state=UState::AttackMoving;e.attackMove.retryAt=0;continue;}
        std::vector<Vec2i> path;
        const bool found=repathUnit(id,(int)intent.x,(int)intent.y,path,INVALID_EID,intent.layer);
        const auto endpoint=path.empty()?Vec2i{(int)e.x,(int)e.y}:path.back();
        if(found && endpoint.x==(int)intent.x && endpoint.y==(int)intent.y) {
            // A* describes cell transitions. Incidental combat can end inside
            // the destination cell without reaching its center; an empty path
            // would otherwise alternate Idle/AttackMoving forever.
            if(path.empty() && e.navLayer==intent.layer)path.push_back(endpoint);
            e.path=std::move(path);e.pathIdx=0;e.state=UState::AttackMoving;e.attackMove.retryAt=0;
        } else {
            e.state=UState::Idle;e.attackMove=intent;
            e.attackMove.retryAt=tick>std::numeric_limits<uint64_t>::max()-LOGIC_FPS?std::numeric_limits<uint64_t>::max():tick+LOGIC_FPS;
        }
    }
}
