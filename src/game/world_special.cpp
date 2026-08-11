#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

bool World::hasParadropSource(int player) const {
    if (player < 0 || player >= numPlayers) return false;
    bool hasAFC = false;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player != player) continue;
        if (e.btype == BldType::TechAirport) return true; // 科技机场：任何阵营均提供伞兵
        if (e.btype == BldType::AirForceCmd) hasAFC = true;
    }
    // 美国特色：空指部附带伞兵支援
    return hasAFC && countryHasParadrop(players[player].country);
}

void World::updateParadrop() {
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        bool src = hasParadropSource(pi) || hasSpyPlaneSource(pi) || hasPsychicRevealSource(pi);
        if (!src) { p.paradropCharge = 0; p.paradropReady = false; continue; }
        if (p.paradropReady) continue;
        if (++p.paradropCharge >= PARADROP_TIME) {
            p.paradropReady = true;
            const char* nm = hasParadropSource(pi) ? TR(S::Paradrop)
                           : (hasSpyPlaneSource(pi) ? TR(S::SpyPlane) : TR(S::PsychicReveal));
            eva(pi, TextFormat(TR(S::EvaSWReadyFmt), nm));
            if (pi == 0) g_sfx.play(Sfx::Ready, 0.7f);
        }
    }
}

bool World::hasSpyPlaneSource(int player) const {
    if (player < 0 || player >= numPlayers) return false;
    Faction f = players[player].faction;
    if (f != Faction::Soviet && f != Faction::China) return false;
    return hasBld(player, BldType::Radar);
}

bool World::hasPsychicRevealSource(int player) const {
    if (player < 0 || player >= numPlayers) return false;
    if (players[player].faction != Faction::Yuri) return false;
    return hasBld(player, BldType::PsychicSensor);
}

// 空投一波基础步兵到目标点周围
// 美国空指部：8 GI；科技机场：盟军 6 / 苏军 9 / 其他 6（form-plan 6/9/6）
void World::orderParadrop(int player, float x, float y) {
    if (player < 0 || player >= numPlayers) return;
    Player& p = players[player];
    if (!p.paradropReady || !hasParadropSource(player)) return;
    if (!map.inBounds((int)x, (int)y) || map.at((int)x, (int)y).terrain == Terrain::Water) return;
    UnitType infT = p.faction == Faction::Allies ? UnitType::GI
                  : p.faction == Faction::Soviet ? UnitType::Conscript
                  : p.faction == Faction::Yuri ? UnitType::Initiate : UnitType::PLA;
    bool fromAirport = false;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player != player) continue;
        if (e.btype == BldType::TechAirport) { fromAirport = true; break; }
    }
    // 有空指部且美国：优先美国伞兵 8；仅机场时按阵营 6/9/6
    int want = 8;
    if (fromAirport && !countryHasParadrop(p.country)) {
        want = (p.faction == Faction::Soviet) ? 9 : 6;
    } else if (fromAirport && countryHasParadrop(p.country)) {
        // 美国同时占有机场：仍按美国空指编制 8（独立冷却未拆分，共享充能槽）
        want = 8;
    }
    int dropped = 0;
    for (int k = 0; k < want; k++) {
        bool ok = false;
        for (int r = 0; r <= 3 && !ok; r++)
            for (int dy = -r; dy <= r && !ok; dy++)
                for (int dx = -r; dx <= r && !ok; dx++) {
                    if (std::max(abs(dx), abs(dy)) != r) continue;
                    int nx = (int)x + dx, ny = (int)y + dy;
                    if (!map.passable(nx, ny) || map.at(nx, ny).terrain == Terrain::Water) continue;
                    if (bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
                    EID u = spawnUnit(player, infT, nx + 0.5f, ny + 0.5f);
                    Effect ef; ef.kind = 9; ef.x = nx + 0.5f; ef.y = ny + 0.5f; ef.maxAge = 18; // 落地光效
                    effects.push_back(ef);
                    (void)u;
                    dropped++;
                    ok = true;
                }
    }
    if (dropped == 0) return; // 无落点：不扣充能
    p.paradropReady = false;
    p.paradropCharge = 0;
    eva(player, TR(S::EvaParadropDrop));
    g_sfx.playAt(Sfx::Place, x, y);
    // 空投区域短暂显形（双方都能看到伞兵落地）
    map.reveal(player, (int)x, (int)y, 6);
}

