#include "game_regression.h"
#include "sfx/sound.h"
#include "sfx/voices.h"
#include <algorithm>
#include <cstdio>

int GameRegression::unitVoiceRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* text){std::printf("UNIT_VOICE %s %s\n",ok?"PASS":"FAIL",text);failures+=!ok;};
    // Hidden Game tests intentionally skip audio. Open a real device here, but
    // mute its process mixer so playback checks never disturb the user.
    struct QuietAudio {
        float previous=1;
        QuietAudio(){if(!IsAudioDeviceReady())InitAudioDevice();if(IsAudioDeviceReady()){previous=GetMasterVolume();SetMasterVolume(0);}}
        ~QuietAudio(){g_sfx.shutdown();if(IsAudioDeviceReady())SetMasterVolume(previous);}
    } quietAudio;
    g_sfx.init();
    check(IsAudioDeviceReady() && g_unitVoices.missingCount()==0 && g_unitVoices.clipCount()>=500,
        "native speech is decoded and loaded into a real audio device");
    if(!IsAudioDeviceReady() || g_unitVoices.missingCount())return failures+1;
    check(g_unitVoices.available((int)UnitType::GI,0,UnitVoiceAction::Select)
        && g_unitVoices.available((int)UnitType::Conscript,1,UnitVoiceAction::Move)
        && g_unitVoices.available((int)UnitType::Brute,3,UnitVoiceAction::Attack)
        && g_unitVoices.available((int)UnitType::MCV,2,UnitVoiceAction::Select),
        "RA2, YR and faction-specific MCV profiles resolve");
    check(!g_unitVoices.available((int)UnitType::PLA,2,UnitVoiceAction::Select)
        && !g_unitVoices.available((int)UnitType::Type99,2,UnitVoiceAction::Move)
        && !g_unitVoices.available((int)UnitType::Hornet,0,UnitVoiceAction::Select),
        "custom units without native voices and unvoiced native subunits have no invented substitutes");
    const float previousVolume=g_sfx.masterVolume();g_sfx.setMasterVol(1);
    world.init(64,64,20261115,1,1,{Faction::Allies,Faction::Soviet});
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
    for(auto& cell:world.map.cells)cell=Cell{};
    const EID gi=world.spawnUnit(0,UnitType::GI,10.5f,10.5f);
    const EID engineer=world.spawnUnit(0,UnitType::Engineer,10.5f,13.5f);
    const EID deployed=world.spawnUnit(0,UnitType::GuardianGI,13.5f,17.5f);world.ents[deployed].deployed=true;
    const EID enemy=world.spawnUnit(1,UnitType::Conscript,18.5f,10.5f);
    const EID miner=world.spawnUnit(0,UnitType::ChronoMiner,12.5f,20.5f);
    world.map.at(20,20).terrain=Terrain::Ore;world.map.at(20,20).ore=world.map.at(20,20).oreMax=40;
    for(int player=0;player<world.numPlayers;++player){world.players[player].defeated=false;std::fill(world.map.fog[player].begin(),world.map.fog[player].end(),FOG_VISIBLE);}
    localPlayer=0;netGame=false;ais.clear();campaignMission=-1;world.presentationEvents.clear();
    camX=-500;camY=0;camZoom=1;
    auto point=[&](EID id){const Rectangle box=unitScreenRect(world.ents[id]);return Vector2{box.x+box.width/2,box.y+box.height/2};};
    auto played=[&](){return g_unitVoices.playback().played;};
    auto before=played();auto state=world.checksum();auto rng=world.rng.s;
    g_unitVoices.reset();const auto giPoint=point(gi);doSelect((int)giPoint.x,(int)giPoint.y,false);
    check(sel==std::vector<EID>{gi} && played()==before+1 && g_unitVoices.playing()
        && g_unitVoices.playback().unitType==(int)UnitType::GI && g_unitVoices.playback().sample.starts_with("ra2_igis")
        && world.checksum()==state && world.rng.s==rng,"real point selection plays the selected GI's original line without changing simulation");
    const auto engPoint=point(engineer);g_unitVoices.reset();before=played();
    doBoxSelect({std::min(giPoint.x,engPoint.x)-25,std::min(giPoint.y,engPoint.y)-25,
        std::abs(giPoint.x-engPoint.x)+50,std::abs(giPoint.y-engPoint.y)+50},false);
    check(sel.size()==2 && played()==before+1 && g_unitVoices.playing(),"box selection speaks once for the group");
    before=played();const auto enemyPoint=point(enemy);doSelect((int)enemyPoint.x,(int)enemyPoint.y,false);
    check(sel.empty() && played()==before,"enemy selection cannot speak through the local unit voice channel");

    World reference=world;reference.map.bldOccRef=&reference.bldOcc;
    auto command=[&](const World::Cmd& request){reference.applyCmd(0,request);issueCmd(request);return reference.checksum()==world.checksum() && reference.rng.s==world.rng.s;};
    World::Cmd request;request.type=World::Cmd::Move;request.ids={deployed,gi};request.x=25;request.y=25;
    g_unitVoices.reset();before=played();bool equal=command(request);
    const auto moved=reference.presentationEvents.back().move;
    check(equal && moved.accepted==1 && moved.rejected==1 && moved.actors==std::vector<OrderActor>{{(int)UnitType::GI,0}}
        && played()==before+1 && g_unitVoices.playback().action==UnitVoiceAction::Move && g_unitVoices.playback().unitType==(int)UnitType::GI,
        "partial move acknowledgement uses an accepted unit, never the rejected deployed unit");
    request.ids={deployed};g_unitVoices.reset();before=played();equal=command(request);
    check(equal && played()==before,"fully rejected movement emits no successful acknowledgement");
    request={};request.type=World::Cmd::Attack;request.ids={engineer,gi};request.a=enemy;
    g_unitVoices.reset();before=played();equal=command(request);
    const auto attacked=reference.presentationEvents.back().attack;
    check(equal && attacked.accepted==1 && attacked.rejected==1 && played()==before+1
        && g_unitVoices.playback().action==UnitVoiceAction::Attack && g_unitVoices.playback().unitType==(int)UnitType::GI,
        "real attack receipt speaks only for the armed unit that accepted the target");
    request.ids={engineer};g_unitVoices.reset();before=played();equal=command(request);
    check(equal && played()==before,"unarmed rejected attack produces no attack voice");

    request={};request.type=World::Cmd::Harvest;request.ids={engineer,miner};request.a=request.b=20;
    g_unitVoices.reset();before=played();equal=command(request);
    check(equal && played()==before+1 && g_unitVoices.playback().unitType==(int)UnitType::ChronoMiner
        && reference.presentationEvents.back().harvestActors==std::vector<OrderActor>{{(int)UnitType::ChronoMiner,0}},
        "mining command acknowledges the actual miner using its authored harvest or same-unit move group");
    request.ids={engineer};g_unitVoices.reset();before=played();equal=command(request);
    check(equal && played()==before,"non-miners do not acknowledge a harvest order");

    request={};request.type=World::Cmd::Attack;request.ids={gi};request.a=enemy;world.applyCmd(0,request);
    world.kill(gi,false);const EID replacement=world.spawnUnit(0,UnitType::Conscript,10.5f,10.5f);
    g_unitVoices.reset();state=world.checksum();rng=world.rng.s;before=played();consumePresentationEvents();
    check(replacement==gi && played()==before+1 && g_unitVoices.playback().unitType==(int)UnitType::GI
        && world.checksum()==state && world.rng.s==rng,
        "delayed receipts retain their original speaker after death and entity-slot reuse");
    request={};request.type=World::Cmd::Move;request.ids={enemy};request.x=25;request.y=30;
    world.applyCmd(1,request);before=played();state=world.checksum();consumePresentationEvents();
    check(played()==before && world.checksum()==state,"remote house command receipts stay out of local speech");
    g_unitVoices.reset();before=played();world.orderMove({replacement},12,12,false);consumePresentationEvents();
    check(played()==before,"AI and internal movement do not generate player acknowledgements");

    const std::vector<OrderActor> actor{{(int)UnitType::GI,0}};
    g_unitVoices.reset();before=played();
    const bool first=g_unitVoices.play(actor,UnitVoiceAction::Select,100);
    const bool interrupted=g_unitVoices.play(actor,UnitVoiceAction::Move,100.01);
    const std::string firstMove=g_unitVoices.playback().sample;
    const bool repeat=g_unitVoices.play(actor,UnitVoiceAction::Move,100.02);
    const bool reselected=g_unitVoices.play(actor,UnitVoiceAction::Select,100.25);
    const bool later=g_unitVoices.play(actor,UnitVoiceAction::Move,100.5);
    check(first && interrupted && !repeat && !reselected && later && played()==before+3
        && firstMove!=g_unitVoices.playback().sample && g_unitVoices.playback().pitch==1,
        "commands can interrupt selection, rapid clicks are limited and consecutive lines vary at native pitch");
    g_sfx.setMasterVol(0);before=played();state=world.checksum();rng=world.rng.s;voiceSelection({replacement});
    check(!g_unitVoices.playing() && played()==before && world.checksum()==state && world.rng.s==rng,
        "live mute stops speech and prevents new playback without touching the world RNG");
    g_sfx.setMasterVol(1);g_unitVoices.reset();
    World replay=world;replay.map.bldOccRef=&replay.bldOcc;bool replaySame=true;
    for(int tick=0;tick<300;++tick) {
        if(tick%30==0)g_unitVoices.play(actor,tick%60?UnitVoiceAction::Attack:UnitVoiceAction::Move,200+tick);
        world.update();replay.update();consumePresentationEvents();replay.presentationEvents.clear();
        replaySame &= world.checksum()==replay.checksum() && world.rng.s==replay.rng.s;
    }
    check(replaySame,"playing native voices throughout combat leaves 300 replay ticks identical");
    g_unitVoices.reset();g_sfx.setMasterVol(previousVolume);
    std::printf("UNIT_VOICE SUMMARY failures=%d clips=%zu played=%llu suppressed=%llu\n",failures,g_unitVoices.clipCount(),
        (unsigned long long)g_unitVoices.playback().played,(unsigned long long)g_unitVoices.playback().suppressed);
    return failures;
}
