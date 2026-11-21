#include "game/world.h"
#include <algorithm>

void World::initializeMovementSurface(Ent& actor) {
    actor.nativeLastEnteredCell=nativeEvents.active && map.inBounds((int)actor.x,(int)actor.y)?cellIdx((int)actor.x,(int)actor.y):-1;
    actor.navLayer=actor.isBuilding || unitDef(actor).isAir()?0:(uint8_t)map.preferredLayer((int)actor.x,(int)actor.y,unitDef(actor).pathDomain());
    actor.previousNavLayer=actor.goalLayer=actor.navLayer;
}

int World::movementLayerAt(const Ent& actor,int x,int y) const {
    const int ax=(int)actor.x,ay=(int)actor.y;
    if(x==ax && y==ay)return actor.navLayer;
    if(std::abs(x-ax)<=1 && std::abs(y-ay)<=1) {
        const int next=movementStep(actor,ax,ay,actor.navLayer,x,y);
        if(next>=0)return next;
    }
    if(!actor.path.empty() && actor.path.back()==Vec2i{x,y}) {
        const int layer=routeEndLayer(actor,actor.path,actor.pathIdx);if(layer>=0)return layer;
    }
    if(x==(int)actor.goalX && y==(int)actor.goalY)return actor.goalLayer;
    return map.preferredLayer(x,y,unitDef(actor).pathDomain());
}

int World::routeEndLayer(const Ent& actor,const std::vector<Vec2i>& route,int index) const {
    int x=(int)actor.x,y=(int)actor.y,layer=actor.navLayer;
    for(int i=index;i<(int)route.size();++i) {
        const auto next=route[i];layer=movementStep(actor,x,y,layer,next.x,next.y);
        if(layer<0)return -1;x=next.x;y=next.y;
    }
    return layer;
}

int World::actorSurfaceHeight(const Ent& actor) const {
    return map.surfaceHeight((int)actor.x,(int)actor.y,actor.navLayer);
}

int World::movementStep(const Ent& actor,int ax,int ay,int layer,int bx,int by,bool corners) const {
    const auto& def=unitDef(actor);
    if(map.hasBridgeDecks() || !map.gateCells.empty())return map.stepLayer(ax,ay,layer,bx,by,def.pathDomain(),corners,def.fitsUnderBridge(),nativeGateAccess(actor));
    return passableStep(ax,ay,bx,by,def.pathDomain(),layer)?0:-1;
}

bool World::movementPassable(const Ent& actor,int x,int y,int layer,bool buildings) const {
    const auto& def=unitDef(actor);
    return map.surfacePassable(x,y,def.pathDomain(),layer,buildings,def.fitsUnderBridge());
}

bool World::interactionWithinReach(const Ent& unit,const Ent& target,float reach) const {
    float x=target.x,y=target.y;
    if(target.isBuilding){x+=bldDef(target).w/2.f;y+=bldDef(target).h/2.f;}
    const bool heightMatters=map.hasBridgeDecks() || usesNativeAirNavigation(target);
    return distf(unit.x,unit.y,x,y)<reach && (!heightMatters || unit.airborne() || target.airborne()
        || std::abs(actorSurfaceHeight(unit)-actorSurfaceHeight(target))<=MAP_MAX_CLIMB);
}

