# 第一关的原生 AI 与工厂生产

本批把原图的桥头小屋进入事件、AI 启动开关、自动队伍需求和真实兵营生产接在一起。保留作者初始 738 个对象与 108 个类型定义，不在生产计时结束时生成替代波次。完整原版 AI、完整第一关及战役仍未完成。

## 原始启动与来源

本图设置 `IgnoreGlobalAITriggers=yes`，有六个本地征召兵攻防触发器。导出保留完整原始记录、启用列表、条件块、权重、三档难度、队伍与 TaskForce；启用表里引用的全局 ID 没有被当成本图需要执行的内容。

作者把 `08F8484C` 挂在 `(80,48)` / `(84,48)` 的 `CABHUT` 上。玩家工程师实际进入并修复关联桥梁时，触发器执行原始动作列表，其中 `74` 启动 BadGuy1 的 AI 触发器，`3` 启动其生产。此前修桥没有通知小屋的 EnteredBy 事件，本批补上通知，并在恢复桥体可能创建实体后重新获取需要的数据，避免持有失效实体引用。

原始条件块 `0100000003000000...` 表示对数量 1 使用比较 3；本图测量自己的 NAHAND 数量。条件、国家与生产资格分别核对原始房屋 ID 和来源科技，不能以玩家共享队列或融合阵营替代。

| 玩家难度 | 攻防编制各自人数 | 各 TeamType 的 Max | 来源 TeamDelays 所用条目 |
|---|---:|---:|---:|
| 简单 | 2 | 2 | 500 原帧 |
| 普通 | 5 | 3 | 750 原帧 |
| 困难 | 10 | 4 | 1000 原帧 |

列表按电脑房屋的难度槽取值，触发器末尾难度开关按玩家选择取值。没有因原图的数值排列而交换作者配置。

## 队伍、招募与选择

AI 以来源 TeamDelays 和房屋初次错峰计时检查本地触发器，保留防御队伍预算、难度、Owner、科技、成员可建造性、Max 和未组装同型队伍门槛。选择使用当前权重的整数部分；显式 Create Team 保留其原有语义，自动门槛不限制作者显式创建。

两种原始招募许可独立保存。原图四艘 DRED 的普通招募允许、自动招募禁止，不能被自动队伍统一抽调。队伍每 tick 对每个缺员编制行最多招募一人，检查实际归属、状态、组、权限和现有队伍优先级。无路径点的空队伍使用来源的空坐标，不虚构基地中心；有成员后使用队伍中心。候选次序采用平方 lepton 距离及跨组附加值，而非欧氏格距。自动创建还会保留对应 TeamType 的 Autocreate 变化，加入无标签队伍不会抹去对象的作者标签。

删除队伍时，以其实际是否执行过 Success(49) 记录结果，影响所有将该 TeamType 放在第一槽的本地触发器。本图成功增量为 20、失败为 -50，权重区间 60..70；生涯结果、次数及权重均保存。原图 Guard(0) 结束的队伍没有伪造 Success。

