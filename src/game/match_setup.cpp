#include "game/match_setup.h"
#include "game/skirmish_map.h"

bool validMatchSetup(const MatchSetup& s) {
    if((unsigned)s.rules>=(unsigned)RulesChoice::COUNT || s.country<1 || s.country>(int)Country::COUNT
        || s.color<0 || s.color>=MAX_PLAYERS || s.ai<0 || s.ai>7 || s.money<0 || s.money>1000000
        || s.size<32 || s.size>256 || s.terrain<0 || s.terrain>6 || s.mode<0 || s.mode>6)return false;
    if(s.reference && (s.size!=64 || s.ai!=1 || s.mode))return false;
    if(!s.originalMap.empty()){
        const auto* map=OriginalSkirmishMaps::find(s.originalMap);
        if(!map || s.rules==RulesChoice::Fusion || s.reference || s.ai<1 || s.ai>=map->players
            || s.size!=map->width+map->height || s.terrain || s.mode>1)return false;
    }
    for(int i=0;i<7;++i)if(s.aiColors[i]<0 || s.aiColors[i]>=MAX_PLAYERS || s.aiCountries[i]<1 || s.aiCountries[i]>(int)Country::COUNT
        || s.aiDifficulty[i]<0 || s.aiDifficulty[i]>3 || s.aiPersonality[i]<0 || s.aiPersonality[i]>4)return false;
    return true;
}

bool writeMatchSetup(FILE* file,const std::optional<MatchSetup>& setup) {
    if(!file || (setup && !validMatchSetup(*setup)))return false;
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& v){bytes+=(uint32_t)sizeof(v);if(ok && fwrite(&v,sizeof(v),1,file)!=1)ok=false;};
    const bool original=setup && !setup->originalMap.empty();
    item(uint32_t{original?2u:1u});item((uint8_t)setup.has_value());
    if(setup){const auto& s=*setup;item((uint8_t)s.rules);item(s.seed);
        for(int v:{s.country,s.color,s.ai,s.money,s.size,s.terrain,s.mode})item(v);
        for(bool v:{s.reference,s.crates,s.alliance,s.sharedVision,s.shortGame,s.superweapons,s.repacks})item((uint8_t)v);
        for(const auto* a:{&s.aiColors,&s.aiCountries,&s.aiDifficulty,&s.aiPersonality})for(int v:*a)item(v);
        if(original){item((uint8_t)s.originalMap.size());for(char c:s.originalMap)item(c);}
    }
    return ok && fwrite(&bytes,sizeof(bytes),1,file)==1;
}

bool readMatchSetup(FILE* file,std::optional<MatchSetup>& setup,bool originalMap) {
    setup.reset();if(!file)return false;bool ok=true;uint32_t bytes=0,version=0,footer=0;uint8_t active=0;
    const auto item=[&](auto& v){bytes+=(uint32_t)sizeof(v);if(ok && fread(&v,sizeof(v),1,file)!=1)ok=false;};
    item(version);item(active);if(!ok || version!=(originalMap?2u:1u) || active>1 || (originalMap && !active))return false;
    MatchSetup s;
    if(active){uint8_t choice=0;item(choice);s.rules=(RulesChoice)choice;item(s.seed);
        for(int* v:{&s.country,&s.color,&s.ai,&s.money,&s.size,&s.terrain,&s.mode})item(*v);
        for(bool* v:{&s.reference,&s.crates,&s.alliance,&s.sharedVision,&s.shortGame,&s.superweapons,&s.repacks}){
            uint8_t b=0;item(b);if(b>1)return false;*v=b!=0;
        }
        for(auto* a:{&s.aiColors,&s.aiCountries,&s.aiDifficulty,&s.aiPersonality})for(int& v:*a)item(v);
        if(originalMap){uint8_t length=0;item(length);if(!ok || !length || length>64)return false;
            s.originalMap.resize(length);for(char& c:s.originalMap)item(c);}
        if(!validMatchSetup(s))return false;
    }
    const auto expected=bytes;item(footer);if(!ok || footer!=expected)return false;
    if(active)setup=s;return true;
}
