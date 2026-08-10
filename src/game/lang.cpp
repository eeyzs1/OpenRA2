#include "game/lang.h"
#include "game/campaign.h"
#include "core/ini.h"
#include "raylib.h"
#include <cstdio>
#include <cstring>

int g_lang = 0; // 0 中文 1 English

// ===================== 字符串表 =====================
// 顺序必须与 enum class S 一一对应
static const char* TBL[(int)S::COUNT][2] = {
    // 通用
    {"返回", "Back"},
    {"开", "On"},
    {"关", "Off"},
    {"设置", "Settings"},
    {"随机", "Random"},
    // 主菜单
    {"共和国之辉", "OPENRA2"},
    {"COMMAND & CONQUER · OPENRA2 复刻", "COMMAND & CONQUER · OPENRA2 REMAKE"},
    {"遭遇战", "Skirmish"},
    {"战役模式", "Campaign"},
    {"退出游戏", "Exit Game"},
    {"程序生成 3D 预渲染素材 · 盟军 / 苏联 / 中国", "Procedural 3D pre-rendered assets · Allies / Soviet / China"},
    {"地图编辑器", "Map Editor"},
    // 战役选择
    {"任务 %d", "Mission %d"},
    {"目标：坚守 %d 分钟", "Objective: Hold for %d minutes"},
    {"目标：歼灭所有敌军", "Objective: Eliminate all enemies"},
    {"目标：完成使命目标", "Objective: Complete mission objectives"},
    {"点击进入", "Click to start"},
    // 遭遇战设置
    {"更换一张", "New Map"},
    {"自订战役", "Customize Battle"},
    {"地图尺寸", "Map Size"},
    {"小 64x64", "Small 64x64"},
    {"中 96x96", "Medium 96x96"},
    {"大 128x128", "Large 128x128"},
    {"地图类型", "Map Type"},
    {"大陆", "Continent"},
    {"岛屿", "Islands"},
    {"湖泊", "Lake"},
    {"玩家", "Player"},
    {"颜色", "Color"},
    {"国家", "Country"},
    {"指挥官（你）", "Commander (You)"},
    {"电脑 %d", "Computer %d"},
    {"移除", "Remove"},
    {"+ 添加电脑", "+ Add Computer"},
    {"初始资金", "Starting Funds"},
    {"游戏速度", "Game Speed"},
    {"慢", "Slow"},
    {"普通", "Normal"},
    {"快", "Fast"},
    {"音量", "Volume"},
    {"补给箱", "Crates"},
    {"AI联盟", "AI Team"},
    {"开始游戏", "Start Game"},
    {"游戏模式", "Game Mode"},
    {"标准作战", "Battle"},
    {"自由混战", "Free For All"},
    {"邪恶联盟", "Unholy Alliance"},
    {"巨富", "Megawealth"},
    {"抢地盘", "Land Rush"},
    {"绞肉机", "Meat Grinder"},
    {"海战", "Naval War"},
    {"快速游戏", "Short Game"},
    {"共享视野", "Shared Vision"},
    {"超级武器", "Superweapons"},
    {"建造厂可回打包", "MCV Repacks"},
    // HUD / 侧边栏
    {"资金", "Funds"},
    {"电", "PWR"},
    {"低电力!", "LOW POWER!"},
    {"需建筑", "Need Bldg"},
    {"就绪", "Ready"},
    {"点击选目标", "Pick target"},
    {"建筑", "Base"},
    {"防御", "Def"},
    {"步兵", "Inf"},
    {"车辆", "Veh"},
    {"海军", "Navy"},
    {"选择放置位置（右键取消）", "Select placement (right-click to cancel)"},
    {"无法建造：缺前置建筑或资金不足", "Cannot build: missing prerequisite or low funds"},
    {"生产队列忙或条件不足", "Production busy or requirements unmet"},
    {"已取消一个", "Canceled one"},
    {"已取消生产", "Production canceled"},
    {"维修", "Repair"},
    {"出售", "Sell"},
    {"菜单", "Menu"},
    {"维修模式：点击己方受损建筑（右键取消）", "Repair mode: click your damaged building (right-click to cancel)"},
    {"出售模式：点击己方建筑（右键取消）", "Sell mode: click your building (right-click to cancel)"},
    {"坚守 %d:%02d", "Hold %d:%02d"},
    {"敌军增援将至（第%d/%d波）", "Enemy wave incoming (%d/%d)"},
    {"歼灭所有敌军", "Eliminate all enemies"},
    {"已选 %d 单位", "%d units selected"},
    {"载员 %d/%d  %s 卸载", "Cargo %d/%d  %s to unload"},
    {"左键选择/框选 右键移动/攻击(点己方运输船=登船) A+右键攻击移动 ESC菜单 · 按键可在设置中修改",
     "LMB select/box  RMB move/attack (click own transport=board)  A+RMB attack-move  ESC menu · keys in Settings"},
    {"雷达离线", "RADAR OFFLINE"},
    {"已暂停", "PAUSED"},
    {"胜 利", "VICTORY"},
    {"失 败", "DEFEAT"},
    {"游戏菜单", "Game Menu"},
    {"再来一局", "Play Again"},
    {"继续游戏", "Continue"},
    {"保存进度", "Save Game"},
    {"读取进度", "Load Game"},
    {"重新开始", "Restart"},
    {"返回主菜单", "Main Menu"},
    // 局内消息
    {"找到基地车，按D键展开！", "Find your MCV, press %s to deploy!"},
    {"电力不足，生产减缓", "Low power: production slowed"},
    {"工程师：前往占领", "Engineer: moving to capture"},
    {"步兵登船中", "Infantry boarding"},
    {"工程师：前往修复", "Engineer: moving to repair"},
    {"无法在此放置", "Cannot place here"},
    {"已出售建筑", "Building sold"},
    {"建造厂不可出售", "Construction Yard cannot be sold"},
    {"建筑维修已切换", "Building repair toggled"},
    {"无需维修或资金不足", "Nothing to repair or low funds"},
    {"建造厂已部署", "Construction Yard deployed"},
    {"已切换部署状态", "Deploy state toggled"},
    {"单位散布", "Units scattering"},
    {"警戒模式：按视野索敌", "Guard mode: engage on sight"},
    {"选择同类单位", "Selected same-type units"},
    {"编队 %d 已设定（%d 单位）", "Squad %d set (%d units)"},
    {"音乐：开", "Music: On"},
    {"音乐：关", "Music: Off"},
    {"进度已保存", "Game saved"},
    {"保存失败", "Save failed"},
    {"进度已读取", "Game loaded"},
    {"读取失败（无存档）", "Load failed (no save)"},
    {"集结点已设置", "Rally point set"},
    {"%s已发射", "%s launched"},
    {"选择目标位置（右键取消）", "Select target (right-click to cancel)"},
    {"步兵进驻建筑", "Infantry garrisoning"},
    {"驻军已撤出", "Garrison evacuated"},
    {"车辆前往维修厂", "Moving to service depot"},
    {"路径点模式：右键追加途径点", "Waypoint mode: right-click to append waypoints"},
    {"路径点模式关闭", "Waypoint mode off"},
    {"伞兵", "Paradrop"},
    {"选择伞兵空降点（右键取消）", "Select paradrop zone (right-click to cancel)"},
    {"伞兵已空降", "Paratroopers dropped"},
    {"秘密实验室：解锁%s特色科技", "Secret Lab: %s special tech unlocked"},
    {"科技机场：伞兵支援开始充能", "Tech Airport: paradrop support charging"},
    {"侦察机", "Spy Plane"},
    {"选择侦察机航线目标（右键取消）", "Select spy plane target (right-click to cancel)"},
    {"心灵揭示", "Psychic Reveal"},
    {"选择心灵揭示中心（右键取消）", "Select psychic reveal center (right-click to cancel)"},
    // EVA 播报
    {"警告：侦测到敌方%s", "Warning: enemy %s detected"},
    {"卸载完成", "Unload complete"},
    {"警告：无可靠岸地点，无法卸载", "Warning: no landing zone, cannot unload"},
    {"警告：核弹已发射", "Warning: nuclear missile launched"},
    {"警告：闪电风暴接近中", "Warning: lightning storm approaching"},
    {"超时空传送启动", "Chronosphere activated"},
    {"%s已就绪", "%s ready"},
    {"单位损失", "Unit lost"},
    {"建筑被占领：%s", "Building captured: %s"},
    {"已占领：%s", "Captured: %s"},
    {"工程师已修复：%s", "Engineer repaired: %s"},
    {"警告：基地遭受攻击", "Warning: base under attack"},
    {"警告：采矿车遭受攻击", "Warning: harvester under attack"},
    {"建筑被摧毁：%s", "Building destroyed: %s"},
    {"%s晋升为老兵", "%s promoted to Veteran"},
    {"%s晋升为精英", "%s promoted to Elite"},
    {"间谍渗透：窃取资金 $%d", "Spy: stole $%d"},
    {"警告：精炼厂被间谍渗透，资金失窃", "Warning: refinery infiltrated, funds stolen"},
    {"警告：电厂被间谍破坏，电力瘫痪", "Warning: power plant sabotaged"},
    {"间谍渗透：敌方电力瘫痪 30 秒", "Spy: enemy power down for 30s"},
    {"间谍渗透：已获取敌方雷达数据", "Spy: enemy radar data acquired"},
    {"警告：雷达站被间谍渗透", "Warning: radar infiltrated"},
    {"间谍渗透：新兵营单位直接晋升老兵", "Spy: new infantry start as veterans"},
    {"间谍渗透：新车辆/空军单位直接晋升老兵", "Spy: new vehicles/aircraft start as veterans"},
    {"间谍渗透：新海军单位直接晋升老兵", "Spy: new naval units start as veterans"},
    {"警告：作战实验室被间谍渗透", "Warning: battle lab infiltrated"},
    {"间谍渗透：窃取技术资料，敌方超武充能已重置", "Spy: tech stolen, enemy superweapon charge reset"},
    {"间谍渗透：%s", "Spy infiltrated: %s"},
    {"补给箱：获得资金 $%d", "Crate: +$%d"},
    {"补给箱：全体单位完全治疗", "Crate: all units fully healed"},
    {"补给箱：单位军衔晋升", "Crate: unit promoted"},
    {"补给箱：获得免费单位", "Crate: free unit"},
    {"补给箱：地图已揭示", "Crate: map revealed"},
    {"补给箱：火力增强", "Crate: firepower up"},
    {"补给箱：护甲增强", "Crate: armor up"},
    {"补给箱：速度增强", "Crate: speed up"},
    {"警告：步兵无法承受超时空传送", "Warning: infantry cannot survive chrono shift"},
    {"警告：敌军增援抵达战场", "Warning: enemy reinforcements have arrived"},
    {"间谍渗透：窃取盟方高科，解锁超时空突击队", "Spy: Allied tech stolen, Chrono Commando unlocked"},
    {"间谍渗透：窃取苏方高科，解锁心灵突击队", "Spy: Soviet tech stolen, Psi Commando unlocked"},
    {"警告：超武被间谍渗透，充能已重置", "Warning: superweapon infiltrated, charge reset"},
    {"间谍渗透：敌方超武充能已重置", "Spy: enemy superweapon charge reset"},
    {"心灵控制：已夺取敌方单位", "Mind control: enemy unit captured"},
    {"警告：我方单位被心灵控制", "Warning: our unit is mind-controlled"},
    // 设置页
    {"语言", "Language"},
    {"显示选项", "Display Options"},
    {"游戏选项", "Game Options"},
    {"界面选项", "Interface Options"},
    {"音效选项", "Sound Options"},
    {"显示模式", "Display Mode"},
    {"无边框全屏", "Borderless Fullscreen"},
    {"窗口", "Windowed"},
    {"分辨率", "Resolution"},
    {"跟随桌面", "Desktop Native"},
    {"选项选单", "Options"},
    {"键盘", "Keyboard"},
    {"网络", "Network"},
    {"按键设置", "Key Bindings"},
    {"恢复默认按键", "Reset Defaults"},
    {"请按新按键…（ESC 取消）", "Press a key... (ESC to cancel)"},
    {"点击右侧键位框，然后按下新按键", "Click a key box, then press a new key"},
    {"停止", "Stop"},
    {"卸载", "Unload"},
    {"部署/展开", "Deploy"},
    {"散布", "Scatter"},
    {"警戒", "Guard"},
    {"路径点", "Waypoints"},
    {"选择同类", "Select Same Type"},
    {"编队 I", "Team 1"},
    {"编队 II", "Team 2"},
    {"音乐开关", "Music Toggle"},
    {"返回基地", "View Base"},
    {"暂停", "Pause"},
    {"设集结点", "Set Rally Point"},
    {"出售建筑", "Sell Building"},
    {"快速存档", "Quick Save"},
    {"快速读档", "Quick Load"},
    {"游戏加速", "Speed Up"},
    {"游戏减速", "Speed Down"},
    // LAN 联机（P8）
    {"局域网联机", "LAN Multiplayer"},
    {"主机开局", "Host Game"},
    {"加入游戏", "Join Game"},
    {"等待对手连接…", "Waiting for opponent..."},
    {"对手已加入", "Opponent joined"},
    {"已连接，等待主机开始…", "Connected. Waiting for host..."},
    {"连接失败", "Connection failed"},
    {"开始对战", "Start Battle"},
    {"IP 地址", "IP Address"},
    {"对手已断开连接", "Opponent disconnected"},
    {"同步校验失败：游戏已不同步", "Desync detected"},
    {"你指挥：%s", "You command: %s"},
    // 生产图标悬停提示 / 选中信息面板
    {"造价 $%d · 耗时 %d 秒", "Cost $%d · %ds"},
    {"需要：%s", "Requires: %s"},
    {"资金不足", "Low funds"},
    {"老兵", "Veteran"},
    {"精英", "Elite"},
    {"生命 %d/%d", "HP %d/%d"},
    // 遭遇战地图扩展
    {"微 48x48", "Tiny 48x48"},
    {"超大 160x160", "XL 160x160"},
    {"巨型 200x200", "Huge 200x200"},
    {"史诗 256x256", "Epic 256x256"},
    {"群岛", "Archipelago"},
    {"海岸", "Coast"},
    {"河谷", "River"},
    {"山地", "Mountain"},
    // 界面悬停提示 / 设置开关
    {"界面提示", "UI Tips"},
    {"标准遭遇战：消灭对手即胜。", "Standard skirmish: destroy the enemy to win."},
    {"自由混战：全员敌对，禁止结盟。", "Free-for-all: everyone is hostile; no alliances."},
    {"邪恶联盟：双方各得两辆 MCV，可跨阵营科技。", "Unholy Alliance: two MCVs each and cross-faction tech."},
    {"巨富：禁用矿车与精炼厂，依赖油井收入。", "Megawealth: no miners/refineries; rely on oil derricks."},
    {"抢地盘：全图已探索，用 MCV 抢占扩张。", "Land Rush: map revealed; race to expand with MCVs."},
    {"绞肉机：禁空军/海军/超武，侧重地面战。", "Meat Grinder: no air/navy/superweapons; ground focus."},
    {"海战：小岛海图，强调海军与两栖。", "Naval War: island map focused on navy and amphibious."},
    {"失去全部建筑且无 MCV 即判负（仍可有作战单位）。", "Lose if no buildings and no MCV remain (units alone do not save you)."},
    {"允许建造厂打包回 MCV（永久心控下仍禁止）。", "Allow packing a Construction Yard back into an MCV."},
    {"地图随机生成补给箱强化。", "Random power-up crates spawn on the map."},
    {"全部电脑组成同一队伍（自由混战会强制关闭）。", "All AI share one team (forced off in Free-for-all)."},
    {"盟友共享当前视野与已探索区域。", "Allies share live vision and explored fog."},
    {"允许充能并使用超级武器。", "Allow charging and launching superweapons."},
    {"调整模拟推进速度（慢/普通/快）。", "Simulation speed: Slow / Normal / Fast."},
    {"开局资金档位。", "Starting credits tier."},
    {"地图边长：影响人数上限与寻路范围。", "Map edge length: caps players and pathing span."},
    {"大陆：大片陆地，水域较少。", "Continent: large landmass with little water."},
    {"岛屿：多岛分布，适合海军。", "Islands: many islands; strong navy play."},
    {"湖泊：内陆水域穿插陆地。", "Lake: inland water mixed with land."},
    {"群岛：碎岛密集。", "Archipelago: dense scattered islets."},
    {"海岸：一侧大陆一侧大海。", "Coast: landmass with an ocean flank."},
    {"河谷：水道贯穿陆地。", "River: waterways cutting through land."},
    {"山地：高地起伏更多。", "Mountain: more elevated terrain."},
    {"选择国家：决定阵营与特色单位（随机则开局抽取）。", "Pick a country for faction and bonuses (Random rolls at start)."},
    {"队伍颜色：仅影响显示，不影响阵营。", "House color is cosmetic only."},
    {"电脑难度：影响思考频率、进攻与超武使用。", "AI difficulty: think rate, aggression, and superweapons."},
    {"玩家槽：名称、国家、颜色与（电脑）难度。", "Player slot: name, country, color, and AI difficulty."},
    {"增加一名电脑对手（受地图尺寸人数上限限制）。", "Add a computer opponent (capped by map size)."},
};

