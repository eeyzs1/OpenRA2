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
    ProjKind kind = ProjKind::Bullet;
    int player = 0;
    float x = 0, y = 0;         // 瓦片浮点坐标
    float tx = 0, ty = 0;       // 目标点
    EID target = INVALID_EID;         // 追踪目标（可 INVALID）
    EID src = INVALID_EID; // 发射者（军衔经验归属）
    int srcGarrisonSlot = -1; // 建筑驻军弹道的实际乘员
    WeaponDef w{};
    int speed = 0;          // 每逻辑帧移动比例
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
    // kind=10 死亡动画参数：utype/dir/colorId
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
};

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
    int money = 10000;
    std::string name;

    ProdItem bldProd;                     // 建筑生产队列（RA2：与防御独立）
    ProdItem defProd;                     // 防御/超武生产队列（可与建筑并行）
    ProdItem unitProd[PROD_CAT_N];        // 单位生产：按类别独立队列（RA2 原作设定）
    std::deque<int> unitQueue[PROD_CAT_N]; // 各类别排队待产的类型索引（队首为当前项的后续）
    BldType placingBld = BldType::COUNT; // 待放置建筑（人类玩家用）

    // 统计缓存（每帧重算）
    int powerMade = 0, powerUsed = 0;
    int powerSabotage = 0;   // 间谍破坏电厂：>0 期间强制低电
    int revealTimer = 0;     // 间谍渗透雷达：>0 期间全图可见
    bool vetCat[PROD_CAT_N] = {}; // 间谍渗透工厂：对应类别新造单位直接 1 级军衔
    int aiDifficulty = 1;    // AI 难度 0 简单 1 普通 2 困难（仅 AI 玩家）
    // ---- RA2 补全：国家/支援技能/中立科技 ----
    int secretLabUnlock = 0;   // 秘密实验室解锁的国家特色（(int)Country；0=未解锁）
    int stolenTech = 0;        // 间谍渗透敌高科解锁的偷科技单位位掩码（bit0 超时空突击队 bit1 心灵突击队）
    int paradropCharge = 0;    // 伞兵充能（>=PARADROP_TIME 就绪；美国空指部/科技机场提供）
    bool paradropReady = false;
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
        struct GarrisonedUnit {
            UnitType type = UnitType::GI;
            int hp = 1;
            int kills = 0;
            int veterancyValue = 0;
            int vetRank = 0;
        };
        bool alive = false;
        bool isBuilding = false;
        int player = -1;
        UnitType utype = UnitType::GI;
        BldType btype = BldType::ConYard;
        float x = 0, y = 0;        // 单位：浮点瓦片坐标；建筑：左上格
        float px = 0, py = 0;      // 上一逻辑帧位置（渲染插值用，update() 开头快照）
        int dir = 2;
        int turretDir = 2;
        int hp = 1;
        // 移动
        std::vector<Vec2i> path;
        int pathIdx = 0;
        int moveTick = 0;
        int blockTick = 0;          // 路径被堵计时（超时放弃路径）
        int walkFrame = 0, walkAnim = 0;
        // 动画状态机（art.ini 序列驱动）
        int fireAnim = 0;       // 开火动画剩余 tick（>0 播放开火序列，phase 由剩余量推导）
        int constructAnim = 0;  // 建筑建造动画剩余 tick（>0 播放 mk 序列，播完为成品）
        int deployAnim = 0;     // MCV 展开/部署动画剩余 tick（>0 期间单位定格播展开序列）
        // 战斗
        UState state = UState::Idle;
        int atkCd = 0;
        EID target = INVALID_EID;
        float goalX = 0, goalY = 0; // 期望目的地
        std::deque<std::pair<float, float>> wps; // 路径点队列（Z 键追加，到位自动接续）
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
    };

    // 补给箱（RA2 随机箱子）：地面单位驶入拾取
    struct Crate {
        bool alive = true;
        int x = 0, y = 0;
        int kind = 0; // 0钱 1治疗 2升阶 3大钱 4隐匿 5治疗+升阶 6免费单位 7火力 8护甲 9速度 10全图揭示
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
    SkirmishMode skirmishMode = SkirmishMode::Battle;
    bool cratesEnabled = true;       // 随机补给箱
    bool aiAlliance = false;         // AI 组成同一队伍（一致对外）
    bool sharedVision = false;       // 盟友共享已探索区与当前视野
    bool shortGame = false;          // 无建筑且无 MCV 即判负
    bool mcvRepacks = false;         // 建造厂可打包为 MCV（官方遭遇战选项）
    bool superweaponsEnabled = true; // 禁用时不充能、不可发射且不可建造超武建筑

    // 敌对判定（考虑 AI 结盟）
    bool isEnemy(int a, int b) const {
        if (a == b || a < 0 || b < 0) return false;
        if (aiAlliance && players[a].isAI && players[b].isAI) return false;
        return true;
    }
    bool isAllied(int a, int b) const {
        return a == b || (a >= 0 && b >= 0 && !isEnemy(a, b));
    }
    bool modeAllowsBuilding(int player, BldType t) const;
    bool modeAllowsUnit(int player, UnitType t) const;
    void ensureMegawealthOilDerricks(int perPlayer = 2);

    // 建筑占格（cellIdx -> eid+1）
    std::vector<int> bldOcc;

    // mapFile 非空时加载手工地图（maps/xxx.txt：地形+预置实体+出生点），失败回退程序生成；
    // noStartForce=true 时不刷初始基地车部队（全部由地图文件放置，仅手工地图有效）
    void init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType = 0,
              const char* mapFile = nullptr, bool noStartForce = false);

    // 手工地图待放置实体（loadHandMap 解析产出，init 内统一放置）
    struct PendingEnt {
        bool isBld = false;
        int player = -1;     // -1 中立
        int typeIdx = 0;     // UnitType / BldType
        int x = 0, y = 0;
        bool guard = false;  // 单位警戒（AI 防守部队）
    };

    // 创建
    EID spawnUnit(int player, UnitType t, float x, float y);
    EID spawnBuilding(int player, BldType t, int bx, int by, bool free_ = false);
    void kill(EID id, bool explode = true); // explode=false：出售等无爆炸拆除

    // 访问
    Ent& ent(EID id) { return ents[id]; }
    bool valid(EID id) const { return id >= 0 && id < (int)ents.size() && ents[id].alive; }

    // 指令
    void orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove, bool append = false);
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
    void orderGarrison(const std::vector<EID>& sel, EID bldId);   // 步兵/车辆进驻建筑（民房/战斗碉堡/坦克碉堡）
    void orderUngarrison(const std::vector<EID>& sel);            // 建筑撤出驻军（U）
    void orderRadDeploy(const std::vector<EID>& sel); // 辐射工兵：部署/收起辐射区（D）
    void orderParadrop(int player, float x, float y);  // 伞兵空投（美国空指部/科技机场）
    void orderSpyPlane(int player, float x, float y);  // YR 侦察机揭雾
    void orderPsychicReveal(int player, float x, float y); // YR 心灵揭示揭雾
    void orderService(const std::vector<EID>& sel, EID depotId); // 车辆开往维修厂（维修+摘除寄生）

    // ---- P8 联机：玩家命令抽象（lockstep 同步的最小单元） ----
    // 双端确定性前提：同种子初始化 + 相同命令序列（同 tick 序、同内容）驱动 update
    struct Cmd {
        enum Type : uint8_t {
            None = 0,
            Move, Attack, Harvest, Stop, Deploy, Capture, Repair, Scatter, Guard,
            Board, Unload, Garrison, Ungarrison, RadDeploy, Paradrop, Service,
            StartUnitProd, CancelUnitProd, StartBldProd, CancelBldProd,
            HoldUnitProd, HoldBldProd, // a=typeIdx；b=1 暂停 / 0 继续（建筑/防御按类型分流队列）
            PlaceBuilding, SetRally, SellBuilding, RepairBuilding, LaunchSW,
        };
        Type type = None;
        std::vector<EID> ids; // 选择集（可空）
        float x = 0, y = 0;   // 目标点（地图格坐标）
        int a = 0, b = 0;     // 参数：type 索引 / 目标 EID / (bx,by)；Move 专用：a&1=追加路径点
        bool attackMove = false; // Move 用：A 键攻击移动
    };
    void applyCmd(int player, const Cmd& c); // 执行一条玩家命令（联机双端对称调用）
    uint32_t checksum() const;               // 世界状态校验和（lockstep 反不同步检测）

    // EVA 播报事件（Game 层消费：字幕+提示音；player = 接收方）
    struct EvaEvent { int player; std::string text; };
    std::deque<EvaEvent> evaQueue;
    void eva(int player, const std::string& text);
    void evaAll(const std::string& text);

    // 生产
    bool startUnitProd(int player, UnitType t);   // 队列空→开工；否则排入队尾（每类最多 30）
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
    bool placeBuilding(int player, BldType t, int bx, int by); // 消耗就绪的生产项
    ProdItem& bldQueueFor(int player, BldType t);
    const ProdItem& bldQueueFor(int player, BldType t) const;
    void setRally(EID factory, int x, int y);
    void sellBuilding(EID id);
    bool repairBuilding(EID id); // 切换持续维修；不可修返回 false
    int unitProductionCost(int player, UnitType t) const; // 含工业工厂车辆造价减免

    // 超武
    bool launchSW(int player, SWType t, float tx, float ty); // 释放（扣充能）
    bool launchChronoShift(int player, const std::vector<EID>& sel, float tx, float ty); // 人类两阶段来源/目标流程
    void updateSW();                                        // 充能与激活效果
    bool swAvailable(int player, SWType t) const;           // 有对应建筑且未战败
    void chronoShiftUnits(const std::vector<EID>& sel, float tx, float ty); // 超时空传送选中车辆

    // 单位可见性（潜艇隐身等）：viewer 能否看见该实体
    bool visibleTo(const Ent& e, int viewer) const;
    bool isDetector(UnitType t) const; // 反潜探测单位（驱逐舰/神盾/海蝎/海豚）
    // 有效武器：部署形态 / IFV 载兵 / 精英军衔 综合（RA2 原作：状态与军衔改变武器）
    WeaponDef effWeapon(const Ent& e) const;

    // 伞兵充能就绪所需帧数（RA2 原作约 4 分钟一波）
    static constexpr int PARADROP_TIME = 30 * 60 * 4;
    // 伞兵来源：美国空指部 或 已占领的科技机场
    bool hasParadropSource(int player) const;
    // YR 支援：苏/中雷达→侦察机；尤里心灵传感器→心灵揭示（与伞兵共用充能字段）
    bool hasSpyPlaneSource(int player) const;
    bool hasPsychicRevealSource(int player) const;

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
    bool groundUnitBlocksCell(int x, int y, EID ignore = INVALID_EID) const;
    // 移动软碰撞：目标格对 mover 是否硬阻挡（RA2：友军/移动中可穿，步兵可叠）
    bool cellHardBlockedForMove(int x, int y, EID mover) const;
    int countInfantryAtCell(int x, int y, EID ignore = INVALID_EID) const;

    // 主更新（逻辑帧）
    void update();

    // 存档/读档（二进制序列化整个模拟状态，追加到已打开的文件；Game 层负责文件头）
    bool saveGame(FILE* f) const;
    bool loadGame(FILE* f);

    // 迷雾：以 player 视角重新计算可见
    void updateFog(int player);

    // 伤害（byEnt 为攻击者实体，用于军衔经验；可为 INVALID）
    void damage(EID id, int dmg, int byPlayer, EID byEnt = INVALID_EID, int byGarrisonSlot = -1);

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
    // 从 (fx,fy) 迈入 (x,y)：地形可走且爬升合法
    bool passableStep(int fx, int fy, int x, int y, int domain) const {
        return passableFor(x, y, domain) && map.climbOk(fx, fy, x, y, domain);
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
    bool spawnFromFactory(int player, const UnitDef& u);
    void recomputePower();
    void checkDefeat();
    bool stepTo(Ent& e, EID id, int nx, int ny);
    bool boardGoal(const Ent& t, int domain, int& gx, int& gy) const; // 登船寻路目标：运输船不可走时取附近最近可走格
    bool buildingApproachGoal(const Ent& b, int domain, int& gx, int& gy) const; // 靠近建筑占地外缘的最近可走格
    bool nearBuildingFootprint(float x, float y, const Ent& b, float pad = 1.75f) const; // 是否贴近建筑占地（占领/停靠）
    bool insideBuildingFootprint(float x, float y, const Ent& b) const; // 是否已走进建筑占地（工程师进入）
    void evacuateGarrison(EID bldId); // 驻军撤出到周围（重伤民房 / 手动撤军共用）
    bool chronoJump(Ent& e, float gx, float gy); // 超时空传送：瞬移至目标点附近空格，按距离产生相位不适
    void placeNeutralTechs();                   // 地图生成后放置中立科技建筑（油井/医院/机械店/科技机场/秘密实验室/民房）
    bool loadHandMap(const char* path, int numPlayers, std::vector<Vec2i>& spawns,
                     std::vector<PendingEnt>& out); // P7 手工地图：解析地形/实体/出生点；失败返回 false
    void applySpyEffect(Ent& spy, Ent& bld, EID spyId); // 间谍渗透建筑效果
    void creditKill(EID byEnt, EID victim, int garrisonSlot = -1); // 按目标价值累计军衔经验
    void spawnCrateTick();                      // 周期性生成补给箱
    void pickupCrates(Ent& e);                  // 地面单位拾取补给箱
    void regrowOre();                           // 矿脉缓慢再生（RA2 矿钻等效）
    void updateTimedBombs();                    // 疯狂伊文炸弹倒计时与引爆
    void garrisonFire(Ent& b, EID id);          // 驻军轮流出击（民房/战斗碉堡/坦克碉堡）
    void applyGapShroud();                      // 裂缝产生器：敌军在黑幕半径内的迷雾降为不可见
    void updateParadrop();                      // 伞兵充能（美国空指部/科技机场）
    // ---- P6：心灵控制 ----
    void mindControlTake(Ent& yuri, EID yid, EID tid); // 夺取目标控制权（先释放旧目标）
    void mindControlRelease(Ent& yuri);                // 释放当前控制（目标恢复原属）
    void applyCaptureEffect(Ent& b, int newOwner); // 工程师占领建筑的特殊效果（科技机场/秘密实验室）
    EID allocEnt();
};