void World::orderSpyPlane(int player, float x, float y) {
    if (player < 0 || player >= numPlayers) return;
    Player& p = players[player];
    if (!p.paradropReady || !hasSpyPlaneSource(player)) return;
    int tx = (int)x, ty = (int)y;
    if (!map.inBounds(tx, ty)) return;
    // YR Spy Plane：沿水平带揭开迷雾（近似直线航线）
    for (int dx = -24; dx <= 24; dx++) {
        int gx = tx + dx;
        if (!map.inBounds(gx, ty)) continue;
        map.reveal(player, gx, ty, 2);
    }
    p.paradropReady = false;
    p.paradropCharge = 0;
    g_sfx.playAt(Sfx::Missile, x, y);
    Effect ef; ef.kind = 8; ef.x = x; ef.y = y; ef.maxAge = 30;
    effects.push_back(ef);
}

void World::orderPsychicReveal(int player, float x, float y) {
    if (player < 0 || player >= numPlayers) return;
    Player& p = players[player];
    if (!p.paradropReady || !hasPsychicRevealSource(player)) return;
    int tx = (int)x, ty = (int)y;
    if (!map.inBounds(tx, ty)) return;
    // YR Psychic Reveal：圆形揭雾
    map.reveal(player, tx, ty, 10);
    p.paradropReady = false;
    p.paradropCharge = 0;
    g_sfx.playAt(Sfx::Tesla, x, y);
    Effect ef; ef.kind = 8; ef.x = x; ef.y = y; ef.maxAge = 35;
    effects.push_back(ef);
}

// 工程师占领特殊效果（RA2 原作：科技机场给伞兵、秘密实验室随机解锁国家特色科技）
void World::applyCaptureEffect(Ent& b, int newOwner) {
    if (newOwner < 0 || newOwner >= numPlayers) return;
    Player& p = players[newOwner];
    if (b.btype == BldType::OilDerrick) {
        p.money = std::min(g_gameRules.maxMoney, p.money + 1000); // YR/手册：占领油田瞬间奖金
        if (newOwner == 0) g_sfx.play(Sfx::Cash, 0.5f);
    } else if (b.btype == BldType::TechAirport) {
        eva(newOwner, TR(S::EvaAirportCaptured)); // 伞兵充能由 updateParadrop 自动推进
    } else if (b.btype == BldType::SecretLab && p.secretLabUnlock == 0) {
        // 随机解锁一个本阵营其他国家（排除自身国家）的特色科技
        std::vector<Country> pool;
        for (Country c : countriesOf(p.faction))
            if (c != p.country) pool.push_back(c);
        if (!pool.empty()) {
            Country c = pool[rng.range(0, (int)pool.size() - 1)];
            p.secretLabUnlock = (int)c;
            eva(newOwner, TextFormat(TR(S::EvaSecretLabFmt), countryName(c)));
        }
    }
}

void World::mindControlRelease(Ent& yuri) {
    if (yuri.mindTarget == INVALID_EID) return;
    if (valid(yuri.mindTarget)) {
        Ent& t = ents[yuri.mindTarget];
        if (t.mindBy != INVALID_EID) {
            t.player = t.origPlayer;      // 恢复原属（RA2 原作：控制者死亡则解放）
            t.mindBy = INVALID_EID;
            t.origPlayer = -1;
            t.state = UState::Idle; t.target = INVALID_EID; t.path.clear();
        }
    }
    yuri.mindTarget = INVALID_EID;
}

