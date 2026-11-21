#include "game/ai.h"
#include <algorithm>

namespace {
int plannedUnits(const World& world,int player,UnitType type) {
    const auto& p=world.players[player];
    const int selector=world.productionSelector(player,true,(int)type);
    const int category=selector>=0?world.productionCategory(true,selector):unitDef(type).prodCat();
    return world.countUnits(player,type)+(p.unitProd[category].active && p.unitProd[category].typeIdx==selector)
        +(int)std::count(p.unitQueue[category].begin(),p.unitQueue[category].end(),selector);
}
}

void SkirmishAI::doProduction(World& w) {
    Player& p = w.players[player];

    // 攒钱建关键建筑：高科/超武造价高，建造序列未完成前暂停暴兵
    const AIBuildConfig& aiCfg = g_aiBuild[(int)p.faction];
    // 尤里超武1：心灵控制仪（替代盟军天气控制器/苏军核弹井）
    BldType swT = p.faction == Faction::Allies ? BldType::WeatherDevice
                : p.faction == Faction::Yuri   ? BldType::PsychicDominator
                : BldType::NukeSilo;
    // Turtler 攒钱更激进（为超武留更多余钱）
    int swSaveMargin = (personality == AIPersonality::Turtler) ? 1200 : 500;
    bool saveMoney = false;
    if (aiCfg.saveForSuperWeapon && w.hasBld(player, BldType::WarFactory) && !p.bldProd.active) {
        if (!w.hasBld(player, BldType::BattleLab) && w.prereqMet(player, bldDef(BldType::BattleLab))
            && p.money < Credits(w.buildingProductionCost(player,BldType::BattleLab)) + 300) saveMoney = true;
        if (w.hasBld(player, BldType::BattleLab) && !w.hasBld(player, swT)
            && p.money < Credits(w.buildingProductionCost(player,swT)) + swSaveMargin) saveMoney = true;
    }

    // ---- 车辆队列（类别1）：采矿车优先（经济命脉），其次主战坦克 ----
    if (w.hasBld(player, BldType::WarFactory) && w.unitQueuedCount(player, 1) < 2) {
        // 基地重建：建造厂被毁但有重工 → 补基地车
        if (!w.hasBld(player, BldType::ConYard)) {
            if (!constructionVehiclePlanned(w) && w.unitPrereqMet(player, unitDef(UnitType::MCV)))
                w.startUnitProd(player, UnitType::MCV);
            return;
        }
        int harvesters = plannedUnits(w,player,harvesterType(p.faction));
        // 采矿车目标按 econFocus 缩放
        int harvBase = aiCfg.enabled ? aiCfg.harvesterTarget : 3;
        int harvTarget = (int)(harvBase * pcfg.econFocus);
        if (harvTarget < 1) harvTarget = 1;
        if (w.skirmishMode != SkirmishMode::Megawealth
            && w.hasBld(player, BldType::OreRefinery) && harvesters < harvTarget) {
            w.startUnitProd(player, harvesterType(p.faction));
        } else if (!saveMoney && !w.nativeSkirmishAIActive(player)) {
            // 混编部队：陆军 >6 坦克且 0 防空车辆时补 AA 护航
            UnitType aaT = (p.faction == Faction::Soviet) ? UnitType::FlakTrack
                        : (p.faction == Faction::Allies) ? UnitType::IFV
                        : (p.faction == Faction::Yuri)   ? UnitType::GatlingTank
                        : UnitType::COUNT;
            // Fusion factions and mods may have a different legal AA roster.
            if(aaT==UnitType::COUNT || !unitDef(aaT).hasAntiAir()
                || !w.unitPrereqMet(player,unitDef(aaT))) {
                aaT=UnitType::COUNT;
                for(int type=0;type<(int)UnitType::COUNT;++type) {
                    const auto& candidate=unitDef((UnitType)type);
                    if(candidate.isAir() || candidate.isNaval() || candidate.isInfantry() || candidate.canHarvet()
                        || !candidate.hasAntiAir() || !w.unitPrereqMet(player,candidate)) continue;
                    if(aaT==UnitType::COUNT || candidate.cost<unitDef(aaT).cost) aaT=(UnitType)type;
                }
            }
            bool producedAA = false;
            if (aaT != UnitType::COUNT) {
                int tanks = 0, aa = 0, airThreats=0;
                for (const World::Ent& e : w.ents) {
                    if(e.alive && !e.isBuilding && w.isEnemy(player,e.player) && e.airborne() && w.map.fogAt(player,(int)e.x,(int)e.y)==FOG_VISIBLE) ++airThreats;
                    if (!e.alive || e.isBuilding || e.player != player) continue;
                    const UnitDef& ud = unitDef(e);
                    if (ud.canHarvet() || ud.isAir() || ud.isNaval() || ud.isInfantry()) continue;
                    if (ud.hasAntiAir()) aa++;
                    else tanks++;
                }
                const int wanted=std::clamp(airThreats,1,4);
                if ((tanks > 6 || airThreats>0) && aa < wanted && w.unitPrereqMet(player, unitDef(aaT))) {
                    w.startUnitProd(player, aaT);
                    producedAA = true;
                }
            }
            if (!producedAA && !produceSourceAircraft(w,1)) {
                bool late = w.hasBld(player, BldType::BattleLab);
                UnitType want;
                if (late) {
                    // 困难 AI 掺入特殊重单位（盟军光棱/苏军基洛夫/尤里主脑）
                    if ((difficulty == AIDiff::Hard || difficulty == AIDiff::Brutal) && attackWave % 3 == 2) {
                        UnitType sp = p.faction == Faction::Allies ? UnitType::PrismTank
                                    : p.faction == Faction::Soviet ? UnitType::Kirov
                                    : p.faction == Faction::Yuri ? UnitType::MasterMind
                                    : UnitType::Kirov;
                        const int source=w.productionSelector(player,true,(int)sp);
                        const bool sourceAir=w.nativeProductionType(source) && w.nativeTypes[source-NATIVE_PRODUCTION_BASE]->unit.isAir();
                        if (w.unitPrereqMet(player, unitDef(sp))
                            && (!sourceAir || plannedUnits(w,player,sp)<2)) { w.startUnitProd(player, sp); return; }
                    }
                    // 国家特色战车（RA2 原作：德国坦克杀手/苏俄磁能/利比亚自爆卡车；尤里无国家特色）
                    UnitType csp = UnitType::COUNT;
                    switch (p.country) {
                        case Country::Germany: csp = UnitType::TankDestroyer; break;
                        case Country::Russia:  csp = UnitType::TeslaTank; break;
                        case Country::Libya:   csp = UnitType::DemoTruck; break;
                        default: break;
                    }
                    if (csp != UnitType::COUNT && attackWave % 2 == 1 && w.countUnits(player, csp) < 6
                        && w.unitPrereqMet(player, unitDef(csp))) { w.startUnitProd(player, csp); return; }
                    // 阵营主战坦克：盟军光棱/苏军天启·磁能/中国99式/尤里狂风·磁电
                    if (p.faction == Faction::Allies) {
                        want = UnitType::PrismTank;
                    } else if (p.faction == Faction::Soviet) {
                        want = attackWave % 2 ? UnitType::Apocalypse : UnitType::TeslaTank;
                    } else if (p.faction == Faction::Yuri) {
                        // Keep damage dealers even when the attack-wave counter
                        // stalls: a frozen odd wave must not produce only support.
                        const int lashers=w.countUnits(player,UnitType::LasherTank);
                        const int magnets=w.countUnits(player,UnitType::Magnetron);
                        want = magnets<std::max(1,lashers/3)?UnitType::Magnetron:UnitType::LasherTank;
                    } else {
                        want = UnitType::Type99;
                    }
                    // 前置不满足回退到基础主战坦克
                    if (!w.unitPrereqMet(player, unitDef(want))) {
                        if (p.faction == Faction::Allies) want = UnitType::Grizzly;
                        else if (p.faction == Faction::Soviet) want = UnitType::Rhino;
                        else if (p.faction == Faction::Yuri) want = UnitType::LasherTank;
                        else want = UnitType::Type99;
                    }
                } else {
                    // 中期国家特色（德国坦克杀手前置=雷达即可）
                    if (p.country == Country::Germany && attackWave % 2 == 1
                        && w.countUnits(player, UnitType::TankDestroyer) < 4
                        && w.unitPrereqMet(player, unitDef(UnitType::TankDestroyer))) {
                        w.startUnitProd(player, UnitType::TankDestroyer);
                        return;
                    }
                    // 阵营基础主战坦克
                    if (p.faction == Faction::Allies) want = UnitType::Grizzly;
                    else if (p.faction == Faction::Soviet) want = UnitType::Rhino;
                    else if (p.faction == Faction::Yuri) want = UnitType::LasherTank;
                    else want = UnitType::Type99;
                }
                w.startUnitProd(player, want);
            }
        }
    }
    if(w.nativeSkirmishAIActive(player))return;
    // ---- 海军队列（类别3）：有船厂后维持舰队规模（战斗舰 4 + 运输船 1）----
    if (w.hasBld(player, BldType::NavalYard) && w.unitQueuedCount(player, 3) < 2) {
        // 阵营主战舰艇：盟军驱逐舰/苏军台风潜艇/中华神盾舰/尤里雷鸣潜艇
        UnitType shipT = UnitType::Destroyer;
        if (p.faction == Faction::Allies) shipT = UnitType::Destroyer;
        else if (p.faction == Faction::Soviet) shipT = UnitType::Typhoon;
        else if (p.faction == Faction::Yuri) shipT = UnitType::Boomer;
        else shipT = UnitType::Aegis;
        const UnitType strikeT=p.faction==Faction::Allies?UnitType::AircraftCarrier
            :p.faction==Faction::Soviet?UnitType::Dreadnought:p.faction==Faction::Yuri?UnitType::Boomer:UnitType::COUNT;
        // One escort first, then legal bombardment hulls. A submarine/AA-only
        // fleet cannot finish an enemy base, and an empty transport cannot raid it.
        if(plannedUnits(w,player,shipT)<1 && w.unitPrereqMet(player,unitDef(shipT))) {
            w.startUnitProd(player, shipT);
        } else if(plannedUnits(w,player,UnitType::AmphTransport)<1 && w.hasBld(player,BldType::Barracks)
            && w.unitPrereqMet(player,unitDef(UnitType::AmphTransport)) && needsTransport(w)) {
            w.startUnitProd(player,UnitType::AmphTransport);
        } else if(strikeT!=UnitType::COUNT && plannedUnits(w,player,strikeT)<2
            && w.unitPrereqMet(player,unitDef(strikeT))) {
            w.startUnitProd(player,strikeT);
        } else if(plannedUnits(w,player,shipT)<4 && w.unitPrereqMet(player,unitDef(shipT))) {
            w.startUnitProd(player,shipT);
        } else if(plannedUnits(w,player,UnitType::AmphTransport)<1
            && w.unitPrereqMet(player,unitDef(UnitType::AmphTransport))) {
            w.startUnitProd(player,UnitType::AmphTransport);
        }
    }
    // ---- 空军队列（类别2）：有空指部后维持 2 架（韩国黑鹰/盟军入侵者/苏军米格/中国基洛夫/尤里飞碟）----
    if(w.usesRulesProfile()) {
        if(!saveMoney)produceSourceAircraft(w,2);
    } else if (w.hasBld(player, BldType::AirForceCmd) && w.unitQueuedCount(player, 2) < 2) {
        UnitType airT;
        if (p.country == Country::Korea) airT = UnitType::BlackEagle;
        else if (p.faction == Faction::Allies) airT = UnitType::Intruder;
        else if (p.faction == Faction::Soviet) airT = UnitType::MiG;
        else if (p.faction == Faction::Yuri) airT = UnitType::FloatingDisc;
        else airT = UnitType::Kirov;
        if (plannedUnits(w,player,airT) < 2 && w.unitPrereqMet(player, unitDef(airT)))
            w.startUnitProd(player, airT);
    }
    // ---- 步兵队列（类别0）：前期暴兵 + 适量工程师/特殊步兵 ----
    if (w.hasBld(player, BldType::Barracks) && w.unitQueuedCount(player, 0) < 2) {
        // 工程师：维持 1~2 名（占领中立建筑/修复用）
        int engs = w.countUnits(player, UnitType::Engineer);
        int engineerTarget = w.skirmishMode == SkirmishMode::Megawealth ? 3 : 1;
        if (engs < engineerTarget && p.money > 700) {
            w.startUnitProd(player, UnitType::Engineer);
        } else if (!saveMoney) {
            // 阵营基础步兵：盟军美国大兵/苏军动员兵/中国解放军/尤里新兵
            // Rusher 产更多步兵（ratio 15）；其他人格 ratio 10
            int infRatio = (personality == AIPersonality::Rusher) ? 15 : 10;
            UnitType inf;
            if (p.faction == Faction::Allies) inf = UnitType::GI;
            else if (p.faction == Faction::Soviet) inf = UnitType::Conscript;
            else if (p.faction == Faction::Yuri) inf = UnitType::Initiate;
            else inf = UnitType::PLA;
            if (w.countUnits(player, inf) < infRatio) w.startUnitProd(player, inf);
        }
        // 特殊步兵（高科后，困难 AI 更积极；国家特色步兵优先：英狙击/古巴恐怖分子/伊辐射工兵；尤里无国家特色）
        if (p.money > 1500 && w.unitQueuedCount(player, 0) < 1) {
            UnitType csp = UnitType::COUNT;
            switch (p.country) {
                case Country::UK:   csp = UnitType::Sniper; break;
                case Country::Cuba: csp = UnitType::Terrorist; break;
                case Country::Iraq: csp = UnitType::Desolator; break;
                default: break;
            }
            if (csp != UnitType::COUNT && w.countUnits(player, csp) < 3
                && w.unitPrereqMet(player, unitDef(csp))) {
                w.startUnitProd(player, csp);
            } else if (w.hasBld(player, BldType::BattleLab)) {
                int roll = (attackWave + (int)(w.tick / 900)) % ((difficulty == AIDiff::Hard || difficulty == AIDiff::Brutal) ? 3 : 5);
                UnitType sp = UnitType::COUNT;
                if (roll == 0) {
                    // 阵营英雄/特种步兵：盟军谭雅/苏军尤里/中国辐射工兵/尤里狂兽人
                    if (p.faction == Faction::Allies) sp = UnitType::Tanya;
                    else if (p.faction == Faction::Soviet) sp = UnitType::Boris; // YR：苏军英雄鲍里斯（尤里归 FY）
                    else if (p.faction == Faction::Yuri) sp = UnitType::Brute;
                    else sp = UnitType::Desolator;
                } else if (roll == 1 && p.faction == Faction::Allies) {
                    sp = UnitType::Spy;  // 盟军间谍：渗透偷钱/断电
                } else if (roll == 1 && p.faction == Faction::Yuri) {
                    sp = UnitType::Virus;  // 尤里病毒狙击手：远程反步兵
                } else if (roll == 2 && p.faction == Faction::Allies) {
                    sp = UnitType::NavySEAL;  // 海豹部队：两栖 C4 突击
                } else if (roll == 2 && p.faction == Faction::Soviet) {
                    sp = UnitType::CrazyIvan;
                } else if (roll == 2 && p.faction == Faction::Yuri) {
                    sp = UnitType::Brute;  // 狂兽人：近战重甲反车辆
                }
                if (sp != UnitType::COUNT && w.unitPrereqMet(player, unitDef(sp))
                    && w.countUnits(player, sp) < 2)
                    w.startUnitProd(player, sp);
            }
        }
    }
}

