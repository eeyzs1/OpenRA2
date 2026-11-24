#include "game/ai.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include <vector>
#include <algorithm>
#include <limits>

namespace {
bool automaticServiceReady(const World& world,EID unit,EID depot) {
    return world.nativeRulesBaseVersion<nativeAutomaticServiceSceneVersion || world.nativeAIRules.isGlobal()
        || world.serviceWorkStatus(unit,depot)==ServiceStatus::Working;
}

bool combatUnit(const World::Ent& e) {
    if(!e.alive || e.isBuilding || e.utype==UnitType::MCV || e.utype==UnitType::Engineer || e.utype==UnitType::Spy) return false;
    if(e.utype==UnitType::Nighthawk && e.nativeJumpjet.motion.started && e.nativeIdentity.definition
        && e.nativeIdentity.definition->jumpjet.version>=2)return false;
    const auto& def=unitDef(e);
    return !def.canHarvet() && (def.weapon.damage>0 || def.isPsychic() || def.hasC4() || e.utype==UnitType::AircraftCarrier);
}

}

// ===================== 初始化 =====================
void SkirmishAI::reset(int p) {
    player = p;
    thinkTimer = 0;
    attackWave = 0;
    attackTimer = 0;
    difficulty = AIDiff::Normal;
    personality = AIPersonality::Balanced;
    hasWater = -1;
    navalPlaceable = -1;
    navalCheckCd = 0;
    navalFail = 0;
    rallyPoint = { -1, -1 };
    rallyCheckTimer = 0;
    expandTimer = 0;
    defenseCheckTimer = 0;
    lastAttacker = INVALID_EID;
    scoutMapW=scoutMapH=0;scoutObserved.clear();knownBuildings.clear();
    pcfg = AIPersonalityConfig{};
    initPersonality();
}

// 根据人格设置 pcfg 字段（rules.ini 覆盖后由 game.h 重新设置 personality 时再次调用）
void SkirmishAI::initPersonality() {
    pcfg = AIPersonalityConfig{};  // 先恢复默认（=Balanced）
    switch (personality) {
        case AIPersonality::Rusher:
            pcfg.aggression = 1.8f;
            pcfg.defensePriority = 0.5f;
            pcfg.techRush = 0.4f;
            pcfg.econFocus = 0.8f;
            pcfg.rallySize = 5;
            pcfg.useEngineers = false;
            pcfg.useSpies = false;
            pcfg.expandBase = false;
            // Rusher 仍建 BattleLab（轻科技但需高科解锁英雄/特种单位）
            break;
        case AIPersonality::Turtler:
            pcfg.aggression = 0.4f;
            pcfg.defensePriority = 2.0f;
            pcfg.techRush = 1.5f;
            pcfg.econFocus = 1.2f;
            pcfg.rallySize = 15;
            pcfg.useSuperWeapon = true;
            pcfg.expandBase = false;
            break;
        case AIPersonality::Steamroller:
            pcfg.aggression = 0.7f;
            pcfg.defensePriority = 0.8f;
            pcfg.techRush = 0.6f;
            pcfg.econFocus = 1.8f;
            pcfg.rallySize = 18;
            pcfg.useEngineers = true;
            pcfg.useSpies = false;
            pcfg.expandBase = true;
            break;
        case AIPersonality::Technician:
            pcfg.aggression = 0.6f;
            pcfg.defensePriority = 1.2f;
            pcfg.techRush = 2.5f;
            pcfg.econFocus = 0.7f;
            pcfg.rallySize = 4;
            pcfg.useEngineers = true;
            pcfg.useSpies = true;
            pcfg.expandBase = false;
            break;
        default: break;  // Balanced：保持默认
    }
}

// ===================== 难度参数 =====================
int SkirmishAI::thinkInterval() const {
    switch (difficulty) {
        case AIDiff::Easy:   return 30;
        case AIDiff::Normal: return 15;
        case AIDiff::Hard:   return 10;
        case AIDiff::Brutal: return 7;
    }
    return 15;
}

int SkirmishAI::attackThreshold() const {
    int base;
    switch (difficulty) {
        case AIDiff::Easy:   base = 15; break;
        case AIDiff::Normal: base = 8;  break;
        case AIDiff::Hard:   base = 6;  break;
        case AIDiff::Brutal: base = 5;  break;
        default: base = 8; break;
    }
    int t = (int)(base / pcfg.aggression);
    // Later waves need reinforcements, but the requirement must not grow
    // forever after losses (or overflow a restored historical wave counter).
    t += std::clamp(attackWave,0,6) * 2;
    if (t < 3) t = 3;
    return t;
}

float SkirmishAI::resourceBonus() const {
    // 不再用思考间隔复利刷钱；Brutal 仅在采矿结算等处可接此倍率（当前不主动加钱）
    return 1.0f;
}

