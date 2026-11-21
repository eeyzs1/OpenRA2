#include "game/world.h"
#include <cmath>

RallyOrderResult World::orderRally(EID factory,float x,float y,bool clear) {
    RallyOrderResult result;result.factory=factory;
    if(!valid(factory) || !ents[factory].isBuilding) result.reason=RallyRejectReason::InvalidTarget;
    else if(!isRallyBuilding(ents[factory].btype)) result.reason=RallyRejectReason::Unsupported;
    else if(ents[factory].selling) result.reason=RallyRejectReason::Selling;
    else if(!clear && (!std::isfinite(x) || !std::isfinite(y) || x<0 || y<0 || x>=map.w || y>=map.h))
        result.reason=RallyRejectReason::InvalidTarget;
    else {
        setRally(factory,clear?-1:(int)x,clear?-1:(int)y);
        result.accepted=true;result.status=clear?RallyStatus::Cleared:RallyStatus::Set;
    }
    return result;
}

void World::emitRallyResult(int player,const RallyOrderResult& result,float x,float y) {
    PresentationEvent event;event.kind=PresentationKind::RallyOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.rally=result;event.entity=result.unit>=0?result.unit:result.factory;
    if(result.accepted && result.status!=RallyStatus::Cleared && std::isfinite(x) && std::isfinite(y)
        && x>=0 && y>=0 && x<map.w && y<map.h) {
        event.x=(int)x+0.5f;event.y=(int)y+0.5f;event.positional=true;
    } else if(valid(event.entity)) {
        const auto& unit=ents[event.entity];event.x=unit.x;event.y=unit.y;event.positional=true;
        if(unit.isBuilding) { event.x+=bldDef(unit).w/2.0f;event.y+=bldDef(unit).h/2.0f; }
        else if(unit.airborne()) event.visualTarget=VisualTarget::Aircraft;
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(std::move(event));
}

void World::applyFactoryRally(EID unit,EID factory,int x,int y) {
    if(!valid(unit) || x<0 || y<0) return;
    const auto move=orderMove({unit},x+0.5f,y+0.5f,false);
    if(move.rejected || move.adjusted) {
        RallyOrderResult result;result.factory=factory;result.unit=unit;
        result.accepted=!move.rejected;result.status=move.rejected?RallyStatus::Unreachable:RallyStatus::Adjusted;
        if(move.adjusted && !ents[unit].path.empty()) {
            const auto point=ents[unit].path.back();x=point.x;y=point.y;
        }
        emitRallyResult(ents[unit].player,result,(float)x,(float)y);
    }
}
