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
    GameTitle, GameSub, Skirmish, Campaign, ExitGame, MainTip,
    // 战役选择
    MissionN, ObjSurvive, ObjEliminate, ClickEnter,
    // 遭遇战设置
    ChangeMap, MapSize, SizeS, SizeM, SizeL, MapType, MapContinent, MapIslands, MapLake,
    Player, Color, Faction, CommanderYou, ComputerN, Remove, AddComputer,
    StartMoney, GameSpeed, SpeedSlow, SpeedNormal, SpeedFast, Volume, Crates, AIAlliance, StartGame,
    // HUD / 侧边栏
    Money, Power, LowPower, NeedBld, Ready, ClickTarget,
    TabBld, TabDef, TabInf, TabVeh, TabNavy,
    MsgPlaceBld, MsgCannotBuild, MsgQueueBusy, MsgCanceledOne, MsgCanceledProd,
    Repair, Sell, Menu, MsgRepairMode, MsgSellMode,
    ObjHoldFmt, ObjWaveFmt, ObjElimAll, SelNFmt, CargoNFmt, TipLine,
    Paused, Victory, Defeat, GameMenu, PlayAgain, Continue, SaveProgress, LoadProgress, Restart, BackToMain,
    // 局内消息（game.cpp）
    MsgFindMCVFmt, MsgLowPower, MsgEngCapture, MsgBoarding, MsgEngRepair, MsgCannotPlace,
    MsgSold, MsgConYardNoSell, MsgRepaired, MsgNoRepair, MsgDeployed, MsgDeployToggled,
    MsgScatter, MsgGuard, MsgSelSameType, MsgGroupSetFmt, MsgMusicOn, MsgMusicOff,
    MsgSaved, MsgSaveFail, MsgLoaded, MsgLoadFail, MsgRallySet, MsgSWLaunchedFmt, MsgSelectTargetSW,
    // EVA 播报（world.cpp）
    EvaDetectEnemySWFmt, EvaUnloadDone, EvaUnloadFail, EvaNukeLaunched, EvaStormComing, EvaChronoStart,
    EvaSWReadyFmt, EvaUnitLost, EvaBldCapturedFmt, EvaCapturedFmt, EvaEngRepairedFmt,
    EvaBaseAttack, EvaHarvAttack, EvaBldDestroyedFmt, EvaPromoteVetFmt, EvaPromoteEliteFmt,
    SpyStealMoneyFmt, SpyMoneyVictim, SpyPowerVictim, SpyPowerOk, SpyRadarOk, SpyRadarVictim,
    SpyBarracks, SpyFactory, SpyNavy, SpyLabVictim, SpyLabOk, SpyGenericFmt,
    CrateMoney, CrateHeal, CrateVet, EvaInfNoChrono, EvaWaveIncoming,
    // 设置页
    Language, DisplaySection, WindowMode, WMFullscreen, WMWindowed, Resolution, ResDesktop,
    KeysSection, ResetKeys, PressKey, KeysTip,
    KaStop, KaUnload, KaDeploy, KaScatter, KaGuard, KaSameType, KaMusic, KaViewBase,
    KaPause, KaRally, KaSell, KaQuickSave, KaQuickLoad, KaSpeedUp, KaSpeedDown,
    COUNT
};

const char* TR(S id); // 当前语言文本

// 本地化名称（数据表 data.cpp 保留中文原名，英文由旁表提供）
const char* unitName(UnitType t);
const char* bldName(BldType t);
const char* swName(SWType t);
const char* factName(Faction f);
const char* missionName(int i);
const char* missionBrief(int i);

// 按键显示名（raylib 键码 → "S"/"F5"/"Space"…，未知返回 "?"）
const char* keyName(int key);

// 字体字模收集：双语全部可见文本（loadFont 调用，保证任何语言下不缺字）
void appendAllFontText(std::string& out);
