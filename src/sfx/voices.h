#pragma once
#include "game/data.h"
#include "game/presentation_events.h"
#include "raylib.h"
#include <array>
#include <span>
#include <string>
#include <vector>

enum class UnitVoiceAction { Select, Move, Attack, Harvest, Deploy, Special, COUNT };

// Read-only playback diagnostics. None of this is simulation state.
struct UnitVoicePlayback {
    uint64_t played=0,suppressed=0;
    int unitType=-1,faction=-1;
    UnitVoiceAction action=UnitVoiceAction::Select;
    std::string sample;
    float volume=0,pitch=1;
};

class UnitVoiceBank {
public:
    void init();
    void shutdown();
    void reset();
    void setMasterVolume(float volume);
    bool available(int type,int faction,UnitVoiceAction action) const;
    bool play(std::span<const OrderActor> actors,UnitVoiceAction action,double seconds=-1);
    bool playing() const;
    int missingCount() const{return missing;}
    size_t clipCount() const{return clips.size();}
    const UnitVoicePlayback& playback() const{return last;}
private:
    static constexpr int ACTIONS=(int)UnitVoiceAction::COUNT;
    struct Clip {std::string name;Sound sound{};};
    struct Group {std::vector<int> clips;float volume=1;int priority=2,previous=-1;};
    struct Profile {std::array<std::vector<int>,ACTIONS> groups;};
    std::array<std::array<Profile,4>,(int)UnitType::COUNT> profiles;
    std::vector<Clip> clips;
    std::vector<Group> groups;
    UnitVoicePlayback last;
    Rng random{0x756e6974766f6963ull};
    int missing=0,active=-1;
    float master=1;
    double lastTime=-1e9;
    bool valid=false;
};

extern UnitVoiceBank g_unitVoices;
