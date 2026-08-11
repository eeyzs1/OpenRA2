#include "sfx/sound.h"
#include "gfx/assets.h"
#include "core/util.h"
#include "core/ini.h"
#include "core/content.h"
#include <cmath>
#include <vector>
#include <cstring>
#include <string>

SoundBank g_sfx;

// ===================== 合成器（44.1kHz 立体声，float 累积 + 母带限幅）=====================
namespace {

constexpr int RATE = 44100;
// PI 宏由 raylib.h 提供

// ---- 立体声 PCM 缓冲：float 累积可超 ±1，削波控制在母带阶段统一处理 ----
struct Buf {
    std::vector<float> d; // 交错 L/R
    void alloc(int frames) { d.assign((size_t)frames * 2, 0.0f); }
    int frames() const { return (int)(d.size() / 2); }
    void mix(int i, float v) { // 中央
        if (i < 0 || i >= frames()) return;
        d[2 * i] += v; d[2 * i + 1] += v;
    }
    void mixPan(int i, float v, float pan) { // 等功率声像
        if (i < 0 || i >= frames()) return;
        float a = pan * PI * 0.5f;
        d[2 * i] += v * cosf(a); d[2 * i + 1] += v * sinf(a);
    }
    void mixLR(int i, float l, float r) {
        if (i < 0 || i >= frames()) return;
        d[2 * i] += l; d[2 * i + 1] += r;
    }
};

// ---- 振荡器 ----
enum class Wave { Sine, Saw, Square, Tri };
float osc(Wave w, float ph) {
    switch (w) {
        case Wave::Sine: return sinf(ph);
        case Wave::Square: return sinf(ph) >= 0 ? 1.0f : -1.0f;
        case Wave::Tri: { float t = ph / (2 * PI); t -= floorf(t); return t < 0.5f ? t * 4 - 1 : 3 - t * 4; }
        default: { float t = ph / (2 * PI); t -= floorf(t); return t * 2 - 1; } // Saw
    }
}

// ---- 状态变量滤波器（Chamberlin SVF）：低通/带通/高通，全程数值稳定 ----
struct SVF {
    float lo = 0, band = 0, f = 0.1f, q = 0.7f;
    void set(float freq, float res = 0.7f) {
        float fc = freq < 20 ? 20 : (freq > RATE * 0.42f ? RATE * 0.42f : freq);
        f = 2.0f * sinf(PI * fc / (float)RATE);
        q = res < 0.05f ? 0.05f : res;
    }
    float low(float x)  { float hp = x - lo - q * band; band += f * hp; lo += f * band; return lo; }
    float bandpass(float x) { float hp = x - lo - q * band; band += f * hp; lo += f * band; return band; }
    float high(float x) { float hp = x - lo - q * band; band += f * hp; lo += f * band; return hp; }
};

// ---- Freeverb 简化混响：4 并联梳状 + 2 串联全通（R 声道路径 +23 采样展宽立体声）----
struct Comb {
    std::vector<float> buf; int idx = 0; float fb = 0.84f, damp = 0.28f, store = 0;
    void init(int n) { buf.assign(n, 0.0f); }
    float step(float x) {
        float out = buf[idx];
        store += (1.0f - damp) * (out - store);
        buf[idx] = x + store * fb;
        if (++idx >= (int)buf.size()) idx = 0;
        return out;
    }
};
struct Allpass {
    std::vector<float> buf; int idx = 0;
    void init(int n) { buf.assign(n, 0.0f); }
    float step(float x) {
        float b = buf[idx];
        float out = b - x;
        buf[idx] = x + b * 0.5f;
        if (++idx >= (int)buf.size()) idx = 0;
        return out;
    }
};
struct Reverb {
    Comb cL[4], cR[4]; Allpass aL[2], aR[2];
    void init(float room = 0.84f, float damp = 0.28f) {
        static const int TL[4] = {1116, 1188, 1277, 1356};
        static const int TR[4] = {1139, 1211, 1300, 1379};
        static const int AL[2] = {556, 441}, AR[2] = {579, 464};
        for (int i = 0; i < 4; i++) { cL[i].init(TL[i]); cL[i].fb = room; cL[i].damp = damp;
                                      cR[i].init(TR[i]); cR[i].fb = room; cR[i].damp = damp; }
        for (int i = 0; i < 2; i++) { aL[i].init(AL[i]); aR[i].init(AR[i]); }
    }
    void step(float in, float& l, float& r) {
        float x = in * 0.55f;
        float sl = 0, sr = 0;
        for (int i = 0; i < 4; i++) { sl += cL[i].step(x); sr += cR[i].step(x); }
        for (int i = 0; i < 2; i++) { sl = aL[i].step(sl); sr = aR[i].step(sr); }
        l = sl; r = sr;
    }
};

// 音效空间化：湿声混入并把混响尾部接入缓冲末尾
void applyReverb(Buf& b, float wet, float room = 0.84f, float damp = 0.30f) {
    if (wet <= 0.001f) return;
    Reverb v; v.init(room, damp);
    int n0 = b.frames(), tail = RATE / 2;
    std::vector<float> out(((size_t)n0 + tail) * 2, 0.0f);
    for (int i = 0; i < n0 + tail; i++) {
        float dry = i < n0 ? (b.d[2 * i] + b.d[2 * i + 1]) * 0.5f : 0.0f;
        float l, r; v.step(dry, l, r);
        float dl = i < n0 ? b.d[2 * i] : 0.0f, dr = i < n0 ? b.d[2 * i + 1] : 0.0f;
        out[2 * i] = dl * (1 - wet) + l * wet;
        out[2 * i + 1] = dr * (1 - wet) + r * wet;
    }
    b.d.swap(out);
}

// BGM 专用：混响尾缠绕回开头，无缝循环不断空间感
void applyReverbLoop(Buf& b, float wet) {
    if (wet <= 0.001f) return;
    Reverb v; v.init(0.80f, 0.34f);
    int n0 = b.frames(), tail = RATE / 2;
    std::vector<float> wb(((size_t)n0 + tail) * 2, 0.0f);
    for (int i = 0; i < n0 + tail; i++) {
        float dry = i < n0 ? (b.d[2 * i] + b.d[2 * i + 1]) * 0.5f : 0.0f;
        float l, r; v.step(dry, l, r);
        wb[2 * i] = l; wb[2 * i + 1] = r;
    }
    for (int i = 0; i < tail; i++) { // 尾部卷入开头
        wb[2 * i] += wb[2 * (n0 + i)];
        wb[2 * i + 1] += wb[2 * (n0 + i) + 1];
    }
    for (int i = 0; i < n0; i++) {
        b.d[2 * i] = b.d[2 * i] * (1 - wet) + wb[2 * i] * wet;
        b.d[2 * i + 1] = b.d[2 * i + 1] * (1 - wet) + wb[2 * i + 1] * wet;
    }
}

// 多抽头乒乓延迟：战场空旷回响（炮击/导弹/舰炮）
void applyDelay(Buf& b, float timeSec, float feedback, float wet) {
    int dly = (int)(timeSec * RATE);
    int n0 = b.frames();
    std::vector<float> src(n0);
    for (int i = 0; i < n0; i++) src[i] = (b.d[2 * i] + b.d[2 * i + 1]) * 0.5f;
    b.d.resize(((size_t)n0 + (size_t)dly * 4) * 2, 0.0f);
    for (int tap = 1; tap <= 4; tap++) {
        float g = wet * powf(feedback, (float)(tap - 1));
        if (g < 0.012f) break;
        float pan = tap % 2 ? 0.66f : 0.34f; // 左右交替
        for (int i = 0; i < n0; i++) b.mixPan(i + tap * dly, src[i] * g, pan);
    }
}

// 母带：峰值归一 + tanh 软限幅（提升响度，无硬削波爆音）
void masterize(Buf& b, float peak = 0.92f, float drive = 1.4f) {
    float mx = 1e-6f;
    for (float v : b.d) mx = fmaxf(mx, fabsf(v));
    float g = peak / mx, norm = tanhf(drive);
    for (float& v : b.d) v = tanhf(v * g * drive) / norm * peak;
}

// float 立体声 → PCM16 交错
std::vector<short> pcm16(const Buf& b) {
    std::vector<short> s(b.d.size());
    for (size_t i = 0; i < b.d.size(); i++) {
        float v = b.d[i]; v = v < -1 ? -1 : (v > 1 ? 1 : v);
        s[i] = (short)(v * 32767.0f);
    }
    return s;
}

// ===================== 基础合成层 =====================
// 低通噪声爆发（炮口燃气/爆轰/尘土）
void noiseBurst(Buf& b, int start, float dur, float decay, float cutoff, float amp, Rng& rng, float pan = 0.5f) {
    int n = (int)(dur * RATE);
    SVF f; f.set(cutoff, 0.7f);
    for (int i = 0; i < n; i++) {
        float w = rng.unit() * 2.0f - 1.0f;
        b.mixPan(start + i, f.low(w) * expf(-decay * i / (float)RATE) * amp, pan);
    }
}

// 带通噪声（响弦/裂响/高频瞬态）
void noiseBand(Buf& b, int start, float dur, float decay, float freq, float q, float amp, Rng& rng, float pan = 0.5f) {
    int n = (int)(dur * RATE);
    SVF f; f.set(freq, q);
    for (int i = 0; i < n; i++) {
        float w = rng.unit() * 2.0f - 1.0f;
        b.mixPan(start + i, f.bandpass(w) * expf(-decay * i / (float)RATE) * amp, pan);
    }
}

// 扫频音：f0→f1 指数扫 + 指数衰减，可选波形
void toneSweep(Buf& b, int start, float f0, float f1, float dur, float decay, float amp,
               Wave w = Wave::Sine, float pan = 0.5f) {
    int n = (int)(dur * RATE);
    float ph = 0, k = logf(f1 / f0) / n;
    for (int i = 0; i < n; i++) {
        ph += 2 * PI * f0 * expf(k * i) / RATE;
        b.mixPan(start + i, osc(w, ph) * expf(-decay * i / (float)RATE) * amp, pan);
    }
}

// 亮音叮：基频+高八泛音，快起音
void blip(Buf& b, float atSec, float freq, float dur, float amp, float pan = 0.5f) {
    int start = (int)(atSec * RATE), n = (int)(dur * RATE);
    float p1 = 0, p2 = 0;
    for (int i = 0; i < n; i++) {
        p1 += 2 * PI * freq / RATE; p2 += 2 * PI * freq * 2.005f / RATE;
        float t = (float)i / n;
        float env = t < 0.07f ? t / 0.07f : expf(-5.5f * (t - 0.07f));
        b.mixPan(start + i, (sinf(p1) + sinf(p2) * 0.28f) * env * amp, pan);
    }
}

// 金属泛音簇：非谐泛音（金属棒振动比），快速衰减 → RA2 打击感核心
void metalPing(Buf& b, int start, float f0, float dur, float decay, float amp, float pan = 0.5f) {
    int n = (int)(dur * RATE);
    static const float RATIO[4] = {1.0f, 2.76f, 4.07f, 5.43f};
    static const float GAIN[4] = {1.0f, 0.56f, 0.36f, 0.22f};
    float ph[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) {
        float s = 0;
        for (int k = 0; k < 4; k++) { ph[k] += 2 * PI * f0 * RATIO[k] / RATE; s += sinf(ph[k]) * GAIN[k]; }
        b.mixPan(start + i, s * expf(-decay * i / (float)RATE) * amp / 1.9f, pan);
    }
}

// 碎裂噼啪：稀疏随机脉冲串经低通，pan 随机游走 → 碎屑溅落/电弧余响
void crackle(Buf& b, int start, float dur, float density, float cutoff, float amp, Rng& rng) {
    int n = (int)(dur * RATE);
    SVF lp; lp.set(cutoff, 0.7f);
    float pan = 0.5f;
    for (int i = 0; i < n; i++) {
        float imp = rng.unit() < density ? (rng.unit() * 2.0f - 1.0f) : 0.0f;
        if (imp != 0.0f) pan = 0.28f + rng.unit() * 0.44f;
        b.mixPan(start + i, lp.low(imp) * expf(-2.5f * i / (float)RATE) * amp, pan);
    }
}

// ===================== 战场音效（25 种，多层分层：瞬态+主体+尾音+空间）=====================
Buf genSfx(Sfx id) {
    Buf b;
    Rng rng(0xC0FFEE + (uint64_t)id * 7919);
    switch (id) {
        case Sfx::Shot: // 步枪：激波脆响 + 中频裂 + 弹壳
            b.alloc((int)(0.14f * RATE));
            noiseBurst(b, 0, 0.018f, 170, 4200, 0.9f, rng);
            noiseBand(b, 0, 0.05f, 55, 1600, 0.5f, 0.55f, rng);
            metalPing(b, 0, 2500, 0.05f, 65, 0.16f);
            applyReverb(b, 0.10f);
            masterize(b, 0.62f);
            break;
        case Sfx::Dig: { // 短脉冲（≤0.25s）：低频一下 + 极少碎石，避免 0.8s+ 研磨墙
            b.alloc((int)(0.22f * RATE));
            toneSweep(b, 0, 88, 52, 0.12f, 22, 0.65f);
            noiseBurst(b, 0, 0.06f, 9, 380, 0.32f, rng);
            crackle(b, (int)(0.015f * RATE), 0.05f, 0.05f, 1100, 0.18f, rng);
            masterize(b, 0.50f);
            break;
        }
        case Sfx::MirageFire:
        case Sfx::RhinoFire:
        case Sfx::ApocFire:
        case Sfx::Cannon: // 坦克炮：低频轰体 + 出膛激波 + 炮管振鸣 + 战场回响
            b.alloc((int)(0.50f * RATE));
            toneSweep(b, 0, 120, 30, 0.40f, 6.5f, 1.0f);
            noiseBurst(b, 0, 0.05f, 55, 950, 0.9f, rng);
            noiseBurst(b, 0, 0.32f, 8.5f, 380, 0.65f, rng);
            metalPing(b, (int)(0.012f * RATE), 390, 0.22f, 15, 0.26f);
            applyDelay(b, 0.19f, 0.38f, 0.30f);
            applyReverb(b, 0.16f, 0.82f, 0.36f);
            masterize(b, 0.95f);
            break;
        case Sfx::Flak: // 高炮：快脆 + 榴霰散片
            b.alloc((int)(0.20f * RATE));
            noiseBurst(b, 0, 0.035f, 65, 2600, 0.9f, rng);
            toneSweep(b, 0, 330, 110, 0.09f, 24, 0.42f);
            crackle(b, (int)(0.03f * RATE), 0.12f, 0.09f, 3400, 0.38f, rng);
            applyReverb(b, 0.11f);
            masterize(b, 0.80f);
            break;
        case Sfx::Missile: { // 导弹：带通呼啸渐升 + 引擎 + 尾音回响
            b.alloc((int)(0.55f * RATE));
            int n = (int)(0.48f * RATE);
            SVF bp; float pan = 0.5f;
            for (int i = 0; i < n; i++) {
                float t = (float)i / RATE;
                bp.set(650 + 3200 * (t / 0.48f), 0.35f);
                float w = rng.unit() * 2.0f - 1.0f;
                float env = t < 0.06f ? t / 0.06f : expf(-3.5f * (t - 0.06f));
                if (i % 440 == 0) pan = 0.4f + rng.unit() * 0.2f;
                b.mixPan(i, bp.bandpass(w) * env * 0.62f, pan);
            }
            toneSweep(b, 0, 150, 430, 0.42f, 3.0f, 0.18f);
            applyDelay(b, 0.16f, 0.32f, 0.22f);
            applyReverb(b, 0.10f);
            masterize(b, 0.75f);
            break;
        }
        case Sfx::Explosion: // 爆炸：次声 drop + 主爆轰 + 起爆 crack + 碎屑
            b.alloc((int)(0.85f * RATE));
            toneSweep(b, 0, 85, 22, 0.65f, 4.0f, 1.0f);
            noiseBurst(b, 0, 0.45f, 6.0f, 820, 1.0f, rng);
            noiseBand(b, 0, 0.07f, 42, 2500, 0.5f, 0.65f, rng);
            crackle(b, (int)(0.12f * RATE), 0.45f, 0.06f, 1100, 0.45f, rng);
            applyReverb(b, 0.15f);
            masterize(b, 0.90f);
            break;
        case Sfx::BigExplosion: // 建筑级：更长次声 + 多层爆轰 + 金属扭曲 + 残骸雨
            b.alloc((int)(1.40f * RATE));
            toneSweep(b, 0, 60, 16, 1.25f, 2.6f, 1.0f);
            noiseBurst(b, 0, 0.07f, 38, 2100, 0.95f, rng);
            noiseBurst(b, 0, 1.05f, 3.8f, 420, 1.0f, rng);
            metalPing(b, (int)(0.03f * RATE), 270, 0.45f, 8, 0.28f);
            crackle(b, (int)(0.28f * RATE), 0.9f, 0.05f, 1500, 0.45f, rng);
            noiseBurst(b, (int)(0.26f * RATE), 0.55f, 7, 260, 0.5f, rng);
            applyReverb(b, 0.19f, 0.85f, 0.38f);
            masterize(b, 1.0f);
            break;
        case Sfx::Tesla: { // 磁暴：跳频电弧 + 工频嗡 + 空气击穿 + 线圈低吼
            b.alloc((int)(0.38f * RATE));
            int n = (int)(0.30f * RATE);
            float ph = 0, fCur = 1400, pan = 0.5f;
            for (int i = 0; i < n; i++) {
                float t = (float)i / RATE;
                if (i % 220 == 0) { fCur = 900 + rng.unit() * 4200; pan = 0.38f + rng.unit() * 0.24f; }
                ph += 2 * PI * fCur / RATE;
                float am = 0.55f + 0.45f * sinf(2 * PI * 117 * t); // 工频调制
                b.mixPan(i, osc(Wave::Saw, ph) * am * expf(-8.5f * t) * 0.5f, pan);
            }
            noiseBurst(b, 0, 0.05f, 60, 6000, 0.4f, rng);       // 击穿瞬态
            toneSweep(b, 0, 120, 52, 0.28f, 8, 0.34f);          // 线圈低吼
            applyReverb(b, 0.13f);
            masterize(b, 0.85f);
            break;
        }
        case Sfx::Prism: { // 光棱：充能啸叫 + 环形调制光束 + 玻璃泛音 + 余晖
            b.alloc((int)(0.45f * RATE));
            toneSweep(b, 0, 900, 2600, 0.11f, 10, 0.18f);
            int n = (int)(0.34f * RATE);
            float ph = 0;
            for (int i = 0; i < n; i++) {
                float t = (float)i / RATE;
                ph += 2 * PI * (1800.0f * expf(-2.8f * t) + 500.0f) / RATE;
                float ring = 0.6f + 0.4f * sinf(2 * PI * 95 * t);
                b.mixPan(i, sinf(ph) * ring * expf(-8.0f * t) * 0.5f, 0.52f);
            }
            metalPing(b, (int)(0.04f * RATE), 2100, 0.26f, 13, 0.18f);
            applyDelay(b, 0.11f, 0.30f, 0.20f);
            masterize(b, 0.80f);
            break;
        }
        case Sfx::Click: // UI 点击：干脆短亮
            b.alloc((int)(0.04f * RATE));
            toneSweep(b, 0, 2600, 1700, 0.022f, 95, 0.5f);
            noiseBand(b, 0, 0.012f, 160, 4200, 0.6f, 0.22f, rng);
            masterize(b, 0.50f, 1.2f);
            break;
        case Sfx::Place: // 建筑放置：落地闷响 + 尘土 + 钢构咬合
            b.alloc((int)(0.32f * RATE));
            toneSweep(b, 0, 130, 38, 0.24f, 9, 1.0f);
            noiseBurst(b, 0, 0.09f, 18, 520, 0.5f, rng);
            metalPing(b, (int)(0.05f * RATE), 540, 0.18f, 14, 0.26f);
            noiseBand(b, (int)(0.05f * RATE), 0.035f, 65, 1900, 0.6f, 0.28f, rng);
            applyReverb(b, 0.13f);
            masterize(b, 0.85f);
            break;
        case Sfx::Ready: // 生产就绪：上行双音
            b.alloc((int)(0.30f * RATE));
            blip(b, 0.00f, 880, 0.09f, 0.42f);
            blip(b, 0.10f, 1318, 0.16f, 0.42f);
            masterize(b, 0.70f, 1.2f);
            break;
        case Sfx::Cash: // 资金：收银机双叮 + 铃
            b.alloc((int)(0.22f * RATE));
            blip(b, 0.00f, 1568, 0.05f, 0.32f);
            blip(b, 0.05f, 2093, 0.09f, 0.32f);
            metalPing(b, 0, 3400, 0.14f, 22, 0.12f);
            masterize(b, 0.60f, 1.2f);
            break;
        case Sfx::Alarm: { // 低电警报：柔和方波双音交替四轮
            b.alloc((int)(0.66f * RATE));
            SVF lp; lp.set(2300, 0.6f);
            for (int seg = 0; seg < 4; seg++) {
                float f = seg % 2 == 0 ? 520 : 690;
                int start = (int)(seg * 0.155f * RATE);
                int n = (int)(0.125f * RATE);
                float ph = 0;
                for (int i = 0; i < n; i++) {
                    ph += 2 * PI * f / RATE;
                    float t = (float)i / n;
                    float env = t < 0.1f ? t / 0.1f : (t > 0.85f ? (1 - t) / 0.15f : 1.0f);
                    b.mix(start + i, lp.low(osc(Wave::Square, ph)) * env * 0.32f);
                }
            }
            masterize(b, 0.55f, 1.2f);
            break;
        }
        case Sfx::Deploy: // 基地车展开：液压 + 伺服 + 三连机械锁定 + 落地
            b.alloc((int)(0.85f * RATE));
            noiseBurst(b, 0, 0.65f, 5, 210, 0.42f, rng);
            toneSweep(b, 0, 200, 480, 0.55f, 2.5f, 0.15f);
            noiseBand(b, (int)(0.12f * RATE), 0.03f, 50, 1500, 0.6f, 0.5f, rng);
            noiseBand(b, (int)(0.34f * RATE), 0.03f, 50, 1600, 0.6f, 0.5f, rng);
            noiseBand(b, (int)(0.56f * RATE), 0.04f, 40, 1400, 0.6f, 0.6f, rng);
            metalPing(b, (int)(0.56f * RATE), 700, 0.15f, 15, 0.22f);
            toneSweep(b, (int)(0.58f * RATE), 170, 55, 0.2f, 10, 0.55f);
            applyReverb(b, 0.12f);
            masterize(b, 0.80f);
            break;
        case Sfx::Sell: // 出售：双叮 + 下滑尾
            b.alloc((int)(0.26f * RATE));
            blip(b, 0.00f, 1180, 0.06f, 0.34f);
            blip(b, 0.07f, 1570, 0.08f, 0.34f);
            toneSweep(b, (int)(0.14f * RATE), 210, 75, 0.10f, 22, 0.4f);
            masterize(b, 0.60f, 1.2f);
            break;
        case Sfx::NukeLaunch: { // 核袭警报：手摇警报器——双锯齿拍频升降 + 旋转 AM + 声像摇曳
            b.alloc((int)(2.6f * RATE));
            for (int cyc = 0; cyc < 2; cyc++) {
                int s = (int)(cyc * 1.3f * RATE);
                int n = (int)(1.25f * RATE);
                float p1 = 0, p2 = 0;
                for (int i = 0; i < n; i++) {
                    float t = (float)i / n;
                    float f = 380 + 320 * (t < 0.5f ? t * 2 : 2 - t * 2);
                    p1 += 2 * PI * f / RATE;
                    p2 += 2 * PI * f * 1.021f / RATE;
                    float am = 0.82f + 0.18f * sinf(2 * PI * 3.1f * t * 1.25f); // 手摇旋转调制
                    float env = t < 0.06f ? t / 0.06f : (t > 0.92f ? (1 - t) / 0.08f : 1.0f);
                    float o = (osc(Wave::Saw, p1) * 0.62f + osc(Wave::Saw, p2) * 0.38f);
                    float pan = 0.5f + 0.18f * sinf(2 * PI * 0.8f * (cyc * 1.25f + t * 1.25f));
                    b.mixPan(s + i, tanhf(o * 1.7f) * am * env * 0.34f, pan);
                }
            }
            applyReverb(b, 0.20f, 0.82f, 0.34f);
            masterize(b, 0.80f);
            break;
        }
        case Sfx::NukeBlast: // 核爆：超长次声 + 主爆 + 翻滚余波 + 火风暴
            b.alloc((int)(3.0f * RATE));
            toneSweep(b, 0, 55, 16, 2.5f, 1.9f, 1.0f);
            noiseBurst(b, 0, 1.7f, 2.3f, 320, 1.0f, rng);
            noiseBurst(b, (int)(0.04f * RATE), 0.18f, 22, 1900, 0.75f, rng);
            noiseBurst(b, (int)(0.38f * RATE), 1.3f, 2.8f, 130, 0.65f, rng);
            crackle(b, (int)(0.5f * RATE), 1.1f, 0.04f, 950, 0.4f, rng);
            applyReverb(b, 0.22f, 0.86f, 0.35f);
            masterize(b, 1.0f);
            break;
        case Sfx::Lightning: // 雷劈：高频撕裂 + 滚雷
            b.alloc((int)(0.85f * RATE));
            noiseBand(b, 0, 0.03f, 95, 5200, 0.4f, 0.8f, rng);
            toneSweep(b, 0, 3200, 400, 0.08f, 26, 0.42f);
            noiseBurst(b, (int)(0.08f * RATE), 0.55f, 4.5f, 260, 0.85f, rng);
            applyReverb(b, 0.17f);
            masterize(b, 0.90f);
            break;
        case Sfx::Storm: { // 风暴：摇曳风声 + 远雷 + 低频铺垫
            b.alloc((int)(1.9f * RATE));
            int n = (int)(1.8f * RATE);
            SVF lp;
            for (int i = 0; i < n; i++) {
                float t = (float)i / RATE;
                lp.set(320 + 250 * sinf(2 * PI * 0.35f * t) + 140 * sinf(2 * PI * 0.13f * t), 0.7f);
                float w = rng.unit() * 2.0f - 1.0f;
                float edge = t < 0.15f ? t / 0.15f : (t > 1.6f ? (1.8f - t) / 0.2f : 1.0f);
                b.mixPan(i, lp.low(w) * edge * 0.4f, 0.5f + 0.24f * sinf(2 * PI * 0.09f * t));
            }
            noiseBurst(b, (int)(0.6f * RATE), 0.55f, 5, 190, 0.45f, rng);
            toneSweep(b, (int)(0.15f * RATE), 80, 35, 1.5f, 2.5f, 0.4f);
            applyReverb(b, 0.18f);
            masterize(b, 0.70f);
            break;
        }
        case Sfx::IronCurtain: // 铁幕：低频能量场 + 上行泛音 + 空间延迟
            b.alloc((int)(1.2f * RATE));
            toneSweep(b, 0, 60, 120, 1.05f, 2.2f, 0.55f);
            toneSweep(b, 0, 240, 520, 0.95f, 3.0f, 0.2f, Wave::Saw);
            blip(b, 0.78f, 1046, 0.14f, 0.26f);
            blip(b, 0.94f, 1568, 0.18f, 0.26f);
            applyDelay(b, 0.14f, 0.32f, 0.22f);
            masterize(b, 0.75f);
            break;
        case Sfx::SWReady: // 超武就绪：三连上行号角
            b.alloc((int)(0.5f * RATE));
            blip(b, 0.00f, 784, 0.10f, 0.4f);
            blip(b, 0.12f, 988, 0.10f, 0.4f);
            blip(b, 0.24f, 1319, 0.20f, 0.44f);
            masterize(b, 0.75f, 1.2f);
            break;
        case Sfx::Crush: // 碾压：湿碎挤压 + 沉重车体
            b.alloc((int)(0.20f * RATE));
            noiseBurst(b, 0, 0.13f, 24, 520, 0.85f, rng);
            crackle(b, 0, 0.15f, 0.12f, 750, 0.45f, rng);
            toneSweep(b, 0, 130, 42, 0.13f, 16, 0.6f);
            masterize(b, 0.70f);
            break;
        case Sfx::Eva: // EVA 播报提示：双音叮
            b.alloc((int)(0.22f * RATE));
            blip(b, 0.00f, 988, 0.06f, 0.4f);
            blip(b, 0.08f, 1319, 0.10f, 0.4f);
            masterize(b, 0.65f, 1.2f);
            break;
        case Sfx::NavalCannon: // 舰炮：更低沉轰鸣 + 炮闩金属 + 水面反射
            b.alloc((int)(0.55f * RATE));
            toneSweep(b, 0, 85, 22, 0.42f, 6, 1.0f);
            noiseBurst(b, 0, 0.05f, 42, 720, 0.9f, rng);
            noiseBurst(b, 0, 0.36f, 9, 300, 0.55f, rng);
            metalPing(b, (int)(0.012f * RATE), 230, 0.28f, 12, 0.26f);
            applyDelay(b, 0.23f, 0.38f, 0.30f);
            applyReverb(b, 0.15f);
            masterize(b, 0.95f);
            break;
        case Sfx::Torpedo: { // 鱼雷：入水 + 上浮气泡群 + 推进低鸣
            b.alloc((int)(0.48f * RATE));
            noiseBurst(b, 0, 0.16f, 13, 520, 0.55f, rng);
            for (int k = 0; k < 6; k++)
                blip(b, 0.04f + 0.045f * k, 300 + k * 110 + rng.unit() * 50, 0.028f, 0.13f, 0.35f + rng.unit() * 0.3f);
            toneSweep(b, 0, 110, 65, 0.34f, 5, 0.4f);
            masterize(b, 0.70f);
            break;
        }
        default: break;
    }
    return b;
}

// ===================== BGM 乐器（Frank Klepacki 工业金属）=====================
float midiFreq(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

// 底鼓：击皮瞬态 + 音高快降 body + 次声延展
void kick(Buf& b, float atSec, Rng& rng, float amp = 1.0f) {
    int s = (int)(atSec * RATE);
    noiseBurst(b, s, 0.005f, 700, 5200, 0.28f * amp, rng);
    int n = (int)(0.30f * RATE);
    float ph = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        ph += 2 * PI * (155.0f * expf(-t * 38.0f) + 42.0f) / RATE;
        b.mix(s + i, sinf(ph) * expf(-t * 11.0f) * 0.95f * amp);
    }
}

// 军鼓：鼓身三角 190Hz + 响弦带通 + 高频气声
void snare(Buf& b, float atSec, Rng& rng, float amp = 1.0f) {
    int s = (int)(atSec * RATE);
    int n1 = (int)(0.07f * RATE);
    float ph = 0;
    for (int i = 0; i < n1; i++) {
        ph += 2 * PI * 190 / RATE;
        b.mix(s + i, osc(Wave::Tri, ph) * expf(-52.0f * i / (float)RATE) * 0.26f * amp);
    }
    noiseBand(b, s, 0.11f, 26, 2200, 0.4f, 0.5f * amp, rng);
    noiseBurst(b, s, 0.15f, 17, 6800, 0.32f * amp, rng);
}

// 镲：高通金属噪声，闭镲短/开镲长
void hat(Buf& b, float atSec, Rng& rng, float amp = 0.15f, float decay = 85.0f) {
    int s = (int)(atSec * RATE), n = (int)(0.35f * RATE);
    SVF hp; hp.set(7600, 0.55f);
    for (int i = 0; i < n; i++) {
        float w = rng.unit() * 2.0f - 1.0f;
        b.mixPan(s + i, hp.high(w) * expf(-decay * i / (float)RATE) * amp, 0.62f);
    }
}

// 吊镲：过门点缀
void crash(Buf& b, float atSec, Rng& rng, float amp = 0.4f) {
    int s = (int)(atSec * RATE), n = (int)(1.3f * RATE);
    SVF hp; hp.set(5400, 0.5f);
    SVF bp; bp.set(8800, 0.3f);
    for (int i = 0; i < n; i++) {
        float w = rng.unit() * 2.0f - 1.0f;
        float y = hp.high(w) * 0.7f + bp.bandpass(w) * 0.45f;
        b.mixPan(s + i, y * expf(-3.4f * i / (float)RATE) * amp, 0.38f);
    }
}

// 行军踏步（Hell March 式引子）
void stomp(Buf& b, float atSec, Rng& rng) {
    int s = (int)(atSec * RATE);
    noiseBurst(b, s, 0.03f, 120, 320, 0.42f, rng);
    toneSweep(b, s, 95, 40, 0.09f, 30, 0.5f);
}

// 失真吉他单音：双轨锯齿失谐 + 动态低通 + tanh 失真，width 控制双轨分离度
void gtrNote(Buf& b, float atSec, float freq, float dur, float amp, float drive, float cutoff, float width) {
    int s = (int)(atSec * RATE), n = (int)(dur * RATE);
    float p1 = 0, p2 = 0;
    SVF f1, f2; f1.set(cutoff, 0.55f); f2.set(cutoff * 1.06f, 0.55f);
    for (int i = 0; i < n; i++) {
        p1 += 2 * PI * freq / RATE;
        p2 += 2 * PI * freq * 1.0045f / RATE;
        float t = (float)i / n;
        float env = t < 0.02f ? t / 0.02f : expf(-3.2f * (t - 0.02f));
        float x1 = tanhf(f1.low(osc(Wave::Saw, p1)) * drive * 2.2f) * env * amp;
        float x2 = tanhf(f2.low(osc(Wave::Saw, p2)) * drive * 2.2f) * env * amp;
        b.mixLR(s + i, x1 * (1 + width) * 0.5f + x2 * (1 - width) * 0.5f,
                       x2 * (1 + width) * 0.5f + x1 * (1 - width) * 0.5f);
    }
}

// 强力和弦：根音+纯五度+八度，全双轨
void powerChord(Buf& b, float atSec, int midi, float dur, float amp, float drive = 3.0f) {
    float f = midiFreq(midi);
    gtrNote(b, atSec, f, dur, amp * 0.55f, drive, 2600, 0.9f);
    gtrNote(b, atSec, f * 1.4983f, dur, amp * 0.42f, drive, 2400, 0.9f);
    gtrNote(b, atSec, f * 2.0f, dur, amp * 0.24f, drive, 2800, 0.9f);
}

// 掌闷 chug：pick 瞬态 + 极低通 + 短包络高失真
void chug(Buf& b, float atSec, int midi, float amp, Rng& rng) {
    float f = midiFreq(midi);
    noiseBand(b, (int)(atSec * RATE), 0.02f, 200, 2600, 0.5f, amp * 0.22f, rng);
    int s = (int)(atSec * RATE), n = (int)(0.105f * RATE);
    float p1 = 0, p2 = 0, p3 = 0;
    SVF lp; lp.set(980, 0.7f);
    for (int i = 0; i < n; i++) {
        p1 += 2 * PI * f / RATE; p2 += 2 * PI * f * 1.4983f / RATE; p3 += 2 * PI * f * 2.0f / RATE;
        float o = osc(Wave::Saw, p1) * 0.55f + osc(Wave::Saw, p2) * 0.40f + osc(Wave::Saw, p3) * 0.22f;
        float v = tanhf(lp.low(o) * 4.2f) * expf(-25.0f * i / (float)RATE) * amp;
        b.mixLR(s + i, v * 0.96f, v);
    }
}

// 贝斯：锯齿+正弦补基频，低通压暗
void bassNote(Buf& b, float atSec, int midi, float dur, float amp) {
    int s = (int)(atSec * RATE), n = (int)(dur * RATE);
    float f = midiFreq(midi), ph = 0;
    SVF lp; lp.set(500, 0.75f);
    for (int i = 0; i < n; i++) {
        ph += 2 * PI * f / RATE;
        float o = osc(Wave::Saw, ph) * 0.75f + sinf(ph) * 0.5f;
        float t = (float)i / n;
        float env = t < 0.03f ? t / 0.03f : 1.0f;
        b.mix(s + i, tanhf(lp.low(o) * 1.6f) * env * amp);
    }
}

// 主音：双锯齿失谐 + 颤音渐入（合成铜管）
void leadNote(Buf& b, float atSec, int midi, float dur, float amp) {
    int s = (int)(atSec * RATE), n = (int)(dur * RATE);
    float f = midiFreq(midi), p1 = 0, p2 = 0;
    SVF lp; lp.set(3300, 0.5f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float vib = 1.0f + 0.006f * sinf(2 * PI * 5.6f * t) * fminf(1.0f, t * 6.0f);
        p1 += 2 * PI * f * vib / RATE; p2 += 2 * PI * f * 1.005f * vib / RATE;
        float tn = (float)i / n;
        float env = tn < 0.04f ? tn / 0.04f : expf(-3.8f * (tn - 0.04f));
        float v = tanhf(lp.low((osc(Wave::Saw, p1) + osc(Wave::Saw, p2)) * 0.5f) * 2.4f) * env * amp;
        b.mixPan(s + i, v, 0.44f);
    }
}

// 警报 pad：双声道失谐缓慢升降（引子/过门氛围）
void siren(Buf& b, float atSec, float dur, float amp) {
    int s = (int)(atSec * RATE), n = (int)(dur * RATE);
    float p1 = 0, p2 = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        float f = 640.0f + 220.0f * sinf(2 * PI * t / (dur * 0.5f));
        p1 += 2 * PI * f / RATE; p2 += 2 * PI * f * 1.004f / RATE;
        float edge = t < dur - t ? t : dur - t;
        edge = edge * 2.0f > 1.0f ? 1.0f : edge * 2.0f;
        b.mixLR(s + i, sinf(p1) * edge * amp, sinf(p2) * edge * amp);
    }
}