const char* TR(S id) {
    int i = (int)id;
    if (i < 0 || i >= (int)S::COUNT) return "?";
    const char* p = TBL[i][g_lang ? 1 : 0];
    // 防御：检测被破坏的指针（空或落入零页的低地址），回退问号避免 TextFormat 崩溃
    if (!p || (uintptr_t)p < 0x10000) return "?";
    return p;
}

// ===================== 外部字符串加载（assets/strings/） =====================
// S 枚举规范名表（与 enum class S 一一对应，INI [Strings] 节的键名）
static const char* kSKey[(int)S::COUNT] = {
    "Back", "On", "Off", "Settings", "Random",
    "GameTitle", "GameSub", "Skirmish", "Campaign", "ExitGame", "MainTip", "MapEditor",
    "MissionN", "ObjSurvive", "ObjEliminate", "ObjTrigger", "ClickEnter",
    "ChangeMap", "CustomizeBattle", "MapSize", "SizeS", "SizeM", "SizeL", "MapType", "MapContinent", "MapIslands", "MapLake",
    "Player", "Color", "Country", "CommanderYou", "ComputerN", "Remove", "AddComputer",
    "StartMoney", "GameSpeed", "SpeedSlow", "SpeedNormal", "SpeedFast", "Volume", "Crates", "AIAlliance", "StartGame",
    "GameMode", "ModeBattle", "ModeFFA", "ModeUnholy", "ModeMegawealth", "ModeLandRush", "ModeMeatGrinder", "ModeNavalWar",
    "ShortGame", "SharedVision", "Superweapons", "McvRepacks",
    "Money", "Power", "LowPower", "NeedBld", "Ready", "ClickTarget",
    "TabBld", "TabDef", "TabInf", "TabVeh", "TabNavy",
    "MsgPlaceBld", "MsgCannotBuild", "MsgQueueBusy", "MsgCanceledOne", "MsgCanceledProd",
    "Repair", "Sell", "Menu", "MsgRepairMode", "MsgSellMode",
    "ObjHoldFmt", "ObjWaveFmt", "ObjElimAll", "SelNFmt", "CargoNFmt", "TipLine",
    "RadarOffline",
    "Paused", "Victory", "Defeat", "GameMenu", "PlayAgain", "Continue", "SaveProgress", "LoadProgress", "Restart", "BackToMain",
    "MsgFindMCVFmt", "MsgLowPower", "MsgEngCapture", "MsgBoarding", "MsgEngRepair", "MsgCannotPlace",
    "MsgSold", "MsgConYardNoSell", "MsgRepaired", "MsgNoRepair", "MsgDeployed", "MsgDeployToggled",
    "MsgScatter", "MsgGuard", "MsgSelSameType", "MsgGroupSetFmt", "MsgMusicOn", "MsgMusicOff",
    "MsgSaved", "MsgSaveFail", "MsgLoaded", "MsgLoadFail", "MsgRallySet", "MsgSWLaunchedFmt", "MsgSelectTargetSW",
    "MsgGarrison", "MsgUngarrison", "MsgService", "MsgWaypointOn", "MsgWaypointOff",
    "Paradrop", "MsgParadropTarget", "EvaParadropDrop", "EvaSecretLabFmt", "EvaAirportCaptured",
    "SpyPlane", "MsgSpyPlaneTarget", "PsychicReveal", "MsgPsychicRevealTarget",
    "EvaDetectEnemySWFmt", "EvaUnloadDone", "EvaUnloadFail", "EvaNukeLaunched", "EvaStormComing", "EvaChronoStart",
    "EvaSWReadyFmt", "EvaUnitLost", "EvaBldCapturedFmt", "EvaCapturedFmt", "EvaEngRepairedFmt",
    "EvaBaseAttack", "EvaHarvAttack", "EvaBldDestroyedFmt", "EvaPromoteVetFmt", "EvaPromoteEliteFmt",
    "SpyStealMoneyFmt", "SpyMoneyVictim", "SpyPowerVictim", "SpyPowerOk", "SpyRadarOk", "SpyRadarVictim",
    "SpyBarracks", "SpyFactory", "SpyNavy", "SpyLabVictim", "SpyLabOk", "SpyGenericFmt",
    "CrateMoney", "CrateHeal", "CrateVet", "CrateUnit", "CrateReveal", "CratePower", "CrateArmor", "CrateSpeed",
    "EvaInfNoChrono", "EvaWaveIncoming",
    "SpyTechChrono", "SpyTechPsi", "SpySWVictim", "SpySWReset", "EvaMindGain", "EvaMindLost",
    "Language", "DisplaySection", "GameOptsSection", "InterfaceSection", "SoundSection",
    "WindowMode", "WMFullscreen", "WMWindowed", "Resolution", "ResDesktop",
    "OptionsMenu", "Keyboard", "NetworkOpts",
    "KeysSection", "ResetKeys", "PressKey", "KeysTip",
    "KaStop", "KaUnload", "KaDeploy", "KaScatter", "KaGuard", "KaWaypoint", "KaSameType", "KaTeam01", "KaTeam02", "KaMusic", "KaViewBase",
    "KaPause", "KaRally", "KaSell", "KaQuickSave", "KaQuickLoad", "KaSpeedUp", "KaSpeedDown",
    "LanGame", "HostGame", "JoinGame", "WaitPeer", "PeerJoined", "WaitHostStart",
    "ConnectFail", "StartBattle", "IpLabel", "PeerLeft", "DesyncWarn", "YourSide",
    "TipCostTimeFmt", "TipRequireFmt", "TipNoMoney", "RankVet", "RankElite", "HpFmt",
    "SizeXS", "SizeXL", "SizeHuge", "SizeEpic",
    "MapArchipelago", "MapCoast", "MapRiver", "MapMountain",
    "UiTips",
    "TipModeBattle", "TipModeFFA", "TipModeUnholy", "TipModeMegawealth",
    "TipModeLandRush", "TipModeMeatGrinder", "TipModeNavalWar",
    "TipShortGame", "TipMcvRepacks", "TipCrates", "TipAIAlliance", "TipSharedVision", "TipSuperweapons",
    "TipGameSpeed", "TipStartMoney", "TipMapSize",
    "TipMapContinent", "TipMapIslands", "TipMapLake", "TipMapArchipelago", "TipMapCoast", "TipMapRiver", "TipMapMountain",
    "TipCountry", "TipColor", "TipDiff", "TipPlayerSlot", "TipAddComputer",
};

