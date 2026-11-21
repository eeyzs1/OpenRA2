#pragma once
#include "game/data.h"
#include "core/ini.h"
#include <array>
#include <string>
#include <memory>
#include <vector>

struct NativeProjectileData {
    std::string id,image;
    ProjectileDef motion;
    int shrapnelWeapon=-1,shrapnelCount=0,airburstWeapon=-1,cluster=1;
    bool level=false,flakScatter=false,airburst=false;
};
struct NativeWarheadData {
    std::string id;
    std::array<float,11> verses{};
    float spread=0,percentAtMax=1;
    bool temporal=false,affectsAllies=true,psychicDamage=false,radiation=false;
    int infantryDeath=0;
};
struct NativeWeaponData {
    std::string id,report;
    int index=-1,projectileIndex=-1,warheadIndex=-1;
    const NativeProjectileData* projectile=nullptr;
    WeaponDef weapon;
    bool laser=false,houseColor=false,sonic=false,electric=false,radBeam=false,suicide=false;
    int laserDuration=15,ambientDamage=0,radLevel=0;
    const NativeWarheadData* warhead=nullptr;
    uint32_t laserColor=0xff0000;
};
struct NativeWeaponCatalog {
    int version=1;
    std::vector<NativeProjectileData> projectiles;
    std::vector<NativeWarheadData> warheads;
    std::vector<NativeWeaponData> weapons;
};
bool parseNativeWeaponCatalog(const Ini& source,bool required,std::shared_ptr<const NativeWeaponCatalog>& catalog);

struct NativeGun {
    bool present=false;
    std::string id,projectile,report;
    std::array<float,11> verses{};
    WeaponDef weapon;
};
struct NativeGunProfile {
    bool available=false;
    int mode=0; // 0 direct, 1 occupant-only, 2 dedicated aircraft spawner
    int selector=0; // 0 general, 1 deployed infantry, 2 C4, 3 dog scanner, 4 staged gatling
    std::array<NativeGun,12> slots; // AG/AA/elite AG/elite AA for each gatling stage; ordinary guns use the first four
    std::array<std::array<int,3>,2> thresholds{};
    int rateUp=0,rateDown=0;
    std::array<int,2> abilities{}; // FIREPOWER=1, ROF=2, STRONGER=4
    float veteranCombat=1,veteranRof=1,veteranArmor=1;
    bool direct() const {return available && mode==0;}
    bool staged() const {return direct() && selector==4;}
    int stage(int heat,int rank) const {const auto& t=thresholds[rank>=2?1:0];return heat>=t[1]?2:heat>=t[0]?1:0;}
};
bool parseNativeGuns(const Ini::Section* source,int type,NativeGunProfile& profile,const NativeWeaponCatalog* catalog=nullptr);
