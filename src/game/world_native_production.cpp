#include "game/world.h"
#include "game/actor_contents.h"
#include <algorithm>
#include <charconv>

namespace {
bool has(const std::vector<std::string>& values,const std::string& value){return std::find(values.begin(),values.end(),value)!=values.end();}
bool overlaps(const std::vector<std::string>& a,const std::vector<std::string>& b){for(const auto& v:a)if(has(b,v))return true;return false;}
}

bool World::nativeProductionType(int selector) const {
    if(selector<NATIVE_PRODUCTION_BASE || selector>=NATIVE_PRODUCTION_BASE+512)return false;
    const int index=selector-NATIVE_PRODUCTION_BASE;
    return selector>=NATIVE_PRODUCTION_BASE && index>=0 && index<(int)nativeTypes.size()
        && nativeProductionRules.available && nativeTypes[index]->production.available && !nativeTypes[index]->kind;
}

bool World::validProductionType(bool unit,int selector) const {
    if(nativeProductionType(selector))return nativeTypes[selector-NATIVE_PRODUCTION_BASE]->isBuilding!=unit;
    return selector>=0 && selector<(unit?(int)UnitType::COUNT:(int)BldType::COUNT);
}

int World::nativeProductionCode(const std::string& name) const {
    for(int i=0;i<(int)nativeTypes.size();++i)if(nativeTypes[i]->id==name && nativeTypes[i]->production.available)return NATIVE_PRODUCTION_BASE+i;
    return -1;
}

int World::productionCategory(bool unit,int selector) const {
    if(nativeProductionType(selector))return nativeTypes[selector-NATIVE_PRODUCTION_BASE]->production.category;
    return unit?unitDef((UnitType)selector).prodCat():isDefenseBld((BldType)selector)?5:4;
}
ProdItem& World::productionItem(int player,bool unit,int selector) {
    const int category=productionCategory(unit,selector);
    return category<4?players[player].unitProd[category]:category==5?players[player].defProd:players[player].bldProd;
}
const ProdItem& World::productionItem(int player,bool unit,int selector) const {
    const int category=productionCategory(unit,selector);
    return category<4?players[player].unitProd[category]:category==5?players[player].defProd:players[player].bldProd;
}

bool World::nativeFactorySupports(const Ent& factory,int type) const {
    if(type<0 || type>=(int)nativeTypes.size() || !factory.alive || !factory.isBuilding || !factory.nativeIdentity.definition)return false;
    const auto& product=*nativeTypes[type];const auto& f=factory.nativeIdentity.definition->production;const auto& p=product.production;
    const int category=product.isBuilding?4:p.category==3?1:p.category;
    if(!f.available || f.factory!=category || (category==1 && f.naval!=(p.category==3)) || !overlaps(f.owners,p.owners))return false;
    if(!product.isBuilding)return true;
    int origin=factory.player;
    for(const auto& [key,owner]:nativeProductionRules.originalHouses)if(key==factory.nativeIdentity.object){origin=owner;break;}
    const auto& key=factory.nativeIdentity.object;
    if(key.rfind("production:",0)==0){const auto end=key.find(':',11);int owner=-1;
        if(end!=std::string::npos){const auto parsed=std::from_chars(key.data()+11,key.data()+end,owner);if(parsed.ec==std::errc{} && parsed.ptr==key.data()+end)origin=owner;}}
    return origin>=0 && origin<numPlayers && has(p.owners,productionCountry(origin));
}

bool World::nativeProductionVisible(int player,int selector) const {
    if(player<0 || player>=numPlayers || !nativeProductionType(selector))return false;
    const auto& d=*nativeTypes[selector-NATIVE_PRODUCTION_BASE];const auto& p=d.production;const auto country=productionCountry(player);
    if(usesRulesProfile() && !sourceProductionModeAllows(d))return false;
    if((players[player].stolenTech&p.requiredStolenTech)!=p.requiredStolenTech)return false;
    if(p.techLevel<0 || p.techLevel>nativeProductionRules.techLevels[player] || p.buildLimit==0
        || (!p.required.empty() && !has(p.required,country)) || has(p.forbidden,country))return false;
    for(const auto& prerequisite:p.prerequisites) {
        bool found=false;
        for(const auto& e:ents)if(e.alive && e.isBuilding && e.player==player && e.nativeIdentity.definition) {
            const auto& id=e.nativeIdentity.definition->id;
            if(id==prerequisite){found=true;break;}
            for(const auto& [group,types]:nativeProductionRules.groups)if(group==prerequisite && has(types,id)){found=true;break;}
            if(found)break;
        }
        if(!found)return false;
    }
    // A technology entry is shown only when a source factory can make it.
    for(const auto& e:ents)if(e.player==player && nativeFactorySupports(e,selector-NATIVE_PRODUCTION_BASE) && !e.selling)return true;
    return false;
}

