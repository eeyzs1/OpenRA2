#include "game_regression.h"
#include "game/campaign_save.h"
#include "game/script.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

int GameRegression::campaignSaveRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* description){std::printf("CAMPAIGN_SAVE %s %s\n",ok?"PASS":"FAIL",description);failures+=!ok;};
    if(missionTable().size()!=1 || missionTable()[0].nameEn!="Campaign persistence fixture (not a recreated mission)") {
        check(false,"run with --mod tests/fixtures/campaign-save; no user campaign is modified");return failures;
    }
    const auto stem=std::filesystem::temp_directory_path()/
        ("openra2-campaign-save-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto encoded=stem.u8string();const std::string path(encoded.begin(),encoded.end()),bad=path+".bad",timeout=path+".timeout";
    auto readBytes=[](const std::string& name){
        std::ifstream file(std::filesystem::u8path(name),std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),{});
    };
    auto writeBytes=[](const std::string& name,const std::vector<uint8_t>& bytes){
        std::ofstream file(std::filesystem::u8path(name),std::ios::binary);file.write((const char*)bytes.data(),bytes.size());return !!file;
    };
    struct Trace {
        uint32_t world;
        std::vector<SkirmishAI> ais;
        CampaignSnapshot campaign;
        std::vector<uint8_t> triggers;
        size_t wave;
        std::string objective;
        ScriptSnapshot script;
        bool operator==(const Trace&) const = default;
    };
    auto observe=[&]() {
        Trace result{world.checksum(),ais,{campaignDifficulty,campRuntime,campaignObjDone,gameOver,victory},{},nextWave,objectiveText,{}};
        for(const auto& trigger:missionTriggers)result.triggers.push_back((uint8_t)(trigger.fired | (trigger.armed<<1) | (trigger.enabled<<2)));
        if(!g_script.captureSnapshot(result.script))++failures;
        return result;
    };
    campaignDifficulty=2;cfgCrates=false;cfgAlliance=false;
    newCampaignGame(0,false);
    for(auto& cell:world.map.cells)cell=Cell{};
    g_script.init(&world,1,0);g_script.onGameStart();
    for(int tick=0;tick<80;++tick)logic();
    check(!gameOver && campRuntime.phase==2 && campRuntime.timerDeadline==910 && campRuntime.timerVisible
        && campaignObjDone==std::vector<bool>({true,false}) && missionTriggers[4].enabled && missionTriggers[6].armed,
        "real triggers establish a phase, countdown, partial objective, enabled tag and armed loss condition");
    const Trace saved=observe();
    const bool written=saveGameFile(path.c_str());const auto bytes=readBytes(path);
    check(written && bytes.size()>8 && !memcmp(bytes.data(),"RA2GAM11",8),"complete mission and declared Lua state write in v11");
    if(!written)return failures+1;
    std::vector<Trace> reference;
    while(!gameOver && reference.size()<500){logic();reference.push_back(observe());}
    check(gameOver && victory && nextWave==2 && campaignObjDone==std::vector<bool>({true,true}),
        "uninterrupted mission spawns both waves, executes Lua action and wins through its final objective");
    campaignDifficulty=0;campRuntime={};campaignObjDone.clear();missionTriggers[4].enabled=false;
    const bool loaded=loadGameFile(path.c_str());
    check(loaded && g_script.boundTo(&world) && observe()==saved,
        "load restores difficulty, every trigger flag, objectives, timer, phase and private Lua data without replaying OnGameStart");
    bool same=loaded;
    if(loaded)for(const auto& expected:reference){logic();same &= observe()==expected;}
    check(same && gameOver && victory,"resumed mission matches every tick, including funds, units, waves, Lua counters and win tick");
    check(saveGameFile(timeout.c_str()) && loadGameFile(timeout.c_str()) && gameOver && victory,
        "loading a completed campaign preserves victory");

    auto reject=[&](const std::vector<uint8_t>& corrupted,const char* label) {
        const Trace before=observe();
        check(writeBytes(bad,corrupted) && !loadGameFile(bad.c_str()) && g_script.boundTo(&world) && observe()==before,label);
    };
    auto corrupted=bytes;corrupted.pop_back();reject(corrupted,"truncated world preserves the live mission and running Lua VM");
    corrupted=bytes;
    const std::string marker="RA2CAMP1";
    auto section=std::search(corrupted.begin(),corrupted.end(),marker.begin(),marker.end());
    if(section!=corrupted.end()){
        section[8]^=1;reject(corrupted,"changed mission identity is rejected before replacing the current match");
    }else check(false,"campaign section exists");
    auto replaceLast=[&](const std::string& before,const std::string& after,const char* label) {
        auto changed=bytes;auto found=std::find_end(changed.begin(),changed.end(),before.begin(),before.end());
        if(found==changed.end() || before.size()!=after.size()){check(false,label);return;}
        std::copy(after.begin(),after.end(),found);reject(changed,label);
    };
    replaceLast("restore_ok","restore_no","OnLoad errors leave the current World, mission and Lua variables untouched");
    replaceLast("restore_ok","write_cash","OnLoad cannot change staged or live match funds");
    replaceLast("restore_ok","write_game","OnLoad cannot invoke live Game victory callbacks");
    replaceLast("PERSISTENCE_FIXTURE_SCRIPT","PERSISTENCE_FIXTURE_SCRIPX","changed saved source is rejected; saves never execute embedded source or bytecode");

    const uint32_t fundsBefore=world.checksum();
    g_script.onTrigger("missing_load");
    check(!saveGameFile(path.c_str()) && readBytes(path)==bytes && world.checksum()==fundsBefore,
        "scripts without the persistence hook pair fail visibly instead of producing an unusable save");
    g_script.onTrigger("normal");
    g_script.onTrigger("invalid_save");
    check(!saveGameFile(path.c_str()) && readBytes(path)==bytes && world.checksum()==fundsBefore,
        "unsupported declared Lua data fails save and retains the previous complete file");
    g_script.onTrigger("write_save");
    check(!saveGameFile(path.c_str()) && readBytes(path)==bytes && world.checksum()==fundsBefore,
        "OnSave cannot mutate the match while taking a snapshot");
    g_script.onTrigger("normal");
    g_script.onTrigger("reject_load");
    check(!saveGameFile(path.c_str()) && readBytes(path)==bytes && world.checksum()==fundsBefore,
        "save preflight rejects data its own OnLoad cannot restore");
    g_script.onTrigger("normal");
    const Credits originalFunds=world.players[0].money;world.players[0].money=777777;
    const auto beforeTopLevel=observe();
    check(!saveGameFile(path.c_str()) && readBytes(path)==bytes && observe()==beforeTopLevel,
        "restore initialization cannot repeat top-level grants or create a non-resumable save");
    world.players[0].money=originalFunds;

    check(loadGameFile(path.c_str()),"original save remains usable after failed writes and reads");
    campRuntime.winOnAllPrimary=false;campRuntime.startTimer((int)world.tick+45,true);
    const auto deadline=campRuntime.timerDeadline;
    check(saveGameFile(timeout.c_str()),"active timeout branch saves");
    for(int tick=0;tick<45;++tick)logic();const Trace expired=observe();
    check(gameOver && !victory && world.tick==(uint64_t)deadline,"mission loses at the actual saved deadline");
    const bool timerLoaded=loadGameFile(timeout.c_str());
    bool timerSame=timerLoaded && !gameOver && campRuntime.timerDeadline==deadline;
    if(timerLoaded)for(int tick=0;tick<45;++tick)logic();
    timerSame &= observe()==expired;
    check(timerSame,"timeout resume preserves the remaining time and exact defeat state");
    check(saveGameFile(timeout.c_str()) && loadGameFile(timeout.c_str()) && gameOver && !victory,
        "loading an ended campaign keeps its result rather than reopening the mission");
    cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgGameMode=(int)SkirmishMode::Battle;
    newGame(20261114);
    check(!g_script.enabled() && campaignMission<0 && campaignObjDone.empty() && saveGameFile(timeout.c_str())
        && loadGameFile(timeout.c_str()) && !g_script.enabled() && campRuntime==CampaignRuntime{} && campaignObjDone.empty(),
        "switching to skirmish and loading it cannot revive mission timers, objectives or installed campaign scripts");
    std::error_code error;
    for(const auto& name:{path,bad,timeout,path+".tmp",timeout+".tmp"})std::filesystem::remove(std::filesystem::u8path(name),error);
    std::printf("CAMPAIGN_SAVE SUMMARY failures=%d compared_ticks=%zu\n",failures,reference.size());
    return failures;
}
