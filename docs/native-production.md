# 原始第一关的玩家生产链

第一关现能用原图谭雅移动进入真实触发区域，按作者的两秒延迟接管基地，再通过侧栏建造原生兵营、付费训练原生工程师，实际前往桥头小屋修复开局被炸断的桥梁。此链路没有额外生成入区单位或工程师，没有改变联盟、瞬移谭雅或直接调用恢复桥梁方法。它是任务中的一段真实操作链，仍不是整关通关证明；本关尚未作为完成战役加入选关。

## 原始资源与可建造范围

玩家原始房屋的科技等级为 3、初始资金为 4000。基地交接区域没有预置盟军兵营，因此不能用已有通用工程师直接替代训练流程。本批在原始 738 个放置对象保持不变的前提下，补入 `ENGINEER` 和 `GAPILE` 两份定义：当前 **108 个类型，其中军队/建筑 73 个**。

`gen_native_production.py` 从原始规则及地图覆盖导出科技等级、Owner、Required/ForbiddenHouses、原始建筑与组别前提、Factory 类别、BuildLimit、建造排序、基础帧数、邻接范围和 FreeUnit。场景通过严格解析恢复这些资料，不借用融合遭遇战的科技树。数据仍覆盖地图已有类型，而不是把整张规则表称为已具备物理实现的内容。

资源专项独立闭合玩家从原图交接基地出发的科技树，得到本关实际可达的七种产品：**发电厂、矿石精炼厂、盟军兵营、机枪碉堡、美国大兵、工程师和军犬**。这些类型全部具备原生定义；坦克、狙击手和战机不会因为通用列表里存在而绕过本关前提或国家限制。

| 产品 | 来源造价 | 来源生命 | 正常单工厂下的 30 Hz 生产时间 |
|---|---:|---:|---:|
| 美国大兵 E1 | 200 | 125 | 216 tick |
| 工程师 ENGINEER | 500 | 75 | 540 tick |
| 盟军兵营 GAPILE | 500 | 500 | 540 tick |

本批还导出原版生产图标及 `cameo.pal`，逐像素比对 PNG。图标使用原版 `language.mix/cameo.mix` 与核心 RA2 资源，没有以相近单位的图标代替。已有单位的身体动画继续沿用其明确映射的原版素材；新增兵营使用原始建筑 SHP 和调色板。

## 队列、支付与身份

现有 `ProdItem` / 建筑与兵种队列保留；命令和队列通过独立的原生编号引用来源定义，不能把它当作通用 UnitType 或 BldType。启用新来源生产的场景拒绝通用生产替代，普通遭遇战仍走其原有路径。

生产按 54 步推进。正常条件使用来源造价、BuildSpeed 和 BuildTimeMultiplier 计算基础原帧，截断成每步 1..255 原帧，再换算为项目 tick。每步支付“未付款余额 / 剩余步数”，不足支付该步时不扣款、不增步数。500 价兵营的首步在 tick10 支付 9，完成时恰好支付 500。HOLD 保留已完成步数和付款；取消先清除负债再返还实际已付金额。

