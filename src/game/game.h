#pragma once
#include "game/world.h"
#include "game/ai.h"
#include "game/lang.h"
#include "game/campaign.h"
#include "net/net.h"
#include "raylib.h"
#include <vector>
#include <deque>
#include <map>
#include <unordered_set>

constexpr int SCREEN_W = 1440;
constexpr int SCREEN_H = 810;
// 菜单壳层逻辑分辨率（原作 640×480）；画到 canvas 时统一放大
constexpr int UI_W = 640;
constexpr int UI_H = 480;

// 游戏阶段
enum class Phase { MainMenu, Setup, MissionSelect, Settings, NetLobby, InGame, MapEditor };

// 可重绑定按键动作（设置页修改，settings.ini 持久化，默认值为 RA2 原作键位）
enum KeyAction : int {
    KA_Stop = 0, KA_Unload, KA_Deploy, KA_Scatter, KA_Guard, KA_SameType,
    KA_Music, KA_ViewBase, KA_Pause, KA_Rally, KA_Sell,
    KA_QuickSave, KA_QuickLoad, KA_SpeedUp, KA_SpeedDown,
    KA_Waypoint, // 追加在末尾：配置文件按序号存储，插入中间会破坏既有键位
    KA_COUNT
};

class Game {
public:
    void init(bool windowed = false, bool hidden = false); // 默认无边框全屏；windowed 调试窗口；hidden 测试用隐藏窗口（不弹窗）
    bool assetsOk() const { return assetsOk_; } // false = 缺素材，main 应拒绝进主循环
    void shutdown();
    void run(); // 主循环
    int smokeTest(int frames); // 无头冒烟测试，返回失败断言数
    int campaignSmokeTest(int mission, int frames); // 战役冒烟测试：开局跑 N 帧，返回失败断言数
    int campaignMatrixTest(int frames); // 32 关启动/触发器/胜负静态与运行矩阵
    void benchCampaign(int mission, int warmTicks, int frames); // 临时诊断：战役真实渲染耗时（解除帧率上限）
    int playTest();             // 自动化完整游玩测试：脚本注入输入，真实窗口跑全流程，返回失败数
    int liveVerify();           // 可见窗口 + 真实 OS 鼠标点选/设置页目检（修复实机选不中）
    int visualAudit();          // 建筑虚线笼/建造出售动画/地形目检截图（人工或脚本审图）
    void debugMenuShot(const char* file, bool setup); // 菜单截图（验证用）
    void debugShot(int warmTicks, const char* file); // 遭遇战截图：预热出基地/电厂/单位后拍全屏（验证用）
    void debugGuiReview(const char* outDir); // 各 GUI 审核截图（含主菜单动画样帧）
    int netSelfTestDriver(int role, int frames); // P8 双进程自测：role 0=--net-host 1=--net-client（main 驱动）
    int statePersistenceTest(); // 存档 schema/边界/复杂状态 round-trip 与 checksum 覆盖

    // 快速存档固定槽位（F5 保存 / F9 读取，游戏内菜单共用）
    static constexpr const char* QUICKSAVE_PATH = "saves/quicksave.sav";

private:
    World world;
    std::vector<SkirmishAI> ais;
    int localPlayer = 0;

    // 阶段与遭遇战配置
    bool assetsOk_ = true; // init 校验素材；缺失则 main 拒绝进主循环
    Phase phase = Phase::MainMenu;
    int cfgCountry = (int)Country::China; // 玩家国家（RA2 原作：国家决定阵营与特色单位；COUNT=随机）
    int cfgColor = 0;       // 玩家颜色
    int cfgAI = 2;          // AI 数量
    int cfgMoney = 10000;   // 初始资金
    int cfgMapSize = 96;    // 地图边长
    int cfgMapType = 0;     // 地图类型：0 大陆 1 岛屿 2 湖泊
    // 每个 AI 槽位的颜色与国家（RA2 式槽位配置；国家 COUNT=随机，国家即定阵营）
    int aiColor[7] = {1, 2, 3, 4, 5, 6, 7};
    int aiCountry[7] = {(int)Country::COUNT, (int)Country::COUNT, (int)Country::COUNT, (int)Country::COUNT,
                        (int)Country::COUNT, (int)Country::COUNT, (int)Country::COUNT};
    int aiDiff[7] = {1, 1, 1, 1, 1, 1, 1};           // AI 难度：0简单 1普通 2困难 3残酷
    int aiPersonality[7] = {0, 0, 0, 0, 0, 0, 0};     // AI 人格：0均衡 1速攻 2龟缩 3轰压 4科技