// ===================== 主更新 =====================
void SkirmishAI::update(World& w) {
    if(player<0 || player>=w.numPlayers)return;
    Player& p = w.players[player];
    if (!p.active || p.defeated) return;
    // Lua AI hook：脚本返回 true 则跳过内置 AI（用户可完全接管 AI 决策）
    const bool scriptOwns=g_script.boundTo(&w) && g_script.onAiThink(player);
    if(w.nativeAIRules.isGlobal())w.configureNativeSkirmishAI(player,(int)difficulty,!scriptOwns);
    if(scriptOwns)return;

    // 逐帧递减的冷却计时（不受思考间隔门控，保证真实时间精度）
    if (expandTimer > 0) expandTimer--;
    if (defenseCheckTimer > 0) defenseCheckTimer--;
    if (rallyCheckTimer > 0) rallyCheckTimer--;

    // 思考间隔按难度分级
    if (++thinkTimer < thinkInterval()) return;
    thinkTimer = 0;
    observeBattlefield(w);

    recoverConstruction(w);

    const bool recovering=recoverEconomy(w) || recoverFactoryProduction(w);
    const bool sourceArmy=w.nativeSkirmishAIActive(player);
    if(sourceArmy)w.nativeAI.houses[player].production=!recovering;
    if(!recovering)doBuildOrder(w);
    if(!recovering)doProduction(w);

    // 建筑/防御生产就绪后直接放置（RA2 双队列）
    auto tryReady = [&](ProdItem& pr) {
        if (!pr.active || !pr.ready) return;
        BldType t = w.nativeProductionType(pr.typeIdx)?w.nativeTypes[pr.typeIdx-NATIVE_PRODUCTION_BASE]->building.type:(BldType)pr.typeIdx;
        if (tryPlaceBld(w, t)) {
            navalFail = 0;
        } else if (t == BldType::NavalYard && ++navalFail > 40) {
            w.cancelBldProd(player, t);
            navalFail = 0;
            navalPlaceable = 0;
        }
    };
    tryReady(p.bldProd);
    tryReady(p.defProd);

    if(!sourceArmy && p.isAI && w.nativeRulesBaseVersion>=nativeAutomaticServiceSceneVersion) {
        for(EID id=0;id<(EID)w.ents.size();++id) {
            const auto& unit=w.ents[id];
            if(!unit.alive || unit.isBuilding || unit.player!=player || unit.state!=UState::Servicing)continue;
            const auto status=w.serviceWorkStatus(id,unit.target);
            if(status==ServiceStatus::WaitingPower || status==ServiceStatus::WaitingMoney)w.orderStop({id});
        }
    }

    if(!sourceArmy){doEngineers(w);doAttack(w);}
    doSuperWeapon(w);
    doSupport(w);
    if(!sourceArmy){doTactics(w);doTransports(w);}
    if(!recovering)doExpansion(w);
    clearFactoryExits(w);
}

// ===================== 地图/船厂探测（沿用原实现） =====================
// 全图扫描是否有 3x3 以上水域（决定是否在建造序列中加入船厂）
bool SkirmishAI::detectWater(World& w) {
    if (hasWater >= 0) return hasWater;
    hasWater = 0;
    for (int y = 0; y + 2 < w.map.h && !hasWater; y += 2)
        for (int x = 0; x + 2 < w.map.w && !hasWater; x += 2) {
            bool allW = true;
            for (int dy = 0; dy < 3 && allW; dy++)
                for (int dx = 0; dx < 3 && allW; dx++)
                    if (w.map.at(x + dx, y + dy).terrain != Terrain::Water) allW = false;
            if (allW) hasWater = 1;
        }
    return hasWater;
}

// 基地建造半径内是否存在可放置 3x3 船厂的水域（带缓存，避免每次思考全图扫描）
bool SkirmishAI::navalSiteAvailable(World& w) {
    if (navalCheckCd > 0 && navalPlaceable >= 0) { navalCheckCd--; return navalPlaceable; }
    navalCheckCd = 10;  // 10 次思考（约 5 秒）后重查
    const BldDef& d = bldDef(BldType::NavalYard);
    navalPlaceable = 0;
    for (int y = 0; y + d.h <= w.map.h && !navalPlaceable; y++)
        for (int x = 0; x + d.w <= w.map.w && !navalPlaceable; x++) {
            if (w.map.at(x, y).terrain != Terrain::Water) continue;
            if (w.canPlace(BldType::NavalYard, x, y, player)) navalPlaceable = 1;
        }
    return navalPlaceable;
}

