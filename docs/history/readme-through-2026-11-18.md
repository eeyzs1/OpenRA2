# OpenRA2

2026-11-18 乘员武器选择批：SourceScene37 / NativeCrew2 / World60 / App11 / LAN55。按原始 OpenTransportWeapon 修复 GI、重装大兵副武器及海豹、谭雅主武器；真实登车／开火修改前20失败，最终探针两构建均129通过，保留旧Scene36历史行为。最终 Release 155/156，原型尤里/盟军20261031在原72000未结算仍失败；相关 ASan 33/33，未报告内存错误。976对真实保存逐字节一致；七局原窗口2结束、2严格通过。完整AI、原始时序及其余37任务仍未完成。见[本批范围与证据](docs/crew-weapon-selection.md)。

2026-11-18 同格到达与建造顺序批：修复空格路由困住未到中心的单位、重复矿厂配额拖延兵营。新增攻击移动72、建造顺序90断言／构建通过；最终Release155/156（原型尤里/盟军20261031在原72000未结算，失败保留）、ASan相关33/33。366对实际保存跨构建相同，11104冻结输入未变；七局原窗口仅2局自然结束、2局全部严格通过。英国首次训练4650tick，原零出兵开局已修复；原型与来源整局窗口失败仍待处理。SourceScene36 / World60 / App11保持，LAN54；完整复刻未完成。详见[实现与限制](docs/ai-arrival-and-build-order.md)。

以《红色警戒 2》为目标的 C++ RTS 复刻工程，另保留尤里的复仇和中国阵营的融合扩展。
当前提供四阵营遭遇战和局域网对战；完整战役、全部规则保真和人工实玩验收仍未完成。最新问题、修复证据及实施顺序见 [项目审查与优化计划](docs/review-plan-2026-11-15.md)，此前批次见 [阶段记录](docs/skirmish-status-2026-11-13.md)。

当前普通来源规则为 SourceScene37 / NativeCrew2 / NativeTemporal1 / NativeWeapons3 / NativeIFV2，World60 / App11 / LAN60。原始全局 AI 的最新实验入口为 Scene41 / NativeAI5 / World62（旧 Scene38／39／40 保留合同），尚未替换默认规则；实验已验证实际开局／续档，以及付费 V3＋防空护卫按原脚本集结和攻城。原版附近格查询已接入基地中心及 53／54 集结，包含来源速度类别、真实占位和原版 3×3 查询；Scene41 补齐建筑地基中心偏移和原始无效格数据；完整地图初始化、源坐标、移动／重组和原时序仍待完成。实现、验证范围及未完成脚本见 [接入记录](docs/source-ai-skirmish-plan.md)。要塞乘员的独立时空、稳定射手身份及城市驻军的版本差异见 [乘员记录](docs/native-crew.md)。军团兵与时空 IFV 的持续抹除、协同、冻结及保存见 [时空武器记录](docs/native-temporal.md)；IFV 乘员映射、维修与炮塔见 [IFV 记录](docs/native-ifv.md)。声波、原始时序与完整特殊武器行为仍在推进。

原版盟军第一关已接入任务入口、文字/语音、初始场景、高架桥双层通行、原始闸门开合及胜负结算；正常指令流程、重开和续档已纳入回归。四艘原始 DRED 按作者脚本攻击自由女神像，区域进入后交接 27 个对象并增援 CMIN，工程师可穿过原闸门。详见 [战役素材与入口](assets/campaigns/README.md)、[原始队伍脚本](docs/native-teams.md)、[原版事件与交接](docs/native-events.md)、[桥梁通行](docs/native-bridges.md)、[修桥](docs/native-bridge-repair.md) 和 [闸门](docs/native-gates.md)。全部脚本语义和人工实玩仍需验收；影片按用户要求排除。

游戏使用从本地原版 MIX 提取的 SHP/VXL、地形、界面和音频资源。运行前须准备完整的 `assets/`；缺少必需素材时会报告错误并禁止启动，不再使用程序生成素材兜底。资源提取和覆盖方式见 [素材管线](docs/asset-pipeline.md)。

## 技术栈

- **C++23**（MSVC 2022 BuildTools）
- **raylib 5.x**（渲染/音频，CMake FetchContent 自动拉取）
- **CMake ≥ 3.24**