    // 遭遇战选项（设置界面可改，开局应用；音量热更新）
    bool cfgCrates = true;   // 随机补给箱
    bool cfgAlliance = false; // AI 组成同一联盟
    bool cfgSharedVision = true;
    bool cfgShortGame = false;
    bool cfgSuperweapons = true;
    bool cfgMcvRepacks = true; // 默认开启：建造厂可打包回 MCV（遭遇战亦可在设置关闭）
    int cfgGameMode = (int)SkirmishMode::Battle;
    int cfgVolume = 4;       // 音量档位 0..4（0/25/50/75/100%）

    // ---- 应用设置（settings.ini 持久化，改动即保存即时生效，无需重启）----
    int cfgLang = 0;        // 界面语言：0 中文 1 English
    int cfgWindowMode = 0;  // 显示模式：0 无边框全屏 1 窗口
    int cfgResIdx = 3;      // 窗口分辨率档位（RES_LIST 下标，默认 1440x810）
    bool borderlessActive = false; // 当前窗口实际状态（applyDisplay 幂等判断）
    bool displayDirty = false;     // 显示模式/分辨率待应用（帧首执行，避免帧中改窗口）
    int keyBind[KA_COUNT] = {};    // 动作 → raylib 键码
    void loadSettings();      // 启动时读取 settings.ini（缺省则保持默认值）
    void saveSettings() const; // 任何设置变更后落盘
    void applyDisplay();      // 应用显示模式与窗口分辨率（热切换）
    void resetKeyBinds();     // 恢复 RA2 默认键位
    static const int RES_LIST[8][2]; // 可选窗口分辨率（16:9 / 16:10 / 4:3）

    // 设置页状态
    bool settingsFromGame = false; // 入口：false 主菜单 / true 局内菜单（返回时恢复 showMenu）
    int rebinding = -1;            // >=0 正在为该 KeyAction 等待新按键

    // 遭遇战设置界面的地图预览
    Map previewMap;
    Texture2D previewTex{};
    bool previewDirty = true;
    uint64_t previewSeed = 20260723;

    // 战役状态（campaignMission < 0 = 遭遇战）
    int campaignMission = -1;
    size_t nextWave = 0;
    // P7 触发器运行时：开局从 MissionDef 拷贝（fired/armed 为可变状态），HUD 目标文本
    std::vector<Trigger> missionTriggers;
    std::string objectiveText;
    void updateTriggers(); // 战役触发器求值与执行（logic 内每帧调用）

    // ---- P8 LAN 联机（确定性 lockstep，1v1 遭遇战） ----
    NetLink net;
    bool netGame = false;          // 联机对局进行中
    int netPlayer = 0;             // 本端槽位：0 主机 1 客户端
    static constexpr int NET_PORT = 55555;
    static constexpr int NET_DELAY = 3; // 输入延迟（tick，30/s）
    uint64_t netSimTick = 0;       // 已推进的模拟 tick（= world.tick）
    std::map<uint64_t, std::vector<World::Cmd>> localCmds, remoteCmds;
    std::vector<World::Cmd> pendingCmds; // 本逻辑帧收集，netAdvance 分配发送
    std::map<uint64_t, uint32_t> localSums; // 本端历史校验和（MsgChecksum 比对用）
    bool netDesync = false;        // 校验和不一致（不同步）
    bool netPeerLeft = false;      // 对手断线
    // 大厅状态
    int lobbyRole = 0;             // 0 主机 1 加入
    std::string lobbyIp = "127.0.0.1";
    int lobbyState = 0;            // 0 选角色 1 等待连接 2 已连接待开局 3 连接失败
    bool lobbyEditingIp = false;
    uint64_t netSeed = 0;
    int peerCountry = -1, peerColor = -1; // 对手国家/颜色（握手交换）
    void issueCmd(const World::Cmd& c);   // 单机立即执行；联机入队（NET_DELAY 后执行）
    void netAdvance();                    // lockstep：收发命令帧，条件满足推进 1 tick
    void netHandleMsgs();                 // 消费 NetLink 消息（大厅握手/局中命令帧）
    void netBeginGame();                  // 双方同步 newGame（host 在 Welcome 内定配置）
    void netLeave();                      // 结束联机状态回主菜单
    void drawNetLobby();                  // 大厅 UI（game_net.cpp）