void World::rebuildNativeBridges(bool resolveLost,bool scriptedCollapse) {
    auto previous=std::move(map.bridgeCells);map.bridgeCells.clear();
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& actor=ents[id];const auto& spec=actor.nativeIdentity.definition;
        if(!actor.alive || !spec || spec->kind!=3 || !map.inBounds((int)actor.x,(int)actor.y))continue;
        const int x=(int)actor.x,y=(int)actor.y;
        if(map.native.tiles.empty() || map.native.tiles[cellIdx(x,y)].overlay!=spec->overlay)continue;
        const bool alongY=spec->id=="BRIDGE2" || spec->id=="BRIDGEB2";
        if(map.bridgeCells.empty())map.bridgeCells.resize(map.cells.size());
        const auto height=(uint8_t)(map.at(x,y).height+4);
        for(int offset=-1;offset<=1;++offset) {
            const int cx=x+(alongY?offset:0),cy=y+(alongY?0:offset);
            if(map.inBounds(cx,cy))map.bridgeCells[cellIdx(cx,cy)]={id,height,(uint8_t)alongY};
        }
    }
    if(!resolveLost)return;
    for(EID id=0;id<(EID)ents.size();++id) {
        auto& actor=ents[id];
        if(!actor.alive || actor.isBuilding || actor.navLayer!=1 || actor.airborne() || actor.magneticHeight>0
            || map.hasDeck((int)actor.x,(int)actor.y))continue;
        const int x=(int)actor.x,y=(int)actor.y;
        int fall=4;if(map.inBounds(x,y) && previous.size()==map.cells.size())fall=std::max(0,(int)previous[cellIdx(x,y)].height-(int)map.at(x,y).height);
        actor.navLayer=actor.previousNavLayer=0;unitOccSync(id);
        if(!map.surfacePassable(x,y,unitDef(actor).pathDomain(),0) || cellHardBlockedForMove(x,y,id,0))kill(id);
        else if(fall) {
            const int amount=scriptedCollapse && nativeEventDefinition?nativeEventDefinition->demolition.bridgeStrength:fall*12;
            damage(id,amount,-1,INVALID_EID,-1,scriptedCollapse);
        }
    }
}

bool World::writeBridgeState(FILE* file) const {
    const bool native=std::any_of(nativeTypes.begin(),nativeTypes.end(),[](const auto& type){return type->kind==3;});
    bool active=native || !nativeBridgesDestroyable;
    for(const auto& actor:ents) {
        if(actor.navLayer>1 || actor.goalLayer>1)return false;
        if(actor.alive && actor.navLayer && (actor.isBuilding || unitDef(actor).isAir() || unitDef(actor).pathDomain()==1))return false;
        active |= actor.navLayer || actor.goalLayer;
        for(const auto& point:actor.wps){if(point.layer < -1 || point.layer>1)return false;active |= point.layer!=-1;}
    }
    const uint8_t enabled=active;if(fwrite(&enabled,1,1,file)!=1)return false;if(!active)return true;
    const uint8_t destroyable=nativeBridgesDestroyable;if(fwrite(&destroyable,1,1,file)!=1)return false;
    const uint32_t count=(uint32_t)ents.size();if(fwrite(&count,sizeof(count),1,file)!=1)return false;
    for(const auto& actor:ents) {
        if(fwrite(&actor.navLayer,1,1,file)!=1 || fwrite(&actor.goalLayer,1,1,file)!=1)return false;
        const uint32_t size=(uint32_t)actor.wps.size();if(fwrite(&size,sizeof(size),1,file)!=1)return false;
        for(const auto& point:actor.wps)if(fwrite(&point.layer,1,1,file)!=1)return false;
    }
    return true;
}

bool World::readBridgeState(FILE* file,bool current) {
    uint8_t enabled=0;if(fread(&enabled,1,1,file)!=1 || enabled>1)return false;
    if(!enabled)return std::none_of(nativeTypes.begin(),nativeTypes.end(),[](const auto& type){return type->kind==3;});
    if(current){uint8_t destroyable=0;if(fread(&destroyable,1,1,file)!=1 || destroyable>1)return false;nativeBridgesDestroyable=destroyable!=0;}
    uint32_t count=0;
    if(fread(&count,sizeof(count),1,file)!=1 || count!=ents.size())return false;
    for(auto& actor:ents) {
        if(fread(&actor.navLayer,1,1,file)!=1 || fread(&actor.goalLayer,1,1,file)!=1 || actor.navLayer>1 || actor.goalLayer>1)return false;
        actor.previousNavLayer=actor.navLayer;uint32_t size=0;
        actor.px=actor.x;actor.py=actor.y;
        if(fread(&size,sizeof(size),1,file)!=1 || size!=actor.wps.size())return false;
        for(auto& point:actor.wps)if(fread(&point.layer,1,1,file)!=1 || point.layer < -1 || point.layer>1)return false;
        if(actor.alive && !actor.isBuilding && !actor.airborne() && actor.magneticHeight==0 && actor.navLayer==1
            && !map.hasDeck((int)actor.x,(int)actor.y))return false;
        if(actor.navLayer && (actor.isBuilding || (actor.alive && (unitDef(actor).isAir() || unitDef(actor).pathDomain()==1))))return false;
    }
    return true;
}