bool SkirmishAI::tryPlaceBld(World& w, BldType t) {
    const int source=w.usesRulesProfile()?w.rulesTypeIndex(player,false,(int)t):-1;
    const BldDef& d = source>=0?w.nativeTypes[source]->building:bldDef(t);
    // 船厂须建于水面：全图扫描 3x3 水域（离基地越近越好）
    if (t == BldType::NavalYard) {
        int ccx = w.map.w / 2, ccy = w.map.h / 2;
        for (const World::Ent& e : w.ents)
            if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
                ccx = (int)e.x; ccy = (int)e.y; break;
            }
        int bx = -1, by = -1;
        float best = 1e9f;
        for (int y = 0; y + d.h <= w.map.h; y++)
            for (int x = 0; x + d.w <= w.map.w; x++) {
                if (w.map.at(x, y).terrain != Terrain::Water) continue;  // 粗筛
                if (!w.canPlace(t, x, y, player)) continue;
                if (!constructionSitePreservesExits(w,d,x,y)) continue;
                float dist = distf((float)x, (float)y, (float)ccx, (float)ccy);
                if (dist < best) { best = dist; bx = x; by = y; }
            }
        if (bx < 0) return false;
        return w.placeBuilding(player, t, bx, by);
    }
    // 围绕建造厂螺旋搜索可放置位置（以建造厂中心为原点）
    int ccx = -1, ccy = -1, yardW = 3, yardH = 3;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            const BldDef& yd = bldDef(e);
            yardW = yd.w; yardH = yd.h;
            ccx = (int)e.x + yardW / 2;
            ccy = (int)e.y + yardH / 2;
            break;
        }
    if (ccx < 0) return false;
    // 占地外缘间距：至少空 1 格，避免贴边视觉重叠（围墙除外）
    auto tooClose = [&](int bx, int by) {
        if (t == BldType::Wall) return false;
        const int gap = 1;
        for (const World::Ent& e : w.ents) {
            if (!e.alive || !e.isBuilding || e.player != player) continue;
            if (e.btype == BldType::Wall) continue;
            const BldDef& ed = bldDef(e);
            int sepX = std::max(0, std::max(bx - ((int)e.x + ed.w), (int)e.x - (bx + d.w)));
            int sepY = std::max(0, std::max(by - ((int)e.y + ed.h), (int)e.y - (by + d.h)));
            if (std::max(sepX, sepY) < gap) return true;
        }
        return false;
    };
    const EID air=airDefenseConstructionTarget(w);
    if(w.valid(air)) {
        World::Ent trial;trial.alive=trial.isBuilding=true;trial.player=player;trial.btype=t;
        if(source>=0){trial.nativeIdentity.type=source;trial.nativeIdentity.definition=w.nativeTypes[source];}
        const auto weapon=w.effWeapon(trial,&w.ents[air]);
        if(weapon.damage>0 && weapon.antiAir){
            Vec2f asset{(float)ccx,(float)ccy};float nearest=10.f;
            for(const auto& e:w.ents)if(e.alive && e.isBuilding && e.player==player && !e.selling){
                const auto& b=bldDef(e);const Vec2f center{e.x+b.w/2.f,e.y+b.h/2.f};
                const float distance=distf(center.x,center.y,w.ents[air].x,w.ents[air].y);
                if(distance<nearest){nearest=distance;asset=center;}}
            Vec2i site{-1,-1};float best=1e9f;
            for(int y=std::max(0,(int)asset.y-8);y+d.h<=w.map.h && y<=(int)asset.y+8;++y)
                for(int x=std::max(0,(int)asset.x-8);x+d.w<=w.map.w && x<=(int)asset.x+8;++x){
                    if(!w.canPlace(t,x,y,player) || tooClose(x,y) || !constructionSitePreservesExits(w,d,x,y))continue;
                    if(distf(x+d.w/2.f,y+d.h/2.f,w.ents[air].x,w.ents[air].y)>weapon.range)continue;
                    const float score=distf(x+d.w/2.f,y+d.h/2.f,asset.x,asset.y);
                    if(score<best){best=score;site={x,y};}}
            if(site.x>=0)return w.placeBuilding(player,t,site.x,site.y);
        }
    }
    // 从建造厂外缘开始螺旋，避免塞进紧贴西北角
    int startR = std::max(2, (std::max(yardW, yardH) + 1) / 2);
    for (int r = startR; r <= 18; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int bx = ccx + dx - d.w / 2, by = ccy + dy - d.h / 2;
                if (!w.canPlace(t, bx, by, player)) continue;
                if (tooClose(bx, by)) continue;
                if (!constructionSitePreservesExits(w,d,bx,by)) continue;
                return w.placeBuilding(player, t, bx, by);
            }
    // 回退：间距放宽仍放不下时允许贴边（保证能落成）
    for (int r = 1; r <= 18; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int bx = ccx + dx - d.w / 2, by = ccy + dy - d.h / 2;
                if (!w.canPlace(t, bx, by, player)) continue;
                if (!constructionSitePreservesExits(w,d,bx,by)) continue;
                return w.placeBuilding(player, t, bx, by);
            }
    return false;
}

