#include "game/campaign_save.h"
#include <cstring>

namespace {
uint64_t fingerprint(const MissionDef* mission) {
    if(!mission)return 0;
    uint64_t hash=14695981039346656037ull;
    auto number=[&](uint64_t value){for(int byte=0;byte<8;++byte){hash^=(uint8_t)(value>>(byte*8));hash*=1099511628211ull;}};
    auto text=[&](const std::string& value){number(value.size());for(unsigned char byte:value){hash^=byte;hash*=1099511628211ull;}};
    auto list=[&](const auto& values){number(values.size());for(const auto& value:values)number((uint64_t)value);};
    const auto& md=*mission;
    text(md.name);text(md.nameEn);text(md.brief);text(md.briefEn);
    number((int)md.playerFaction);list(md.aiFactions);number(md.mapSize);number(md.mapType);number(md.money);
    number(md.waves.size());for(const auto& wave:md.waves){number(wave.atTick);list(wave.units);}
    number(md.objective);number(md.objectiveTick);text(md.mapFile);number(md.noStartForce);
    number(md.triggers.size());for(const auto& trigger:md.triggers) {
        number((int)trigger.cond);for(int value:trigger.c)number(value);
        number((int)trigger.act);for(int value:trigger.a)number(value);
        list(trigger.units);text(trigger.msg);text(trigger.msgEn);text(trigger.tag);
        number(trigger.once);number(trigger.enabled);number(trigger.requiresPhase);
    }
    number(md.track);text(md.lineId);number(md.lineIndex);number(md.nextMission);number((int)md.playerCountry);
    list(md.allowedBuildings);list(md.allowedUnits);text(md.briefArt);
    number(md.objectives.size());for(const auto& objective:md.objectives) {
        text(objective.text);text(objective.textEn);number(objective.primary);number(objective.gateWin);
    }
    number(md.winOnAllPrimary);number(md.startPhase);number(md.timerVisibleDefault);
    // Keep existing custom-campaign fingerprints stable when the native
    // extension is absent, including previously written RA2CAMP1 snapshots.
    if(!md.nativeScene.empty()) {
        text("native-campaign-1");text(md.nativeScene);text(md.nativeTextFile);
        number(md.nativeTexts.size());for(const auto& [key,value]:md.nativeTexts){text(key);text(value);}
        for(const auto& objective:md.objectives){text(objective.nativeCompleteTrigger);number(objective.nativeCompleteOnVictory);}
    }
    return hash;
}
bool valid(const MissionDef* mission,const CampaignSnapshot& state) {
    if(state.difficulty<0 || state.difficulty>2 || (state.victory && !state.gameOver)
        || state.runtime.phase<0 || state.runtime.timerDeadline < -1)return false;
    if(mission)return state.objectives.size()==mission->objectives.size() && state.objectives.size()<=256;
    const CampaignRuntime empty;
    return state.objectives.empty() && state.runtime.phase==empty.phase && state.runtime.timerDeadline==empty.timerDeadline
        && state.runtime.timerVisible==empty.timerVisible && state.runtime.winOnAllPrimary==empty.winOnAllPrimary && !state.runtime.armed;
}
}

bool writeCampaignSnapshot(FILE* file,const MissionDef* mission,const CampaignSnapshot& state) {
    if(!valid(mission,state) || fwrite("RA2CAMP1",1,8,file)!=8)return false;
    auto put=[&](const auto& value){return fwrite(&value,sizeof(value),1,file)==1;};
    if(!put(fingerprint(mission)) || !put(state.difficulty) || !put(state.runtime.phase) || !put(state.runtime.timerDeadline)
        || !put((uint8_t)state.runtime.timerVisible) || !put((uint8_t)state.runtime.winOnAllPrimary) || !put((uint8_t)state.runtime.armed)
        || !put((uint8_t)state.gameOver) || !put((uint8_t)state.victory) || !put((uint32_t)state.objectives.size()))return false;
    for(bool complete:state.objectives)if(!put((uint8_t)complete))return false;
    return true;
}

bool readCampaignSnapshot(FILE* file,const MissionDef* mission,CampaignSnapshot& state) {
    CampaignSnapshot parsed;char magic[8]{};uint64_t identity=0;uint32_t count=0;
    auto get=[&](auto& value){return fread(&value,sizeof(value),1,file)==1;};
    auto flag=[&](bool& value){uint8_t byte=0;if(!get(byte) || byte>1)return false;value=byte!=0;return true;};
    if(fread(magic,1,8,file)!=8 || memcmp(magic,"RA2CAMP1",8) || !get(identity) || identity!=fingerprint(mission)
        || !get(parsed.difficulty) || !get(parsed.runtime.phase) || !get(parsed.runtime.timerDeadline)
        || !flag(parsed.runtime.timerVisible) || !flag(parsed.runtime.winOnAllPrimary) || !flag(parsed.runtime.armed)
        || !flag(parsed.gameOver) || !flag(parsed.victory) || !get(count) || count>256
        || count!=(mission?mission->objectives.size():0))return false;
    parsed.objectives.reserve(count);
    for(uint32_t index=0;index<count;++index){bool complete=false;if(!flag(complete))return false;parsed.objectives.push_back(complete);}
    if(!valid(mission,parsed))return false;
    state=std::move(parsed);return true;
}
