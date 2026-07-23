#include "sfx/sound.h"
#include "core/util.h"
#include <cmath>
#include <vector>
#include <cstring>

SoundBank g_sfx;

// ===================== 合成器 =====================
namespace {

constexpr int RATE = 22050;
// PI 宏由 raylib.h 提供

struct Buf {
    std::vector<short> d;
    void alloc(int frames) { d.assign(frames, 0); }
    int frames() const { return (int)d.size(); }
    void mix(int i, float v) {
        if (i < 0 || i >= (int)d.size()) return;
        int x = d[i] + (int)(v * 29000.0f);
        d[i] = (short)clampi(x, -32000, 32000);
    }
};

// 白噪声爆发：一阶低通 + 指数衰减
void noiseBurst(Buf& b, int start, float dur, float decay, float lp, float amp, Rng& rng) {
    int n = (int)(dur * RATE);
    float y = 0;
    for (int i = 0; i < n; i++) {
        float w = rng.unit() * 2.0f - 1.0f;
        y += lp * (w - y);
        float env = expf(-decay * i / (float)RATE);
        b.mix(start + i, y * env * amp);
    }
}

// 正弦扫频：f0→f1 指数扫频 + 指数衰减
void toneSweep(Buf& b, int start, float f0, float f1, float dur, float decay, float amp, bool square = false) {
    int n = (int)(dur * RATE);
    float phase = 0;
    float k = logf(f1 / f0) / n;
    for (int i = 0; i < n; i++) {
        float f = f0 * expf(k * i);
        phase += 2 * PI * f / RATE;
        float s = square ? (sinf(phase) >= 0 ? 0.7f : -0.7f) : sinf(phase);
        float env = expf(-decay * i / (float)RATE);
        b.mix(start + i, s * env * amp);
    }
}

// 定时音叮：指定起始秒的纯音
void blip(Buf& b, float atSec, float freq, float dur, float amp) {
    int start = (int)(atSec * RATE);
    int n = (int)(dur * RATE);
    float phase = 0;
    for (int i = 0; i < n; i++) {
        phase += 2 * PI * freq / RATE;
        float t = (float)i / n;
        float env = t < 0.15f ? t / 0.15f : expf(-6.0f * (t - 0.15f));
        b.mix(start + i, sinf(phase) * env * amp);
    }
}

Buf genSfx(Sfx id) {
    Buf b;
    Rng rng(0xC0FFEE + (uint64_t)id * 7919);
    switch (id) {
        case Sfx::Shot:
            b.alloc((int)(0.10f * RATE));
            noiseBurst(b, 0, 0.09f, 46, 0.85f, 1.0f, rng);
            toneSweep(b, 0, 1900, 700, 0.05f, 42, 0.30f);
            break;
        case Sfx::Cannon:
            b.alloc((int)(0.34f * RATE));
            noiseBurst(b, 0, 0.05f, 34, 1.0f, 0.9f, rng);          // 出膛脆响
            toneSweep(b, 0, 115, 30, 0.32f, 9, 1.0f);              // 低频轰
            noiseBurst(b, 0, 0.22f, 15, 0.30f, 0.55f, rng);        // 尾音
            break;
        case Sfx::Flak:
            b.alloc((int)(0.14f * RATE));
            noiseBurst(b, 0, 0.12f, 30, 0.6f, 0.9f, rng);
            toneSweep(b, 0, 320, 110, 0.10f, 26, 0.5f);
            break;
        case Sfx::Missile: {
            b.alloc((int)(0.48f * RATE));
            // 发射呼啸：噪声带通感渐升 + 上升音调
            int n = (int)(0.44f * RATE);
            float y = 0;
            for (int i = 0; i < n; i++) {
                float t = (float)i / RATE;
                float w = rng.unit() * 2.0f - 1.0f;
                float lp = 0.15f + 0.55f * (t / 0.44f);
                y += lp * (w - y);
                float env = t < 0.07f ? t / 0.07f : expf(-4.5f * (t - 0.07f));
                b.mix(i, y * env * 0.8f);
            }
            toneSweep(b, 0, 170, 430, 0.40f, 3.5f, 0.22f);
            break;
        }
        case Sfx::Explosion:
            b.alloc((int)(0.55f * RATE));
            noiseBurst(b, 0, 0.5f, 7.5f, 0.22f, 1.0f, rng);
            toneSweep(b, 0, 72, 22, 0.5f, 6, 0.85f);
            break;
        case Sfx::BigExplosion:
            b.alloc((int)(1.05f * RATE));
            noiseBurst(b, 0, 0.95f, 4.5f, 0.16f, 1.0f, rng);
            toneSweep(b, 0, 55, 17, 0.9f, 4, 1.0f);
            noiseBurst(b, (int)(0.22f * RATE), 0.5f, 8, 0.20f, 0.55f, rng); // 二次闷响
            break;
        case Sfx::Tesla: {
            b.alloc((int)(0.26f * RATE));
            // 电报式跳频方波 = 电弧劈啪
            int i = 0, total = (int)(0.24f * RATE);
            while (i < total) {
                float f = 1400.0f + rng.unit() * 3600.0f;
                int seg = (int)(0.002f * RATE) + rng.range(1, 4) * (int)(0.001f * RATE);
                float phase = 0;
                for (int j = 0; j < seg && i + j < total; j++) {
                    phase += 2 * PI * f / RATE;
                    float env = expf(-13.0f * (i + j) / (float)RATE);
                    b.mix(i + j, (sinf(phase) >= 0 ? 0.6f : -0.6f) * env * 0.8f);
                }
                i += seg;
            }
            noiseBurst(b, 0, 0.22f, 18, 0.9f, 0.35f, rng);
            break;
        }
        case Sfx::Prism: {
            b.alloc((int)(0.34f * RATE));
            int n = (int)(0.32f * RATE);
            float phase = 0;
            for (int i = 0; i < n; i++) {
                float t = (float)i / RATE;
                float f = 1500.0f * expf(-3.4f * t) + 420.0f;
                phase += 2 * PI * f / RATE;
                float ring = 0.6f + 0.4f * sinf(2 * PI * 88 * t); // 环形调制颤音
                b.mix(i, sinf(phase) * ring * expf(-9.0f * t) * 0.7f);
            }
            noiseBurst(b, 0, 0.1f, 24, 1.0f, 0.20f, rng);
            break;
        }
        case Sfx::Click:
            b.alloc((int)(0.05f * RATE));
            toneSweep(b, 0, 2300, 1500, 0.04f, 55, 0.5f);
            break;
        case Sfx::Place:
            b.alloc((int)(0.26f * RATE));
            toneSweep(b, 0, 150, 52, 0.22f, 11, 0.9f);   // 落地闷响
            noiseBurst(b, 0, 0.09f, 20, 0.4f, 0.5f, rng); // 尘土
            blip(b, 0.05f, 780, 0.06f, 0.22f);            // 金属碰响
            break;
        case Sfx::Ready:
            b.alloc((int)(0.30f * RATE));
            blip(b, 0.00f, 880, 0.09f, 0.5f);   // A5
            blip(b, 0.10f, 1318, 0.16f, 0.5f);  // E6
            break;
        case Sfx::Cash:
            b.alloc((int)(0.20f * RATE));
            blip(b, 0.00f, 1568, 0.05f, 0.4f);
            blip(b, 0.06f, 2093, 0.09f, 0.4f);
            break;
        case Sfx::Alarm: {
            b.alloc((int)(0.62f * RATE));
            for (int seg = 0; seg < 4; seg++) {
                float f = seg % 2 == 0 ? 520 : 690;
                int start = (int)(seg * 0.15f * RATE);
                int n = (int)(0.12f * RATE);
                float phase = 0;
                for (int i = 0; i < n; i++) {
                    phase += 2 * PI * f / RATE;
                    float s = sinf(phase) >= 0 ? 0.5f : -0.5f;
                    float t = (float)i / n;
                    float env = t < 0.1f ? t / 0.1f : (t > 0.85f ? (1 - t) / 0.15f : 1.0f);
                    b.mix(start + i, s * env * 0.35f);
                }
            }
            break;
        }
        case Sfx::Deploy:
            b.alloc((int)(0.75f * RATE));
            noiseBurst(b, 0, 0.7f, 5, 0.18f, 0.5f, rng);                    // 液压低鸣
            toneSweep(b, 0, 230, 470, 0.55f, 3, 0.18f);                     // 伺服上升
            noiseBurst(b, (int)(0.10f * RATE), 0.03f, 40, 0.5f, 0.7f, rng); // 机械咔哒×3
            noiseBurst(b, (int)(0.30f * RATE), 0.03f, 40, 0.5f, 0.7f, rng);
            noiseBurst(b, (int)(0.55f * RATE), 0.04f, 30, 0.45f, 0.8f, rng);
            break;
        case Sfx::Sell:
            b.alloc((int)(0.24f * RATE));
            blip(b, 0.00f, 1180, 0.06f, 0.4f);
            blip(b, 0.07f, 1570, 0.08f, 0.4f);
            toneSweep(b, (int)(0.13f * RATE), 210, 75, 0.10f, 22, 0.5f);
            break;
        case Sfx::NukeLaunch: {
            // 空袭警报：起伏鸣笛两轮
            b.alloc((int)(2.2f * RATE));
            for (int seg = 0; seg < 4; seg++) {
                int start = (int)(seg * 0.55f * RATE);
                int n = (int)(0.5f * RATE);
                float phase = 0;
                for (int i = 0; i < n; i++) {
                    float t = (float)i / n;
                    float f = seg % 2 == 0 ? 420 + 260 * t : 680 - 260 * t; // 升降调交替
                    phase += 2 * PI * f / RATE;
                    float env = t < 0.08f ? t / 0.08f : (t > 0.9f ? (1 - t) / 0.1f : 1.0f);
                    b.mix(start + i, sinf(phase) * env * 0.42f);
                }
            }
            break;
        }
        case Sfx::NukeBlast:
            b.alloc((int)(2.8f * RATE));
            noiseBurst(b, 0, 1.6f, 2.8f, 0.10f, 1.0f, rng);                 // 主爆轰
            toneSweep(b, 0, 48, 14, 2.4f, 2.2f, 1.0f);                      // 次声低频
            noiseBurst(b, (int)(0.30f * RATE), 1.2f, 3.5f, 0.07f, 0.7f, rng); // 翻滚余波
            noiseBurst(b, (int)(1.1f * RATE), 0.8f, 4.5f, 0.12f, 0.45f, rng);
            break;
        case Sfx::Lightning: {
            // 雷劈：高频撕裂 + 低频滚雷
            b.alloc((int)(0.7f * RATE));
            noiseBurst(b, 0, 0.06f, 60, 1.0f, 0.9f, rng);   // 劈啪
            toneSweep(b, 0, 2600, 300, 0.09f, 30, 0.5f);
            noiseBurst(b, (int)(0.07f * RATE), 0.5f, 6, 0.14f, 0.85f, rng); // 滚雷
            break;
        }
        case Sfx::Storm: {
            // 风暴起：持续风声 + 远雷
            b.alloc((int)(1.6f * RATE));
            noiseBurst(b, 0, 1.5f, 2.2f, 0.06f, 0.55f, rng);
            noiseBurst(b, (int)(0.4f * RATE), 0.5f, 5, 0.10f, 0.5f, rng);
            toneSweep(b, (int)(0.1f * RATE), 90, 40, 1.3f, 3, 0.5f);
            break;
        }
        case Sfx::IronCurtain: {
            // 能量场展开：低沉嗡鸣 + 上升泛音
            b.alloc((int)(1.1f * RATE));
            toneSweep(b, 0, 70, 140, 1.0f, 2.5f, 0.7f);
            toneSweep(b, 0, 280, 560, 0.9f, 3.5f, 0.3f);
            blip(b, 0.75f, 1046, 0.12f, 0.35f);
            blip(b, 0.88f, 1568, 0.16f, 0.35f);
            break;
        }
        case Sfx::SWReady:
            // 就绪提示：三连上行音
            b.alloc((int)(0.5f * RATE));
            blip(b, 0.00f, 784, 0.10f, 0.5f);   // G5
            blip(b, 0.12f, 988, 0.10f, 0.5f);   // B5
            blip(b, 0.24f, 1319, 0.20f, 0.55f); // E6
            break;
        case Sfx::Crush:
            // 碾压：湿碎挤压 + 车体沉重感
            b.alloc((int)(0.16f * RATE));
            noiseBurst(b, 0, 0.12f, 26, 0.35f, 0.9f, rng);
            toneSweep(b, 0, 140, 45, 0.12f, 18, 0.7f);
            break;
        case Sfx::Eva:
            // EVA 播报提示：双音叮
            b.alloc((int)(0.22f * RATE));
            blip(b, 0.00f, 988, 0.06f, 0.45f);  // B5
            blip(b, 0.08f, 1319, 0.10f, 0.45f); // E6
            break;
        case Sfx::NavalCannon:
            // 舰炮：比坦克炮更低沉的轰鸣 + 金属尾音
            b.alloc((int)(0.42f * RATE));
            noiseBurst(b, 0, 0.05f, 30, 1.0f, 0.9f, rng);
            toneSweep(b, 0, 90, 24, 0.38f, 7, 1.0f);
            noiseBurst(b, 0, 0.30f, 12, 0.24f, 0.5f, rng);
            break;
        case Sfx::Torpedo:
            // 鱼雷发射：入水气泡 + 低鸣推进
            b.alloc((int)(0.32f * RATE));
            noiseBurst(b, 0, 0.22f, 14, 0.30f, 0.65f, rng);
            toneSweep(b, 0, 130, 70, 0.26f, 6, 0.45f);
            break;
        default: break;
    }
    return b;
}

// ===================== 进行曲 BGM（D 小调，116 BPM，8 小节无缝循环）=====================
float midiFreq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

// 固定音高音符：起音/释音包络，可选方波
void note(Buf& b, float atSec, int midi, float dur, float amp, bool square) {
    int start = (int)(atSec * RATE);
    int n = (int)(dur * RATE);
    float f = midiFreq(midi);
    float phase = 0;
    for (int i = 0; i < n; i++) {
        phase += 2 * PI * f / RATE;
        float t = (float)i / n;
        float env = t < 0.08f ? t / 0.08f : (t > 0.8f ? (1.0f - t) / 0.2f : 1.0f);
        float s = square ? (sinf(phase) >= 0 ? 0.5f : -0.5f) : sinf(phase);
        b.mix(start + i, s * env * amp);
    }
}

void kick(Buf& b, float atSec) { toneSweep(b, (int)(atSec * RATE), 110, 40, 0.12f, 14, 0.85f); }
void snare(Buf& b, float atSec, Rng& rng) {
    noiseBurst(b, (int)(atSec * RATE), 0.09f, 32, 0.5f, 0.5f, rng);
    toneSweep(b, (int)(atSec * RATE), 210, 150, 0.07f, 26, 0.30f);
}

Buf genMarch() {
    Buf b;
    const float BEAT = 60.0f / 116.0f; // 四分音符时长
    const float E8 = BEAT / 2.0f;      // 八分音符时长
    b.alloc((int)(8 * 4 * BEAT * RATE)); // 8 小节整，无缝循环
    Rng rng(0xBEEF);

    // 主旋律：8 小节 × 8 个八分位（0=休止 -1=延音）
    static const int LEAD[64] = {
        62, 65, 69, 74, -1, 72, 69, 65, // D 小调上行
        62, 65, 69, 74, -1, 76, 74, 72,
        67, 70, 74, 79, -1, 77, 74, 70, // G 小调
        69, 73, 76, 81, -1, 76, 73, 69, // A7
        74, -1, -1, 00, 69, 72, 74, 76, // 长音 + 过渡
        77, -1, 76, 74, 72, 74, 69, 65,
        70, 74, 77, 82, -1, 77, 74, 70, // Bb
        69, 73, 76, 81, -1, 79, 76, 73, // A7 回主
    };
    // 每小节低音根音（D D G A D D Bb A）
    static const int ROOT[8] = {38, 38, 43, 45, 38, 38, 46, 45};

    for (int bar = 0; bar < 8; bar++) {
        float barStart = bar * 4 * BEAT;
        // 鼓：四踩 + 二四军鼓
        for (int e = 0; e < 8; e += 2) kick(b, barStart + e * E8);
        snare(b, barStart + 2 * E8, rng);
        snare(b, barStart + 6 * E8, rng);
        if (bar == 7) // 结尾军鼓滚奏
            for (int e = 4; e < 8; e++) snare(b, barStart + e * E8 + E8 / 2, rng);
        // 贝斯：根音-五音交替八分
        for (int e = 0; e < 8; e++) {
            int m = ROOT[bar] + (e % 2 ? 7 : 0);
            note(b, barStart + e * E8, m, E8 * 0.9f, 0.34f, false);
        }
        // 旋律：方波 lead
        for (int e = 0; e < 8; e++) {
            int m = LEAD[bar * 8 + e];
            if (m > 0) note(b, barStart + e * E8, m, E8 * 0.92f, 0.26f, true);
        }
    }
    return b;
}

// PCM → 内存 WAV 字节流
std::vector<unsigned char> wavBytes(const Buf& b) {
    uint32_t dataSize = (uint32_t)b.frames() * 2;
    std::vector<unsigned char> w(44 + dataSize);
    auto w32 = [&](int off, uint32_t v) { memcpy(w.data() + off, &v, 4); };
    auto w16 = [&](int off, uint16_t v) { memcpy(w.data() + off, &v, 2); };
    memcpy(w.data(), "RIFF", 4);      w32(4, 36 + dataSize);
    memcpy(w.data() + 8, "WAVE", 4);
    memcpy(w.data() + 12, "fmt ", 4); w32(16, 16);
    w16(20, 1);                       // PCM
    w16(22, 1);                       // 单声道
    w32(24, RATE);
    w32(28, RATE * 2);                // 字节率
    w16(32, 2);                       // 块对齐
    w16(34, 16);                      // 位深
    memcpy(w.data() + 36, "data", 4); w32(40, dataSize);
    memcpy(w.data() + 44, b.d.data(), dataSize);
    return w;
}

} // namespace