// ===================== 单位生产 =====================
// ===================== 进攻（集结点 + 多路进攻） =====================
void SkirmishAI::doAttack(World& w) {
    int threshold = attackThreshold();

    // Visible raids on owned infrastructure need a local response before
    // the global army is ready to gather or cross an ocean.
    const auto airDefenders=assignAirDefense(w);
    // Air raids must not wait for ground units to gather across an ocean.
    std::vector<EID> armyIds, navyIds, airIds;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (e.player != player || !combatUnit(e) || e.state==UState::Boarding || e.state==UState::Servicing) continue;
        if(std::find(airDefenders.begin(),airDefenders.end(),(EID)i)!=airDefenders.end())continue;
        const UnitDef& ud = unitDef(e);
        if (ud.canHarvet()) continue;
        if (ud.isAir()) airIds.push_back((int)i);
        else if (ud.isNaval() && !ud.isAmphib()) navyIds.push_back((int)i);
        else armyIds.push_back((int)i);
    }

    Vec2i ac = findArmyCenter(w);
    if(!armyIds.empty()) {
        int sx=0,sy=0;
        for(EID id:armyIds){sx+=(int)w.ents[id].x;sy+=(int)w.ents[id].y;}
        ac={sx/(int)armyIds.size(),sy/(int)armyIds.size()};
    }
    auto center=[&](const World::Ent& e) {
        return Vec2f{e.x+(e.isBuilding?bldDef(e).w/2.0f:0),
                       e.y+(e.isBuilding?bldDef(e).h/2.0f:0)};
    };
    auto visibleEnemy=[&](const World::Ent& e) {
        if(!e.alive || !w.isEnemy(player,e.player)) return false;
        const auto point=center(e);
        return w.map.fogAt(player,(int)point.x,(int)point.y)==FOG_VISIBLE;
    };
    // A strategic target may be immune to part of a mixed army. Keep legal
    // orders and give rejected units a visible, compatible alternative.
    const auto localContact=[&](const World::Ent& unit,const World::Ent& target) {
        if(target.isBuilding)return true;
        const auto point=center(target);
        return distf(unit.x,unit.y,point.x,point.y)<=std::max((float)unitDef(unit).sight,w.attackRange(unit,target));
    };
    auto issueAttack=[&](const std::vector<EID>& group,EID preferred,bool localMobileTargets=false) {
        int accepted=0;
        for(EID id:group) {
            if(!w.valid(id) || w.ents[id].immobilized() || w.ents[id].deploying())continue;
            const auto& unit=w.ents[id];
            if((unit.state==UState::Chasing || unit.state==UState::Attacking)
                && w.valid(unit.target) && visibleEnemy(w.ents[unit.target])
                && (!localMobileTargets || localContact(unit,w.ents[unit.target]))
                && w.attackRejection(id,unit.target)==AttackRejectReason::None) {
                continue;
            }
            if(w.valid(preferred) && visibleEnemy(w.ents[preferred])
                && (!localMobileTargets || localContact(unit,w.ents[preferred]))
                && w.orderAttack({id},preferred).accepted) { ++accepted;continue; }
            EID fallback=INVALID_EID;float best=1e9f;
            for(size_t index=0;index<w.ents.size();++index) {
                if((EID)index==preferred || !visibleEnemy(w.ents[index])
                    || (localMobileTargets && !localContact(unit,w.ents[index]))
                    || w.attackRejection(id,(EID)index)!=AttackRejectReason::None) continue;
                const auto point=center(w.ents[index]);
                const float distance=distf(w.ents[id].x,w.ents[id].y,point.x,point.y);
                if(distance<best) { best=distance;fallback=(EID)index; }
            }
            if(fallback!=INVALID_EID) accepted+=w.orderAttack({id},fallback).accepted;
        }
        return accepted;
    };

    // Nearby visible contacts remain the tactical fallback. Ground waves use
    // remembered infrastructure when a physical approach is available.
    EID targetB = INVALID_EID;
    float bd = 1e9f;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (!visibleEnemy(e)) continue;
        const auto point=center(e);
        float d = distf((float)ac.x, (float)ac.y, point.x, point.y);
        if (d < bd) { bd = d; targetB = (int)i; }
    }

    const auto objectives=assaultObjectives(w,ac);
    // Reacquire previously observed bases and revisit stale explored regions.
    // Memory offers movement destinations, never a hidden live attack target.
    int airAttacks=0;
    std::vector<Vec2i> airSurvey;
    if(w.nativeProductionRules.version>=8)for(EID id:airIds) {
        const auto& unit=w.ents[id];
        if(unit.attackMove.active)airSurvey.push_back({(int)unit.attackMove.x,(int)unit.attackMove.y});
    }
    for(EID id:airIds) {
        const auto& e=w.ents[id];const auto& def=unitDef(e);
        const auto* source=e.nativeIdentity.definition.get();
        const bool localMobileTargets=w.nativeRulesBaseVersion>=nativeCrushingSceneVersion
            && source && source->jumpjet.available && source->jumpjet.balloon;
        const bool readyOnPad=e.state==UState::Landed && def.ammo>0 && e.ammo>=def.ammo;
        if(e.utype==UnitType::Hornet || e.state==UState::Returning || (e.state==UState::Landed && !readyOnPad) || e.immobilized()
            || (def.ammo>0 && e.ammo<=0)) continue;
        EID structure=INVALID_EID;
        for(const auto& objective:objectives) {
            const EID candidate=w.bldAt(objective.x,objective.y);if(!w.valid(candidate))continue;
            const auto& building=w.ents[candidate];
            if(building.player==objective.owner && building.btype==objective.type && building.x==objective.x && building.y==objective.y
                && buildingVisible(w,building) && w.attackRejection(id,candidate)==AttackRejectReason::None){structure=candidate;break;}
        }
        bool resumeSurvey=false;
        if((e.state==UState::Chasing || e.state==UState::Attacking) && w.valid(e.target)) {
            // Retain actual fire and strategic pursuit. A bomber chasing a
            // mobile decoy can instead begin a visible infrastructure raid.
            if(e.state==UState::Chasing && !w.ents[e.target].isBuilding && structure!=INVALID_EID) {
                airAttacks+=w.orderAttack({id},structure).accepted>0;
                continue;
            }
            // A slow survey bomber must not abandon reconnaissance for a
            // mobile contact seen only far away by another friendly actor.
            resumeSurvey=localMobileTargets && e.state==UState::Chasing && !w.ents[e.target].isBuilding
                && (!visibleEnemy(w.ents[e.target]) || !localContact(e,w.ents[e.target]));
            if(!resumeSurvey)continue;
        }
        const EID preferred=structure!=INVALID_EID?structure:targetB;
        if(preferred!=INVALID_EID && issueAttack({id},preferred,localMobileTargets)>0) { ++airAttacks;continue; }
        if(!resumeSurvey && e.state!=UState::Idle && e.state!=UState::Circling && !readyOnPad) continue;
        const auto scout=scoutDestination(w,e,airSurvey);
        if(scout.x>=0) {
            const bool probe=w.nativeSceneVersion>=36 && w.effWeapon(e).damage>0;
            if(w.orderMove({id},scout.x+0.5f,scout.y+0.5f,probe).accepted && w.nativeProductionRules.version>=8)
                airSurvey.push_back({(int)w.ents[id].goalX,(int)w.ents[id].goalY});
        } else if(localMobileTargets && preferred!=INVALID_EID) {
            // With no fog left to survey, a distant observed survivor is still
            // a legitimate target. Do not strand the bomber on a revealed map.
            airAttacks+=issueAttack({id},preferred)>0;
        }
    }
    if(airAttacks>0 && attackWave<std::numeric_limits<int>::max()) ++attackWave;

    // Ships explore their reachable water component before a target is known.
    // A weapon-incompatible or unreachable enemy must not pin the fleet in port.
    int navalAttacks=0;
    for(EID id:navyIds) {
        const auto& ship=w.ents[id];
        if(ship.immobilized())continue;
        if((ship.state==UState::Chasing || ship.state==UState::Attacking) && w.valid(ship.target))continue;
        if(targetB!=INVALID_EID && issueAttack({id},targetB)>0){++navalAttacks;continue;}
        if(ship.state!=UState::Idle)continue;
        const auto scout=scoutDestination(w,ship);
        if(scout.x>=0)w.orderMove({id},scout.x+0.5f,scout.y+0.5f,false);
    }
    if(navalAttacks>0 && attackWave<std::numeric_limits<int>::max())++attackWave;

    if (targetB == INVALID_EID && objectives.empty()) {
        scoutGround(w,armyIds);
        return;
    }
    if(armyIds.empty())return;
    const int army=(int)armyIds.size();

    if (rallyPoint.x<0 || rallyPoint.y<0) {
        // Put the rendezvous on a real approach path from an existing soldier.
        // A geometric midpoint can be inside a lake, cliff or sealed courtyard.
        const auto& anchor=w.ents[armyIds.front()];
        rallyPoint={(int)anchor.x,(int)anchor.y};
        const auto destination=objectives.empty()?center(w.ents[targetB]):Vec2f{objectives.front().x+0.5f,objectives.front().y+0.5f};
        std::vector<Vec2i> route;
        const bool found=w.map.findPathToRange((int)anchor.x,(int)anchor.y,destination.x,destination.y,
            3.0f,route,20000,unitDef(anchor).pathDomain(),nullptr,anchor.navLayer,-1,-1,
            unitDef(anchor).fitsUnderBridge(),w.nativeGateAccess(anchor));
        if(found && !route.empty())
            rallyPoint=route[std::min<size_t>(route.size()/3,11)];
        if(!found && targetB==INVALID_EID){scoutGround(w,armyIds);return;}
        rallyCheckTimer=900;
    }
    int gathered=0;
    std::vector<EID> idleArmy;
    for(EID id:armyIds) {
        const auto& unit=w.ents[id];
        if(distf(unit.x,unit.y,rallyPoint.x+0.5f,rallyPoint.y+0.5f)<8.0f)++gathered;
        else if(unit.state==UState::Idle && !unit.attackMove.active && !unit.guard && !unit.deployed && !unit.radDeployed
            && !unit.immobilized() && !unit.deploying())idleArmy.push_back(id);
    }
    if((army<threshold || gathered<std::min(army,std::max(1,pcfg.rallySize))) && rallyCheckTimer>0) {
        if(!idleArmy.empty())w.orderMove(idleArmy,rallyPoint.x+0.5f,rallyPoint.y+0.5f,false);
        return;
    }

    // 集结完毕：发起进攻
    if (++attackTimer < 8) return;
    attackTimer = 0;

    // 困难/残暴：多路进攻（分两组从不同方向夹击）
    bool multiVector = (difficulty == AIDiff::Hard || difficulty == AIDiff::Brutal);
    int accepted=0;
    if(issueAssault(w,armyIds,objectives,ac,accepted)) {
        // The accepted attack-move owns the destination through local combat.
    } else if(targetB==INVALID_EID) {
        scoutGround(w,armyIds);return;
    } else if (multiVector && (int)armyIds.size() >= 12) {
        std::vector<EID> g1, g2;
        for (size_t i = 0; i < armyIds.size(); i++) {
            (i % 2 == 0 ? g1 : g2).push_back(armyIds[i]);
        }
        // 第二组从侧翼攻击一个更远的目标（不同方向）
        EID flankTarget = INVALID_EID;
        float fd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& e = w.ents[i];
            if (!visibleEnemy(e)) continue;
            if ((int)i == targetB) continue;
            const auto point=center(e);
            float d = distf((float)ac.x, (float)ac.y, point.x, point.y);
            if (d > 12.0f && d < fd) { fd = d; flankTarget = (int)i; }
        }
        if (flankTarget == INVALID_EID) flankTarget = targetB;
        accepted+=issueAttack(g1,targetB);
        accepted+=issueAttack(g2,flankTarget);
    } else {
        // 混编部队整体进攻（坦克在前、AA 护航跟随、步兵同组）
        accepted=issueAttack(armyIds,targetB);
    }

    if(accepted>0) {
        if(attackWave<std::numeric_limits<int>::max()) ++attackWave;
        rallyPoint = { -1, -1 };  // Only a real accepted attack completes a wave.
    }
}