// ===================== 曲目生成（16 小节无缝循环）=====================
struct TrackSpec {
    float bpm;
    const int* riff;    // 16 小节 chug 根音
    const int* stab;    // 16 小节强力和弦
    const int* lead;    // 主音 16 分步进（nullptr=无主音；0=休止 -1=延音）
    int leadFrom, leadTo;
    int drums;          // 0=标准驱动 1=半速沉重 2=双踩疾驰
    bool gallop;        // chug 马蹄节奏
    uint64_t seed;
};

Buf genTrack(const TrackSpec& ts) {
    Buf b;
    const float BEAT = 60.0f / ts.bpm;
    const float E8 = BEAT / 2.0f, E16 = BEAT / 4.0f;
    const int BARS = 16;
    b.alloc((int)(BARS * 4 * BEAT * RATE));
    Rng rng(ts.seed);

    for (int bar = 0; bar < BARS; bar++) {
        float bs = bar * 4 * BEAT;
        int root = ts.riff[bar];
        bool intro = bar < 2; // 引子：踏步 + 军鼓滚奏渐强 + 警报
        // ---- 鼓 ----
        if (intro) {
            for (int q = 0; q < 4; q++) stomp(b, bs + q * BEAT, rng);
            for (int e = 0; e < 16; e++) snare(b, bs + e * E16, rng, 0.22f + 0.78f * e / 15.0f);
            if (bar == 1) for (int e = 0; e < 8; e++) hat(b, bs + e * E8, rng, 0.10f);
        } else if (ts.drums == 1) {          // 半速：沉重碾压感
            kick(b, bs, rng);
            kick(b, bs + 7 * E8, rng);
            snare(b, bs + 4 * E8, rng);
            for (int e = 0; e < 8; e++) hat(b, bs + e * E8, rng, e % 2 ? 0.09f : 0.15f);
        } else if (ts.drums == 2) {          // 双踩疾驰
            for (int e = 0; e < 8; e++) kick(b, bs + e * E8, rng, 0.9f);
            kick(b, bs + 15 * E16, rng, 0.8f);
            snare(b, bs + 2 * E8, rng); snare(b, bs + 6 * E8, rng);
            for (int e = 0; e < 8; e++) hat(b, bs + e * E8, rng, 0.12f);
        } else {                             // 标准驱动
            kick(b, bs, rng); kick(b, bs + 2 * E8, rng);
            kick(b, bs + 4 * E8, rng); kick(b, bs + 7 * E8, rng, 0.85f);
            snare(b, bs + 2 * E8, rng); snare(b, bs + 6 * E8, rng);
            for (int e = 0; e < 8; e++) hat(b, bs + e * E8, rng, e % 2 ? 0.09f : 0.15f);
        }
        if (bar == 3 || bar == 7 || bar == 14 || bar == 15) // 过门军鼓滚奏渐强
            for (int e = 12; e < 16; e++) snare(b, bs + e * E16, rng, 0.35f + 0.65f * (e - 12) / 3.0f);
        if (!intro && bar % 4 == 0) crash(b, bs, rng, 0.32f);
        // ---- 警报 pad ----
        if (bar == 0) siren(b, bs, 4 * BEAT, 0.08f);
        if (bar == 8) { siren(b, bs, 2 * BEAT, 0.06f); crash(b, bs, rng, 0.42f); }
        if (intro) continue;
        // ---- 节奏吉他 chug ----
        if (ts.gallop) { // 马蹄节奏：每拍 8分+16分×2
            for (int q = 0; q < 4; q++) {
                chug(b, bs + q * 4 * E16, root, 0.36f, rng);
                chug(b, bs + (q * 4 + 2) * E16, root, 0.24f, rng);
                chug(b, bs + (q * 4 + 3) * E16, root + (q == 3 && bar % 4 == 3 ? 2 : 0), 0.28f, rng);
            }
        } else {         // 8 分驱动 + 小节末 16 分加花
            for (int e = 0; e < 8; e++) chug(b, bs + e * E8, root, e % 2 ? 0.28f : 0.36f, rng);
            chug(b, bs + 14 * E16, root, 0.28f, rng);
            chug(b, bs + 15 * E16, root + (bar % 4 == 3 ? 2 : 0), 0.30f, rng);
        }
        // ---- 强力和弦 stab ----
        powerChord(b, bs, ts.stab[bar], E8 * 3.0f, 0.32f);
        if (bar % 2 == 1) powerChord(b, bs + 5 * E8, ts.stab[bar], E8 * 1.5f, 0.25f);
        // ---- 贝斯：根音 8 分 ----
        for (int e = 0; e < 8; e++) bassNote(b, bs + e * E8, root - 12, E8 * 0.9f, 0.32f);
        // ---- 主音 ----
        if (ts.lead && bar >= ts.leadFrom && bar <= ts.leadTo) {
            const int* row = ts.lead + (bar - ts.leadFrom) * 16;
            for (int e = 0; e < 16; e++)
                if (row[e] > 0) leadNote(b, bs + e * E16, row[e], E16 * 1.8f, 0.19f);
        }
    }
    powerChord(b, (BARS - 1) * 4 * BEAT + 3 * BEAT, ts.stab[BARS - 1], BEAT * 0.8f, 0.36f);
    applyReverbLoop(b, 0.13f);   // 空间感（缠绕保持无缝循环）
    masterize(b, 0.92f, 1.5f);   // 响度最大化
    return b;
}