// ===================== BGM =====================
void SoundBank::initBgm() {
    if (!IsAudioDeviceReady()) return;
    Buf b = genMarch();
    std::vector<unsigned char> w = wavBytes(b);
    bgm = LoadMusicStreamFromMemory(".wav", w.data(), (int)w.size());
    if (bgm.stream.buffer == nullptr) {
        TraceLog(LOG_WARNING, "RA2 bgm: music stream load failed");
        return;
    }
    bgm.looping = true;
    SetMusicVolume(bgm, 0.35f * masterVol);
    PlayMusicStream(bgm);
    bgmOk = true;
    TraceLog(LOG_INFO, "RA2 bgm: march synthesized, %.1fs loop", (float)b.frames() / RATE);
}

void SoundBank::updateBgm() {
    if (bgmOk && bgmOn) UpdateMusicStream(bgm);
}

void SoundBank::toggleBgm() {
    if (!bgmOk) return;
    bgmOn = !bgmOn;
    if (bgmOn) ResumeMusicStream(bgm);
    else PauseMusicStream(bgm);
}

// ===================== 播放 =====================
void SoundBank::init() {
    if (!IsAudioDeviceReady()) {
        InitAudioDevice();
    }
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "RA2 sfx: no audio device, muted");
        return;
    }
    for (int i = 0; i < (int)Sfx::COUNT; i++) {
        Buf b = genSfx((Sfx)i);
        Wave w;
        w.frameCount = (unsigned)b.frames();
        w.sampleRate = RATE;
        w.sampleSize = 16;
        w.channels = 1;
        w.data = b.d.data();
        Sound base = LoadSoundFromWave(w);
        snd[i][0] = base;
        for (int a = 1; a < ALIAS; a++) snd[i][a] = LoadSoundAlias(base);
    }
    ok = true;
    TraceLog(LOG_INFO, "RA2 sfx: %d sounds synthesized", (int)Sfx::COUNT);
}

