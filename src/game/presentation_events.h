#pragma once
#include <cstdint>
#include <vector>

// Event-time unit identities, independent of later death, capture or slot reuse.
// They describe accepted orders only and never enter simulation snapshots/hashes.
struct OrderActor {
    int unitType=-1,faction=-1;
    bool operator==(const OrderActor&) const = default;
};
inline void addOrderActor(std::vector<OrderActor>& actors,int type,int faction) {
    for(const auto& actor:actors)if(actor.unitType==type && actor.faction==faction)return;
    actors.push_back({type,faction});
}

enum class Sfx : uint8_t {
    Shot = 0,   // 步枪
    Cannon,     // 坦克炮
    Flak,       // 高炮
    Missile,    // 导弹发射
    Explosion,  // 爆炸
    BigExplosion, // 建筑级大爆炸
    Tesla,      // 磁暴电弧
    Prism,      // 光棱光束
    Click,      // UI 点击
    Place,      // 建筑放置
    Ready,      // 生产就绪提示
    Cash,       // 资金到账
    Alarm,      // 低电力警报
    Deploy,     // 基地车展开
    Sell,       // 出售建筑
    NukeLaunch, // 核弹发射警报
    NukeBlast,  // 核爆
    Lightning,  // 闪电劈落
    Storm,      // 风暴起
    IronCurtain,// 铁幕启动
    SWReady,    // 超武就绪
    Crush,      // 坦克碾压
    Eva,        // EVA 播报提示音
    NavalCannon,// 舰炮
    Torpedo,    // 鱼雷发射
    Dig,        // 矿车挖掘研磨
    MirageFire, // 幻影坦克主炮
    RhinoFire,  // 犀牛/99式主炮
    ApocFire,   // 天启双管
    COUNT
};

enum class PresentationKind : uint8_t { Sound, MoveOrder, Muzzle, Impact, AttackOrder, EngineerOrder, EntryOrder, ReleaseOrder, ServiceOrder, DeployOrder, RallyOrder, PlacementOrder, ProductionOrder, SuperweaponOrder, BuildingOrder, HarvestOrder };
enum class BuildingAction : uint8_t { Sell, Repair };
enum class BuildingRejectReason : uint8_t { None, InvalidTarget, WrongOwner, Selling, ConstructionYard, FullHealth, Constructing };
struct BuildingOrderResult {
    BuildingAction action=BuildingAction::Repair;
    BuildingRejectReason reason=BuildingRejectReason::None;
    bool accepted=false,repairing=false;
};
enum class SuperweaponRejectReason : uint8_t { None, InvalidType, InvalidTarget, Unavailable, NotReady, NoSource, Blocked };
struct SuperweaponOrderResult {
    int type=-1,transferred=0;
    bool accepted=false;
    SuperweaponRejectReason reason=SuperweaponRejectReason::None;
};
enum class ProductionAction : uint8_t { Start, Pause, Resume, Cancel };
enum class ProductionRejectReason : uint8_t { None, InvalidType, Unavailable, MissingFactory, MissingPrerequisite, QueueFull, UniqueLimit, Busy, NoOrder, Ready };
struct ProductionOrderResult {
    int type=-1,refund=0,queueLength=0;
    bool unit=false,accepted=false,queued=false;
    ProductionAction action=ProductionAction::Start;
    ProductionRejectReason reason=ProductionRejectReason::None;
};
enum class PlacementRejectReason : uint8_t { None, InvalidType, InvalidLocation, NotReady, Blocked };
struct PlacementOrderResult {
    int type=-1;
    int request=0; // UI receipt token; has no effect on simulation.
    bool accepted=false;
    PlacementRejectReason reason=PlacementRejectReason::None;
};
enum class RallyStatus : uint8_t { Set, Cleared, Adjusted, Unreachable };
enum class RallyRejectReason : uint8_t { None, Unsupported, InvalidTarget, Selling };
struct RallyOrderResult {
    int factory=-1,unit=-1;bool accepted=false;
    RallyStatus status=RallyStatus::Set;
    RallyRejectReason reason=RallyRejectReason::None;
};
enum class DeployRejectReason : uint8_t { None, Unsupported, Immobilized, Blocked, Controlled, RepackingDisabled, InvalidTarget, Busy };
struct DeployChange { int source=-1,replacement=-1;bool building=false;uint64_t sourceGeneration=0,replacementGeneration=0; };
struct DeployOrderResult {
    int accepted=0,rejected=0,deployed=0,undeployed=0,expanded=0,packed=0;
    int deploying=0,undeploying=0;
    int failedEntity=-1,feedbackEntity=-1;
    float x=0,y=0;
    DeployRejectReason reason=DeployRejectReason::None;
    std::vector<DeployChange> changes;
};
enum class ServiceRejectReason : uint8_t { None, InvalidTarget, Unsupported, WrongOwner, Incompatible, FullHealth, Deployed, Unreachable };
enum class ServiceStatus : uint8_t { Accepted, Working, WaitingMoney, WaitingPower, Completed, Failed };
struct ServiceOrderResult {
    int accepted=0,rejected=0,failedEntity=-1;
    ServiceRejectReason reason=ServiceRejectReason::None;
};

