#include "game/game.h"
#include "game/campaign.h"
#include "game/campaign_save.h"
#include "game/application_save_format.h"
#include "game/world_save_format.h"
#include "game/native_scene_contract.h"
#include "game/skirmish_map.h"
#include <cstdlib>
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

#include <cstdio>
#include <filesystem>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#define CloseWindow CloseWindowWin32
#define ShowCursor ShowCursorWin32
#include <windows.h>
#undef CloseWindow
#undef ShowCursor
#endif

namespace {
bool setupMatchesWorld(const MatchSetup& s,const World& w){
    if(!validMatchSetup(s) || s.ai+1!=w.numPlayers || s.size!=w.map.w || w.map.w!=w.map.h || s.mode!=(int)w.skirmishMode
        || (s.rules==RulesChoice::Fusion)==w.usesRulesProfile())return false;
    if((w.nativeSceneVersion==nativeSkirmishMapSceneVersion)!=!s.originalMap.empty()
        || (!s.originalMap.empty() && s.originalMap!=w.map.native.assetId))return false;
    if(s.country!=(int)Country::COUNT && s.country!=(int)w.players[0].country)return false;
    if(s.color!=w.players[0].colorId)return false;
    if(s.country!=(int)Country::COUNT && !w.rulesCountryAllowed((Country)s.country))return false;
    for(int i=0;i<s.ai;++i)if(s.aiCountries[i]!=(int)Country::COUNT && !w.rulesCountryAllowed((Country)s.aiCountries[i]))return false;
    if(s.rules==RulesChoice::RA2 && w.rulesProfileId.starts_with("yr-"))return false;
    if(s.rules==RulesChoice::YR && w.rulesProfileId.starts_with("ra2-"))return false;
    return true;
}
}

