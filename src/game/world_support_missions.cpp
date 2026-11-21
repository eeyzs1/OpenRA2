#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>
#include <limits>

const SupportDeliveryRules& World::supportDeliveryRules() const {
    static const SupportDeliveryRules original;
    return nativeProductionRules.version>=5?nativeProductionRules.delivery:original;
}

const NativeFlightProfile& World::fixedWingProfile(const Ent& actor) const {
    static const NativeFlightProfile support{38,512,500,true},empty;
    if(actor.nativeIdentity.definition)return actor.nativeIdentity.definition->flight;
    return actor.utype==UnitType::CargoPlane || actor.utype==UnitType::SpyPlane?support:empty;
}

bool World::supportLandingCell(int x,int y,int& layer,EID except) const {
    if(!map.inBounds(x,y))return false;
    layer=map.preferredLayer(x,y,0);
    const auto occupant=unitAtCell(x,y,layer);
    if(!map.surfacePassable(x,y,0,layer) || bldBlocked(x,y,layer) || (occupant!=INVALID_EID && occupant!=except))return false;
    // Falling actors do not occupy ground cells, but reserve their own landing
    // locations so two passengers are not deliberately sent to the same cell.
    for(EID id=0;id<(EID)ents.size();++id)if(id!=except && ents[id].alive && ents[id].supportAir==2
        && (int)ents[id].x==x && (int)ents[id].y==y)return false;
    return true;
}

bool World::launchSupportMission(int player,SupportType type,float x,float y) {
    if(!supportDeliveryEnabled || (unsigned)type>=(unsigned)SupportType::PsychicReveal)return false;
    const bool spy=type==SupportType::SpyPlane;const auto planeType=spy?UnitType::SpyPlane:UnitType::CargoPlane;
    int aircraft=-1,payload=-1;UnitType infantry=UnitType::GI;int count=0;
    if(usesRulesProfile()) {
        for(int i=0;i<(int)nativeTypes.size();++i)if(!nativeTypes[i]->isBuilding && nativeTypes[i]->unit.type==planeType)aircraft=i;
        if(aircraft<0 || !nativeTypes[aircraft]->flight.available)return false;
    }
    if(!spy) {
        const auto faction=players[player].faction;const int side=faction==Faction::Soviet?1:faction==Faction::Yuri?2:0;
        const auto& rule=supportRule(type);count=rule.count[side];
        if(usesRulesProfile()) {
            for(int i=0;i<(int)nativeTypes.size();++i)if(!nativeTypes[i]->isBuilding && nativeTypes[i]->id==rule.infantry[side])payload=i;
            if(payload<0 || !nativeTypes[payload]->unit.isInfantry())return false;infantry=nativeTypes[payload]->unit.type;
        } else if(type==SupportType::Paradrop)
            infantry=faction==Faction::Soviet?UnitType::Conscript:faction==Faction::Yuri?UnitType::Initiate:faction==Faction::China?UnitType::PLA:UnitType::GI;
        if(count<1 || count>64)return false;
    }
    if(usesRulesProfile() && nativeProductionSerial>std::numeric_limits<uint64_t>::max()-(uint64_t)count-1)return false;
    // Select the nearest map edge with a stable tie order. Ingress and egress
    // share a straight corridor through the requested ground position.
    const float distances[]={x,y,map.w-x,map.h-y};int edge=0;
    for(int i=1;i<4;++i)if(distances[i]<distances[edge])edge=i;
    float sx=x,sy=y,ex=x,ey=y;
    if(edge==0){sx=.5f;ex=map.w-.5f;}else if(edge==1){sy=.5f;ey=map.h-.5f;}
    else if(edge==2){sx=map.w-.5f;ex=.5f;}else{sy=map.h-.5f;ey=.5f;}
    const EID id=spawnUnit(player,planeType,sx,sy,aircraft);if(!valid(id))return false;
    auto& e=ents[id];e.supportAir=1;e.supportSince=tick;e.state=UState::Circling;e.target=INVALID_EID;
    e.nativeAltitude=e.previousNativeAltitude=(float)supportDeliveryRules().flightLevel+map.surfaceHeight((int)sx,(int)sy,0)*104.f;
    const auto facing=NativeFlightMath::direction(ex-sx,ey-sy);
    e.dir=e.turretDir=(((facing+4096)/8192)-1)&7;
    for(int i=0;i<count;++i) {
        Ent::GarrisonedUnit passenger;passenger.type=infantry;passenger.hp=payload>=0?nativeTypes[payload]->unit.hp:unitDef(infantry).hp;
        if(payload>=0)passenger.nativeIdentity={payload,nativeTypes[payload],"production:"+std::to_string(player)+":"+std::to_string(nativeProductionSerial++),"",false};
        e.cargo.push_back(std::move(passenger));
    }
    initializeCrewIdentities(e);
    supportMissions.push_back({entityRef(id),type,0,x,y,ex,ey,tick});
    unitOccSync(id);airOccSync(id);
    if(!spy)eva(player,TR(S::EvaParadropDrop));
    emitSoundAt(Sfx::Missile,sx,sy);return true;
}

