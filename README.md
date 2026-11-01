# OpenRA2

红色警戒 2 · 共和国之辉 —— C++ 复刻版。免费开源软件，不作商业用途。
全部美术与音频默认由代码程序化生成（零外部资源依赖），同时支持 `assets/` 目录外部素材热覆盖。

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

- **遭遇战**：1~7 个 AI 槽位（颜色/国家/难度/人格），11 个国家分属盟军、苏军、中国、尤里四阵营；Setup 可选 Battle、Free For All、Unholy Alliance、Megawealth、Land Rush、Meat Grinder、Naval War，以及 Short Game、AI 联盟、共享视野、补给箱和超级武器
- **战役模式**：32 个任务（中国 8 / 盟军 8 / 苏军 8 / 尤里 8），固定种子地图 + 波次增援脚本 + 触发器系统（条件：计时/全灭/进区/资金；动作：刷兵/播报/给钱/开图/胜负），部分关卡为 `maps/*.txt` 手工地图
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
- **完整经济**：矿脉分布、采矿车（超时空/武装/普通）自动采矿卸载、电力系统（低电减速生产）
- **海军全套**：海军船厂、台风潜艇、海蝎、神盾、无畏/航母、海豚/巨型乌贼、两栖运输船、陆/水/两栖三寻路域
- **超级武器**：战术核弹、闪电风暴、铁幕、超时空传送，建造即向敌方广播侦测警告
- **特殊机制**：超时空军团兵、幻影坦克伪装、辐射区、V3 远程溅射、磁爆链式伤害、间谍渗透（重置迷雾/偷科技/重置超武）、尤里心灵控制（可夺回）、海豹部队 C4 爆破、偷科技解锁单位（超时空/心灵突击队）
- **中立科技建筑**：油井 / 医院 / 机械店 / 科技机场 / 秘密实验室 / 民房，工程师占领生效
- **遭遇战 AI**：四阵营合法科技树、固定种子确定性门禁、海陆适配、经济防停摆、尤里生化反应堆/回收炉使用、编队进攻和超武选点
- **战场体验**：EVA 事件播报（中英双语字幕）、BGM（M 开关）、编队/散布/警戒、小地图、战争迷雾、老兵等级
- **界面国际化**：全部 UI / 名称 / 战役简报中英双语热切换（双语字模全量预载）

## 素材系统（更换为自己的素材）

默认素材 100% 程序化生成；在 exe 同级创建 `assets/` 目录后，**同名文件自动覆盖**，缺失项回退程序生成——可只替换任意子集，无需打包、无需改代码、重启即生效。

```
assets/
  sprites/   PNG 图片（命名规则见下表）
  sfx/       音效：<名>.wav / .ogg / .mp3（shot、cannon、tesla、nukeblast 等 25 种）
  music/     BGM：任意 .ogg/.mp3/.wav/.flac，全部文件自动组成播放列表
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
- 方向可不全：缺哪个方向就用程序生成的补

## 自动化验证

```powershell
ctest --test-dir build -C Release --output-on-failure # 自动回归合集（含窗口交互、截图、耐久与网络）
.\build\Release\ra2.exe --smoke 20000          # 无头跑 N 帧遭遇战 + 全要素强制验证 + 三方视角截图
.\build\Release\ra2.exe --smoke-campaign 31 5000   # 无头跑第 32 关 5000 帧（任务编号 0~31）
.\build\Release\ra2.exe --campaign-matrix 60   # 32 关启动/触发器/胜负矩阵
.\build\Release\ra2.exe --hud-regression review_out/hud # 四阵营双语侧栏与生产提示截图
.\build\Release\ra2.exe --skirmish-regression 72000 -1 0 --require-outcomes # 12 局自然开局必须结算
.\build\Release\ra2.exe --magnetron-regression review_out/magnetron # 主副武器、规则覆盖、续档与实绘
.\build\Release\ra2.exe --ai-attack-regression # 合法攻击、侧翼迷雾与四阵营防空选择
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

固定陆战样板入口：**遭遇战 → 地图类型 → 陆战样板 → 开始游戏**。地图为 64×64、玩家与一名 AI，可选盟军、苏军、中国、尤里及原有难度、资金等选项。更改尺寸、模式、种子或选择随机新图将返回程序生成地图；样板文件加载失败时禁止开始并提示原因。此入口仅用于本地遭遇战。