void World::mindControlTake(Ent& yuri, EID yid, EID tid) {
    if (!valid(tid)) return;
    Ent& t = ents[tid];
    if (t.isBuilding || psychicImmune(t.utype) || t.permaControlled || t.mindBy != INVALID_EID) return;
    if (t.player < 0 || !isEnemy(yuri.player, t.player)) return;
    mindControlRelease(yuri); // RA2 原作：同一时刻仅控制一个单位，控制新目标即释放旧目标
    Ent& tt = ents[tid];      // release 不会触发实体扩容，但保险起见重新取引用
    tt.origPlayer = tt.player;
    tt.player = yuri.player;
    tt.mindBy = yid;
    yuri.mindTarget = tid;
    tt.state = UState::Idle; tt.target = INVALID_EID; tt.path.clear();
    // 心灵波特效（控制者→目标）
    Effect fx; fx.kind = 2; fx.x = yuri.x; fx.y = yuri.y; fx.x2 = tt.x; fx.y2 = tt.y; fx.maxAge = 12;
    effects.push_back(fx);
    g_sfx.playAt(Sfx::Tesla, tt.x, tt.y);
    if (yuri.player == 0) eva(0, TR(S::EvaMindGain));
    if (tt.origPlayer == 0) eva(0, TR(S::EvaMindLost));
}

void World::psychicAreaReleaseAll(Ent& beacon, EID bid) {
    for (EID tid : beacon.mindTargets) {
        if (!valid(tid)) continue;
        Ent& t = ents[tid];
        if (t.mindBy == bid && !t.permaControlled) {
            t.player = t.origPlayer;
            t.mindBy = INVALID_EID;
            t.origPlayer = -1;
            t.state = UState::Idle; t.target = INVALID_EID; t.path.clear();
        }
    }
    beacon.mindTargets.clear();
    beacon.mindTarget = INVALID_EID;
}

void World::psychicAreaTake(Ent& beacon, EID bid, EID tid) {
    if (!valid(tid)) return;
    Ent& t = ents[tid];
    if (t.isBuilding || psychicImmune(t.utype) || t.permaControlled || t.mindBy != INVALID_EID) return;
    if (t.player < 0 || !isEnemy(beacon.player, t.player)) return;
    int cap = (beacon.btype == BldType::PsychicAmplifier) ? 8 : 5;
    if ((int)beacon.mindTargets.size() >= cap) return;
    Ent& tt = ents[tid];
    tt.origPlayer = tt.player;
    tt.player = beacon.player;
    tt.mindBy = bid;
    beacon.mindTargets.push_back(tid);
    tt.state = UState::Idle; tt.target = INVALID_EID; tt.path.clear();
    Effect fx; fx.kind = 2; fx.x = beacon.x; fx.y = beacon.y; fx.x2 = tt.x; fx.y2 = tt.y; fx.maxAge = 12;
    effects.push_back(fx);
    if (tt.origPlayer == 0) eva(0, TR(S::EvaMindLost));
}

void World::updatePsychicAreaDevices() {
    for (size_t i = 0; i < ents.size(); i++) {
        Ent& e = ents[i];
        if (!e.alive || !e.isBuilding || e.player < 0) continue;
        if (!isPsychicAreaBld(e.btype)) continue;
        EID id = (EID)i;
        // 清理失效链接
        e.mindTargets.erase(std::remove_if(e.mindTargets.begin(), e.mindTargets.end(),
            [&](EID t) { return !valid(t) || ents[t].mindBy != id; }), e.mindTargets.end());
        // 断电：释放全部（TimeMachine 不心控，仅占位）
        if (players[e.player].lowPower()) {
            psychicAreaReleaseAll(e, id);
            continue;
        }
        const BldDef& bd = bldDef(e.btype);
        float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
        float range = (e.btype == BldType::PsychicAmplifier) ? 14.0f : 10.0f;
        if ((tick + (uint64_t)id) % 15 != 0) continue; // 节流扫描
        for (size_t j = 0; j < ents.size(); j++) {
            Ent& t = ents[j];
            if (!t.alive || t.isBuilding || t.player < 0) continue;
            if (!isEnemy(e.player, t.player)) continue;
            float tx = t.x, ty = t.y;
            if (distf(cx, cy, tx, ty) > range) continue;
            psychicAreaTake(e, id, (EID)j);
        }
    }
}

