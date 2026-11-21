#include "sfx/sound.h"
#include "sfx/voices.h"
#include "sfx/announcer.h"
#include "gfx/assets.h"
#include "core/util.h"
#include "core/ini.h"
#include "core/content.h"
#include <cmath>
#include <vector>
#include <cstring>
#include <string>

SoundBank g_sfx;

// Native soundtrack streaming lives in music.cpp; no synthesized fallback.
void SoundBank::initBgm() { music.init(); missingAssets+=music.missingCount(); }
void SoundBank::updateBgm() { music.update(); }
void SoundBank::toggleBgm() { music.toggle(); }

bool SoundBank::genSfxAssets(const char* /*dir*/) {
    TraceLog(LOG_ERROR, "genSfxAssets disabled: place extracted audio under assets/sfx and assets/music");
    fprintf(stderr, "genSfxAssets disabled: place extracted audio under assets/sfx and assets/music\n");
    return false;
}

void SoundBank::init() {
    missingAssets = 0;
    if (!IsAudioDeviceReady()) {
        InitAudioDevice();
    }
    g_unitVoices.init();
    missingAssets+=g_unitVoices.missingCount();
    g_announcer.init();missingAssets+=g_announcer.missingCount();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "RA2 sfx: no audio device, muted");
        return;
    }
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
    g_announcer.shutdown();g_unitVoices.shutdown();music.shutdown();
    for (int i = 0; i < (int)Sfx::COUNT; i++) {
        for (int a = 1; a < ALIAS; a++)if(snd[i][a].stream.buffer){UnloadSoundAlias(snd[i][a]);snd[i][a]={};}
        if(snd[i][0].stream.buffer){UnloadSound(snd[i][0]);snd[i][0]={};}
    }
    ok = false;
}

void SoundBank::setMasterVol(float v) {
    masterVol = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    g_unitVoices.setMasterVolume(masterVol);
    g_announcer.setMasterVolume(masterVol);music.setMasterVolume(masterVol);
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
