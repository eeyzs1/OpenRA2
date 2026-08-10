OpenRA2 素材与规则说明（Modding Guide）
=========================================

本目录下所有内容均为游戏启动时加载的外部素材。任何数值、文本、关卡、
音乐都不是写死在程序里的——按本说明修改/添加文件即可定制游戏。
所有文件缺失或键缺失时，游戏自动回退内置默认值，不会崩溃。

重新生成全部模板：ra2.exe --export-assets
重新生成图像/音频：ra2.exe --gen-assets

通用约定
--------
- 所有 INI/TXT 均为 UTF-8（无 BOM）纯文本；';' 或 '#' 起为注释。
- INI 节名与键名区分大小写；同名节会合并，重复键以【首个】为准
  （想改某个值，请直接编辑原有那一行，不要在文件末尾再加一节）。
- 布尔值写法：yes/no、true/false、1/0。

目录结构
--------
assets/
  rules/rules.ini        单位/建筑/超武全部数值（本文件第 1 节）
  rules/projectiles.ini  YR Projectile= 子集（Arcing/ROT/Inviso/Speed；由 projSprite 桥接）
  campaigns/             战役：campaign.ini 列表 + 每关一个 INI（第 2 节）
  strings/zh.ini|en.ini  界面与播报文本（第 3 节）
  music/                 BGM wav + music.ini 播放列表（第 4 节）
  maps/  (游戏根目录)    手工地图 txt，被战役 MapFile= 引用（第 5 节）
  sprites/               全部 PNG 图像（--gen-assets / tools/ra2pack 生成；步兵/建筑仍用）
  voxels/                运行时 VXL/HVA（载具真体素，从 MIX 提取；见 tools/ra2pack）
  palettes/unittem.pal   单位调色板；voxels/voxels.vpl 体素光照表
  sfx/                   全部 WAV 音效（优先从原版 audio.bag 提取；见 tools/ra2pack/gen_audio.py）

