#include "game/game.h"
#include "sfx/announcer.h"
#include <algorithm>

namespace {
std::string folded(std::string text) {
    for(char& c:text)if(c>='A' && c<='Z')c+='a'-'A';return text;
}
}

void Game::preloadNativeMissionSpeech() {
    if(!world.nativeEventDefinition)return;
    const int faction=(int)world.players[localPlayer].faction;
    for(const auto& trigger:world.nativeEventDefinition->triggers)for(const auto& action:trigger.actions)
        if(action.type==21)g_announcer.preloadNative(action.args[1],faction);
}

void Game::updateNativeCampaignObjectives() {
    if(campaignMission<0 || !world.nativeEvents.active)return;
    const auto& mission=missionTable()[campaignMission];
    if(mission.nativeScene.empty())return;
    bool changed=false;
    for(size_t i=0;i<mission.objectives.size() && i<campaignObjDone.size();++i) {
        const auto& objective=mission.objectives[i];
        bool complete=objective.nativeCompleteOnVictory && world.nativeEvents.outcome==NativeMissionOutcome::Victory;
        const int trigger=objective.nativeCompleteTrigger.empty()?-1:world.nativeTriggerIndex(objective.nativeCompleteTrigger);
        if(trigger>=0)complete |= std::any_of(world.nativeEvents.triggers.begin(),world.nativeEvents.triggers.end(),
            [&](const auto& state){return state.definition==trigger && state.firings>0;});
        if(complete && !campaignObjDone[i]){campaignObjDone[i]=true;changed=true;}
    }
    if(changed)for(size_t i=0;i<mission.objectives.size();++i)if(!campaignObjDone[i]) {
        const auto& objective=mission.objectives[i];
        objectiveText=g_lang && !objective.textEn.empty()?objective.textEn:objective.text;break;
    }
}

void Game::consumeNativeEventCues() {
    if(nativeEventCueEpoch!=world.presentationEpoch) {
        nativeEventCueEpoch=world.presentationEpoch;pendingNativeEventCues.clear();nativeEventCueOverflow=0;
    }
    while(!world.nativeEventCues.empty()) {
        auto cue=std::move(world.nativeEventCues.front());world.nativeEventCues.pop_front();
        // Input lock has already been applied in the authoritative simulation.
        if(cue.action==46 || cue.action==47)continue;
        if(cue.house==localPlayer) {
            if(cue.action==21)g_announcer.requestNative(cue.key,(int)world.players[localPlayer].faction);
            if(cue.action==11 && campaignMission>=0) {
                const auto& mission=missionTable()[campaignMission];const auto key=folded(cue.key);
                for(const auto& [label,text]:mission.nativeTexts)if(label==key){message(text);break;}
            }
        }
        // Retain the exact source requests as a bounded presentation journal,
        // including unsupported effects. Film playback is outside this task.
        if(pendingNativeEventCues.size()==4096){pendingNativeEventCues.pop_front();++nativeEventCueOverflow;}
        pendingNativeEventCues.push_back(std::move(cue));
    }
}
