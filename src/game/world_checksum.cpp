#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
        if(nativeEvents.active){h.i32(p.nativeEventTag);h.i32(p.nativeLastEnteredCell);}
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
        h.i32(w.nativeType);h.i32(w.nativeSlot);h.boolean(w.nativeVerses!=nullptr);if(w.nativeVerses)for(int i=0;i<11;++i)h.fp(w.nativeVerses[i]);
    };

    h.u64(tick); h.i32(numPlayers); h.u64(rng.s);
    h.boolean(cratesEnabled); h.boolean(aiAlliance); h.u8((uint8_t)skirmishMode);
    h.boolean(sharedVision); h.boolean(shortGame); h.boolean(superweaponsEnabled);
    h.boolean(mcvRepacks);
    h.i32(map.w); h.i32(map.h); h.size(map.cells.size());
    for (const Cell& c : map.cells) {
        h.u8((uint8_t)c.terrain); h.u8((uint8_t)c.overlay); h.u8(c.variant);
        h.i32(c.ore); h.i32(c.oreMax); h.u8(c.height);
        h.boolean(c.present);h.boolean(c.blocked);
    }
    auto nativeText=[&](const std::string& text){h.size(text.size());for(unsigned char c:text)h.u8(c);};
    const auto& native=map.native;nativeText(native.assetId);nativeText(native.sourceHash);nativeText(native.theater);
    h.i32(native.width);h.i32(native.height);for(int v:native.local)h.i32(v);h.size(native.tiles.size());
    for(const auto& t:native.tiles) {
        h.u32(t.tile);h.u32(t.auxiliary);h.u8(t.subtile);h.u8(t.auxiliary2);h.u8(t.overlay);h.u8(t.overlayData);
        h.u8(t.terrainType);h.u8(t.ramp);
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

    h.boolean(nativeBridgesDestroyable);
    for(const auto& entity:ents)h.u64(entity.generation);
    h.u32(attackMoveChecksum());
    if(nativeProductionRules.available)h.u64(nativeProductionSerial);
    if(nativeAIRules.available)h.u32(nativeAIChecksum());
    if(nativeCrateRules.available)h.u32(nativeCrateChecksum());
    if(nativeEventDefinition && nativeEventDefinition->extendedConditions)h.u32(nativeConditionChecksum());
    if(nativeEventDefinition) {
        h.boolean(nativeEvents.active);
        if(nativeEvents.active) {
            h.u8((uint8_t)nativeEvents.outcome);h.u64(nativeEvents.outcomeAt);h.i32(nativeEvents.outcomeTrigger);
            h.i32(nativeEvents.difficulty);h.boolean(nativeEvents.inputLocked);h.size(nativeEvents.tags.size());
            for(const auto& tag:nativeEvents.tags){h.i32(tag.definition);h.boolean(tag.alive);}
            h.size(nativeEvents.triggers.size());
            for(const auto& trigger:nativeEvents.triggers){h.i32(trigger.definition);h.i32(trigger.tag);h.boolean(trigger.enabled);h.boolean(trigger.destroyed);
                h.u64(trigger.timerStart);h.u64(trigger.eventMask);h.u64(trigger.firings);h.i32(trigger.blockedAction);}
            h.size(nativeEvents.teams.size());
            for(const auto& team:nativeEvents.teams){h.i32(team.definition);h.i32(team.tag);h.i32(team.step);h.boolean(team.alive);h.boolean(team.reinforced);
                h.u64(team.waitUntil);h.boolean(team.assembled);h.boolean(team.lineStarted);h.boolean(team.focusFixed);
                h.i32(team.memberHouse);h.i32(team.focusLayer);
                h.boolean(team.advancePending);h.boolean(team.succeeded);
                h.i32(team.focus);h.i32(team.focusCell);h.i32(team.blockedScript);
                h.size(team.members.size());for(auto member:team.members)h.i32(member);}
        }
    }
    h.size(ents.size());
    h.size(nativeLaunchers.size());for(const auto& l:nativeLaunchers){h.i32(l.actor);h.i32(l.definition);h.i32(l.target);h.u64(l.nextLaunch);
        h.size(l.nodes.size());for(const auto& node:l.nodes){h.i32(node.missile);h.u64(node.readyAt);}}
    h.boolean(aswEnabled);h.boolean(aircraftSpawnersEnabled);h.size(aircraftSpawners.size());
    for(const auto& s:aircraftSpawners){h.i32(s.owner.id);h.u64(s.owner.generation);h.i32(s.target.id);h.u64(s.target.generation);
        h.i32(s.player);h.i32(s.definition);h.i32(s.launchRemaining);h.u64(s.pending);
        h.i32(s.regenerateTicks);h.i32(s.reloadTicks);h.i32(s.launchDelayTicks);h.size(s.nodes.size());
        for(const auto& n:s.nodes){h.u8((uint8_t)n.phase);h.i32(n.aircraft.id);h.u64(n.aircraft.generation);h.i32(n.cargo);h.i32(n.remaining);}}
    h.size(nativeRockets.size());for(const auto& r:nativeRockets){h.i32(r.actor);h.i32(r.definition);h.i32(r.source);h.i32(r.slot);h.u8((uint8_t)r.phase);
        h.i32(r.phaseTicks);h.fp(r.altitude);h.fp(r.speed);h.fp(r.pitch);h.fp(r.heading);h.fp(r.destinationX);h.fp(r.destinationY);h.fp(r.destinationZ);
        h.boolean(r.elite);h.boolean(r.cruised);h.boolean(r.diving);}
    for (const Ent& e : ents) {
        h.boolean(e.alive);
        if (!e.alive) continue;
        h.boolean(e.isBuilding); h.i32(e.player); h.u8((uint8_t)e.utype); h.u8((uint8_t)e.btype);
        h.fp(e.x); h.fp(e.y); h.i32(e.dir); h.i32(e.turretDir); h.i32(e.hp);
        h.u8(e.navLayer);h.u8(e.goalLayer);
        if(nativeEvents.active){h.i32(e.nativeEventTag);h.i32(e.nativeLastEnteredCell);}
        if(e.nativeIdentity.definition && e.nativeIdentity.definition->gate) {
            h.u8((uint8_t)e.nativeGatePhase);h.i32(e.nativeGateProgress);h.i32(e.nativeGateHold);
        }
        h.size(e.path.size());
        for (const Vec2i& wp : e.path) { h.i32(wp.x); h.i32(wp.y); }
        h.size(e.wps.size());
        for (const auto& wp : e.wps) { h.fp(wp.first); h.fp(wp.second);h.u8((uint8_t)(wp.layer+1)); }
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
        if(nativeCrateRules.randomCrates)h.i32(c.remainingTicks);
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
    nativeText(nativeScenarioSource);for(uint8_t row:nativeAllies)h.u8(row);
    auto identity=[&](const NativeActorIdentity& n){h.i32(n.type);nativeText(n.object);nativeText(n.tag);h.boolean(n.sleeping);};
    auto passengerIdentity=[&](auto&& self,const Ent::GarrisonedUnit& p)->void{identity(p.nativeIdentity);for(const auto& child:p.cargo)self(self,child);};
    for(const auto& e:ents){identity(e.nativeIdentity);for(const auto& p:e.cargo)passengerIdentity(passengerIdentity,p);for(const auto& p:e.garrison)passengerIdentity(passengerIdentity,p);}
    if(nativeMovementRules.available)h.u32(nativeMovementChecksum());
    h.u32(nativeFlightChecksum());h.u32(nativeJumpjetChecksum());h.u32(nativeAirNavigationChecksum());
    // Keep historical hashes unchanged when both World56 features are disabled.
    if(veterancyProgressEnabled || independentSupportEnabled) {
        h.u32(0x53555050);h.boolean(veterancyProgressEnabled);h.boolean(independentSupportEnabled);
        for(const auto& player:players)for(const auto& charge:player.support){h.i32(charge.ticks);h.boolean(charge.ready);}
    }
    if(supportDeliveryEnabled)h.u32(supportDeliveryChecksum());
    if(nativeCellOrderEnabled()) {
        h.u32(0x4f4c4543);h.u64(nativeCellSerial);
        for(const auto& e:ents)h.u64(e.nativeCellOrder);
    }
    if(nativeTemporalEnabled)h.u32(nativeTemporalChecksum());
    if(nativeCrewRules.enabled)h.u32(nativeCrewChecksum());
    if(nativeGroundEnabled())h.u32(nativeGroundChecksum());
    if(nativeTravelEnabled())h.u32(nativeTravelChecksum());
    return h.value;
}

