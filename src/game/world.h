#pragma once
#include "game/native_ground.h"
#include "game/native_travel.h"
#include "game/money.h"
#include "game/data.h"
#include "game/native_actor_types.h"
#include "game/native_event_types.h"
#include "game/native_condition_types.h"
#include "game/native_ai_types.h"
#include "game/native_crate_types.h"
#include "game/native_rocket_types.h"
#include "game/native_aircraft_types.h"
#include "game/map.h"
#include "game/presentation_events.h"
#include "game/eva_cues.h"
#include <vector>
#include <deque>
#include <string>
#include <cstdio>
#include <map>

// 实体 ID 类型（索引）
using EID = int;
constexpr EID INVALID_EID = -1;

// 单位状态
enum class UState : uint8_t { Idle, Moving, AttackMoving, Chasing, Attacking, HarvestGo, HarvestDig, HarvestReturn, HarvestUnload,
                              Circling, Returning, Landed, Boarding, Servicing,
                              DeployLanding, DeployUnfolding, DeployFolding, DeployTakeoff };

//  projectile 类型
enum class ProjKind : uint8_t { Bullet, Shell, Flak, Missile };

struct Projectile {
    bool alive = true;
    ProjKind kind = ProjKind::Bullet;
    int player = 0;
    float x = 0, y = 0;         // 瓦片浮点坐标
    float tx = 0, ty = 0;       // 目标点（开火锁定或导弹瞄准点）
    float ox = 0, oy = 0;       // 发射点（弧线进度）
    float z = 0;                // 绘制高度（格，弧线/对空）
    EID target = INVALID_EID;         // 追踪目标（可 INVALID）
    EID src = INVALID_EID; // 发射者（军衔经验归属）
    uint64_t sourceActor=0; // Scene36 durable combat identity, including contained/unloaded passengers.
    int srcGarrisonSlot = -1; // 建筑驻军弹道的实际乘员
    WeaponDef w{};
    int speed = 0;          // 每 tick 位移（百分之一格）；由 SpeedLeptons 换算
    int rot = 0;            // rulesmd ROT；>0 制导
    bool arcing = false;
    bool proximity = false;
    int trail = 0;
    int hp = 0;         // 可拦截导弹生命（0=不可拦截；V3/无畏舰大型导弹 >0，防空火力可击落）
};

struct Effect {
    bool alive = true;
    int kind = 0;           // 0 爆炸 1 烟雾 2 磁暴电弧 3 光棱光束 4 建筑爆炸 9 超时空抹除 10 单位死亡动画 11 采矿尘土
    float x = 0, y = 0;
    float x2 = 0, y2 = 0;       // 光束终点
    int age = 0;
    int maxAge = 0;
    // kind=10 死亡动画参数：utype/dir/colorId；kind=3 aux=1 磁电建筑束
    int aux = 0, aux2 = 0, aux3 = 0;
};

// 生产项
struct ProdItem {
    bool active = false;
    bool isUnit = false;
    int typeIdx = 0;        // UnitType 或 BldType
    int progress = 0;       // 0..buildTime
    int totalCost = 0;  // 开工时锁定造价（工业工厂中途增毁不追溯）
    int paid = 0;       // 已实际扣款；取消时原额返还，避免速度倍率导致退款误差
    bool ready = false; // 建筑已就绪待放置
    bool held = false;  // RA2：右键暂停生产（HOLD），左键继续，再右键取消
    int workRemainder = 0; // fractional work, 0..1023; schema v19
};

struct ProductionFacilityState {
    int present=0,operational=0,frozen=0,drained=0,constructing=0,selling=0;
};

enum class MatchOutcome : uint8_t { Running, Victory, Defeat };

// 单位生产队列类别数（步兵/车辆/空军/海军）
constexpr int PROD_CAT_N = 4;

// 官方遭遇战模式。规则由 World 统一执行，避免菜单选项只有显示效果。
enum class SkirmishMode : uint8_t {
    Battle = 0,
    FreeForAll,
    UnholyAlliance,
    Megawealth,
    LandRush,
    MeatGrinder,
    NavalWar,
    COUNT,
};

struct Player {
    bool active = false;
    bool isAI = false;
    bool defeated = false;
    Faction faction = Faction::Allies;
    Country country = Country::None; // 国家（RA2 原作：阵营内细分，决定特色单位/能力）
    int colorId = 0;
    Credits money = 10000;
    std::string name;

    ProdItem bldProd;                     // 建筑生产队列（RA2：与防御独立）
    ProdItem defProd;                     // 防御/超武生产队列（可与建筑并行）
    ProdItem unitProd[PROD_CAT_N];        // 单位生产：按类别独立队列（RA2 原作设定）
    std::deque<int> unitQueue[PROD_CAT_N]; // 各类别排队待产的类型索引（队首为当前项的后续）
    BldType placingBld = BldType::COUNT; // Legacy v6-v23 save slot; current UI preview lives in Game.

    // 统计缓存（每帧重算）
    int powerMade = 0, powerUsed = 0;
    int powerSabotage = 0;   // 间谍破坏电厂：>0 期间强制低电
    int revealTimer = 0;     // 间谍渗透雷达：>0 期间全图可见
    bool vetCat[PROD_CAT_N] = {}; // 间谍渗透工厂：对应类别新造单位直接 1 级军衔
    int aiDifficulty = 1;    // AI 难度 0 简单 1 普通 2 困难（仅 AI 玩家）
    // ---- RA2 补全：国家/支援技能/中立科技 ----
    int secretLabUnlock = 0;   // 秘密实验室解锁的国家特色（(int)Country；0=未解锁）
    int stolenTech = 0;        // Source v27: allied/Soviet/third house technology bits 1/2/4; legacy/fusion: unit unlock bits.
    int paradropCharge = 0;    // 伞兵充能（>=PARADROP_TIME 就绪；美国空指部/科技机场提供）
    bool paradropReady = false;
    std::array<SupportCharge,SUPPORT_COUNT> support;
    bool lowPower() const { return powerSabotage > 0 || powerUsed > powerMade; }

    // 超武：充能进度（>=chargeTime 即就绪）；激活效果计时
    int swCharge[(int)SWType::COUNT] = {};
    bool swReady[(int)SWType::COUNT] = {};
    // 闪电风暴激活中
    int stormTimer = 0;
    float stormX = 0, stormY = 0;
    int stormBoltCd = 0;

    // EVA 播报节流（逻辑帧倒计时）
    int evaBaseCd = 0;   // 基地遭袭
    int evaMinerCd = 0;  // 采矿车遭袭
    int evaUnitCd = 0;   // 单位损失
};

// 核弹飞行物（全局，跨玩家）
struct Nuke {
    bool active = false;
    int player = 0;
    float tx = 0, ty = 0;   // 目标点
    int timer = 0;          // 落地倒计时
};

class World {
public:
    // A slot and its incarnation fit a positive Lua int64. Exhausted slots
    // are retired permanently, so public handles cannot wrap to an old unit.
    static constexpr uint64_t MAX_ENTITY_GENERATION=0x7fffffffull;
    class EntityMutationScope {
        World& world;
    public:
        explicit EntityMutationScope(World& owner);
        ~EntityMutationScope();
        EntityMutationScope(const EntityMutationScope&)=delete;
        EntityMutationScope& operator=(const EntityMutationScope&)=delete;
    };
    struct EntityRef {
        EID id=INVALID_EID;
        uint64_t generation=0;
        bool operator==(const EntityRef&) const = default;
    };
    // Application observation identity, excluded from persistence and simulation checksum.
    static uint64_t nextPresentationEpoch();
    uint64_t presentationEpoch = 0;
    // Derived per-tick movement diagnostics; not persisted or hashed.
    std::vector<EID> movementReservations; // reconstructed each tick in stable EID order
    int repathsThisTick = 0;
    uint64_t repathRequests = 0, repathFailures = 0;
    struct TrafficDebug {
        int domain=0,anchor=-1,exit=-1,cells=0,ports=0,groups=0,occupants=0;
        uint64_t remaining=0;
        bool draining=false,opposed=false;
    };
    std::vector<TrafficDebug> trafficDebug() const;
    uint64_t producedUnits[MAX_PLAYERS] = {}, harvestedIncome[MAX_PLAYERS] = {}, combatDamage[MAX_PLAYERS] = {};
    bool repathUnit(EID id, int gx, int gy, std::vector<Vec2i>& path, EID fixedBlocker=INVALID_EID,int goalLayer=-1);
    std::deque<PresentationEvent> presentationEvents;
    uint64_t presentationSequence = 0;
    void emitMoveResult(int player, EID entity, float x, float y, const MoveOrderResult& result) {
        if (!result.accepted && !result.rejected && !result.queued) return;
        PresentationEvent event;
        event.tick=tick; event.sequence=presentationSequence++;
        event.kind=PresentationKind::MoveOrder; event.player=player;
        event.entity=result.failedEntity!=INVALID_EID ? result.failedEntity : entity;
        event.x=x; event.y=y; event.move=result;
        if(std::isfinite(x) && std::isfinite(y) && x>=0 && y>=0 && x<map.w && y<map.h && map.inBounds((int)x,(int)y)) {
            const int layer=valid(entity) && !ents[entity].isBuilding?movementLayerAt(ents[entity],(int)x,(int)y):0;
            event.terrainHeight=map.surfaceHeight((int)x,(int)y,layer);
        }
        if (presentationEvents.size()>=4096) presentationEvents.pop_front();
        presentationEvents.push_back(event);
    }
    void emitSound(Sfx sound, float volume = 1.0f, int player = -1) {
        if (presentationEvents.size() >= 4096) presentationEvents.pop_front();
        presentationEvents.push_back({tick, presentationSequence++, INVALID_EID, sound, 0, 0, volume, false, PresentationKind::Sound, player});
    }
    void emitSoundAt(Sfx sound, float x, float y, EID entity = INVALID_EID) {
        if (presentationEvents.size() >= 4096) presentationEvents.pop_front();
        presentationEvents.push_back({tick, presentationSequence++, entity, sound, x, y, 1, true});
    }

