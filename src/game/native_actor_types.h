#pragma once
#include "game/data.h"
#include "game/native_target_types.h"
#include "game/native_weapon_types.h"
#include "game/native_ifv_types.h"
#include "game/native_crew_types.h"
#include "game/native_production_types.h"
#include "game/native_movement_types.h"
#include "game/native_flight.h"
#include "game/native_jumpjet.h"
#include <memory>
#include <array>
enum class NativeGatePhase : uint8_t { Closed,Opening,Open,Closing };

struct NativeActorDefinition {
    std::string id,name,art,transformsTo;
    bool isBuilding=false,selectable=true,significant=true,blocksMovement=true,remappable=false;
    bool rulesOnly=false; // Source gameplay definition with the ordinary sprite/interaction path.
    bool trainable=true;
    int rulesFaction=-1; // Source AIBasePlanningSide; -1 has no explicit building-side preference.
    int kind=0; // 0 placed army/building, 1 terrain object, 2 overlay, 3 elevated bridge
    bool immune=false,wall=false,bridgeRepairHut=false;
    bool immuneToPsionicWeapons=false,immuneToRadiation=false,immuneToPoison=false;
    bool vehicleRepairable=false;
    bool warpable=false; // Scene35: original Warpable; older scenes keep their own combat contract.
    bool gate=false;
    bool nativeBody=false;
    bool nativeRocket=false;
    bool canPassiveAcquire=true;
    bool sourceCrushing=false,crusher=false,crushable=false; // Scene47; earlier scenes retain armor-based eligibility.
    bool fraidycat=false; // Scene50 source infantry may scatter while already moving.
    bool isTrain=false; // Scene58 exit selection retains a train's rounded facing.
    bool crewed=false,engineer=false; // Scene64 source sale selection flags.
    bool sourceNavalTargeting=false,underwater=false;
    int navalTargeting=0,landTargeting=0;
    int walkSpeedLeptons=-1; // Scene15 walk locomotor; -1 keeps historical prototype movement.
    std::array<float,3> walkVeteranSpeed{1.f,1.f,1.f};
    NativeMovementProfile movement;
    NativeFlightProfile flight;
    NativeJumpjetProfile jumpjet;
    int airObstacleHeight=0;
    std::string weaponProjectile,weaponReport;
    std::array<float,11> weaponVerses{};
    std::array<float,11> eliteVerses{};
    WeaponDef nativeElite;
    bool hasNativeWeapon=false;
    int gateStages=9,gateOpenTicks=0,gateCloseTicks=0;
    int overlay=-1,artFrames=2,damageLevels=1;
    uint32_t radarRGB=0x5c5c5c;
    Armor armor=Armor::None;
    int originX=0,originY=0;
    UnitDef unit{};
    BldDef building{};
    NativeTargetProfile targeting;
    NativeGunProfile guns;
    NativeIFVProfile ifv;
    NativeCrewProfile crew;
    std::shared_ptr<const NativeWeaponCatalog> weaponCatalog; // keeps source pointers alive with actor/passenger definitions
    NativeProductionProfile production;
};

struct NativeActorIdentity {
    int type=-1;
    std::shared_ptr<const NativeActorDefinition> definition; // immutable; safe in World copies
    std::string object,tag;
    bool sleeping=false;
};

struct NativeSaleSurvivorRules {
    bool enabled=false;
    int alliedDivisor=0,sovietDivisor=0;
    int alliedCrew=-1,sovietCrew=-1,engineer=-1,technician=-1;
};

struct NativeBuildingSaleState {
    int8_t originalSide=-2; // -2 absent; -1 neutral; 0 Allied, 1 Soviet, 2 other.
    bool captured=false,pending=false;
};
