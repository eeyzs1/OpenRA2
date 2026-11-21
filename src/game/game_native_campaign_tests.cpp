#include "game_regression.h"
#include "sfx/sound.h"
#include "sfx/announcer.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::nativeCampaignRegression(const char* outputDir) {
    int mission=-1;
    for(int i=0;i<(int)missionTable().size();++i)
        if(missionTable()[i].nativeScene=="assets/maps/native/all01t/scene.ini"){mission=i;break;}
    if(mission<0){std::printf("NATIVE_CAMPAIGN SKIP: export the original scene and mission text\n");return 77;}
    ScopedUserFile progress("userdata/campaign_progress.ini");
    if(!progress.ready())return 1;
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_CAMPAIGN %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    struct QuietAudio {
        float prior=1;
        QuietAudio(){if(!IsAudioDeviceReady())InitAudioDevice();if(IsAudioDeviceReady()){prior=GetMasterVolume();SetMasterVolume(0);}}
        ~QuietAudio(){g_sfx.shutdown();if(IsAudioDeviceReady())SetMasterVolume(prior);}
    } audio;
    g_sfx.init();g_sfx.setMasterVol(1);SetTargetFPS(0);
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};phase=Phase::MainMenu;
    const auto click=[&](float x,float y) {
        sim.pos=menuCanvasFromUi(x,y);sim.pressedL=sim.downL=true;render();
        sim.pressedL=sim.downL=false;sim.releasedL=true;render();sim.releasedL=false;
    };
    render();click(556,237);
    check(phase==Phase::MissionSelect,"the normal main-menu campaign button opens the installed catalog");
    render();click(112,128);
    check(phase==Phase::MissionBrief && pendingMission==mission,"the initial faction tab exposes the real first mission and opens its briefing");
    const auto& definition=missionTable()[mission];
    check(definition.nativeTexts.size()>=24 && definition.brief.find('\n')!=std::string::npos
        && !definition.objectives.empty(),"the briefing and all authored message labels come from the original CSF catalog");
    shotAtDisplaySize=true;shotFile=(std::filesystem::path(outputDir)/"normal-briefing.png").string();render();
    // Deliberately conflicting skirmish preferences must not rewrite the scene.
    cfgCrates=cfgAlliance=cfgShortGame=true;cfgMoney=50000;campaignDifficulty=2;click(556,392);
    const bool started=phase==Phase::InGame && campaignMission==mission && world.nativeEvents.active;
    check(started,"the briefing Start button launches the original native runtime");
    if(!started){sim=SimInput{};return failures+1;}
    std::printf("NATIVE_CAMPAIGN START map=%s objects=%zu houses=%d legacy_ai=%zu money=%lld allied04=%d crates=%d short=%d difficulty=%d authored_ai=%d\n",
        world.map.native.assetId.c_str(),world.ents.size(),world.numPlayers,ais.size(),(long long)world.players[0].money,
        world.isAllied(0,4),world.cratesEnabled,world.shortGame,world.nativeEvents.difficulty,world.nativeAI.active);
    check(world.map.native.assetId=="all01t" && world.ents.size()==738 && world.numPlayers==8 && ais.empty()
        && world.players[0].money==4000 && world.isAllied(0,4) && !world.isAllied(4,0) && !world.cratesEnabled && !world.shortGame
        && world.nativeEvents.difficulty==2 && world.nativeAI.active,
        "all 738 source objects, eight houses, original funds, diplomacy and authored AI survive conflicting skirmish settings");
    const auto hero=[&](){for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id)
        && world.ents[id].nativeIdentity.object=="infantry:0")return id;return INVALID_EID;};
    while(world.tick<149)logic();g_announcer.reset();logic();
    check(g_announcer.playing() && g_announcer.playback().sample=="ma1ev03"
        && std::any_of(pendingNativeEventCues.begin(),pendingNativeEventCues.end(),[](const auto& cue){return cue.action==21 && cue.key=="Mis_A1_EvaProtectStatue";}),
        "the actual five-second source trigger reaches the original mission voice through normal Game logic");
    const auto caption=[&](const char* key)->std::string{for(const auto& text:definition.nativeTexts)if(text.first==key)return text.second;return {};};
    const auto expected=caption("mission:all01a");
    NativeEventCue hint;hint.action=11;hint.house=localPlayer;hint.key="MISSION:ALL01a";world.nativeEventCues.push_back(hint);
    const auto hash=world.checksum();consumeNativeEventCues();
    check(!expected.empty() && msg==expected && world.checksum()==hash,
        "authored text resolves case-insensitive CSF labels without changing simulation");
    hint.house=1;hint.key="MISSION:All01b";world.nativeEventCues.push_back(hint);consumeNativeEventCues();
    check(msg==expected,"another house's presentation cue cannot overwrite local mission instructions");
    const auto checkpoint=(std::filesystem::path(outputDir)/"normal-opening.sav").string();
    const auto state=world.checksum();const auto objectives=campaignObjDone;
    bool exact=saveGameFile(checkpoint.c_str());for(int i=0;i<20;++i)logic();const auto continuation=world.checksum();
    exact &= loadGameFile(checkpoint.c_str()) && world.checksum()==state && campaignMission==mission
        && campaignDifficulty==2 && campaignObjDone==objectives;
    for(int i=0;i<20;++i)logic();
    check(exact && continuation==world.checksum(),"an actual campaign application save restores the opening and its next 20 ticks exactly");
    while(world.tick<2400 && (world.tick<1650 || world.nativeEvents.inputLocked))logic();
    check(!gameOver && !world.nativeEvents.inputLocked && world.valid(hero()),
        "the source opening runs to its authored input unlock with the original Tanya alive");
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);render();shotAtDisplaySize=true;
        shotFile=(std::filesystem::path(outputDir)/("normal-opening-"+std::to_string(size.x)+".png")).string();
        const auto before=world.checksum();render();check(world.checksum()==before,"the normal campaign HUD renders without mutating the mission");
    }
    if(world.valid(hero()))world.damage(hero(),10000,1);
    for(int i=0;i<6*LOGIC_FPS && !gameOver;++i)logic();
    check(gameOver && !victory && world.nativeEvents.outcome==NativeMissionOutcome::Defeat
        && !campaignObjDone.back(),"the real tagged Tanya death reaches the original failure chain and leaves her survival objective incomplete");
    click(556,196);
    check(campaignMission==mission && world.tick==0 && !gameOver && world.valid(hero())
        && std::none_of(campaignObjDone.begin(),campaignObjDone.end(),[](bool done){return done;}),
        "the normal result screen's Play Again button restarts the same original mission with clean objectives");
    showMenu=true;render();click(556,364);
    check(phase==Phase::MainMenu,"the normal pause menu returns from the campaign to the main menu");
    sim=SimInput{};progress.restore();campaignLoadProgress();
    std::printf("NATIVE_CAMPAIGN failures=%d\n",failures);return failures;
}