// ---- 曲目 1：工业进行曲（D 小调 112 BPM，Hell March 式标准驱动）----
const int MARCH_RIFF[16] = {
    38, 38, 46, 48,  38, 38, 46, 48,
    38, 41, 43, 45,  38, 41, 46, 45,
};
const int MARCH_STAB[16] = {
    50, 50, 58, 60,  50, 50, 58, 60,
    50, 53, 55, 57,  50, 53, 58, 57,
};
const int MARCH_LEAD[6 * 16] = {
    74, 0, 76, 0, 77, 0, 76, 0, 74, 0, 73, 0, 74, 0, -1, 0,
    77, 0, 79, 0, 81, 0, 79, 0, 77, 0, 76, 0, 77, 0, -1, 0,
    81, 0, 79, 0, 77, 0, 76, 0, 74, 0, -1, 0, -1, 0, 0, 0,
    73, 0, 74, 0, 76, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    74, 0, 74, 0, 77, 0, 76, 0, 74, 0, 73, 0, 74, 0, -1, 0,
    70, 0, 73, 0, 74, 0, -1, 0, -1, 0, 0, 0, 0, 0, 0, 0,
};
const TrackSpec TRACK_MARCH = {112.0f, MARCH_RIFF, MARCH_STAB, MARCH_LEAD, 8, 13, 0, false, 0xDEAD};

