#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include "rlgl.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

void Game::consumePresentationEvents() {
    while (!world.presentationEvents.empty()) {
        const auto ev = world.presentationEvents.front();
        world.presentationEvents.pop_front();
        if (ev.kind == PresentationKind::Sound) {
            if (ev.positional) g_sfx.playAt(ev.sound, ev.x, ev.y);
            else g_sfx.play(ev.sound, ev.volume);
            continue;
        }
        if (ev.kind==PresentationKind::Muzzle || ev.kind==PresentationKind::Impact) {
            if(ev.player!=localPlayer && world.map.fogAt(localPlayer,(int)ev.x,(int)ev.y)!=FOG_VISIBLE) continue;
            if(combatCues.size()>=128) combatCues.pop_front();
            combatCues.push_back({ev,0,ev.kind==PresentationKind::Muzzle?0.13f:0.2f});
            continue;
        }
        if (ev.player != localPlayer) continue;
        if(ev.kind==PresentationKind::BuildingOrder) {
            const auto& result=ev.building;
            if(result.accepted && result.action==BuildingAction::Sell && selBuilding==ev.entity)
                selBuilding=INVALID_EID;
            S text=result.action==BuildingAction::Sell?S::MsgSold:
                result.repairing?S::MsgRepairStarted:S::MsgRepairStopped;
            if(!result.accepted)switch(result.reason) {
                case BuildingRejectReason::WrongOwner:text=S::MsgBuildingOwner;break;
                case BuildingRejectReason::Selling:text=S::MsgBuildingSelling;break;
                case BuildingRejectReason::ConstructionYard:text=S::MsgConYardNoSell;break;
                case BuildingRejectReason::FullHealth:text=S::MsgNoRepair;break;
                case BuildingRejectReason::Constructing:text=S::MsgBuildingConstructing;break;
                default:text=S::MsgBuildingInvalid;break;
            }
            message(TR(text));
            attackFeedback=false;moveFeedbackPartial=false;moveFeedbackRejected=!result.accepted;
            feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackTimer=ev.positional?0.9f:0;
            continue;
        }
        if(ev.kind==PresentationKind::SuperweaponOrder) {
            message(superweaponFeedbackText(ev.superweapon));
            attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;moveFeedbackPartial=false;moveFeedbackRejected=!ev.superweapon.accepted;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackTimer=ev.positional?0.9f:0;
            continue;
        }
        if(ev.kind==PresentationKind::ProductionOrder) {
            message(productionFeedbackText(ev.production));continue;
        }
        if(ev.kind==PresentationKind::PlacementOrder) {
            const auto& result=ev.placement;
            if(placing && placementPending && placementPending==result.request && (int)placementType==result.type) {
                placementPending=0;
                if(result.accepted || result.reason==PlacementRejectReason::NotReady ||
                    result.reason==PlacementRejectReason::InvalidType) clearPlacement();
            }
            attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;moveFeedbackPartial=false;
            moveFeedbackRejected=!result.accepted;moveFeedbackTarget={ev.x,ev.y};
            moveFeedbackTimer=ev.positional?0.9f:0;
            message(TR(result.accepted?S::MsgBuildingPlaced:
                result.reason==PlacementRejectReason::NotReady || result.reason==PlacementRejectReason::InvalidType
                    ?S::MsgPlacementNotReady:S::MsgCannotPlace));
            continue;
        }
        if(ev.kind==PresentationKind::RallyOrder) {
            const auto& result=ev.rally;
            if(result.status==RallyStatus::Adjusted && selBuilding!=result.factory) continue;
            attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackRejected=!result.accepted;moveFeedbackPartial=false;
            moveFeedbackTimer=ev.positional?0.9f:0;
            S text=result.status==RallyStatus::Cleared?S::MsgRallyCleared:result.status==RallyStatus::Adjusted?S::MsgRallyAdjusted:
                result.status==RallyStatus::Unreachable?S::MsgRallyUnreachable:S::MsgRallySet;
            if(result.reason==RallyRejectReason::Unsupported) text=S::MsgRallyUnsupported;
            else if(result.reason==RallyRejectReason::InvalidTarget) text=S::MsgRallyInvalid;
            else if(result.reason==RallyRejectReason::Selling) text=S::MsgRallySelling;
            message(TR(text));continue;
        }
        if(ev.kind==PresentationKind::DeployOrder) {
            const auto& result=ev.deploy;
            for(const auto& change:result.changes) {
                const bool selected=selBuilding==change.source || std::find(sel.begin(),sel.end(),change.source)!=sel.end();
                if(!selected) continue;
                std::erase(sel,change.source);if(selBuilding==change.source) selBuilding=INVALID_EID;
                if(!world.valid(change.replacement) || world.ents[change.replacement].player!=localPlayer) continue;
                const auto& replacement=world.ents[change.replacement];
                if(change.building && replacement.isBuilding && replacement.btype==BldType::ConYard) {
                    if(sel.empty()) selBuilding=change.replacement;
                } else if(!change.building && !replacement.isBuilding && replacement.utype==UnitType::MCV) {
                    if(std::find(sel.begin(),sel.end(),change.replacement)==sel.end()) sel.push_back(change.replacement);
                }
            }
            attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackRejected=result.rejected && !result.accepted;
            moveFeedbackPartial=result.rejected && result.accepted;moveFeedbackTimer=ev.positional?0.9f:0;
            S text=S::MsgDeployInvalid;
            switch(result.reason) {
                case DeployRejectReason::Unsupported:text=S::MsgDeployUnsupported;break;
                case DeployRejectReason::Immobilized:text=S::MsgImmobilized;break;
                case DeployRejectReason::Busy:text=S::MsgDeployBusy;break;
                case DeployRejectReason::Blocked:text=S::MsgDeployBlocked;break;
                case DeployRejectReason::Controlled:text=S::MsgDeployControlled;break;
                case DeployRejectReason::RepackingDisabled:text=S::MsgRepackingDisabled;break;
                default:break;
            }
            if(moveFeedbackPartial) message(TextFormat(TR(S::MsgDeployPartial),result.accepted,result.rejected,TR(text)));
            else if(moveFeedbackRejected) message(TR(text));
            else {
                text=result.deploying==result.accepted?S::MsgDeployStarting:result.undeploying==result.accepted?S::MsgUndeployStarting:
                    result.expanded==result.accepted?S::MsgExpandDone:result.packed==result.accepted?S::MsgPackDone:
                    result.deployed==result.accepted?S::MsgDeployDone:result.undeployed==result.accepted?S::MsgUndeployDone:S::MsgDeployMixed;
                message(TextFormat(TR(text),result.accepted));
            }
            continue;
        }
        if(ev.kind==PresentationKind::ServiceOrder) {
            const auto& result=ev.service;attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackRejected=result.rejected && !result.accepted;
            moveFeedbackPartial=result.rejected && result.accepted;moveFeedbackTimer=ev.positional?0.9f:0;
            S text=S::MsgServiceAccepted;
            if(ev.serviceStatus==ServiceStatus::Completed) text=S::MsgServiceDone;
            else if(ev.serviceStatus==ServiceStatus::WaitingMoney) text=S::MsgServiceMoney;
            else if(ev.serviceStatus==ServiceStatus::WaitingPower) text=S::MsgServicePower;
            else if(ev.serviceStatus==ServiceStatus::Working) text=S::MsgServiceWorking;
            if(result.rejected) switch(result.reason) {
                case ServiceRejectReason::Unsupported:text=S::MsgServiceUnsupported;break;
                case ServiceRejectReason::WrongOwner:text=S::MsgServiceOwner;break;
                case ServiceRejectReason::Incompatible:text=S::MsgServiceIncompatible;break;
                case ServiceRejectReason::FullHealth:text=S::MsgServiceFull;break;
                case ServiceRejectReason::Deployed:text=S::MsgServiceDeployed;break;
                case ServiceRejectReason::Unreachable:text=S::MsgServiceUnreachable;break;
                default:text=S::MsgServiceInvalid;break;
            }
            if(moveFeedbackPartial) message(TextFormat(TR(S::MsgServicePartial),result.accepted,result.rejected,TR(text)));
            else if(ev.serviceStatus==ServiceStatus::Accepted && !result.rejected) message(TextFormat(TR(text),result.accepted));
            else message(TR(text));
            continue;
        }
        if(ev.kind==PresentationKind::ReleaseOrder) {
            const auto& result=ev.release;attackFeedback=false;
            feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;moveFeedbackTarget={ev.x,ev.y};
            moveFeedbackPartial=result.released>0 && result.rejected>0;moveFeedbackRejected=!result.released && result.rejected>0;
            moveFeedbackTimer=ev.positional?0.9f:0;
            S reason=result.reason==ReleaseRejectReason::Empty?S::MsgReleaseEmpty:
                result.reason==ReleaseRejectReason::Unsupported?S::MsgReleaseUnsupported:S::MsgReleaseInvalid;
            if(result.remaining>0 && result.released) message(TextFormat(TR(S::MsgReleasePartial),result.released,result.remaining));
            else if(result.remaining>0) message(TextFormat(TR(S::MsgReleaseBlocked),result.remaining));
            else if(moveFeedbackPartial) message(TextFormat(TR(S::MsgReleaseSomeTargets),result.released,result.rejected,TR(reason)));
            else if(moveFeedbackRejected) message(TR(reason));
            else message(TextFormat(TR(ev.releaseKind==ReleaseKind::Unload?S::MsgUnloadDone:S::MsgUngarrisonDone),result.released));
            continue;
        }
        if(ev.kind==PresentationKind::EntryOrder) {
            const auto& result=ev.entry;attackFeedback=false;
            feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackRejected=result.rejected && !result.accepted;
            moveFeedbackPartial=result.rejected && result.accepted;moveFeedbackTimer=ev.positional?0.9f:0;
            S text=ev.entryKind==EntryKind::Board?S::MsgBoardAccepted:S::MsgGarrisonAccepted;
            if(result.rejected) switch(result.reason) {
                case EntryRejectReason::Unsupported:text=S::MsgEntryUnsupported;break;
                case EntryRejectReason::WrongOwner:text=S::MsgEntryOwner;break;
                case EntryRejectReason::Incompatible:text=S::MsgEntryIncompatible;break;
                case EntryRejectReason::Full:text=S::MsgEntryFull;break;
                case EntryRejectReason::Deployed:text=S::MsgEntryDeployed;break;
                case EntryRejectReason::Unreachable:text=S::MsgEntryUnreachable;break;
                default:text=S::MsgEntryInvalid;break;
            }
            if(moveFeedbackPartial) message(TextFormat(TR(S::MsgEntryPartial),result.rejected,TR(text)));
            else message(TR(text));
            continue;
        }
        if(ev.kind==PresentationKind::EngineerOrder) {
            const auto& result=ev.engineer;
            attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;moveFeedbackTarget={ev.x,ev.y};
            moveFeedbackRejected=result.rejected && !result.accepted;moveFeedbackPartial=result.rejected && result.accepted;
            moveFeedbackTimer=ev.positional?0.9f:0;
            S text=ev.engineerIntent==EngineerIntent::Capture?S::MsgCaptureAccepted:S::MsgRepairAccepted;
            if(result.rejected) switch(result.reason) {
                case EngineerRejectReason::NoEngineer:text=S::MsgEngineerRequired;break;
                case EngineerRejectReason::CaptureOwner:text=S::MsgCaptureOwner;break;
                case EngineerRejectReason::RepairOwner:text=S::MsgRepairOwner;break;
                case EngineerRejectReason::NotCapturable:text=S::MsgNotCapturable;break;
                case EngineerRejectReason::FullHealth:text=S::MsgRepairFullHealth;break;
                case EngineerRejectReason::Unreachable:text=S::MsgEngineerUnreachable;break;
                default:text=S::MsgEngineerInvalid;break;
            }
            if(moveFeedbackPartial) message(TextFormat(TR(S::MsgEngineerPartial),result.rejected,TR(text)));
            else message(TR(text));
            continue;
        }
        if(ev.kind==PresentationKind::AttackOrder) {
            const auto& result=ev.attack;
            attackFeedback=true;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
            moveFeedbackTarget={ev.x,ev.y};moveFeedbackRejected=result.rejected && !result.accepted;
            moveFeedbackPartial=result.rejected && result.accepted;
            moveFeedbackTimer=ev.positional?0.9f:0;
            S text=S::MsgAttackAccepted;
            if(result.rejected) switch(result.reason) {
                case AttackRejectReason::InvalidTarget:text=S::MsgAttackInvalid;break;
                case AttackRejectReason::NoWeapon:text=S::MsgAttackNoWeapon;break;
                case AttackRejectReason::NoAntiAir:text=S::MsgAttackNoAA;break;
                case AttackRejectReason::NoAntiGround:text=S::MsgAttackNoGround;break;
                case AttackRejectReason::NavalOnly:text=S::MsgAttackNaval;break;
                case AttackRejectReason::Ineffective:text=S::MsgAttackIneffective;break;
                case AttackRejectReason::Deployed:text=S::MsgAttackDeployed;break;
                case AttackRejectReason::Busy:text=S::MsgDeployBusy;break;
                case AttackRejectReason::Unreachable:text=S::MsgAttackUnreachable;break;
                default:text=S::MsgAttackUnsupported;break;
            }
            if(moveFeedbackPartial) message(TextFormat(TR(S::MsgAttackPartial),result.rejected,TR(text)));
            else message(TR(text));
            continue;
        }
        attackFeedback=false;feedbackLift=eventRenderLift(ev);feedbackTerrainHeight=ev.terrainHeight;
        const auto& result = ev.move;
        const bool any = result.accepted || result.queued;
        moveFeedbackRejected = result.rejected && !any;
        moveFeedbackPartial = result.rejected && any;
        moveFeedbackTarget = {std::floor(ev.x) + 0.5f, std::floor(ev.y) + 0.5f};
        moveFeedbackTimer = std::isfinite(ev.x) && std::isfinite(ev.y)
            && ev.x >= 0 && ev.y >= 0 && ev.x < world.map.w && ev.y < world.map.h ? 0.9f : 0;
        if (moveFeedbackPartial) message(TextFormat(TR(S::MsgMovePartial), result.rejected));
        else if (moveFeedbackRejected) {
            message(TR(result.reason == MoveRejectReason::Busy ? S::MsgDeployBusy
                : result.reason == MoveRejectReason::Deployed ? S::MsgMoveDeploy
                : result.reason == MoveRejectReason::Immobilized ? S::MsgImmobilized
                : result.reason == MoveRejectReason::InvalidTarget ? S::MsgMoveInvalid : S::MsgMoveUnreachable));
        } else if (result.queued) message(TR(S::MsgMoveQueued));
        else if (result.adjusted) message(TR(S::MsgMoveAdjusted));
    }
}