## 构建与运行

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\ra2.exe
```

默认无边框全屏启动（自适应任意分辨率与高 DPI 缩放）；调试可加 `--windowed` 强制窗口模式。

## 游戏模式

遭遇战设置页和联机大厅可选择融合、红警 2 基础、尤里基础规则。基础规则使用各自来源定义和可用国家；读档与重开使用保存的规则快照。固定翼水平核心、Jumpjet 升降、持续装卸及机场机位互斥已接通，完整原版行为仍在校准，见 [规则包](docs/rules-profiles.md)、[飞行与地面交互](docs/native-air-ground.md)、[机场与生产](docs/airfield-production.md)、[来源 AI 空运](docs/source-ai-airlift.md) 和 [普通开局与结算](docs/source-ai-opening.md)。

- **遭遇战**：1~7 个 AI 槽位（颜色/国家/难度/人格），11 个国家分属盟军、苏军、中国、尤里四阵营；Setup 可选 Battle、Free For All、Unholy Alliance、Megawealth、Land Rush、Meat Grinder、Naval War，以及 Short Game、AI 联盟、共享视野、补给箱和超级武器
- **战役**：导出本地原版素材后，正常菜单可进入盟军第一关 `all01t`，使用原地图事件、任务文字和语音，支持重开、存读档与结算。其他原版关卡尚未接入，旧的融合与近似官方关卡保持撤下。详见 `assets/campaigns/README.md`。影片按当前需求不还原。
- **局域网对战**：主菜单「局域网对战」进入大厅，主机/客户端 TCP 直连，确定性锁步同步（3 tick 输入延迟 + 每 90 tick FNV 校验），断线判负、失同步判负；联机模式自动禁用存读档/暂停/调速
- **设置页**（主菜单 / 局内菜单均可进入，改动即保存 `settings.ini` 即时生效）：中英双语热切换、显示模式、窗口分辨率（1280×720~1920×1080）、主音量、按键重绑定（冲突自动让位）

### 地图类型

| 类型 | 说明 |
|------|------|
| 大陆 | FBM 噪声散布湖区（默认） |
| 岛屿 | 每出生点一座主岛 + 中央争夺岛，其余为海 |
| 湖泊 | 中央大湖，四周环陆地 |

## 操作

> 以下为默认键位，全部可在设置页重绑定

| 按键 | 功能 |
|------|------|
| 左键 / 框选 | 选择单位；双击基地车展开 |
| 右键 | 移动 / 攻击 / 采矿 / 登船（按住 A = 攻击移动） |
| S / X / G / T | 停止 / 散布 / 警戒 / 选同类 |
| D | 基地车展开 · 辐射工兵部署辐射区 |
| U | 运输船卸载乘员 |
| Ctrl+0~9 / 0~9 | 编队设定 / 选取 |
| R / Shift+R / Delete | 设集结点 / 清除集结点 / 卖建筑 |
| 侧边栏 维修/出售 | 点击后进入对应模式，点己方建筑执行（右键取消） |
| H / P / M | 回基地视角 / 暂停 / 音乐开关 |
| F5 / F9 | 快速存档 / 快速读档（局内菜单同入口；联机禁用） |
| +/- / 方向键 | 游戏速度（慢/普通/快）/ 卷屏 |

## 已实现内容

- **四阵营十一国**：盟军 / 苏军 / 中国 / 尤里，差异化单位与建筑树；国家特色单位（坦克杀手/自爆卡车/恐怖分子/狙击手等）
- **经济主流程**：矿脉分布、采矿车（超时空/武装/普通）自动采矿卸载、电力系统（低电减速生产）
- **海军全套**：海军船厂、台风潜艇、海蝎、神盾、无畏/航母、海豚/巨型乌贼、两栖运输船、陆/水/两栖三寻路域
- **超级武器**：战术核弹、闪电风暴、铁幕、超时空传送，建造即向敌方广播侦测警告
- **特殊机制**：超时空军团兵、幻影坦克伪装、辐射区、V3 远程溅射、磁爆链式伤害、间谍渗透（重置迷雾/偷科技/重置超武）、尤里心灵控制（可夺回）、海豹部队 C4 爆破、偷科技解锁单位（超时空/心灵突击队）
- **中立科技建筑**：油井 / 医院 / 机械店 / 科技机场 / 秘密实验室 / 民房，工程师占领生效
- **遭遇战 AI**：四阵营合法科技树、固定种子确定性门禁、海陆适配、经济防停摆、尤里生化反应堆/回收炉使用、编队进攻和超武选点
- **战场体验**：原版单位选择/移动/攻击/采矿语音、盟军/苏军原版 EVA 播报、13 首原版对战配乐及菜单/结算曲（M 暂停继续）、编队/散布/警戒、小地图、战争迷雾、老兵等级

单位语音的原始来源、生成方式和验证范围见 [原版单位语音](docs/unit-voices.md)。
EVA 与配乐的资源恢复、播放规则及缺失范围见 [原版 EVA 与配乐](docs/native-audio.md)。
- **界面国际化**：全部 UI / 名称 / 战役简报中英双语热切换（双语字模全量预载）

## 素材系统（更换为自己的素材）

基础资源通过本地素材管线准备。`mods/` 与 `userdata/content/` 可以覆盖同名资源；只需提供要替换的部分，其余沿用基础资源，重启生效。基础资源本身缺失时会报错，不会回退到程序生成的单位或建筑。

```
assets/
  sprites/   PNG 图片（命名规则见下表）
  sfx/       音效：<名>.wav / .ogg / .mp3；voices/ 存放原版单位语音与清单
  music/     music.ini 配置；native/ 存放原版 WAV 和按场景生成的清单
