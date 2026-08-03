#pragma once
#include "raylib.h"
#include <cstdint>
#include <vector>
#include <string>

// 音效系统：优先加载 assets/sfx/ 外部音频，缺失时回退到程序合成波形
enum class Sfx : uint8_t {
    Shot = 0,   // 步枪
    Cannon,     // 坦克炮
    Flak,       // 高炮
    Missile,    // 导弹发射
    Explosion,  // 爆炸
    BigExplosion, // 建筑级大爆炸
    Tesla,      // 磁暴电弧
    Prism,      // 光棱光束
    Click,      // UI 点击
    Place,      // 建筑放置
    Ready,      // 生产就绪提示
    Cash,       // 资金到账
    Alarm,      // 低电力警报
    Deploy,     // 基地车展开
    Sell,       // 出售建筑
    NukeLaunch, // 核弹发射警报
    NukeBlast,  // 核爆
    Lightning,  // 闪电劈落
    Storm,      // 风暴起
    IronCurtain,// 铁幕启动
    SWReady,    // 超武就绪
    Crush,      // 坦克碾压
    Eva,        // EVA 播报提示音
    NavalCannon,// 舰炮
    Torpedo,    // 鱼雷发射
    Dig,        // 矿车挖掘研磨
    MirageFire, // 幻影坦克主炮
    RhinoFire,  // 犀牛/99式主炮
    ApocFire,   // 天启双管
    COUNT
};

class SoundBank {
public:
    void init();
    void shutdown();

    // 全局/UI 播放
    void play(Sfx id, float vol = 1.0f);
    // 战场定位播放：按与听者（摄像机中心，瓦片坐标）距离衰减、按水平偏差声像
    void playAt(Sfx id, float tx, float ty);
    void setListener(float tx, float ty) { lisX = tx; lisY = ty; }

    // 程序合成 BGM（进行曲，循环流式播放）
    void initBgm();
    void updateBgm();            // 每帧调用驱动音乐流
    void toggleBgm();
    bool bgmEnabled() const { return bgmOn; }

    // 主音量 0..1（音效+音乐；选项界面热更新，无需重启）
    void setMasterVol(float v);
    float masterVolume() const { return masterVol; }

    // 离线素材生成（--gen-assets）：全量导出 WAV 到 assets/sfx/ + BGM 到 assets/music/；无需音频设备
    bool genSfxAssets(const char* dir);

private:
    static constexpr int ALIAS = 3; // 同音并发数
    Sound snd[(int)Sfx::COUNT][ALIAS]{};
    int rr[(int)Sfx::COUNT]{};
    double last[(int)Sfx::COUNT]{};
    float lisX = 0, lisY = 0;
    bool ok = false;
    float masterVol = 1.0f;

    Music bgm{};
    bool bgmOk = false;
    bool bgmOn = true;
    // 内置合成 BGM 的 WAV 字节：drwav 流式解码只引用不复制该内存，必须与流同生命周期（常驻成员）
    std::vector<unsigned char> bgmMem;
    // 外部音乐播放列表（assets/music/）；空时使用内置合成进行曲
    std::vector<std::string> bgmFiles;
    int bgmIdx = -1;
    bool bgmFromFiles = false;
    void playBgmTrack(int idx);
};

extern SoundBank g_sfx;
