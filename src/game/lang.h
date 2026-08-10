#pragma once
// 界面国际化（中英双语）：字符串表 + 本地化名称助手。
// 语言热切换：g_lang 改变即刻生效（字体字模双语全量预载，无需重启）。
#include "game/data.h"
#include <string>

// 当前语言：0 中文 1 English（由设置页/配置文件驱动）
extern int g_lang;

// 字符串表 ID（界面所有可见文本；世界内 EVA 播报亦走此表）
enum class S : int {
    // 通用
    Back, On, Off, Settings, Random,
    // 主菜单
    GameTitle, GameSub, Skirmish, Campaign, ExitGame, MainTip, MapEditor,
    // 战役选择
    MissionN, ObjSurvive, ObjEliminate, ObjTrigger, ClickEnter,
    // 遭遇战设置
    ChangeMap, CustomizeBattle, MapSize, SizeS, SizeM, SizeL, MapType, MapContinent, MapIslands, MapLake,
    Player, Color, Country, CommanderYou, ComputerN, Remove, AddComputer,
    StartMoney, GameSpeed, SpeedSlow, SpeedNormal, SpeedFast, Volume, Crates, AIAlliance, StartGame,
    GameMode, ModeBattle, ModeFFA, ModeUnholy, ModeMegawealth, ModeLandRush, ModeMeatGrinder, ModeNavalWar,
    ShortGame, SharedVision, Superweapons, McvRepacks,
    // HUD / 侧边栏
    Money, Power, LowPower, NeedBld, Ready, ClickTarget,
    TabBld, TabDef, TabInf, TabVeh, TabNavy,
    MsgPlaceBld, MsgCannotBuild, MsgQueueBusy, MsgCanceledOne, MsgCanceledProd,
    Repair, Sell, Menu, MsgRepairMode, MsgSellMode,
    ObjHoldFmt, ObjWaveFmt, ObjElimAll, SelNFmt, CargoNFmt, TipLine,
    RadarOffline,
    Paused, Victory, Defeat, GameMenu, PlayAgain, Continue, SaveProgress, LoadProgress, Restart, BackToMain,
    // 局内消息（game.cpp）
    MsgFindMCVFmt, MsgLowPower, MsgEngCapture, MsgBoarding, MsgEngRepair, MsgCannotPlace,
    MsgSold, MsgConYardNoSell, MsgRepaired, MsgNoRepair, MsgDeployed, MsgDeployToggled,
    MsgScatter, MsgGuard, MsgSelSameType, MsgGroupSetFmt, MsgMusicOn, MsgMusicOff,
    MsgSaved, MsgSaveFail, MsgLoaded, MsgLoadFail, MsgRallySet, MsgSWLaunchedFmt, MsgSelectTargetSW,
    MsgGarrison, MsgUngarrison, MsgService, MsgWaypointOn, MsgWaypointOff,
    Paradrop, MsgParadropTarget, EvaParadropDrop, EvaSecretLabFmt, EvaAirportCaptured,
    SpyPlane, MsgSpyPlaneTarget, PsychicReveal, MsgPsychicRevealTarget,
    // EVA 播报（world.cpp）
    EvaDetectEnemySWFmt, EvaUnloadDone, EvaUnloadFail, EvaNukeLaunched, EvaStormComing, EvaChronoStart,
    EvaSWReadyFmt, EvaUnitLost, EvaBldCapturedFmt, EvaCapturedFmt, EvaEngRepairedFmt,
    EvaBaseAttack, EvaHarvAttack, EvaBldDestroyedFmt, EvaPromoteVetFmt, EvaPromoteEliteFmt,
    SpyStealMoneyFmt, SpyMoneyVictim, SpyPowerVictim, SpyPowerOk, SpyRadarOk, SpyRadarVictim,
    SpyBarracks, SpyFactory, SpyNavy, SpyLabVictim, SpyLabOk, SpyGenericFmt,
    CrateMoney, CrateHeal, CrateVet, CrateUnit, CrateReveal, CratePower, CrateArmor, CrateSpeed,
    EvaInfNoChrono, EvaWaveIncoming,
    SpyTechChrono, SpyTechPsi, SpySWVictim, SpySWReset, EvaMindGain, EvaMindLost,
    // 设置页
    Language, DisplaySection, GameOptsSection, InterfaceSection, SoundSection,
    WindowMode, WMFullscreen, WMWindowed, Resolution, ResDesktop,
    OptionsMenu, Keyboard, NetworkOpts,
    KeysSection, ResetKeys, PressKey, KeysTip,
    KaStop, KaUnload, KaDeploy, KaScatter, KaGuard, KaWaypoint, KaSameType, KaTeam01, KaTeam02, KaMusic, KaViewBase,
    KaPause, KaRally, KaSell, KaQuickSave, KaQuickLoad, KaSpeedUp, KaSpeedDown,
    // LAN 联机（P8）
    LanGame, HostGame, JoinGame, WaitPeer, PeerJoined, WaitHostStart,
    ConnectFail, StartBattle, IpLabel, PeerLeft, DesyncWarn, YourSide,
    // 生产图标悬停提示 / 选中信息面板
    TipCostTimeFmt, TipRequireFmt, TipNoMoney, RankVet, RankElite, HpFmt,
    // 遭遇战地图扩展（追加在末尾，避免打乱既有 S 序号）
    SizeXS, SizeXL, SizeHuge, SizeEpic,
    MapArchipelago, MapCoast, MapRiver, MapMountain,
    // 界面悬停提示 / 设置开关（追加在末尾）
    UiTips,
    TipModeBattle, TipModeFFA, TipModeUnholy, TipModeMegawealth,
    TipModeLandRush, TipModeMeatGrinder, TipModeNavalWar,
    TipShortGame, TipMcvRepacks, TipCrates, TipAIAlliance, TipSharedVision, TipSuperweapons,
    TipGameSpeed, TipStartMoney, TipMapSize,
    TipMapContinent, TipMapIslands, TipMapLake, TipMapArchipelago, TipMapCoast, TipMapRiver, TipMapMountain,
    TipCountry, TipColor, TipDiff, TipPlayerSlot, TipAddComputer,
    COUNT
};

const char* TR(S id); // 当前语言文本

// 外部字符串加载（assets/strings/zh.ini / en.ini，lang: 0 中文 1 English）
// 键名为 enum class S 的枚举名（如 "Back"、"GameTitle"）；缺失键回退内置文本。
// 启动时双语均加载（字体字模需双语全量预载）。
// en.ini 另有 [Unit]/[Bld]/[SW]/[Faction]/[Country] 英文名称节；zh.ini 另有 [Country] 节。
void loadStrings(const char* path, int lang);

// 字符串导出（--export-assets）：把内置文本写成 zh.ini / en.ini 模板
void exportStrings(const char* dir);

// 本地化名称（数据表 data.cpp 保留中文原名，英文由旁表提供）
const char* unitName(UnitType t);
const char* bldName(BldType t);
const char* swName(SWType t);
const char* factName(Faction f);
const char* countryName(Country c);
const char* missionName(int i);
const char* missionBrief(int i);

// 按键显示名（raylib 键码 → "S"/"F5"/"Space"…，未知返回 "?"）
const char* keyName(int key);

// 字体字模收集：双语全部可见文本（loadFont 调用，保证任何语言下不缺字）
void appendAllFontText(std::string& out);