```

| 文件 | 说明 |
|------|------|
| `tile_<地形>_<变体0-3>.png` | 地形瓦片（clear/rough/water/ore/gems/bridge，菱形 64×32） |
| `overlay_<名>.png` | 树木/岩石（tree1~3、rock1~2） |
| `unit_<单位名>_d<0-7>.png` | 单位，8 方向（0=东顺时针）；可加 `_f<帧>` 区分满载/行走帧 |
| `turret_<单位名>_d<0-7>.png` | 炮塔（独立旋转层） |
| `bld_<建筑名>.png` / `bld_<建筑名>_scaffold.png` | 建筑 / 建造中脚手架 |
| `icon_unit_<名>.png` / `icon_bld_<名>.png` | 侧边栏 56×44 生产图标 |
| `fx_explosion_<0-11>.png`、`fx_muzzle.png`、`fx_smoke_<n>.png`、`fx_proj_<kind>_d<dir>.png` | 特效 |
| `fx_warpin_<n>.png`、`fx_warpout_<n>.png`、`fx_warpaway_<n>.png` | 原版传送/抹除完整帧；帧数在 `assets/chrono_fx.ini` |

从本地 MIX 单独重建超时空素材：`python tools/ra2pack/gen_chrono_fx.py`；完整 `gen_assets.py` 也会生成。专用生成器保留 SHP 全画布与 `anim.pal` 原色，写入来源哈希，不重写单位/建筑动画元数据。

要点：

- 单位/建筑/音效的内部名称见 [src/gfx/assets.h](src/gfx/assets.h) 的 `unitAssetName / bldAssetName / sfxAssetName`（如 `unit_nighthawk_d2.png`、`bld_cloningvat.png`）
- **阵营换色**：需随玩家变色的像素涂纯红 `(255,0,0)`，运行时自动替换为阵营色（暗部 `(176,0,0)`、亮部 `(255,128,128)`）
- **锚点**：单位/炮塔按画布中心，建筑按底边中心（下留 4px）
- 基础资源必须提供完整方向与动画；缺失时报告素材错误并拒绝启动。

## 自动化验证

```powershell
ctest --test-dir build -C Release --output-on-failure # 自动回归合集（含窗口交互、截图、耐久与网络）
.\build\Release\ra2.exe --smoke 20000          # 无头跑 N 帧遭遇战 + 全要素强制验证 + 三方视角截图
.\build\Release\ra2.exe --hud-regression review_out/hud # 四阵营双语侧栏与生产提示截图
.\build\Release\ra2.exe --skirmish-regression 72000 -1 0 --require-outcomes # 12 局自然开局必须结算
.\build\Release\ra2.exe --magnetron-regression review_out/magnetron # 主副武器、规则覆盖、续档与实绘
.\build\Release\ra2.exe --ai-attack-regression # 合法攻击、侧翼迷雾与四阵营防空选择
.\build\Release\ra2.exe --ai-scouting-regression # 侦察记忆、盟友超武保护、隐藏变化隔离与当前存档续跑
ctest --test-dir build -C Release -R "^ra2.entity_identity$" --output-on-failure # 延迟指令、编队、部署回执、Lua 句柄、存档与 TCP 身份防复用
.\build\Release\ra2.exe --unit-voice-regression # 真实音频设备静音播放、选择/指令、编号复用与战斗回放隔离
.\build\Release\ra2.exe --native-audio-regression # 原版 EVA 优先级、真实事件、配乐场景、暂停继续及回放隔离
ctest --test-dir build -C Release -R '^ra2.(native_scene|native_scene_assets|native_scenery_assets)$' --output-on-failure # 盟军第一关原始实体、地表/覆盖物及素材；尚无原版任务事件
ctest --test-dir build -C Release -R '^ra2.campaign_save$' --output-on-failure # 独立夹具验证任务与 Lua 续档，不代表原版战役内容
ctest --test-dir build -C Release -R '^ra2.(native_campaign|native_campaign_assets|native_playthrough)$' --output-on-failure # 实际原版第一关入口、文字来源与正常指令通关
.\build\Release\ra2.exe --net-protocol-regression # 同 tick 超武顺序、提前/延迟校验、256 地图、随机国家和换局规则隔离
.\build\Release\ra2.exe --group-order-regression # 200/500 单位不可达编队指令的响应耗时、合法落点与回放
.\build\Release\ra2.exe --ai-air-defense-regression # 四阵营基地空防实战、冻结/预算/断路恢复、迷雾、维修/登船保护与当前快照
.\build\Release\ra2.exe --ai-factory-exit-regression # 四阵营陆军/步兵/舰船实际清口、密集队尾让路、保护状态、付款与当前快照
.\build\Release\ra2.exe --ai-economy-regression # 采集单位丢失后的退单补产、资金守恒、尤里补员与当前快照续跑
.\build\Release\ra2.exe --ai-transport-regression # 四阵营步兵/坦克混编投送、尺寸容量、载矿/嵌套乘员、登陆作战、返航与快照续跑
.\build\Release\ra2.exe --moving-yield-regression # 窄格换位、失败扫掠预约释放、连续移动、原目标及排队命令、盟友与保护、当前快照
.\build\Release\ra2.exe --blocker-regression # 敌方堵路、三域占用、步兵容量与失败恢复
.\build\Release\ra2.exe --idle-yield-regression # 长窄路空闲友车让路、命令覆盖与续档
.\build\Release\ra2.exe --chopper-ground-regression # 攻城直升机部署、落地点、空地占位与续档
.\build\Release\ra2.exe --chopper-transition-regression review_out/chopper-transition # 部署四阶段、降落点预留、冻结续接、心控、快照与起降高度
.\build\Release\ra2.exe --siege-pose-regression review_out/siege-pose # 部署直升机车体/炮塔、阴影、点选及双分辨率实绘
.\build\Release\ra2.exe --voxel-animation-regression review_out/voxel-animation # 旋翼/盖特炮管原生 HVA 两帧、锚点、停转及实际帧缓冲
.\build\Release\ra2.exe --immobilized-regression # 冻结、部署/瞬移拒绝、自动恢复及存档回放
.\build\Release\ra2.exe --deployment-feedback-regression # 部署真实反馈、混选、打包与延迟选择恢复
.\build\Release\ra2.exe --feedback-visual-regression review_out/feedback-visual # 双语反馈、长提示、高地/空中标记实绘
.\build\Release\ra2.exe --elevation-feedback-regression review_out/elevation-feedback # 低空/高地/举升反馈高度、真实事件、延迟消费及实体槽复用
.\build\Release\ra2.exe --rally-regression # 集结点设置/清除、出厂派令、水陆目标与续档
.\build\Release\ra2.exe --rally-visual-regression review_out/rally-visual # 旗标、高地/迷雾、清除及归属隔离实绘
.\build\Release\ra2.exe --refinery-recovery-regression # 备用卸矿点、混合队列、卸矿中被毁/易主与续档
.\build\Release\ra2.exe --magnetic-landing-regression review_out/magnetic-landing # 举升占位、落地/坠毁、续档及截图
.\build\Release\ra2.exe --traffic-regression  # 弯折窄路、连续车流、回放与存档检查
.\build\Release\ra2.exe --service-regression  # 固定维修机制、拥堵、旧存档迁移检查
.\build\Release\ra2.exe --placement-regression review_out/placement # 放置预览隔离、实际回执、堵塞重试、续档及截图
.\build\Release\ra2.exe --production-feedback-regression review_out/production-feedback # 生产/暂停/取消的实际结果、队列限制及双语截图
.\build\Release\ra2.exe --refund-regression review_out/refund # 封顶全额退款、实际卸矿、大额余额与当前版本续档
.\build\Release\ra2.exe --terrain-visual-regression review_out/terrain # 矿量密度、采空底材、重烘一致及双分辨率截图
.\build\Release\ra2.exe --fog-knowledge-regression review_out/fog # 最后侦察记录、隐藏变更、小地图和当前存档恢复
.\build\Release\ra2.exe --fog-feedback-regression review_out/fog-feedback # 隐藏战斗特效、光束可见片段、矿车光标与右键
.\build\Release\ra2.exe --superweapon-regression review_out/superweapon # 超武实际回执、超时空充能/落点/占位、取消与存档
.\build\Release\ra2.exe --chrono-visual-regression review_out/chrono # 传送出入/抹除、冻结状态、遮挡、迷雾及双分辨率
.\build\Release\ra2.exe --factory-state-regression review_out/factory-state # 冻结/干扰/施工设施的生产暂停、替代工厂、退款与恢复
.\build\Release\ra2.exe --generated-map-structure review_out/generated-map # 441个配置的近矿/连通、真实出生与菜单预览
.\build\Release\ra2.exe --generated-map-health # 七地图×四阵营×三种子的自然五分钟开局（非完整对局）
.\build\Release\ra2.exe --generated-match 36000 2 305419896 3 # 固定群岛中尤完整对局：失败退让重寻预算饥饿回归
.\build\Release\ra2.exe --generated-match 72000 0 20261031 1 # 40分钟随机岛屿长局诊断，参数为tick/阵营/种子/地图；尚有已知失败
.\build\Release\ra2.exe --play-test 5          # 脚本开局交互测试（含 UI 点击，不代替人工实玩）
.\build\Release\ra2.exe --net-host 900         # 联机自测主机端（配合 --net-client 双进程）
```

`--smoke`、`--smoke-campaign` 与 `--campaign-matrix` 会汇总每项断言；任一断言失败、场景无法建立或测试抛出异常时，进程返回非零。

遭遇战优化的基线、分项证据与未完成验收见 [skirmish-optimization.md](docs/skirmish-optimization.md)。机制夹具、自然开局耐久、逐 tick 确定性和截图检查分别报告，不将自动脚本当作人工完整对局。

当前应用存档格式为 `RA2GAM11`（v11），保存开局配置、AI 侦察记录、任务阶段/期限/目标/触发器、难度、胜负和脚本声明的状态，支持慢、普通、快三档速度续档。兼容应用 v10/v9/v8/v7，更早应用封套明确拒绝加载；World 内部格式为 v59，LAN 模拟协议为 v52。已启用的脚本须提供配套 `OnSave` / `OnLoad`，具体约束见 [战役与脚本存档](docs/campaign-persistence.md)。原始事件另按 [原版事件模型](docs/native-events.md) 保存。没有安装战役内容时，战役矩阵通过 CTest 的跳过状态报告，不计作战役完成证据。

原版盟军/苏军第一关的地形已可直接从本机 MIX 导入，包含原始道路、高度和悬崖立面。盟军第一关的 249 个初始单位/建筑、344 个地表对象及 145 个非矿石覆盖物已实例化，使用 103 种原始类型和 8 个房屋；另加载 CMIN 增援、DMISL、V3ROCKET、ENGINEER、GAPILE 和 SAPC 运行时定义，总定义数 109。高架桥已实现双层通行，部分原始事件已执行，四艘原始 DRED 和三辆原始 V3 已接入可被防空击落的独立导弹实体；盟军第一关已开放任务入口，其他任务仍待接入。用法与边界见 [原版地图导入](docs/native-maps.md)、[原版场景实体](docs/native-actors.md)、[地表对象与覆盖物](docs/native-scenery.md)、[原版事件](docs/native-events.md) 和 [原生导弹](docs/native-rockets.md)。

原始 Tanya 的开场移动、计时警戒和输入交还已执行；GI 交接保留原作者 14 人编制与实际 11 人预置，通过独立的整体归属动作完成。创建队伍不再因后续未知脚本而阻断已有事件，未实现的行仍明确停在游标上。验收和剩余边界见 [原始队伍命令](docs/native-team-orders.md)。

原始 Attack(0) 已接入类别选敌、追击/开火、目标重选和无目标后的脚本推进；SourceScene v7 保留原始评分及类别资料。原始空编组不借走其他房屋的部队，未校准类别保持明确阻塞。实际原场景与独立战斗机制的范围见 [类别攻击验收](docs/native-team-attack.md)。

SourceScene v8 已将原始常规武器的主、副及精英槽接入模拟，修正步兵伤害/冷却、部署 GI 覆盖、军衔倍率和 Tanya 的 Sapper；弹丸保留发射时武器槽并可续档。普通遭遇战表保持独立，范围与尚未校准项见 [原始常规武器](docs/native-weapons.md)。SourceScene v9 另接入 V3 的单槽、独立飞行实体、原图弹头及空载车体，修正高空导弹被地面迷雾遮盖的问题；弧线仍是待实机校准的近似，见 [原始 V3](docs/native-v3.md)。 原图全灭条件和延迟胜负动作已驱动任务结算，仍需完整事件、媒体和整关验收，见 [原始目标与胜负](docs/native-mission.md)。

SourceScene v10 接入原图开局的十次动作 63：按数字路径点执行 C4 爆炸，炸断两座原桥，并通过原始移交、闸门与工程师命令修复关联跨度。普通火力保护继续执行；连续坍塌及原版精确伤害边界仍待校准，见 [脚本爆破与断桥](docs/native-demolition.md)。

原始脚本 Jump(6) 与 Success(49) 已接入逐 tick 执行和续档，V3 的原始齐射脚本可在目标消失后跳转并重新捕获目标。控制语义、实际导弹验证和边界见 [队伍脚本控制](docs/native-script-control.md)；后续本地 AI 的接入范围见 [原生 AI](docs/native-ai.md)。

SourceScene v11 接通原始玩家生产：原图谭雅实际进入基地触发区后，可以通过来源科技、原版图标和付费队列建造盟军兵营、训练原生工程师，再实际修复断桥。当前涵盖本关玩家可达的七种产品，见 [原始生产链](docs/native-production.md)。

SourceScene v12 接通原图修桥小屋的 AI 启动事件、六个本地征召兵触发器、招募与成功/失败权重，以及两座真实兵营独立付款出兵。实战专项以真实付费 GI 和谭雅造成损失后观察到新 E2 出厂，并验证续档。全局 AI、完整防御响应、原版时序/动画及整关验收仍未完成，见 [原生 AI 与工厂生产](docs/native-ai.md)。

SourceScene v13 接通原图固定种类奖励箱和路径点重遮蔽，NativeEvents v3 按顺序尝试动作，单条失败后继续后续条目。真实付费护卫队掩护谭雅取得四个原始箱子，护甲、火力和速度使用来源永久倍率并可续档；其他箱子种类、动画/语音和无敌箱边界仍有缺口，见 [原始奖励箱与顺序动作](docs/native-crates.md)。

固定陆战样板入口：**遭遇战 → 地图类型 → 陆战样板 → 开始游戏**。地图为 64×64、玩家与一名 AI，可选盟军、苏军、中国、尤里及原有难度、资金等选项。更改尺寸、模式、种子或选择随机新图将返回程序生成地图；样板文件加载失败时禁止开始并提示原因。此入口仅用于本地遭遇战。

## 目录结构

```
src/
  core/    基础工具（RNG、数学）
  game/    数据表、地图生成、世界模拟、AI、战役/触发器、游戏主控/UI、存档
  gfx/     程序生成精灵 + 外部素材覆盖（assets.h 为命名约定索引）
  sfx/     音效、原版单位语音与外部音乐播放
  net/     局域网锁步联机（Winsock TCP 帧协议）
