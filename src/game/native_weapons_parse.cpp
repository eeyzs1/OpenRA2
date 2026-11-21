#include "game/native_weapon_types.h"
#include "game/native_rocket_types.h"
#include <charconv>
#include <algorithm>
#include <cstring>

bool parseNativeGuns(const Ini::Section* source,int type,NativeGunProfile& profile,const NativeWeaponCatalog* catalog) {
    const auto integer=[&](const std::string& key,int& value,int low,int high) {
        const char* text=source?source->get(key.c_str()):nullptr;if(!text)return false;
        const auto r=std::from_chars(text,text+strlen(text),value);
        return r.ec==std::errc{} && !*r.ptr && value>=low && value<=high;
    };
    const auto text=[&](const std::string& key,std::string& value,bool required) {
        const char* raw=source?source->get(key.c_str()):nullptr;if(!raw)return false;value=raw;
        return value.size()<=128 && (!required || !value.empty());
    };
    int enabled=0;
    if(!integer("NativeGuns",enabled,1,1) || !integer("GunMode",profile.mode,0,2)
        || !integer("GunSelector",profile.selector,0,4) || !integer("GunVeteranAbilities",profile.abilities[0],0,7)
        || !integer("GunEliteAbilities",profile.abilities[1],0,7)
        || !nativeSourceNumber(source->get("GunVeteranCombat"),profile.veteranCombat,0.01f,100)
        || !nativeSourceNumber(source->get("GunVeteranROF"),profile.veteranRof,0.01f,100)
        || !nativeSourceNumber(source->get("GunVeteranArmor"),profile.veteranArmor,0.01f,100))return false;
    profile.available=true;
    if(profile.selector==4 && profile.mode!=0)return false;
    if(profile.staged()) {
        int stages=0;if(!integer("GunStages",stages,3,3) || !integer("GunRateUp",profile.rateUp,0,1000000)
            || !integer("GunRateDown",profile.rateDown,0,1000000))return false;
        for(int rank=0;rank<2;++rank)for(int stage=0;stage<3;++stage)
            if(!integer(std::string(rank?"GunEliteStage":"GunStage")+std::to_string(stage+1),profile.thresholds[rank][stage],stage?profile.thresholds[rank][stage-1]+1:1,1000000))return false;
    }
    for(int slot=0;slot<(profile.staged()?12:4);++slot) {
        auto& gun=profile.slots[slot];auto& w=gun.weapon;const std::string p="Gun"+std::to_string(slot);
        int present=0,rof=0,aa=0,ag=0,speed=0,style=0,minimum=0,maximum=0;
        if(!integer(p+"Present",present,profile.staged()?1:0,1))return false;gun.present=present!=0;if(!gun.present)continue;
        if(!text(p+"Id",gun.id,true) || !text(p+"Projectile",gun.projectile,true) || !text(p+"Report",gun.report,false)
            || !integer(p+"Damage",w.damage,catalog && catalog->version>=3?-1000000:0,1000000) || !integer(p+"ROF",rof,1,900000)
            || !integer(p+"Burst",w.burst,1,16) || !integer(p+"DelayMin",minimum,0,900)
            || !integer(p+"DelayMax",maximum,minimum,900) || !integer(p+"AA",aa,0,1) || !integer(p+"AG",ag,0,1)
            || !integer(p+"Speed",speed,0,1000000) || !integer(p+"Style",style,0,3)
            || !nativeSourceNumber(source->get((p+"Range").c_str()),w.range,-2,128)
            || !nativeSourceNumber(source->get((p+"Spread").c_str()),w.cellSpread,0,11)
            || !nativeSourceNumber(source->get((p+"PercentAtMax").c_str()),w.percentAtMax,0,1)
            || !nativeSourceVerses(source->get((p+"Verses").c_str()),gun.verses))return false;
        w.cooldown=rof*LOGIC_FPS/15;w.burstDelayMin=minimum*LOGIC_FPS/15;w.burstDelayMax=maximum*LOGIC_FPS/15;
        w.antiAir=aa!=0;w.antiGround=ag!=0;w.speedLeptons=std::min(255,std::min(speed,100)*256/100);
        w.projSprite=style==0?"bullet":style==1?"shell":style==2?"missile":"tesla";
        w.projectile=gun.projectile.c_str();w.report=gun.report.c_str();
        w.nativeType=type;w.nativeSlot=slot;w.nativeVerses=gun.verses.data();
        if(catalog) {
            int reference=-1;if(!integer(p+"Reference",reference,0,(int)catalog->weapons.size()-1))return false;
            const auto& d=catalog->weapons[reference];const auto& expected=d.weapon;
            const auto sameId=[](const std::string& a,const std::string& b) {
                const auto upper=[](char c){return c>='a' && c<='z'?c-'a'+'A':(int)c;};
                return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[&](char x,char y){return upper(x)==upper(y);});
            };
            if(!sameId(gun.id,d.id) || !sameId(gun.projectile,d.projectile->id) || gun.report!=d.report
                || w.damage!=expected.damage || w.range!=expected.range || w.cooldown!=expected.cooldown || w.burst!=expected.burst
                || w.antiAir!=expected.antiAir || w.antiGround!=expected.antiGround || w.speedLeptons!=expected.speedLeptons
                || std::strcmp(w.projSprite,expected.projSprite) || w.cellSpread!=expected.cellSpread || w.percentAtMax!=expected.percentAtMax
                || gun.verses!=catalog->warheads[d.warheadIndex].verses)return false;
            w.nativeSource=&d;
        } else if(source->get((p+"Reference").c_str()))return false;
    }
    return true;
}
