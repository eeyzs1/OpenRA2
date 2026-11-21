#include "game_regression.h"
#include "core/content.h"
#include "sfx/sound.h"
#include "sfx/announcer.h"
#include "sfx/voices.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace {
std::atomic<unsigned> mixedBuffers=0;
std::atomic<unsigned> audibleBuffers=0;

void observeMixedAudio(void* buffer,unsigned frames) {
    const auto* samples=static_cast<const float*>(buffer);
    if(std::any_of(samples,samples+frames,[](float value){return std::abs(value)>.0005f;}))
        audibleBuffers.fetch_add(1,std::memory_order_relaxed);
    mixedBuffers.fetch_add(1,std::memory_order_relaxed);
}
}

int GameRegression::nativeAudioRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* text){std::printf("NATIVE_AUDIO %s %s\n",ok?"PASS":"FAIL",text);failures+=!ok;};
    struct QuietAudio {
        float prior=1;
        QuietAudio(){if(!IsAudioDeviceReady())InitAudioDevice();if(IsAudioDeviceReady()){prior=GetMasterVolume();SetMasterVolume(0);}}
        ~QuietAudio(){g_sfx.shutdown();if(IsAudioDeviceReady())SetMasterVolume(prior);}
    } quietAudio;
    g_sfx.init();g_sfx.setMasterVol(1);
    if(IsAudioDeviceReady() && g_sfx.assetsOk()) {
        struct MixerObservation {
            MixerObservation(){AttachAudioMixedProcessor(observeMixedAudio);}
            ~MixerObservation(){DetachAudioMixedProcessor(observeMixedAudio);}
        } observation;
        const int previousListener=localPlayer;
        const auto playFor=[&](int owner,int listener) {
            const auto buffers=mixedBuffers.load(std::memory_order_relaxed);
            const auto audible=audibleBuffers.load(std::memory_order_relaxed);
            localPlayer=listener;world.presentationEvents.clear();
            PresentationEvent cue;cue.kind=PresentationKind::Sound;cue.player=owner;cue.sound=Sfx::Cash;cue.volume=.55f;
            world.presentationEvents.push_back(cue);consumePresentationEvents();
            WaitTime(.35);
            return std::pair{mixedBuffers.load(std::memory_order_relaxed)>buffers,
                audibleBuffers.load(std::memory_order_relaxed)>audible};
        };
        for(int owner:{0,1}) {
            const auto [remoteRunning,remoteHeard]=playFor(owner,1-owner);
            const auto [localRunning,localHeard]=playFor(owner,owner);
            std::printf("NATIVE_AUDIO_CREDIT owner=%d running=%d remote=%d local=%d\n",
                owner,remoteRunning && localRunning,remoteHeard,localHeard);
            check(remoteRunning && localRunning && !remoteHeard && localHeard,
                "the normal sound consumer only mixes the credit cue for its owning client");
        }
        const auto [running,heard]=playFor(-1,1);
        check(running && heard,"broadcast sound still mixes for the player-one listener");
        localPlayer=previousListener;
    }
    g_sfx.initBgm();
    check(IsAudioDeviceReady() && g_sfx.assetsOk() && g_announcer.clipCount()>=70,"real muted audio device loads native announcer and unit speech");
    if(!IsAudioDeviceReady() || !g_sfx.assetsOk())return failures+1;
    auto& music=g_sfx.musicPlayer();
    check(music.ready() && music.playing() && music.currentTrack().ends_with("ra2_grinder.wav") && music.duration()>140 && music.duration()<150,
        "menu streams the original full Grinder recording");
    music.setScene(MusicScene::Battle);const auto first=music.currentTrack();music.next();
    check(music.trackCount(MusicScene::Battle)==13 && music.playing() && music.currentTrack()!=first && music.duration()>140
        && music.currentTrack().find("native")!=std::string::npos,"battle rotates 13 original songs without including generated loops");
    for(int frame=0;frame<25;++frame){music.update();WaitTime(0.02);}
    const float played=music.position();music.toggle();const float pausedAt=music.position();
    for(int frame=0;frame<5;++frame){music.update();WaitTime(0.01);}
    const bool paused=!music.enabled() && !music.playing() && std::abs(music.position()-pausedAt)<0.03f;
    music.toggle();music.update();
    check(played>0.1f && paused && music.enabled() && music.playing() && music.position()>=pausedAt-0.03f,
        "music toggle pauses and resumes the same stream rather than restarting");

    const std::vector<OrderActor> actors{{(int)UnitType::GI,0}};
    g_unitVoices.reset();g_unitVoices.play(actors,UnitVoiceAction::Select);
    g_announcer.reset();auto before=g_announcer.playback().played;
    g_announcer.request(EvaCue::ConstructionComplete,0,100);
    check(g_announcer.playing() && g_announcer.playback().sample=="ceva048" && g_announcer.playback().played==before+1,
        "construction complete plays the original Allied EVA recording");
    before=g_announcer.playback().played;g_announcer.request(EvaCue::ConstructionComplete,0,100.01);
    g_announcer.request(EvaCue::LowPower,0,100.02);
    check(g_announcer.playback().played==before && g_announcer.pendingCount()==1,"duplicate dialogue coalesces while queued warnings wait");
    g_announcer.request(EvaCue::NukeLaunched,0,100.03);
    const bool preempted=g_announcer.playback().cue==EvaCue::NukeLaunched && !g_unitVoices.playing();
    before=g_announcer.playback().played;g_announcer.request(EvaCue::UnitLost,0,100.04);
    check(preempted && g_announcer.playback().played==before && !g_unitVoices.play(actors,UnitVoiceAction::Select),
        "critical nuclear warning preempts lower speech and protects its unit-speech channel");
    g_announcer.update(g_announcer.playback().until+0.01);
    check(g_announcer.playback().cue==EvaCue::LowPower && g_announcer.playing() && g_announcer.pendingCount()==0,
        "queued warning starts after the critical utterance ends");
    g_announcer.reset();g_announcer.request(EvaCue::UnitReady,1);
    check(g_announcer.playing() && g_announcer.playback().sample=="csof062","Soviet house uses Sofia rather than Allied EVA");
    g_announcer.reset();before=g_announcer.playback().played;
    const bool hasYuri=g_announcer.available(EvaCue::LowPower,3),yuri=g_announcer.request(EvaCue::LowPower,3);
    check(hasYuri?(yuri && g_announcer.playback().sample.starts_with("cyur")):(!yuri && g_announcer.playback().played==before),
        "missing Yuri recordings remain unavailable instead of borrowing another faction");
    g_announcer.reset();g_announcer.request(EvaCue::NukeLaunched,0,200);g_announcer.request(EvaCue::LowPower,0,200.01);
    before=g_announcer.playback().played;g_announcer.update(220);
    check(g_announcer.pendingCount()==0 && g_announcer.playback().played==before,"stale queued warnings expire rather than playing after a long interruption");

    world.init(64,64,20261114,1,1,{Faction::Allies,Faction::Soviet});
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
    for(auto& cell:world.map.cells)cell=Cell{};
    world.spawnBuilding(0,BldType::ConYard,5,5,true);world.spawnBuilding(0,BldType::Radar,12,5,true);
    world.spawnUnit(1,UnitType::MCV,50.5f,50.5f);
    for(auto& player:world.players){player.defeated=false;player.money=10000;}
    localPlayer=0;netGame=false;campaignMission=-1;ais.clear();phase=Phase::InGame;gameOver=victory=false;
    world.evaQueue.clear();world.presentationEvents.clear();resetNotifications();g_announcer.reset();
    msg="preserve command feedback";msgTimer=4;const auto checksum=world.checksum();const auto seed=world.rng.s;
    updateAnnouncements();
    check(g_announcer.playback().cue==EvaCue::LowPower && msg=="preserve command feedback" && !evaLines.empty()
        && evaLines.front().powerWarning && world.checksum()==checksum && world.rng.s==seed,
        "real low-power edge speaks without overwriting a command result or changing simulation");
    g_announcer.reset();before=g_announcer.playback().played;
    world.eva(1,"remote low-power text",EvaCue::LowPower);updateAnnouncements();
    check(g_announcer.playback().played==before,"remote house dialogue never enters local speech");
    g_announcer.reset();world.eva(0,"arbitrary translated caption",EvaCue::UnitLost);updateAnnouncements();
    check(g_announcer.playback().sample=="ceva064","native cue routing is independent of subtitle text and language");
    g_announcer.reset();g_announcer.request(EvaCue::NukeLaunched,0);g_announcer.request(EvaCue::LowPower,0);
    world.spawnBuilding(0,BldType::PowerPlant,5,12,true);updateAnnouncements();
    check(!world.players[0].lowPower() && g_announcer.pendingCount()==0,"restored power cancels an obsolete queued warning");

    const EID expansion=world.spawnUnit(0,UnitType::MCV,28.5f,12.5f);
    const int yardsBefore=world.countBlds(0,BldType::ConYard);
    World::Cmd expand;expand.type=World::Cmd::Deploy;expand.ids={expansion};g_announcer.reset();issueCmd(expand);
    check(world.countBlds(0,BldType::ConYard)==yardsBefore+1 && (!world.valid(expansion) || world.ents[expansion].isBuilding)
        && g_announcer.playback().cue==EvaCue::NewConstructionOptions,
        "actual MCV expansion announces new construction options");
    world.spawnBuilding(1,BldType::OreRefinery,40,25,true);
    const EID spy=world.spawnUnit(0,UnitType::Spy,37.5f,25.5f);world.update();
    world.orderAttack({spy},world.bldAt(40,25));world.evaQueue.clear();
    for(int tick=0;tick<1200 && world.valid(spy);++tick)world.update();
    const bool infiltrated=std::any_of(world.evaQueue.begin(),world.evaQueue.end(),[](const World::EvaEvent& event){return event.player==0 && event.cue==EvaCue::BuildingInfiltrated;});
    g_announcer.reset();updateAnnouncements();
    check(!world.valid(spy) && infiltrated && g_announcer.playback().cue==EvaCue::BuildingInfiltrated,
        "successful spy entry announces infiltration to the spy's owner");

    world.spawnBuilding(0,BldType::Barracks,12,12,true);g_announcer.reset();
    World::Cmd order;order.type=World::Cmd::StartUnitProd;order.a=(int)UnitType::GI;issueCmd(order);
    check(g_announcer.playback().cue==EvaCue::Training,"accepted production command announces Training");
    const auto production=world.producedUnits[0];
    for(int tick=0;tick<4000 && world.producedUnits[0]==production;++tick)world.update();
    const bool completion=std::any_of(world.evaQueue.begin(),world.evaQueue.end(),[](const World::EvaEvent& event){return event.player==0 && event.cue==EvaCue::UnitReady;});
    g_announcer.reset();updateAnnouncements();
    check(completion && g_announcer.playback().cue==EvaCue::UnitReady,"actual factory release emits a typed Unit Ready announcement");
    world.spawnBuilding(1,BldType::PowerPlant,44,40,true);world.spawnBuilding(1,BldType::Barracks,44,46,true);
    world.startUnitProd(1,UnitType::Conscript);const auto remoteProduction=world.producedUnits[1];
    for(int tick=0;tick<4000 && world.producedUnits[1]==remoteProduction;++tick)world.update();
    localPlayer=1;resetNotifications();g_announcer.reset();updateAnnouncements();
    check(world.producedUnits[1]>remoteProduction && g_announcer.playback().sample=="csof062",
        "client/player-one production has the same native readiness announcement");
    world.spawnBuilding(0,BldType::NukeSilo,20,20,true);world.players[0].swReady[(int)SWType::Nuke]=true;
    world.evaQueue.clear();g_announcer.reset();const bool launched=world.launchSW(0,SWType::Nuke,40,40);updateAnnouncements();
    check(launched && g_announcer.playback().cue==EvaCue::NukeLaunched && g_announcer.playback().sample=="csof002",
        "real enemy nuclear launch is voiced for the local listener faction");
    World reference=world;reference.map.bldOccRef=&reference.bldOcc;bool identical=true;
    for(int tick=0;tick<300;++tick) {
        if(tick%60==0)music.next();
        world.update();reference.update();consumePresentationEvents();updateAnnouncements();music.update();
        identical &= world.checksum()==reference.checksum() && world.rng.s==reference.rng.s;
        reference.presentationEvents.clear();
    }
    check(identical,"active native speech and streamed music leave 300 combat replay ticks unchanged");
    gameOver=true;victory=false;wasAudioGameOver=false;updateAudioPresentation();before=g_announcer.playback().played;updateAudioPresentation();
    check(g_announcer.playback().cue==EvaCue::Defeat && g_announcer.playback().sample=="csof023" && g_announcer.playback().played==before
        && music.scene()==MusicScene::Score && music.currentTrack().ends_with("ra2_ra2-sco.wav"),
        "defeat speaks once and switches to the original result music");
    phase=Phase::MainMenu;updateAudioPresentation();
    check(g_announcer.playback().cue==EvaCue::BattleTerminated && music.currentTrack().ends_with("ra2_grinder.wav"),
        "leaving battle switches back to original menu music and terminates battle control");
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(!scene.empty() && FileExists(scene.c_str())) {
        const bool loaded=world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
            && world.loadNativeScene(scene.c_str()) && world.startNativeEvents();
        check(loaded,"the authored scenario loads for mission-result audio routing");
        if(loaded) {
            World authored=world;authored.map.bldOccRef=&authored.bldOcc;
            for(bool win:{true,false}) {
                world=authored;world.map.bldOccRef=&world.bldOcc;localPlayer=0;campaignMission=-1;ais.clear();
                phase=Phase::InGame;gameOver=victory=false;netGame=false;resetNotifications();audioPhase=Phase::InGame;
                for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id)) {
                    const auto& e=world.ents[id];
                    if(win && e.player==1 && e.isBuilding && e.nativeIdentity.definition->kind==0)world.kill(id,false);
                    if(!win && e.nativeIdentity.definition->id=="TANY")world.damage(id,10000,1);
                }
                for(int i=0;i<400 && !gameOver;++i)logic();
                updateAudioPresentation();const auto count=g_announcer.playback().played;updateAudioPresentation();
                check(gameOver && victory==win && g_announcer.playback().cue==(win?EvaCue::MissionAccomplished:EvaCue::MissionFailed)
                    && g_announcer.playback().played==count && g_announcer.playing(),
                    "an actual authored terminal action plays the original mission-result EVA once instead of the skirmish result cue");
            }
        }
    }
    g_announcer.reset();const auto clips=g_announcer.clipCount();
    const bool loaded=g_announcer.preloadNative("Mis_A1_EvaProtectStatue",0);const auto afterLoad=g_announcer.clipCount();
    check(loaded && g_announcer.preloadNative("mis_a1_evaprotectstatue",0)
        && g_announcer.clipCount()==afterLoad && afterLoad>=clips && afterLoad<=clips+1,
        "authored dialogue resolves case-insensitive source keys and loads each original clip once");
    before=g_announcer.playback().played;g_announcer.requestNative("Mis_A1_EvaProtectStatue",0);
    check(g_announcer.playing() && g_announcer.playback().sample=="ma1ev03" && g_announcer.playback().played==before+1,
        "the first Allied mission plays its original Protect Statue recording");
    const auto count=g_announcer.clipCount();before=g_announcer.playback().played;
    check(!g_announcer.requestNative("../../missing.wav",0) && g_announcer.clipCount()==count && g_announcer.playback().played==before,
        "an unknown authored key cannot load an arbitrary file or substitute unrelated dialogue");
    g_sfx.setMasterVol(0);before=g_announcer.playback().played;g_announcer.request(EvaCue::UnitLost,0);
    check(!g_announcer.playing() && !g_unitVoices.playing() && g_announcer.playback().played==before,"live mute stops and clears speech without replaying it later");
    std::printf("NATIVE_AUDIO SUMMARY failures=%d announcer_clips=%zu battle_tracks=%zu\n",failures,g_announcer.clipCount(),music.trackCount(MusicScene::Battle));
    return failures;
}
