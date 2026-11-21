#include "sfx/announcer.h"
#include "sfx/voices.h"
#include "core/content.h"
#include "core/ini.h"
#include <algorithm>
#include <cmath>

namespace {
std::string folded(std::string_view text) {
    std::string key(text);for(char& c:key)if(c>='A' && c<='Z')c+='a'-'A';return key;
}
}
AnnouncerBank g_announcer;
void AnnouncerBank::init() {
    shutdown();cues.resize((int)EvaCue::COUNT);
    std::map<std::string,std::map<std::string,std::string>> sections;
    for(const auto& path:contentResolveStack("assets/sfx/announcer/manifest.ini")) {
        Ini ini;if(!ini.load(path.c_str()))continue;
        for(const auto& section:ini.sections)for(const auto& [key,value]:section.kv)sections[section.name][key]=value;
    }
    auto fail=[&](const std::string& reason){++missing;TraceLog(LOG_ERROR,"ANNOUNCER-MISSING %s",reason.c_str());};
    if(sections["Meta"]["Version"]!="1"){fail("native announcer manifest");return;}
    for(int type=1;type<(int)EvaCue::COUNT;++type)nativeCueIndices[folded(evaCueName((EvaCue)type))]=type;
    const char* sides[]={"Allied","Soviet","China","Yuri"};
    for(const auto& [section,fields]:sections) {
        if(!section.starts_with("Cue."))continue;
        const std::string name=section.substr(4),key=folded(name);int type=0;
        if(auto found=nativeCueIndices.find(key);found!=nativeCueIndices.end())type=found->second;
        else {type=(int)cues.size();nativeCueIndices[key]=type;cues.emplace_back();}
        auto& cue=cues[type];if(!cue.key.empty()){fail(section+":duplicate key");continue;}cue.key=name;
        auto text=[&](const char* field){auto value=fields.find(field);return value==fields.end()?std::string{}:value->second;};
        cue.priority=Ini::toInt(text("Priority").c_str(),-1);cue.queued=text("Queued")=="1";
        if(cue.priority<0 || cue.priority>3){fail(section+":priority");continue;}
        for(int side=0;side<4;++side) {
            const std::string sample=text(sides[side]);
            if(sample.size()>64 || !std::all_of(sample.begin(),sample.end(),[](char c){return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='_';})) {
                fail(section+":sample");continue;
            }
            cue.samples[side]=sample;
        }
    }
    // Core EVA stays preloaded. Authored mission recordings load before that
    // mission starts, rather than allocating every campaign's audio at boot.
    for(int type=1;type<(int)EvaCue::COUNT;++type) {
        if(cues[type].key.empty()){fail(std::string(evaCueName((EvaCue)type)));continue;}
        for(int side=0;side<4;++side)if(!cues[type].samples[side].empty())loadClip(type,side);
    }
    valid=missing==0;TraceLog(valid?LOG_INFO:LOG_ERROR,"NATIVE ANNOUNCER: clips=%d missing=%d",(int)clips.size(),missing);
}
int AnnouncerBank::nativeIndex(std::string_view key) const {
    const auto found=nativeCueIndices.find(folded(key));return found==nativeCueIndices.end()?-1:found->second;
}
bool AnnouncerBank::loadClip(int type,int faction) {
    if(type<=0 || type>=(int)cues.size() || faction<0 || faction>=4)return false;
    auto& cue=cues[type];auto& index=cue.clips[faction];if(index!=-1)return index>=0;
    const auto& sample=cue.samples[faction];if(sample.empty())return false;
    if(auto prior=loadedClips.find(sample);prior!=loadedClips.end()){index=prior->second;return true;}
    index=-2;const auto path=contentResolve(("assets/sfx/announcer/"+sample+".wav").c_str());
    const auto fail=[&](){++missing;TraceLog(LOG_ERROR,"ANNOUNCER-MISSING %s",sample.c_str());return false;};
    if(path.empty())return fail();
    Wave wave=LoadWave(path.c_str());
    if(!wave.data || !wave.frameCount || !wave.sampleRate || wave.frameCount>(uint64_t)wave.sampleRate*300) {
        if(wave.data)UnloadWave(wave);return fail();
    }
    Clip clip;clip.name=sample;clip.duration=(double)wave.frameCount/wave.sampleRate;
    if(IsAudioDeviceReady())clip.sound=LoadSoundFromWave(wave);UnloadWave(wave);
    if(IsAudioDeviceReady() && !clip.sound.stream.buffer)return fail();
    index=(int)clips.size();loadedClips[sample]=index;clips.push_back(std::move(clip));return true;
}
bool AnnouncerBank::preloadNative(std::string_view key,int faction) {return valid && loadClip(nativeIndex(key),faction);}
bool AnnouncerBank::available(EvaCue cue,int faction) const {
    return valid && cue>EvaCue::None && cue<EvaCue::COUNT && faction>=0 && faction<4 && cues[(int)cue].clips[faction]>=0;
}
bool AnnouncerBank::playing() const {return active>=0 && clips[active].sound.stream.buffer && IsSoundPlaying(clips[active].sound);}
void AnnouncerBank::start(int type,int faction,double now) {
    if(active>=0 && clips[active].sound.stream.buffer)StopSound(clips[active].sound);
    const auto& info=cues[type];active=info.clips[faction];activeCue=type;const auto& clip=clips[active];
    if(info.priority>=2)g_unitVoices.reset();
    SetSoundVolume(clip.sound,master);SetSoundPitch(clip.sound,1);SetSoundPan(clip.sound,0.5f);PlaySound(clip.sound);
    last.cue=type<(int)EvaCue::COUNT?(EvaCue)type:EvaCue::None;last.key=info.key;
    last.faction=faction;last.priority=info.priority;last.sample=clip.name;last.until=now+clip.duration+0.25;++last.played;
}
bool AnnouncerBank::request(EvaCue cue,int faction,double seconds) {
    return available(cue,faction) && requestIndex((int)cue,faction,seconds);
}
bool AnnouncerBank::requestNative(std::string_view key,int faction,double seconds) {
    const int index=nativeIndex(key);return valid && loadClip(index,faction) && requestIndex(index,faction,seconds);
}
bool AnnouncerBank::requestIndex(int type,int faction,double seconds) {
    if(!IsAudioDeviceReady() || master<=0.001f || !clips[cues[type].clips[faction]].sound.stream.buffer)return false;
    const double now=seconds<0?GetTime():seconds;if(!std::isfinite(now))return false;
    update(now);
    if((active>=0 && activeCue==type) || std::any_of(pending.begin(),pending.end(),[&](const Pending& item){return item.cue==type;})) {++last.dropped;return true;}
    const auto& info=cues[type];
    if(!info.queued) {
        if(active<0 || info.priority>last.priority)start(type,faction,now);else ++last.dropped;
        return true;
    }
    if(pending.size()>=12) {
        auto lowest=std::min_element(pending.begin(),pending.end(),[&](const Pending& a,const Pending& b){return cues[a.cue].priority<cues[b.cue].priority;});
        if(cues[lowest->cue].priority>=info.priority){++last.dropped;return true;}
        pending.erase(lowest);++last.dropped;
    }
    pending.push_back({type,faction,now+10});update(now);return true;
}
void AnnouncerBank::update(double seconds) {
    if(!valid || !IsAudioDeviceReady())return;const double now=seconds<0?GetTime():seconds;
    if(active>=0 && (now>=last.until || !playing())){StopSound(clips[active].sound);active=-1;activeCue=0;}
    std::erase_if(pending,[&](const Pending& item){if(item.expiry>=now)return false;++last.dropped;return true;});
    if(active>=0 || pending.empty() || master<=0.001f)return;
    auto next=std::max_element(pending.begin(),pending.end(),[&](const Pending& a,const Pending& b){return cues[a.cue].priority<cues[b.cue].priority;});
    const auto request=*next;pending.erase(next);start(request.cue,request.faction,now);
}
void AnnouncerBank::cancel(EvaCue cue) {std::erase_if(pending,[&](const Pending& item){return item.cue==(int)cue;});}
void AnnouncerBank::reset() {
    if(active>=0 && clips[active].sound.stream.buffer)StopSound(clips[active].sound);active=-1;activeCue=0;pending.clear();
}
void AnnouncerBank::setMasterVolume(float volume) {
    master=std::clamp(volume,0.f,1.f);if(master<=0.001f)reset();else if(active>=0)SetSoundVolume(clips[active].sound,master);
}
void AnnouncerBank::shutdown() {
    reset();for(auto& clip:clips)if(clip.sound.stream.buffer)UnloadSound(clip.sound);
    clips.clear();cues.clear();nativeCueIndices.clear();loadedClips.clear();last={};valid=false;missing=0;
}