void SoundBank::shutdown() {
    if (bgmOk) { StopMusicStream(bgm); UnloadMusicStream(bgm); bgmOk = false; }
    if (!ok) return;
    for (int i = 0; i < (int)Sfx::COUNT; i++) {
        for (int a = 1; a < ALIAS; a++) UnloadSoundAlias(snd[i][a]);
        UnloadSound(snd[i][0]);
    }
    ok = false;
}

void SoundBank::setMasterVol(float v) {
    masterVol = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    if (bgmOk) SetMusicVolume(bgm, 0.35f * masterVol);
}

void SoundBank::play(Sfx id, float vol) {
    vol *= masterVol;
    if (!ok || vol <= 0.01f) return;
    int i = (int)id;
    double now = GetTime();
    if (now - last[i] < 0.045) return; // 同类音效节流
    last[i] = now;
    Sound& s = snd[i][rr[i]++ % ALIAS];
    SetSoundVolume(s, vol > 1.0f ? 1.0f : vol);
    SetSoundPitch(s, 0.94f + (float)(rand() % 13) / 100.0f);
    PlaySound(s);
}

void SoundBank::playAt(Sfx id, float tx, float ty) {
    float d = distf(tx, ty, lisX, lisY);
    float vol = (1.0f - d / 30.0f) * masterVol;
    if (vol <= 0.05f) return;
    int i = (int)id;
    double now = GetTime();
    if (now - last[i] < 0.045) return;
    last[i] = now;
    Sound& s = snd[i][rr[i]++ % ALIAS];
    SetSoundVolume(s, vol > 1.0f ? 1.0f : vol);
    float pan = 0.5f + (tx - lisX) / 36.0f;
    SetSoundPan(s, pan < 0.05f ? 0.05f : (pan > 0.95f ? 0.95f : pan));
    SetSoundPitch(s, 0.94f + (float)(rand() % 13) / 100.0f);
    PlaySound(s);
}