ProductionRejectReason World::nativeProductionRejection(int player,bool unit,int selector) const {
    if(player<0 || player>=numPlayers || !nativeProductionType(selector) || !validProductionType(unit,selector))return ProductionRejectReason::InvalidType;
    const auto& d=*nativeTypes[selector-NATIVE_PRODUCTION_BASE];const auto& profile=d.production;const auto& p=players[player];
    const auto country=productionCountry(player);
    if(usesRulesProfile() && !sourceProductionModeAllows(d))return ProductionRejectReason::Unavailable;
    if((p.stolenTech&profile.requiredStolenTech)!=profile.requiredStolenTech)return ProductionRejectReason::MissingPrerequisite;
    if(profile.techLevel<0 || profile.techLevel>nativeProductionRules.techLevels[player] || profile.buildLimit<=0
        || (!profile.required.empty() && !has(profile.required,country)) || has(profile.forbidden,country))return ProductionRejectReason::Unavailable;
    bool factory=false;for(const auto& e:ents)if(e.player==player && nativeFactorySupports(e,selector-NATIVE_PRODUCTION_BASE) && !e.selling)factory=true;
    if(!factory)return ProductionRejectReason::MissingFactory;
    if(!nativeProductionVisible(player,selector))return ProductionRejectReason::MissingPrerequisite;
    if(!profile.freeUnit.empty() && nativeUnitIndex(profile.freeUnit)<0)return ProductionRejectReason::Unavailable;
    const auto& item=productionItem(player,unit,selector);
    if(!unit && item.active)return ProductionRejectReason::Busy;
    if(unit && item.active && p.unitQueue[profile.category].size()>=(size_t)nativeProductionRules.queueLimit)return ProductionRejectReason::QueueFull;
    if(profile.buildLimit<1000000) {
        int count=0;const int type=selector-NATIVE_PRODUCTION_BASE;
        visitLivePlayerContents(*this,player,[&](const auto& actor){if(actor.nativeIdentity.type==type)++count;});
        if(item.active && item.typeIdx==selector)++count;
        for(const auto& job:nativeAI.jobs)if(job.house==player && job.type==type)++count;
        if(unit)for(int queued:p.unitQueue[profile.category])if(queued==selector)++count;
        if(count>=profile.buildLimit)return ProductionRejectReason::UniqueLimit;
    }
    return ProductionRejectReason::None;
}

void World::activateNativeProduction(int player,ProdItem& item,int selector) {
    const auto& d=*nativeTypes[selector-NATIVE_PRODUCTION_BASE];item={};item.active=true;item.isUnit=!d.isBuilding;item.typeIdx=selector;
    item.totalCost=nativeProductionCost(player,selector-NATIVE_PRODUCTION_BASE);
}

void World::applyNativeProductionCommand(int player,const Cmd& command) {
    if(player<0 || player>=numPlayers)return;
    ProductionOrderResult result;result.type=command.a;result.unit=command.type==Cmd::StartUnitProd || command.type==Cmd::CancelUnitProd || command.type==Cmd::HoldUnitProd;
    result.action=command.type==Cmd::CancelUnitProd || command.type==Cmd::CancelBldProd?ProductionAction::Cancel:
        command.type==Cmd::HoldUnitProd || command.type==Cmd::HoldBldProd?(command.b?ProductionAction::Pause:ProductionAction::Resume):ProductionAction::Start;
    if(!nativeProductionType(result.type) || !validProductionType(result.unit,result.type))result.reason=ProductionRejectReason::InvalidType;
    else {
        auto& item=productionItem(player,result.unit,result.type);const int category=productionCategory(result.unit,result.type);auto& p=players[player];
        if(result.action==ProductionAction::Start && item.active && item.typeIdx==result.type && item.held){item.held=false;result.accepted=true;result.action=ProductionAction::Resume;}
        else if(result.action==ProductionAction::Start) {
            result.reason=nativeProductionRejection(player,result.unit,result.type);
            if(result.reason==ProductionRejectReason::None){result.queued=item.active;if(item.active)p.unitQueue[category].push_back(result.type);else activateNativeProduction(player,item,result.type);result.accepted=true;}
        } else if(result.action==ProductionAction::Cancel) {
            if(result.unit){auto& queue=p.unitQueue[category];for(auto it=queue.rbegin();it!=queue.rend();++it)if(*it==result.type){queue.erase(std::next(it).base());result.queued=result.accepted=true;break;}}
            if(!result.queued && item.active && item.typeIdx==result.type){result.refund=item.paid;item={};changeMoney(player,result.refund);result.accepted=true;
                if(result.unit && !p.unitQueue[category].empty()){const int next=p.unitQueue[category].front();p.unitQueue[category].pop_front();activateNativeProduction(player,item,next);}}
            if(!result.accepted)result.reason=ProductionRejectReason::NoOrder;
        } else if(!item.active || item.typeIdx!=result.type)result.reason=ProductionRejectReason::NoOrder;
        else if(item.ready)result.reason=ProductionRejectReason::Ready;
        else {item.held=command.b!=0;result.accepted=true;}
        result.queueLength=result.unit?unitQueuedCount(player,category):(item.active?1:0);
    }
    PresentationEvent event;event.kind=PresentationKind::ProductionOrder;event.tick=tick;event.sequence=presentationSequence++;event.player=player;event.production=result;
    if(presentationEvents.size()>=4096)presentationEvents.pop_front();presentationEvents.push_back(event);
}