    // 摄像机（世界像素偏移）
    float camX = 0, camY = 0;
    float camSpeed = 14;
    float camZoom = 1.0f; // 滚轮缩放：0.5=拉远，2.0=拉近
    int camEdgeLock = 0; // 开局若干逻辑帧内禁止边缘卷轴，避免点「开始」后镜头被拖走
    static constexpr float CAM_ZOOM_MIN = 0.5f;
    static constexpr float CAM_ZOOM_MAX = 2.0f;

    // 选择
    std::vector<EID> sel;
    EID selBuilding = INVALID_EID;
    bool dragging = false;
    Vector2 dragStart{0, 0};
    bool dragPressSelected = false; // 按下时已点中单位/建筑（松手小拖动则保持，大框则改框选）

    // 鼠标光标（RA2 mouse.shp 提取帧）
    enum class CursorKind : uint8_t {
        Arrow, Move, Attack, Harvest, Enter, Deploy, Repair, Sell, NoMove, COUNT
    };
    CursorKind cursorKind = CursorKind::Arrow;
    void updateHoverCursor(int mx, int my);
    void drawGameCursor(int mx, int my);
    void loadGameCursors();
    void unloadGameCursors();
    struct CursorDef { int start, count, interval, hx, hy; };
    // mouse.shp 全量约 450 帧；AttackMove=404 必须可加载（Ares 表）
    static constexpr int CURSOR_MAX_FRAMES = 512;
    Texture2D cursorFrames[CURSOR_MAX_FRAMES]{};
    int cursorFrameN = 0;
    CursorDef cursorDefs[(int)CursorKind::COUNT]{};
    bool cursorsLoaded = false;

    // 编队（Ctrl+数字设定，数字召回，双击数字跳转视角）
    std::vector<EID> groups[10];
    int lastGroupKey = -1;
    double lastGroupTap = 0;

    // EVA 播报字幕队列（逐条显示）
    std::deque<std::string> evaLines;

    // 建筑放置
    bool placing = false;
    bool visualAuditMarkers = false; // --visual-audit：叠青框/锚点核对选中笼


    // 超武目标选择模式（COUNT = 无）
    SWType targetingSW = SWType::COUNT;
    std::vector<EID> chronoSourceSel; // 第一阶段来源区域车辆；最终随 lockstep Cmd 同步
    // 伞兵空降点选择模式（RA2 原作：美国空指部/科技机场支援技能）
    bool targetingParadrop = false;

    // 侧边栏维修/出售点击模式（RA2 标志性按钮）：0 无 1 维修 2 出售
    int sideMode = 0;

    // UI
    Font font{};
    bool fontOk = false;
    int sidebarW = 184; // RA2 原作侧边栏占屏宽 ~12.5%（1366x768 原作实测 171px → 1440 等比 180+）
    static constexpr int BOTTOM_BAR_H = 35; // 底部命令栏（原作 1366x768 底栏 33px → 810 等比 35）
    int uiTab = 0; // 0 建筑 1 防御(含超武) 2 步兵 3 车辆/海军（RA2 原作 4 页签）
    int uiScroll = 0;   // 生产网格滚动行（超出一页时 ▲▼ 滚动）
    bool showFps = false; // F3 帧率/耗时显示（性能诊断）
    bool paused = false;
    bool showMenu = false;
    int gameSpeed = 1; // 1x 2x
    std::string msg;
    float msgTimer = 0;
    bool gameOver = false;
    bool victory = false;
    bool wasLowPower = false;
    std::string shotFile; // 非空时本帧渲染后截图

    // 迷雾贴图
    Texture2D fogBlack{}, fogDim{};

    // ---- 整图地表烘焙（连续噪声：消除逐瓦片菱形网格感；渲染 draw 调用 ~800 → 1）----
    Texture2D terrainTex{};
    int terrainW = 0, terrainH = 0; // 世界像素域尺寸
    float terrainOX = 0;            // 等距 sx 原点偏移（tileToScreen 的 sx 最小值为负，平移到 0）
    static constexpr float TERRAIN_SC = 1.0f; // 烘焙缩放目标（原生分辨率 = 清晰；过大时按 GPU 纹理上限降档）
    float terrainSC = TERRAIN_SC;             // 实际生效缩放（bakeTerrain 按纹理上限钳制后写入）
    void bakeTerrain();             // 开局/读档后调用（地图静态，矿脉动态瓦片除外）

