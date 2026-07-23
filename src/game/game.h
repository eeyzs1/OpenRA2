#pragma once
#include "game/world.h"
#include "game/ai.h"
#include "raylib.h"
#include <vector>
#include <deque>
#include <unordered_set>

constexpr int SCREEN_W = 1440;
constexpr int SCREEN_H = 810;

// 游戏阶段
enum class Phase { MainMenu, Setup, MissionSelect, InGame };

class Game {
public:
    void init(bool windowed = false); // 默认无边框全屏（自适应任意桌面分辨率）；windowed=true 调试用窗口
    void shutdown();
    void run(); // 主循环
    void smokeTest(int frames); // 无头冒烟测试
    int playTest();             // 自动化完整游玩测试：脚本注入输入，真实窗口跑全流程，返回失败数
    void debugMenuShot(const char* file, bool setup); // 菜单截图（验证用）

private:
    World world;
    std::vector<SkirmishAI> ais;
    int localPlayer = 0;

    // 阶段与遭遇战配置
    Phase phase = Phase::MainMenu;
    int cfgFaction = 2;     // 玩家阵营（默认中国）
    int cfgColor = 0;       // 玩家颜色
    int cfgAI = 2;          // AI 数量
    int cfgMoney = 10000;   // 初始资金
    int cfgMapSize = 96;    // 地图边长
    int cfgMapType = 0;     // 地图类型：0 大陆 1 岛屿 2 湖泊
    // 每个 AI 槽位的颜色与阵营（RA2 式槽位配置；阵营 3=随机）
    int aiColor[7] = {1, 2, 3, 4, 5, 6, 7};
    int aiFaction[7] = {3, 3, 3, 3, 3, 3, 3};

    // 遭遇战设置界面的地图预览
    Map previewMap;
    Texture2D previewTex{};
    bool previewDirty = true;
    uint64_t previewSeed = 20260723;

    // 战役状态（campaignMission < 0 = 遭遇战）
    int campaignMission = -1;
    size_t nextWave = 0;

    // 摄像机（世界像素偏移）
    float camX = 0, camY = 0;
    float camSpeed = 14;

    // 选择
    std::vector<EID> sel;
    EID selBuilding = INVALID_EID;
    bool dragging = false;
    Vector2 dragStart{0, 0};

    // 编队（Ctrl+数字设定，数字召回，双击数字跳转视角）
    std::vector<EID> groups[10];
    int lastGroupKey = -1;
    double lastGroupTap = 0;

    // EVA 播报字幕队列（逐条显示）
    std::deque<std::string> evaLines;

    // 建筑放置
    bool placing = false;

    // 超武目标选择模式（COUNT = 无）
    SWType targetingSW = SWType::COUNT;

    // 侧边栏维修/出售点击模式（RA2 标志性按钮）：0 无 1 维修 2 出售
    int sideMode = 0;

    // UI
    Font font{};
    bool fontOk = false;
    int sidebarW = 190;
    int uiTab = 0; // 0 建筑 1 防御 2 步兵 3 车辆 4 海军
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

    // 逻辑分辨率离屏画布（DPI 点对点放大）
    RenderTexture2D canvas{};

    // 小地图
    RenderTexture2D minimap{};
    int minimapTimer = 0;

    float logicAcc = 0;

    // ---- 内部 ----
    void newGame(uint64_t seed);
    void newCampaignGame(int mission);
    void spawnCampaignWave();
    void loadFont();
    void logic();
    void render();
    void handleInput();
    void updateCamera();

    // 渲染子模块
    void drawWorld();
    void drawEntities();
    void drawEffectsLayer();
    void drawFogLayer();
    void drawHUD();
    void updateMinimap(); // 定时重绘小地图纹理（须在画布渲染通道外调用）
    void drawMinimap();
    void drawSidebar();
    void drawPlacement();
    void drawHealthBar(int px, int py, int w, float frac, bool selected);

    // 菜单
    void drawMainMenu();
    void drawSetup();
    void drawMissionSelect();
    void refreshMapPreview(); // 设置界面地图缩略图重生成

    // 坐标
    void worldToScreen(float wx, float wy, int& sx, int& sy) const;
    void screenToWorld(int sx, int sy, float& wx, float& wy) const;
    Vector2 unitScreenPos(const World::Ent& e) const;
    Vector2 bldScreenPos(const World::Ent& e) const;

    // 输入辅助
    EID pickUnit(int mx, int my) const;
    EID pickBuilding(int mx, int my) const;
    void doSelect(int mx, int my, bool additive);
    void doBoxSelect(Rectangle r, bool additive);
    void issueSmartOrder(int mx, int my);
    void message(const std::string& m);

    // 输入包装：统一逻辑坐标（高 DPI 修正）+ 脚本注入（playTest 自动化）
    Vector2 mousePos() const;
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