// ---- 曲目 2：重型碾压（E 小调 96 BPM，半速鼓）----
const int GRIND_RIFF[16] = {
    40, 40, 43, 45,  40, 40, 43, 45,
    40, 36, 38, 47,  40, 36, 43, 47,
};
const int GRIND_STAB[16] = {
    52, 52, 55, 57,  52, 52, 55, 57,
    52, 48, 50, 59,  52, 48, 55, 59,
};
const int GRIND_LEAD[6 * 16] = {
    76, 0, 0, 79, 0, 0, 81, 0, 79, 0, 76, 0, 74, 0, 76, 0,
    71, 0, 74, 0, 76, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    79, 0, 81, 0, 83, 0, 81, 0, 79, 0, 76, 0, 74, 0, -1, 0,
    76, 0, 74, 0, 71, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    76, 0, 76, 0, 79, 0, 76, 0, 74, 0, 71, 0, 74, 0, -1, 0,
    71, 0, 72, 0, 74, 0, -1, 0, -1, 0, 0, 0, 0, 0, 0, 0,
};
const TrackSpec TRACK_GRIND = {96.0f, GRIND_RIFF, GRIND_STAB, GRIND_LEAD, 8, 13, 1, false, 0xBEEF};

// ---- 曲目 3：超速疾驰（A 小调 132 BPM，双踩 + 马蹄 chug）----
const int OVERDRIVE_RIFF[16] = {
    45, 45, 41, 43,  45, 45, 41, 43,
    45, 48, 50, 52,  45, 48, 41, 43,
};
const int OVERDRIVE_STAB[16] = {
    57, 57, 53, 55,  57, 57, 53, 55,
    57, 60, 62, 64,  57, 60, 53, 55,
};
const int OVERDRIVE_LEAD[6 * 16] = {
    81, 0, 83, 0, 84, 0, 83, 0, 81, 0, 80, 0, 81, 0, -1, 0,
    84, 0, 86, 0, 88, 0, 86, 0, 84, 0, 83, 0, 84, 0, -1, 0,
    88, 0, 86, 0, 84, 0, 83, 0, 81, 0, -1, 0, -1, 0, 0, 0,
    80, 0, 81, 0, 83, 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    81, 0, 81, 0, 84, 0, 83, 0, 81, 0, 80, 0, 81, 0, -1, 0,
    77, 0, 80, 0, 81, 0, -1, 0, -1, 0, 0, 0, 0, 0, 0, 0,
};
const TrackSpec TRACK_OVERDRIVE = {132.0f, OVERDRIVE_RIFF, OVERDRIVE_STAB, OVERDRIVE_LEAD, 8, 13, 2, true, 0xF00D};

