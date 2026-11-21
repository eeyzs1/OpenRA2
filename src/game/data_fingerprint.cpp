#include "game/data.h"
#include <bit>
#include <cmath>
#include <limits>
#include <string_view>

namespace {
class RulesHash {
    Sha256 hash;
public:
    bool valid=true;
    void flag(bool v){const uint8_t b=v?1:0;hash.update(&b,1);}
    void u32(uint32_t v){uint8_t b[4]{};for(int i=0;i<4;++i)b[i]=(uint8_t)(v>>(i*8));hash.update(b,4);}
    void count(uint64_t v){u32((uint32_t)v);u32((uint32_t)(v>>32));}
    void real(float v){static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
        if(!std::isfinite(v)){valid=false;return;}u32(std::bit_cast<uint32_t>(v==0?0.f:v));}
    void text(std::string_view v){count(v.size());hash.update(v);}
    void projectile(const ProjectileDef& p){
        // Psychic is a simulation dispatch identity even when its numeric
        // profile matches Invisible. Image remains presentation-only.
        text(p.name?p.name:"");flag(p.arcing);flag(p.inviso);flag(p.vertical);flag(p.inaccurate);
        flag(p.proximity);u32(p.rot);flag(p.aa);flag(p.ag);u32(p.speedLeptons);}
    void weapon(const WeaponDef& w){u32(w.damage);real(w.range);u32(w.cooldown);flag(w.antiAir);flag(w.antiGround);
        // Proj selects special simulation effects as well as presentation.
        text(w.projSprite?w.projSprite:"shell");real(w.vsInfantry);real(w.vsVehicle);real(w.vsBuilding);
        flag(w.navalOnly);real(w.splash);u32((uint32_t)w.warhead);u32(w.burst);u32(w.burstDelayMin);u32(w.burstDelayMax);
        u32(w.speedLeptons);real(w.cellSpread);real(w.percentAtMax);u32(w.nativeType);u32(w.nativeSlot);
        flag(w.nativeVerses!=nullptr);if(w.nativeVerses)for(int i=0;i<(int)Armor::COUNT;++i)real(w.nativeVerses[i]);
        projectile(projectileForWeapon(w));}
    Sha256Digest digest()const{return hash.digest();}
};
}

std::optional<Sha256Digest> simulationRulesFingerprint() {
    RulesHash h;h.text("OpenRA2 human LAN effective rules");h.u32(2);h.u32(LOGIC_FPS);
    h.u32((uint32_t)UnitType::COUNT);h.u32((uint32_t)BldType::COUNT);h.u32((uint32_t)SWType::COUNT);
    h.u32((uint32_t)Armor::COUNT);h.u32((uint32_t)WeaponDef::Warhead::COUNT);
    const auto& r=g_gameRules;
    h.u32(r.maxMoney);h.u32(r.startingMoneyCap);h.u32(r.buildingAnimationTicks);h.u32(r.fireAnimationTicks);h.u32(r.deathAnimationTicks);
    h.u32(r.chopperLandingTicks);h.u32(r.chopperDeployTicks);h.u32(r.chopperDeployFacing);h.real(r.lowPowerSpeedFactor);
    h.u32(r.carrierRegenerateTicks);h.u32(r.carrierReloadTicks);h.u32(r.carrierLaunchDelayTicks);
    h.u32(r.destroyerRegenerateTicks);h.u32(r.destroyerReloadTicks);h.u32(r.destroyerLaunchDelayTicks);
    for(float v:r.veteranismDmgBonus)h.real(v);for(float v:r.veteranArmorBonus)h.real(v);
    for(float v:r.veteranSpeedBonus)h.real(v);for(float v:r.veteranRofBonus)h.real(v);for(int v:r.veteranSelfHeal)h.u32(v);
    h.real(r.veteranRatio);h.u32(r.bioReactorPowerPerOccupant);h.real(r.grinderRefund);
    for(const auto& row:r.warheadVerses)for(float v:row)h.real(v);
    for(const auto& area:r.warheadArea){h.real(area.cellSpread);h.real(area.percentAtMax);}
    h.u32(r.crateInterval);h.u32(r.oreRegrowRate);
    for(int i=0;i<(int)UnitType::COUNT;++i){const auto& d=unitDef((UnitType)i);h.u32((uint32_t)d.type);
        h.u32(d.cost);h.u32(d.buildTime);h.u32(d.hp);h.u32(d.speed);h.u32(d.sight);h.u32((uint32_t)d.armor);h.u32((uint32_t)d.move);
        h.weapon(d.weapon);h.u32(d.factionMask);h.u32((uint32_t)d.prereq);h.u32(d.ammo);h.u32(d.cargoCap);
        h.flag(d.elite!=nullptr);if(d.elite)h.weapon(*d.elite);h.u32((uint32_t)d.countryReq);h.u32(d.passengerSize);h.u32(d.cargoSizeLimit);
        h.flag(d.hasAirWeapon);if(d.hasAirWeapon)h.weapon(d.airWeapon);h.u32((uint32_t)(int32_t)d.bridgeClearance);}
    for(int i=0;i<(int)BldType::COUNT;++i){const auto& d=bldDef((BldType)i);h.u32((uint32_t)d.type);
        h.u32(d.cost);h.u32(d.buildTime);h.u32(d.hp);h.u32(d.w);h.u32(d.h);h.u32(d.power);h.u32(d.sight);h.weapon(d.weapon);
        h.u32(d.factionMask);h.u32((uint32_t)d.prereq);h.flag(d.capturable);h.u32(d.garrisonCap);h.u32((uint32_t)d.countryReq);}
    for(int i=0;i<(int)SWType::COUNT;++i){const auto& d=swDef((SWType)i);h.u32((uint32_t)d.type);h.u32(d.chargeTime);h.u32((uint32_t)d.fromBld);}
    h.weapon(giDeployedWeapon());h.weapon(ggiDeployedWeapon());h.weapon(siegeChopperDeployedWeapon());h.weapon(magnetronBuildingWeapon());
    const auto projectiles=projectileRulesSnapshot();h.projectile(projectiles.fallback);
    h.count(projectiles.definitions.size());for(const auto& [key,p]:projectiles.definitions){h.text(key);h.projectile(p);}
    h.count(projectiles.bridges.size());for(const auto& [key,value]:projectiles.bridges){h.text(key);h.text(value);}
    return h.valid?std::optional<Sha256Digest>{h.digest()}:std::nullopt;
}