// ===================== 战术微操 =====================
void SkirmishAI::doTactics(World& w) {
    Player& p = w.players[player];

    // 基地中心（建造厂位置）
    float bcx = -1, bcy = -1;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            bcx = e.x; bcy = e.y; break;
        }

    // 1. 集火：附近空闲单位协助正在交战的单位攻击同一目标
    if (pcfg.focusFire) {
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& atk = w.ents[i];
            if (!atk.alive || atk.isBuilding || atk.player != player) continue;
            if (atk.state != UState::Attacking) continue;
            EID tgt = atk.target;
            if (tgt == INVALID_EID || !w.valid(tgt)) continue;
            const auto& enemy=w.ents[tgt];
            if(!w.isEnemy(player,enemy.player) || (enemy.isBuilding?!buildingVisible(w,enemy)
                :w.map.fogAt(player,(int)enemy.x,(int)enemy.y)!=FOG_VISIBLE))continue;
            // 收集附近空闲 AI 单位协助攻击
            std::vector<EID> idle;
            for (size_t j = 0; j < w.ents.size(); j++) {
                World::Ent& u = w.ents[j];
                if (!u.alive || u.isBuilding || u.player != player) continue;
                if (u.state != UState::Idle || u.attackMove.active) continue;
                if (distf(u.x, u.y, atk.x, atk.y) > 6.0f) continue;
                idle.push_back((int)j);
                if ((int)idle.size() >= 4) break;
            }
            if (!idle.empty()) w.orderAttack(idle, tgt);
        }
    }

    // 2. 残血撤退：HP < 30% 且在交战中的车辆撤回最近维修厂或基地中心
    if (pcfg.retreatDamaged) {
        EID depot = INVALID_EID;
        for (size_t i = 0; i < w.ents.size(); i++)
            if (w.ents[i].alive && w.ents[i].isBuilding && w.ents[i].player == player
                && w.ents[i].btype == BldType::ServiceDepot) { depot = (int)i; break; }
        std::vector<EID> hurt;
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || e.player != player) continue;
            const UnitDef& ud = unitDef(e);
            if (ud.isInfantry() || ud.isAir() || ud.isNaval() || ud.canHarvet()) continue;
            if (e.hp >= ud.hp * 30 / 100) continue;
            if (e.state != UState::Attacking && e.state != UState::Chasing) continue;
            if(depot!=INVALID_EID && !automaticServiceReady(w,(EID)i,depot))continue;
            hurt.push_back((int)i);
            if ((int)hurt.size() >= 5) break;  // 每波最多撤 5 辆
        }
        if (!hurt.empty()) {
            if (depot != INVALID_EID) w.orderService(hurt, depot);
            else if (bcx >= 0) w.orderMove(hurt, bcx + 0.5f, bcy + 0.5f, false);  // 无维修厂退回基地
        }
    }

    // 3. 防御反应：敌方单位靠近基地（建造厂 10 格内）
    if (bcx >= 0 && defenseCheckTimer == 0) {
        EID threat = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || !w.isEnemy(player, e.player)) continue;
            if(w.map.fogAt(player,(int)e.x,(int)e.y)!=FOG_VISIBLE)continue;
            float d = distf(e.x, e.y, bcx, bcy);
            if (d < 10.0f && d < bd) { bd = d; threat = (int)i; }
        }
        if (threat != INVALID_EID) {
            lastAttacker = threat;
            // 召集附近空闲单位防守
            std::vector<EID> defenders;
            for (size_t i = 0; i < w.ents.size(); i++) {
                World::Ent& e = w.ents[i];
                if (!e.alive || e.isBuilding || e.player != player) continue;
                if (e.state != UState::Idle) continue;
                if (distf(e.x, e.y, bcx, bcy) > 14.0f) continue;
                defenders.push_back((int)i);
                if ((int)defenders.size() >= 6) break;
            }
            if (!defenders.empty()) w.orderAttack(defenders, threat);
            defenseCheckTimer = 90;  // 节流：约 3 秒后再响应（逐帧递减）
        }
    }
}

