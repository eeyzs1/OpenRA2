#pragma once
#include "raylib.h"
#include <cstdint>

// 程序化音效：全部波形由代码合成，无外部素材
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

private:
    static constexpr int ALIAS = 3; // 同音并发数
    Sound snd[(int)Sfx::COUNT][ALIAS]{};
    int rr[(int)Sfx::COUNT]{};
    double last[(int)Sfx::COUNT]{};
    float lisX = 0, lisY = 0;
    bool ok = false;

    Music bgm{};
    bool bgmOk = false;
    bool bgmOn = true;
};

extern SoundBank g_sfx;
