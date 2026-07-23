#pragma once
#include "game/data.h"
#include "game/map.h"
#include <vector>
#include <deque>
#include <string>
#include <cstdio>

// 实体 ID 类型（索引）
using EID = int;
constexpr EID INVALID_EID = -1;

// 单位状态
enum class UState : uint8_t { Idle, Moving, AttackMoving, Chasing, Attacking, HarvestGo, HarvestDig, HarvestReturn, HarvestUnload,
                              Circling, Returning, Landed, Boarding }; // Circling/Returning/Landed 为战机状态；Boarding 步兵登船

//  projectile 类型
enum class ProjKind : uint8_t { Bullet, Shell, Flak, Missile };

struct Projectile {
    bool alive = true;
    ProjKind kind;
    int player;
    float x, y;         // 瓦片浮点坐标
    float tx, ty;       // 目标点
    EID target;         // 追踪目标（可 INVALID）
    EID src = INVALID_EID; // 发射者（军衔经验归属）
    WeaponDef w;
    int speed;          // 每逻辑帧移动比例
    int trail = 0;
};

struct Effect {
    bool alive = true;
    int kind;           // 0 爆炸 1 烟雾 2 磁暴电弧 3 光棱光束 4 建筑爆炸
    float x, y;
    float x2, y2;       // 光束终点
    int age = 0;
    int maxAge;
};

// 生产项
struct ProdItem {
    bool active = false;
    bool isUnit;
    int typeIdx;        // UnitType 或 BldType
    int progress;       // 0..buildTime
    bool ready = false; // 建筑已就绪待放置
};

// 单位生产队列类别数（步兵/车辆/空军/海军）
constexpr int PROD_CAT_N = 4;

struct Player {
    bool active = false;
    bool isAI = false;
    bool defeated = false;
    Faction faction = Faction::Allies;
    int colorId = 0;
    int money = 10000;
    std::string name;

    ProdItem bldProd;                     // 建筑生产（单队列）
    ProdItem unitProd[PROD_CAT_N];        // 单位生产：按类别独立队列（RA2 原作设定）
    std::deque<int> unitQueue[PROD_CAT_N]; // 各类别排队待产的类型索引（队首为当前项的后续）
    BldType placingBld = BldType::COUNT; // 待放置建筑（人类玩家用）

    // 统计缓存（每帧重算）
    int powerMade = 0, powerUsed = 0;
    int powerSabotage = 0;   // 间谍破坏电厂：>0 期间强制低电
    int revealTimer = 0;     // 间谍渗透雷达：>0 期间全图可见
    bool vetCat[PROD_CAT_N] = {}; // 间谍渗透工厂：对应类别新造单位直接 1 级军衔
    int aiDifficulty = 1;    // AI 难度 0 简单 1 普通 2 困难（仅 AI 玩家）
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
    Map map;

    struct Ent {
        bool alive = false;
        bool isBuilding = false;
        int player = -1;
        UnitType utype = UnitType::GI;
        BldType btype = BldType::ConYard;
        float x = 0, y = 0;        // 单位：浮点瓦片坐标；建筑：左上格
        int dir = 2;
        int turretDir = 2;
        int hp = 1;
        // 移动
        std::vector<Vec2i> path;
        int pathIdx = 0;
        int moveTick = 0;
        int blockTick = 0;          // 路径被堵计时（超时放弃路径）
        int walkFrame = 0, walkAnim = 0;
        // 战斗
        UState state = UState::Idle;
        int atkCd = 0;
        EID target = INVALID_EID;
        float goalX = 0, goalY = 0; // 期望目的地
        // 采矿
        int oreLoad = 0;
        Vec2i oreCell{-1, -1};
        EID dockRefinery = INVALID_EID;
        int digTimer = 0;
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
        bool guard = false;         // 警戒模式：按视野半径索敌
        // 运输载具：已装载的单位类型（货舱）
        std::vector<UnitType> cargo;
        // ---- 特殊单位机制 ----
        int chrono = 0;             // 超时空抹除进度：>0 冻结（每帧衰减），累积超阈值即抹除
        int tpSick = 0;             // 超时空传送后相位不适帧数（不能行动）
        bool camouflaged = false;   // 幻影坦克：静止伪装成树（敌方无法自动索敌）
        int camoTick = 0;           // 静止积累计时（达阈值进入伪装）
        bool radDeployed = false;   // 辐射工兵：已部署辐射区（不能移动，持续范围伤害）
        bool deployed = false;      // 重装大兵：已部署反装甲炮（不能移动，不可被碾压）
        int subReveal = 0;          // 台风潜艇：开火后暴露计时（>0 期间可被索敌）
        int kills = 0;              // 击杀数（军衔经验）
        int vetRank = 0;            // 军衔：0 新兵 1 老兵 2 精英（伤害加成，精英自愈）
    };