    Map map;

    struct Ent {
        struct GarrisonedUnit {
            UnitType type = UnitType::GI;
            int hp = 1;
            int kills = 0;
            int veterancyValue = 0;
            int vetRank = 0;
            uint64_t combatActor=0;
            int oreLoad=0,gemLoad=0,ammo=0,atkCd=0,invuln=0;
            int crateDmgBoost=0,crateArmorBoost=0,crateSpeedBoost=0;
            int airstrikeCd=0,confused=0,gatlingHeat=0,gatlingStage=0;
            bool autoHarvest=true,permaControlled=false;
            std::vector<GarrisonedUnit> cargo;
            NativeActorIdentity nativeIdentity;
            int nativeEventTag=-1,nativeLastEnteredCell=-1;
        };
        bool alive = false;
        uint64_t combatActor=0;
        uint64_t generation = 0; // Slot incarnation; survives retirement and is saved/hashed.
        bool isBuilding = false;
        int player = -1;
        UnitType utype = UnitType::GI;
        BldType btype = BldType::ConYard;
        NativeActorIdentity nativeIdentity;
        uint8_t navLayer=0,goalLayer=0;
        NativeMovementState nativeMovement;
        NativeGroundState nativeGround;
        NativeDriveArc driveArc;
        uint16_t previousHullHeading=0; // presentation only
        uint16_t previousTurretHeading=0; // presentation only
        struct DockState {
            NativeDockPhase phase=NativeDockPhase::None;
            EntityRef receiver;
            bool operator==(const DockState&) const=default;
        } nativeDock;
        EntityRef pendingDockRefinery; // Explicit target kept until a physical bay is reserved.
        bool hasPendingDockReturn() const {
            return alive && state==UState::HarvestReturn && pendingDockRefinery.id!=INVALID_EID;
        }
        ChronoReturnState chronoReturn;
        bool claimsNativeDock() const {
            return nativeDock.phase>=NativeDockPhase::Align || chronoReturn.phase==ChronoReturnPhase::Charging
                || chronoReturn.phase==ChronoReturnPhase::Settling;
        }
        bool exitingNativeDock() const {
            return nativeDock.phase==NativeDockPhase::Exit || nativeDock.phase==NativeDockPhase::DriveExit;
        }
        bool waitingForNativeDeparture() const {
            return exitingNativeDock() && digTimer<0;
        }
        void cancelNativeDockTiming() {
            if(digTimer<0 && (nativeDock.phase==NativeDockPhase::Unload || exitingNativeDock()))
                digTimer=0;
            if(nativeDock.phase==NativeDockPhase::DriveExit) {
                nativeDock.phase=NativeDockPhase::Exit;
                nativeMovement={};
            }
        }
        bool hasChronoYieldPath() const {
            return chronoReturn.phase==ChronoReturnPhase::Waiting && chronoReturn.directBerth && !path.empty();
        }
        void cancelChronoReturn() {
            if(chronoReturn.phase==ChronoReturnPhase::None)return;
            if(state==UState::HarvestReturn && hasChronoYieldPath()) {
                clearPath();nativeMovement={};blockTick=0;
            }
            chronoReturn={};nativeDock={};dockRefinery=INVALID_EID;
        }
        NativeFlightState nativeFlight;
        NativeJumpjetState nativeJumpjet;
        bool nativeAirUnload=false;
        uint8_t supportAir=0,supportFallHalf=0; // 1 mission aircraft, 2 descending passenger
        int supportFallSpeed=0;
        uint64_t supportSince=0;
        uint64_t nativeAirLandingRetry=0; // bounded retry for a landing search or blocked unloading exit
        uint8_t previousNavLayer=0; // interpolation only, neither saved nor hashed
        int nativeGateProgress=0,nativeGateHold=0;
        NativeGatePhase nativeGatePhase=NativeGatePhase::Closed;
        float nativeAltitude=0,previousNativeAltitude=0,nativePitch=0; // derived from native rocket state
        int nativeEventTag=-1,nativeLastEnteredCell=-1;
        float x = 0, y = 0;        // 单位：浮点瓦片坐标；建筑：左上格
        float px = 0, py = 0;      // 上一逻辑帧位置（渲染插值用，update() 开头快照）
        int occCell = -1;          // 单位格占位链表：当前 cellIdx，-1=未挂入（派生数据，不入存档/checksum）
        EID occNext = INVALID_EID; // 同格下一单位
        uint64_t nativeCellOrder=0; // Scene33: original movable-object LIFO order
        int nativeOrderCell=-1,nativeOrderLayer=-1; // derived tracking, not serialized
        int airBucket = -1;        // 空中粗桶索引（4×4 格），派生数据
        EID airNext = INVALID_EID; // 同桶下一飞行单位
        int dir = 2;
        int turretDir = 2;
        int previousDir=2,previousTurretDir=2; // interpolation only, neither saved nor hashed
        int hp = 1;
        // 移动
        std::vector<Vec2i> path;
        int pathIdx = 0;
        void clearPath() { path.clear();pathIdx=0;driveArc={}; }
        int moveTick = 0;
        int blockTick = 0;          // 路径被堵计时（超时放弃路径）
        int walkFrame = 0, walkAnim = 0; // walkAnim: continuous successful-motion ticks, independent of sprite frames
        // 动画状态机（art.ini 序列驱动）
        int fireAnim = 0;       // 开火动画剩余 tick（>0 播放开火序列，phase 由剩余量推导）
        int constructAnim = 0;  // 建筑建造动画剩余 tick（>0 播放 mk 序列，播完为成品）
        int deployAnim = 0;     // Remaining simulation ticks in the current deployment phase.
        // 战斗
        UState state = UState::Idle;
        int atkCd = 0;
        EID target = INVALID_EID;
        int burstRemaining=0,burstDelay=0,burstOwner=-1;
        EID burstTarget=INVALID_EID;
        bool burstAir=false;
        void cancelBurst(){burstRemaining=burstDelay=0;burstOwner=-1;burstTarget=INVALID_EID;burstAir=false;}
        EngineerIntent engineerIntent=EngineerIntent::None;
        EID feedbackEngineerTarget=INVALID_EID; // presentation provenance, not saved or hashed
        EID feedbackServiceTarget=INVALID_EID; // presentation-only command provenance
        ServiceStatus feedbackServiceStatus=ServiceStatus::Accepted;
        EID feedbackEntryTarget=INVALID_EID; // presentation provenance, not saved or hashed
        EntryKind feedbackEntryKind=EntryKind::Board;
        struct FailedPursuit {
            EID target = INVALID_EID;
            int sourceX=0, sourceY=0, targetX=0, targetY=0;
            uint64_t retryTick=0;
        };
        std::vector<FailedPursuit> failedPursuits; // at most 8; simulation state, saved and hashed
        EID feedbackAttackTarget = INVALID_EID; // presentation-only command provenance; neither saved nor hashed
        float goalX = 0, goalY = 0; // 期望目的地
        struct AttackMoveOrder {
            bool active=false;
            int owner=-1;
            float x=0,y=0;
            uint8_t layer=0;
            uint64_t retryAt=0;
            bool operator==(const AttackMoveOrder&) const=default;
        } attackMove;
        struct Waypoint {
            float first=0,second=0;int8_t layer=-1;
            int8_t attackMode=-1; // -1 preserves the historical inherited mode.
            Waypoint()=default;
            Waypoint(float x,float y,int surface=-1,int mode=-1):first(x),second(y),layer((int8_t)surface),attackMode((int8_t)mode){}
            bool operator==(const Waypoint&) const=default;
            bool operator==(const std::pair<float,float>& other) const {return first==other.first && second==other.second;}
        };
        std::deque<Waypoint> wps; // Z queue, including requested surface (-1 automatic)
        // 采矿
        int oreLoad = 0;
        int gemLoad = 0;              // 载荷中宝石单位数（宝石价值为普通矿 2 倍）
        Vec2i oreCell{-1, -1};
        EID dockRefinery = INVALID_EID;
        int digTimer = 0;
        bool autoHarvest = true;      // Stop/Move 后为 false；仅显式采矿令/卸完自动再出发时恢复
        // 铁幕无敌剩余帧
        int invuln = 0;
        // 战机
        int ammo = 0;               // 当前弹药
        int rearmTimer = 0;         // 装填计时
        EID airbase = INVALID_EID;  // 所属空指部
        float orbitA = 0;           // 盘旋角
        // 建筑专属
        int rallyX = -1, rallyY = -1;
        int bldAnim = 0;
        int undeploy = 0;           // MCV 部署计时
        bool repairing = false;     // 建筑持续维修（RA2：按造价比例扣款回血）
        bool selling = false;       // 建筑出售中：倒放建造动画，结束后无爆炸移除
        bool guard = false;         // 警戒模式：按视野半径索敌
        // 运输载具：货舱（保存类型/生命/军衔，与驻军同结构）
        std::vector<GarrisonedUnit> cargo;
        int crateDmgBoost = 0;      // 箱子火力增益剩余帧
        int crateArmorBoost = 0;    // 箱子护甲增益剩余帧
        int crateSpeedBoost = 0;    // 箱子速度增益剩余帧
        // ---- 特殊单位机制 ----
        int chrono = 0;             // 超时空抹除进度：>0 冻结（每帧衰减），累积超阈值即抹除
        int64_t nativeTemporalHead=INVALID_EID; // derived from the saved source Temporal graph
        int tpSick = 0;             // 超时空传送后相位不适帧数（不能行动）
        bool camouflaged = false;   // 幻影坦克：静止伪装成树（敌方无法自动索敌）
        int camoTick = 0;           // 静止积累计时（达阈值进入伪装）
        bool radDeployed = false;   // 辐射工兵：已部署辐射区（不能移动，持续范围伤害）
        bool deployed = false;      // 重装大兵：已部署反装甲炮（不能移动，不可被碾压）
        int subReveal = 0;          // 台风潜艇：开火后暴露计时（>0 期间可被索敌）
        int kills = 0;              // 击杀数（仅统计）
        int veterancyValue = 0;     // 累计摧毁目标价值
        int vetRank = 0;            // 军衔：0 新兵 1 老兵 2 精英（伤害加成，精英自愈）
        // ---- RA2 补全：驻军/寄生/磁暴充电 ----
        std::vector<GarrisonedUnit> garrison; // 保存驻军类型、生命与军衔
        EID parasite = INVALID_EID;     // 宿主车辆：附着其上的恐怖机器人
        EID parasiteHost = INVALID_EID; // 恐怖机器人：当前寄生的宿主
        bool parasiting = false;        // 恐怖机器人：已附着宿主（隐藏、持续啃噬）
        int teslaCharge = 0;            // 磁暴线圈：正在为其充电的磁暴步兵数
        // ---- P6：心灵控制 ----
        EID mindBy = INVALID_EID;       // 被控单位：控制者（尤里/心灵突击队）实体
        EID mindTarget = INVALID_EID;   // 控制者：当前被控单位（同一时刻仅一个，RA2 原作）
        std::vector<EID> mindTargets;    // 主脑坦克的多目标控制
        int origPlayer = -1;            // 被控单位：被控制前的原属玩家（控制解除时恢复）
        bool permaControlled = false;    // 心灵控制仪永久控制后免疫再次心控
        // ---- 尤复补全：YR 新单位特殊机制 ----
        int airstrikeCd = 0;            // 鲍里斯：米格空袭冷却（>0 期间不可再次呼叫）
        int confused = 0;               // 混乱无人机毒气：混乱剩余帧（>0 自相残杀）
        EID magneticBy = INVALID_EID;    // 磁电坦克举升来源
        int magneticHeight = 0;          // >0 时悬空且不可行动
        EID drainedBy = INVALID_EID;     // 飞碟悬停吸取/瘫痪来源
        int gatlingHeat = 0;
        int gatlingStage = 0;
        // 被炸/寄生时的掀起动画剩余帧（左右交替）
        int rockTilt = 0;
        bool deploying() const { return !isBuilding && utype==UnitType::SiegeChopper
            && state>=UState::DeployLanding && state<=UState::DeployTakeoff; }
        bool airborne() const {
            if(supportAir)return true;
            if(nativeJumpjet.motion.started && nativeAltitude<=nativeJumpjet.ground)return false;
            return !isBuilding && unitDef(*this).isAir() && state!=UState::Landed
                && !(utype==UnitType::SiegeChopper && deployed);
        }
        bool immobilized() const { return supportAir || chrono>0 || nativeTemporalHead!=INVALID_EID || tpSick>0 || magneticHeight>0 || magneticBy!=INVALID_EID; }
        float flightFraction() const {
            if(!airborne())return 0;
            const float remaining=std::clamp((float)deployAnim/std::max(1,g_gameRules.chopperLandingTicks),0.f,1.f);
            if(state==UState::DeployLanding)return remaining;
            if(state==UState::DeployTakeoff)return 1-remaining;
            return 1;
        }
    };