static int sKeyByName(const char* s) {
    if (!s) return -1;
    for (int i = 0; i < (int)S::COUNT; i++)
        if (kSKey[i] && !strcmp(kSKey[i], s)) return i;
    return -1;
}

// 覆盖串持久存储（c_str 在单次加载后稳定）
static std::string g_strPool[(int)S::COUNT][2];
static std::string g_unitEnPool[(int)UnitType::COUNT];
static std::string g_bldEnPool[(int)BldType::COUNT];
static std::string g_swEnPool[(int)SWType::COUNT];
static std::string g_factEnPool[4];
static std::string g_countryCnPool[(int)Country::COUNT];
static std::string g_countryEnPool[(int)Country::COUNT];

// ===================== 本地化名称 =====================
static const char* UNIT_EN[(int)UnitType::COUNT] = {
    "MCV", "Harvester",
    "GI", "Conscript", "PLA",
    "Engineer", "Attack Dog", "Spy",
    "Flak Trooper", "Tesla Trooper",
    "Sniper", "Tanya",
    "Desolator", "Chrono Legionnaire", "Guardian GI", "Crazy Ivan",
    "Grizzly Tank", "Rhino Tank", "Type 99 Tank",
    "Flak Track", "IFV",
    "Prism Tank", "Tesla Tank", "Mirage Tank",
    "V3 Launcher", "Apocalypse", "Terror Drone",
    "Intruder", "MiG", "Black Eagle",
    "Kirov Airship", "Rocketeer",
    "Destroyer", "Typhoon Sub", "Aegis Cruiser",
    "Sea Scorpion", "Dreadnought", "Aircraft Carrier", "Amphibious Transport",
    "Chrono Miner", "War Miner",
    "Tank Destroyer", "Terrorist", "Demolition Truck",
    "Nighthawk", "Dolphin", "Giant Squid",
    "Robot Tank", "Battle Fortress", "Hornet",
    "Navy SEAL", "Yuri", "Chrono Commando", "Psi Commando",
    "Initiate", "Brute", "Virus", "Lasher Tank", "Gatling Tank",
    "Magnetron", "Master Mind", "Floating Disc", "Boomer",
    "Boris", "Siege Chopper", "Chaos Drone",
    "Slave", "Slave Miner",
    "Yuri Prime", "Chrono Ivan",
};

