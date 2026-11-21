#pragma once
#include "game/game.h"

// Regression methods keep direct access to Game internals without becoming
// part of its shipping API. Only ra2_regression compiles their definitions.
class GameRegression final : public Game {
public:
    int liveVerify();           // 可见窗口 + 真实 OS 鼠标点选/设置页目检（修复实机选不中）
    void debugMenuShot(const char* file, bool setup); // 菜单截图（验证用）
    void debugGuiReview(const char* outDir); // 各 GUI 审核截图（含主菜单动画样帧）
    int aiAirDefenseRegression();
    int aiAirTransportRegression(const char* outputDir);
    int aiAssaultRegression();
    int aiAttackRegression();
    int aiCleanupRegression();
    int aiConstructionRegression(const char* outputDir);
    int aiEconomyRegression();
    int aiFactoryExitRegression();
    int aiFactoryLayoutRegression(const char* outputDir);
    int aiScoutingRegression();
    int aiTransportRegression();
    int airGroundRegression(const char* outputDir);
    int airProductionRegression(const char* outputDir);
    int aircraftSpawnerRegression(const char* outputDir);
    int animationRegression(const char* outputDir);
    int apocalypseRegression();
    int appPersistenceTest();
    int aswSpawnerRegression(const char* outputDir);
    int attackMoveRegression(const char* outputDir);
    int attackRegression();
    void benchCampaign(int mission, int warmTicks, int frames); // 临时诊断：战役真实渲染耗时（解除帧率上限）
    int blockerRegression();
    int burstRegression();
    int campaignMatrixTest(int frames); // 32 关启动/触发器/胜负静态与运行矩阵
    int campaignSaveRegression();
    int campaignSmokeTest(int mission, int frames); // 战役冒烟测试：开局跑 N 帧，返回失败断言数
    int chopperGroundRegression();
    int chopperTransitionRegression(const char* outputDir);
    int chronoVisualRegression(const char* outputDir);
    int combatPressureRegression(int scenarioFilter=-1, const char* outputDir="review_out/combat-pressure");
    void debugShot(int warmTicks, const char* file); // 遭遇战截图：预热出基地/电厂/单位后拍全屏（验证用）
    int deploymentFeedbackRegression();
    int economyRegression();
    int elevationFeedbackRegression(const char* outputDir);
    int engineerRegression();
    int entityIdentityRegression();
    int entryRegression();
    int factoryStateRegression(const char* outputDir);
    int feedbackVisualRegression(const char* outputDir);
    int fogFeedbackRegression(const char* outputDir);
    int fogKnowledgeRegression(const char* outputDir);
    int generatedMapRegression(bool health,const char* outputDir);
    int groupOrderRegression();
    int hudRegression(const char* outputDir);
    int idleYieldRegression();
    int immobilizedRegression();
    int instantCombatRegression();
    int junctionRegression(int scenarioFilter=-1);
    int lifecycleRegression();
    int magneticLandingRegression(const char* outputDir);
    int magnetronRegression(const char* outputDir="review_out/magnetron");
    int movementRegression();
    int movingYieldRegression();
    int nativeAIRegression(const char* outputDir);
    int nativeAudioRegression();
    int nativeBridgeRegression(const char* outputDir);
    int nativeBridgeRepairRegression(const char* outputDir);
    int nativeCampaignRegression(const char* outputDir);
    int nativeConditionRegression(const char* outputDir);
    int nativeCrateRegression(const char* outputDir);
    int nativeCrewRegression(const char* outputDir);
    int nativeCrewWeaponRegression(const char* outputDir);
    int nativeDemolitionRegression(const char* outputDir);
    int nativeEventRegression(const char* outputDir);
    int nativeFlightRegression(const char* outputDir);
    int nativeGateRegression(const char* outputDir);
    int nativeGatlingRegression(const char* outputDir);
    int nativeGlobalAIRegression(const char* outputDir);
    int nativeGlobalAppRegression(const char* outputDir);
    int nativeGroundImpactRegression(const World& airborne,EID missile,const char* outputDir);
    int nativeHouseLeaderRegression(const char* outputDir);
    int nativeIFVRegression(const char* outputDir);
    int nativeJumpjetRegression(const char* outputDir);
    int nativeMapRegression(const char* outputDir);
    int nativeMissionRegression(const char* outputDir);
    int nativeMotionRegression(const char* outputDir);
    int nativeMovementRegression(const char* outputDir);
    int nativeNearbyRegression();
    int nativeNearbyRuntimeRegression(const char* outputDir);
    int nativePlaythroughRegression(const char* outputDir);
    int nativeProductionRegression(const char* outputDir);
    int nativeRocketRegression(const char* outputDir);
    int nativeSceneRegression(const char* outputDir);
    int nativeSceneryChecks(const char* outputDir);
    int nativeScriptControlRegression(const char* outputDir);
    int nativeShrapnelRegression(const char* outputDir);
    int nativeSourceCoordinateRegression(const char* outputDir);
    int nativeTeamAttackRegression(const char* outputDir);
    int nativeTeamOrdersRegression(const char* outputDir);
    int nativeTeamRegression(const char* outputDir);
    int nativeTemporalRegression(const char* outputDir);
    int nativeV3Regression(const char* outputDir);
    int nativeWeaponActionsRegression(const char* outputDir);
    int nativeWeaponRegression(const char* outputDir);
    int netProtocolRegression();
    int netProductionRegression(const char* outputDir);
    int modalInputRegression(const char* outputDir);
    int classicInputRegression(const char* outputDir);
    int originalSkirmishRegression(const char* outputDir,const char* inspectWorld=nullptr);
    int originalSkirmishNaturalRegression(const char* outputDir,int mapIndex);
    int netSelfTestDriver(int role,int frames,RulesChoice choice=RulesChoice::Fusion,int country=0);
    int placementRegression(const char* outputDir);
    int playTest();             // 自动化完整游玩测试：脚本注入输入，真实窗口跑全流程，返回失败数
    int presentationReplay(bool sound);
    int productionFeedbackRegression(const char* outputDir);
    int rallyRegression();
    int rallyVisualRegression(const char* outputDir);
    int referenceMapRegression(const char* outputDir);
    int refineryRecoveryRegression();
    int refundRegression(const char* outputDir);
    int releaseRegression();
    int rulesConsistencyRegression(const char* outputDir);
    int rulesProfileRegression(const char* outputDir);
    int rulesSelectionRegression(const char* outputDir);
    int rulesSpawnerRegression(const char* outputDir);
    int serviceRegression();
    int siegePoseRegression(const char* outputDir);
    int skirmishRegression(int ticks, int factionFilter=-1, uint64_t seedFilter=0, bool requireOutcomes=false, int generatedMapType=-1);
    int smokeTest(int frames); // 无头冒烟测试，返回失败断言数
    int sourceCrateRegression(const char* outputDir);
    int sourceEconomyRegression(const char* outputDir);
    int groundControlRegression(const char* outputDir);
    int groundTravelRegression(const char* outputDir);
    int sourceMatchRegression(const char* outputDir,int ticks,int editionFilter,int terrainFilter);
    int sourceModeRegression(const char* outputDir);
    int sourceSupportRegression(const char* outputDir);
    int splashRegression();
    int statePersistenceTest(); // 存档 schema/边界/复杂状态 round-trip 与 checksum 覆盖
    int superweaponRegression(const char* outputDir);
    int supportDeliveryRegression(const char* outputDir);
    int terrainVisualRegression(const char* outputDir);
    int trafficRegression(int scenarioFilter=-1);
    int unitVoiceRegression();
    int visualAudit();          // 建筑虚线笼/建造出售动画/地形目检截图（人工或脚本审图）
    int visualRegression(const char* outputDir);
    int voxelAnimationRegression(const char* outputDir);
};