    // 补给箱（RA2 随机箱子）：地面单位驶入拾取
    struct Crate {
        static constexpr int KIND_COUNT=11;
        bool alive = true;
        int x = 0, y = 0;
        int kind = 0; // Prototype 0..10; NATIVE_CRATE_BASE + original powerup ID for authored native crates.
        int remainingTicks=0; // World55: randomized source-crate lifetime; zero for fixed/legacy crates.
    };

    // 疯狂伊文定时炸弹（谭雅 C4 复用：高伤害单点）
    struct TimedBomb {
        float x = 0, y = 0;
        int timer = 0;
        int player = 0;
        EID attachedTo = INVALID_EID; // 附着的实体（INVALID=地面）
        int dmg = 400;                // 中心伤害（谭雅 C4=6000）
        float radius = 2.5f;          // 溅射半径（谭雅 C4≈单点）
        bool rockVehicles = false;    // 基洛夫等：命中地面载具时掀起
    };

    // Ownership keeps creation identity, cancels the previous player's
    // retained intent and updates gate access. Action-specific control links
    // and batched house/power synchronization retain their lifecycle boundary.
    static bool hasAttackMoveState(const Ent& actor);
    void changeActorOwner(EID actor,int owner);
    void synchronizeBuildingOwners();

    // Appending entities during combat/scripts must preserve live entity references.
    // Iterators are not stable across append: mutating traversals use entity IDs.
    std::deque<Ent> ents;
    std::vector<int> freeList;
    std::vector<Projectile> projs;
    std::vector<Effect> effects;
    std::vector<Player> players;
    std::vector<Nuke> nukes;        // 飞行中的核弹
    std::vector<Crate> crates;      // 场上的补给箱
    std::vector<TimedBomb> timedBombs; // 疯狂伊文安放的炸弹
    int numPlayers = 0;
    uint64_t tick = 0;
    Rng rng{12345};

    // 遭遇战选项（由 Game 在开局时设置）
    SkirmishMode skirmishMode = SkirmishMode::Battle;
    bool cratesEnabled = true;       // 随机补给箱
    bool aiAlliance = false;         // AI 组成同一队伍（一致对外）
    bool sharedVision = false;       // 盟友共享已探索区与当前视野
    bool shortGame = false;          // 无建筑且无 MCV 即判负
    bool mcvRepacks = false;         // 建造厂可打包为 MCV（官方遭遇战选项）
    bool superweaponsEnabled = true; // 禁用时不充能、不可发射且不可建造超武建筑
    // 战役科技门：true 且白名单非空时，仅允许列表内建筑/单位（遭遇战保持 false）
    bool campaignTechGate = false;
    std::vector<BldType> campaignAllowedBlds;
    std::vector<UnitType> campaignAllowedUnits;

