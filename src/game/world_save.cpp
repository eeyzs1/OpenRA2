#include "game/world.h"
#include "game/lang.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdio>

// ===================== 存档/读档 =====================
namespace {
// 二进制序列化助手：任何一步读写失败即标记 ok=false（调用方整体放弃）
// v25 writes explicit passenger state, including a loaded infantry vehicle.
constexpr char WORLD_SCHEMA_V19[8] = {'R','A','2','W','R','L','D','J'}; // v19: fractional production work
constexpr char WORLD_SCHEMA_V20[8] = {'R','A','2','W','R','L','D','K'}; // v20: failed automatic pursuit retry state
constexpr char WORLD_SCHEMA_V25[8] = {'R','A','2','W','R','L','D','P'}; // passenger state
constexpr char WORLD_SCHEMA_V29[8] = {'R','A','2','W','R','L','D','T'}; // projectile area parameters
constexpr char WORLD_SCHEMA_V28[8] = {'R','A','2','W','R','L','D','S'}; // pending weapon burst
constexpr char WORLD_SCHEMA_V27[8] = {'R','A','2','W','R','L','D','R'}; // fractional weapon ranges
constexpr char WORLD_SCHEMA_V26[8] = {'R','A','2','W','R','L','D','Q'}; // explicit timed deployment states
constexpr char WORLD_SCHEMA_V24[8] = {'R','A','2','W','R','L','D','O'}; // 64-bit cash ledger
constexpr char WORLD_SCHEMA_V23[8] = {'R','A','2','W','R','L','D','N'}; // v23: persistent traffic admission phases
constexpr char WORLD_SCHEMA_V22[8] = {'R','A','2','W','R','L','D','M'}; // v22: explicit servicing state
constexpr char WORLD_SCHEMA_V21[8] = {'R','A','2','W','R','L','D','L'}; // v21: explicit engineer capture/repair intent
constexpr char WORLD_SCHEMA_V18[8] = {'R','A','2','W','R','L','D','I'}; // 'I' = 18: projectile arcing/ROT/z
constexpr char WORLD_SCHEMA_V17[8] = {'R','A','2','W','R','L','D','H'}; // 'H' = 17: Cell.height
constexpr char WORLD_SCHEMA_V16[8] = {'R','A','2','W','R','L','D','G'}; // 'G' = 16: Ent.selling
constexpr char WORLD_SCHEMA_V15[8] = {'R','A','2','W','R','L','D','F'}; // 'F' = 15: autoHarvest + rockVehicles
constexpr char WORLD_SCHEMA_V14[8] = {'R','A','2','W','R','L','D','E'}; // 'E' = 14：Player.defProd 防御队列
constexpr char WORLD_SCHEMA_V13[8] = {'R','A','2','W','R','L','D','D'}; // 'D' = 13：ProdItem.held
constexpr char WORLD_SCHEMA_V12[8] = {'R','A','2','W','R','L','D','C'}; // 'C' = 12：货舱保存生命/军衔 + 箱子增益
constexpr char WORLD_SCHEMA_V11[8] = {'R','A','2','W','R','L','D','B'}; // 'B' = 11
constexpr char WORLD_SCHEMA_V10[8] = {'R','A','2','W','R','L','D','A'}; // 'A' = 10
constexpr char WORLD_SCHEMA_V9[8]  = {'R','A','2','W','R','L','D','9'};
struct Ser {
    FILE* f;
    bool ok = true;
    template <typename T> void w(const T& v) { if (ok && fwrite(&v, sizeof(v), 1, f) != 1) ok = false; }
    template <typename T> void r(T& v) { if (ok && fread(&v, sizeof(v), 1, f) != 1) ok = false; }
    void wbuf(const void* p, size_t n) { if (ok && n && fwrite(p, 1, n, f) != n) ok = false; }
    void rbuf(void* p, size_t n) { if (ok && n && fread(p, 1, n, f) != n) ok = false; }
    void wstr(const std::string& s) { uint32_t n = (uint32_t)s.size(); w(n); wbuf(s.data(), n); }
    void rstr(std::string& s) {
        uint32_t n = 0; r(n);
        if (!ok || n > (1u << 20)) { ok = false; return; }
        s.resize(n);
        rbuf(s.data(), n);
    }
};

using Passenger=World::Ent::GarrisonedUnit;
void serPassengerExtra(Ser& s,const Passenger& p) {
    s.w(p.oreLoad);
    s.w(p.gemLoad);
    s.w(p.ammo);
    s.w(p.atkCd);
    s.w(p.invuln);
    s.w(p.crateDmgBoost);
    s.w(p.crateArmorBoost);
    s.w(p.crateSpeedBoost);
    s.w(p.airstrikeCd);
    s.w(p.confused);
    s.w(p.gatlingHeat);
    s.w(p.gatlingStage);
    s.w(p.autoHarvest);
    s.w(p.permaControlled);
    uint32_t count=(uint32_t)p.cargo.size();s.w(count);
    for(const auto& child:p.cargo) {
        uint8_t type=(uint8_t)child.type;s.w(type);
        s.w(child.hp);
        s.w(child.kills);
        s.w(child.veterancyValue);
        s.w(child.vetRank);
        serPassengerExtra(s,child);
    }
}
void deserPassengerExtra(Ser& s,Passenger& p,int depth=0) {
    s.r(p.oreLoad);
    s.r(p.gemLoad);
    s.r(p.ammo);
    s.r(p.atkCd);
    s.r(p.invuln);
    s.r(p.crateDmgBoost);
    s.r(p.crateArmorBoost);
    s.r(p.crateSpeedBoost);
    s.r(p.airstrikeCd);
    s.r(p.confused);
    s.r(p.gatlingHeat);
    s.r(p.gatlingStage);
    s.r(p.autoHarvest);
    s.r(p.permaControlled);
    uint32_t count=0;s.r(count);
    if(!s.ok || count>64 || (depth>=2 && count)) {s.ok=false;return;}
    p.cargo.resize(count);
    for(auto& child:p.cargo) {
        uint8_t type=0;s.r(type);
        if(type>=(uint8_t)UnitType::COUNT){s.ok=false;return;}
        child.type=(UnitType)type;
        s.r(child.hp);
        s.r(child.kills);
        s.r(child.veterancyValue);
        s.r(child.vetRank);
        deserPassengerExtra(s,child,depth+1);if(!s.ok)return;
    }
}
bool validPassenger(const Passenger& p,int depth=0) {
    if((int)p.type<0 || p.type>=UnitType::COUNT || p.hp<0 || p.kills<0 || p.veterancyValue<0 || p.vetRank<0 || p.vetRank>2
        || p.oreLoad<0 || p.gemLoad<0 || p.gemLoad>p.oreLoad || p.ammo<0 || p.atkCd<0 || p.invuln<0
        || p.crateDmgBoost<0 || p.crateArmorBoost<0 || p.crateSpeedBoost<0 || p.airstrikeCd<0 || p.confused<0
        || p.gatlingHeat<0 || p.gatlingStage<0 || p.gatlingStage>2 || p.cargo.size()>64 || (depth>=2 && !p.cargo.empty()))return false;
    for(const auto& child:p.cargo)if(!validPassenger(child,depth+1))return false;
    const auto& def=unitDef(p.type);
    if(!p.cargo.empty() && World::cargoLoad(p.cargo)>def.cargoCap)return false;
    for(const auto& child:p.cargo)if(unitDef(child.type).transportSize()>def.cargoSizeLimit)return false;
    return true;
}

// projSprite 是 const char*：存档写字符串，读档映射回静态字面量（悬空指针防护）
static const char* kProjSprites[] = {
    "shell", "bullet", "flak", "tesla", "prism", "missile", "psi", "rad", "chrono", "naval", "torpedo"
};
void serWeapon(Ser& s, const WeaponDef& wd) {
    s.w(wd.damage); s.w(wd.range); s.w(wd.cooldown);
    s.w(wd.antiAir); s.w(wd.antiGround);
    std::string ps = wd.projSprite ? wd.projSprite : "shell";
    s.wstr(ps);
    s.w(wd.vsInfantry); s.w(wd.vsVehicle); s.w(wd.vsBuilding);
    s.w(wd.navalOnly); s.w(wd.splash);
    uint8_t wh = (uint8_t)wd.warhead; s.w(wh);
    s.w(wd.cellSpread);s.w(wd.percentAtMax);
}
void deserWeapon(Ser& s, WeaponDef& wd, bool schema7, bool fractionalRange,bool withArea) {
    s.r(wd.damage);
    if(fractionalRange)s.r(wd.range);else{int range=0;s.r(range);wd.range=(float)range;}
    s.r(wd.cooldown);
    s.r(wd.antiAir); s.r(wd.antiGround);
    std::string ps; s.rstr(ps);
    wd.projSprite = "shell";
    for (const char* k : kProjSprites)
        if (ps == k) { wd.projSprite = k; break; }
    s.r(wd.vsInfantry); s.r(wd.vsVehicle); s.r(wd.vsBuilding);
    s.r(wd.navalOnly); s.r(wd.splash);
    if (schema7) {
        uint8_t wh = 0; s.r(wh); wd.warhead = (WeaponDef::Warhead)wh;
    } else {
        wd.warhead = WeaponDef::Warhead::Legacy;
    }
    if(withArea){s.r(wd.cellSpread);s.r(wd.percentAtMax);}
}
void serProd(Ser& s, const ProdItem& p) {
    s.w(p.active); s.w(p.isUnit); s.w(p.typeIdx); s.w(p.progress);
    s.w(p.totalCost); s.w(p.paid); s.w(p.ready); s.w(p.held); s.w(p.workRemainder);
}
void deserProd(Ser& s, ProdItem& p, bool withHeld, bool withWork) {
    s.r(p.active); s.r(p.isUnit); s.r(p.typeIdx); s.r(p.progress);
    s.r(p.totalCost); s.r(p.paid); s.r(p.ready);
    if (withHeld) s.r(p.held); else p.held = false;
    if (withWork) s.r(p.workRemainder); else p.workRemainder = 0;
}
} // namespace