54 步可由 YRpp 的 [FactoryClass](https://github.com/Phobos-developers/YRpp/blob/master/FactoryClass.h) 和原始生产时钟帧佐证。步进、分期及工厂依据另参考 OpenTS 的 [生产实现说明](https://opents-developers.github.io/OpenTS/systems/production/)；TS 的具体低电、难度与失败出厂路径没有当作 RA2 实机一致性证明。

完成单位通过工厂实际出口创建 `spawnNativeActor`，保留来源类型、生命、原生武器/目标资料及颜色。每个新产品取得独立 `production:<creator>:<sequence>` 身份，序列被保存和校验；读档或实体编号复用不会把它变成通用原型。正 BuildLimit 会检查存活单位、嵌套乘员、当前订单及队列。非正/生命周期限制暂不开放，不能据此称完整限制矩阵已完成。

兵营放置检查原生脚印、平整高度、坡型、桥面、单位和建筑占位，并按来源 BaseNormal / Adjacent 邻接现有基地。精炼厂的来源 FreeUnit 为 CMIN，沿原生身份生成并接入已有采矿路径。原生界面使用同一模拟查询，并通过 `World::Cmd` 提交；放置预览只属于应用，执行回执后才关闭。

## 格式与模块

本批引入 **World v43（RA2WLD43）/ App v10 / SourceScene v11 / NativeProduction v1 / LAN v21**。后续 [原生 AI 批](native-ai.md) 接通独立工厂，[奖励箱批](native-crates.md) 已推进到 **World v45 / SourceScene v13 / NativeEvents v3 / NativeAI v1 / NativeCrates v1 / LAN v23**，App v10 与 NativeProduction v1 保留。旧 World v44 及更早读档继续支持；没有生产、AI 或奖励箱资料的旧场景保留其既有能力边界，不自动替换保存中的来源。

- `native_production_types.h` / `native_production_parse.cpp` 管理来源配置；`world_native_production.cpp` 管理准入和队列命令。
- `world_native_production_update.cpp` 管理步进、支付及原生出厂；`world_native_construction.cpp` 管理来源建筑放置与 FreeUnit。
- `world_native_production_save.cpp` 校验并保存生产序列及类型/队列一致性；原有队列字段保存原生编号，已进入 checksum。任务全灭计数也覆盖当前原生订单。
- `game_native_production.cpp` 管理原版图标、54 帧时钟、队列交互和原生放置预览，不直接改变模拟规则。

普通 v10/World42 兼容夹具验证旧封套布局；升级前真实保存的 World42/Scene10 成功循环存档另验证原生脚本与无生产资料边界。新队列的等待、暂停、付款中和完成状态均比较逐 tick 续跑。

## 验证

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_production|native_production_assets|native_scene|native_events|native_mission|native_script_control|state|app_state)$' -V
```

`ra2.native_production` 使用原始谭雅发出移动命令，实际抵达 CellTag `(64,39)`，作者基地交接于测试 tick3053 执行。随后在原图合法位置 `(69,29)` 完成并从原版图标打开兵营预览，通过实际地图点击、延迟命令和回执放下 `GAPILE`；原生工程师也由真实图标点击付费生产。其后实际工程师经过原始闸门前往小屋，被消耗并恢复原始跨度。

另覆盖未接管基地、科技与前提、拒绝通用原型、步进首款、暂停/恢复、精确退款、应用存读档及实体来源身份。历史 Scene11 的单点出口被占时保留完整付款订单；Scene12 及之后使用来源 GDI/Nod 首选格与周边回退，首选格被占仍能从其他合法格释放工程师。所有合法出口被占时的保留、清出一格后只释放一个对象，另在原生 AI 专项验证。精确 RA2 出口失败/取消行为仍待实机校准。额外堵门与退款分支使用明确快照，不称为原图自然战斗结果。

画面位于 `build/native-production-audit/native-engineer-{1280,1920}.png` 和 `native-repaired-{1280,1920}.png`，覆盖原始基地、实际新建兵营及来源图标；绘制前后 checksum 相同。工程师已生产存档为 `native-engineer-produced.sav`。原始文件、移交建筑、七种产品资料、图标来源及画面/存档哈希在本地 `review_out/native-campaign/native-production-source-audit.json`；旧存档哈希、构建和完整验证记录在 `native-production-validation.json`，日志为 `build/native-production-*` 及 `build-asan/native-production-*`。

冻结前相关 Release **8/8 通过**，34.16 秒；最终四项复测 **4/4 通过**，14.47 秒。

完整 Release **111 项中 108 通过、空战役矩阵 1 项跳过、2 项旧存档夹具失败**，904.90 秒。交通与闸门的行为、回放及续档检查通过，失败来自损坏记录测试仍按旧后缀长度定位数据。两处夹具改用实际序列化后缀定位，保留原有非法值、重复记录和截断拒绝检查；重建后的相关 Release **7/7 通过**，55.69 秒。此后只改测试层，冻结的生产模拟及来源输入哈希保持一致；完整运行与复测分别记录，不写成一次全绿回归。

最终相关 **ASan Release 34/34 通过**，753.74 秒，未报告内存错误。覆盖实际生产修桥链、修正后的交通/闸门损坏记录、原生场景/事件/队伍/武器/导弹、历史应用存档和网络/状态一致性。使用 `/fsanitize=address`，与普通 Release 的 GUI 检查顺序执行；这不是全部测试的 ASan 成绩。最终二进制、两份修正夹具及冻结的模拟/来源输入哈希再次核对一致，完整证据见上述验证 JSON。

## 仍需完成

本图六个本地触发器、招募、权重历史和独立工厂作业已在后续 [原生 AI 批](native-ai.md) 接通，其实战证据与受控分支单独记录。完整全局 AI、防御响应和基地规划仍未完成。低电计时梯度、难度/价格折扣、首要工厂和捕获边界；生命周期 BuildLimit、形态转换限制、原版起楼/工厂开门及出厂动画、完整精炼厂/空军/海军工厂矩阵仍未校准。当前低电使用来源上下界作连续近似，不宣称精确原引擎计时。

生产批完成时制定的原生 AI 顺序如下；后续实现与验收以 [原生 AI 记录](native-ai.md) 为准，这份历史计划不代表全部原版 AI 已完成：

1. 保留来源 `IgnoreGlobalAITriggers=yes`、本图六个征召兵攻防触发器及各难度开关，连同原始比较条件、权重、`TeamDelays` 和队伍资料严格导出解析。不能把启用列表里引用的全局 ID 当成需要激活的本关内容。
2. 为实际敌军兵营建立独立生产作业，接入真实 E2 的支付、出口、招募和队伍组装；本图编制为 2/5/10 人，自动队伍上限为 2/3/4，均按作者配置，不补生成波次。玩家共享队列和显式 Create Team 的既有语义继续各自处理。
3. 接通成功/失败权重及补员调度，保存并校验全部可变状态。验收须覆盖两座实际兵营、堵门、资金不足、工厂被毁、编制与上限、存读档逐 tick 一致，并重新验证有真实敌军生产时的玩家接管与修桥链；具体原引擎时序仍须单独校准。

其他任务、文字/语音/影片、原生重开与战役进度、完整战役和人工整关验收仍未完成。此批自然基地操作链不代表整个任务或整个复刻目标完成。