// PCM → 内存 WAV 字节流（立体声 16bit）
std::vector<unsigned char> wavBytes(const Buf& b) {
    std::vector<short> pcm = pcm16(b);
    uint32_t dataSize = (uint32_t)pcm.size() * 2;
    std::vector<unsigned char> w(44 + dataSize);
    auto w32 = [&](int off, uint32_t v) { memcpy(w.data() + off, &v, 4); };
    auto w16 = [&](int off, uint16_t v) { memcpy(w.data() + off, &v, 2); };
    memcpy(w.data(), "RIFF", 4);      w32(4, 36 + dataSize);
    memcpy(w.data() + 8, "WAVE", 4);
    memcpy(w.data() + 12, "fmt ", 4); w32(16, 16);
    w16(20, 1);                       // PCM
    w16(22, 2);                       // 立体声
    w32(24, RATE);
    w32(28, RATE * 4);                // 字节率
    w16(32, 4);                       // 块对齐
    w16(34, 16);                      // 位深
    memcpy(w.data() + 36, "data", 4); w32(40, dataSize);
    memcpy(w.data() + 44, pcm.data(), dataSize);
    return w;
}

} // namespace

// ===================== BGM =====================
void SoundBank::playBgmTrack(int idx) {
    if (bgmOk) { StopMusicStream(bgm); UnloadMusicStream(bgm); bgmOk = false; }
    if (idx < 0 || idx >= (int)bgmFiles.size()) return;
    bgm = LoadMusicStream(bgmFiles[idx].c_str());
    if (!bgm.stream.buffer) { TraceLog(LOG_WARNING, "RA2 bgm: failed to load %s", bgmFiles[idx].c_str()); return; }
    bgm.looping = false; // 手动轮换下一首
    bgmIdx = idx;
    SetMusicVolume(bgm, 0.35f * masterVol);
    PlayMusicStream(bgm);
    bgmOk = true;
    TraceLog(LOG_INFO, "RA2 bgm: playing %s", bgmFiles[idx].c_str());
}

