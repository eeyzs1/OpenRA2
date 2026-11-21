#pragma once
#include "core/util.h"
#include "raylib.h"
#include <array>
#include <string>
#include <vector>

enum class MusicScene { Menu, Battle, Score, Loading, Credits, COUNT };
class NativeMusicPlayer {
public:
    void init();
    void shutdown();
    void update();
    void setScene(MusicScene scene);
    void next();
    void toggle();
    void setMasterVolume(float volume);
    bool ready() const{return loaded;}
    bool enabled() const{return on;}
    bool playing() const{return loaded && IsMusicStreamPlaying(stream);}
    MusicScene scene() const{return context;}
    const std::string& currentTrack() const{return current;}
    size_t trackCount(MusicScene scene) const{return lists[(int)scene].size();}
    float duration() const{return loaded?GetMusicTimeLength(stream):0;}
    float position() const{return loaded?GetMusicTimePlayed(stream):0;}
    int missingCount() const{return missing;}
private:
    std::array<std::vector<std::string>,(int)MusicScene::COUNT> lists;
    Music stream{};
    std::string current;
    MusicScene context=MusicScene::Menu;
    int index=-1,missing=0;
    bool loaded=false,on=true,configured=false,started=false;
    float master=1,authoredVolume=0.35f;
    Rng random{0x6e61746976656d75ull};
    void start(int track);
};
