#pragma once
#include "core/ini.h"
#include <array>
#include <cstdint>
#include <vector>

struct NativeRocketWarhead {
    float spread=0,percentAtMax=1;
    std::array<float,11> verses{};
};
struct NativeRocketDefinition {
    int launcher=-1,missile=-1,weaponSlot=0,capacity=2,regenerate=0,reload=0,launchDelay=0,rof=0;
    int pause=0,tilt=0,altitude=0,damage=0,eliteDamage=0,bodyLength=0;
    float range=0,minimumRange=0,pitchInitial=0,pitchFinal=0,turnRate=0,acceleration=0,maxSpeed=0,yawRate=0;
    std::array<float,3> flh{};
    bool lazyCurve=false,noSpawnAlt=false;
    NativeRocketWarhead normal,elite;
};
enum class NativeRocketPhase : uint8_t { Pause,Tilt,Flight };
struct NativeSpawnNode {int missile=-1;uint64_t readyAt=0;};
struct NativeLauncherState {
    int actor=-1,definition=-1,target=-1;uint64_t nextLaunch=0;
    std::vector<NativeSpawnNode> nodes;
};
struct NativeRocketState {
    int actor=-1,definition=-1,source=-1,slot=-1;
    NativeRocketPhase phase=NativeRocketPhase::Pause;int phaseTicks=0;
    float altitude=0,speed=0,pitch=0,heading=0,destinationX=0,destinationY=0,destinationZ=0;
    bool elite=false,cruised=false,diving=false;
};
bool parseNativeRocketDefinitions(const Ini& source,int types,std::vector<NativeRocketDefinition>& destination);
bool nativeSourceNumber(const char* text,float& value,float low,float high);
bool nativeSourceVerses(const char* text,std::array<float,11>& destination);
