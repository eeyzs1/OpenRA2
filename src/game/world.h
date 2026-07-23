#pragma once
#include "game/data.h"
#include "game/map.h"
#include <vector>
#include <deque>
#include <string>

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

struct Player {
    bool active = false;
    bool isAI = false;
    bool defeated = false;
    Faction faction = Faction::Allies;
    int colorId = 0;
    int money = 10000;
    std::string name;

    ProdItem bldProd;   // 建筑生产
    ProdItem unitProd;  // 单位生产
    BldType placingBld = BldType::COUNT; // 待放置建筑（人类玩家用）

    // 统计缓存（每帧重算）
    int powerMade = 0, powerUsed = 0;
    bool lowPower() const { return powerUsed > powerMade; }

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
    };

    std::vector<Ent> ents;
    std::vector<int> freeList;
    std::vector<Projectile> projs;
    std::vector<Effect> effects;
    std::vector<Player> players;
    std::vector<Nuke> nukes;        // 飞行中的核弹
    int numPlayers = 0;
    uint64_t tick = 0;
    Rng rng{12345};

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
    bool startUnitProd(int player, UnitType t);
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

    // 查询
    bool hasBld(int player, BldType t) const;
    bool hasFactoryFor(int player, const UnitDef& u) const;
    bool prereqMet(int player, const BldDef& d) const;
    bool unitPrereqMet(int player, const UnitDef& u) const;
    int countUnits(int player, UnitType t) const;
    int countBlds(int player, BldType t) const;
    EID findNearestEnemy(int player, float x, float y, float maxR, bool includeBlds = true, const WeaponDef* w = nullptr);
    EID bldAt(int bx, int by) const;
    EID unitAtCell(int x, int y) const;

    // 主更新（逻辑帧）
    void update();

    // 迷雾：以 player 视角重新计算可见
    void updateFog(int player);

    // 伤害
    void damage(EID id, int dmg, int byPlayer);

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
    EID allocEnt();
};