static const char* BLD_EN[(int)BldType::COUNT] = {
    "Construction Yard",
    "Power Plant", "Tesla Reactor", "Nuclear Reactor",
    "Barracks", "War Factory", "Ore Refinery",
    "Radar Tower", "Battle Lab",
    "Airforce Command",
    "Naval Yard",
    "Pillbox", "Sentry Gun", "Prism Tower", "Tesla Coil",
    "Flak Cannon", "Grand Cannon",
    "Patriot Missile",
    "Wall",
    "Ore Purifier", "Industrial Plant",
    "Nuclear Silo", "Weather Device", "Iron Curtain",
    "Chronosphere",
    "Tech Oil Derrick", "Hospital", "Machine Shop",
    "Cloning Vat", "Service Depot", "Gap Generator",
    "Spy Satellite", "Psychic Sensor",
    "Battle Bunker", "Tank Bunker",
    "Tech Airport", "Secret Lab", "Civilian House",
    "Bio Reactor", "Gatling Cannon", "Grinder", "Genetic Mutator", "Psychic Dominator",
    "Psychic Tower", "Robot Control Center", "Tech Power Plant", "Tech Outpost",
};

static const char* SW_EN[(int)SWType::COUNT] = {
    "Nuclear Missile", "Lightning Storm", "Iron Curtain", "Chrono Shift",
    "Genetic Mutator", "Psychic Dominator", "Force Shield",
};

