# 普通地图的原始规则包

普通入口默认RA2与美国，融合和YR仍可手动选择。来源基线是`ra2-local-base`，规则来自`ra2.mix/local.mix`，SHA-256为`fd1e95cea0306ea78049dc81c8cd816e18c28c496872a1ff02edd50bd082062f`。当前普通RA2为Scene69 / NativeProduction11，用World77保存建筑初始阵营、占领历史和出售阶段，并封装既有64..74主体及COW1/TUN1；YR仍为Scene43 / NativeProduction6 / World60，作者场景及冻结来源AI各保留自身合同。App11/12 / LAN96及各World布局见[当前架构](architecture.md)。

当前普通新局从`Riparius.Value`与`Cruentus.Value`读取25/50矿价。旧Scene37及缺省旧Scene42存档保留当时35/70。地图Scene42用`RulesBaseVersion=43/44/45/46`区分价格、采集、停靠和行驶语义。满载CMIN普通矿500、宝石1000，HARV普通矿1000、宝石2000。Production8起按类型整批卸矿，矿石先于宝石。每批的有效供电达到需求时，精炼器按当批数量计算增益并截断。拥有一个精炼器且两批均足电时，10矿石加10宝石实际入账937。

RA2新局的Production7保存`HarvesterLoadRate`，缺省2原版帧/阶段，9阶段为一次取矿；对应项目默认36 tick。旧Scene43及其Scene42地图快照保留20 tick，YR当前也保持原节奏。此项依据实际本地RA2采矿核心的隔离调用，不包含完整任务调度或停靠复刻。

Production8继续保留上述采集计时，并增加`HarvesterDumpFrames`、`PurifierBonus`和每个生产类型的`ROT`。原程序默认`HarvesterDumpRate=.016`分钟，乘900后向上取整得到15个原版帧，即30项目tick；原版库存按首个非空类型整批扣除和入账，空库存再经过一个卸载周期后离开。停靠坐标为建筑基格+(3,1)，进出点为+(4,1)，车头为16位方向0x4000。原程序隔离调用、反汇编位置、原始hash与528个转向样本保存在`tests/fixtures/ra2-ground-reference.json`。这不等同完整原版Drive曲线、无线电调度或原版整局运行。旧Scene44/Production7续档保留旧停靠、卸载与八方向转向。

Production9的初次卸矿在待付计时前加入一个原版启动帧。原版已建联泊位的任务实测首笔在第16帧、混装第二笔在第31帧，项目对应第32、62 tick。历史规则以及旧档里已经开始的卸矿按原计时继续。

Production9启用项目自己的履带转弯曲线和超时空矿车回厂时序。履带车辆按弧长消耗每tick的行驶预算，车头随弧线切线更新。原地对正和炮塔转向使用ROT计时。传送落点获准后的首次移动更新交给停靠。武装矿车收到回厂命令后向西入泊，跨入泊位格时请求转东。Scene45存档中的矿车继续按旧规则立即传送。

支援与经验批使用过 Scene30 / NativeProduction4 / World56 / App11 / LAN46，详见 [支援与经验](source-support-and-veterancy.md)。其前一经济与间谍批使用 Scene29 / NativeProduction3 / LAN45 和 World55 / App11。原始 Soylent、反应炉吸收、克隆联动与间谍矩阵见 [来源经济与间谍](source-economy-and-espionage.md)。下述全量 151/151 与 ASan 23/23 属于更早的 Scene28 批，保留为历史结果。

本批保留来源对象的 `RequiresStolen*Tech`、`FactoryPlant`、五类 `CostBonus` 以及 `SuperWeapon/SuperWeapon2` 绑定。能力按原始 `Type` 识别，RA2 的 `MultiSpecial` 与 YR 的 `NukeSpecial` 都是核弹；RA2 不从雷达或实验室取得 YR 的侦察机或力场护盾。缴获建筑按自身定义提供能力；实验室渗透另区分版本：RA2 原函数按当前所属阵营，YR 按原始实验室科技身份。