// ===================== 间谍渗透 =====================
// RA2 原作效果：精炼厂=偷钱，电厂=断电，雷达=获取视野，兵营/工厂=新单位直接老兵，高科=破坏超武
void World::applySpyEffect(Ent& spy, Ent& bld, EID spyId) {
    int victim = bld.player;
    Player& sp = players[spy.player];
    const BldDef& bd = bldDef(bld.btype);
    switch (bld.btype) {
        case BldType::OreRefinery: {
            int steal = players[victim].money * 2 / 5; // 窃取对方 40% 资金
            players[victim].money -= steal;
            sp.money += steal;
            eva(spy.player, TextFormat(TR(S::SpyStealMoneyFmt), steal));
            if (victim >= 0) eva(victim, TR(S::SpyMoneyVictim));
            break;
        }
        case BldType::PowerPlant: case BldType::TeslaReactor: case BldType::NuclearReactor:
        case BldType::BioReactor: case BldType::TechPowerPlant: {
            if (victim >= 0) {
                players[victim].powerSabotage = 30 * 30; // 断电 30 秒
                eva(victim, TR(S::SpyPowerVictim));
            }
            eva(spy.player, TR(S::SpyPowerOk));
            break;
        }
        case BldType::Radar: case BldType::PsychicSensor: {
            // 雷达/心灵探测器：获取全图视野；受害方迷雾回退
            sp.revealTimer = 30 * 60; // 全图视野 60 秒
            eva(spy.player, TR(S::SpyRadarOk));
            if (victim >= 0) {
                for (auto& c : map.fog[victim])
                    if (c == FOG_SEEN) c = FOG_UNSEEN;
                eva(victim, TR(S::SpyRadarVictim));
            }
            break;
        }
        case BldType::AirForceCmd: {
            // 空指部：揭雾 + 空军老兵
            sp.revealTimer = 30 * 60;
            sp.vetCat[2] = true;
            eva(spy.player, TR(S::SpyRadarOk));
            if (victim >= 0) {
                for (auto& c : map.fog[victim])
                    if (c == FOG_SEEN) c = FOG_UNSEEN;
                eva(victim, TR(S::SpyRadarVictim));
            }
            break;
        }
        case BldType::Barracks:
            sp.vetCat[0] = true;
            eva(spy.player, TR(S::SpyBarracks));
            break;
        case BldType::WarFactory:
            sp.vetCat[1] = true;
            eva(spy.player, TR(S::SpyFactory));
            break;
        case BldType::NavalYard:
            sp.vetCat[3] = true;
            eva(spy.player, TR(S::SpyNavy));
            break;
        case BldType::ConYard: {
            // 建造厂：窃取部分资金
            if (victim >= 0) {
                int steal = std::min(2000, players[victim].money * 3 / 10);
                players[victim].money -= steal;
                sp.money += steal;
                eva(spy.player, TextFormat(TR(S::SpyStealMoneyFmt), steal));
                eva(victim, TR(S::SpyMoneyVictim));
            }
            break;
        }
        case BldType::BattleLab: {
            // 渗透高科：窃取 $1500 + 偷科技 —— 盟高科→超时空突击队；苏/中高科→心灵突击队+超时空伊文
            if (victim >= 0) {
                int steal = std::min(1500, players[victim].money);
                players[victim].money -= steal;
                sp.money += steal;
                if (players[victim].faction == Faction::Allies) {
                    if (!(sp.stolenTech & 1)) {
                        sp.stolenTech |= 1;
                        eva(spy.player, TR(S::SpyTechChrono));
                    }
                } else {
                    if (!(sp.stolenTech & 2)) {
                        sp.stolenTech |= 2;
                        eva(spy.player, TR(S::SpyTechPsi));
                    }
                    if (!(sp.stolenTech & 4)) {
                        sp.stolenTech |= 4; // Chrono Ivan
                        eva(spy.player, g_lang ? "Tech stolen: Chrono Ivan" : "偷取科技：超时空伊文");
                    }
                }
                eva(victim, TR(S::SpyLabVictim));
            }
            eva(spy.player, TR(S::SpyLabOk));
            break;
        }
        // RA2 原作：渗透超武建筑 → 重置其充能倒计时
        case BldType::NukeSilo: case BldType::WeatherDevice:
        case BldType::IronCurtain: case BldType::ChronoSphere:
        case BldType::GeneticMutator: case BldType::PsychicDominator: {
            if (victim >= 0) {
                SWType sw = bldProvidesSW(bld.btype);
                if (sw != SWType::COUNT) {
                    players[victim].swCharge[(int)sw] = 0;
                    players[victim].swReady[(int)sw] = false;
                }
                eva(victim, TR(S::SpySWVictim));
            }
            eva(spy.player, TR(S::SpySWReset));
            break;
        }
        default:
            sp.revealTimer = 30 * 15; // 其他建筑：短暂全图侦查
            eva(spy.player, TextFormat(TR(S::SpyGenericFmt), bldName(bld.btype)));
            break;
    }
    g_sfx.playAt(Sfx::Eva, spy.x, spy.y);
    spy.alive = false; // 间谍消耗（RA2 原作设定）
    freeList.push_back(spyId);
}

