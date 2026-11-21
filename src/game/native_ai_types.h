#pragma once
#include "game/native_event_types.h"
#include <map>

struct NativeAITriggerDefinition {
    std::string id,name,conditionObject,country;
    int first=-1,second=-1,house=-1,condition=-1,operand=0,comparison=0,side=0,techLevel=0;
    double weight=1,minimum=1,maximum=1;
    bool enabled=false,skirmish=false,baseDefense=false;
    std::array<bool,3> difficulty{true,true,true};
};
struct NativeRecruitState {bool normal=true,automatic=true;int group=-1;};
struct NativeAIRules {
    bool available=false,ignoreGlobal=true,useMinDefense=true;
    const NativeGlobalAIContract* globalContract=nullptr;
    bool isGlobal() const {return globalContract!=nullptr;}
    bool hasOriginalTeamControl() const {return globalContract && globalContract->houseControl;}
    bool hasOriginalNearbyGeometry() const {return globalContract && globalContract->nearbyGeometry;}
    bool hasOriginalSourceCoordinates() const {return globalContract && globalContract->sourceCoordinates;}
    float minorSuperReady=.7f;
    int safeDistance=20;
    std::map<std::string,int> countrySides;
    std::map<std::string,int> leadership;
    std::map<std::string,int> nearbySpeeds;
    std::map<int,bool> nearbyWalls; // original OverlayTypes index -> Crushable
    std::vector<std::string> baseBuildings;
    std::array<int,3> delays{},cap{},minimumDefense{},maximumDefense{},fillEarliest{};
    double successDelta=1,failureDelta=-1,historyCoefficient=1;
    std::vector<std::string> countries;
    std::vector<int> ratios;
    std::vector<NativeAITriggerDefinition> triggers;
    std::map<std::string,NativeRecruitState> originalRecruit;
};
struct NativeAIHouseState {
    bool enabled=false,production=false;int ratio=100;
    uint64_t nextPass=0,nextChoice=0;
    std::array<int,4> wanted{-1,-1,-1,-1};
    bool sourceConfigured=false;
    int sourceDifficulty=1,sourceEnemy=-1;
};
struct NativeAITriggerState {double weight=1;uint64_t runs=0,successes=0;};
struct NativeAIFactoryJob {
    int actor=-1,house=-1,type=-1,progress=0,work=0,paid=0;
    std::string object;
};
struct NativeHouseControlState {
    bool initialized=false;
    Vec2i center{}; // Source leptons, (0,0) is the source empty center.
};
struct NativeTeamFootState {uint64_t joined=0;bool leader=false;};
struct NativeAIState {
    bool active=false;
    std::vector<NativeAIHouseState> houses;
    std::vector<NativeAITriggerState> triggers;
    std::vector<uint8_t> autocreate;
    std::vector<uint64_t> teamBorn;
    std::map<std::string,NativeRecruitState> recruit;
    std::vector<NativeAIFactoryJob> jobs;
    std::map<std::string,int> memberTeams; // Source object keys survive actual boarding and unloading.
    std::vector<NativeHouseControlState> centers;
    std::map<std::string,NativeTeamFootState> feet;
    uint64_t nextFoot=0;
    std::vector<uint8_t> underStrength;
};
bool parseNativeAIRules(const Ini& source,const NativeEventDefinition* events,int houses,NativeAIRules& out);