bool World::saveGame(FILE* f) const {
    if(updateInProgress || areaDamageDepth>0 || !retiredDuringUpdate.empty()) return false;
    Ser s{f};
    if (!f || numPlayers <= 0 || numPlayers > MAX_PLAYERS || players.size() != (size_t)numPlayers
        || map.w <= 0 || map.h <= 0 || map.cells.size() != (size_t)map.w * map.h
        || map.fog.size() != (size_t)numPlayers || bldOcc.size() != map.cells.size()
        || ents.size() > 65536 || freeList.size() > 65536 || projs.size() > 8192
        || effects.size() > 8192 || nukes.size() > 64 || crates.size() > 1024
        || timedBombs.size() > 1024 || evaQueue.size() > 256) return false;
    for (const auto& fog : map.fog)
        if (fog.size() != map.cells.size()) return false;
    for(int player=0;player<numPlayers;++player)
        if(players[player].money<0 || players[player].money>MAX_CREDIT_BALANCE ||
            reservedProductionFunds(player)>MAX_CREDIT_BALANCE-players[player].money) return false;
    for (const Player& p : players)
        for (int c = 0; c < PROD_CAT_N; ++c)
            if (p.unitQueue[c].size() > 64) return false;
    for (const Ent& e : ents)
        if (e.path.size() > 4096 || e.wps.size() > 4096 || e.cargo.size() > 64
            || e.garrison.size() > 64 || e.mindTargets.size() > 64 || e.failedPursuits.size()>8) return false;
    for(const auto& e:ents) {
        for(const auto& passenger:e.cargo)if(!validPassenger(passenger))return false;
        for(const auto& passenger:e.garrison)if(!validPassenger(passenger))return false;
    }
    s.wbuf(WORLD_SCHEMA_V29, sizeof(WORLD_SCHEMA_V29));
    s.w(tick); s.w(numPlayers); s.w(rng.s);
    s.w(cratesEnabled); s.w(aiAlliance);
    uint8_t mode = (uint8_t)skirmishMode;
    s.w(mode); s.w(sharedVision); s.w(shortGame); s.w(superweaponsEnabled);
    s.w(mcvRepacks);
    // 地图（含矿石余量、格高度与迷雾）
    s.w(map.w); s.w(map.h);
    for (const Cell& c : map.cells) {
        uint8_t t = (uint8_t)c.terrain, o = (uint8_t)c.overlay;
        s.w(t); s.w(o); s.w(c.variant); s.w(c.ore); s.w(c.oreMax); s.w(c.height);
    }
    for (int p = 0; p < numPlayers; p++)
        s.wbuf(map.fog[p].data(), map.fog[p].size());
    s.wbuf(bldOcc.data(), bldOcc.size() * sizeof(int));
    // 玩家
    for (const Player& p : players) {
        s.w(p.active); s.w(p.isAI); s.w(p.defeated);
        uint8_t fac = (uint8_t)p.faction;
        s.w(fac);
        uint8_t ctry = (uint8_t)p.country;
        s.w(ctry);
        s.w(p.secretLabUnlock); s.w(p.paradropCharge); s.w(p.paradropReady);
        s.w(p.colorId); s.w(p.money); s.wstr(p.name);
        serProd(s, p.bldProd);
        serProd(s, p.defProd);
        for (int c = 0; c < PROD_CAT_N; c++) serProd(s, p.unitProd[c]);
        for (int c = 0; c < PROD_CAT_N; c++) {
            uint32_t n = (uint32_t)p.unitQueue[c].size();
            s.w(n);
            for (int t : p.unitQueue[c]) s.w(t);
        }
        uint8_t pb = (uint8_t)p.placingBld;
        s.w(pb);
        s.w(p.powerSabotage); s.w(p.revealTimer);
        for (int c = 0; c < PROD_CAT_N; c++) s.w(p.vetCat[c]);
        s.w(p.stolenTech);
        s.w(p.aiDifficulty);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.w(p.swCharge[i]);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.w(p.swReady[i]);
        s.w(p.stormTimer); s.w(p.stormX); s.w(p.stormY); s.w(p.stormBoltCd);
        s.w(p.evaBaseCd); s.w(p.evaMinerCd); s.w(p.evaUnitCd);
    }
    // 实体
    {
        uint32_t n = (uint32_t)ents.size();
        s.w(n);
        for (const Ent& e : ents) {
            s.w(e.alive); s.w(e.isBuilding); s.w(e.player);
            uint8_t ut = (uint8_t)e.utype, bt = (uint8_t)e.btype, st = (uint8_t)e.state;
            s.w(ut); s.w(bt);
            s.w(e.x); s.w(e.y); s.w(e.dir); s.w(e.turretDir); s.w(e.hp);
            uint32_t pn = (uint32_t)e.path.size();
            s.w(pn);
            for (const Vec2i& wp : e.path) { s.w(wp.x); s.w(wp.y); }
            uint32_t wn = (uint32_t)e.wps.size();
            s.w(wn);
            for (const auto& wp : e.wps) { s.w(wp.first); s.w(wp.second); }
            s.w(e.pathIdx); s.w(e.moveTick); s.w(e.blockTick);
            s.w(e.walkFrame); s.w(e.walkAnim);
            s.w(e.fireAnim); s.w(e.constructAnim); s.w(e.deployAnim);
            s.w(e.burstRemaining);s.w(e.burstDelay);s.w(e.burstOwner);s.w(e.burstTarget);s.w(e.burstAir);
            s.w(st); s.w(e.atkCd); s.w(e.target); s.w(e.goalX); s.w(e.goalY);
            uint32_t failures=(uint32_t)e.failedPursuits.size();s.w(failures);
            for(const auto& failure:e.failedPursuits) {
                s.w(failure.target);s.w(failure.sourceX);s.w(failure.sourceY);
                s.w(failure.targetX);s.w(failure.targetY);s.w(failure.retryTick);
            }
            uint8_t engineerIntent=(uint8_t)e.engineerIntent;s.w(engineerIntent);
            s.w(e.oreLoad); s.w(e.gemLoad); s.w(e.oreCell.x); s.w(e.oreCell.y); s.w(e.dockRefinery); s.w(e.digTimer);
            s.w(e.autoHarvest);
            s.w(e.invuln);
            s.w(e.ammo); s.w(e.rearmTimer); s.w(e.airbase); s.w(e.orbitA);
            s.w(e.rallyX); s.w(e.rallyY); s.w(e.bldAnim); s.w(e.undeploy); s.w(e.guard); s.w(e.repairing); s.w(e.selling);
            uint32_t cn = (uint32_t)e.cargo.size();
            s.w(cn);
            for (const Ent::GarrisonedUnit& cu : e.cargo) {
                uint8_t ct = (uint8_t)cu.type; s.w(ct);
                s.w(cu.hp); s.w(cu.kills); s.w(cu.veterancyValue); s.w(cu.vetRank);
                serPassengerExtra(s,cu);
            }
            s.w(e.chrono); s.w(e.tpSick); s.w(e.camouflaged); s.w(e.camoTick);
            s.w(e.radDeployed); s.w(e.deployed); s.w(e.subReveal);
            s.w(e.kills); s.w(e.veterancyValue); s.w(e.vetRank);
            s.w(e.crateDmgBoost); s.w(e.crateArmorBoost); s.w(e.crateSpeedBoost);
            // 驻军/寄生/磁暴充电
            uint32_t gn = (uint32_t)e.garrison.size();
            s.w(gn);
            for (const Ent::GarrisonedUnit& gu : e.garrison) {
                uint8_t gt = (uint8_t)gu.type; s.w(gt);
                s.w(gu.hp); s.w(gu.kills); s.w(gu.veterancyValue); s.w(gu.vetRank);
                serPassengerExtra(s,gu);
            }
            s.w(e.parasite); s.w(e.parasiteHost); s.w(e.parasiting); s.w(e.teslaCharge);
            // P6 心灵控制
            s.w(e.mindBy); s.w(e.mindTarget);
            uint32_t mn = (uint32_t)e.mindTargets.size(); s.w(mn);
            for (EID tid : e.mindTargets) s.w(tid);
            s.w(e.origPlayer); s.w(e.permaControlled);
            // 尤复补全：YR 新单位特殊机制
            s.w(e.airstrikeCd); s.w(e.confused);
            s.w(e.magneticBy); s.w(e.magneticHeight); s.w(e.drainedBy);
            s.w(e.gatlingHeat); s.w(e.gatlingStage);
        }
        uint32_t fn = (uint32_t)freeList.size();
        s.w(fn);
        for (int id : freeList) s.w(id);
    }
    // 投射物 / 特效 / 核弹 / 补给箱 / 定时炸弹 / EVA 队列
    {
        uint32_t n = (uint32_t)projs.size();
        s.w(n);
        for (const Projectile& p : projs) {
            s.w(p.alive);
            uint8_t k = (uint8_t)p.kind;
            s.w(k);
            s.w(p.player); s.w(p.x); s.w(p.y); s.w(p.tx); s.w(p.ty);
            s.w(p.target); s.w(p.src); s.w(p.srcGarrisonSlot);
            serWeapon(s, p.w);
            s.w(p.speed); s.w(p.trail); s.w(p.hp);
            s.w(p.ox); s.w(p.oy); s.w(p.z); s.w(p.rot); s.w(p.arcing); s.w(p.proximity);
        }
    }
    {
        uint32_t n = (uint32_t)effects.size();
        s.w(n);
        for (const Effect& e : effects) {
            s.w(e.alive); s.w(e.kind); s.w(e.x); s.w(e.y); s.w(e.x2); s.w(e.y2); s.w(e.age); s.w(e.maxAge);
            s.w(e.aux); s.w(e.aux2); s.w(e.aux3);
        }
    }
    {
        uint32_t n = (uint32_t)nukes.size();
        s.w(n);
        for (const Nuke& nk : nukes) { s.w(nk.active); s.w(nk.player); s.w(nk.tx); s.w(nk.ty); s.w(nk.timer); }
    }
    {
        uint32_t n = (uint32_t)crates.size();
        s.w(n);
        for (const Crate& c : crates) { s.w(c.alive); s.w(c.x); s.w(c.y); s.w(c.kind); }
    }
    {
        uint32_t n = (uint32_t)timedBombs.size();
        s.w(n);
        for (const TimedBomb& b : timedBombs) { s.w(b.x); s.w(b.y); s.w(b.timer); s.w(b.player); s.w(b.attachedTo); s.w(b.dmg); s.w(b.radius); s.w(b.rockVehicles); }
    }
    {
        uint32_t n = (uint32_t)evaQueue.size();
        s.w(n);
        for (const EvaEvent& ev : evaQueue) { s.w(ev.player); s.wstr(ev.text); }
    }
    if(trafficGates.size()>196608 || trafficGates.size()>map.cells.size()*3) return false;
    s.w((uint32_t)trafficGates.size());
    for(const auto& gate:trafficGates) { s.w(gate.domain);s.w(gate.anchor);s.w(gate.exit);s.w(gate.until);s.w((uint8_t)gate.draining); }
    return s.ok;
}

