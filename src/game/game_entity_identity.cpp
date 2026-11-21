#include "game/game.h"
#include <algorithm>
#include <unordered_set>

void Game::captureInteractionReferences() {
    if(interactionIdentityEpoch!=world.presentationEpoch) {
        selectionGenerations.clear();chronoGenerations.clear();selectedBuildingRef={};
        for(auto& group:groupGenerations)group.clear();
        interactionIdentityEpoch=world.presentationEpoch;
    }
    const auto capture=[&](const auto& ids,auto& generations) {
        const std::unordered_set<EID> current(ids.begin(),ids.end());
        std::erase_if(generations,[&](const auto& pair){return !current.contains(pair.first);});
        for(EID id:ids)generations.try_emplace(id,world.entityRef(id).generation);
    };
    capture(sel,selectionGenerations);capture(chronoSourceSel,chronoGenerations);
    for(int group=0;group<10;++group)capture(groups[group],groupGenerations[group]);
    if(selectedBuildingRef.id!=selBuilding)selectedBuildingRef=world.entityRef(selBuilding);
}

void Game::rememberSelection() {
    selectionGenerations.clear();
    captureInteractionReferences();
    selectedBuildingRef=world.entityRef(selBuilding);
}

void Game::pruneInteractionReferences() {
    captureInteractionReferences();
    const auto prune=[&](auto& ids,auto& generations) {
        std::erase_if(ids,[&](EID id){const auto entry=generations.find(id);
            return entry==generations.end() || !world.valid(World::EntityRef{id,entry->second})
                || world.ents[id].player!=localPlayer;});
    };
    prune(sel,selectionGenerations);prune(chronoSourceSel,chronoGenerations);
    for(int group=0;group<10;++group)prune(groups[group],groupGenerations[group]);
    if(!world.valid(selectedBuildingRef) || !world.ents[selectedBuildingRef.id].isBuilding
        || world.ents[selectedBuildingRef.id].player!=localPlayer)selBuilding=INVALID_EID;
}