bool Game::saveGameFile(const char* path) {
    if(netGame || campaignMission < -1 || campaignMission >= (int)missionTable().size() || objectiveText.size()>1024)return false;
    const MissionDef* mission=campaignMission>=0?&missionTable()[campaignMission]:nullptr;
    if(missionTriggers.size()!=(mission?mission->triggers.size():0) || nextWave>(mission?mission->waves.size():0))return false;
    const bool originalMap=world.nativeSceneVersion==nativeSkirmishMapSceneVersion;
    if(originalMap && (mission || !matchSetup || !setupMatchesWorld(*matchSetup,world)))return false;
    ScriptSnapshot script;
    if(!g_script.captureSnapshot(script)) {
        TraceLog(LOG_WARNING,"SAVE: %s",g_script.persistenceError().c_str());return false;
    }
    if(script.active && !script.sources.empty()) {
        World staged=world;staged.map.bldOccRef=&staged.bldOcc;
        ScriptEngine prepared;
        if(!prepared.prepareRestore(&staged,mission?1:0,campaignMission,script)) {
            TraceLog(LOG_WARNING,"SAVE: declared script state cannot resume: %s",prepared.persistenceError().c_str());return false;
        }
    }
    syncFogKnowledge();
    for(const auto& ai:ais)if(!ai.knowledgeValid(world))return false;
    MakeDirectory("saves");
    const auto destination=std::filesystem::u8path(path);
    auto staging=destination; staging += ".tmp";
#ifdef _WIN32
    FILE* f = _wfopen(staging.c_str(),L"wb");
#else
    FILE* f = fopen(staging.c_str(),"wb");
#endif
    if (!f) return false;
    const auto& format=originalMap?ApplicationSaveFormat::originalSkirmish:ApplicationSaveFormat::current;
    bool ok = fwrite(format.magic.data(), 1, 8, f) == 8;
    auto w = [&](const auto& v) { if (ok && fwrite(&v, sizeof(v), 1, f) != 1) ok = false; };
    w(campaignMission);
    uint64_t nw = (uint64_t)nextWave;
    w(nw);
    w(localPlayer);
    w(camX); w(camY);
    w(gameSpeed);
    // AI 状态（思考计时/进攻波次/海军检测缓存，避免读档后行为跳变）
    uint32_t an = (uint32_t)ais.size();
    w(an);
    for (const SkirmishAI& a : ais) {
        w(a.player); w(a.thinkTimer); w(a.attackWave); w(a.attackTimer); w(a.difficulty);
        w(a.hasWater); w(a.navalPlaceable); w(a.navalCheckCd); w(a.navalFail);
        w(a.personality); w(a.rallyPoint.x); w(a.rallyPoint.y);
        w(a.rallyCheckTimer); w(a.expandTimer); w(a.defenseCheckTimer); w(a.lastAttacker);
        w(a.pcfg.aggression); w(a.pcfg.defensePriority); w(a.pcfg.techRush); w(a.pcfg.econFocus); w(a.pcfg.unitMix);
        w(a.pcfg.rallySize); w(a.pcfg.useEngineers); w(a.pcfg.useSpies); w(a.pcfg.useSuperWeapon);
        w(a.pcfg.expandBase); w(a.pcfg.retreatDamaged); w(a.pcfg.focusFire);
        if(ok)ok=a.saveKnowledge(f);
    }
    // P7 触发器运行时状态（fired/armed）与 HUD 目标文本
    uint32_t tn = (uint32_t)missionTriggers.size();
    w(tn);
    for (const Trigger& t : missionTriggers) {
        uint8_t fired = t.fired ? 1 : 0, armed = t.armed ? 1 : 0, enabled=t.enabled?1:0;
        w(fired); w(armed);w(enabled);
    }
    uint32_t ol = (uint32_t)objectiveText.size();
    w(ol);
    if (ok && ol > 0 && fwrite(objectiveText.data(), 1, ol, f) != ol) ok = false;
    const CampaignSnapshot campaign{campaignDifficulty,mission?campRuntime:CampaignRuntime{},mission?campaignObjDone:std::vector<bool>{},gameOver,victory};
    if(ok)ok=writeCampaignSnapshot(f,mission,campaign) && script.write(f);
    if(ok)ok=writeMatchSetup(f,!mission && matchSetup && setupMatchesWorld(*matchSetup,world)?matchSetup:std::nullopt);
    if (ok) ok = writeFogKnowledge(f);
    if (ok) ok = world.saveGame(f);
    if (fflush(f)!=0) ok=false;
    if (fclose(f)!=0) ok=false;
    std::error_code error;
    if (ok) {
#ifdef _WIN32
        ok=MoveFileExW(staging.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        std::filesystem::rename(staging,destination,error); ok=!error;
#endif
    }
    if (!ok) std::filesystem::remove(staging,error);
    return ok;
}

bool Game::loadGameFile(const char* path) {
    const auto source=std::filesystem::u8path(path);
#ifdef _WIN32
    FILE* f=_wfopen(source.c_str(),L"rb");
#else
    FILE* f=fopen(source.c_str(),"rb");
#endif
    if (!f) return false;
    bool ok=true;
    char magic[8]{};
    ok=fread(magic,1,8,f)==8;
    const auto* format=ok?ApplicationSaveFormat::identify(magic):nullptr;
    ok=format!=nullptr;
    const bool setupSnapshot=format && format->matchSetup;
    const bool nativeFrames=format && format->nativeFrames;
    const bool nativeActors=format && format->nativeActors;
    const bool cellMasks=format && format->cellMasks;
    auto r=[&](auto& v) { if(ok && fread(&v,sizeof(v),1,f)!=1) ok=false; };
    int savedMission=-1,savedPlayer=0,savedSpeed=1;
    uint64_t savedWave=0;
    float savedX=0,savedY=0;
    r(savedMission);r(savedWave);r(savedPlayer);r(savedX);r(savedY);r(savedSpeed);
    if(savedMission < -1 || savedMission >= (int)missionTable().size() || savedWave>1000000
        || savedPlayer<0 || savedPlayer>=MAX_PLAYERS || !std::isfinite(savedX) || !std::isfinite(savedY)
        || savedSpeed<0 || savedSpeed>2) ok=false;
    if(ok && !format->snapshots && (savedMission!=-1 || savedWave || savedPlayer!=0))ok=false;
    uint32_t count=0;r(count);
    if(count>MAX_PLAYERS) ok=false;
    std::vector<SkirmishAI> savedAis;
    if(ok) savedAis.resize(count);
    for(auto& ai:savedAis) {
        r(ai.player);r(ai.thinkTimer);r(ai.attackWave);r(ai.attackTimer);r(ai.difficulty);
        r(ai.hasWater);r(ai.navalPlaceable);r(ai.navalCheckCd);r(ai.navalFail);
        r(ai.personality);r(ai.rallyPoint.x);r(ai.rallyPoint.y);
        {
            r(ai.rallyCheckTimer);r(ai.expandTimer);r(ai.defenseCheckTimer);r(ai.lastAttacker);
            r(ai.pcfg.aggression);r(ai.pcfg.defensePriority);r(ai.pcfg.techRush);r(ai.pcfg.econFocus);r(ai.pcfg.unitMix);
            r(ai.pcfg.rallySize);r(ai.pcfg.useEngineers);r(ai.pcfg.useSpies);r(ai.pcfg.useSuperWeapon);
            r(ai.pcfg.expandBase);r(ai.pcfg.retreatDamaged);r(ai.pcfg.focusFire);
        }
        if(ok)ok=ai.loadKnowledge(f);
        const auto scalar=[](float v) { return std::isfinite(v) && v>=0 && v<=100; };
        if(ai.player<0 || ai.player>=MAX_PLAYERS || ai.thinkTimer<0 || ai.attackWave<0 || ai.attackTimer<0
            || ai.navalCheckCd<0 || ai.navalFail<0 || ai.rallyCheckTimer<0 || ai.expandTimer<0 || ai.defenseCheckTimer<0
            || ai.lastAttacker<INVALID_EID || ai.hasWater < -1 || ai.hasWater>1
            || ai.navalPlaceable < -1 || ai.navalPlaceable>1
            || (int)ai.difficulty>(int)AIDiff::Brutal || (int)ai.personality>(int)AIPersonality::Technician
            || !scalar(ai.pcfg.aggression) || ai.pcfg.aggression==0 || !scalar(ai.pcfg.defensePriority)
            || !scalar(ai.pcfg.techRush) || !scalar(ai.pcfg.econFocus) || ai.pcfg.econFocus==0
            || !scalar(ai.pcfg.unitMix) || ai.pcfg.rallySize<1 || ai.pcfg.rallySize>10000) ok=false;
    }
    uint32_t triggerCount=0;r(triggerCount);
    if(triggerCount>256) ok=false;
    std::vector<Trigger> savedTriggers;
    if(ok && savedMission>=0) savedTriggers=missionTable()[savedMission].triggers;
    const MissionDef* mission=ok && savedMission>=0?&missionTable()[savedMission]:nullptr;
    if(ok && (triggerCount!=savedTriggers.size() || savedWave>(mission?mission->waves.size():0)))ok=false;
    if(ok) for(uint32_t i=0;i<triggerCount;++i) {
        uint8_t fired=0,armed=0,enabled=0;r(fired);r(armed);r(enabled);
        if(fired>1 || armed>1 || enabled>1) ok=false;
        if(ok) { savedTriggers[i].fired=fired!=0;savedTriggers[i].armed=armed!=0;savedTriggers[i].enabled=enabled!=0; }
    }
    uint32_t length=0;r(length);
    if(length>1024) ok=false;
    std::string savedObjective;
    if(ok) {
        savedObjective.resize(length);
        if(length && fread(savedObjective.data(),1,length,f)!=length) ok=false;
    }
    CampaignSnapshot savedCampaign;ScriptSnapshot savedScript;
    if(ok && format->snapshots)ok=readCampaignSnapshot(f,mission,savedCampaign) && savedScript.read(f);
    if(ok && !format->snapshots && (triggerCount || !savedObjective.empty()))ok=false;
    std::optional<MatchSetup> savedSetup;if(ok && setupSnapshot)ok=readMatchSetup(f,savedSetup,format->originalMap);
    FogKnowledge savedKnowledge;
    if(ok) ok=readFogKnowledge(f,savedKnowledge,cellMasks,nativeActors,nativeFrames);
    if(ok && format->requiredWorldRevision) {
        fpos_t position;char worldMagic[8]{};
        ok=fgetpos(f,&position)==0 && fread(worldMagic,1,8,f)==8;
        const auto* worldFormat=ok?WorldSaveFormat::identify(worldMagic):nullptr;
        ok=worldFormat && worldFormat->revision==format->requiredWorldRevision;
        if(ok)ok=fsetpos(f,&position)==0;
    }
    World savedWorld;
    if(ok) ok=savedWorld.loadGame(f);
    if(ok)ok=format->originalMap==(savedWorld.nativeSceneVersion==nativeSkirmishMapSceneVersion);
    if(ok && savedSetup)ok=savedMission==-1 && savedPlayer==0 && setupMatchesWorld(*savedSetup,savedWorld);
    if(ok && format->originalMap){OriginalSkirmishMaps::Data data;std::string validated;
        ok=savedSetup && OriginalSkirmishMaps::load(savedSetup->originalMap,data,savedWorld.numPlayers)
            && OriginalSkirmishMaps::composeRules(savedWorld.nativeScenarioSource,data,validated);}
    if(ok && savedWorld.usesRulesProfile()){
        RulesInfo info;ok=validateRulesSource({savedWorld.nativeScenarioSource,savedWorld.aircraftSpawnersEnabled,savedWorld.aswEnabled,savedWorld.nativeFlightEnabled,savedWorld.nativeJumpjetEnabled,savedWorld.veterancyProgressEnabled,savedWorld.independentSupportEnabled,savedWorld.supportDeliveryEnabled},info,savedWorld.map.native.active()?&savedWorld.map:nullptr);
    }
    if(ok) ok=validateFogKnowledge(savedKnowledge,savedWorld,savedPlayer);
    if(ok && (fgetc(f)!=EOF || ferror(f)))ok=false;
    fclose(f);
    if(ok && savedPlayer>=savedWorld.numPlayers) ok=false;
    if(ok) {
        std::vector<bool> seenAi(savedWorld.numPlayers,false);
        for(const auto& ai:savedAis) {
            if(ai.player>=savedWorld.numPlayers || seenAi[ai.player]
                || ai.lastAttacker >= (EID)savedWorld.ents.size() || !ai.knowledgeValid(savedWorld)) { ok=false;break; }
            seenAi[ai.player]=true;
        }
    }
    if(!ok) return false;
    ScriptEngine preparedScript;
    if(!preparedScript.prepareRestore(&savedWorld,savedMission>=0?1:0,savedMission,savedScript)) {
        TraceLog(LOG_WARNING,"LOAD: %s",preparedScript.persistenceError().c_str());return false;
    }
    // Commit only after the complete envelope and World have been validated.
    g_script.shutdown();
    resetMatchInteraction();
    campaignMission=savedMission;nextWave=(size_t)savedWave;localPlayer=savedPlayer;
    pendingMission=savedMission;campaignDifficulty=savedCampaign.difficulty;
    campRuntime=savedCampaign.runtime;campaignObjDone=std::move(savedCampaign.objectives);
    camX=savedX;camY=savedY;gameSpeed=savedSpeed;
    ais=std::move(savedAis);missionTriggers=std::move(savedTriggers);objectiveText=std::move(savedObjective);
    world=std::move(savedWorld);world.map.bldOccRef=&world.bldOcc;
    matchSetup=savedSetup;if(matchSetup)applyMatchSetup(*matchSetup);else deriveMatchSetup();
    g_script.takePrepared(preparedScript,&world);
    fogKnowledge=std::move(savedKnowledge);fogKnowledge.epoch=world.presentationEpoch;minimapTimer=0;
    moveFeedbackTimer = 0;
    feedbackTerrainHeight=-1;
    combatCues.clear();
    // 界面状态复位（与开局一致）
    sel.clear();
    selBuilding = INVALID_EID;
    clearPlacement();
    targetingSW = SWType::COUNT;
    chronoSourceSel.clear();
    targetingParadrop = false;
    targetingSupport = SupportType::COUNT;
    sideMode = 0;
    paused = false;
    showMenu = false;
    gameOver=savedCampaign.gameOver;victory=savedCampaign.victory;
    resetNotifications();
    phase = Phase::InGame;
    bakeTerrain(); // 读档后按载入地图重烘地表
    g_sprites.preloadMatch(localPlayer);
    preloadNativeMissionSpeech();
    return true;
}

