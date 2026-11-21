#include "sfx/voices.h"
#include "sfx/announcer.h"
#include "core/content.h"
#include "core/ini.h"
#include "gfx/assets.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

UnitVoiceBank g_unitVoices;

namespace {
constexpr const char* MANIFEST="assets/sfx/voices/manifest.ini";
constexpr const char* ACTION_NAMES[]={"Select","Move","Attack","Harvest","Deploy","Special"};
constexpr const char* FACTION_NAMES[]={"allies","soviet","china","yuri"};
bool identifier(const std::string& text) {
    return !text.empty() && text.size()<=96 && std::all_of(text.begin(),text.end(),[](unsigned char c){
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';});
}
std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> result;size_t start=0;
    while(start<text.size()) {
        const size_t comma=text.find(',',start);std::string word=text.substr(start,comma-start);
        const auto first=word.find_first_not_of(" \t"),last=word.find_last_not_of(" \t");
        if(first!=std::string::npos)result.push_back(word.substr(first,last-first+1));
        if(comma==std::string::npos)break;start=comma+1;
    }
    return result;
}
}

void UnitVoiceBank::init() {
    shutdown();
    auto fail=[&](const std::string& description){++missing;TraceLog(LOG_ERROR,"VOICE-MISSING %s",description.c_str());};
    // Merge named fields through the same content roots as sprites and SFX.
    std::map<std::string,std::map<std::string,std::string>> sections;
    const auto paths=contentResolveStack(MANIFEST);
    if(paths.empty()){fail(MANIFEST);return;}
    for(const auto& path:paths) {
        Ini ini;if(!ini.load(path.c_str())){fail(path);continue;}
        for(const auto& section:ini.sections)for(const auto& [key,value]:section.kv)sections[section.name][key]=value;
    }
    if(sections["Meta"]["Version"]!="1"){fail("unit voice manifest version must be 1");return;}
    std::map<std::string,int> groupIds,clipIds;
    for(const auto& [name,fields]:sections) {
        if(!name.starts_with("Group."))continue;
        const std::string key=name.substr(6);
        if(!identifier(key) || groups.size()>=1024){fail("invalid group "+name);continue;}
        Group group;
        auto field=[&](const char* label,const char* fallback){auto found=fields.find(label);return found==fields.end()?std::string(fallback):found->second;};
        const std::string volume=field("Volume","100"),priority=field("Priority","2");
        char* end=nullptr;const float percent=strtof(volume.c_str(),&end);
        if(end==volume.c_str() || *end || !std::isfinite(percent) || percent<0 || percent>100){fail("invalid volume "+name);continue;}
        group.volume=percent/100;
        const long rank=strtol(priority.c_str(),&end,10);
        if(end==priority.c_str() || *end || rank<0 || rank>4){fail("invalid priority "+name);continue;}
        group.priority=(int)rank;
        const auto samples=split(field("Samples",""));
        if(samples.empty() || samples.size()>64){fail("invalid sample list "+name);continue;}
        for(const auto& sample:samples) {
            if(!identifier(sample)){fail("invalid sample identifier "+sample);continue;}
            if(auto found=clipIds.find(sample);found!=clipIds.end()){group.clips.push_back(found->second);continue;}
            if(clips.size()>=4096){fail("too many unit voice clips");break;}
            std::string source;
            for(const char* extension:{".wav",".ogg",".mp3"}) {
                source=contentResolve(("assets/sfx/voices/"+sample+extension).c_str());if(!source.empty())break;
            }
            if(source.empty()){fail(sample);continue;}
            Wave wave=LoadWave(source.c_str());
            if(!wave.data || !wave.frameCount || wave.sampleRate<4000 || wave.sampleRate>96000
                || wave.frameCount>(unsigned)wave.sampleRate*30 || wave.channels<1 || wave.channels>2) {
                if(wave.data)UnloadWave(wave);fail("invalid audio "+sample);continue;
            }
            Clip clip;clip.name=sample;
            if(IsAudioDeviceReady())clip.sound=LoadSoundFromWave(wave);
            UnloadWave(wave);
            if(IsAudioDeviceReady() && !clip.sound.stream.buffer){fail("cannot load "+sample);continue;}
            const int index=(int)clips.size();clips.push_back(std::move(clip));clipIds.emplace(sample,index);group.clips.push_back(index);
        }
        groupIds.emplace(key,(int)groups.size());groups.push_back(std::move(group));
    }
    for(int type=0;type<(int)UnitType::COUNT;++type) {
        if((UnitType)type==UnitType::NativeVehicle)continue; // scene-only carrier; never substitute a regular unit's speech
        const std::string unit=std::string("Unit.")+unitAssetName((UnitType)type);
        auto base=sections.find(unit);
        if(base==sections.end()){fail("missing profile "+unit);continue;}
        for(int faction=0;faction<4;++faction) {
            auto sided=sections.find(unit+"."+FACTION_NAMES[faction]);
            for(int action=0;action<ACTIONS;++action) {
                auto fields=base->second;
                if(sided!=sections.end())for(const auto& [key,value]:sided->second)fields[key]=value;
                auto field=fields.find(ACTION_NAMES[action]);
                if(field==fields.end()){fail("missing action "+unit+":"+ACTION_NAMES[action]);continue;}
                for(const auto& key:split(field->second)) {
                    auto found=groupIds.find(key);
                    if(found==groupIds.end())fail("unknown voice group "+key);
                    else profiles[type][faction].groups[action].push_back(found->second);
                }
            }
        }
    }
    valid=missing==0;
    TraceLog(valid?LOG_INFO:LOG_ERROR,"UNIT VOICES: clips=%d groups=%d missing=%d",(int)clips.size(),(int)groups.size(),missing);
}

void UnitVoiceBank::reset() {
    if(active>=0 && clips[active].sound.stream.buffer)StopSound(clips[active].sound);
    active=-1;lastTime=-1e9;
}

void UnitVoiceBank::shutdown() {
    reset();
    for(auto& clip:clips)if(clip.sound.stream.buffer)UnloadSound(clip.sound);
    clips.clear();groups.clear();for(auto& unit:profiles)for(auto& side:unit)for(auto& group:side.groups)group.clear();
    valid=false;missing=0;last={};random=Rng(0x756e6974766f6963ull);
}

bool UnitVoiceBank::available(int type,int faction,UnitVoiceAction action) const {
    return valid && type>=0 && type<(int)UnitType::COUNT && faction>=0 && faction<4 && (unsigned)action<ACTIONS
        && !profiles[type][faction].groups[(int)action].empty();
}

bool UnitVoiceBank::playing() const {
    return active>=0 && clips[active].sound.stream.buffer && IsSoundPlaying(clips[active].sound);
}

void UnitVoiceBank::setMasterVolume(float volume) {
    master=std::clamp(volume,0.f,1.f);
    if(active>=0 && clips[active].sound.stream.buffer) {
        // Keep the active utterance's authored group volume during live changes.
        const float authored=last.volume;SetSoundVolume(clips[active].sound,authored*master);
        if(master<=0.001f)reset();
    }
}

bool UnitVoiceBank::play(std::span<const OrderActor> actors,UnitVoiceAction action,double seconds) {
    if(!valid || !IsAudioDeviceReady() || master<=0.001f || g_announcer.blocksUnitSpeech())return false;
    const double now=seconds<0?GetTime():seconds;
    const double delay=action==UnitVoiceAction::Select?0.20:0.13;
    const bool commandOverSelection=action!=UnitVoiceAction::Select && last.action==UnitVoiceAction::Select;
    if(!std::isfinite(now) || (!commandOverSelection && now-lastTime<delay)
        || (action==UnitVoiceAction::Select && last.action!=UnitVoiceAction::Select && now-lastTime<0.45 && playing())) {
        ++last.suppressed;return false;
    }
    const OrderActor* speaker=nullptr;int rank=-1;
    for(const auto& actor:actors) {
        if(!available(actor.unitType,actor.faction,action))continue;
        for(int id:profiles[actor.unitType][actor.faction].groups[(int)action])if(groups[id].priority>rank) {
            rank=groups[id].priority;speaker=&actor;
        }
    }
    if(!speaker)return false;
    const auto& choices=profiles[speaker->unitType][speaker->faction].groups[(int)action];
    Group& group=groups[choices[random.next()%choices.size()]];
    if(group.clips.empty())return false;
    int pick=(int)(random.next()%group.clips.size());
    if(group.clips.size()>1 && pick==group.previous)pick=(pick+1+(int)(random.next()%(group.clips.size()-1)))%(int)group.clips.size();
    const int clip=group.clips[pick];if(!clips[clip].sound.stream.buffer)return false;
    reset();group.previous=pick;active=clip;lastTime=now;
    Sound sound=clips[clip].sound;
    SetSoundVolume(sound,group.volume*master);SetSoundPitch(sound,1);SetSoundPan(sound,0.5f);PlaySound(sound);
    ++last.played;last.unitType=speaker->unitType;last.faction=speaker->faction;last.action=action;
    last.sample=clips[clip].name;last.volume=group.volume;last.pitch=1;
    return true;
}