void Game::resetNotifications() {
    msg.clear();msgTimer=0;evaLines.clear();wasLowPower=false;
}

void Game::advancePresentation(float seconds) {
    moveFeedbackTimer=std::max(0.0f,moveFeedbackTimer-std::max(0.0f,seconds));
    for(auto& cue:combatCues) cue.age+=std::max(0.0f,seconds);
    std::erase_if(combatCues,[](const CombatCue& cue) { return cue.age>=cue.duration; });
}

void Game::logic() {
    if (netGame) {
        netAdvance(); // lockstep：远端命令就绪才 world.update()（本帧可能不推进）
    } else {
        world.update();
        for (auto& ai : ais) ai.update(world);
    }
    // 脚本引擎：每逻辑帧触发 OnTick（联机模式下跳过，避免非确定性）
    if (!netGame && g_script.boundTo(&world)) g_script.onTick(world.tick);
    syncFogKnowledge();

    // 听者位置 = 视野中心瓦片
    {
        int ltx, lty;
        screenToTile(camX + (SCREEN_W - sidebarW) / 2.0f, camY + SCREEN_H / 2.0f, ltx, lty);
        g_sfx.setListener((float)ltx, (float)lty);
    }
    advancePresentation(1.0f/LOGIC_FPS);
    consumePresentationEvents();
    // 低电力警报（上升沿触发）
    {
        bool low = world.players[localPlayer].lowPower();
        if(!low)std::erase_if(evaLines,[](const Subtitle& line){return line.powerWarning;});
        if (low && !wasLowPower) {
            g_sfx.play(Sfx::Alarm, 0.8f);
            if(msgTimer>0 && !msg.empty()) {
                // Preserve the player's current command result. Keep one
                // pending warning ahead of routine EVA subtitles; the alarm
                // and power indicator still respond immediately.
                const std::string warning=TR(S::MsgLowPower);
                std::erase_if(evaLines,[](const Subtitle& line){return line.powerWarning;});
                if(evaLines.size()>=4)evaLines.pop_back();
                evaLines.push_front({warning,true});
            } else message(TR(S::MsgLowPower));
        }
        wasLowPower = low;
    }

    // EVA 播报：消费世界事件 → 字幕队列（仅本地玩家）
    while (!world.evaQueue.empty()) {
        const World::EvaEvent& ev = world.evaQueue.front();
        if (ev.player == localPlayer && evaLines.size() < 4) {
            evaLines.push_back({ev.text,false});
            g_sfx.play(Sfx::Eva, 0.7f);
        }
        world.evaQueue.pop_front();
    }
    // 逐条显示（不覆盖当前消息）
    if (msgTimer <= 0 && !evaLines.empty()) {
        msg = evaLines.front().powerWarning?TR(S::MsgLowPower):evaLines.front().text;
        msgTimer = 2.6f;
        evaLines.pop_front();
    }

    // 胜负判定
    if (!gameOver) {
        bool meDead = world.players[localPlayer].defeated;
        if (campaignMission >= 0) {
            // 战役：先刷波次与触发器再判定
            const MissionDef& md = missionTable()[campaignMission];
            if (nextWave < md.waves.size() && world.tick >= (uint64_t)md.waves[nextWave].atTick) {
                spawnCampaignWave();
                nextWave++;
            }
            updateTriggers(); // P7 触发器脚本（可在判定前直接 Win/Lose；含 AllPrimary 门闩）
            if (gameOver) { /* 触发器/门闩已终结本局 */ }
            else if (meDead) { gameOver = true; victory = false; }
            else if (md.objective == 2) {
                // 触发器 / AllPrimary 门闩决定胜负：无歼灭默认捷径
            } else if (md.objective == 1) {
                // 存活目标：坚守到指定帧数
                if (world.tick >= (uint64_t)md.objectiveTick) { gameOver = true; victory = true; }
            } else {
                // 歼灭目标：敌军全灭且脚本波次刷完
                bool allAIDead = true;
                for (int i = 1; i < world.numPlayers; i++)
                    if (!world.players[i].defeated) allAIDead = false;
                if (allAIDead && nextWave >= md.waves.size()) { gameOver = true; victory = true; }
            }
            if (gameOver && victory) {
                if (!md.lineId.empty())
                    campaignSetProgress(md.lineId, md.lineIndex + 1);
            }
        } else {
            const MatchOutcome outcome=world.matchOutcome(localPlayer);
            if (outcome!=MatchOutcome::Running) {
                gameOver=true;
                victory=outcome==MatchOutcome::Victory;
            }
        }
    }
    if (msgTimer > 0) msgTimer -= 1.0f / LOGIC_FPS;
    // 联机异常结算：对手断线判胜（弃权），不同步判负并提示
    if (netGame && !gameOver) {
        if (netPeerLeft) { gameOver = true; victory = true; message(TR(S::PeerLeft)); }
        else if (netDesync) { gameOver = true; victory = false; message(TR(S::DesyncWarn)); }
    }
}

// ===================== 输入 =====================