enum class ReleaseKind : uint8_t { Unload, Ungarrison };
enum class ReleaseRejectReason : uint8_t { None, InvalidTarget, Unsupported, Empty, Blocked, Immobilized };
struct ReleaseOrderResult {
    int released=0,remaining=0,rejected=0,failedEntity=-1;
    ReleaseRejectReason reason=ReleaseRejectReason::None;
    int pending=0;
};
enum class EntryKind : uint8_t { Board, Garrison };
enum class EntryRejectReason : uint8_t { None, InvalidTarget, Unsupported, WrongOwner, Incompatible, Full, Deployed, Unreachable };
struct EntryOrderResult {
    int accepted=0,rejected=0,failedEntity=-1;
    EntryRejectReason reason=EntryRejectReason::None;
};
enum class EngineerIntent : uint8_t { None, Capture, Repair, RepairBridge };
enum class EngineerRejectReason : uint8_t { None, InvalidTarget, NoEngineer, CaptureOwner, RepairOwner, NotCapturable, FullHealth, Unreachable, NoBridge, BridgeIntact, BridgeBlocked };
struct EngineerOrderResult {
    int accepted=0,rejected=0,failedEntity=-1;
    EngineerRejectReason reason=EngineerRejectReason::None;
};
enum class AttackRejectReason : uint8_t { None, InvalidTarget, NoWeapon, NoAntiAir, NoAntiGround, NavalOnly, Ineffective, Unsupported, Deployed, Unreachable, Busy, FullHealth };
struct AttackOrderResult {
    int accepted=0,rejected=0,failedEntity=-1;
    AttackRejectReason reason=AttackRejectReason::None;
    std::vector<OrderActor> actors;
    std::vector<int> acceptedUnits; // Immediate command receipt, before any entity slot can be reused.
    bool healing=false;
};
enum class ChronoVisual : uint8_t { Arrival, Departure, Erase };

enum class VisualTarget : uint8_t { Infantry, Vehicle, Aircraft, Building };
enum class MoveRejectReason : uint8_t { None, Unreachable, Deployed, InvalidTarget, Immobilized, Busy };
struct MoveOrderResult {
    int accepted = 0, rejected = 0, queued = 0, adjusted = 0;
    MoveRejectReason reason = MoveRejectReason::None;
    int failedEntity = -1;
    std::vector<OrderActor> actors;
    std::vector<int> acceptedUnits; // Includes queued orders; independent of presentation collection.
    bool attackMove=false;
};

// Presentation-only stream; neither serialized nor hashed. Consumers never mutate World.
struct PresentationEvent {
    uint64_t tick = 0, sequence = 0;
    int entity = -1;
    Sfx sound = Sfx::Click;
    float x = 0, y = 0, volume = 1;
    bool positional = false;
    PresentationKind kind = PresentationKind::Sound;
    int player = -1;
    MoveOrderResult move;
    DeployOrderResult deploy;
    RallyOrderResult rally;
    PlacementOrderResult placement;
    ProductionOrderResult production;
    SuperweaponOrderResult superweapon;
    BuildingOrderResult building;
    AttackOrderResult attack;
    EngineerOrderResult engineer;
    EngineerIntent engineerIntent=EngineerIntent::None;
    EntryOrderResult entry;
    EntryKind entryKind=EntryKind::Board;
    ServiceOrderResult service;
    ServiceStatus serviceStatus=ServiceStatus::Accepted;
    ReleaseOrderResult release;
    ReleaseKind releaseKind=ReleaseKind::Unload;
    VisualTarget visualTarget = VisualTarget::Vehicle;
    // Event-time elevation, independent of later entity reuse or terrain changes.
    // Uncaptured synthetic events retain the previous full-altitude/default-ground policy.
    float flightFraction = 1;
    float nativeAltitudePixels = -1;
    int magneticHeight = 0, terrainHeight = -1;
    float targetX = 0, targetY = 0;
    int damage = 0;
    bool lethal = false;
    std::vector<OrderActor> harvestActors;
    // Timed turret pose at emission. The renderer resolves the model's barrel end.
    int muzzleUnitType = -1;
    uint16_t muzzleHeading = 0;
};