    // ---- 迷雾软遮罩（屏幕空间 1/8 分辨率 alpha 图，bilinear 放大 = 软边界，消除棋盘格）----
    Texture2D fogMaskTex{};
    int fogMaskTick = -1;           // 上次烘焙的逻辑 tick（定期重烘）
    void bakeFogMask();

    // 逻辑分辨率离屏画布（DPI 点对点放大）
    RenderTexture2D canvas{};

    // 小地图
    RenderTexture2D minimap{};
    int minimapTimer = 0;

    float logicAcc = 0;
    float interpAlpha = 1.0f; // 渲染插值系数：逻辑帧间进度 0..1（30Hz 逻辑 → 60fps 平滑）
    bool waypointLatch = false; // 路径点模式（Z）：右键追加路径点而非替换目标

    // ---- 内部 ----
    void newGame(uint64_t seed);
    void newCampaignGame(int mission, bool prepareRender = true);
    void spawnCampaignWave();
    bool saveGameFile(const char* path); // 快速存档（F5/菜单）：Game 头 + World 全量状态
    bool loadGameFile(const char* path); // 快速读档（F9/菜单）
    void loadFont();
    void logic();
    void render();
    void handleInput();
    void updateCamera();

    // 渲染子模块
    void drawWorld();
    void drawEntities();
    void drawSelectionOverlay();
    void flushWorldOverlayRects(); // 叠层已在 zoom 矩阵内 DrawTexturePro；保留空实现兼容调用
    void drawEffectsLayer();
    void drawFogLayer();
    void drawHUD();
    void updateMinimap(); // 定时重绘小地图纹理（须在画布渲染通道外调用）
    void drawMinimap(int x, int y, int w, int h);
    bool radarOnline() const; // 雷达类建筑在线且电力充足（RA2 小地图激活条件）
    void drawSidebar();
    void drawPlacement();
    // pipCount<=0：按宽度估算格数；建筑修理逻辑用 20 格，宜显式传入
    void drawHealthBar(int px, int py, int w, float frac, bool selected, int pipCount = 0);

    // 菜单
    void drawMainMenu();
    void drawSetup();
    void drawMissionSelect();
    void drawSettings();     // 设置页（语言/显示/音量/按键）
    void drawGameMenuOverlay(); // ESC/结算：画在 640 UI RT 上（勿嵌套 canvas）
    int pollAnyKey();        // 重绑定捕获：真实 GetKeyPressed 或脚本注入
    void refreshMapPreview(); // 设置界面地图缩略图重生成

    // 地图编辑器（game_editor.cpp）
    int edTool = 0;          // 0 地形 1 装饰 2 单位 3 建筑 4 出生点 5 擦除
    int edTerrain = 0;       // 当前地形类型（0=clear 1=rough 2=water 3=ore 4=gems 5=bridge）
    int edHeight = 0;        // 地形刷写入的格高度 0..3
    int edOverlay = 0;       // 当前装饰类型
    int edUnitIdx = 0;       // 当前单位类型索引
    int edBldIdx = 0;        // 当前建筑类型索引
    int edPlayer = 0;        // 当前玩家（-1=中立）
    int edMapSize = 96;      // 编辑器地图大小
    int edSpawnIdx = 0;      // 出生点编辑索引
    std::string edMapName = "custom_1";
    void drawMapEditor();
    void editorPlace(int mx, int my);
    void editorSave();
    void editorNewMap();

    // 坐标
    void worldToScreen(float wx, float wy, int& sx, int& sy) const;
    void screenToWorld(int sx, int sy, float& wx, float& wy) const;
    Vector2 unitScreenPos(const World::Ent& e) const;
    Vector2 bldScreenPos(const World::Ent& e) const;
    // 单位屏幕贴图矩形（含锚点/飞行高度）；点选与选中框共用，避免脚底锚点导致难选
    Rectangle unitScreenRect(const World::Ent& e) const;

    // 输入辅助
    EID pickUnit(int mx, int my) const;
    EID pickBuilding(int mx, int my) const;
    void doSelect(int mx, int my, bool additive);
    void doBoxSelect(Rectangle r, bool additive);
    void issueSmartOrder(int mx, int my);
    void cmdDeploySel(); // 部署选中单位（MCV 展开/步兵部署，底栏按钮与 D 热键共用）
    void message(const std::string& m);