    // 敌对判定（考虑 AI 结盟）
    bool isEnemy(int a, int b) const {
        if (a == b || a < 0 || b < 0) return false;
        if(!nativeScenarioSource.empty() && !usesRulesProfile())return (nativeAllies[a]&(1u<<b))==0;
        if (aiAlliance && players[a].isAI && players[b].isAI) return false;
        return true;
    }
    MatchOutcome matchOutcome(int player) const {
        if (player < 0 || player >= numPlayers) return MatchOutcome::Running;
        if (!players[player].active || players[player].defeated) return MatchOutcome::Defeat;
        for (int other=0; other<numPlayers; ++other)
            if (players[other].active && !players[other].defeated && isEnemy(player,other))
                return MatchOutcome::Running;
        return MatchOutcome::Victory;
    }
    bool isAllied(int a, int b) const {
        return a == b || (a >= 0 && b >= 0 && !isEnemy(a, b));
    }
    // Source definition is serialized once. Definitions are derived immutable
    // caches, while actor identity and mission state survive slot reuse.
    std::string nativeScenarioSource;
    std::string rulesProfileId,rulesProfileName;
    uint16_t rulesCountryMask=0;
    std::array<std::string,(int)Country::COUNT> rulesCountryKeys;
    std::array<double,(int)Country::COUNT> rulesCountrySpeed{};
    bool usesRulesProfile() const{return !rulesProfileId.empty();}
    bool rulesCountryAllowed(Country country) const;
    bool sourceProductionModeAllows(const NativeActorDefinition& definition) const;
    std::string productionCountry(int player) const;
    int rulesTypeIndex(int player,bool unit,int base) const;
    int productionSelector(int player,bool unit,int type) const;
    int rulesTransformIndex(const Ent& actor) const;
    double movementSpeedMultiplier(int player) const;
    struct RulesSnapshot {std::string source;bool fixedAircraft=true,asw=true,flight=true,jumpjet=true,veterancy=true,support=true,delivery=true;};
    bool loadRulesProfile(const char* path);
    bool loadRulesSnapshot(const RulesSnapshot& snapshot);
    NativeProductionRules nativeProductionRules;
    NativeMovementRules nativeMovementRules;
    uint64_t nativeProductionSerial=0;
    bool nativeBridgesDestroyable=true;
    std::shared_ptr<const NativeEventDefinition> nativeEventDefinition;
    std::vector<NativeRocketDefinition> nativeRocketDefinitions;
    std::vector<NativeLauncherState> nativeLaunchers;
    std::vector<NativeRocketState> nativeRockets;
    enum class AircraftPhase : uint8_t { Docked,Preparing,Attacking,Returning,Reloading,Dead };
    struct AircraftNode {
        AircraftPhase phase=AircraftPhase::Docked;
        EntityRef aircraft;
        int cargo=-1,remaining=0;
    };
    struct AircraftSpawner {
        EntityRef owner,target;
        int player=-1,definition=-1,launchRemaining=0;
        int regenerateTicks=0,reloadTicks=0,launchDelayTicks=0;
        uint64_t pending=0;
        std::vector<AircraftNode> nodes;
    };
    bool aircraftSpawnersEnabled=true;
    bool aswEnabled=true; // World51: older worlds retain their original destroyer behavior.
    bool nativeFlightEnabled=true; // World52: preserve older horizontal flight on legacy loads.
    bool nativeJumpjetEnabled=true; // World53: preserve older Jumpjet behavior in previous snapshots.
    bool veterancyProgressEnabled=true,independentSupportEnabled=true; // World56; old snapshots retain their prior behavior.
    bool supportDeliveryEnabled=true; // World57; historical snapshots retain instantaneous support.
    struct SupportMission {
        EntityRef aircraft;
        SupportType type=SupportType::Paradrop;
        uint8_t phase=0; // approach, overfly, departure
        float x=0,y=0,exitX=0,exitY=0;
        uint64_t nextDrop=0;
        uint8_t passes=5; // Original approach counter, reset after a successful release.
    };
    std::vector<SupportMission> supportMissions;
    std::vector<NativeAircraftDefinition> nativeAircraftDefinitions;
    std::vector<AircraftSpawner> aircraftSpawners;
    bool initializeAircraftSpawner(EID actor);
    bool hasAircraftSpawner(EID actor) const;
    bool underwaterTarget(const Ent& target) const;
    bool aircraftTargets(EID owner,EID target) const;
    bool managedAircraft(EID actor) const;
    int dockedAircraft(EID actor,bool readyOnly=false) const;
    int aircraftCapacity(EID actor) const;
    bool launchAircraft(EID actor,EID target);
    void stopAircraft(EID actor);
    void updateAircraftSpawners();
    bool updateSpawnedAircraft(EID actor);
    void retireAircraftActor(EID actor);
    bool validateAircraftState() const;
    bool writeAircraftState(FILE* file) const;
    bool readAircraftState(FILE* file);
    void resetNativeRockets();
    int nativeLauncherDefinition(const Ent& actor) const;
    bool nativeRocketTargets(EID actor,EID target) const;
    void initializeNativeLauncher(EID actor);
    int nativeDockedSpawns(EID actor) const;
    void updateNativeLaunchers();
    bool launchNativeRocket(EID actor,EID target);
    void updateNativeRocket(EID actor);
    void retireNativeRocketActor(EID actor);
    bool writeNativeRocketState(FILE* file) const;
    bool readNativeRocketState(FILE* file,bool extendedWeapons=true);
    bool validateNativeRocketState() const;
    NativeEventState nativeEvents;
    NativeConditionState nativeConditions;
    void startNativeConditions();
    void updateNativeDiscovery(bool seed=false);
    void offerNativeDamage(EID victim,EID attacker,bool lethal);
    void recordNativeBuilt(int player,int nativeType);
    bool nativeExtendedCondition(const NativeEventCondition& event,int occurrence,EID actor,int trigger) const;
    void setNativeLocal(int index,bool value);
    uint32_t nativeSelectionIdentity(EID actor) const;
    void selectNativeActor(int player,EID actor,uint32_t identity);
    void updateNativeTeamTransport(int team);
    bool validateNativeConditionState() const;
    bool writeNativeConditionState(FILE* file) const;
    bool readNativeConditionState(FILE* file);
    uint32_t nativeConditionChecksum() const;
    NativeAIRules nativeAIRules;
    NativeAIState nativeAI;
    std::vector<std::vector<uint64_t>> nativeHouseInventory; // derived building-lifecycle cache
    std::vector<int> nativeTeamMemberCounts; // derived once per team pass, invalidated on roster changes
    void initializeNativeHouseControl();
    void updateNativeHouseControl();
    void rebuildNativeHouseInventory();
    EID nativeTeamLeader(const NativeTeamState& team) const;
    void nativeTeamFootJoined(int team,EID actor);
    void pruneNativeTeamFeet();
    void updateNativeTeamStrength(int team);
    NativeCrateRules nativeCrateRules;
    std::map<std::string,NativeCrateBonus> nativeCrateBonuses;
    bool nativeCrateType(int kind) const;
    bool createNativeCrate(int powerup,Vec2i point);
    bool pickupNativeCrate(Ent& actor,const Crate& crate);
    bool pickupSourceCrate(Ent& actor,const Crate& crate);
    bool createSourceRandomCrate();
    void updateSourceRandomCrates();
    float nativeCrateMultiplier(const NativeActorIdentity& identity,int kind) const;
    bool reshroudNativeWaypoint(int radius,Vec2i point);
    bool validateNativeCrateState(bool lifetimes=true) const;
    bool writeSourceCrateLifetimes(FILE* file) const;
    bool readSourceCrateLifetimes(FILE* file);
    bool writeNativeCrateState(FILE* file) const;
    bool readNativeCrateState(FILE* file);
    uint32_t nativeCrateChecksum() const;
    void startNativeAI();
    void updateNativeAIScheduler();
    void updateNativeAIProduction();
    bool nativeAITriggerEligible(int house,int trigger,bool defensiveOnly=false,bool skipDefense=false) const;
    bool nativeAITypeBuildable(int house,int type) const;
    void nativeAITeamCreated(int team);
    void nativeAITeamOutcome(int team);
    void recruitNativeAITeam(int team);
    void nativeAIJoined(int team,EID actor);
    void nativeAIClearFactoryExit(EID actor,EID factory);
    bool writeNativeAIState(FILE* file) const;
    bool readNativeAIState(FILE* file);
    bool validateNativeAIState() const;
    uint32_t nativeAIChecksum() const;
    bool configureNativeSkirmishAI(int house,int difficulty,bool enabled=true,int enemy=-2);
    bool nativeSkirmishAIActive(int house) const;
    bool nativeAIGlobalCondition(int house,int trigger) const;
    int nativeAITeamCount(int team,int type) const;
    void updateNativeSkirmishProduction();
    bool writeNativeAIGlobalState(FILE* file) const;
    bool readNativeAIGlobalState(FILE* file);
    bool validateNativeAIGlobalState() const;
    std::deque<NativeEventCue> nativeEventCues;
    std::vector<int> nativeEventDispatchTags; // transient reentrancy guard
    int nativeEventOperations=0;
    void resetNativeEvents();
    bool startNativeEvents(int difficulty=1);
    int nativeEventTagInstance(int definition,bool separate=false);
    void updateNativeEvents();
    void updateNativeTeams();
    void updateNativeTeamScript(int team);
    void updateNativeTeamControl(int team);
    void setNativeTeamScriptStep(int team,int step);
    void updateNativeTeamOrders(int team);
    void updateNativeTeamStaging(int team);
    void updateNativeTeamAttack(int team);
    EID nativeTeamAttackTarget(int team,int quarry) const;
    bool hasNativeGuns(const Ent& actor) const;
    bool hasNativeGatling(const Ent& actor) const;
    void updateNativeGatling(Ent& actor);
    bool hasNativeC4(const Ent& actor) const;
    bool nativeWeapon(const Ent& actor,const Ent* target,WeaponDef& weapon) const;
    bool hasNativeIFV(const Ent& actor) const;
    int nativeIFVMode(const Ent& actor) const;
    int nativeIFVTurret(const Ent& actor) const;
    bool nativeIFVWeapon(const Ent& actor,WeaponDef& weapon) const;
    void scaleNativeWeapon(const Ent& actor,const NativeGunProfile& profile,WeaponDef& weapon) const;
    bool nativeRepairMode(const Ent& actor) const;
    bool nativeRepairIntent(const std::vector<EID>& actors,EID target) const;
    void nativeIFVCrewChanged(EID actor);
    AttackRejectReason nativeRepairRejection(EID actor,EID target,const WeaponDef& weapon) const;
    void acquireNativeRepairTarget(Ent& actor,EID id);
    bool applyNativeRepair(const WeaponDef& weapon,int player,EID target,EID source);
    float nativeArmorScale(const Ent& actor) const;
    void attackTargetPoint(const Ent& actor,const Ent& target,float& x,float& y) const;
    float attackRange(const Ent& actor,const Ent& target) const;
    int nativeWeaponReference(const WeaponDef& weapon) const;
    bool restoreNativeWeaponReference(WeaponDef& weapon,int code,bool extended) const;
    void advanceNativeTeamScript(int team);
    void releaseNativePlayerUnits(const std::vector<int>& actors);
    void finishNativeTeam(int team);
    EID nativeWaypointObject(int cell) const;
    void offerNativeCellEntry(EID actor,int x,int y);
    void offerNativeTagEvent(int tag,int occurrence,EID actor=-1);
    void retireNativeEventActor(EID actor,bool destroyed=true);
    bool nativeEventCondition(const NativeEventCondition& condition,int occurrence,EID actor,int trigger) const;
    int nativeActionBlocker(const NativeEventAction& action,int tag) const;
    int prepareNativeActions(const std::vector<NativeEventAction>& actions,int tag,std::vector<std::vector<std::pair<int,Vec2i>>>& placements) const;
    bool executeNativeAction(const NativeEventAction& action,int trigger,const std::vector<std::pair<int,Vec2i>>* placements=nullptr);
    int nativeHouseNumber(int original) const;
    int nativeTriggerIndex(const std::string& id) const;
    int nativeTeamIndex(const std::string& id) const;
    int nativeUnitIndex(const std::string& id) const;
    int nativeTeamBlocker(int team) const;
    bool nativeTeamPlacements(int team,int waypoint,std::vector<std::pair<int,Vec2i>>& placements,const std::vector<uint8_t>* reserved=nullptr) const;
    int createNativeTeam(int team,bool reinforce,int waypoint=-1,const std::vector<std::pair<int,Vec2i>>* placements=nullptr);
    Vec2i nativeWaypoint(int id) const;
    void changeNativeHouse(int tag,int owner,bool all=false,int from=-1);
    bool writeNativeEventState(FILE* file) const;
    bool readNativeEventState(FILE* file,bool teamExecution=true,bool teamOrders=true,bool teamAttack=true,bool missionOutcome=true,bool teamControl=true);
    bool validateNativeEventState() const;
    bool nativeHouseEliminated(int originalHouse,int event) const;
    bool concludeNativeMission(bool win,int runtime);
    MatchOutcome nativeMissionOutcome(int player) const;
    bool validateNativeMissionOutcome() const;
    void rebuildNativeBridges(bool resolveLost=false,bool scriptedCollapse=false);
    bool nativeDemolitionAt(int waypoint);
    void collapseNativeBridgeAt(int x,int y);
    void initializeMovementSurface(Ent& actor);
    int movementLayerAt(const Ent& actor,int x,int y) const;
    int routeEndLayer(const Ent& actor,const std::vector<Vec2i>& route,int index=0) const;
    int actorSurfaceHeight(const Ent& actor) const;
    int movementStep(const Ent& actor,int ax,int ay,int layer,int bx,int by,bool corners=true) const;
    bool movementPassable(const Ent& actor,int x,int y,int layer,bool buildings=false) const;
    size_t movementSlotCount() const {return map.cells.size()*(map.hasBridgeDecks()?2:1);}
    size_t movementSlot(int x,int y,int layer) const {return (size_t)y*map.w+x+(layer==1?map.cells.size():0);}
    bool writeBridgeState(FILE* file) const;
    bool readBridgeState(FILE* file,bool current);
    uint8_t nativeGateAccess(const Ent& actor) const;
    void rebuildNativeGates();
    void updateNativeGates();
    bool waitingForNativeGate(const Ent& actor,Vec2i next) const;
    bool writeNativeGateState(FILE* file) const;
    bool readNativeGateState(FILE* file);
    struct NativeBridgeBlueprint {
        int type=0,x=0,y=0,height=0,frame=0,house=-1,facing=0,group=-1;
        std::string object,tag;
    };
    std::vector<NativeBridgeBlueprint> nativeBridgeBlueprints; // derived from the saved authored scene
    std::vector<std::vector<int>> nativeBridgeGroups;
    void resetNativeBridgeScenario();
    void rebuildNativeBridgeGroups();
    int bridgeForHut(EID hut) const;
    EID liveBridgeSegment(const NativeBridgeBlueprint& segment) const;
    bool bridgeNeedsRepair(int group) const;
    bool canRestoreBridge(int group) const;
    bool restoreBridge(int group);
    void updateBridgeEngineer(Ent& actor,EID id);
    void retireNativeScenery(const Ent& actor,bool scriptedCollapse=false);
    std::vector<std::shared_ptr<const NativeActorDefinition>> nativeTypes;
    std::shared_ptr<const NativeWeaponCatalog> nativeWeaponCatalog;
    struct NativeTemporalBeam {
        EntityRef owner,target;
        uint64_t passenger=0; // zero = physical owner; nonzero = durable passenger identity
        int64_t next=INVALID_EID,prev=INVALID_EID; // toward the head / away from the head
        int64_t remaining=0; // only the head owns the shared Strength*10 budget
    };
    int nativeSceneVersion=0; // immutable contract derived from the embedded source
    bool nativeTemporalEnabled=false; // derived from embedded SourceScene35
    std::map<int64_t,NativeTemporalBeam> nativeTemporalBeams;
    bool nativeTemporalWeapon(const WeaponDef& weapon) const;
    bool nativeTemporalCanTarget(EID source,EID target) const;
    bool startNativeTemporal(EID source,EID target,const WeaponDef& weapon);
    void stopNativeTemporal(int64_t source);
    bool bindNativeTemporal(int64_t key,EID carrier,uint64_t passenger,EID target,const WeaponDef& weapon);
    bool startNativeCrewTemporal(EID carrier,uint64_t passenger,EID target,const WeaponDef& weapon);
    void stopNativeCrewTemporal(EID carrier);
    static int64_t nativeCrewTemporalKey(uint64_t passenger) {return -1-(int64_t)passenger;}
    void retireNativeTemporalActor(EID actor);
    void updateNativeTemporalSource(EID source);
    void updateNativeTemporalTarget(EID target);
    int nativeTemporalDamage(int64_t source) const;
    bool nativeTemporalAbilityOnline(int player,uint32_t mask,bool support=false) const;
    std::vector<Effect> nativeTemporalVisuals() const;
    bool validateNativeTemporalState() const;
    bool writeNativeTemporalState(FILE* file) const;
    bool readNativeTemporalState(FILE* file);
    uint32_t nativeTemporalChecksum() const;
    NativeCrewRules nativeCrewRules;
    struct NativeCrewBurst {
        EntityRef carrier,target;
        int remaining=0,delay=0,player=-1;
        bool air=false;
    };
    std::map<uint64_t,NativeCrewBurst> nativeCrewBursts;
    uint64_t nextCombatActor=1;
    uint64_t allocateCombatActor();
    void initializeCrewIdentities(Ent& actor);
    void retireCombatIdentity(EID actor);
    void consumeCrewPassenger(EID carrier,uint64_t identity);
    Ent passengerActor(const Ent& carrier,const Ent::GarrisonedUnit& passenger) const;
    WeaponDef nativeCrewWeapon(const Ent& carrier,const Ent::GarrisonedUnit& passenger,const Ent* target=nullptr) const;
    Ent::GarrisonedUnit* findCrewPassenger(EID carrier,uint64_t identity);
    const Ent::GarrisonedUnit* findCrewPassenger(EID carrier,uint64_t identity) const;
    bool nativeCrewFires(const Ent& carrier) const;
    void updateNativeCrew(EID carrier);
    std::vector<EID> nativeCrewCandidates(const Ent& carrier,const Ent::GarrisonedUnit& passenger) const;
    void creditCombatKill(uint64_t source,EID victim);
    bool validateNativeCrewState() const;
    bool writeNativeCrewState(FILE* file) const;
    bool readNativeCrewState(FILE* file);
    uint32_t nativeCrewChecksum() const;
    uint64_t nativeCellSerial=0;
    bool nativeCellOrderEnabled() const;
    void syncNativeCellOrder(Ent& actor);
    EID nativeCellFirstObject(int x,int y) const;
    bool validateNativeCellOrder() const;
    bool writeNativeCellOrder(FILE* file) const;
    bool readNativeCellOrder(FILE* file);
    uint8_t nativeAllies[MAX_PLAYERS]{};
    uint32_t nativeColors[MAX_PLAYERS]{}; // derived from the saved source color scheme
    bool loadNativeScene(const char* path);
    bool validateAttackMoveState() const;
    bool writeAttackMoveState(FILE* file) const;
    bool readAttackMoveState(FILE* file);
    uint32_t attackMoveChecksum() const;
    bool validateNativeMovementState() const;
    bool nativeGroundEnabled() const {return nativeProductionRules.version>=8;}
    bool nativeTravelEnabled() const {return nativeProductionRules.version>=9;}
    bool usesNativeGround(const Ent& e) const;
    bool harvesterUnloading(const Ent& e) const {return e.state==UState::HarvestUnload && (!usesNativeGround(e) || e.nativeDock.phase==NativeDockPhase::Unload);}
    uint16_t nativeHullHeading(const Ent& e,uint64_t atTick) const;
    uint16_t nativeTurretHeading(const Ent& e,uint64_t atTick) const;
    bool writeNativeMovementState(FILE* file) const;
    bool readNativeMovementState(FILE* file);
    uint32_t nativeMovementChecksum() const;
    bool validateNativeFlightState() const;
    bool writeNativeFlightState(FILE* file) const;
    bool readNativeFlightState(FILE* file);
    uint32_t nativeFlightChecksum() const;
    bool usesNativeJumpjet(const Ent& e) const;
    void initializeNativeJumpjet(Ent& e);
    bool nativeJumpjetToward(Ent& e,float x,float y);
    void updateNativeJumpjet(Ent& e,EID id);
    bool validateNativeJumpjetState() const;
    bool writeNativeJumpjetState(FILE* file) const;
    bool readNativeJumpjetState(FILE* file);
    uint32_t nativeJumpjetChecksum() const;
    bool usesNativeAirNavigation(const Ent& e) const;
    int nativeAirLandingLayer(int x,int y,EID actor) const;
    NativeAirProbeCell nativeAirProbeCell(int x,int y) const;
    bool findNativeAirLanding(Ent& e,EID actor);
    bool validateNativeAirNavigation() const;
    bool writeNativeAirNavigation(FILE* file) const;
    bool readNativeAirNavigation(FILE* file);
    uint32_t nativeAirNavigationChecksum() const;
    bool restoreNativeDefinitions(const std::string& source);
    bool initializeRulesMapStarts(std::vector<Vec2i>& starts);
    EID spawnNativeActor(int type,int player,float x,float y,int health,int facing,
                         const std::string& object,const std::string& tag,const std::string& mission);
    bool writeNativeActors(FILE* file) const;
    bool readNativeActors(FILE* file);
    bool modeAllowsBuilding(int player, BldType t) const;
    bool modeAllowsUnit(int player, UnitType t) const;
    bool campaignAllowsBuilding(BldType t) const;
    bool campaignAllowsUnit(UnitType t) const;
    void ensureMegawealthOilDerricks(int perPlayer = 2);

