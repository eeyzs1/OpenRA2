#include "game/game.h"
#include "sfx/sound.h"
#include "sfx/announcer.h"
#include "sfx/voices.h"
#include <algorithm>

void Game::announcementOrder(const PresentationEvent& event) {
    if(event.player!=localPlayer)return;
    EvaCue cue=EvaCue::None;
    if(event.kind==PresentationKind::ProductionOrder && event.production.accepted) {
        const auto& result=event.production;
        if(result.action==ProductionAction::Pause)cue=EvaCue::OnHold;
        else if(result.action==ProductionAction::Cancel)cue=EvaCue::Canceled;
        else if(result.action==ProductionAction::Resume || !result.queued)cue=result.unit?EvaCue::Training:EvaCue::Building;
    } else if(event.kind==PresentationKind::BuildingOrder && event.building.accepted) {
        if(event.building.action==BuildingAction::Sell)cue=EvaCue::StructureSold;
        else if(event.building.repairing)cue=EvaCue::Repairing;
    } else if(event.kind==PresentationKind::PlacementOrder && !event.placement.accepted && event.placement.reason==PlacementRejectReason::Blocked)
        cue=EvaCue::CannotDeploy;
    else if(event.kind==PresentationKind::ServiceOrder && event.serviceStatus==ServiceStatus::Completed)
        cue=EvaCue::UnitRepaired;
    else if(event.kind==PresentationKind::DeployOrder && event.deploy.expanded)
        cue=EvaCue::NewConstructionOptions;
    if(cue!=EvaCue::None)g_announcer.request(cue,(int)world.players[localPlayer].faction);
}

void Game::updateAnnouncements() {
    const auto& player=world.players[localPlayer];const int side=(int)player.faction;
    const bool low=player.lowPower();
    if(!low){std::erase_if(evaLines,[](const Subtitle& line){return line.powerWarning;});g_announcer.cancel(EvaCue::LowPower);}
    if(low && !wasLowPower) {
        if(!g_announcer.request(EvaCue::LowPower,side))g_sfx.play(Sfx::Alarm,0.8f);
        if(msgTimer>0 && !msg.empty()) {
            const std::string warning=TR(S::MsgLowPower);
            std::erase_if(evaLines,[](const Subtitle& line){return line.powerWarning;});
            if(evaLines.size()>=4)evaLines.pop_back();evaLines.push_front({warning,true});
        } else message(TR(S::MsgLowPower));
    }
    wasLowPower=low;
    auto unpaid=[](const ProdItem& order){return order.active && !order.held && !order.ready && order.paid<order.totalCost;};
    bool waiting=player.money==0 && !low && (unpaid(player.bldProd) || unpaid(player.defProd));
    for(const auto& order:player.unitProd)waiting |= player.money==0 && !low && unpaid(order);
    if(waiting && !wasInsufficientFunds) {
        g_announcer.request(EvaCue::InsufficientFunds,side);
        if(evaLines.size()<4)evaLines.push_back({TR(S::EvaInsufficientFunds),false});
    }
    if(!waiting)g_announcer.cancel(EvaCue::InsufficientFunds);wasInsufficientFunds=waiting;
    while(!world.evaQueue.empty()) {
        const auto event=world.evaQueue.front();world.evaQueue.pop_front();if(event.player!=localPlayer)continue;
        const bool spoken=g_announcer.request(event.cue,side);
        if(evaLines.size()<4) {
            evaLines.push_back({event.text,false});if(!spoken)g_sfx.play(Sfx::Eva,0.7f);
        }
    }
    if(msgTimer<=0 && !evaLines.empty()) {
        msg=evaLines.front().powerWarning?TR(S::MsgLowPower):evaLines.front().text;
        msgTimer=2.6f;evaLines.pop_front();
    }
}

void Game::updateAudioPresentation() {
    const bool inMatch=phase==Phase::InGame || (phase==Phase::Settings && settingsFromGame);
    if(phase==Phase::MainMenu && audioPhase!=Phase::MainMenu) {
        g_announcer.reset();g_unitVoices.reset();
        if(audioPhase==Phase::InGame)g_announcer.request(EvaCue::BattleTerminated,(int)world.players[localPlayer].faction);
    }
    if(inMatch && gameOver && !wasAudioGameOver) {
        g_announcer.reset();g_unitVoices.reset();
        const bool mission=campaignMission>=0 || world.nativeEvents.active;
        const auto cue=mission?(victory?EvaCue::MissionAccomplished:EvaCue::MissionFailed):(victory?EvaCue::Victory:EvaCue::Defeat);
        g_announcer.request(cue,(int)world.players[localPlayer].faction);
    }
    if(inMatch && targetingSW!=SWType::COUNT && audioTargeting==SWType::COUNT)
        g_announcer.request(EvaCue::SelectTarget,(int)world.players[localPlayer].faction);
    audioTargeting=targetingSW;wasAudioGameOver=inMatch && gameOver;audioPhase=phase;
    g_sfx.setMusicScene(inMatch?(gameOver?MusicScene::Score:MusicScene::Battle):MusicScene::Menu);
    g_announcer.update();g_sfx.updateBgm();
}