// ===================== 疯狂伊文定时炸弹 =====================
void World::updateTimedBombs() {
    for (auto& b : timedBombs) {
        if (b.timer < 0) continue;
        // 附着目标：跟随其位置（被摧毁则留在原地）
        if (valid(b.attachedTo)) {
            const Ent& t = ents[b.attachedTo];
            b.x = t.x; b.y = t.y;
            if (t.isBuilding) { b.x += bldDef(t.btype).w / 2.0f; b.y += bldDef(t.btype).h / 2.0f; }
        }
        if (--b.timer > 0) continue;
        // 爆炸：按炸弹规格（伊文 400/2.5 格；谭雅 C4 6000/0.6 格单点爆破）
        const float R = b.radius;
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive || e.invuln > 0) continue;
            // 定时炸弹为地面爆炸：空中单位免疫（避免基洛夫空艇被自己的炸弹误伤）
            if (!e.isBuilding && unitDef(e.utype).isAir() && e.state != UState::Landed) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            float d = distf(ex, ey, b.x, b.y);
            if (d > R) continue;
            int dmg = (int)(b.dmg * (1.0f - d / (R + 1.0f)));
            damage((int)i, dmg, b.player);
            // 基洛夫等：地面载具被炸掀起（左右交替）
            if (b.rockVehicles && !e.isBuilding) {
                const UnitDef& ud = unitDef(e.utype);
                if (!ud.isInfantry() && !ud.isAir() && !ud.isNaval())
                    e.rockTilt = std::max(e.rockTilt, 36);
            }
        }
        explodeAt(b.x, b.y, 1);
        b.timer = -1;
    }
    timedBombs.erase(std::remove_if(timedBombs.begin(), timedBombs.end(), [](const TimedBomb& b) { return b.timer < 0; }), timedBombs.end());
}

// ===================== 补给箱 =====================
void World::spawnCrateTick() {
    if (!cratesEnabled) return;
    if (g_gameRules.crateInterval <= 0 || tick % (uint64_t)g_gameRules.crateInterval != 0) return;
    if ((int)crates.size() >= 6) return;
    for (int tries = 0; tries < 40; tries++) {
        int x = rng.range(2, map.w - 3), y = rng.range(2, map.h - 3);
        if (!map.passable(x, y) || bldBlocked(x, y)) continue;
        Crate c;
        c.x = x; c.y = y; c.kind = rng.range(0, 10); // 见 Crate.kind 注释
        crates.push_back(c);
        break;
    }
}