static const char* FACTION_EN[4] = {"Allies", "Soviet", "China", "Yuri"};

static const char* COUNTRY_CN[(int)Country::COUNT] = {
    "", "美国", "韩国", "法国", "德国", "英国",
    "苏俄", "古巴", "利比亚", "伊拉克", "中国", "尤里",
};
static const char* COUNTRY_EN[(int)Country::COUNT] = {
    "", "America", "Korea", "France", "Germany", "Great Britain",
    "Russia", "Cuba", "Libya", "Iraq", "China", "Yuri",
};
const char* countryName(Country c) {
    int i = (int)c;
    if (i < 0 || i >= (int)Country::COUNT) return "?";
    return g_lang ? COUNTRY_EN[i] : COUNTRY_CN[i];
}

const char* unitName(UnitType t) {
    int i = (int)t;
    if (i < 0 || i >= (int)UnitType::COUNT) return "?";
    const char* p = g_lang ? UNIT_EN[i] : unitDef(t).name;
    return (!p || (uintptr_t)p < 0x10000) ? "?" : p;
}
const char* bldName(BldType t) {
    int i = (int)t;
    if (i < 0 || i >= (int)BldType::COUNT) return "?";
    const char* p = g_lang ? BLD_EN[i] : bldDef(t).name;
    return (!p || (uintptr_t)p < 0x10000) ? "?" : p;
}
const char* swName(SWType t) {
    int i = (int)t;
    if (i < 0 || i >= (int)SWType::COUNT) return "?";
    const char* p = g_lang ? SW_EN[i] : swDef(t).name;
    return (!p || (uintptr_t)p < 0x10000) ? "?" : p;
}
const char* factName(Faction f) {
    int i = (int)f;
    if (i < 0 || i >= 4) return "?";
    const char* p = g_lang ? FACTION_EN[i] : factionName(f);
    return (!p || (uintptr_t)p < 0x10000) ? "?" : p;
}
// 任务名/简报：中英直接取 MissionDef（内置或 assets/campaigns 加载均含双语字段）
const char* missionName(int i) {
    const MissionDef& md = missionTable()[i];
    return (g_lang && !md.nameEn.empty()) ? md.nameEn.c_str() : md.name.c_str();
}
const char* missionBrief(int i) {
    const MissionDef& md = missionTable()[i];
    return (g_lang && !md.briefEn.empty()) ? md.briefEn.c_str() : md.brief.c_str();
}