    // 输入包装：统一逻辑坐标（高 DPI 修正）+ 脚本注入（playTest 自动化）
    Vector2 mousePos() const;
    // 画布 letterbox 目标矩形（与 render 绘制必须同公式）
    static Rectangle letterboxDst();
    bool mPressed(int btn) const;
    bool mDown(int btn) const;
    bool mReleased(int btn) const;
    bool kPressed(int key) const;
    bool kDown(int key) const;
    struct SimInput {
        bool active = false;
        Vector2 pos{0, 0};
        bool downL = false, downR = false;
        bool pressedL = false, pressedR = false;   // 本帧按下沿
        bool releasedL = false, releasedR = false; // 本帧释放沿
        std::unordered_set<int> keysDown;
        std::unordered_set<int> keysPressed;
    };
    SimInput sim{};

    // UI 辅助
    bool uiButton(Rectangle r, const char* text, bool enabled, bool active = false);
    std::vector<BldType> tabBuildings() const;
    std::vector<UnitType> tabUnits() const;
};

// 菜单共享 UI 组件（game_menu.cpp 实现，game_settings.cpp 复用）
void drawTextM(Font f, const char* s, int x, int y, int size, Color c);
int textW(Font f, const char* s, int size);
bool ra2Button(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size = 20,
               bool enabled = true, bool danger = false);
bool ra2TextButton(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size = 28);
void drawMenuBackdrop(Font font, const char* title); // 兼容旧调用 → RA2 shell
// theme: 0=load 选项/遭遇战/战役 / 1=盟军暂停 / 2=multi 联机大厅；drawEmptyMonitor=false 时监视器留给调用方
void drawRa2Shell(Font font, const char* sideTitle, int theme = 0, bool drawEmptyMonitor = true);
// 红框滑条（离散档）；点击/拖动改档时返回 true 且更新 step
bool ra2RedSlider(Font font, Vector2 m, bool pressed, int x, int y, int trackW,
                  int& step, int nSteps, const char* valueText);
Rectangle menuShellContent(); // 左内容区（drawRa2Shell 之后有效）
Rectangle menuShellSide();    // 右侧栏
Rectangle menuShellMonitor(); // 右侧监视器槽
void ensureMenuGui();
bool drawMenuPanelChrome(int x, int y, int w, int h);
void drawMenuOptSlot(Rectangle r, bool hover);
void drawMenuOptSlot(Rectangle r, bool hover, bool showArrow);
void drawMenuPip(float x, float y, bool on); // 原作 pips 勾选
void menuSetBikForceFrame(int frame);
int menuBikFrameCount();
// 菜单 UI 坐标 ↔ canvas（1440×810）坐标；壳层页绘制期间有效
Vector2 menuUiFromCanvas(Vector2 canvasPos);
Vector2 menuCanvasFromUi(float uiX, float uiY);
float menuUiScale();
void menuBeginUi();  // 开始画到 640×480 RT（勿嵌套在其它 RenderTexture 内）
void menuEndUi();    // 结束 UI RT（不 blit）
void menuBlitUi();   // 将 UI RT 整数/等比 POINT 放大贴到当前目标（canvas）
bool menuShellPhase(Phase p); // MainMenu/Setup/Settings/MissionSelect/NetLobby

// RA2 金属 GUI 辅助（game_hud.cpp 实现，菜单/设置页共享复用，确保全 GUI 风格一致）
void guiMetalFill(int x, int y, int w, int h);          // 平铺拉丝金属底
void guiBevel(Rectangle r, bool sunken);                // 棱台斜面（凸起/凹陷）
void guiRivet(int x, int y);                            // 铆钉角饰
void guiSlot(Rectangle r);                              // 凹陷信息槽
void guiPanel(int x, int y, int w, int h);              // 金属面板（拉丝+棱+金线+铆钉）
void drawTextS(Font f, const char* s, int x, int y, int size, Color c); // 带投影文字
// RA2 GUI 共享色板（与 game_hud.cpp 内一致）
extern const Color GUI_GOLD;
extern const Color GUI_GOLD_HI;
extern const Color GUI_EDGE_HI;
extern const Color GUI_EDGE_LO;