void SoundBank::initBgm() {
    if (!IsAudioDeviceReady()) return;
    // 扫描内容根下 music（含 mods 覆盖/追加）
    {
        auto names = contentListFiles("assets/music", nullptr);
        for (const std::string& name : names) {
            if (!IsFileExtension(name.c_str(), ".ogg;.mp3;.wav;.flac")) continue;
            std::string virt = std::string("assets/music/") + name;
            std::string path = contentResolve(virt.c_str());
            if (!path.empty()) bgmFiles.push_back(path);
        }
    }
    // 播放列表元数据：叠加载 music.ini（后写覆盖顺序意图：合并 Track 列表，后文件优先重排）
    {
        auto stacks = contentResolveStack("assets/music/music.ini");
        for (const auto& iniPath : stacks) {
            Ini mini;
            if (!mini.load(iniPath.c_str())) continue;
            if (const Ini::Section* pl = mini.find("Playlist")) {
                std::vector<std::string> ordered;
                for (const auto& p : pl->kv) {
                    if (p.first != "Track") continue;
                    for (auto it = bgmFiles.begin(); it != bgmFiles.end(); ++it) {
                        const std::string& full = *it;
                        if (full.size() >= p.second.size()
                            && full.compare(full.size() - p.second.size(), p.second.size(), p.second) == 0) {
                            ordered.push_back(full);
                            bgmFiles.erase(it);
                            break;
                        }
                    }
                }
                ordered.insert(ordered.end(), bgmFiles.begin(), bgmFiles.end());
                if (!ordered.empty()) bgmFiles = std::move(ordered);
            }
        }
    }
    if (!bgmFiles.empty()) {
        bgmFromFiles = true;
        playBgmTrack(bgmFiles.size() > 1 ? GetRandomValue(0, (int)bgmFiles.size() - 1) : 0);
        return;
    }
    // 无外部音乐：禁止程序合成回退
    missingAssets++;
    TraceLog(LOG_ERROR, "BGM-MISSING: no files in assets/music/ (procedural BGM disabled; refuse start)");
    fprintf(stderr, "BGM-MISSING: no files in assets/music/ (procedural BGM disabled; refuse start)\n");
}