    // 建筑占格（cellIdx -> eid+1）
    std::vector<int> bldOcc;
    // 单位占格链表头（cellIdx -> 首个地面单位 EID）；同格串在 Ent::occNext
    std::vector<EID> unitOccHead;
    // 空中单位粗桶（4×4 格一桶，飞行中不进 unitOcc）；派生数据
    static constexpr int AIR_BUCKET = 4;
    int airBucketW = 0;
    std::vector<EID> airOccHead;

    // mapFile 非空时加载手工地图（maps/xxx.txt：地形+预置实体+出生点），失败回退程序生成；
    // noStartForce=true 时不刷初始基地车部队（全部由地图文件放置，仅手工地图有效）
    // requireMap=true rejects unavailable hand maps without generating a replacement.
    bool init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType = 0,
              const char* mapFile = nullptr, bool noStartForce = false, bool requireMap = false,const char* rulesFile = nullptr,
              const std::vector<Country>& countries = {},const RulesSnapshot* rulesSnapshot=nullptr);

    // 手工地图待放置实体（loadHandMap 解析产出，init 内统一放置）
    struct PendingEnt {
        bool isBld = false;
        int player = -1;     // -1 中立
        int typeIdx = 0;     // UnitType / BldType
        int x = 0, y = 0;
        bool guard = false;  // 单位警戒（AI 防守部队）
        int hpOverride = -1; // 变体 HP（>=0 时覆盖默认）
    };

    // 创建
    EID spawnUnit(int player, UnitType t, float x, float y,int sourceType=-1,const NativeActorIdentity* existingIdentity=nullptr);
    EID spawnBuilding(int player, BldType t, int bx, int by, bool free_ = false,int sourceType=-1,int initialHp=-1);
    void kill(EID id, bool explode = true,bool scriptedCollapse=false,bool erased=false); // erased skips physical death effects

    // 访问
    Ent& ent(EID id) { return ents[id]; }
    bool valid(EID id) const { return id >= 0 && id < (int)ents.size() && ents[id].alive; }
    EntityRef entityRef(EID id) const { return valid(id)?EntityRef{id,ents[id].generation}:EntityRef{}; }
    bool valid(EntityRef ref) const { return ref.generation && valid(ref.id) && ents[ref.id].generation==ref.generation; }
    bool writeEntityGenerations(FILE* file) const;
    bool readEntityGenerations(FILE* file);

    // 指令
    enum class MoveFormation { MapGrid, Directional };
    MoveOrderResult orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove, bool append = false,
                              bool feedback = false,int layer=-1,MoveFormation formation=MoveFormation::MapGrid);
    AttackOrderResult orderAttack(const std::vector<EID>& sel, EID target, bool feedback=false);
    AttackRejectReason attackRejection(EID attacker,EID target) const;
    void emitAttackResult(int player,EID target,const AttackOrderResult& result);
    std::vector<OrderActor> orderHarvest(const std::vector<EID>& sel, int x, int y, bool feedback=false);
    void orderReturnToRefinery(const std::vector<EID>& sel, EID preferredRef = INVALID_EID);
    void orderStop(const std::vector<EID>& sel,bool cancelDriveFacing=false);
    EID orderDeploy(EID id);                    // 基地车展开 / 建造厂打包；返回新实体
    DeployOrderResult orderDeployment(const std::vector<EID>& sel,bool modesOnly=false);
    void emitDeployResult(int player,const DeployOrderResult& result);
    void captureVisualTarget(PresentationEvent& event,const Ent& entity) const;
    EID packConYardToMcv(EID id);                // MCV Repacks：建造厂→基地车，返回新 EID
    EngineerOrderResult orderCapture(const std::vector<EID>& sel, EID bldId, bool feedback=false);
    EngineerOrderResult orderBridgeRepair(const std::vector<EID>& sel,EID hut,bool feedback=false);
    EngineerOrderResult orderRepair(const std::vector<EID>& sel, EID bldId, bool feedback=false);
    EngineerOrderResult orderEngineer(const std::vector<EID>& sel,EID target,EngineerIntent intent,bool feedback);
    EngineerRejectReason engineerRejection(EID unit,EID target,EngineerIntent intent) const;
    void emitEngineerResult(int player,EID target,EngineerIntent intent,const EngineerOrderResult& result);
    std::vector<int> orderScatter(const std::vector<EID>& sel); // X 散布
    void orderGuard(const std::vector<EID>& sel);   // G 警戒（视野索敌）
    EntryOrderResult orderBoard(const std::vector<EID>& sel, EID transportId,bool feedback=false);
    ReleaseOrderResult orderUnload(const std::vector<EID>& sel,bool feedback=false);
    EntryOrderResult orderGarrison(const std::vector<EID>& sel, EID bldId,bool feedback=false);
    static int cargoUnitCount(const std::vector<Ent::GarrisonedUnit>& cargo,UnitType type) {
        int count=0;for(const auto& p:cargo)count+=(p.type==type)+cargoUnitCount(p.cargo,type);return count;
    }
    static int cargoLoad(const std::vector<Ent::GarrisonedUnit>& cargo) {
        int load=0;for(const auto& passenger:cargo)load+=unitDef(passenger).transportSize();return load;
    }
    EntryRejectReason entryRejection(EID unit,EID target,EntryKind kind) const;
    int buildingEntryDomain(const Ent& building) const;
    void emitEntryResult(int player,EID target,EntryKind kind,const EntryOrderResult& result);
    ReleaseOrderResult orderUngarrison(const std::vector<EID>& sel,bool feedback=false);
    void emitReleaseResult(int player,EID source,ReleaseKind kind,const ReleaseOrderResult& result);
    void orderRadDeploy(const std::vector<EID>& sel); // 辐射工兵：部署/收起辐射区（D）
    void orderParadrop(int player, float x, float y);  // 伞兵空投（美国空指部/科技机场）
    void orderSpyPlane(int player, float x, float y);  // YR 侦察机揭雾
    void orderPsychicReveal(int player, float x, float y); // YR 心灵揭示揭雾
    static bool serviceFacility(BldType type) { return type==BldType::ServiceDepot || type==BldType::TechOutpost; }
    ServiceRejectReason serviceRejection(EID unit,EID depot) const;
    ServiceOrderResult orderService(const std::vector<EID>& sel, EID depotId,bool feedback=false); // 车辆开往维修厂（维修+摘除寄生）

    // ---- P8 联机：玩家命令抽象（lockstep 同步的最小单元） ----
    // 双端确定性前提：同种子初始化 + 相同命令序列（同 tick 序、同内容）驱动 update
    struct Cmd {
        enum Type : uint8_t {
            None = 0,
            Move, Attack, Harvest, ReturnHarvester, Stop, Deploy, Capture, Repair, Scatter, Guard,
            Board, Unload, Garrison, Ungarrison, RadDeploy, Paradrop, Service,
            StartUnitProd, CancelUnitProd, StartBldProd, CancelBldProd,
            HoldUnitProd, HoldBldProd, // a=typeIdx；b=1 暂停 / 0 继续（建筑/防御按类型分流队列）
            PlaceBuilding, SetRally, SellBuilding, RepairBuilding, LaunchSW, NativeSelect,
        };
        Type type = None;
        std::vector<EID> ids; // 选择集（可空）
        float x = 0, y = 0;   // 目标点（地图格坐标）
        int a = 0, b = 0;     // 参数：type 索引 / 目标 EID / (bx,by)；Move 专用：a&1=追加路径点
        // PlaceBuilding: a=building type; b=UI receipt token, ignored by simulation.
        bool attackMove = false; // Move 用：A 键攻击移动
        // Immediate simulation callers may submit raw IDs. Every deferred or
        // external command must capture identities at submission, never execution.
        bool identitiesBound=false;
        std::vector<uint64_t> generations;
        uint64_t targetGeneration=0;
    };
    static bool commandTargetsEntity(const Cmd& command);
    void bindCommand(Cmd& command) const;
    void applyCmd(int player, const Cmd& c); // 执行一条玩家命令（联机双端对称调用）
    void applyPlacementCommand(int player,int type,float x,float y,int request=0);
    uint32_t checksum() const;               // 世界状态校验和（lockstep 反不同步检测）

    // EVA 播报事件（Game 层消费：字幕+提示音；player = 接收方）
    struct EvaEvent { int player; std::string text; EvaCue cue=EvaCue::None; }; // cue is transient presentation metadata
    std::deque<EvaEvent> evaQueue;
    void eva(int player, const std::string& text, EvaCue cue=EvaCue::None);
    void evaAll(const std::string& text, EvaCue cue=EvaCue::None);

    // 生产
    bool startUnitProd(int player, UnitType t);   // 队列空→开工；否则排入队尾（每类最多 30）
    Credits reservedProductionFunds(int player) const;
    Credits changeMoney(int player,Credits delta); // Protected refunds/transfers or explicit grants; returns actual delta.
    Credits creditIncome(int player,Credits amount); // New income soft cap; never reduces an existing balance.
    Credits transferMoney(int from,int to,Credits amount);
    ProductionRejectReason productionStartRejection(int player,bool unit,int type) const;
    void applyProductionCommand(int player,const Cmd& command);
    void cancelUnitProd(int player, UnitType t);  // 取消一个该类型（先排队项后进行中项，返还资金）
    void holdUnitProd(int player, UnitType t, bool hold); // RA2 HOLD
    void holdBldProd(int player, BldType t, bool hold);   // 按建筑/防御队列分别 HOLD
    int unitQueuedCount(int player, int cat) const; // 该类别排队总数（含进行中）
    bool startBldProd(int player, BldType t);
    void cancelProd(int player, bool isUnit); // 兼容：取消建筑队列（无类型时取活跃项）
    void cancelBldProd(int player, BldType t); // 取消指定类型所在队列（建筑或防御）
    static int harvesterCapacity(UnitType t); // HARV/WarMiner=40，CMIN=20
    bool canPlace(BldType t, int bx, int by, int player) const;
    // MCV→建造厂：脚印须无其他单位/建筑；ignore 自身所占格
    bool canDeployMcv(EID id) const;
    bool canDeployMcvAt(EID id,int x,int y) const; // prospective center cell, same source foundation checks
    bool placeBuilding(int player, BldType t, int bx, int by); // 消耗就绪的生产项
    ProdItem& bldQueueFor(int player, BldType t);
    const ProdItem& bldQueueFor(int player, BldType t) const;
    void setRally(EID factory, int x, int y);
    RallyOrderResult orderRally(EID factory,float x,float y,bool clear=false);
    void emitRallyResult(int player,const RallyOrderResult& result,float x,float y);
    void sellBuilding(EID id);
    bool repairBuilding(EID id); // 切换持续维修；不可修返回 false
    int unitProductionCost(int player, UnitType t) const; // 含工业工厂车辆造价减免
    int buildingProductionCost(int player,BldType type) const;
    int nativeProductionCost(int player,int type) const;
    Credits grindingValue(const Ent& unit) const;
    void syncSuperweaponSources();

    // 超武
    bool launchSW(int player, SWType t, float tx, float ty); // 释放（扣充能）
    bool launchChronoShift(int player, const std::vector<EID>& sel, float tx, float ty, int* transferred=nullptr); // 人类两阶段来源/目标流程
    void updateSW();                                        // 充能与激活效果
    SuperweaponRejectReason superweaponRejection(int player,int type,float x,float y) const;
    bool chronoSourceEligible(int player,EID id) const;
    bool swAvailable(int player, SWType t,bool allowFrozen=false) const; // allowFrozen retains suspended charge state
    int chronoShiftUnits(const std::vector<EID>& sel, float tx, float ty); // 超时空传送选中车辆

    // 单位可见性（潜艇隐身等）：viewer 能否看见该实体
    bool visibleTo(const Ent& e, int viewer) const;
    bool isDetector(UnitType t) const; // 反潜探测单位（驱逐舰/神盾/海蝎/海豚）
    // 有效武器：部署形态 / IFV 载兵 / 精英军衔 综合（RA2 原作：状态与军衔改变武器）
    WeaponDef effWeapon(const Ent& e,const Ent* target=nullptr) const;

    // 伞兵充能就绪所需帧数（RA2 原作约 4 分钟一波）
    static constexpr int PARADROP_TIME = 30 * 60 * 4;
    // 伞兵来源：美国空指部 或 已占领的科技机场
    bool hasParadropSource(int player) const;
    bool hasNativeSupportSource(int player,unsigned capability) const;
    // YR 支援：苏/中雷达→侦察机；尤里心灵传感器→心灵揭示（与伞兵共用充能字段）
    bool hasSpyPlaneSource(int player) const;
    bool hasPsychicRevealSource(int player) const;
    bool hasSupportSource(int player,SupportType type) const;
    const SupportRule& supportRule(SupportType type) const;
    bool supportReady(int player,SupportType type) const;
    int supportCharge(int player,SupportType type) const;
    SupportType preferredParadrop(int player) const;
    SuperweaponRejectReason supportRejection(int player,SupportType type,float x,float y) const;
    bool orderSupport(int player,SupportType type,float x,float y);
    void updateSupportPowers();
    void syncSupportSources();
    bool validateSupportState() const;
    bool writeSupportState(FILE* file) const;
    bool readSupportState(FILE* file);
    bool launchSupportMission(int player,SupportType type,float x,float y);
    void updateSupportAircraft(EID actor);
    void updateSupportDescent(EID actor);
    void retireSupportActor(EID actor);
    const SupportDeliveryRules& supportDeliveryRules() const;
    const NativeFlightProfile& fixedWingProfile(const Ent& actor) const;
    bool supportLandingCell(int x,int y,int& layer,EID except=INVALID_EID) const;
    bool validateSupportDelivery() const;
    bool writeSupportDelivery(FILE* file) const;
    bool readSupportDelivery(FILE* file);
    uint32_t supportDeliveryChecksum() const;
    void initializeFactoryVeterancy(Ent& actor,bool veteran) const;

    // 查询
    bool hasBld(int player, BldType t) const;
    bool hasFactoryFor(int player, const UnitDef& u) const;
    std::vector<Vec2i> factoryExitCells(EID factory,const UnitDef& unit) const;
    static bool productionFacilityOperational(const Ent& building);
    ProductionFacilityState productionFacilities(int player,bool unit,int type) const;
    bool prereqMet(int player, const BldDef& d) const;
    bool unitPrereqMet(int player, const UnitDef& u) const;
    int countUnits(int player, UnitType t) const;
    int countBlds(int player, BldType t) const;
    EID findNearestEnemy(int player, float x, float y, float maxR, bool includeBlds = true, const WeaponDef* w = nullptr,
                         UnitType seeker = UnitType::COUNT, EID seekerId = INVALID_EID, bool firingOnly = false);
    bool activePursuitFailure(const Ent& seeker, const Ent::FailedPursuit& failure) const;
    void rememberPursuitFailure(EID seeker, EID target);
    EID bldAt(int bx, int by) const;
    EID unitAtCell(int x, int y,int layer=-1) const;
    Vec2i nativeNearbyPoint(Vec2i requested,int speed,int footprint=1) const;
    int nativeNearbySpeed(EID actor) const;
    bool groundUnitBlocksCell(int x, int y, EID ignore = INVALID_EID,int layer=-1) const;
    // 移动占位：车/舰与其他地面单位互斥，步兵同格≤3；合法碾压在入格前结算。
    bool cellHardBlockedForMove(int x, int y, EID mover,int layer=-1) const;
    int countInfantryAtCell(int x, int y, EID ignore = INVALID_EID,int layer=-1) const;
    void unitOccSync(EID id);      // 按当前 x/y/状态刷新占位链表（O(同格人数)）
    void rebuildUnitOcc();         // 全量重建（init/load 后）
    void airOccSync(EID id);       // 飞行单位粗桶刷新
    void rebuildAirOcc();          // 全量重建空中桶（init/load 后）

    // 主更新（逻辑帧）
    void update();

    // 存档/读档（二进制序列化整个模拟状态，追加到已打开的文件；Game 层负责文件头）
    bool saveGame(FILE* f) const;
    bool loadGame(FILE* f);

    // 迷雾：以 player 视角重新计算可见
    void updateFog(int player);

    // 伤害（byEnt 为攻击者实体，用于军衔经验；可为 INVALID）
    void applyCellSpreadDamage(const WeaponDef& weapon,int player,float x,float y,EID source,int sourceSlot=-1,bool chaosGas=false,uint64_t sourceActor=0);
    void damage(EID id, int dmg, int byPlayer, EID byEnt = INVALID_EID, int byGarrisonSlot = -1,bool forced=false,uint64_t sourceActor=0);

    int cellIdx(int x, int y) const { return y * map.w + x; }
    bool bldBlocked(int x, int y,int layer=0) const;
    // 按寻路域判断格可通行性：0 陆地 1 水面 2 两栖（不含单位占用）
    bool passableFor(int x, int y, int domain,int layer=-1) const {
        if(map.hasBridgeDecks())return map.surfacePassable(x,y,domain,layer<0?map.preferredLayer(x,y,domain):layer,false);
        if (!map.inBounds(x, y)) return false;
        const Cell& c = map.at(x, y);
        if(c.blocked)return false;
        if (domain == 1) return c.terrain == Terrain::Water;
        if (domain == 2) return c.passable() || c.terrain == Terrain::Water;
        return c.passable();
    }
    // 从 (fx,fy) 迈入 (x,y)：地形可走且爬升合法
    bool passableStep(int fx, int fy, int x, int y, int domain,int layer=-1) const {
        if(map.hasBridgeDecks())return map.stepLayer(fx,fy,layer<0?map.preferredLayer(fx,fy,domain):layer,x,y,domain)>=0;
        return passableFor(x, y, domain) && map.climbOk(fx, fy, x, y, domain);
    }

