#include "game/game.h"
#include <algorithm>

void Game::reportNativeSelection(const std::vector<EID>& selected,const std::vector<EID>& previous,bool all) {
    // Original selection events are local single-player intents. Never add
    // local UI selection to a lockstep command frame or to voice playback.
    if(netGame || !world.nativeConditions.active)return;
    for(EID id:selected) {
        if(std::find(previous.begin(),previous.end(),id)!=previous.end() || !world.valid(id))continue;
        World::Cmd command;command.type=World::Cmd::NativeSelect;command.ids={id};
        command.a=(int)world.nativeSelectionIdentity(id);issueCmd(command);
        if(!all)break;
    }
}