// ===================== 外部字符串加载（assets/strings/） =====================
void loadStrings(const char* path, int lang) {
    Ini ini;
    if (!ini.load(path)) {
        TraceLog(LOG_INFO, "RA2 strings: %s not found, using built-in text", path);
        return;
    }
    int col = lang ? 1 : 0;
    int patched = 0;
    if (const Ini::Section* s = ini.find("Strings")) {
        for (const auto& p : s->kv) {
            int id = sKeyByName(p.first.c_str());
            if (id < 0) {
                TraceLog(LOG_WARNING, "RA2 strings: %s [Strings] unknown key %s, skipped", path, p.first.c_str());
                continue;
            }
            g_strPool[id][col] = p.second;
            TBL[id][col] = g_strPool[id][col].c_str();
            patched++;
        }
    }
    // 名称表节：en.ini 覆盖英文旁表；zh.ini 仅 [Country]（中文单位/建筑/超武名由 rules.ini Name= 覆盖）
    auto warnKey = [&](const char* sec, const char* key) {
        TraceLog(LOG_WARNING, "RA2 strings: %s [%s] unknown key %s, skipped", path, sec, key);
    };
    if (lang) {
        if (const Ini::Section* s = ini.find("Unit"))
            for (const auto& p : s->kv) {
                UnitType t;
                if (!unitTypeByName(p.first.c_str(), t)) { warnKey("Unit", p.first.c_str()); continue; }
                g_unitEnPool[(int)t] = p.second;
                UNIT_EN[(int)t] = g_unitEnPool[(int)t].c_str();
                patched++;
            }
        if (const Ini::Section* s = ini.find("Bld"))
            for (const auto& p : s->kv) {
                BldType t;
                if (!bldTypeByName(p.first.c_str(), t)) { warnKey("Bld", p.first.c_str()); continue; }
                g_bldEnPool[(int)t] = p.second;
                BLD_EN[(int)t] = g_bldEnPool[(int)t].c_str();
                patched++;
            }
        if (const Ini::Section* s = ini.find("SW"))
            for (const auto& p : s->kv) {
                SWType t;
                if (!swTypeByName(p.first.c_str(), t)) { warnKey("SW", p.first.c_str()); continue; }
                g_swEnPool[(int)t] = p.second;
                SW_EN[(int)t] = g_swEnPool[(int)t].c_str();
                patched++;
            }
        if (const Ini::Section* s = ini.find("Faction"))
            for (const auto& p : s->kv) {
                Faction f;
                if (!factionByName(p.first.c_str(), f)) { warnKey("Faction", p.first.c_str()); continue; }
                g_factEnPool[(int)f] = p.second;
                FACTION_EN[(int)f] = g_factEnPool[(int)f].c_str();
                patched++;
            }
        if (const Ini::Section* s = ini.find("Country"))
            for (const auto& p : s->kv) {
                Country c;
                if (!countryByName(p.first.c_str(), c)) { warnKey("Country", p.first.c_str()); continue; }
                g_countryEnPool[(int)c] = p.second;
                COUNTRY_EN[(int)c] = g_countryEnPool[(int)c].c_str();
                patched++;
            }
    } else {
        if (const Ini::Section* s = ini.find("Country"))
            for (const auto& p : s->kv) {
                Country c;
                if (!countryByName(p.first.c_str(), c)) { warnKey("Country", p.first.c_str()); continue; }
                g_countryCnPool[(int)c] = p.second;
                COUNTRY_CN[(int)c] = g_countryCnPool[(int)c].c_str();
                patched++;
            }
    }
    TraceLog(LOG_INFO, "RA2 strings: %s loaded, %d entries applied (lang=%d)", path, patched, lang);
}