// ===================== 分基地扩张 =====================
void SkirmishAI::doExpansion(World& w) {
    if (!pcfg.expandBase) return;     // 仅 Steamroller/Brutal 扩张
    if (expandTimer > 0) return;       // 冷却中（expandTimer 在 update() 中逐帧递减）

    Player& p = w.players[player];
    expandTimer = 900;                 // 默认 30 秒冷却（≈900 帧）

    // 基地中心（建造厂位置）
    float bcx = -1, bcy = -1;
    for (const World::Ent& e : w.ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
            bcx = e.x; bcy = e.y; break;
        }

    // 已有 2 个建造厂则不再扩张
    int conYards = w.countBlds(player, BldType::ConYard);
    if (conYards == 0 || conYards >= 2) return;

    // 资金不足或无重工时短冷却重试
    if (p.money < 3000 || !w.hasBld(player, BldType::WarFactory)) {
        expandTimer = 300;
        return;
    }

    // 生产 MCV（队列空闲时）
    if (!constructionVehiclePlanned(w) && w.unitQueuedCount(player, 1) < 2
        && w.unitPrereqMet(player, unitDef(UnitType::MCV))) {
        w.startUnitProd(player, UnitType::MCV);
    }

    // 移动/部署扩张用 MCV（仅当已有基地时；基地丢失的 MCV 由 update() 直接展开重建）
    if (conYards >= 1 && bcx >= 0) {
        // 寻找远离基地的矿场作为分基地目标
        int fx = -1, fy = -1;
        float bestDist = -1.0f;
        for (int y = 0; y < w.map.h; y++)
            for (int x = 0; x < w.map.w; x++) {
                const Cell& c = w.map.at(x, y);
                if (c.terrain != Terrain::Ore && c.terrain != Terrain::Gems) continue;
                float d = distf((float)x, (float)y, bcx, bcy);
                if (d > 20.0f && d > bestDist) { bestDist = d; fx = x; fy = y; }
            }
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || e.player != player || e.utype != UnitType::MCV) continue;
            if (e.state != UState::Idle) continue;
            // 空闲 MCV：远离基地（已到矿场）则展开；在基地则派往矿场
            if (distf(e.x, e.y, bcx, bcy) > 15.0f) {
                deployConstructionVehicle(w,(EID)i);
            } else if (fx >= 0) {
                w.orderMove({ (int)i }, (float)fx + 0.5f, (float)fy + 0.5f, false);
            }
            break;
        }
    }
}