maps/      固定遭遇战地图；自定义地图支持地形、装饰、出生点和预置实体
assets/    【可选】外部素材覆盖目录（见上节）
saves/     存档（快速存档/读档）
```

## 架构与贡献

固定混合战斗压力检查：`ctest --test-dir build -C Release -R "ra2.combat_pressure_" --output-on-failure`；也可运行 `build/Release/ra2.exe --combat-pressure-regression 3 review_out/combat-pressure`（场景 0..3，省略场景则全部）。四场使用正常武器和血量，覆盖 200/500 分组对射与持续补兵混战，检查逐 tick 回放、当前快照续跑及两分辨率实际战斗绘制。补兵由测试夹具控制，无宏观 AI；World tick 与静态快照渲染分别计时，不能解释为自然对局或完整游戏 FPS。详细结果和未通过项见优化记录。

天启坦克已拆分地面炮与对空导弹，修正单发参数、精英地面弹头及小数射程，按模拟 tick 发射普通双发/精英地面四发，并接入规则驱动的范围伤害和地基逐格结算。专项检查：`ctest --test-dir build -C Release -R "^ra2.(apocalypse|burst|splash)$" --output-on-failure`（或对应 `--apocalypse-regression` / `--burst-regression` / `--splash-regression`）。当前范围距离仍为二维；原版墙钟时间、三维高度/查表及专属特效仍待校准，差异见 fidelity 文档。

模块边界、`Game`/`World` 职责与锁步不变量见 [docs/architecture.md](docs/architecture.md)。  
RA2 行为 fidelity 合同见 [docs/ra2-fidelity.md](docs/ra2-fidelity.md) / [docs/ra2-ui-fidelity.md](docs/ra2-ui-fidelity.md)。

原始步兵速度、水上动作和正常指令通关的范围见 [docs/native-motion.md](docs/native-motion.md)。专项入口为 `--native-motion-regression` / `--native-playthrough-regression`，对应 CTest `ra2.native_motion` / `ra2.native_playthrough`。原始车辆与舰船的计时、加速和存档范围见 [docs/native-movement.md](docs/native-movement.md)，专项为 `ra2.native_movement`。攻击移动独立保留交战前的目的地和队列模式，详见 [docs/attack-move.md](docs/attack-move.md)，专项为 `ra2.attack_move`。联机在开局前双向核对有效规则并等待确认，详见 [docs/lan-rules.md](docs/lan-rules.md)。当前默认为 World60 / App11 / 地图 SourceScene16 / 规则 Scene37 / LAN56；实验全局 AI 规则 Scene38 使用 World61，完整战役和完整复刻仍在推进。

RA2 / 尤里来源遭遇战的随机补给箱已接入原始权重、可奖励车辆、永久升级、经验晋升与寿命保存；新开来源规则对局使用新包。范围、World55 兼容与上一批 Release 150/150、ASan 42/42 验证见 [随机补给箱](docs/source-random-crates.md)。

普通地图的原始规则包接入范围、重建方法和菜单/LAN 边界见 [规则包说明](docs/rules-profiles.md)。地图 Scene16 保持，默认普通规则包使用 Scene37 并继续读取 Scene17–37 嵌入存档；实验 Scene38 入口尚未切换为默认策略。来源包已接通 DRED、V3 和尤里 Boomer 导弹，航母及驱逐舰反潜机以固定槽位区分在途、整备和损失再生；新融合对局也采用该控制器，详见 [舰载机记录](docs/aircraft-spawners.md)。

Scene28 规则一致性批恢复共用的生产/能力侧栏、来源偷科技门槛、工业工厂实际报价和超武来源清理，修复原始光棱坦克武器漏读及死亡连射导致的续档失败。该批最终 Release 151/151、相关 ASan 23/23 通过，121 组实际存档跨构建相同；这些是该历史批次的成绩。最新版本的实际验证范围及仍然简化的支援和特殊效果见 [项目审查方案](docs/review-plan-2026-11-15.md)。

本轮已修复问题与后续实施顺序见 [项目审查方案](docs/review-plan-2026-11-15.md)。影片恢复已按用户要求排除。

贡献约定（摘要）：

- 仿真逻辑进 `world_*.cpp`，UI/渲染/输入进 `game_*.cpp`，不要往 `game.cpp` / `world.cpp` 继续堆大块系统
- 玩家意图一律经 `World::Cmd` → `applyCmd` → `update`；改仿真字段须同步 `checksum` 与存档
- Cursor 规则在 `.cursor/rules/`（架构边界、子系统归属、任务模板）


遭遇战实施与未通过项见 [优化记录](docs/skirmish-optimization.md)。原生直升机展开素材可用 `python tools/ra2pack/gen_chopper_deploy.py` 从本地 MIX 重建；完整素材生成也包含此步骤。`ra2.voxel_projection` 检查体素八方向、队色明暗及直升机过渡锚点，`ra2.chopper_pipeline` 检查 SHP 导出契约。

`ra2.junction` 检查十字路口与环路的四向对流，包括单批 50 辆及连续四批 200 辆。单独诊断可运行 `ra2.exe --junction-regression 0`（场景 0–7；不带参数跑 0–3，完整 CTest 另含 4–7）；检查包含全员到达、合法占用、速度、确定性、当前快照续跑，以及 200 辆场景的等待/重寻和通行时间约束。无环分支路网按最远出口距离估算窗口，有环路网保留保守窗口；同朝向、共用连续开阔地形通道的平行出口可共同放行。场景 4 检查通道被地形打断时分开调度，场景 5 检查整图旋转，场景 6/7 检查行进中出现和移除建筑、当前物理出口失效及恢复；其他路网与动态占用组合仍需扩展。`World::trafficDebug()` 提供只读放行与路网诊断。

MSVC 的 ASan CTest 现在从所选编译器旁发现运行库并自动设置测试 PATH，无需手动改 shell 环境；该构建将四个路口场景拆为 `ra2.junction_0`–`ra2.junction_3`，每场保留相同场景与断言。普通 Release 仍使用 `ra2.junction`。

`ra2.movement` 的八个压力场景现在都会独立执行指令、逐 tick 比较回放并验证中途快照；日志中的 `replay_checks` 显示实际比较次数，200/500 单位各为 1000 次。压力窗口到达数与全员到达验收分开报告。

SourceScene v14 / NativeEvents v4 接入发现、受击、生产类型、选择及局部变量条件，并保留原始类型注册表和变量默认值。脚本卸载与设定局部变量已通过实际乘员、冻结续档和原始撤退队招募的隔离验证；自动混合运输编队和整关流程仍未完成，见 [原始事件条件与卸载](docs/native-conditions.md)。

遭遇战与联机大厅现可选择融合扩展、红警2基础和尤里基础规则；国家、随机、起始来源身份、存档与重开保持所选规则。规则包仍在完善，具体范围、旧档限制及验证见 [规则选择](docs/rules-selection.md)。

来源回收、反应炉吸收、克隆联动及间谍矩阵见 [来源经济与间谍](docs/source-economy-and-espionage.md)，专项入口为 `--source-economy-regression` / `ra2.source_economy`。

独立支援冷却、原始投送人数与出生老兵经验的接入范围见 [支援与经验](docs/source-support-and-veterancy.md)，专项为 `ra2.source_support`。完整运输机投送演出仍在后续工作中。

新增 4/8 人自然整局及真实中盘基线见 [自然对局与动态帧](docs/natural-matrix-and-dynamic-performance.md)。[建筑缓存优化](docs/render-cache-performance.md) 将本机八人配对短测整帧 p95 从 20–21ms 降至 8.6–9.0ms，画面与保存一致；相关 Release/ASan 各 35/35 通过。新增四局中两局未在 360000 tick 内决胜，失败仍保留；冷态卡顿、AI 后期恢复、完整矩阵与人工实玩继续处理。

[AI 建造厂恢复](docs/ai-construction-recovery.md) 已修复现有/排队/运输中基地车漏计及展开受阻不换位。两种构建相关各 26/26，启动入口补齐后各追加 1/1，通过 123 条机制断言和隐藏窗口检查，50 对实际保存跨构建一致。两局 YR 重开的基地车峰值均为一辆，但在原定上限内仍未决胜；后期进攻效率继续验收。

[AI 进攻与战术视野](docs/ai-assault-and-vision.md) 已改为依据已侦察设施持续推进，并过滤迷雾中的防守/集火目标。相关 Release/ASan 各 28/28、78 条新断言及 62 对实际跨构建保存通过；三局普通重开自然结束，42 个 App 检查点精确。旧四人残局仍未结束，已确认恢复后的建造厂堵住重工出口，继续修复。本机短测 p95 约 5.7–9.1ms，但预热出现 3.1 秒峰值，完整性能验收仍未完成。

[AI 工厂出口恢复](docs/ai-factory-outlet-recovery.md) 已补齐基地车与普通建筑选址的出口保护，并通过正常付费备用重工释放真实残局的已付款订单。相关 Release/ASan 各 28/28，162 条新布局检查和 102 对跨构建保存通过。旧残局仍未决胜，另已发现残余建筑被进攻名单排除，以及 DMISL 长期绕圈；完整复刻与性能验收继续。

[后期导弹与目标清理](docs/late-combat-and-target-cleanup.md) 已修复 DMISL 长期近地绕圈、残余已观察建筑被排除，以及普通移动占用侦察名额的问题。LAN 升为 47，保存仍为 World56/App11。首轮全量 153/154 的 V3 精度回归已修正，之后相关 Release23/23、ASan16/16；136 对最终保存相同。两份旧残局和三局普通重开均自然结束，完整四阶段仍在继续。

运输机、空中投送、下降与移动侦察视野的实现和待校准边界见 [支援任务说明](docs/support-delivery.md)，专项入口为 `--support-delivery-regression`。该批相关 Release/ASan 各 21/21、空闲性能 7/7，64 对保存一致；最终三局多人仍超时，完整复刻尚未完成。

来源棱镜现按格环和同格首对象选择分裂目标，并保存入格顺序；Scene32 旧规则保持历史行为。原始弹丸结算时序仍待校准，详见 [棱镜格子扫描](docs/native-shrapnel.md)。