// ===================== 字符串导出（--export-assets） =====================
// 导出内置文本为 zh.ini / en.ini，作为用户改写的模板
void exportStrings(const char* dir) {
    MakeDirectory("assets");
    MakeDirectory(dir);
    char path[256];
    for (int lang = 0; lang < 2; lang++) {
        snprintf(path, sizeof(path), "%s/%s.ini", dir, lang ? "en" : "zh");
        FILE* f = fopen(path, "wb");
        if (!f) { TraceLog(LOG_WARNING, "RA2 export: cannot write %s", path); continue; }
        fprintf(f, "; OpenRA2 %s strings - edit values to customize; delete keys to fall back to built-in\n",
                lang ? "English" : "Chinese");
        fprintf(f, "[Strings]\n");
        for (int i = 0; i < (int)S::COUNT; i++)
            fprintf(f, "%s=%s\n", kSKey[i], TBL[i][lang]);
        if (lang) {
            fprintf(f, "\n[Unit]\n");
            for (int i = 0; i < (int)UnitType::COUNT; i++)
                fprintf(f, "%s=%s\n", unitTypeKey((UnitType)i), UNIT_EN[i]);
            fprintf(f, "\n[Bld]\n");
            for (int i = 0; i < (int)BldType::COUNT; i++)
                fprintf(f, "%s=%s\n", bldTypeKey((BldType)i), BLD_EN[i]);
            fprintf(f, "\n[SW]\n");
            for (int i = 0; i < (int)SWType::COUNT; i++)
                fprintf(f, "%s=%s\n", swTypeKey((SWType)i), SW_EN[i]);
            fprintf(f, "\n[Faction]\n");
            for (int i = 0; i < 4; i++)
                fprintf(f, "%s=%s\n", factionKey((Faction)i), FACTION_EN[i]);
            fprintf(f, "\n[Country]\n");
            for (int i = 0; i < (int)Country::COUNT; i++)
                fprintf(f, "%s=%s\n", countryKey((Country)i), COUNTRY_EN[i]);
        } else {
            fprintf(f, "\n[Country]\n");
            for (int i = 0; i < (int)Country::COUNT; i++)
                fprintf(f, "%s=%s\n", countryKey((Country)i), COUNTRY_CN[i]);
        }
        fclose(f);
        TraceLog(LOG_INFO, "RA2 export: %s written", path);
    }
}

