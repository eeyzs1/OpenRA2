#include "game_regression.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace {
std::vector<char> bytes(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
}

int GameRegression::nativeGlobalAppRegression(const char* outputDir) {
    const std::string directory=outputDir;
    for(const char* edition:{"ra2","yr"})if(!FileExists((directory+"/"+edition+"-profile.ini").c_str()))return 77;
    int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("GLOBAL_APP %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    for(const char* edition:{"ra2","yr"}) {
        const auto profile=bytes(directory+"/"+edition+"-profile.ini");
        const World::RulesSnapshot snapshot{std::string(profile.begin(),profile.end())};
        selectRulesChoice(std::string(edition)=="yr"?RulesChoice::YR:RulesChoice::RA2);
        cfgCountry=(int)Country::America;cfgAI=1;aiCountry[0]=(int)Country::Russia;aiDiff[0]=1;
        cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        cfgCrates=false;cfgAlliance=false;
        newGame(1701,&snapshot);
        const bool started=phase==Phase::InGame && world.nativeSceneVersion==41 && world.nativeAIRules.hasOriginalNearbyGeometry() && world.nativeAIRules.hasOriginalSourceCoordinates() && world.nativeAIRules.isGlobal()
            && ais.size()==1 && matchSetup;
        check(started,"normal newGame installs an explicit experimental profile without replacing installed profiles");
        if(!started)continue;
        bool deployed=false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0 && world.ents[id].utype==UnitType::MCV) {
            World::Cmd command{};command.type=World::Cmd::Deploy;command.ids={id};world.applyCmd(0,command);
            deployed=std::any_of(world.ents.begin(),world.ents.end(),[](const auto& actor){
                return actor.alive && actor.player==0 && actor.isBuilding && actor.btype==BldType::ConYard;});break;
        }
        check(deployed,"the human uses the public deploy command in the actual application match");
        const auto path=(std::filesystem::path(directory)/"app"/edition).string();std::filesystem::create_directories(path);
        std::map<int,std::vector<char>> uninterrupted;
        const auto save=[&](const std::string& name){
            const auto file=path+"/"+name+".app11";
            return saveGameFile(file.c_str())?bytes(file):std::vector<char>{};
        };
        uninterrupted[0]=save("tick-0");
        bool ran=true,configured=false;
        for(int offset=1;offset<=6200;++offset) {
            logic();ran &= world.tick==(uint64_t)offset && phase==Phase::InGame && !gameOver;
            if(offset==120 && world.nativeAI.active && world.nativeAI.houses.size()==2) {
                const auto& source=world.nativeAI.houses[1];
                configured=source.sourceConfigured && source.enabled && source.production && source.sourceDifficulty==1
                    && source.sourceEnemy==0 && !world.nativeAI.houses[0].sourceConfigured && !world.nativeEvents.inputLocked;
            }
            if(offset==120 || offset==3000 || offset==6000 || offset==6200)uninterrupted[offset]=save("tick-"+std::to_string(offset));
        }
        check(ran && world.harvestedIncome[1]>0 && world.nativeAI.houses[1].sourceConfigured,
            "actual Game.logic advances 6200 ticks with source scheduling and ordinary economic recovery");
        check(configured,"the ordinary bot update configures the Normal source house while the human remains controllable");
        check(uninterrupted.size()==5 && std::all_of(uninterrupted.begin(),uninterrupted.end(),[](const auto& entry){
            return entry.second.size()>8 && std::string(entry.second.data(),8)=="RA2GAM11";}),
            "actual App11 snapshots include the embedded source graph, ordinary bots and match setup");
        bool zero=true,continued=true;
        for(int start:{0,3000}) {
            const auto input=path+"/tick-"+std::to_string(start)+".app11";
            const bool loaded=loadGameFile(input.c_str());
            zero &= loaded && save("resaved-"+std::to_string(start))==uninterrupted[start];
            if(!loaded){continued=false;continue;}
            for(int offset=1;offset<=3000;++offset) {
                logic();continued &= world.tick==(uint64_t)(start+offset);
                if(uninterrupted.contains(start+offset))continued &= save("resumed-"+std::to_string(start+offset))==uninterrupted[start+offset];
            }
        }
        check(zero,"reading actual App11 and immediately resaving preserves every byte");
        check(continued,"resumed Game.logic matches uninterrupted App11 bytes at 120, 3000 and 6000 ticks");
        // This window checks application integration and persistence. It does
        // not assert a naturally completed attack or a full original match.
    }
    return failures;
}