bool World::loadGame(FILE* f) {
    if(updateInProgress || areaDamageDepth>0 || !f) return false;
    retiredDuringUpdate.clear();
    presentationEpoch=nextPresentationEpoch();
    presentationEvents.clear(); presentationSequence = 0;
    Ser s{f};
    char magic[8];
    s.rbuf(magic, 8);
    const bool schema29=s.ok && memcmp(magic,WORLD_SCHEMA_V29,8)==0;
    const bool schema28=schema29 || (s.ok && memcmp(magic,WORLD_SCHEMA_V28,8)==0);
    const bool schema27=schema28 || (s.ok && memcmp(magic,WORLD_SCHEMA_V27,8)==0);
    const bool schema26=schema27 || (s.ok && memcmp(magic,WORLD_SCHEMA_V26,8)==0);
    const bool schema25=schema26 || (s.ok && memcmp(magic,WORLD_SCHEMA_V25,8)==0);
    bool schema24 = schema25 || (s.ok && memcmp(magic,WORLD_SCHEMA_V24,8)==0);
    bool schema23 = s.ok && memcmp(magic,WORLD_SCHEMA_V23,8)==0;
    bool schema22 = s.ok && memcmp(magic,WORLD_SCHEMA_V22,8)==0;
    bool schema21 = s.ok && memcmp(magic,WORLD_SCHEMA_V21,8)==0;
    bool schema20 = s.ok && memcmp(magic, WORLD_SCHEMA_V20, 8) == 0;
    bool schema19 = s.ok && memcmp(magic, WORLD_SCHEMA_V19, 8) == 0;
    bool schema18 = s.ok && memcmp(magic, WORLD_SCHEMA_V18, 8) == 0;
    bool schema17 = s.ok && memcmp(magic, WORLD_SCHEMA_V17, 8) == 0;
    bool schema16 = s.ok && memcmp(magic, WORLD_SCHEMA_V16, 8) == 0;
    bool schema15 = s.ok && memcmp(magic, WORLD_SCHEMA_V15, 8) == 0;
    bool schema14 = s.ok && memcmp(magic, WORLD_SCHEMA_V14, 8) == 0;
    bool schema13 = s.ok && memcmp(magic, WORLD_SCHEMA_V13, 8) == 0;
    bool schema12 = s.ok && memcmp(magic, WORLD_SCHEMA_V12, 8) == 0;
    bool schema11 = s.ok && memcmp(magic, WORLD_SCHEMA_V11, 8) == 0;
    bool schema10 = s.ok && memcmp(magic, WORLD_SCHEMA_V10, 8) == 0;
    bool schema9 = s.ok && memcmp(magic, WORLD_SCHEMA_V9, 8) == 0;
    bool schema8 = s.ok && memcmp(magic, "RA2WRLD8", 8) == 0;
    bool schema7 = s.ok && memcmp(magic, "RA2WRLD7", 8) == 0;
    bool schema6 = s.ok && memcmp(magic, "RA2WRLD6", 8) == 0;
    if (!schema24 && !schema23 && !schema22 && !schema21 && !schema20 && !schema19 && !schema18 && !schema17 && !schema16 && !schema15 && !schema14 && !schema13 && !schema12 && !schema11 && !schema10 && !schema9 && !schema8 && !schema7 && !schema6) return false;
    const bool schemaAtLeast7 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14 || schema13 || schema12 || schema11 || schema10 || schema9 || schema8 || schema7;
    const bool schemaAtLeast8 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14 || schema13 || schema12 || schema11 || schema10 || schema9 || schema8;
    const bool schemaAtLeast9 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14 || schema13 || schema12 || schema11 || schema10 || schema9;
    const bool schemaAtLeast11 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14 || schema13 || schema12 || schema11;
    const bool schemaAtLeast12 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14 || schema13 || schema12;
    const bool schemaAtLeast13 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14 || schema13;
    const bool schemaAtLeast14 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15 || schema14;
    const bool schemaAtLeast15 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16 || schema15;
    const bool schemaAtLeast16 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17 || schema16;
    const bool schemaAtLeast17 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18 || schema17;
    const bool schemaAtLeast18 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19 || schema18;
    const bool schemaAtLeast19 = schema24 || schema23 || schema22 || schema21 || schema20 || schema19;
    const bool schemaAtLeast20=schema24 || schema23 || schema22 || schema21 || schema20;
    s.r(tick); s.r(numPlayers); s.r(rng.s);
    s.r(cratesEnabled); s.r(aiAlliance);
    if (!s.ok || numPlayers <= 0 || numPlayers > MAX_PLAYERS || rng.s == 0) return false;
    if (schemaAtLeast8) {
        uint8_t mode = 0;
        s.r(mode);
        if (mode >= (uint8_t)SkirmishMode::COUNT) return false;
        skirmishMode = (SkirmishMode)mode;
        s.r(sharedVision); s.r(shortGame); s.r(superweaponsEnabled);
        if (schema10 || schemaAtLeast11) s.r(mcvRepacks);
        else mcvRepacks = false;
    } else {
        skirmishMode = SkirmishMode::Battle;
        sharedVision = false;
        shortGame = false;
        superweaponsEnabled = true;
        mcvRepacks = false;
    }
    // 地图
    s.r(map.w); s.r(map.h);
    if (!s.ok || map.w <= 0 || map.h <= 0 || map.w > 512 || map.h > 512) return false;
    map.cells.resize((size_t)map.w * map.h);
    for (Cell& c : map.cells) {
        uint8_t t = 0, o = 0;
        s.r(t); s.r(o); s.r(c.variant); s.r(c.ore); s.r(c.oreMax);
        if (schemaAtLeast17) s.r(c.height); else c.height = 0;
        if (t > (uint8_t)Terrain::Bridge || o > (uint8_t)Overlay::Rock2
            || c.ore < 0 || c.oreMax < 0 || c.ore > c.oreMax) return false;
        c.terrain = (Terrain)t; c.overlay = (Overlay)o;
    }
    map.fog.assign(numPlayers, std::vector<uint8_t>((size_t)map.w * map.h));
    for (int p = 0; p < numPlayers; p++)
        s.rbuf(map.fog[p].data(), map.fog[p].size());
    for (const auto& fog : map.fog)
        for (uint8_t value : fog)
            if (value > FOG_VISIBLE) return false;
    bldOcc.assign((size_t)map.w * map.h, -1);
    s.rbuf(bldOcc.data(), bldOcc.size() * sizeof(int));
    map.bldOccRef = &bldOcc; // 重新挂载寻路占用表
    // 玩家
    players.assign(numPlayers, Player{});
    for (Player& p : players) {
        s.r(p.active); s.r(p.isAI); s.r(p.defeated);
        uint8_t fac = 0;
        s.r(fac); p.faction = (Faction)fac;
        uint8_t ctry = 0;
        s.r(ctry); p.country = (Country)ctry;
        if (fac > (uint8_t)Faction::Yuri || ctry >= (uint8_t)Country::COUNT) return false;
        s.r(p.secretLabUnlock); s.r(p.paradropCharge); s.r(p.paradropReady);
        s.r(p.colorId);
        if(schema24) s.r(p.money);
        else { int32_t oldMoney=0;s.r(oldMoney);p.money=oldMoney; }
        s.rstr(p.name);
        deserProd(s, p.bldProd, schemaAtLeast13, schemaAtLeast19);
        if (schemaAtLeast14) deserProd(s, p.defProd, true, schemaAtLeast19);
        else p.defProd = ProdItem{};
        for (int c = 0; c < PROD_CAT_N; c++) deserProd(s, p.unitProd[c], schemaAtLeast13, schemaAtLeast19);
        for (int c = 0; c < PROD_CAT_N; c++) {
            uint32_t n = 0;
            s.r(n);
            if (n > 64) { s.ok = false; break; }
            p.unitQueue[c].clear();
            for (uint32_t i = 0; i < n; i++) {
                int t = 0;
                s.r(t);
                if (t < 0 || t >= (int)UnitType::COUNT) { s.ok = false; break; }
                p.unitQueue[c].push_back(t);
            }
        }
        uint8_t pb = 0;
        s.r(pb); p.placingBld = (BldType)pb;
        if (pb > (uint8_t)BldType::COUNT) return false;
        s.r(p.powerSabotage); s.r(p.revealTimer);
        for (int c = 0; c < PROD_CAT_N; c++) s.r(p.vetCat[c]);
        s.r(p.stolenTech);
        s.r(p.aiDifficulty);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.r(p.swCharge[i]);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.r(p.swReady[i]);
        s.r(p.stormTimer); s.r(p.stormX); s.r(p.stormY); s.r(p.stormBoltCd);
        s.r(p.evaBaseCd); s.r(p.evaMinerCd); s.r(p.evaUnitCd);
    }
    // 实体
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 65536) return false;
        ents.assign(n, Ent{});
        for (Ent& e : ents) {
            s.r(e.alive); s.r(e.isBuilding); s.r(e.player);
            uint8_t ut = 0, bt = 0, st = 0;
            s.r(ut); s.r(bt);
            if (ut >= (uint8_t)UnitType::COUNT || bt >= (uint8_t)BldType::COUNT) return false;
            e.utype = (UnitType)ut; e.btype = (BldType)bt;
            s.r(e.x); s.r(e.y); s.r(e.dir); s.r(e.turretDir); s.r(e.hp);
            uint32_t pn = 0;
            s.r(pn);
            if (pn > 4096) { s.ok = false; break; }
            e.path.resize(pn);
            for (Vec2i& wp : e.path) { s.r(wp.x); s.r(wp.y); }
            if (schemaAtLeast9) {
                uint32_t wn = 0;
                s.r(wn);
                if (wn > 4096) { s.ok = false; break; }
                for (uint32_t i = 0; i < wn; ++i) {
                    float x = 0, y = 0;
                    s.r(x); s.r(y);
                    e.wps.emplace_back(x, y);
                }
            }
            s.r(e.pathIdx); s.r(e.moveTick); s.r(e.blockTick);
            s.r(e.walkFrame); s.r(e.walkAnim);
            s.r(e.fireAnim); s.r(e.constructAnim); s.r(e.deployAnim);
            if(schema28){s.r(e.burstRemaining);s.r(e.burstDelay);s.r(e.burstOwner);s.r(e.burstTarget);s.r(e.burstAir);}
            s.r(st); e.state = (UState)st;
            if (st > (uint8_t)(schema26?UState::DeployTakeoff:(schema24 || schema23 || schema22)?UState::Servicing:UState::Boarding)) return false;
            s.r(e.atkCd); s.r(e.target); s.r(e.goalX); s.r(e.goalY);
            if(schemaAtLeast20) {
                uint32_t failures=0;s.r(failures);
                if(!s.ok || failures>8) return false;
                e.failedPursuits.resize(failures);
                for(auto& failure:e.failedPursuits) {
                    s.r(failure.target);s.r(failure.sourceX);s.r(failure.sourceY);
                    s.r(failure.targetX);s.r(failure.targetY);s.r(failure.retryTick);
                }
            }
            if(schema24 || schema23 || schema22 || schema21) {
                uint8_t intent=0;s.r(intent);
                if(!s.ok || intent>(uint8_t)EngineerIntent::Repair) return false;
                e.engineerIntent=(EngineerIntent)intent;
            }
            s.r(e.oreLoad); s.r(e.gemLoad); s.r(e.oreCell.x); s.r(e.oreCell.y); s.r(e.dockRefinery); s.r(e.digTimer);
            if (schemaAtLeast15) s.r(e.autoHarvest);
            else e.autoHarvest = true;
            s.r(e.invuln);
            s.r(e.ammo); s.r(e.rearmTimer); s.r(e.airbase); s.r(e.orbitA);
            s.r(e.rallyX); s.r(e.rallyY); s.r(e.bldAnim); s.r(e.undeploy); s.r(e.guard);
            if (schemaAtLeast11) s.r(e.repairing);
            else e.repairing = false;
            if (schemaAtLeast16) s.r(e.selling);
            else e.selling = false;
            uint32_t cn = 0;
            s.r(cn);
            if (cn > 64) { s.ok = false; break; }
            e.cargo.resize(cn);
            for (Ent::GarrisonedUnit& cu : e.cargo) {
                uint8_t ct = 0;
                s.r(ct);
                if (ct >= (uint8_t)UnitType::COUNT) { s.ok = false; break; }
                cu.type = (UnitType)ct;
                if (schemaAtLeast12) {
                    s.r(cu.hp); s.r(cu.kills); s.r(cu.veterancyValue); s.r(cu.vetRank);
                    if(schema25)deserPassengerExtra(s,cu);
                } else {
                    cu.hp = unitDef(cu.type).hp;
                    cu.kills = 0; cu.veterancyValue = 0; cu.vetRank = 0;
                }
            }
            s.r(e.chrono); s.r(e.tpSick); s.r(e.camouflaged); s.r(e.camoTick);
            s.r(e.radDeployed); s.r(e.deployed); s.r(e.subReveal);
            s.r(e.kills);
            if (schemaAtLeast7) {
                s.r(e.veterancyValue); s.r(e.vetRank);
            } else {
                s.r(e.vetRank);
                e.veterancyValue = e.isBuilding ? 0
                    : (int)std::ceil(unitDef(e.utype).cost * g_gameRules.veteranRatio * e.vetRank);
            }
            if (schemaAtLeast12) {
                s.r(e.crateDmgBoost); s.r(e.crateArmorBoost); s.r(e.crateSpeedBoost);
            } else {
                e.crateDmgBoost = e.crateArmorBoost = e.crateSpeedBoost = 0;
            }
            // 驻军/寄生/磁暴充电
            uint32_t gn = 0;
            s.r(gn);
            if (gn > 64) { s.ok = false; break; }
            e.garrison.resize(gn);
            for (Ent::GarrisonedUnit& gu : e.garrison) {
                uint8_t gt = 0; s.r(gt); gu.type = (UnitType)gt;
                if (gt >= (uint8_t)UnitType::COUNT) { s.ok = false; break; }
                if (schemaAtLeast7) {
                    s.r(gu.hp); s.r(gu.kills); s.r(gu.veterancyValue); s.r(gu.vetRank);
                    if(schema25)deserPassengerExtra(s,gu);
                } else {
                    gu.hp = unitDef(gu.type).hp;
                }
            }
            s.r(e.parasite); s.r(e.parasiteHost); s.r(e.parasiting); s.r(e.teslaCharge);
            // P6 心灵控制
            s.r(e.mindBy); s.r(e.mindTarget);
            if (schemaAtLeast7) {
                uint32_t mn = 0; s.r(mn);
                if (mn > 64) { s.ok = false; break; }
                e.mindTargets.resize(mn);
                for (EID& tid : e.mindTargets) s.r(tid);
                s.r(e.origPlayer); s.r(e.permaControlled);
            } else {
                s.r(e.origPlayer);
            }
            // 尤复补全：YR 新单位特殊机制
            s.r(e.airstrikeCd); s.r(e.confused);
            if (schemaAtLeast7) {
                s.r(e.magneticBy); s.r(e.magneticHeight); s.r(e.drainedBy);
                s.r(e.gatlingHeat); s.r(e.gatlingStage);
            }
        }
        uint32_t fn = 0;
        s.r(fn);
        if (fn > 65536) s.ok = false;
        freeList.clear();
        for (uint32_t i = 0; s.ok && i < fn; i++) { int id = 0; s.r(id); freeList.push_back(id); }
    }
    // 投射物 / 特效 / 核弹 / 补给箱 / 定时炸弹 / EVA 队列
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 8192) return false;
        projs.assign(n, Projectile{});
        for (Projectile& p : projs) {
            s.r(p.alive);
            uint8_t k = 0;
            s.r(k); p.kind = (ProjKind)k;
            if (k > (uint8_t)ProjKind::Missile) return false;
            s.r(p.player); s.r(p.x); s.r(p.y); s.r(p.tx); s.r(p.ty);
            s.r(p.target); s.r(p.src);
            if (schemaAtLeast7) s.r(p.srcGarrisonSlot);
            deserWeapon(s, p.w, schemaAtLeast7,schema27,schema29);
            s.r(p.speed); s.r(p.trail); s.r(p.hp);
            if (schemaAtLeast18) {
                s.r(p.ox); s.r(p.oy); s.r(p.z); s.r(p.rot); s.r(p.arcing); s.r(p.proximity);
            } else {
                p.ox = p.x; p.oy = p.y; p.z = 0; p.rot = 0; p.arcing = false; p.proximity = false;
                if (p.kind == ProjKind::Missile) p.rot = 8;
                if (p.kind == ProjKind::Shell) p.arcing = true;
            }
        }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 8192) return false;
        effects.assign(n, Effect{});
        for (Effect& e : effects) {
            s.r(e.alive); s.r(e.kind); s.r(e.x); s.r(e.y); s.r(e.x2); s.r(e.y2); s.r(e.age); s.r(e.maxAge);
            s.r(e.aux); s.r(e.aux2); s.r(e.aux3);
        }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 64) return false;
        nukes.assign(n, Nuke{});
        for (Nuke& nk : nukes) { s.r(nk.active); s.r(nk.player); s.r(nk.tx); s.r(nk.ty); s.r(nk.timer); }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 1024) return false;
        crates.assign(n, Crate{});
        for (Crate& c : crates) { s.r(c.alive); s.r(c.x); s.r(c.y); s.r(c.kind); }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 1024) return false;
        timedBombs.assign(n, TimedBomb{});
        for (TimedBomb& b : timedBombs) {
            s.r(b.x); s.r(b.y); s.r(b.timer); s.r(b.player); s.r(b.attachedTo); s.r(b.dmg); s.r(b.radius);
            if (schemaAtLeast15) s.r(b.rockVehicles); else b.rockVehicles = false;
        }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 256) return false;
        evaQueue.clear();
        for (uint32_t i = 0; i < n; i++) {
            EvaEvent ev;
            s.r(ev.player); s.rstr(ev.text);
            evaQueue.push_back(std::move(ev));
        }
    }
    if (!s.ok) return false;

    if (!schemaAtLeast19) {
        auto migrate = [](ProdItem& p, bool unit) {
            if (!p.active) return true;
            const int limit = unit ? (int)UnitType::COUNT : (int)BldType::COUNT;
            if (p.typeIdx < 0 || p.typeIdx >= limit || p.progress < 0
                || p.totalCost < 0 || p.paid < 0 || p.paid > p.totalCost) return false;
            const int time = std::max(1, unit ? unitDef((UnitType)p.typeIdx).buildTime : bldDef((BldType)p.typeIdx).buildTime);
            // Legacy elapsed ticks used changing durations. Paid fraction is stable.
            const int64_t work = p.ready ? int64_t(time) * 1024
                : (p.totalCost ? int64_t(time) * 1024 * p.paid / p.totalCost : 0);
            p.progress = int(work / 1024); p.workRemainder = int(work % 1024);
            return true;
        };
        for (Player& p : players) {
            if (!migrate(p.bldProd, false) || !migrate(p.defProd, false)) return false;
            for (ProdItem& item : p.unitProd) if (!migrate(item, true)) return false;
        }
    }
    if(!schema24 && !schema23 && !schema22 && !schema21) for(auto& e:ents) {
        if(e.alive && !e.isBuilding && e.utype==UnitType::Engineer && (e.state==UState::Chasing || e.state==UState::Attacking)
            && valid(e.target) && ents[e.target].isBuilding)
            e.engineerIntent=ents[e.target].player==e.player?EngineerIntent::Repair:EngineerIntent::Capture;
    }
    trafficGates.clear();resetMovementCorridors();
    if(schema24 || schema23) {
        uint32_t count=0;s.r(count);
        if(!s.ok || count>196608 || count>map.cells.size()*3) return false;
        trafficGates.resize(count);
        for(size_t index=0;index<count;++index) {
            auto& gate=trafficGates[index];s.r(gate.domain);s.r(gate.anchor);s.r(gate.exit);s.r(gate.until);
            uint8_t draining=0;s.r(draining);gate.draining=draining!=0;
            if(!s.ok || draining>1 || gate.domain<0 || gate.domain>2 || gate.anchor<0 || gate.anchor>=(int)map.cells.size()
                || gate.exit<0 || gate.exit>=(int)map.cells.size() || (gate.until>tick && gate.until-tick>1000000)) return false;
            if(index && (trafficGates[index-1].domain>gate.domain
                || (trafficGates[index-1].domain==gate.domain && trafficGates[index-1].anchor>=gate.anchor))) return false;
        }
    } // v6-v22 have no historical phase: the first update derives a fresh admission.
    if (fgetc(f) != EOF || ferror(f)) return false;
    // Legacy servicing was encoded as a normal move/idle targeting a depot.
    if(!schema24 && !schema23 && !schema22) for(auto& e:ents) {
        if(e.alive && !e.isBuilding && (e.state==UState::Moving || e.state==UState::Idle)
            && valid(e.target) && ents[e.target].isBuilding && ents[e.target].btype==BldType::ServiceDepot
            && ents[e.target].player==e.player && !unitDef(e.utype).isInfantry() && !unitDef(e.utype).isAir()
            && unitDef(e.utype).pathDomain()==0 && !unitDef(e.utype).canHarvet()) e.state=UState::Servicing;
    }
    const auto finite = [](float value) { return std::isfinite(value); };
    const auto validRef = [&](EID id) {
        return id == INVALID_EID || (id >= 0 && id < (EID)ents.size());
    };
    const auto validProd = [](const ProdItem& item, bool unit) {
        const int limit = unit ? (int)UnitType::COUNT : (int)BldType::COUNT;
        return item.typeIdx >= 0 && item.typeIdx < limit
            && item.workRemainder >= 0 && item.workRemainder < 1024
            && item.progress >= 0 && item.totalCost >= 0 && item.paid >= 0
            && item.paid <= item.totalCost;
    };
    for (const Player& p : players) {
        if (p.colorId < 0 || p.colorId >= MAX_PLAYERS || p.money < 0 || p.money>MAX_CREDIT_BALANCE
            || reservedProductionFunds((int)(&p-players.data()))>MAX_CREDIT_BALANCE-p.money
            || p.aiDifficulty < 0 || p.aiDifficulty > 2
            || p.secretLabUnlock < 0 || p.secretLabUnlock >= (int)Country::COUNT
            || p.paradropCharge < 0 || p.powerSabotage < 0 || p.revealTimer < 0
            || p.stolenTech < 0 || !validProd(p.bldProd, false) || !validProd(p.defProd, false)) return false;
        for (int c = 0; c < PROD_CAT_N; ++c)
            if (!validProd(p.unitProd[c], true)) return false;
        for (int i = 0; i < (int)SWType::COUNT; ++i)
            if (p.swCharge[i] < 0) return false;
        if (p.stormTimer < 0 || p.stormBoltCd < 0 || !finite(p.stormX) || !finite(p.stormY)
            || p.evaBaseCd < 0 || p.evaMinerCd < 0 || p.evaUnitCd < 0) return false;
    }
    std::vector<uint8_t> freeSeen(ents.size(), 0);
    for (int id : freeList) {
        if (id < 0 || id >= (int)ents.size() || freeSeen[id] || ents[id].alive) return false;
        freeSeen[id] = 1;
    }
    for (size_t i = 0; i < ents.size(); ++i) {
        const Ent& e = ents[i];
        if (e.player < -1 || e.player >= numPlayers || !finite(e.x) || !finite(e.y)
            || (e.state>=UState::DeployLanding && (!e.deploying() || e.deployAnim<1 || e.deployAnim>3600
                || e.deployed!=(e.state==UState::DeployUnfolding || e.state==UState::DeployFolding)))
            || !finite(e.goalX) || !finite(e.goalY) || !finite(e.orbitA)
            || e.x < -16.0f || e.y < -16.0f || e.x > map.w + 16.0f || e.y > map.h + 16.0f
            || (e.alive && e.state==UState::Servicing && (e.isBuilding || unitDef(e.utype).isInfantry()
                || unitDef(e.utype).isAir() || unitDef(e.utype).pathDomain()!=0 || unitDef(e.utype).canHarvet()
                || !valid(e.target) || !ents[e.target].isBuilding || !serviceFacility(ents[e.target].btype)))
            || (e.engineerIntent!=EngineerIntent::None && (e.isBuilding || e.utype!=UnitType::Engineer))
            || e.pathIdx < 0 || (!e.path.empty() && e.pathIdx > (int)e.path.size()) || e.walkAnim < 0
            // Overkill leaves negative HP in dead slots until reuse; it is valid
            // historical state, whereas a living entity must not have negative HP.
            || (e.alive && e.hp < 0) || e.dir < 0 || e.dir > 7 || e.turretDir < 0 || e.turretDir > 7
            || e.vetRank < 0 || e.vetRank > 2 || e.veterancyValue < 0 || e.kills < 0
            || e.gatlingStage < 0 || e.gatlingStage > 2 || e.gatlingHeat < 0
            || e.magneticHeight<0 || e.magneticHeight>12
            || (e.alive && e.isBuilding && isRallyBuilding(e.btype)
                && !((e.rallyX==-1 && e.rallyY==-1) || map.inBounds(e.rallyX,e.rallyY)))
            || e.burstRemaining<0 || e.burstRemaining>15 || e.burstDelay<0 || e.burstDelay>600
            || (e.burstRemaining>0 && (!valid(e.burstTarget) || e.burstOwner<0 || e.burstOwner>=numPlayers || e.isBuilding))
            || (e.burstRemaining==0 && (e.burstDelay!=0 || e.burstOwner!=-1 || e.burstTarget!=INVALID_EID || e.burstAir))
            || !validRef(e.target) || !validRef(e.dockRefinery) || !validRef(e.airbase)
            || !validRef(e.parasite) || !validRef(e.parasiteHost) || !validRef(e.mindBy)
            || !validRef(e.mindTarget) || !validRef(e.magneticBy) || !validRef(e.drainedBy)) return false;
        for (const Vec2i& wp : e.path)
            if (!map.inBounds(wp.x, wp.y)) return false;
        for(size_t j=0;j<e.failedPursuits.size();++j) {
            const auto& failure=e.failedPursuits[j];
            if(failure.target<0 || !validRef(failure.target)
                || !map.inBounds(failure.sourceX,failure.sourceY) || !map.inBounds(failure.targetX,failure.targetY)
                || (failure.retryTick>tick && failure.retryTick-tick>180)) return false;
            for(size_t k=0;k<j;++k) if(e.failedPursuits[k].target==failure.target) return false;
        }
        for (const auto& wp : e.wps)
            if (!finite(wp.first) || !finite(wp.second)
                || wp.first < -1.0f || wp.second < -1.0f
                || wp.first > map.w + 1.0f || wp.second > map.h + 1.0f) return false;
        for (EID id : e.mindTargets)
            if (!validRef(id)) return false;
        for(const auto& passenger:e.cargo)if(!validPassenger(passenger))return false;
        for(const auto& passenger:e.garrison)if(!validPassenger(passenger))return false;
    }
    for (int occ : bldOcc)
        if (occ < -1 || occ > (int)ents.size()) return false;
    for (const Projectile& p : projs) {
        if (p.player < 0 || p.player >= numPlayers || !finite(p.x) || !finite(p.y)
            || !finite(p.tx) || !finite(p.ty) || !finite(p.w.vsInfantry)
            || !finite(p.w.vsVehicle) || !finite(p.w.vsBuilding) || !finite(p.w.splash)
            || !validRef(p.target) || !validRef(p.src) || p.srcGarrisonSlot < -1
            || p.speed < 0 || p.hp < 0 || p.w.damage < 0 || (!std::isfinite(p.w.range) || p.w.range < 0)
            || !finite(p.w.cellSpread) || p.w.cellSpread < -1 || p.w.cellSpread>11
            || !finite(p.w.percentAtMax) || p.w.percentAtMax<0 || p.w.percentAtMax>1
            || p.w.cooldown < 0 || p.w.warhead >= WeaponDef::Warhead::COUNT) return false;
    }
    for (const Effect& e : effects)
        if (!finite(e.x) || !finite(e.y) || !finite(e.x2) || !finite(e.y2)
            || e.age < 0 || e.maxAge < 0
            || (e.kind==9 && (e.aux<0 || e.aux>2))
            || (e.kind==3 && e.aux==1 && (e.aux2<0 || e.aux2>12))) return false;
    for (const Nuke& n : nukes)
        if (n.player < 0 || n.player >= numPlayers || !finite(n.tx) || !finite(n.ty) || n.timer < 0) return false;
    for (const Crate& c : crates)
        if (!map.inBounds(c.x, c.y) || c.kind < 0 || c.kind >= Crate::KIND_COUNT) return false;
    for (const TimedBomb& b : timedBombs)
        if (b.player < 0 || b.player >= numPlayers || !finite(b.x) || !finite(b.y)
            || !finite(b.radius) || b.timer < 0 || b.dmg < 0 || b.radius < 0
            || !validRef(b.attachedTo)) return false;
    for (const EvaEvent& ev : evaQueue)
        if (ev.player < 0 || ev.player >= numPlayers) return false;
    recomputePower();
    rebuildUnitOcc(); // 占位链表为派生数据，读档后重建
    rebuildAirOcc();
    return true;
}

