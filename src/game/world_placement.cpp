#include "game/world.h"
#include <cmath>

void World::applyPlacementCommand(int player,int type,float x,float y,int request) {
    if(player<0 || player>=numPlayers) return;
    if(usesRulesProfile())type=productionSelector(player,false,type);
    PresentationEvent event;
    event.kind=PresentationKind::PlacementOrder;event.tick=tick;
    event.player=player;event.placement.type=type;event.placement.request=request;
    auto& result=event.placement;
    const bool native=nativeProductionType(type) && validProductionType(false,type);
    if(!native && (type<0 || type>=(int)BldType::COUNT)) result.reason=PlacementRejectReason::InvalidType;
    else if(!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=map.w || y>=map.h)
        result.reason=PlacementRejectReason::InvalidLocation;
    else {
        const BldType building=(BldType)type;
        const auto& def=native?nativeTypes[type-NATIVE_PRODUCTION_BASE]->building:bldDef(building);
        event.x=std::floor(x)+def.w/2.f;event.y=std::floor(y)+def.h/2.f;
        event.positional=event.x>=0 && event.y>=0 && event.x<map.w && event.y<map.h;
        event.visualTarget=VisualTarget::Building;
        const auto& prod=native?productionItem(player,false,type):bldQueueFor(player,building);
        if(!prod.active || !prod.ready || prod.typeIdx!=type) result.reason=PlacementRejectReason::NotReady;
        else if(!(native?placeNativeBuilding(player,type,(int)x,(int)y):placeBuilding(player,building,(int)x,(int)y))) result.reason=PlacementRejectReason::Blocked;
        else { result.accepted=true;event.entity=bldAt((int)x,(int)y); }
    }
    // placeBuilding may emit sounds first; sequence follows actual stream order.
    event.sequence=presentationSequence++;
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}