void World::updateSupportAircraft(EID id) {
    const auto find=[&]{return std::find_if(supportMissions.begin(),supportMissions.end(),[&](const auto& m){return m.aircraft==entityRef(id);});};
    auto found=find();if(found==supportMissions.end()){consumeEntity(id);return;}
    auto mission=*found;const auto ref=mission.aircraft;auto& e=ents[id];
    if(tick==e.supportSince)return;
    const auto& rules=supportDeliveryRules();const bool spy=mission.type==SupportType::SpyPlane;
    const float distance=distf(e.x,e.y,mission.x,mission.y),radius=rules.dropRadius/256.f;
    if(mission.phase==0 && distance<=(spy?(float)rules.spyRange:radius)) {
        mission.phase=1;if(!spy && mission.passes)--mission.passes;
    }
    if(!spy && mission.phase==1 && !e.cargo.empty() && tick>=mission.nextDrop) {
        // RA2's overfly mission checks every five original frames. The dummy
        // ParaDropWeapon ROF is not the passenger release cadence.
        mission.nextDrop=tick+10;
        const int centerX=(int)e.x,centerY=(int)e.y;int lx=-1,ly=-1,layer=0;
        for(int r=0;r<=3 && lx<0;++r)for(int dy=-r;dy<=r && lx<0;++dy)for(int dx=-r;dx<=r && lx<0;++dx)
            if(std::max(std::abs(dx),std::abs(dy))==r && supportLandingCell(centerX+dx,centerY+dy,layer)) {lx=centerX+dx;ly=centerY+dy;}
        if(lx>=0) {
            const auto passenger=e.cargo.front();const auto identity=passenger.nativeIdentity;
            const EID child=spawnUnit(e.player,passenger.type,lx+.5f,ly+.5f,identity.type,identity.definition?&identity:nullptr);
            if(valid(ref) && valid(child)) {
                auto& falling=ents[child];falling.hp=passenger.hp;falling.combatActor=passenger.combatActor;falling.supportAir=2;falling.supportSince=tick;
                falling.nativeAltitude=falling.previousNativeAltitude=e.nativeAltitude;falling.state=UState::Idle;
                falling.navLayer=falling.previousNavLayer=0;falling.clearPath();falling.target=INVALID_EID;
                unitOccSync(child);airOccSync(child);e.cargo.erase(e.cargo.begin());mission.passes=5;
            }
            if(!valid(ref))return;
        }
        if(e.cargo.empty())mission.phase=2;
    }
    if(!spy && mission.phase==1 && distance>radius && !e.cargo.empty())mission.phase=mission.passes?0:2;
    const float tx=mission.phase==0?mission.x:mission.exitX,ty=mission.phase==0?mission.y:mission.exitY;
    nativeFlyToward(e,tx,ty);
    // Departure is physical flight through the map boundary, never a timeout
    // that removes an ordinary combat unit or awards undelivered passengers.
    if(e.x<0 || e.y<0 || e.x>=map.w || e.y>=map.h
        || (mission.phase!=0 && distf(e.x,e.y,mission.exitX,mission.exitY)<.15f)) {
        e.x=std::clamp(e.x,0.f,map.w-.001f);e.y=std::clamp(e.y,0.f,map.h-.001f);consumeEntity(id);checkDefeat();return;
    }
    found=find();if(found!=supportMissions.end())*found=mission;
    unitOccSync(id);airOccSync(id);
}

void World::updateSupportDescent(EID id) {
    auto& e=ents[id];if(tick==e.supportSince)return;
    // Original falling physics adds the current vertical velocity, then
    // subtracts one and clamps it to ParachuteMaxFallRate. Half steps retain
    // the original 15-Hz result within the 30-Hz World.
    e.nativeAltitude+=e.supportFallSpeed*.5f;
    if(e.supportFallHalf)e.supportFallSpeed=std::max(supportDeliveryRules().parachuteFall,e.supportFallSpeed-1);
    e.supportFallHalf^=1;
    const int x=(int)e.x,y=(int)e.y,preferred=map.preferredLayer(x,y,0);
    const float ground=map.surfaceHeight(x,y,preferred)*104.f;
    if(e.nativeAltitude>ground)return;
    int lx=-1,ly=-1,layer=0;
    for(int r=0;r<=3 && lx<0;++r)for(int dy=-r;dy<=r && lx<0;++dy)for(int dx=-r;dx<=r && lx<0;++dx)
        if(std::max(std::abs(dx),std::abs(dy))==r && supportLandingCell(x+dx,y+dy,layer,id)){lx=x+dx;ly=y+dy;}
    if(lx<0){kill(id,true);return;}
    e.x=e.px=lx+.5f;e.y=e.py=ly+.5f;e.navLayer=e.previousNavLayer=(uint8_t)layer;
    e.supportAir=e.supportFallHalf=0;e.supportFallSpeed=0;e.supportSince=0;
    e.nativeAltitude=e.previousNativeAltitude=0;e.state=UState::Idle;
    unitOccSync(id);airOccSync(id);
}

void World::retireSupportActor(EID id) {
    std::erase_if(supportMissions,[&](const auto& mission){return mission.aircraft.id==id;});
    if(id<0 || id>=(EID)ents.size())return;
    auto& e=ents[id];e.supportAir=e.supportFallHalf=0;e.supportFallSpeed=0;e.supportSince=0;
}