来源单位/建筑报价、侧栏和普通 AI 建造预算使用 World 的共同价格查询。实际订单保存开工报价，工业工厂失电、出售或易主只改变后续报价；取消精确退还已付款。当前单厂例为犀牛 900→675。多厂倍率处理的技术参考见 [Phobos 的 FactoryPlant 说明](https://github.com/Phobos-developers/Phobos/blob/develop/docs/Fixed-or-Improved-Logics.md#factoryplant-customizations)，仍不能据此宣称所有价格舍入和原版生产时间已实机校准。

生产列表、超武和支援共用侧栏网格及滚动。`ra2.rules_consistency` 的 77 条断言覆盖三种规则真实点击/取消/发射、来源渗透、实际扣款/退款、订单续档、合法高倍率规则下的 AI 预算，以及两种实际显示尺寸。

整局验证另发现原始 SREF 使用 `IsChargeTurret=yes`、`WeaponCount=1`、`Weapon1/EliteWeapon1`，旧导入器只查 `Primary/ElitePrimary`，两种来源包都漏掉光棱坦克武器。Scene28 导入该单武器布局，普通/精英真实攻击和续档已验证。旧 YR 陆地图在 180000 tick 时积累 70 辆无武器光棱，续跑到 360000 仍未结算；修复后同开局自然运行，180000 tick 后实际续档在 189081 结算。该对局没有改资金、地形或胜负条件，测试观察窗口改为 240000 tick 并继续要求引擎自然胜负。现有通用光棱分裂效果仍是简化实现，不能据此宣称已经校准原始碎片弹道。

上一 Scene28 批最终 Release 全量 151/151（1640.03 秒）、相关 ASan 23/23（1747.84 秒）通过，均无失败或跳过，未报告 ASan 内存错误。10728 项冻结输入在两轮验证中未变化，121 组实际 World55/App11 存档跨构建逐字节相同；范围为四局自然对局的开局、最终状态、原始/再次保存的应用检查点，以及折扣活跃订单的应用存档。完整证据在 `review_out/implementation-20261116/`，检查脚本、逐项日志及哈希分别见 `compare_saves.py`、`final-*-tests.log`、`cross-build-saves.json` 和 `final-input-check.json`。

首轮完整 Release 为 150/151，唯一失败为上述 YR 陆地图观察超时；期间 10728 项冻结输入未变化。该成绩只作为修复前历史，不计入上述最终全量。

后续回归在 RA2 陆地图第 117000 tick 发现一次真实 App11 读取失败：死亡的 FV 保留待发连射，目标也死亡后，读取器将该惰性历史状态当成活跃攻击拒绝。死亡清理现在会在过滤死亡实体之前取消连射；读取旧快照时允许死亡实体引用合法范围内的退休槽位，仍拒绝越界 ID，并保留存档中的原始状态和校验值。三条新增回归覆盖立即保存、历史惰性字段及损坏存档隔离，修改前其中两条失败；原失败 App11 已成功读入并复演 90 tick。该轮全量在完成 87 项（86 通过、1 失败）后主动停止修复，不计作完整全量结果。

自然对局夹具现在使用明确国家与原始开局配置，并在每个检查点执行保存、读取、再次保存、再次读取及后续逐 tick 对照；这同时检验正常 App11 配置链路，避免仅用省略国家的底层 World 夹具代替实际应用保存。上一 Scene28 批两种构建的最终自然结算帧均为 RA2 陆地 105845、RA2 岛屿 90264、YR 陆地 199006、YR 岛屿 81458，全部检查点复演一致。它们与此前省略国家的历史诊断属于不同夹具；四局样本不能证明阵营平衡，也不替代 4/8 人、其余地图及人工整局验收。

用户已明确排除影片恢复。规则、任务原文、语音、输入、生产和存档继续验收。

本批把原始规则接到普通地图的 World、实体、武器、生产、地面移动、AI 和渲染路径，已提供可执行专项。正常设置页和联机大厅已开放融合、RA2 基础与 YR 基础选择，开局、存档、重开和包一致性见 [规则选择](rules-selection.md)。基础规则的其余原版行为仍在完善。

## 来源与重建

从项目本地安装的 MIX 重建：

```powershell
python tools/ra2pack/source_rules.py ra2 --profile --output assets/rules/profiles/ra2.ini
python tools/ra2pack/source_rules.py yr --profile --output assets/rules/profiles/yr.ini
```

完整 `gen_assets.py` 也会导出两个规则包。输出属于可重建的本地素材，不入库。去掉 `--profile` 可导出带原始字段的 JSON 清单；显式 `--scenario all01t.map` 才应用任务覆盖，不能与 `--profile` 混用。

RA2 从 `ra2.mix/local.mix` 选择 `rules.ini` 和 `art.ini`。YR 分别优先使用 `expandmd01.mix` 内对应文件，否则使用 `ra2md.mix/localmd.mix` 基础包；当前本地来源标识是 `yr-local-base`。归档路径不是版本真实性证明，不据基础包宣称已取得 YR 1.001。规则包保留规则、建筑画布来源及各自 SHA-256。

原始基础 `M1Carbine.Damage=15`，第一关地图覆盖为 10。GI 基础为 15/40 ticks，动员兵为 15/50 ticks，灰熊为 65/120 ticks。原始 ROF 的单位是 15Hz 帧，运行时使用 30Hz tick；关卡和基础值保持独立。

## 运行边界

当前普通包导出 Scene43，使用 `Mode=Rules`、空 `Map`、零 `Houses/Actors` 表示普通对局规则；继续读取受支持的旧嵌入快照，并保留其中的定义。作者关卡仍使用Scene16，普通原版遭遇战使用Scene42并显式记录基础规则版本。规则包在创建开局实体前验证，一局中不可替换；国家列表也在此时校验，RA2 不接受中国或尤里，YR 基础包不接受中国。规则入口、固定翼、Jumpjet 与持续装卸分别见 [规则选择](rules-selection.md)、[固定翼记录](native-flight.md)、[升降飞行记录](native-jumpjet.md) 和 [飞行与地面交互](native-air-ground.md)。

原始单位和建筑绑定不可变的 `NativeActorDefinition`。`rulesOnly` 让这些实体继续使用普通地图的图片、队色、选中和放置路径，同时复用原生武器、生产和移动模块。原版地图的演出、外交、任务终止逻辑不会被带入普通对局。

共享 `Owner` 字段不能区分盟苏矿厂或兵营：原始建筑经常允许所有国家拥有。普通类型接口结合 `AIBasePlanningSide`、国家允许/禁止列表选择对应来源对象。苏军工程师、尤里工程师、阵营 MCV、原始矿车和缴获建造厂的打包/展开关系保留独立身份；生产队列保存原始类型编号，AI 不再将它截断成普通枚举。

World55 起保存完整规则包文本、实体/乘员绑定、生产序号、固定飞机槽位、飞行状态、装卸待办、有限重试和随机箱剩余寿命；App11 另保留应用状态与开局配置。恢复时从快照解析定义，重开也使用保存的种子和嵌入规则。当前 LAN46，普通大厅已接通融合／RA2／尤里规则，四个握手阶段核对规则选择与有效内容摘要。旧程序会拒绝不认识的格式与模拟版本；完整多人能力仍在后续清单中。

MCV 展开和建造厂打包按来源对象的最大生命保留受损比例，至少保留 1 点生命；建筑完成 Lua 回调也直接看到转换后的生命。转换全过程隔离退休编号，避免嵌套回调复用尚在处理的实体。

## 普通地图导弹发射器

Scene18 批次接入 RA2 的 DRED/DMISL、V3/V3ROCKET，以及 YR 的 BSUB/CMISL；当时来源类型数为 101、144，加入 ASW 后当前为 102、145。使用各版本基础规则，不混入第一关覆盖：DMISL 为 50 生命、special_2、Altitude=768，基础 DRED 普通伤害为 300；第一关仍保留自身的 75 生命和 1550 高度。

发射器复用实际导弹实体、独立槽位、暂停/抬头/飞行、防空拦截与既有存档状态。Boomer 的主武器是鱼雷，副武器才是生成器；导出器按 `Spawner=yes` 选择武器槽。运行时对舰船发射普通鱼雷，对地面目标使用射程 20、ROF 50 的 CruiseLauncher，发射后进入潜艇暴露计时。

专用舰体与导弹美术从所属版本的原始 VXL/HVA 重建，位于 `assets/rules/profiles/<RulesId>/actors/`，旁边的 `manifest.json` 保留原始文件来源和哈希。每种导弹生成 17 档俯仰 × 8 朝向；NoSpawnAlt 发射器另有空载舰体。普通地图队色和既有地图美术目录保持各自的路径。重构后的 DMISL 导出曾逐张对照既有地图的 136 帧，字节一致。

## 已验证与限制

专项入口：

```powershell
ctest --test-dir build -C Release -R '^ra2\.(rules_profile|rules_selection|rules_spawners|aircraft_spawners|asw_spawners|native_flight|source_rules|deployment_feedback|entity_identity|net_protocol|net_profiles)$' --output-on-failure
```

`ra2.rules_profile` 覆盖来源数值、国家和阵营别名、开局实体、原版计费、普通 MCV 指令、缴获科技转换、两个规则包的十分钟 AI、生产/移动/战斗续档、生成地图水面速度、三个独立舰载机乘员、实际建筑点选、侧栏生产、放置回执、双分辨率渲染及应用存读档。`source_rules` 对照本地原始文件与第一关任务覆盖，并独立重建规则包检查来源、特殊射程、阵营和计时元数据。

同时修正生成地图的高度点选：旧 `surfaceAt` 只为原版地图逆投影高度，而普通地图同样绘制高差。现在点选和放置使用可见/已知地面的相同投影，隐藏地形仍不参与反查。

`ra2.rules_spawners` 覆盖五种版本/发射器组合：普通攻击指令生成真实导弹，暂停和飞行分别保存恢复，爱国者以实际防空弹击落导弹，原始机体在两个分辨率绘制；另检查 Boomer 鱼雷分流。展示夹具只在渲染副本打开视野，不改变受测 World 的正常视野。`rules_profile` 另读取归档的实际 World49/Scene17 应用存档，确认升级后仍保留快照中的旧规则。

尚待完成：

- 固定翼完整任务、起降与高度，Jumpjet 完整任务／地形／坠毁、超时空移动和完整地形速度的原始行为校准；固定翼水平和 Jumpjet 健康运动核心已建立原程序对照。
- 舰载机与反潜机的起降、集结与整备逐帧校准、特殊弹头与能力；导弹精确时序、轨迹和专属表现仍需原版校准。
- 国家成本/产能修正、工业工厂价格与生产计时的完整实机校准、特殊模式、缴获科技的全部生产与美术差异。Scene29 已导入原始 `Soylent`，并验证克隆 GI 与回收的实际联动；其余国家经济倍率、受损与缴获等完整边界继续校准。
- Scene30 / World56 已拆分四种支援冷却及按钮，实际载荷和旧快照兼容见 [支援与经验](source-support-and-veterancy.md)。完整运输机和降落任务仍待接入。Scene29 的 Spyable、50% 资金转移、2000 tick 停电、雷达重遮蔽、可晋级生产和分版本实验室已接入，见 [来源经济与间谍](source-economy-and-espionage.md)。全部复合建筑的效果优先级与独立 YR 二进制校准仍需继续；融合及旧快照保留历史行为。
- 进一步校准特殊模式及完整对局的原版行为；规则入口、开局配置与大厅包一致性已接通。
- 完整对局输入、AI、模拟、渲染联动，以及人工手感与其余战役验收。

测试中的来源包对局不能当成完整原版模式或完整游戏帧率证明。分批成绩与最终冻结输入以 `review_out/project-review-20261115/` 的验收记录为准。

上一批 Scene17 的 Release 全量 131/131 通过，1073.80 秒；ASan 相关五项通过，163.82 秒。当时冻结的 368 项输入及程序未变化，实际应用存档为 312437 字节。历史记录保留于本地 `review_out/project-review-20261115/rules-profile-validation.json`，不能作为本批 Scene18 的全量成绩。

本批 Scene18 全量首轮为 131/132，1171.89 秒；唯一失败是 `ra2.state` 仍假设 MCV 原编号立即变成建造厂，与转换期间的编号隔离冲突。仅修正该测试：分别要求源槽和替代槽递增代次、安全复用，并验证占用链。随后相关 Release 8/8 通过，29.21 秒；未再次运行完整 132 项，因此不记作一次性全量通过。

Scene18 批初步 ASan 六项通过，169.95 秒；修正状态夹具后的五项通过，65.78 秒，未报告内存错误。当时两种构建的应用存档为 316812 字节且逐字节相同，最终 1207 项输入与首轮全量相比仅有状态测试变化。历史证据保留在本地 `review_out/project-review-20261115/source-spawners-validation.json`；本批后续修改不套用该冻结成绩。

Scene19 接通 CARRIER/HORNET 固定槽位：出航继续占位、同波准备、真实攻击、移动母舰返航、独立整备和损失再生，保留来源身份与经验。现有飞机重新出航不占用新的生产序号，己方点选/框选及直接命令不接管受控飞机。新融合对局也使用此控制器；旧 World49/Scene17/18 快照保留历史舰载机行为。状态、原始依据、专项和剩余飞行/ASW 边界见 [舰载机记录](aircraft-spawners.md)，本批成绩独立保存。

Scene20 / World51 / LAN34 接通 DEST/ASW：水下目标走原始虚拟副武器和真实深水炸弹，水面/陆地目标保留舰炮，实际飞机占一个固定槽并使用 800/300 tick 的损失再生/整备。源槽保留 Range=-2、AG=no；ASW 与 DESTWO 均有专用原版素材。旧 World50/Scene19 应用快照继续使用原航母状态机与旧驱逐舰行为，原版地图 Scene16 和 App10 保持。当前专项、完整回归和剩余边界分别记录于舰载机说明及本地 asw-validation.json，未把历史阶段的成绩计入当前版本。

App11 / LAN35 接通正常规则入口与开局配置；SourceScene20 / World51 保持。当前 UI、真实 TCP、两端规则差异与版本边界见 [规则选择](rules-selection.md)，单独记录本批结果。

Production10保存来源建筑维修的BuildingRepairFrames=14、BuildingRepairStep=8和各类型BuildingRepairCost。默认每28逻辑tick按全局相位修复8HP，未满一步仍收完整费率。导出器执行已隔离校准的整数除法及x87截断，保留原型造价扣除FreeUnit与SeparateAircraft分支；46种本地来源建筑的派生费用均与原函数执行对照。费用属于导出合同，修改原始造价、生命或维修参数后应重新导出规则。旧Production9及更早不能携带这些新字段；冻结Scene41导出会剥离它们。

Production11在每个NativeProduction.Type段保存BuildingSaleHuman与BuildingSaleOther；非建筑必须为零，缺失、负数、越界与伪署旧Scene/Production均拒绝。RA2导出来自本地原版返款查询：非零Soylent直接覆盖，否则BuildingType有效造价扣除FreeUnit，人类按RefundPercent返款，其他普通房屋保留有效造价。本地国家的CostBuildingsMult/CostDefensesMult默认1；任意模组或作者房屋倍率未纳入本合同。默认精炼厂返300且赠车保留；HP不缩减正常返款。冻结Scene41 / Production5剥离新增字段，YR43 / Production6保持原包。详见tests/fixtures/ra2-building-sale-reference.json。

Scene63修正建造厂出售准入及输入映射，Production11派生价格和类型字段保持不变；与Scene62规则包相比仅Scene版本头变化。普通人类建造厂返1500、其他普通房屋返3000，沿用完成时入账。普通现金出售不受MCV Repacks开关影响，部署打包仍走原开关及转换路径。旧Scene62包和YR/融合/作者合同保持原策略，冻结Scene41导出字节保持一致。原始入口、字段绑定和受控执行边界见tests/fixtures/ra2-conyard-sale-reference.json。

Scene64 / Production11引入以下乘员字段，Scene65继续使用。NativeSaleSurvivors（Version1）读取AlliedSurvivorDivisor、SovietSurvivorDivisor以及AlliedCrew/SovietCrew/Engineer/Technician引用；引用必须解析到现有步兵定义，工程师引用须有Engineer标志。Type.*增加SaleCrewed和Engineer，旧来源拒绝这些字段。附加CTECH类型放在所有既有类型之后，使用追加的NativeInfantry载体；TechLevel-1且StartAllowed关闭，不进入建造或开局列表。

原始规则导出包含CTECH的Pistola与CIV3美术；冻结实验Scene41导出先移除该普通来源专用类型，再重建可达武器目录，恢复原有碎片武器编号。RA2与YR冻结输出已与本批修改前版本逐字节比较；YR43原普通profile也保持原字节。新普通RA2的初始阵营、占领历史与待撤出阶段由World77保存，不扩充Production或App合同。

非工程师乘员引用必须没有Engineer标志；不支持把普通乘员或技师配置成工程师的组合，以保证重复工程师候选能够结束并在装载阶段拒绝错误规则。

## Scene65空坐标Scatter

Scene65校正empty-source步兵散开的Nearby主分支与出售后路线等待，不增加INI字段。Scene64以前的相邻格算法按来源版本保留；NativeSaleSurvivors1、Production11、CTECH和World77合同沿用。冻结Scene41导出与YR43保持各自规则，不启用新分支。

同一未发布Scene65还修正普通AI自动返修：经济/供电工作条件由World共享查询提供，房屋控制权限来自已编码状态，场景/规则未加字段。Scene64保留旧调度，世界服务仍允许玩家按原合同等待资金或电力。

## Scene66高度投影分组

Scene66只增加Nearby候选的基础高度投影分组策略，不加INI字段。2,070条原版投影查询覆盖基础等级0..14及六个偏移，另有1,008条真实原程序散开输出；source规则导出与消费者使用当前Scene66。Scene65保留首个非空方环策略；Production11、CTECH、服务价格和原AI返修策略不变。

## Scene67玩家步兵散开

Scene67 / Production11不新增INI字段。手动Scatter复用现有PlayerScatter与Fraidycat以及Nearby规则，来源版本隔离新手动语义；旧66继续原手动算法。自动预警、Drive强制让路及出售使用各自既有策略，YR43与冻结Scene41导出的规则和字节合同不变。原版供应条件与捕获见tests/fixtures/ra2-player-scatter-reference.json。

## Scene68 Unit车辆散开

Scene68 / Production11不新增INI字段。新手动车辆分支重用IsTrain、MovementLocomotor及既有状态；与步兵不同，它不消费散开随机数。现有Scene67及以前的车辆算法保持，步兵/自动散开、YR43与冻结Scene41沿用；World77/App11/12和未发布LAN96不变。原版准入/水面/计时器捕获见tests/fixtures/ra2-vehicle-scatter-reference.json。

## Scene69 Walk整数步进

普通RA2 Scene69沿用Production11与现有速度/老兵/增益来源，不增加INI字段。Walk类型在原版帧边界计算整数位移与分阶段舍入；既有Move、目标、占位和层查询继续负责准入。旧来源不切换新运动，YR43与冻结Scene41导出的字节合同保持。World77/App11/12/LAN96不加字段。
