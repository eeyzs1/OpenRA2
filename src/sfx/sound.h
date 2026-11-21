#pragma once
#include "raylib.h"
#include <cstdint>
#include <vector>
#include <string>
#include "sfx/music.h"

// 音效系统：仅加载 assets/sfx/ 外部音频；缺失则记错并拒绝启动（无程序合成回退）
#include "game/presentation_events.h"


class SoundBank {
public:
    void init();
    void shutdown();
    int missingCount() const { return missingAssets; }
    bool assetsOk() const { return missingAssets == 0; } // 仅统计缺失文件；无声卡不算素材失败

    // 全局/UI 播放
    void play(Sfx id, float vol = 1.0f);
    // 战场定位播放：按与听者（摄像机中心，瓦片坐标）距离衰减、按水平偏差声像
    void playAt(Sfx id, float tx, float ty);
    void setListener(float tx, float ty) { lisX = tx; lisY = ty; }

    // 背景音乐：仅播放 assets/music/ 文件；无文件则记错，不合成
    void initBgm();
    void updateBgm();            // 每帧调用驱动音乐流
    void toggleBgm();
    bool bgmEnabled() const { return music.enabled(); }
    bool bgmReady() const { return music.ready(); }
    void setMusicScene(MusicScene scene) { music.setScene(scene); }
    NativeMusicPlayer& musicPlayer(){return music;}

    // 主音量 0..1（音效+音乐；选项界面热更新，无需重启）
    void setMasterVol(float v);
    float masterVolume() const { return masterVol; }

    // 已禁用：禁止程序合成 WAV 写入 assets
    bool genSfxAssets(const char* dir);

private:
    static constexpr int ALIAS = 3; // 同音并发数
    Sound snd[(int)Sfx::COUNT][ALIAS]{};
    int rr[(int)Sfx::COUNT]{};
    double last[(int)Sfx::COUNT]{};
    float lisX = 0, lisY = 0;
    bool ok = false;
    int missingAssets = 0;
    float masterVol = 1.0f;

    NativeMusicPlayer music;
};

extern SoundBank g_sfx;