void World::pickupCrates(Ent& e) {
    if (unitDef(e.utype).isAir()) return;
    for (auto& c : crates) {
        if (!c.alive) continue;
        if ((int)e.x != c.x || (int)e.y != c.y) continue;
        c.alive = false;
        g_sfx.playAt(Sfx::Cash, e.x, e.y);
        if (c.kind == 0) {
            int gain = 1000;
            players[e.player].money += gain;
            if (e.player == 0) eva(0, TextFormat(TR(S::CrateMoney), gain));
        } else if (c.kind == 1) {
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player) o.hp = unitDef(o.utype).hp;
            if (e.player == 0) eva(0, TR(S::CrateHeal));
        } else if (c.kind == 2) {
            e.vetRank = std::min(2, e.vetRank + 1);
            if (e.player == 0) eva(0, TR(S::CrateVet));
        } else if (c.kind == 3) {
            int gain = 5000; // MIX CrateRules.SoloCrateMoney
            players[e.player].money += gain;
            if (e.player == 0) eva(0, TextFormat(TR(S::CrateMoney), gain));
        } else if (c.kind == 4) {
            e.camouflaged = true;
            e.camoTick = 30 * 20;
            if (e.player == 0) eva(0, TR(S::CrateHeal));
        } else if (c.kind == 5) {
            e.hp = unitDef(e.utype).hp;
            e.vetRank = std::min(2, e.vetRank + 1);
            if (e.player == 0) eva(0, TR(S::CrateVet));
        } else if (c.kind == 6) {
            // 免费单位箱：在附近生成一辆基础坦克
            UnitType freeU = UnitType::Grizzly;
            Faction f = players[e.player].faction;
            if (f == Faction::Soviet || f == Faction::China) freeU = UnitType::Rhino;
            else if (f == Faction::Yuri) freeU = UnitType::LasherTank;
            for (int r = 1; r <= 2; r++) {
                bool placed = false;
                for (int dy = -r; dy <= r && !placed; dy++)
                    for (int dx = -r; dx <= r && !placed; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = (int)e.x + dx, ny = (int)e.y + dy;
                        if (!map.passable(nx, ny) || bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
                        spawnUnit(e.player, freeU, nx + 0.5f, ny + 0.5f);
                        placed = true;
                    }
                if (placed) break;
            }
            if (e.player == 0) eva(0, TR(S::CrateUnit));
        } else if (c.kind == 7) {
            e.crateDmgBoost = 30 * 45;
            if (e.player == 0) eva(0, TR(S::CratePower));
        } else if (c.kind == 8) {
            e.crateArmorBoost = 30 * 45;
            if (e.player == 0) eva(0, TR(S::CrateArmor));
        } else if (c.kind == 9) {
            e.crateSpeedBoost = 30 * 45;
            if (e.player == 0) eva(0, TR(S::CrateSpeed));
        } else {
            // 全图揭示
            for (int y = 0; y < map.h; y++)
                for (int x = 0; x < map.w; x++)
                    map.reveal(e.player, x, y, 0);
            if (e.player == 0) eva(0, TR(S::CrateReveal));
        }
    }
    crates.erase(std::remove_if(crates.begin(), crates.end(), [](const Crate& c) { return !c.alive; }), crates.end());
}

// ===================== 矿脉再生 =====================
// RA2 矿钻等效：矿脉以极慢速度恢复，避免残局经济彻底枯竭
void World::regrowOre() {
    if (tick % 120 != 0) return; // 每 4 秒一批
    if (g_gameRules.oreRegrowRate <= 0) return;
    for (int k = 0; k < 32; k++) {
        int x = rng.range(0, map.w - 1), y = rng.range(0, map.h - 1);
        Cell& c = map.at(x, y);
        if (c.oreMax <= 0 || c.ore >= c.oreMax) continue;
        if (c.ore == 0) {
            // 采空的格子（已变 Rough）：无占用才恢复矿脉地形
            if (bldBlocked(x, y) || unitAtCell(x, y) != INVALID_EID) continue;
            c.terrain = c.oreMax <= 150 ? Terrain::Gems : Terrain::Ore;
        }
        c.ore = (int16_t)std::min<int>(c.oreMax, c.ore + g_gameRules.oreRegrowRate);
    }
}

// ===================== 超时空传送 =====================