    // 补给箱（RA2 随机箱子）：地面单位驶入拾取
    struct Crate {
        bool alive = true;
        int x = 0, y = 0;
        int kind = 0; // 0 资金 1 治疗 2 升阶
    };

    // 疯狂伊文定时炸弹
    struct TimedBomb {
        float x = 0, y = 0;
        int timer = 0;
        int player = 0;
        EID attachedTo = INVALID_EID; // 附着的实体（INVALID=地面）
    };

    std::vector<Ent> ents;
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
    bool cratesEnabled = true;  // 随机补给箱
    bool aiAlliance = false;    // AI 互相结盟（一致对外）

    // 敌对判定（考虑 AI 结盟）
    bool isEnemy(int a, int b) const {
        if (a == b || a < 0 || b < 0) return false;
        if (aiAlliance && players[a].isAI && players[b].isAI) return false;
        return true;
    }

    // 建筑占格（cellIdx -> eid+1）
    std::vector<int> bldOcc;

    void init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType = 0);

    // 创建
    EID spawnUnit(int player, UnitType t, float x, float y);
    EID spawnBuilding(int player, BldType t, int bx, int by, bool free_ = false);
    void kill(EID id);

    // 访问
    Ent& ent(EID id) { return ents[id]; }
    bool valid(EID id) const { return id >= 0 && id < (int)ents.size() && ents[id].alive; }

    // 指令
    void orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove);
    void orderAttack(const std::vector<EID>& sel, EID target);
    void orderHarvest(const std::vector<EID>& sel, int x, int y);
    void orderStop(const std::vector<EID>& sel);
    void orderDeploy(EID id);                    // 基地车展开
    void orderCapture(const std::vector<EID>& sel, EID bldId);
    void orderRepair(const std::vector<EID>& sel, EID bldId);   // 工程师修复己方受损建筑
    void orderScatter(const std::vector<EID>& sel); // X 散布
    void orderGuard(const std::vector<EID>& sel);   // G 警戒（视野索敌）
    void orderBoard(const std::vector<EID>& sel, EID transportId); // 步兵登上运输载具
    void orderUnload(const std::vector<EID>& sel);  // 运输载具卸下乘员（U）
    void orderRadDeploy(const std::vector<EID>& sel); // 辐射工兵：部署/收起辐射区（D）

    // EVA 播报事件（Game 层消费：字幕+提示音；player = 接收方）
    struct EvaEvent { int player; std::string text; };
    std::deque<EvaEvent> evaQueue;
    void eva(int player, const std::string& text);
    void evaAll(const std::string& text);

    // 生产
    bool startUnitProd(int player, UnitType t);   // 队列空→开工；否则排入队尾（每类最多 8 个）
    void cancelUnitProd(int player, UnitType t);  // 取消一个该类型（先排队项后进行中项，返还资金）
    int unitQueuedCount(int player, int cat) const; // 该类别排队总数（含进行中）
    bool startBldProd(int player, BldType t);
    void cancelProd(int player, bool isUnit);
    bool canPlace(BldType t, int bx, int by, int player) const;
    bool placeBuilding(int player, BldType t, int bx, int by); // 消耗就绪的生产项
    void setRally(EID factory, int x, int y);
    void sellBuilding(EID id);
    bool repairBuilding(EID id); // 花费=缺失HP占造价一半，立即修满；不可修返回false

    // 超武
    bool launchSW(int player, SWType t, float tx, float ty); // 释放（扣充能）
    void updateSW();                                        // 充能与激活效果
    bool swAvailable(int player, SWType t) const;           // 有对应建筑且未战败
    void chronoShiftUnits(const std::vector<EID>& sel, float tx, float ty); // 超时空传送选中车辆

    // 单位可见性（潜艇隐身等）：viewer 能否看见该实体
    bool visibleTo(const Ent& e, int viewer) const;
    bool isDetector(UnitType t) const; // 反潜探测单位（驱逐舰/神盾/海蝎）

    // 查询
    bool hasBld(int player, BldType t) const;
    bool hasFactoryFor(int player, const UnitDef& u) const;
    bool prereqMet(int player, const BldDef& d) const;
    bool unitPrereqMet(int player, const UnitDef& u) const;
    int countUnits(int player, UnitType t) const;
    int countBlds(int player, BldType t) const;
    EID findNearestEnemy(int player, float x, float y, float maxR, bool includeBlds = true, const WeaponDef* w = nullptr,
                         UnitType seeker = UnitType::COUNT);
    EID bldAt(int bx, int by) const;
    EID unitAtCell(int x, int y) const;

    // 主更新（逻辑帧）
    void update();

    // 存档/读档（二进制序列化整个模拟状态，追加到已打开的文件；Game 层负责文件头）
    bool saveGame(FILE* f) const;
    bool loadGame(FILE* f);

    // 迷雾：以 player 视角重新计算可见
    void updateFog(int player);

    // 伤害（byEnt 为攻击者实体，用于军衔经验；可为 INVALID）
    void damage(EID id, int dmg, int byPlayer, EID byEnt = INVALID_EID);

    int cellIdx(int x, int y) const { return y * map.w + x; }
    bool bldBlocked(int x, int y) const;
    // 按寻路域判断格可通行性：0 陆地 1 水面 2 两栖（不含单位占用）
    bool passableFor(int x, int y, int domain) const {
        if (!map.inBounds(x, y)) return false;
        const Cell& c = map.at(x, y);
        if (domain == 1) return c.terrain == Terrain::Water;
        if (domain == 2) return c.passable() || c.terrain == Terrain::Water;
        return c.passable();
    }

