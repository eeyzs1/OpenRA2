#include "game/native_weapon_types.h"
#include "game/native_rocket_types.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>

namespace {
bool number(const Ini::Section* s,const char* key,int& n,int low,int high) {
    const char* text=s?s->get(key):nullptr;if(!text)return false;
    const auto r=std::from_chars(text,text+std::strlen(text),n);
    return r.ec==std::errc{} && !*r.ptr && n>=low && n<=high;
}
bool flag(const Ini::Section* s,const char* key,bool& value) {
    int n=0;if(!number(s,key,n,0,1))return false;value=n!=0;return true;
}
bool text(const Ini::Section* s,const char* key,std::string& value,bool id=true) {
    const char* raw=s?s->get(key):nullptr;if(!raw)return false;value=raw;
    if(value.size()>128 || (id && value.empty()))return false;
    return !id || std::all_of(value.begin(),value.end(),[](unsigned char c){
        return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';
    });
}
std::string idKey(std::string id) {
    for(char& c:id)if(c>='a' && c<='z')c-=32;return id;
}
}

bool parseNativeWeaponCatalog(const Ini& source,bool required,std::shared_ptr<const NativeWeaponCatalog>& result) {
    const auto* header=source.find("NativeWeapons");result.reset();
    if(!required)return header==nullptr;
    int version=0,weapons=0,projectiles=0,warheads=0;
    if(!number(header,"Version",version,1,3) || !number(header,"Weapons",weapons,1,2048)
        || !number(header,"Projectiles",projectiles,1,1024) || !number(header,"Warheads",warheads,1,1024))return false;
    auto catalog=std::make_shared<NativeWeaponCatalog>();
    catalog->version=version;
    catalog->weapons.resize(weapons);catalog->projectiles.resize(projectiles);catalog->warheads.resize(warheads);
    std::set<std::string> ids;
    for(int i=0;i<projectiles;++i) {
        const auto* s=source.find(("NativeProjectile."+std::to_string(i)).c_str());
        auto& p=catalog->projectiles[i];auto& m=p.motion;
        if(!text(s,"Id",p.id) || !ids.insert(idKey(p.id)).second || !text(s,"Image",p.image,false)
            || !flag(s,"Arcing",m.arcing) || !flag(s,"Inviso",m.inviso) || !flag(s,"Vertical",m.vertical)
            || !flag(s,"Inaccurate",m.inaccurate) || !flag(s,"Proximity",m.proximity)
            || !flag(s,"AA",m.aa) || !flag(s,"AG",m.ag) || !number(s,"ROT",m.rot,0,256)
            || !flag(s,"Level",p.level) || !flag(s,"FlakScatter",p.flakScatter) || !flag(s,"Airburst",p.airburst)
            || !number(s,"ShrapnelWeapon",p.shrapnelWeapon,-1,weapons-1)
            || !number(s,"ShrapnelCount",p.shrapnelCount,-256,256)
            || !number(s,"AirburstWeapon",p.airburstWeapon,-1,weapons-1) || !number(s,"Cluster",p.cluster,1,256))return false;
        m.name=p.id.c_str();m.image=p.image.c_str();
        if((p.shrapnelWeapon<0 && p.shrapnelCount!=0) || (p.airburstWeapon>=0 && !p.airburst))return false;
    }
    ids.clear();
    for(int i=0;i<warheads;++i) {
        const auto* s=source.find(("NativeWarhead."+std::to_string(i)).c_str());auto& w=catalog->warheads[i];
        if(!text(s,"Id",w.id) || !ids.insert(idKey(w.id)).second || !flag(s,"Temporal",w.temporal)
            || !nativeSourceVerses(s?s->get("Verses"):nullptr,w.verses)
            || !nativeSourceNumber(s?s->get("Spread"):nullptr,w.spread,0,11)
            || !nativeSourceNumber(s?s->get("PercentAtMax"):nullptr,w.percentAtMax,0,1))return false;
        if(version>=3 && (!flag(s,"AffectsAllies",w.affectsAllies) || !flag(s,"PsychicDamage",w.psychicDamage)
            || !flag(s,"Radiation",w.radiation) || !number(s,"InfantryDeath",w.infantryDeath,0,100)))return false;
    }
    ids.clear();
    for(int i=0;i<weapons;++i) {
        const auto* s=source.find(("NativeWeapon."+std::to_string(i)).c_str());auto& d=catalog->weapons[i];auto& w=d.weapon;
        int rof=0,speed=0,color=0;
        if(!text(s,"Id",d.id) || !ids.insert(idKey(d.id)).second || !text(s,"Report",d.report,false)
            || !number(s,"Projectile",d.projectileIndex,0,projectiles-1) || !number(s,"Warhead",d.warheadIndex,0,warheads-1)
            || !number(s,"Damage",w.damage,version>=3?-1000000:0,1000000) || !number(s,"ROF",rof,1,900000)
            || !number(s,"Burst",w.burst,1,16) || !number(s,"Speed",speed,0,1000000)
            || !nativeSourceNumber(s?s->get("Range"):nullptr,w.range,-2,128)
            || !flag(s,"Laser",d.laser) || !flag(s,"HouseColor",d.houseColor) || !flag(s,"Sonic",d.sonic)
            || !flag(s,"Electric",d.electric) || !flag(s,"RadBeam",d.radBeam)
            || !number(s,"LaserDuration",d.laserDuration,0,127) || !number(s,"LaserColor",color,0,0xffffff)
            || !number(s,"AmbientDamage",d.ambientDamage,0,1000000))return false;
        if(version>=3 && (!flag(s,"Suicide",d.suicide) || !number(s,"RadLevel",d.radLevel,0,1000000)))return false;
        d.index=i;d.laserColor=(uint32_t)color;d.projectile=&catalog->projectiles[d.projectileIndex];
        const auto& p=*d.projectile;const auto& wh=catalog->warheads[d.warheadIndex];
        d.warhead=&wh;
        w.cooldown=rof*LOGIC_FPS/15;w.speedLeptons=std::min(255,std::min(speed,100)*256/100);
        w.antiAir=p.motion.aa;w.antiGround=p.motion.ag;
        w.projSprite=d.electric?"tesla":p.motion.inviso?"bullet":p.motion.rot>0?"missile":"shell";
        w.projectile=p.id.c_str();w.report=d.report.c_str();w.cellSpread=wh.spread;w.percentAtMax=wh.percentAtMax;
        w.nativeVerses=wh.verses.data();w.nativeSource=&d;w.nativeType=-2;w.nativeSlot=i;
    }
    // Reject recursive mods before play. The execution graph is finite and has
    // a bounded total number of descendants; no runtime depth cutoff hides it.
    std::vector<int> state(weapons),cost(weapons),height(weapons);
    const auto visit=[&](auto&& self,int i,int depth)->bool {
        if(depth>16 || state[i]==1)return false;if(state[i]==2)return true;state[i]=1;
        int total=1,levels=1;const auto& p=*catalog->weapons[i].projectile;
        for(const auto [next,count]:{std::pair{p.shrapnelWeapon,version>=2 && p.shrapnelCount<0?std::max(3,std::abs(p.shrapnelCount)):std::abs(p.shrapnelCount)},std::pair{p.airburstWeapon,p.airburst?p.cluster:0}}) {
            if(next<0)continue;
            if(!self(self,next,depth+1))return false;
            levels=std::max(levels,1+height[next]);if(levels>16)return false;
            if(count && cost[next]>(4096-total)/count)return false;total+=cost[next]*count;
        }
        state[i]=2;cost[i]=total;height[i]=levels;return true;
    };
    for(int i=0;i<weapons;++i)if(!visit(visit,i,0))return false;
    result=std::move(catalog);return true;
}
