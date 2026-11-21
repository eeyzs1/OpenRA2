#include "sfx/music.h"
#include "core/content.h"
#include "core/ini.h"
#include <algorithm>
#include <map>

namespace {
constexpr const char* SCENES[]={"Menu","Battle","Score","Loading","Credits"};
}
void NativeMusicPlayer::init() {
    shutdown();
    std::string edition="ra2";
    for(const auto& path:contentResolveStack("assets/music/music.ini")) {
        Ini settings;if(!settings.load(path.c_str()))continue;
        if(const char* value=settings.get("Music","Edition"))edition=value;
        if(const char* value=settings.get("Music","Volume"))authoredVolume=std::clamp(Ini::toInt(value,35),0,100)/100.f;
    }
    if(edition!="ra2" && edition!="yr"){++missing;return;}
    std::map<std::string,std::vector<std::string>> sections;bool version=false;
    for(const auto& path:contentResolveStack("assets/music/native/manifest.ini")) {
        Ini manifest;if(!manifest.load(path.c_str()))continue;
        if(const char* value=manifest.get("Meta","Version"))version=std::string(value)=="1";
        for(const auto& section:manifest.sections)if(section.name.starts_with(edition+".")) {
            std::vector<std::string> tracks;
            for(const auto& [key,value]:section.kv)if(key=="Track")tracks.push_back(value);
            sections[section.name]=std::move(tracks);
        }
    }
    if(!version){++missing;TraceLog(LOG_ERROR,"BGM-MISSING: native soundtrack manifest");return;}
    for(int scene=0;scene<(int)MusicScene::COUNT;++scene) {
        for(const auto& file:sections[edition+"."+SCENES[scene]]) {
            if(file.empty() || file.find_first_of("/\\")!=std::string::npos || file.find("..")!=std::string::npos){++missing;continue;}
            const auto path=contentResolve(("assets/music/native/"+file).c_str());
            if(path.empty()){++missing;TraceLog(LOG_ERROR,"BGM-MISSING: %s",file.c_str());}
            else lists[scene].push_back(path);
        }
    }
    for(MusicScene required:{MusicScene::Menu,MusicScene::Battle,MusicScene::Score})if(lists[(int)required].empty())++missing;
    configured=missing==0;
    if(configured && IsAudioDeviceReady())start(0);
}
void NativeMusicPlayer::start(int track) {
    if(loaded){StopMusicStream(stream);UnloadMusicStream(stream);loaded=false;stream={};}
    const auto& list=lists[(int)context];
    if(track<0 || track>=(int)list.size() || !IsAudioDeviceReady())return;
    stream=LoadMusicStream(list[track].c_str());
    if(!stream.stream.buffer){++missing;TraceLog(LOG_ERROR,"BGM-MISSING: cannot decode %s",list[track].c_str());return;}
    index=track;current=list[track];loaded=true;stream.looping=context!=MusicScene::Battle;
    SetMusicVolume(stream,master*authoredVolume);
    started=false;if(on){PlayMusicStream(stream);started=true;}
    TraceLog(LOG_INFO,"NATIVE MUSIC: scene=%s file=%s length=%.2f",SCENES[(int)context],current.c_str(),GetMusicTimeLength(stream));
}
void NativeMusicPlayer::setScene(MusicScene scene) {
    if(!configured || (unsigned)scene>=(unsigned)MusicScene::COUNT || lists[(int)scene].empty())return;
    if(scene==context && loaded)return;
    context=scene;const auto count=lists[(int)context].size();start(count>1?(int)(random.next()%count):0);
}
void NativeMusicPlayer::next() {
    if(!configured || lists[(int)context].empty())return;
    const int count=(int)lists[(int)context].size();int next=0;
    if(count>1){next=(int)(random.next()%(count-1));if(next>=index)++next;}
    start(next);
}
void NativeMusicPlayer::update() {
    if(!loaded || !on)return;
    UpdateMusicStream(stream);
    if(context==MusicScene::Battle && !IsMusicStreamPlaying(stream))next();
}
void NativeMusicPlayer::toggle() {
    on=!on;if(!loaded)return;
    if(on){if(started)ResumeMusicStream(stream);else {PlayMusicStream(stream);started=true;}}
    else PauseMusicStream(stream);
}
void NativeMusicPlayer::setMasterVolume(float volume) {
    master=std::clamp(volume,0.f,1.f);if(loaded)SetMusicVolume(stream,master*authoredVolume);
}
void NativeMusicPlayer::shutdown() {
    if(loaded){StopMusicStream(stream);UnloadMusicStream(stream);}
    stream={};loaded=false;configured=false;started=false;index=-1;missing=0;context=MusicScene::Menu;current.clear();
    for(auto& list:lists)list.clear();on=true;authoredVolume=0.35f;random=Rng(0x6e61746976656d75ull);
}