private:
    void updateUnit(Ent& e, EID id);
    void updateAircraft(Ent& e, EID id);    // 战机状态机
    bool flyToward(Ent& e, float tx, float ty); // 直线飞行，到达返回 true
    Vec2f airPadPos(const Ent& af, int slot) const; // 停机位（空指部中心 2x2 分布）
    void updateBuilding(Ent& e, EID id);
    void updateCombat(Ent& e, EID id);
    void updateHarvester(Ent& e, EID id);
    void moveAlongPath(Ent& e, EID id);
    void fireWeapon(Ent& e, EID id, EID targetId);
    void explodeAt(float x, float y, int big);
    void spawnFromFactory(int player, const UnitDef& u);
    void recomputePower();
    void checkDefeat();
    bool stepTo(Ent& e, EID id, int nx, int ny);
    bool boardGoal(const Ent& t, int domain, int& gx, int& gy) const; // 登船寻路目标：运输船不可走时取附近最近可走格
    bool chronoJump(Ent& e, float gx, float gy); // 超时空传送：瞬移至目标点附近空格，按距离产生相位不适
    void placeNeutralTechs();                   // 地图生成后放置中立科技建筑（油井/医院/机械店）
    void applySpyEffect(Ent& spy, Ent& bld, EID spyId); // 间谍渗透建筑效果
    void creditKill(EID byEnt, EID victim);     // 军衔经验：击杀计数与晋升
    void spawnCrateTick();                      // 周期性生成补给箱
    void pickupCrates(Ent& e);                  // 地面单位拾取补给箱
    void regrowOre();                           // 矿脉缓慢再生（RA2 矿钻等效）
    void updateTimedBombs();                    // 疯狂伊文炸弹倒计时与引爆
    EID allocEnt();
};
