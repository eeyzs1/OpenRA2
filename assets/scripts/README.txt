OpenRA2 Lua 脚本引擎
====================

本目录下的 *.lua 文件在游戏开始时自动加载（遭遇战与战役均生效）。
*.lua.example 仅为示例，不会自动加载；启用时请复制为同名 .lua。
所有函数均可选，缺失则跳过。多个 .lua 文件按文件名顺序加载，共享同一个 Lua 状态。
注意：OnAiThink 若跨世界/重开残留共享状态，会导致 AI 校验和不确定；内置 AI 冒烟测试依赖不加载接管型示例脚本。

一、事件 Hook（定义即生效）
---------------------------
function OnGameStart()                       -- 开局调用
function OnTick(tick)                        -- 每逻辑帧（tick = 帧序号）
function OnUnitKilled(eid, player, unitType) -- 单位死亡
function OnBuildingComplete(eid, player, bldType)  -- 建筑建造完成
function OnBuildingDestroyed(eid, player, bldType) -- 建筑被毁
function OnSuperWeapon(player, swType, x, y) -- 超武释放
function OnPlayerDefeated(player)            -- 玩家被歼灭
function OnTrigger(tag)                      -- 触发器 Act=Script 调用
function OnTriggerCond(tag)                  -- 触发器 Cond=Script 条件检查（返回 bool）
function OnAiThink(player)                   -- AI 决策 hook（返回 true 跳过内置 AI）

二、API（全局表 ra2）
---------------------
ra2.tick()                      当前逻辑帧
ra2.money(player)               玩家资金
ra2.giveMoney(player, amount)   增减资金
ra2.spawnUnit(player, name, x, y)     生成单位，返回 eid（失败 -1）
ra2.spawnBuilding(player, name, x, y) 生成建筑，返回 eid（失败 -1）
ra2.killEntity(eid)
ra2.damageEntity(eid, dmg, byPlayer)
ra2.countUnits(player, name)
ra2.countBlds(player, name)
ra2.hasBld(player, name)        返回 bool
ra2.eva(player, text)           对指定玩家显示 EVA 消息
ra2.evaAll(text)                对全体玩家显示 EVA 消息
ra2.setObjective(text)          设置任务目标文本
ra2.win()                       判定胜利
ra2.lose()                      判定失败
ra2.revealMap(player, x, y, radius)  临时揭示地图区域
ra2.entityPos(eid)              返回 x, y（无效返回 nil）
ra2.entityHp(eid)
ra2.entityPlayer(eid)
ra2.entityType(eid)             返回 "unit:Grizzly" / "bld:WarFactory" / nil
ra2.findEnemy(player, x, y, maxR)   返回最近敌方 eid（无 -1）
ra2.moveTo(eid, x, y)           指令移动
ra2.attack(eid, targetEid)      指令攻击
ra2.stop(eid)                   指令停止
ra2.gameMode()                  0=遭遇战 1=战役
ra2.missionId()                 当前战役关卡（0-based，遭遇战返回 -1）

三、单位/建筑名称（spawn/count 等函数的 name 参数）
--------------------------------------------------
单位：MCV, Harvester, GI, Conscript, PLA, Engineer, AttackDog, Spy,
      FlakTrooper, TeslaTrooper, Sniper, Tanya, Desolator, ChronoLegionnaire,
      GuardianGI, CrazyIvan, Grizzly, Rhino, Type99, FlakTrack, IFV,
      PrismTank, TeslaTank, MirageTank, V3, Apocalypse, TerrorDrone,
      Intruder, MiG, BlackEagle, Kirov, Rocketeer, Destroyer, Typhoon,
      Aegis, SeaScorpion, Dreadnought, Carrier, AmphibiousTransport,
      ChronoMiner, WarMiner, TankDestroyer, Terrorist, DemoTruck,
      Nighthawk, Dolphin, GiantSquid, RobotTank, BattleFortress, Hornet,
      NavySEAL, Yuri, ChronoCommando, PsiCommando,
      Initiate, Brute, Virus, Lasher, GatlingTank, Magnetron, MasterMind,
      FloatingDisc, Boomer

建筑：ConYard, PowerPlant, TeslaReactor, NuclearReactor, Barracks, WarFactory,
      OreRefinery, Radar, BattleLab, AirforceCmd, NavalYard,
      Pillbox, SentryGun, PrismTower, TeslaCoil, FlakCannon, GrandCannon,
      Patriot, Wall, OrePurifier, IndustrialPlant,
      NukeSilo, WeatherDevice, IronCurtain, Chronosphere,
      OilDerrick, Hospital, MachineShop, CloningVat, ServiceDepot,
      GapGenerator, SpySat, PsychicSensor, BattleBunker, TankBunker,
      TechAirport, SecretLab, CivHouse,
      BioReactor, GatlingCannon, Grinder, GeneticMutator, PsychicDominator

四、示例脚本
-----------
example_events.lua    —— 事件 hook 演示（开局奖励、单位阵亡通报）
example_triggers.lua  —— 自定义触发器条件与动作
example_ai.lua        —— AI 决策 hook（脚本接管 AI）