载具优先用运行时 VXL 渲染（ra2.exe 启动读 assets/voxels/*.vxl）；缺文件时回退 PNG / 程序化。
诊断样张：ra2.exe --dump-vxl → tools/ra2pack/out/rt_*.png

数值对齐原版：可用 tools/ra2pack/sync_rules_from_mix.py 从安装目录 MIX 的 rules.ini
同步 Cost/HP 到本仓库 assets/rules/rules.ini（需配置 tools/ra2pack/game）。

1. rules/rules.ini —— 数值规则
------------------------------
节类型：
  [Unit.<单位名>]        例：[Unit.Grizzly]
  [Bld.<建筑名>]         例：[Bld.PrismTower]
  [SW.<超武名>]          Nuke / Lightning / IronCurtain / ChronoShift / GeneticMutator / PsychicDominator
  [DeployWeapon.GI|GuardianGI]  美国大兵/重装大兵部署后的武器
  [Warhead.<弹头名>]      SmallArms / AP / HE / HollowPoint / Psychic / Radiation

单位键：
  Name        中文显示名（英文显示名在 strings/en.ini 的 [Unit] 节）
  Cost BuildTime HP Speed Sight
  Speed       每逻辑帧移动 1/Speed 格，【越大越慢】
  Armor       None / Flak / Plate / Light / Medium / Heavy / Wood / Steel /
              Concrete / Special1 / Special2（Building 兼容映射到 Concrete）
  Move        Infantry / Vehicle / Air / Naval / Amphibious
  Factions    All / None，或管道组合：Allies|Soviet|China|Yuri
  Prereq      前置建筑名（None = 仅需生产建筑本身）
  Ammo        弹药数（0=无限；战机打空返航装填）
  Cargo       运载容量（0=非运输）
  Country     国家限制：None=阵营通用；America/Korea/France/Germany/UK/
              Russia/Cuba/Libya/Iraq/China = 仅该国可造
  Weapon.*    主武器（见下）；Elite.* 精英军衔武器（有任何 Elite. 键即启用）

建筑键：
  Name Cost BuildTime HP W H Power Sight Factions Prereq
  Power       正=发电 负=耗电
  Capturable  可否被工程师占领
  Garrison    可进驻步兵数（0=不可进驻）
  Country     同单位
  Weapon.*    防御建筑武器

超武键：Name ChargeTime（充能帧数，30 帧/秒）FromBld（提供建筑）

武器键（前缀 Weapon./Elite.；DeployWeapon 节无前缀）：
  Damage Range Cooldown AntiAir AntiGround Proj
  VsInf VsVeh VsBld     对步兵/车辆/建筑伤害系数
  NavalOnly             仅攻击水上目标
  Splash                溅射半径（格，0=单体）
  Warhead               Legacy（默认沿用 VsInf/VsVeh/VsBld）或上列弹头名
  Proj 贴图名：shell bullet tesla prism missile flak rad chrono psi
              naval torpedo

弹头节：
  Verses=               11 个逗号分隔倍率，顺序为
                        None,Flak,Plate,Light,Medium,Heavy,Wood,Steel,
                        Concrete,Special1,Special2

[GameRules] 官方机制相关键：
  VeteranRatio          每级军衔所需摧毁价值 / 单位自身价值（默认 3）
  VeteranDmgBonus       新兵/老兵/精英伤害百分比
  VeteranArmorBonus     承伤百分比
  VeteranSpeedBonus     移速百分比
  VeteranRofBonus       开火间隔百分比
  VeteranSelfHeal       每次自愈点数
  BioReactorPowerPerOccupant  生化反应堆每名驻军增电
  GrinderRefund         回收炉返还造价比例（1 = 100%）

枚举名（节名后缀/键值共用一套规范名，即 data.h 中的枚举名）：
  单位：MCV Harvester GI Conscript PLA Engineer AttackDog Spy
       FlakTrooper TeslaTrooper Sniper Tanya Desolator Chrono
       GuardianGI CrazyIvan Grizzly Rhino Type99 FlakTrack IFV
       PrismTank TeslaTank MirageTank V3Launcher Apocalypse TerrorDrone
       Intruder MiG BlackEagle Kirov Rocketeer
       Destroyer Typhoon Aegis SeaScorpion Dreadnought AircraftCarrier
       AmphTransport ChronoMiner WarMiner TankDestroyer Terrorist
       DemoTruck Nighthawk Dolphin Squid RobotTank BattleFortress Hornet
       NavySEAL Yuri ChronoCommando PsiCommando
       Initiate Brute Virus LasherTank GatlingTank Magnetron MasterMind
       FloatingDisc Boomer Boris SiegeChopper ChaosDrone
  建筑：ConYard PowerPlant TeslaReactor NuclearReactor Barracks WarFactory
       OreRefinery Radar BattleLab AirForceCmd NavalYard Pillbox SentryGun
       PrismTower TeslaCoil FlakCannon GrandCannon PatriotMissile Wall
       OrePurifier IndustrialPlant NukeSilo WeatherDevice IronCurtain
       ChronoSphere OilDerrick Hospital MachineShop CloningVat ServiceDepot
       GapGenerator SpySat PsychicSensor BattleBunker TankBunker
       TechAirport SecretLab CivHouse
       BioReactor GatlingCannon Grinder GeneticMutator PsychicDominator
       PsychicTower TechPowerPlant TechOutpost
  超武：Nuke Lightning IronCurtain ChronoShift GeneticMutator PsychicDominator
  阵营：Allies Soviet China Yuri

注意：本表只改【数值与名称】。单位/建筑种类本身由程序定义，
不能通过 INI 新增全新种类；但可以通过改 Factions/Prereq/Country/
武器参数做出完全不同的平衡性与科技树。

2. campaigns/ —— 战役与关卡
---------------------------
campaign.ini：
  [Missions]
  Mission=mission01.ini     每行一个，顺序即战役顺序
  Mission=mission02.ini     前 8 关=中国页签，9-16=盟军，17-24=苏军，25-32=尤里
  ...                       增删关卡只需增删行与对应文件

每关文件（例 mission01.ini）：
  [General]
  Name=中文名      NameEn=英文名
  Brief=中文简报   BriefEn=英文简报
  Faction=China             玩家阵营：Allies/Soviet/China/Yuri
  AI=Soviet,Soviet          敌方阵营列表（逗号分隔，个数=AI数）
  MapSize=96                32..256；有 MapFile 时以地图文件为准
  MapType=0                 0 大陆 1 岛屿 2 湖泊（程序生成图）
  Money=9000                初始资金
  Objective=0               0 歼灭敌军 1 坚守至 ObjectiveTick 2 触发器定胜负
  ObjectiveTick=0
  MapFile=maps/xxx.txt      可选：手工地图（第 5 节），省略则程序生成
  NoStartForce=yes          可选：不刷初始基地车部队（全靠地图摆放）

  [Wave.1]                  敌方增援波次（可多个，编号任意）
  At=2700                   触发帧（30 帧/秒，2700=90 秒）
  Units=Conscript,Rhino     单位名逗号分隔，刷出后攻向玩家基地

  [Trig.1]                  触发器（RA2 式地图脚本，可多个）
  Cond=Always               条件：
                            Always        开局即触发
                            Time          C0=帧：时间到
                            PlayerBldLost C0=玩家 C1=建筑枚举值：该建筑全灭
                            PlayerAllDead C0=玩家：被歼灭
                            UnitInRect    C0=玩家 C1..C4=矩形：单位进入
                            MoneyBelow    C0=玩家 C1=金额：资金低于
  C0=0 .. C4=0              条件参数
  Act=Eva                   动作：
                            SpawnAt   A0=玩家 A1,A2=刷出点 A3,A4=攻击移动目标
                                      （A3<0 不移动）；Units= 给出单位列表
                            Eva       Msg= 播报文本
                            GiveMoney A0=玩家 A1=金额
                            RevealMap A0=玩家 A1,A2=圆心 A3=半径
                            Win / Lose            立即胜利/失败
                            Objective Msg= 更新 HUD 目标文本
  A0=0 .. A4=-1
  Units=Yuri,Yuri           SpawnAt 用
  Msg=中文文本  MsgEn=英文   Eva/Objective 用
  Once=yes                  no=可重复触发

3. strings/ —— 界面文本
-----------------------
zh.ini / en.ini 结构相同，游戏双语同时加载、热切换。
  [Strings]   键为内部枚举名（Back、GameTitle、MsgSold……），
              值为显示文本；含 %d %s 的是格式化串，占位符个数不可改。
en.ini 另有：
  [Unit] [Bld] [SW] [Faction] [Country]   各类英文显示名
zh.ini 另有：
  [Country]   国家中文名
注意：单位/建筑/超武的【中文名】由 rules.ini 的 Name= 决定，
不在 zh.ini 里；想汉化/改名请改 rules.ini。

4. music/ —— 音乐
-----------------
运行时扫描本目录全部 .wav 轮换播放。
music.ini（可选）：
  [Playlist]
  Track=industrial_march.wav    每行一个，顺序即轮换顺序
  Track=grind_heavy.wav         未列出的 wav 追加在列表之后
想加 BGM：把 wav 拷进本目录即可（22050Hz 16bit 单声道最佳）。

5. maps/ —— 手工地图（位于游戏根目录 maps/，非 assets/）
--------------------------------------------------------
纯文本命令文件，'#' 起注释。首行命令必须是 size。
  size 96 96                地图尺寸（32..256），只出现一次
  fill clear                全图填充地形
  rect rough 16 30 14 6     矩形地形：类型 x y 宽 高
  blob water 34 44 5        圆形地形：类型 圆心x 圆心y 半径
  deco tree1 6 20 26 22 14  区域内撒装饰：类型 x y 宽 高 数量
  spawn 0 10 10             设置玩家出生点（默认四角+边中）
  unit 0 Tanya 8 82         摆放单位：玩家 类型 x y [guard]
  bld 1 ConYard 72 10       摆放建筑：玩家 类型 x y（玩家 -1=中立）
  地形名：clear rough water ore gems bridge
  装饰名：tree1 tree2 tree3 rock1 rock2
  单位/建筑名：同 rules.ini 规范名
在战役 INI 里用 MapFile=maps/xxx.txt 引用。

6. sprites/ 与 sfx/ —— 图像与音效
---------------------------------
权威来源：用原版 MIX 提取（tools/ra2pack/gen_assets.py / gen_terrain.py）。
文件名为程序约定，例如：
  unit_grizzly_d0_f0.png   单位<方向d0..d7><帧f0..>
  turret_grizzly_d0.png    炮塔×8 方向
  bld_prismtower.png       建筑本体（建造过程优先 mk 帧序列）
  icon_unit_grizzly.png    侧边栏图标（icon_bld_* 建筑图标）
  tile_clear_0.png         地形 tile×8 变体（clear/rough/water/ore/gems/bridge）
  fx_explosion_0.png       特效序列帧；shot.wav 等音效同名 WAV
游戏启动时逐一加载；缺失的文件才由程序化生成兜底。
想替换外观：用同名 PNG/WAV 覆盖即可。
ra2.exe --gen-assets 只补缺失文件，不会覆盖已有 MIX 提取产物。
建筑 SHP 必须保留原画布偏移并以 64/60 对齐引擎瓦片；不要再裁切居中。