void SoundBank::updateBgm() {
    if (!bgmOk || !bgmOn) return;
    UpdateMusicStream(bgm);
    // 外部播放列表：当前曲目播完 → 随机轮换下一首（RA2 原作为多曲轮换）
    if (bgmFromFiles && !IsMusicStreamPlaying(bgm) && !bgmFiles.empty()) {
        int next = bgmIdx;
        if (bgmFiles.size() > 1) {
            while (next == bgmIdx) next = GetRandomValue(0, (int)bgmFiles.size() - 1);
        }
        playBgmTrack(next);
    }
}

void SoundBank::toggleBgm() {
    if (!bgmOk) return;
    bgmOn = !bgmOn;
    if (bgmOn) ResumeMusicStream(bgm);
    else PauseMusicStream(bgm);
}

// ===================== 播放 =====================
// 已禁用：禁止程序合成波形写入 assets/
bool SoundBank::genSfxAssets(const char* /*dir*/) {
    TraceLog(LOG_ERROR, "genSfxAssets disabled: place extracted audio under assets/sfx and assets/music");
    fprintf(stderr, "genSfxAssets disabled: place extracted audio under assets/sfx and assets/music\n");
    return false;
}

void SoundBank::init() {
    if (!IsAudioDeviceReady()) {
        InitAudioDevice();
    }
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "RA2 sfx: no audio device, muted");
        return;
    }
    missingAssets = 0;
    int external = 0;
    for (int i = 0; i < (int)Sfx::COUNT; i++) {
        Sound base{};
        const char* nm = sfxAssetName((Sfx)i);
        for (const char* ext : {".wav", ".ogg", ".mp3"}) {
            char virt[192];
            snprintf(virt, sizeof(virt), "assets/sfx/%s%s", nm, ext);
            std::string resolved = contentResolve(virt);
            if (!resolved.empty()) { base = LoadSound(resolved.c_str()); break; }
        }
        if (base.stream.buffer) {
            external++;
            snd[i][0] = base;
            for (int a = 1; a < ALIAS; a++) snd[i][a] = LoadSoundAlias(base);
        } else {
            missingAssets++;
            TraceLog(LOG_ERROR, "SFX-MISSING %s (procedural synth disabled; refuse start)", nm);
            fprintf(stderr, "SFX-MISSING %s (procedural synth disabled; refuse start)\n", nm);
        }
    }
    ok = (missingAssets == 0);
    TraceLog(LOG_INFO, "RA2 sfx: %d/%d external sounds loaded, missing=%d",
             external, (int)Sfx::COUNT, missingAssets);
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
    if (!ok) return; // 无音频设备（隐藏窗口无头测试）：直接静默
    float d = distf(tx, ty, lisX, lisY);
    float vol = (1.0f - d / 30.0f) * masterVol;
    if (id == Sfx::Dig) vol *= 0.40f; // 挖掘提示音压低
    if (vol <= 0.05f) return;
    int i = (int)id;
    double now = GetTime();
    // Dig：短脉冲 + ≥2s 节流；主炮类 0.12s 避免叠成噪音墙
    double gap = 0.045;
    if (id == Sfx::Dig) gap = 2.0;
    else if (id == Sfx::Cannon || id == Sfx::MirageFire || id == Sfx::RhinoFire || id == Sfx::ApocFire) gap = 0.12;
    if (now - last[i] < gap) return;
    last[i] = now;
    Sound& s = snd[i][rr[i]++ % ALIAS];
    SetSoundVolume(s, vol > 1.0f ? 1.0f : vol);
    float pan = 0.5f + (tx - lisX) / 36.0f;
    SetSoundPan(s, pan < 0.05f ? 0.05f : (pan > 0.95f ? 0.95f : pan));
    SetSoundPitch(s, 0.94f + (float)(rand() % 13) / 100.0f);
    PlaySound(s);
}