## 目录结构

```
src/
  core/    基础工具（RNG、数学）
  game/    数据表、地图生成、世界模拟、AI、战役/触发器、游戏主控/UI、存档
  gfx/     程序生成精灵 + 外部素材覆盖（assets.h 为命名约定索引）
  sfx/     程序合成音效与 BGM + 外部音频加载
  net/     局域网锁步联机（Winsock TCP 帧协议）
maps/      手工战役地图（文本格式：地形/装饰/出生点/预置实体）
assets/    【可选】外部素材覆盖目录（见上节）
saves/     存档（快速存档/读档）
```

## 架构与贡献

固定混合战斗压力检查：`ctest --test-dir build -C Release -R "ra2.combat_pressure_" --output-on-failure`；也可运行 `build/Release/ra2.exe --combat-pressure-regression 3 review_out/combat-pressure`（场景 0..3，省略场景则全部）。四场使用正常武器和血量，覆盖 200/500 分组对射与持续补兵混战，检查逐 tick 回放、当前快照续跑及两分辨率实际战斗绘制。补兵由测试夹具控制，无宏观 AI；World tick 与静态快照渲染分别计时，不能解释为自然对局或完整游戏 FPS。详细结果和未通过项见优化记录。

天启坦克已拆分地面炮与对空导弹，修正单发参数、精英地面弹头及小数射程，按模拟 tick 发射普通双发/精英地面四发，并接入规则驱动的范围伤害和地基逐格结算。专项检查：`ctest --test-dir build -C Release -R "^ra2.(apocalypse|burst|splash)$" --output-on-failure`（或对应 `--apocalypse-regression` / `--burst-regression` / `--splash-regression`）。当前范围距离仍为二维；原版墙钟时间、三维高度/查表及专属特效仍待校准，差异见 fidelity 文档。

模块边界、`Game`/`World` 职责与锁步不变量见 [docs/architecture.md](docs/architecture.md)。  
RA2 行为 fidelity 合同见 [docs/ra2-fidelity.md](docs/ra2-fidelity.md) / [docs/ra2-ui-fidelity.md](docs/ra2-ui-fidelity.md)。

贡献约定（摘要）：

- 仿真逻辑进 `world_*.cpp`，UI/渲染/输入进 `game_*.cpp`，不要往 `game.cpp` / `world.cpp` 继续堆大块系统
- 玩家意图一律经 `World::Cmd` → `applyCmd` → `update`；改仿真字段须同步 `checksum` 与存档
- Cursor 规则在 `.cursor/rules/`（架构边界、子系统归属、任务模板）


遭遇战实施与未通过项见 [优化记录](docs/skirmish-optimization.md)。原生直升机展开素材可用 `python tools/ra2pack/gen_chopper_deploy.py` 从本地 MIX 重建；完整素材生成也包含此步骤。`ra2.voxel_projection` 检查体素八方向、队色明暗及直升机过渡锚点，`ra2.chopper_pipeline` 检查 SHP 导出契约。

`ra2.junction` 检查十字路口与环路的四向对流，包括单批 50 辆及连续四批 200 辆。单独诊断可运行 `ra2.exe --junction-regression 0`（场景 0–7；不带参数跑 0–3，完整 CTest 另含 4–7）；检查包含全员到达、合法占用、速度、确定性、当前快照续跑，以及 200 辆场景的等待/重寻和通行时间约束。无环分支路网按最远出口距离估算窗口，有环路网保留保守窗口；同朝向、共用连续开阔地形通道的平行出口可共同放行。场景 4 检查通道被地形打断时分开调度，场景 5 检查整图旋转，场景 6/7 检查行进中出现和移除建筑、当前物理出口失效及恢复；其他路网与动态占用组合仍需扩展。`World::trafficDebug()` 提供只读放行与路网诊断。

MSVC 的 ASan CTest 现在从所选编译器旁发现运行库并自动设置测试 PATH，无需手动改 shell 环境；该构建将四个路口场景拆为 `ra2.junction_0`–`ra2.junction_3`，每场保留相同场景与断言。普通 Release 仍使用 `ra2.junction`。

`ra2.movement` 的八个压力场景现在都会独立执行指令、逐 tick 比较回放并验证中途快照；日志中的 `replay_checks` 显示实际比较次数，200/500 单位各为 1000 次。压力窗口到达数与全员到达验收分开报告。