// ===================== 工程师行为（沿用原实现 + 人格门控） =====================
void SkirmishAI::doEngineers(World& w) {
    const bool engineers=pcfg.useEngineers || w.skirmishMode==SkirmishMode::Megawealth;
    if (!engineers && !pcfg.useSpies) return;
    // 收集空闲工程师与间谍
    std::vector<EID> idleEngs, idleSpies;
    for (size_t i = 0; i < w.ents.size(); i++) {
        const World::Ent& e = w.ents[i];
        if (!e.alive || e.isBuilding || e.player != player) continue;
        if (engineers && e.utype == UnitType::Engineer && e.state == UState::Idle) idleEngs.push_back((int)i);
        if (e.utype == UnitType::Spy && e.state == UState::Idle) idleSpies.push_back((int)i);
    }
    // 工程师：优先占领中立科技建筑（player=-1），其次修复己方受损建筑
    for (EID eng : idleEngs) {
        EID best = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding) continue;
            if (b.player != -1) continue;  // 中立
            if (!buildingVisible(w,b)) continue;
            if (!bldDef(b).capturable) continue;
            float d = distf(w.ents[eng].x, w.ents[eng].y, b.x, b.y);
            if (d < bd) { bd = d; best = (int)i; }
        }
        if (best != INVALID_EID) { w.orderCapture({ eng }, best); continue; }
        // 修复己方受损建筑（造价高优先）
        EID dmg = INVALID_EID;
        int bestCost = 0;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding || b.player != player) continue;
            const BldDef& bd2 = bldDef(b);
            if (b.hp < bd2.hp * 2 / 3 && bd2.cost > bestCost) { bestCost = bd2.cost; dmg = (int)i; }
        }
        if (dmg != INVALID_EID) w.orderRepair({ eng }, dmg);
    }
    // 间谍：渗透敌方精炼厂（偷钱）> 电厂（断电）
    if (!pcfg.useSpies) return;
    for (EID spy : idleSpies) {
        EID best = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < w.ents.size(); i++) {
            const World::Ent& b = w.ents[i];
            if (!b.alive || !b.isBuilding || !w.isEnemy(player, b.player)) continue;
            if (!buildingVisible(w,b)) continue;
            if (b.btype != BldType::OreRefinery && b.btype != BldType::PowerPlant
                && b.btype != BldType::TeslaReactor) continue;
            if(w.attackRejection(spy,(EID)i)!=AttackRejectReason::None)continue;
            float d = distf(w.ents[spy].x, w.ents[spy].y, b.x, b.y);
            if (d < bd) { bd = d; best = (int)i; }
        }
        if (best != INVALID_EID) w.orderAttack({ spy }, best);
        else {
            // Reacquire remembered installations by movement. Only current
            // observation above may resolve a live entity as an infiltration target.
            const auto destination=scoutDestination(w,w.ents[spy]);
            if(destination.x>=0)w.orderMove({spy},destination.x+.5f,destination.y+.5f,false);
        }
    }
}

// ===================== 超武（沿用原实现 + 人格门控） =====================
void SkirmishAI::doSuperWeapon(World& w) {
    if (!pcfg.useSuperWeapon || !w.superweaponsEnabled) return;  // 人格/对局选项门控
    Player& p = w.players[player];
    for (int i = 0; i < (int)SWType::COUNT; i++) {
        if (!p.swReady[i]) continue;
        SWType t = (SWType)i;
        if (t == SWType::IronCurtain || t == SWType::ForceShield) {
            // 铁幕/力场：套在己方部队或基地中心
            if (t == SWType::IronCurtain && countArmy(w) < 6) continue;
            Vec2i ac = findArmyCenter(w);
            if (t == SWType::ForceShield) {
                // 优先罩己方建造厂
                for (const World::Ent& e : w.ents) {
                    if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) {
                        ac = {(int)(e.x + 1), (int)(e.y + 1)};
                        break;
                    }
                }
            }
            w.launchSW(player, t, (float)ac.x, (float)ac.y);
        } else {
            // 核弹/闪电：炸敌方建筑密集区（优先建造厂/高科）
            float bestScore = -1, bx = 0, by = 0;
            for (const KnownBuilding& e : knownBuildings) {
                if (!w.isEnemy(player,e.owner)) continue;
                const BldDef& bd = bldDef(e.type);
                float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
                // 评分：高价值建筑 + 周围敌建筑密度
                float score = bd.cost / 500.0f;
                if (e.type == BldType::ConYard) score += 6;
                if (e.type == BldType::BattleLab) score += 3;
                for (const KnownBuilding& o : knownBuildings) {
                    if (o.owner != e.owner) continue;
                    const BldDef& od = bldDef(o.type);
                    if (distf(cx, cy, o.x + od.w / 2.0f, o.y + od.h / 2.0f) < 6.0f) score += 1.0f;
                }
                if (score > bestScore) { bestScore = score; bx = cx; by = cy; }
            }
            if (bestScore > 0) w.launchSW(player, t, bx, by);
        }
    }
}

