// Frozen v22 writer before persistent traffic admission phases. Keep its wire format unchanged.
#include "game/world.h"
#include "game/lang.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdio>

// ===================== 存档/读档 =====================
namespace {
// 二进制序列化助手：任何一步读写失败即标记 ok=false（调用方整体放弃）
// Frozen v22: retain the historical field order.新增字段只允许追加到对应结构的既定位置，
// 并必须由 schema 分支提供旧版默认值，避免“结构体内存快照”式隐式 ABI。
constexpr char WORLD_SCHEMA_V19[8] = {'R','A','2','W','R','L','D','J'}; // v19: fractional production work
constexpr char WORLD_SCHEMA_V22[8] = {'R','A','2','W','R','L','D','M'}; // v22: explicit engineer capture/repair intent
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

// projSprite 是 const char*：存档写字符串，读档映射回静态字面量（悬空指针防护）
static const char* kProjSprites[] = {
    "shell", "bullet", "flak", "tesla", "prism", "missile", "psi", "rad", "chrono", "naval", "torpedo"
};
void serWeapon(Ser& s, const WeaponDef& wd) {
    s.w(wd.damage); s.w((int)wd.range); s.w(wd.cooldown);
    s.w(wd.antiAir); s.w(wd.antiGround);
    std::string ps = wd.projSprite ? wd.projSprite : "shell";
    s.wstr(ps);
    s.w(wd.vsInfantry); s.w(wd.vsVehicle); s.w(wd.vsBuilding);
    s.w(wd.navalOnly); s.w(wd.splash);
    uint8_t wh = (uint8_t)wd.warhead; s.w(wh);
}
void deserWeapon(Ser& s, WeaponDef& wd, bool schema7) {
    s.r(wd.damage); s.r(wd.range); s.r(wd.cooldown);
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

bool writeLegacyV22(const World& world, FILE* f) {
    const auto& tick = world.tick;
    const auto& numPlayers = world.numPlayers;
    const auto& rng = world.rng;
    const auto& cratesEnabled = world.cratesEnabled;
    const auto& aiAlliance = world.aiAlliance;
    const auto& skirmishMode = world.skirmishMode;
    const auto& sharedVision = world.sharedVision;
    const auto& shortGame = world.shortGame;
    const auto& superweaponsEnabled = world.superweaponsEnabled;
    const auto& mcvRepacks = world.mcvRepacks;
    const auto& map = world.map;
    const auto& bldOcc = world.bldOcc;
    const auto& players = world.players;
    const auto& ents = world.ents;
    const auto& freeList = world.freeList;
    const auto& projs = world.projs;
    const auto& effects = world.effects;
    const auto& nukes = world.nukes;
    const auto& crates = world.crates;
    const auto& timedBombs = world.timedBombs;
    const auto& evaQueue = world.evaQueue;
    using Ent=World::Ent; using Crate=World::Crate; using TimedBomb=World::TimedBomb; using EvaEvent=World::EvaEvent;


    Ser s{f};
    if (!f || numPlayers <= 0 || numPlayers > MAX_PLAYERS || players.size() != (size_t)numPlayers
        || map.w <= 0 || map.h <= 0 || map.cells.size() != (size_t)map.w * map.h
        || map.fog.size() != (size_t)numPlayers || bldOcc.size() != map.cells.size()
        || ents.size() > 65536 || freeList.size() > 65536 || projs.size() > 8192
        || effects.size() > 8192 || nukes.size() > 64 || crates.size() > 1024
        || timedBombs.size() > 1024 || evaQueue.size() > 256) return false;
    for (const auto& fog : map.fog)
        if (fog.size() != map.cells.size()) return false;
    for(const auto& p:players)
        if(p.money<std::numeric_limits<int32_t>::min() || p.money>std::numeric_limits<int32_t>::max()) return false;
    for (const Player& p : players)
        for (int c = 0; c < PROD_CAT_N; ++c)
            if (p.unitQueue[c].size() > 64) return false;
    for (const Ent& e : ents)
        if (e.path.size() > 4096 || e.wps.size() > 4096 || e.cargo.size() > 64
            || e.garrison.size() > 64 || e.mindTargets.size() > 64 || e.failedPursuits.size()>8) return false;
    s.wbuf(WORLD_SCHEMA_V22, sizeof(WORLD_SCHEMA_V22));
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
        s.w(p.colorId); s.w((int32_t)p.money); s.wstr(p.name);
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
            s.w(st); s.w(e.atkCd); s.w(e.target); s.w(e.goalX); s.w(e.goalY);
            uint32_t failures=(uint32_t)e.failedPursuits.size();s.w(failures);
            for(const auto& failure:e.failedPursuits) {
                s.w(failure.target);s.w(failure.sourceX);s.w(failure.sourceY);
                s.w(failure.targetX);s.w(failure.targetY);s.w(failure.retryTick);
            }
            s.w((uint8_t)e.engineerIntent);
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
    return s.ok;
}