private:
    struct TrafficGate {
        int domain=0,anchor=0,exit=-1;
        uint64_t until=0;
        bool draining=false,touched=false; // touched is derived, all other fields affect simulation
    };
    std::vector<TrafficGate> trafficGates;
    struct MovementCorridor {
        std::vector<int> cells,ports,portGroups;
        int exit=-1;bool initialized=false,opposed=false,occupied=false;
    };
    std::vector<int> movementCorridorLabels[3];
    std::vector<MovementCorridor> movementCorridors[3];
    void resetMovementCorridors();
    bool narrowMovementCell(int x,int y,int domain,int layer=-1,bool underBridge=true) const;
    int movementCorridor(int cell,int domain);
    bool admitMovementCorridor(Ent& unit,EID id);
    void releaseReversingLane(EID id);
    enum class IdleYieldResult { NotApplicable, Started, NoPullout, BudgetWait };
    IdleYieldResult yieldIdleCorridor(EID requester,EID blocker);
    bool yieldMovingConflict(EID requester,EID blocker,bool admissionBlocked=false);
    bool updateInProgress = false;
    int entityMutationDepth = 0; // whole-command callback quarantine; never persisted
    int areaDamageDepth = 0; // transient callback quarantine; zero at snapshot boundaries
    std::vector<EID> retiredDuringUpdate; // empty at every save/replay boundary
    void retireEntitySlot(EID id);
    void updateService(Ent& e,EID id);
    void endService(Ent& e,EID id,ServiceRejectReason reason);
    void emitServiceResult(int player,EID depot,const ServiceOrderResult& result,ServiceStatus status=ServiceStatus::Accepted);
    float serviceReach(const Ent& depot) const;
    bool interactionPath(const Ent& unit,const Ent& target,float reach,std::vector<Vec2i>& path) const;
    bool interactionWithinReach(const Ent& unit,const Ent& target,float reach) const;
    void updateUnit(Ent& e, EID id);
    bool updateMagnetic(Ent& e,EID id);
    void updateAircraft(Ent& e, EID id);    // 战机状态机
    void updateChopperDeployment(Ent& e,EID id);
    bool flyToward(Ent& e, float tx, float ty); // 直线飞行，到达返回 true
    bool nativeFlyToward(Ent& e,float tx,float ty);
    bool airfieldHasSpace(EID base,EID actor=INVALID_EID) const;
    Vec2f airPadPos(const Ent& af, EID actor) const;
    void updateBuilding(Ent& e, EID id);
    void updateCombat(Ent& e, EID id);
    void updateHarvester(Ent& e, EID id);
    void beginHarvesterReturn(Ent& e, EID mover, EID preferredRef = INVALID_EID, bool avoidOccupied = false);
    void initializeNativeGround(Ent& e);
    void updateNativeGroundFacing(Ent& e);
    void desireNativeHull(Ent& e,uint16_t heading);
    void desireNativeTurret(Ent& e,uint16_t heading);
    bool nativeGroundAimAligned(const Ent& e,uint16_t heading) const;
    bool aimNativeGround(Ent& e,float x,float y);
    bool beginNativeHarvesterReturn(Ent& e,EID id,EID preferred,bool preferOpenApproach=false);
    void beginChronoReturn(Ent& e,EID id,EID preferred,bool directBerth=true);
    bool updateChronoReturn(Ent& e,EID id);
    bool chronoReceiverUsable(const Ent& e,EID receiver) const;
    bool chronoLandingAvailable(EID id,EID receiver,bool directBerth) const;
    void teleportHarvester(Ent& e,EID id,int x,int y,int layer);
    bool updateNativeDock(Ent& e,EID id);
    void finishHarvesterUnload(Ent& e);
    bool nativeDockAvailable(EID receiver,EID miner) const;
    bool nativeDockTerrainPassable(const Ent& miner,EID receiver) const;
    bool continuesNativeDock(const Ent& miner,EID receiver) const;
    bool ownsNativeDockRefinery(const Ent& miner,EntityRef receiver) const;
    bool requiresForwardDockSave() const;
    bool requiresChronoBerthSave() const;
    bool requiresChronoYieldSave() const;
    bool requiresUnloadStartupSave() const;
    bool requiresDepartureWaitSave() const;
    bool requiresDriveExitSave() const;
    bool validateNativeGroundState() const;
    bool writeNativeGroundState(FILE* file) const;
    bool readNativeGroundState(FILE* file,int saveRevision);
    bool validateNativeDockRedirect(const Ent& miner,bool allowApproach) const;
    bool writeNativeDockRedirects(FILE* file,uint32_t count,int saveRevision) const;
    bool readNativeDockRedirects(FILE* file,int saveRevision);
    uint32_t nativeGroundChecksum() const;
    bool usesDriveArc(const Ent& e) const;
    bool driveArcMatchesRoute(const Ent& e) const;
    bool validateChronoReturn(const Ent& e) const;
    NativeDriveArc planDriveArc(const Ent& e,Vec2i corner,int layer,EID id=INVALID_EID) const;
    bool advanceDriveArc(Ent& e,bool aimTurret);
    bool writeNativeTravelState(FILE* file) const;
    bool readNativeTravelState(FILE* file,int saveRevision);
    uint32_t nativeTravelChecksum() const;
    bool findNearestReachableOre(int sx, int sy, int maxR, Vec2i& out) const;
    void moveAlongPath(Ent& e, EID id,int nativeSegments=8);
    void prepareNativeMovement(Ent& e,int x,int y,int layer,Vec2i goal);
    float nativeMovementStep(Ent& e,int x,int y,int layer,Vec2i goal,float stopBefore=0);
    void tryUnstackIdle(Ent& e, EID id); // 静止叠格拆开（车互斥 / 步兵超叠）
    void fireWeapon(Ent& e, EID id, EID targetId, bool continuation=false);
    bool emitNativeLaser(int player,float sx,float sy,EID target,EID source,int sourceSlot,const WeaponDef& weapon,uint64_t sourceActor=0);
    bool emitNativeCellLaser(int player,float sx,float sy,EID target,EID source,int sourceSlot,const WeaponDef& weapon,uint64_t sourceActor=0);
    void updateWeaponBurst(EID id);
    // 按 Weapon/ProjectileDef 瞬时命中或生成飞行弹（驻军/战斗要塞与 fireWeapon 共用）
    void emitWeaponProjectile(int player, float sx, float sy, EID tgt, EID src, int srcGarrisonSlot, const WeaponDef& w,uint64_t sourceActor=0);
    void interceptProjectile(Projectile& projectile);
    void explodeAt(float x, float y, int big);
    bool spawnFromFactory(int player, const UnitDef& u,int nativeType=-1,EID factory=-1,EID* produced=nullptr);
