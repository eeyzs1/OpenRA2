#pragma once
#include "game/eva_cues.h"
#include "raylib.h"
#include <array>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <vector>

struct AnnouncerPlayback {
    EvaCue cue=EvaCue::None;
    int faction=-1,priority=-1;
    uint64_t played=0,dropped=0;
    std::string sample;
    std::string key;
    double until=0;
};
class AnnouncerBank {
public:
    void init();
    void shutdown();
    void reset();
    void cancel(EvaCue cue);
    void setMasterVolume(float volume);
    bool available(EvaCue cue,int faction) const;
    bool request(EvaCue cue,int faction,double seconds=-1);
    bool preloadNative(std::string_view key,int faction);
    bool requestNative(std::string_view key,int faction,double seconds=-1);
    void update(double seconds=-1);
    bool playing() const;
    bool blocksUnitSpeech() const{return playing() && last.priority>=2;}
    int missingCount() const{return missing;}
    size_t pendingCount() const{return pending.size();}
    size_t clipCount() const{return clips.size();}
    const AnnouncerPlayback& playback() const{return last;}
private:
    struct Clip {std::string name;Sound sound{};double duration=0;};
    struct Cue {
        std::string key;std::array<std::string,4> samples;
        std::array<int,4> clips{-1,-1,-1,-1};int priority=1;bool queued=false;
    };
    struct Pending {int cue;int faction;double expiry;};
    std::vector<Cue> cues;
    std::map<std::string,int> nativeCueIndices,loadedClips;
    std::vector<Clip> clips;
    std::deque<Pending> pending;
    AnnouncerPlayback last;
    float master=1;
    int active=-1,missing=0;
    bool valid=false;
    int activeCue=0;
    int nativeIndex(std::string_view key) const;
    bool loadClip(int cue,int faction);
    bool requestIndex(int cue,int faction,double seconds);
    void start(int cue,int faction,double seconds);
};
extern AnnouncerBank g_announcer;
