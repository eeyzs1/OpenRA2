#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void World::applyCmd(int player, const Cmd& c) {
    if (player < 0 || player >= numPlayers) return;
    if(nativeEvents.active && nativeEvents.inputLocked)return;
    EntityMutationScope lifetimes(*this);
    std::vector<EID> own;
    own.reserve(c.ids.size());
    if(c.identitiesBound && c.generations.size()!=c.ids.size())return;
    const auto identityValid=[&](size_t index){return !c.identitiesBound || valid(EntityRef{c.ids[index],c.generations[index]});};
    for(size_t index=0;index<c.ids.size();++index) {
        const EID id=c.ids[index];
        if(valid(id) && identityValid(index) && ents[id].player==player && !managedAircraft(id)
            && (!ents[id].nativeIdentity.definition || !ents[id].nativeIdentity.definition->nativeRocket))own.push_back(id);
    }
    const EID target=!c.identitiesBound || !commandTargetsEntity(c) || valid(EntityRef{c.a,c.targetGeneration})?c.a:INVALID_EID;
    switch (c.type) {
        case Cmd::NativeSelect:
            if(c.ids.size()==1 && identityValid(0))selectNativeActor(player,c.ids.front(),(uint32_t)c.a);
            break;
        case Cmd::Move: {
            const int layer=c.b>=0 && c.b<=2?c.b-1:-2;
            auto result = orderMove(own, c.x, c.y, c.attackMove, (c.a & 1) != 0, true,layer,MoveFormation::Directional);
            releaseNativePlayerUnits(result.acceptedUnits);
            emitMoveResult(player, own.empty()?INVALID_EID:own.front(), c.x, c.y, result);
            break;
        }
        case Cmd::Attack: {
            const auto result=orderAttack(own,target,true);releaseNativePlayerUnits(result.acceptedUnits);
            emitAttackResult(player,target,result);break;
        }
        case Cmd::Harvest:
            if(map.inBounds(c.a,c.b)) {
                auto actors=orderHarvest(own,c.a,c.b,true);
                if(!actors.empty()) {
                    PresentationEvent event;event.kind=PresentationKind::HarvestOrder;
                    event.tick=tick;event.sequence=presentationSequence++;event.player=player;event.harvestActors=std::move(actors);
                    if(presentationEvents.size()>=4096)presentationEvents.pop_front();
                    presentationEvents.push_back(std::move(event));
                }
            }
            break;
        case Cmd::ReturnHarvester: if(!c.identitiesBound || c.a==INVALID_EID || target!=INVALID_EID)orderReturnToRefinery(own,target); break;
        case Cmd::Stop: {
            std::vector<int> stopped;for(EID id:own)if(!ents[id].isBuilding && !ents[id].deploying())stopped.push_back(id);
            orderStop(own,true);releaseNativePlayerUnits(stopped);break;
        }
        case Cmd::Deploy:      emitDeployResult(player,orderDeployment(own)); break;
        case Cmd::Capture:     emitEngineerResult(player,target,EngineerIntent::Capture,orderCapture(own,target,true)); break;
        case Cmd::Repair: {
            const auto intent=c.b==0?EngineerIntent::Repair:c.b==1?EngineerIntent::RepairBridge:EngineerIntent::None;
            emitEngineerResult(player,target,intent,orderEngineer(own,target,intent,true));break;
        }
        case Cmd::Scatter:     releaseNativePlayerUnits(orderScatter(own)); break;
        case Cmd::Guard: {
            std::vector<int> guarded;for(EID id:own)if(!ents[id].isBuilding && unitDef(ents[id]).weapon.damage>0)guarded.push_back(id);
            orderGuard(own);releaseNativePlayerUnits(guarded);break;
        }
        case Cmd::Board:       emitEntryResult(player,target,EntryKind::Board,orderBoard(own,target,true)); break;
        case Cmd::Unload:      emitReleaseResult(player,own.empty()?INVALID_EID:own.front(),ReleaseKind::Unload,orderUnload(own,true)); break;
        case Cmd::Garrison:    emitEntryResult(player,target,EntryKind::Garrison,orderGarrison(own,target,true)); break;
        case Cmd::Ungarrison:  emitReleaseResult(player,own.empty()?INVALID_EID:own.front(),ReleaseKind::Ungarrison,orderUngarrison(own,true)); break;
        case Cmd::RadDeploy:   emitDeployResult(player,orderDeployment(own,true)); break;
        case Cmd::Paradrop:
            if(independentSupportEnabled) {
                const auto type=c.a==0?preferredParadrop(player):c.a>=1 && c.a<=SUPPORT_COUNT?(SupportType)(c.a-1):SupportType::COUNT;
                const auto rejection=supportRejection(player,type,c.x,c.y);
                if(rejection==SuperweaponRejectReason::None)orderSupport(player,type,c.x,c.y);
                else eva(player,TR(rejection==SuperweaponRejectReason::NotReady?S::MsgSWNotReady:
                    rejection==SuperweaponRejectReason::InvalidTarget || rejection==SuperweaponRejectReason::Blocked?S::MsgSWBlocked:S::MsgSWUnavailable));
                break;
            }
            // 优先级与 HUD/EVA 一致：伞兵 > 侦察机 > 心灵揭示（共用充能槽）
            if (hasParadropSource(player)) orderParadrop(player, c.x, c.y);
            else if (hasSpyPlaneSource(player)) orderSpyPlane(player, c.x, c.y);
            else orderPsychicReveal(player, c.x, c.y);
            break;
        case Cmd::Service:     emitServiceResult(player,target,orderService(own,target,true)); break;
        case Cmd::StartUnitProd: case Cmd::CancelUnitProd: case Cmd::StartBldProd:
        case Cmd::CancelBldProd: case Cmd::HoldUnitProd: case Cmd::HoldBldProd:
            applyProductionCommand(player,c);
            break;
        case Cmd::PlaceBuilding:
            applyPlacementCommand(player,c.a,c.x,c.y,c.b);
            break;
        case Cmd::SetRally:
            if(!own.empty()) emitRallyResult(player,orderRally(own[0],c.x,c.y,(c.a&1)!=0),c.x,c.y);
            break;
        case Cmd::SellBuilding: case Cmd::RepairBuilding: {
            PresentationEvent event;event.kind=PresentationKind::BuildingOrder;
            event.tick=tick;event.sequence=presentationSequence++;event.player=player;
            event.entity=c.ids.empty() || !identityValid(0)?INVALID_EID:c.ids.front();
            auto& result=event.building;
            result.action=c.type==Cmd::SellBuilding?BuildingAction::Sell:BuildingAction::Repair;
            if(!valid(event.entity) || !ents[event.entity].isBuilding)
                result.reason=BuildingRejectReason::InvalidTarget;
            else {
                auto& building=ents[event.entity];const auto& def=bldDef(building);
                event.x=building.x+def.w*0.5f;event.y=building.y+def.h*0.5f;event.positional=true;
                event.terrainHeight=map.at((int)building.x,(int)building.y).height;
                if(building.player!=player)result.reason=BuildingRejectReason::WrongOwner;
                else if(building.selling)result.reason=BuildingRejectReason::Selling;
                else if(result.action==BuildingAction::Sell && building.btype==BldType::ConYard)
                    result.reason=BuildingRejectReason::ConstructionYard;
                else if(result.action==BuildingAction::Repair && building.constructAnim>0)
                    result.reason=BuildingRejectReason::Constructing;
                else if(result.action==BuildingAction::Repair && building.hp>=def.hp && !building.repairing)
                    result.reason=BuildingRejectReason::FullHealth;
                else {
                    if(result.action==BuildingAction::Sell)sellBuilding(event.entity);
                    else repairBuilding(event.entity);
                    result.accepted=true;result.repairing=building.repairing;
                }
            }
            if(presentationEvents.size()>=4096)presentationEvents.pop_front();
            presentationEvents.push_back(event);
            break;
        }
        case Cmd::LaunchSW: {
            SuperweaponOrderResult result;result.type=c.a;result.reason=superweaponRejection(player,c.a,c.x,c.y);
            if(result.reason==SuperweaponRejectReason::None) {
                if(c.a==(int)SWType::ChronoShift) {
                    const bool source=std::any_of(own.begin(),own.end(),[&](EID id){return chronoSourceEligible(player,id);});
                    if(!source) result.reason=SuperweaponRejectReason::NoSource;
                    else if(!(result.accepted=launchChronoShift(player,own,c.x,c.y,&result.transferred))) result.reason=SuperweaponRejectReason::Blocked;
                } else result.accepted=launchSW(player,(SWType)c.a,c.x,c.y);
            }
            PresentationEvent event;event.kind=PresentationKind::SuperweaponOrder;event.tick=tick;event.sequence=presentationSequence++;
            event.player=player;event.x=std::isfinite(c.x)?c.x:0;event.y=std::isfinite(c.y)?c.y:0;
            event.positional=std::isfinite(c.x) && std::isfinite(c.y) && c.x>=0 && c.y>=0 && c.x<map.w && c.y<map.h;
            event.superweapon=result;if(presentationEvents.size()>=4096) presentationEvents.pop_front();presentationEvents.push_back(event);
            break;
        }
        default: break;
    }
}

