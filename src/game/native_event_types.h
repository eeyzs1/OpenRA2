#pragma once
#include "core/ini.h"
#include "game/native_ai_contract.h"
#include "core/util.h"
#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

struct NativeEventCondition { int type=0,kind=0,value=0; };
struct NativeEventAction { int type=0;std::array<std::string,7> args; };
struct NativeTriggerDefinition {
    std::string id,name;int house=-1,linked=-1;bool disabled=false;
    std::array<bool,3> difficulty{true,true,true};
    std::vector<NativeEventCondition> events;
    std::vector<NativeEventAction> actions;
};
struct NativeTagDefinition {std::string id,name;int trigger=-1,persistence=0;};
struct NativeScriptDefinition {std::string id;std::vector<Vec2i> steps;};
struct NativeTaskForceDefinition {std::string id;int group=-1;std::vector<std::pair<int,std::string>> units;};
struct NativeTeamDefinition {
    std::string id,name;int house=-1,tag=-1,script=-1,taskForce=-1,waypoint=-1,max=1,veterancy=1;
    bool loadable=false,recruitable=false,reinforce=false,prebuild=false,aggressive=false,onlyTargetEnemy=false;
    bool autocreate=false,recruiter=false,baseDefense=false;int priority=7,group=-1;
    bool transportsReturn=false;
    bool guardSlower=false;
};
struct NativeDemolitionDefinition {
    bool available=false,wall=false,wood=false,tiberium=false;std::string warhead;
    int bridgeStrength=1500,flightLevel=1500;
    float spread=0,percentAtMax=1;
    std::array<float,11> verses{};
};
struct NativeEventDefinition {
    NativeDemolitionDefinition demolition;
    std::vector<NativeTriggerDefinition> triggers;
    std::vector<NativeTagDefinition> tags;
    std::vector<NativeScriptDefinition> scripts;
    std::vector<NativeTaskForceDefinition> taskForces;
    std::vector<NativeTeamDefinition> teams;
    std::vector<std::pair<int,Vec2i>> waypoints;
    std::vector<std::pair<Vec2i,int>> cells;
    std::vector<std::pair<int,int>> houseNumbers;
    std::vector<std::pair<int,std::string>> buildingTypes;
    std::array<std::vector<std::pair<int,std::string>>,3> mobileTypes;
    std::array<uint8_t,50> initialLocals{};
    int revealRadius=9,playerHouse=0;
    float strayDistance=2;
    float relaxedStrayDistance=3;
    bool fogOfWar=true,orderedActions=false,extendedConditions=false,aiOnly=false;
    const NativeGlobalAIContract* globalContract=nullptr;
    bool hasOriginalTeamControl() const {return globalContract && globalContract->houseControl;}
    bool hasOriginalNearbyGeometry() const {return globalContract && globalContract->nearbyGeometry;}
    bool hasOriginalSourceCoordinates() const {return globalContract && globalContract->sourceCoordinates;}
};
struct NativeTriggerState {
    int definition=-1,tag=-1,blockedAction=-1;
    bool enabled=false,destroyed=false;uint64_t timerStart=0,eventMask=0,firings=0;
};
struct NativeTagState {int definition=-1;bool alive=true;};
struct NativeTeamState {
    int definition=-1,tag=-1,step=0;bool alive=true,reinforced=false;
    uint64_t waitUntil=0;std::vector<int> members;
    bool assembled=false,lineStarted=false,focusFixed=false,advancePending=false,succeeded=false;
    int focus=-1,focusCell=-1,blockedScript=-1;
    int memberHouse=-1,focusLayer=-1;
};
enum class NativeMissionOutcome : uint8_t { Running,Victory,Defeat };
struct NativeEventState {
    bool active=false,inputLocked=false;int difficulty=1;
    NativeMissionOutcome outcome=NativeMissionOutcome::Running;
    uint64_t outcomeAt=0;int outcomeTrigger=-1;
    std::vector<NativeTagState> tags;
    std::vector<NativeTriggerState> triggers;
    std::deque<NativeTeamState> teams; // Script orders may create further teams through cell triggers.
};
// Presentation requests retain source action/arguments. They never stand in
// for successful playback, and they do not affect the simulation checksum.
struct NativeEventCue {
    int action=0,house=-1,value=0,x=-1,y=-1;uint64_t tick=0;
    std::string key;
    std::array<std::string,7> args;
};
bool parseNativeEventDefinitions(const Ini& source,int width,int height,int houses,
    std::shared_ptr<const NativeEventDefinition>& destination);