// ===================== 支援技能（沿用原实现） =====================
void SkirmishAI::doSupport(World& w) {
    Player& p = w.players[player];
    // 尤里经济：优先让闲置新兵进入生化反应堆补电；把严重受损车辆送入回收炉，
    // 避免 AI 建出官方经济建筑却从不使用。
    if (p.faction == Faction::Yuri) {
        for (size_t bi = 0; bi < w.ents.size(); ++bi) {
            const World::Ent& b = w.ents[bi];
            if (!b.alive || !b.isBuilding || b.player != player || b.btype != BldType::BioReactor
                || (int)b.garrison.size() >= bldDef(b).garrisonCap) continue;
            std::vector<EID> occupants;
            for (size_t ui = 0; ui < w.ents.size(); ++ui) {
                const World::Ent& u = w.ents[ui];
                if (!u.alive || u.isBuilding || u.player != player || u.state != UState::Idle
                    || u.utype != UnitType::Initiate) continue;
                occupants.push_back((int)ui);
                if ((int)occupants.size() >= bldDef(b).garrisonCap - (int)b.garrison.size()) break;
            }
            if (!occupants.empty()) w.orderGarrison(occupants, (int)bi);
        }
        EID grinder = INVALID_EID;
        for (size_t i = 0; i < w.ents.size(); ++i)
            if (w.ents[i].alive && w.ents[i].isBuilding && w.ents[i].player == player
                && w.ents[i].btype == BldType::Grinder) { grinder = (int)i; break; }
        if (grinder != INVALID_EID) {
            std::vector<EID> recycle;
            for (size_t i = 0; i < w.ents.size(); ++i) {
                const World::Ent& u = w.ents[i];
                if (!u.alive || u.isBuilding || u.player != player || u.state != UState::Idle) continue;
                const UnitDef& ud = unitDef(u);
                if (ud.isAir() || ud.isNaval() || ud.canHarvet() || isHero(u.utype)) continue;
                if (u.hp < ud.hp / 4) recycle.push_back((int)i);
                if (recycle.size() >= 2) break;
            }
            if (!recycle.empty()) w.orderGarrison(recycle, grinder);
        }
    }
    // 1. 支援能力（伞兵 / 侦察机 / 心灵揭示）：对敌建造厂附近释放
    if(w.independentSupportEnabled)doIndependentSupport(w);
    else if (p.paradropReady) {
        float bx = -1, by = -1;
        for (const World::Ent& e : w.ents)
            if (e.alive && e.isBuilding && e.player >= 0 && w.isEnemy(player, e.player)
                && e.btype == BldType::ConYard) { bx = e.x + 3; by = e.y + 5; break; }
        if (bx < 0)
            for (const World::Ent& e : w.ents)
                if (e.alive && e.isBuilding && e.player >= 0 && w.isEnemy(player, e.player)) { bx = e.x + 2; by = e.y + 3; break; }
        if (bx >= 0) {
            auto fireSupport = [&](float x, float y) {
                if (w.hasParadropSource(player)) w.orderParadrop(player, x, y);
                else if (w.hasSpyPlaneSource(player)) w.orderSpyPlane(player, x, y);
                else if (w.hasPsychicRevealSource(player)) w.orderPsychicReveal(player, x, y);
            };
            if (w.hasParadropSource(player)) {
                // 伞兵：落点须为可站立陆地
                for (int r = 0; r < 8 && p.paradropReady; r++) {
                    for (int dy = -r; dy <= r && p.paradropReady; dy++)
                        for (int dx = -r; dx <= r && p.paradropReady; dx++) {
                            int nx = (int)bx + dx, ny = (int)by + dy;
                            if (!w.map.inBounds(nx, ny) || !w.map.passable(nx, ny)) continue;
                            if (w.map.at(nx, ny).terrain == Terrain::Water) continue;
                            fireSupport(nx + 0.5f, ny + 0.5f);
                        }
                }
            } else {
                fireSupport(bx, by);
            }
        }
    }
    // 2. 维修厂：重伤（<55%）或被寄生的车辆自动回厂维修
    EID depot = INVALID_EID;
    for (size_t i = 0; i < w.ents.size(); i++)
        if (w.ents[i].alive && w.ents[i].isBuilding && w.ents[i].player == player
            && w.ents[i].btype == BldType::ServiceDepot) { depot = (int)i; break; }
    if (depot != INVALID_EID) {
        std::vector<EID> hurt;
        for (size_t i = 0; i < w.ents.size(); i++) {
            World::Ent& e = w.ents[i];
            if (!e.alive || e.isBuilding || e.player != player) continue;
            const UnitDef& ud = unitDef(e);
            if (ud.isInfantry() || ud.isAir() || ud.pathDomain() != 0 || ud.canHarvet()) continue;
            bool needFix = e.hp < ud.hp * 55 / 100 || e.parasite != INVALID_EID;
            if (!needFix) continue;
            if(!automaticServiceReady(w,(EID)i,depot))continue;
            if (e.state == UState::Idle || (e.state == UState::Moving && e.target == INVALID_EID))
                hurt.push_back((int)i);
            if ((int)hurt.size() >= 3) break;  // 每波最多送修 3 辆，避免前线空虚
        }
        if (!hurt.empty()) w.orderService(hurt, depot);
    }
    // 3. 碉堡驻军：闲置步兵进驻战斗碉堡/中立民房，闲置坦克进驻坦克碉堡（加固基地防线）
    {
        // 基地中心（建造厂位置）
        float bcx = -1, bcy = -1;
        for (const World::Ent& e : w.ents)
            if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ConYard) { bcx = e.x; bcy = e.y; break; }
        if (bcx >= 0) {
            for (size_t i = 0; i < w.ents.size(); i++) {
                World::Ent& b = w.ents[i];
                if (!b.alive || !b.isBuilding) continue;
                int gdom = w.buildingEntryDomain(b);
                if (gdom == 0 || (int)b.garrison.size() >= bldDef(b).garrisonCap) continue;
                if (b.player >= 0 && b.player != player) continue;  // 只填己方或中立碉堡
                if (distf(b.x, b.y, bcx, bcy) > 24.0f) continue;   // 只填基地附近
                std::vector<EID> idle;
                for (size_t j = 0; j < w.ents.size(); j++) {
                    World::Ent& u = w.ents[j];
                    if (!u.alive || u.isBuilding || u.player != player || u.state != UState::Idle || u.attackMove.active) continue;
                    const UnitDef& ud = unitDef(u);
                    bool fit = (gdom == 1 && ud.isInfantry() && u.utype != UnitType::Engineer)
                            || (gdom == 2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain() == 0 && !ud.canHarvet());
                    if (fit && w.entryRejection((EID)j,(EID)i,EntryKind::Garrison)==EntryRejectReason::None)idle.push_back((int)j);
                    if ((int)idle.size() >= bldDef(b).garrisonCap - (int)b.garrison.size()) break;
                }
                if (!idle.empty()) w.orderGarrison(idle, (int)i);
            }
        }
    }
}

// ===================== 查询（沿用原实现） =====================
int SkirmishAI::countArmy(World& w) {
    int n = 0;
    for (const World::Ent& e : w.ents)
        if (e.player == player && combatUnit(e))
            n++;
    return n;
}

Vec2i SkirmishAI::findArmyCenter(World& w) {
    int sx = 0, sy = 0, n = 0;
    for (const World::Ent& e : w.ents)
        if (e.player == player && combatUnit(e)) {
            sx += (int)e.x; sy += (int)e.y; n++;
        }
    if (n == 0) return { w.map.w / 2, w.map.h / 2 };
    return { sx / n, sy / n };
}