void World::applyCmd(int player, const Cmd& c) {
    if (player < 0 || player >= numPlayers) return;
    std::vector<EID> own;
    own.reserve(c.ids.size());
    for (EID id : c.ids)
        if (valid(id) && ents[id].player == player) own.push_back(id);
    switch (c.type) {
        case Cmd::Move: {
            auto result = orderMove(own, c.x, c.y, c.attackMove, (c.a & 1) != 0);
            emitMoveResult(player, own.empty()?INVALID_EID:own.front(), c.x, c.y, result);
            break;
        }
        case Cmd::Attack:      emitAttackResult(player,c.a,orderAttack(own,c.a,true)); break;
        case Cmd::Harvest:     if (map.inBounds(c.a, c.b)) orderHarvest(own, c.a, c.b); break;
        case Cmd::ReturnHarvester: orderReturnToRefinery(own, c.a); break;
        case Cmd::Stop:        orderStop(own); break;
        case Cmd::Deploy:      emitDeployResult(player,orderDeployment(own)); break;
        case Cmd::Capture:     emitEngineerResult(player,c.a,EngineerIntent::Capture,orderCapture(own,c.a,true)); break;
        case Cmd::Repair:      emitEngineerResult(player,c.a,EngineerIntent::Repair,orderRepair(own,c.a,true)); break;
        case Cmd::Scatter:     orderScatter(own); break;
        case Cmd::Guard:       orderGuard(own); break;
        case Cmd::Board:       emitEntryResult(player,c.a,EntryKind::Board,orderBoard(own,c.a,true)); break;
        case Cmd::Unload:      emitReleaseResult(player,own.empty()?INVALID_EID:own.front(),ReleaseKind::Unload,orderUnload(own,true)); break;
        case Cmd::Garrison:    emitEntryResult(player,c.a,EntryKind::Garrison,orderGarrison(own,c.a,true)); break;
        case Cmd::Ungarrison:  emitReleaseResult(player,own.empty()?INVALID_EID:own.front(),ReleaseKind::Ungarrison,orderUngarrison(own,true)); break;
        case Cmd::RadDeploy:   emitDeployResult(player,orderDeployment(own,true)); break;
        case Cmd::Paradrop:
            // 优先级与 HUD/EVA 一致：伞兵 > 侦察机 > 心灵揭示（共用充能槽）
            if (hasParadropSource(player)) orderParadrop(player, c.x, c.y);
            else if (hasSpyPlaneSource(player)) orderSpyPlane(player, c.x, c.y);
            else orderPsychicReveal(player, c.x, c.y);
            break;
        case Cmd::Service:     emitServiceResult(player,c.a,orderService(own,c.a,true)); break;
        case Cmd::StartUnitProd: case Cmd::CancelUnitProd: case Cmd::StartBldProd:
        case Cmd::CancelBldProd: case Cmd::HoldUnitProd: case Cmd::HoldBldProd:
            applyProductionCommand(player,c);
            break;
        case Cmd::PlaceBuilding:
            applyPlacementCommand(player,c.a,c.x,c.y,c.b);
            break;
        case Cmd::SetRally:
            if(!own.empty()) emitRallyResult(player,orderRally(own[0],c.x,c.y,(c.a&1)!=0),c.x,c.y);
            break;
        case Cmd::SellBuilding: case Cmd::RepairBuilding: {
            PresentationEvent event;event.kind=PresentationKind::BuildingOrder;
            event.tick=tick;event.sequence=presentationSequence++;event.player=player;
            event.entity=c.ids.empty()?INVALID_EID:c.ids.front();
            auto& result=event.building;
            result.action=c.type==Cmd::SellBuilding?BuildingAction::Sell:BuildingAction::Repair;
            if(!valid(event.entity) || !ents[event.entity].isBuilding)
                result.reason=BuildingRejectReason::InvalidTarget;
            else {
                auto& building=ents[event.entity];const auto& def=bldDef(building.btype);
                event.x=building.x+def.w*0.5f;event.y=building.y+def.h*0.5f;event.positional=true;
                event.terrainHeight=map.at((int)building.x,(int)building.y).height;
                if(building.player!=player)result.reason=BuildingRejectReason::WrongOwner;
                else if(building.selling)result.reason=BuildingRejectReason::Selling;
                else if(result.action==BuildingAction::Sell && building.btype==BldType::ConYard)
                    result.reason=BuildingRejectReason::ConstructionYard;
                else if(result.action==BuildingAction::Repair && building.constructAnim>0)
                    result.reason=BuildingRejectReason::Constructing;
                else if(result.action==BuildingAction::Repair && building.hp>=def.hp && !building.repairing)
                    result.reason=BuildingRejectReason::FullHealth;
                else {
                    if(result.action==BuildingAction::Sell)sellBuilding(event.entity);
                    else repairBuilding(event.entity);
                    result.accepted=true;result.repairing=building.repairing;
                }
            }
            if(presentationEvents.size()>=4096)presentationEvents.pop_front();
            presentationEvents.push_back(event);
            break;
        }
        case Cmd::LaunchSW: {
            SuperweaponOrderResult result;result.type=c.a;result.reason=superweaponRejection(player,c.a,c.x,c.y);
            if(result.reason==SuperweaponRejectReason::None) {
                if(c.a==(int)SWType::ChronoShift) {
                    const bool source=std::any_of(c.ids.begin(),c.ids.end(),[&](EID id){return chronoSourceEligible(player,id);});
                    if(!source) result.reason=SuperweaponRejectReason::NoSource;
                    else if(!(result.accepted=launchChronoShift(player,c.ids,c.x,c.y,&result.transferred))) result.reason=SuperweaponRejectReason::Blocked;
                } else result.accepted=launchSW(player,(SWType)c.a,c.x,c.y);
            }
            PresentationEvent event;event.kind=PresentationKind::SuperweaponOrder;event.tick=tick;event.sequence=presentationSequence++;
            event.player=player;event.x=std::isfinite(c.x)?c.x:0;event.y=std::isfinite(c.y)?c.y:0;
            event.positional=std::isfinite(c.x) && std::isfinite(c.y) && c.x>=0 && c.y>=0 && c.x<map.w && c.y<map.h;
            event.superweapon=result;if(presentationEvents.size()>=4096) presentationEvents.pop_front();presentationEvents.push_back(event);
            break;
        }
        default: break;
    }
}