// ===================== 按键显示名 =====================
const char* keyName(int key) {
    static char buf[4][24]; // 轮换缓冲（同一表达式多次调用安全）
    static int bi = 0;
    bi = (bi + 1) % 4;
    if (key >= KEY_A && key <= KEY_Z) { snprintf(buf[bi], 24, "%c", 'A' + (key - KEY_A)); return buf[bi]; }
    if (key >= KEY_ZERO && key <= KEY_NINE) { snprintf(buf[bi], 24, "%c", '0' + (key - KEY_ZERO)); return buf[bi]; }
    if (key >= KEY_F1 && key <= KEY_F12) { snprintf(buf[bi], 24, "F%d", key - KEY_F1 + 1); return buf[bi]; }
    if (key >= KEY_KP_0 && key <= KEY_KP_9) { snprintf(buf[bi], 24, "KP%d", key - KEY_KP_0); return buf[bi]; }
    switch (key) {
        case KEY_SPACE: return "Space";
        case KEY_ESCAPE: return "Esc";
        case KEY_ENTER: return "Enter";
        case KEY_TAB: return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_INSERT: return "Ins";
        case KEY_DELETE: return "Del";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_PAGE_UP: return "PgUp";
        case KEY_PAGE_DOWN: return "PgDn";
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case KEY_LEFT_SHIFT: return "LShift";
        case KEY_RIGHT_SHIFT: return "RShift";
        case KEY_LEFT_CONTROL: return "LCtrl";
        case KEY_RIGHT_CONTROL: return "RCtrl";
        case KEY_LEFT_ALT: return "LAlt";
        case KEY_RIGHT_ALT: return "RAlt";
        case KEY_MINUS: return "-";
        case KEY_EQUAL: return "=";
        case KEY_LEFT_BRACKET: return "[";
        case KEY_RIGHT_BRACKET: return "]";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_COMMA: return ",";
        case KEY_PERIOD: return ".";
        case KEY_SLASH: return "/";
        case KEY_BACKSLASH: return "\\";
        case KEY_GRAVE: return "`";
        case KEY_KP_ADD: return "KP+";
        case KEY_KP_SUBTRACT: return "KP-";
        case KEY_KP_MULTIPLY: return "KP*";
        case KEY_KP_DIVIDE: return "KP/";
        case KEY_KP_ENTER: return "KPEnter";
        case KEY_CAPS_LOCK: return "CapsLock";
        case KEY_NUM_LOCK: return "NumLock";
        case KEY_SCROLL_LOCK: return "ScrLock";
        case KEY_PRINT_SCREEN: return "PrtSc";
        case KEY_PAUSE: return "Pause";
    }
    return "?";
}

// ===================== 字体字模收集 =====================
void appendAllFontText(std::string& out) {
    for (int i = 0; i < (int)S::COUNT; i++) { out += TBL[i][0]; out += TBL[i][1]; }
    for (int i = 0; i < (int)UnitType::COUNT; i++) { out += unitDef((UnitType)i).name; out += UNIT_EN[i]; }
    for (int i = 0; i < (int)BldType::COUNT; i++) { out += bldDef((BldType)i).name; out += BLD_EN[i]; }
    for (int i = 0; i < (int)SWType::COUNT; i++) { out += swDef((SWType)i).name; out += SW_EN[i]; }
    for (int i = 0; i < 4; i++) { out += factionName((Faction)i); out += FACTION_EN[i]; }
    // 国家名（遭遇战下拉）；此前未收集 → 缺字显示为 ?
    for (int i = 0; i < (int)Country::COUNT; i++) {
        out += COUNTRY_CN[i];
        out += COUNTRY_EN[i];
    }
    for (const MissionDef& md : missionTable()) {
        out += md.name; out += md.brief;
        out += md.nameEn; out += md.briefEn;
        for (const Trigger& t : md.triggers) { out += t.msg; out += t.msgEn; }
    }
    // 窗口标题 / 遭遇战难度下拉（硬编码文案）与杂项
    out += "OpenRA2 - 共和国之辉 复刻";
    out += "简单普通困难残酷";
    out += "EasyNormalHardBrutal";
    out += "主菜单";
    // 开局提示 + 常用全角标点（ini 覆盖后仍须进字模，避免缺字方框）
    out += "找到基地车，按D键展开！";
    out += "，。！？、；：“”‘’（）【】…—";
}
