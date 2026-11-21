#include "game/game.h"
#include "sfx/voices.h"
#include <algorithm>

void Game::voiceSelection(const std::vector<EID>& ids) {
    std::vector<OrderActor> actors;
    for(EID id:ids)if(world.valid(id)) {
        const auto& unit=world.ents[id];
        if(!unit.isBuilding && unit.player==localPlayer)
            addOrderActor(actors,(int)unit.utype,(int)world.players[unit.player].faction);
    }
    g_unitVoices.play(actors,UnitVoiceAction::Select);
}

void Game::voiceOrder(const PresentationEvent& event) {
    if(event.player!=localPlayer)return;
    if(event.kind==PresentationKind::MoveOrder && (event.move.accepted || event.move.queued))
        g_unitVoices.play(event.move.actors,event.move.attackMove?UnitVoiceAction::Attack:UnitVoiceAction::Move);
    else if(event.kind==PresentationKind::AttackOrder && event.attack.accepted)
        g_unitVoices.play(event.attack.actors,UnitVoiceAction::Attack);
    else if(event.kind==PresentationKind::HarvestOrder) {
        const bool dedicated=std::any_of(event.harvestActors.begin(),event.harvestActors.end(),[](const OrderActor& actor){
            return g_unitVoices.available(actor.unitType,actor.faction,UnitVoiceAction::Harvest);
        });
        g_unitVoices.play(event.harvestActors,dedicated?UnitVoiceAction::Harvest:UnitVoiceAction::Move);
    }
}