public:
    bool nativeProductionType(int selector) const;
    bool validProductionType(bool unit,int selector) const;
    int nativeProductionCode(const std::string& id) const;
    int productionCategory(bool unit,int selector) const;
    ProdItem& productionItem(int player,bool unit,int selector);
    const ProdItem& productionItem(int player,bool unit,int selector) const;
    ProductionRejectReason nativeProductionRejection(int player,bool unit,int selector) const;
    bool nativeProductionVisible(int player,int selector) const;
    bool nativeFactorySupports(const Ent& factory,int type) const;
    bool canPlaceNativeBuilding(int player,int selector,int x,int y) const;
    bool writeNativeProductionState(FILE* file) const;
    bool readNativeProductionState(FILE* file);
    bool validateNativeProductionState() const;
private:
    void applyNativeProductionCommand(int player,const Cmd& command);
    void activateNativeProduction(int player,ProdItem& item,int selector);
    void updateNativeUnitProduction(int player,int category);
    void updateNativeBuildingProduction(int player,ProdItem& item);
    bool advanceNativeProduction(int player,ProdItem& item);
    int nativeProductionStepTicks(int player,int type) const;
    bool placeNativeBuilding(int player,int selector,int x,int y);
    EID spawnProductionActor(int player,UnitType type,float x,float y,int nativeType);
    void applyFactoryRally(EID unit,EID factory,int x,int y);
    void recomputePower(bool syncAbilities=true);
    void checkDefeat();
    bool stepTo(Ent& e, EID id, int nx, int ny);
    float entryReach(const Ent& target) const;
    bool entryPath(const Ent& unit,const Ent& target,std::vector<Vec2i>& path) const;
    EntryOrderResult orderEntry(const std::vector<EID>& sel,EID target,EntryKind kind,bool feedback);
    void endEntry(Ent& unit,EID id,EntryRejectReason reason);
    bool buildingApproachGoal(const Ent& b, int domain, int& gx, int& gy) const; // 靠近建筑占地外缘的最近可走格
    bool nearBuildingFootprint(float x, float y, const Ent& b, float pad = 1.75f) const; // 是否贴近建筑占地（占领/停靠）
    bool insideBuildingFootprint(float x, float y, const Ent& b) const; // 是否已走进建筑占地（工程师进入）
    ReleaseOrderResult evacuateGarrison(EID bldId);
    struct ReleaseCell { int x=0,y=0,layer=0; };
    std::vector<ReleaseCell> releaseCells(const Ent& source,const UnitDef& passenger,int radius) const;
    ReleaseOrderResult releaseContainer(EID source,ReleaseKind kind);
    ReleaseOrderResult orderRelease(const std::vector<EID>& sel,ReleaseKind kind,bool feedback);
    bool chronoJump(EID id, float gx, float gy); // 超时空传送：瞬移至目标点附近空格，按距离产生相位不适
    void placeNeutralTechs();                   // 地图生成后放置中立科技建筑（油井/医院/机械店/科技机场/秘密实验室/民房）
    bool loadHandMap(const char* path, int numPlayers, std::vector<Vec2i>& spawns,
                     std::vector<PendingEnt>& out); // P7 手工地图：解析地形/实体/出生点；失败返回 false
    void applySpyEffect(Ent& spy, Ent& bld, EID spyId); // 间谍渗透建筑效果
    void applyNativeSpyEffect(Ent& spy,Ent& building,EID spyId);
    void creditKill(EID byEnt, EID victim, int garrisonSlot = -1); // 按目标价值累计军衔经验
    void spawnCrateTick();                      // 周期性生成补给箱
    void pickupCrates(Ent& e);                  // 地面单位拾取补给箱
    void regrowOre();                           // 矿脉缓慢再生（RA2 矿钻等效）
    void updateTimedBombs();                    // 疯狂伊文炸弹倒计时与引爆
    void garrisonFire(Ent& b, EID id);          // 驻军轮流出击（民房/战斗碉堡/坦克碉堡）
    void applyGapShroud();                      // 裂缝产生器：敌军在黑幕半径内的迷雾降为不可见
    void updateParadrop();                      // 伞兵充能（美国空指部/科技机场）
    void updatePsychicAreaDevices();            // 战役心灵信标/放大器范围心控
    // ---- P6：心灵控制 ----
    void mindControlTake(Ent& yuri, EID yid, EID tid); // 夺取目标控制权（先释放旧目标）
    void mindControlRelease(Ent& yuri);                // 释放当前控制（目标恢复原属）
    void psychicAreaTake(Ent& beacon, EID bid, EID tid);
    void psychicAreaReleaseAll(Ent& beacon, EID bid);
    void applyCaptureEffect(Ent& b, int newOwner); // 工程师占领建筑的特殊效果（科技机场/秘密实验室）
    EID allocEnt();
    void invalidateEntityReferences(EID id);
    void consumeEntity(EID id); // remove spatial links before making a slot reusable
    void rememberAttackMove(EID id,bool attack);
    void completeMoveLeg(EID id,bool attack);
    void updateAttackMoveOrders();
};