// FNV-1a 校验和：覆盖影响模拟的全部状态（联机双端定期比对，不一致即不同步）
uint32_t World::checksum() const {
    struct Hash {
        uint32_t value = 2166136261u;
        void u8(uint8_t v) { value = (value ^ v) * 16777619u; }
        void u32(uint32_t v) {
            u8((uint8_t)v); u8((uint8_t)(v >> 8)); u8((uint8_t)(v >> 16)); u8((uint8_t)(v >> 24));
        }
        void i32(int32_t v) { u32((uint32_t)v); }
        void u64(uint64_t v) { u32((uint32_t)v); u32((uint32_t)(v >> 32)); }
        void boolean(bool v) { u8(v ? 1u : 0u); }
        void fp(float v) {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(v));
            std::memcpy(&bits, &v, sizeof(bits));
            u32(bits);
        }
        void size(size_t v) { u64((uint64_t)v); }
    } h;
    auto hashPassenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->void {
        h.u8((uint8_t)p.type);
        h.i32(p.hp);
        h.i32(p.kills);
        h.i32(p.veterancyValue);
        h.i32(p.vetRank);
        h.i32(p.oreLoad);
        h.i32(p.gemLoad);
        h.i32(p.ammo);
        h.i32(p.atkCd);
        h.i32(p.invuln);
        h.i32(p.crateDmgBoost);
        h.i32(p.crateArmorBoost);
        h.i32(p.crateSpeedBoost);
        h.i32(p.airstrikeCd);
        h.i32(p.confused);
        h.i32(p.gatlingHeat);
        h.i32(p.gatlingStage);
        h.boolean(p.autoHarvest);h.boolean(p.permaControlled);h.size(p.cargo.size());
        for(const auto& child:p.cargo)self(self,child);
    };
    auto prod = [&](const ProdItem& p) {
        h.boolean(p.active); h.boolean(p.isUnit); h.i32(p.typeIdx); h.i32(p.progress); h.i32(p.workRemainder);
        h.i32(p.totalCost); h.i32(p.paid); h.boolean(p.ready); h.boolean(p.held);
    };
    auto weapon = [&](const WeaponDef& w) {
        h.i32(w.damage); h.fp(w.range); h.i32(w.cooldown);
        h.boolean(w.antiAir); h.boolean(w.antiGround);
        h.fp(w.vsInfantry); h.fp(w.vsVehicle); h.fp(w.vsBuilding);
        h.boolean(w.navalOnly); h.fp(w.splash); h.fp(w.cellSpread);h.fp(w.percentAtMax); h.u8((uint8_t)w.warhead);
    };

    h.u64(tick); h.i32(numPlayers); h.u64(rng.s);
    h.boolean(cratesEnabled); h.boolean(aiAlliance); h.u8((uint8_t)skirmishMode);
    h.boolean(sharedVision); h.boolean(shortGame); h.boolean(superweaponsEnabled);
    h.boolean(mcvRepacks);
    h.i32(map.w); h.i32(map.h); h.size(map.cells.size());
    for (const Cell& c : map.cells) {
        h.u8((uint8_t)c.terrain); h.u8((uint8_t)c.overlay); h.u8(c.variant);
        h.i32(c.ore); h.i32(c.oreMax); h.u8(c.height);
    }
    h.size(map.fog.size());
    for (const auto& fog : map.fog) {
        h.size(fog.size());
        for (uint8_t value : fog) h.u8(value);
    }
    h.size(bldOcc.size());
    for (int value : bldOcc) h.i32(value);

    h.size(players.size());
    for (const Player& p : players) {
        h.boolean(p.active); h.boolean(p.isAI); h.boolean(p.defeated);
        h.u8((uint8_t)p.faction); h.u8((uint8_t)p.country);
        h.i32(p.secretLabUnlock); h.i32(p.paradropCharge); h.boolean(p.paradropReady);
        h.i32(p.colorId); h.u64((uint64_t)p.money); h.i32(p.powerMade); h.i32(p.powerUsed);
        prod(p.bldProd);
        prod(p.defProd);
        for (const ProdItem& item : p.unitProd) prod(item);
        for (const auto& queue : p.unitQueue) {
            h.size(queue.size());
            for (int type : queue) h.i32(type);
        }
        h.u8((uint8_t)p.placingBld); h.i32(p.powerSabotage); h.i32(p.revealTimer);
        for (bool veteran : p.vetCat) h.boolean(veteran);
        h.i32(p.stolenTech); h.i32(p.aiDifficulty);
        for (int charge : p.swCharge) h.i32(charge);
        for (bool ready : p.swReady) h.boolean(ready);
        h.i32(p.stormTimer); h.fp(p.stormX); h.fp(p.stormY); h.i32(p.stormBoltCd);
        h.i32(p.evaBaseCd); h.i32(p.evaMinerCd); h.i32(p.evaUnitCd);
    }

    h.size(ents.size());
    for (const Ent& e : ents) {
        h.boolean(e.alive);
        if (!e.alive) continue;
        h.boolean(e.isBuilding); h.i32(e.player); h.u8((uint8_t)e.utype); h.u8((uint8_t)e.btype);
        h.fp(e.x); h.fp(e.y); h.i32(e.dir); h.i32(e.turretDir); h.i32(e.hp);
        h.size(e.path.size());
        for (const Vec2i& wp : e.path) { h.i32(wp.x); h.i32(wp.y); }
        h.size(e.wps.size());
        for (const auto& wp : e.wps) { h.fp(wp.first); h.fp(wp.second); }
        h.i32(e.pathIdx); h.i32(e.moveTick); h.i32(e.blockTick);
        h.i32(e.walkFrame); h.i32(e.walkAnim); h.i32(e.fireAnim); h.i32(e.constructAnim); h.i32(e.deployAnim);
        h.i32(e.burstRemaining);h.i32(e.burstDelay);h.i32(e.burstOwner);h.i32(e.burstTarget);h.boolean(e.burstAir);
        h.u8((uint8_t)e.state); h.i32(e.atkCd); h.i32(e.target); h.fp(e.goalX); h.fp(e.goalY);
        h.size(e.failedPursuits.size());
        for(const auto& failure:e.failedPursuits) {
            h.i32(failure.target);h.i32(failure.sourceX);h.i32(failure.sourceY);
            h.i32(failure.targetX);h.i32(failure.targetY);h.u64(failure.retryTick);
        }
        h.u8((uint8_t)e.engineerIntent);
        h.i32(e.oreLoad); h.i32(e.gemLoad); h.i32(e.oreCell.x); h.i32(e.oreCell.y);
        h.i32(e.dockRefinery); h.i32(e.digTimer); h.boolean(e.autoHarvest); h.i32(e.invuln);
        h.i32(e.ammo); h.i32(e.rearmTimer); h.i32(e.airbase); h.fp(e.orbitA);
        h.i32(e.rallyX); h.i32(e.rallyY); h.i32(e.bldAnim); h.i32(e.undeploy); h.boolean(e.guard);
        h.boolean(e.repairing); h.boolean(e.selling);
        h.size(e.cargo.size());
        for (const Ent::GarrisonedUnit& cu : e.cargo) {
            hashPassenger(hashPassenger,cu);
        }
        h.i32(e.chrono); h.i32(e.tpSick); h.boolean(e.camouflaged); h.i32(e.camoTick);
        h.boolean(e.radDeployed); h.boolean(e.deployed); h.i32(e.subReveal);
        h.i32(e.kills); h.i32(e.veterancyValue); h.i32(e.vetRank);
        h.i32(e.crateDmgBoost); h.i32(e.crateArmorBoost); h.i32(e.crateSpeedBoost);
        h.size(e.garrison.size());
        for (const Ent::GarrisonedUnit& gu : e.garrison) {
            hashPassenger(hashPassenger,gu);
        }
        h.i32(e.parasite); h.i32(e.parasiteHost); h.boolean(e.parasiting); h.i32(e.teslaCharge);
        h.i32(e.mindBy); h.i32(e.mindTarget); h.size(e.mindTargets.size());
        for (EID id : e.mindTargets) h.i32(id);
        h.i32(e.origPlayer); h.boolean(e.permaControlled); h.i32(e.airstrikeCd); h.i32(e.confused);
        h.i32(e.magneticBy); h.i32(e.magneticHeight); h.i32(e.drainedBy);
        h.i32(e.gatlingHeat); h.i32(e.gatlingStage);
    }
    h.size(freeList.size());
    for (int id : freeList) h.i32(id);

    h.size(projs.size());
    for (const Projectile& p : projs) {
        h.boolean(p.alive); h.u8((uint8_t)p.kind); h.i32(p.player);
        h.fp(p.x); h.fp(p.y); h.fp(p.tx); h.fp(p.ty); h.i32(p.target); h.i32(p.src);
        h.i32(p.srcGarrisonSlot); weapon(p.w); h.i32(p.speed); h.i32(p.trail); h.i32(p.hp);
        h.fp(p.ox); h.fp(p.oy); h.fp(p.z); h.i32(p.rot); h.boolean(p.arcing); h.boolean(p.proximity);
    }
    h.size(nukes.size());
    for (const Nuke& n : nukes) {
        h.boolean(n.active); h.i32(n.player); h.fp(n.tx); h.fp(n.ty); h.i32(n.timer);
    }
    h.size(crates.size());
    for (const Crate& c : crates) {
        h.boolean(c.alive); h.i32(c.x); h.i32(c.y); h.i32(c.kind);
    }
    h.size(timedBombs.size());
    for (const TimedBomb& b : timedBombs) {
        h.fp(b.x); h.fp(b.y); h.i32(b.timer); h.i32(b.player); h.i32(b.attachedTo);
        h.i32(b.dmg); h.fp(b.radius); h.boolean(b.rockVehicles);
    }
    h.size(trafficGates.size());
    for(const auto& gate:trafficGates) {
        h.i32(gate.domain);h.i32(gate.anchor);h.i32(gate.exit);h.u64(gate.until);h.boolean(gate.draining);
    }
    return h.value;
}