机制参考为 OpenTS 的 [AI 队伍与生产](https://opents-developers.github.io/OpenTS/systems/ai-team-production/) 和 [AI 触发器格式](https://opents-developers.github.io/OpenTS/mapping/ai-triggers/)。YRpp 的 [AITriggerTypeClass](https://github.com/Phobos-developers/YRpp/blob/master/AITriggerTypeClass.h) 与 [HouseClass](https://github.com/Phobos-developers/YRpp/blob/master/HouseClass.h) 另提供权重、结果计数、房屋开关和各类生产需求的结构佐证。TS 实现和这些结构不是 RA2 实机逐帧一致性的证明。

招募中心、距离与兵营出口另核对固定版本的 [OpenTS 源码](https://github.com/OpenTS-Developers/OpenTS/tree/4e5fd62e930339a03c2c18be53a43a167fcf9a79/code) 中 `team.cpp`、`object.cpp` 与 `building.cpp`。这些是当前实现的可追溯依据，后续仍需与 RA2 实机校准。

## 独立工厂作业

AI 的待造类型由真实未满员队伍及可招募库存形成，检查资金、来源前提和实际工厂。每座兵营保有自己的类型、进度、计时余量和已付款金额；玩家原有共享队列独立处理。并行工厂沿用同一房屋需求，不把两份工厂作业强行合并。原版并行行为另参考 Phobos 对 [平行 AI 队列](https://phobos.readthedocs.io/en/latest/Fixed-or-Improved-Logics.html#forbid-parallel-ai-queues) 的说明。

两座 NAHAND 位于 `(89,65)` 和 `(90,72)`。E2 使用原始 100 造价、125 生命与原生武器资料，按 54 步支付，由订单所属的实际兵营出口创建。产生的对象使用既有 `production:<creator>:<sequence>` 身份；其他兵营不能替它释放订单。出厂单位通过真实移动让开出口，再参与招募。

原图第二座兵营的首选门口及相邻格被作者放置的 CAMISC01/02 桶占用，旧的单点出口因此永久堵塞。新来源导出 `GDIBarracks` / `NODBarracks`：GDI 首选偏移 `(1,2)`，Nod 首选 `(2,2)`，再按来源顺序检查周边格的通行与占位。原桶保持不变；旧 SourceScene 的历史出口行为也保留。当前直接在合法出口格生成，厂内移动、子格叠放和车辆/海空军出口矩阵仍未校准。

资金不足不增加已付金额或进度；出口占用时保留完整付款作业。兵营被毁会丢弃自己的作业，来源对象键防止实体编号复用继承旧订单。作者整体换房屋时清除旧主人作业。当前堵门保留和销毁损失策略仍需原机校准，不能据此称完整 RA2 出厂、退款与捕获边界已还原。

## 格式和模块

本批引入 **World v44 / App v10 / SourceScene v12 / NativeAI v1 / NativeEvents v2 / NativeProduction v1 / LAN v22**。后续 [奖励箱与顺序动作批](native-crates.md) 已推进到 **World v45 / SourceScene v13 / NativeEvents v3 / NativeCrates v1 / LAN v23**，其余版本保留。下面的完整/ASan 成绩属于原生 AI 批的历史冻结输入。

新增状态位于 World 末尾扩展：房屋开关与计时、待造类型、权重与结果计数、类型级 Autocreate、队伍创建时间、按来源身份保存的招募资料、按工厂身份保存的作业。所有影响模拟的字段进入 checksum；来源定义继续由已保存的原始场景重建，不以缓存指针入档。

- `gen_native_ai.py` 与 `native_ai_parse.cpp` 导出、严格解析来源。
- `world_native_ai.cpp` 处理选择、门槛和权重；`world_native_ai_recruit.cpp` 处理招募。
- `world_native_ai_production.cpp` 处理需求、独立工厂与真实出厂；`world_native_ai_save.cpp` 处理校验、续档和校验和。
- `game_native_ai_tests.cpp` 与 `native_ai_assets_test.py` 验证实际操作、模拟续跑及独立来源比对。

普通 World43 兼容夹具和升级前实际保存的 World43/Scene11 工程师存档分别验证封套布局与原生历史边界。旧来源没有新 AI 资料时，不替换其来源或自动启动新 AI。

## 验证范围

专项先复用原图谭雅入区、基地移交、建兵营、付费训练工程师和实际修桥，再由玩家真实付费训练十二名原生 GI。玩家通过公开指令进攻可见步兵，并在出现补兵需求后撤开；没有改联盟、资金或生命，没有直接删除原始征召兵或额外生成主流程单位。原图已有 22 名 BadGuy1 征召兵，不能把“尚可招募现存部队”算作没有执行生产。

当前实战专项在项目 tick9461 记录到两座原始兵营的独立作业与两名新出厂 E2，谭雅剩余 74 生命且任务仍在运行。这个时刻属于项目测试，不代表原版时序，也不是整关通关。

另设明确的机制分支验证缺钱、堵门、工厂被毁、整体换房屋、三档编制/Max、权重、损坏存档和逐 tick 续跑。堵门分支用已有单位及额外原生占位对象占住一座实际兵营的所有合法出口，以保护和移动停顿隔离占位；另一座继续生产，清出一格后只释放一个等待对象。额外占位对象不属于上述实战流程。成功权重分支通过真实停止命令释放并重新招募 E2，再显式把原始脚本游标移到 Success(49)，不称自然战斗成功。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_ai|native_ai_assets|native_production|native_team_attack|native_script_control|app_state|state)$' -V
```

本地日志为 `build/native-ai-*` 与 `build-asan/native-ai-*`；画面及实际活动 AI 存档在 `build/native-ai-audit/`。画面分支在实战快照上切换到苏军观察方并显式揭开迷雾，分别检查 1280×720 与 1920×1080 的工厂、出厂单位和侧栏；这项显示设置不作为玩家自然视野或无作弊通关证据。

相关 Release **11/11 通过**，47.91 秒。冻结后完整 Release **113 项中 112 通过、空战役矩阵 1 项跳过、0 失败**，908.49 秒；包含自然开局、完整遭遇战、独立表现矩阵、原生 AI 与双进程同步。构建、模拟和来源共 53 项冻结输入在回归后哈希保持一致。精简记录为 `build/native-ai-full-audit.txt`，完整记录为 `build/native-ai-full-tests.log` / `native-ai-full-console.log`；本地 `review_out/native-campaign/native-ai-validation.json` 记录来源、构建与旧存档哈希。

最终相关 **ASan Release 35/35 通过**，764.51 秒，未报告内存错误。使用 `RA2_ASAN=ON` / MSVC `/fsanitize=address`，覆盖交通、原生 AI 与生产、桥梁/事件/队伍/武器/导弹、应用存档、网络和状态；它是相关选择集，并非全量 ASan。Release 与 ASan 的实战专项均在 tick9461 记录到两名新 E2、谭雅 74 生命、任务运行中，保存的活动 AI 应用存档逐字节一致。日志与精简记录为 `build-asan/native-ai-final-{tests.log,console.log,audit.txt}`，选择集为 `native-ai-selection.txt`。两组 GUI 检查顺序运行；测试后再次核对冻结输入与二进制哈希。

## 后续保真工作

本批执行当前地图实际使用的本地条件与步兵需求；全局 AI、非零 Side、其他条件、完整敌意/受击仇恨选择、防御响应、复组补员、海空军与基地规划仍需完成。当前敌人条件以场景玩家为目标，并非完整原版仇恨表。

AI 思考/工厂刷新顺序、难度造价与计时倍率、低电梯度、起楼/开门与出厂动画、特殊 BuildLimit、捕获和出口失败极端情况尚未完成原机校准。当前出口疏散沿用项目的可达位置搜索。其他任务、原始任务媒体、战役菜单与进度，以及人工从开局到结算的整关验收仍未完成。

后续按以下顺序验收：

1. 先跑第一关从真实开局到作者结算的完整操作链，记录实际触发的未支持指令与卡点。正常指令、原始规则和实际战斗必须支撑结果；局部机制分支不能代替这项验收。
2. 按整关暴露的问题补防御响应、复组补员、敌意选择及生产生命周期，并对照原机校准时序。当前只覆盖本图使用条件，新增 AI 条件与全局行为须有独立来源和运行证据。
3. 补任务媒体、原版动画、选关与进度，再扩展其他任务和海空军矩阵；与人工完整实玩、听感及视觉验收一起判断复刻程度。

本批完成时的静态盘点得到 4 组包含未实现动作的来源批次、15 条未支持事件条件和 26 条未支持脚本行，明细在本地 `review_out/native-campaign/native-ai-next-mission-audit.json`。其中动作 108/101 和整批预检问题现已在 [奖励箱批](native-crates.md) 处理，并用真实部队和谭雅取得四箱、原始 Create Thugs 动作列表及失败顺序专项验证。其余条件和脚本仍需沿实际流程确认可达性与优先级，不能把全部保留记录都当作已经执行或通关必需。
