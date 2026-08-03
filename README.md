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
| R / Delete | 设集结点 / 卖建筑 |
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

要点：

- 单位/建筑/音效的内部名称见 [src/gfx/assets.h](src/gfx/assets.h) 的 `unitAssetName / bldAssetName / sfxAssetName`（如 `unit_nighthawk_d2.png`、`bld_cloningvat.png`）
- **阵营换色**：需随玩家变色的像素涂纯红 `(255,0,0)`，运行时自动替换为阵营色（暗部 `(176,0,0)`、亮部 `(255,128,128)`）
- **锚点**：单位/炮塔按画布中心，建筑按底边中心（下留 4px）
- 方向可不全：缺哪个方向就用程序生成的补

## 自动化验证

```powershell
ctest --test-dir build -C Release --output-on-failure # 无窗口纯逻辑测试（RNG/坐标/规则辅助/寻路/采矿）
.\build\Release\ra2.exe --smoke 20000          # 无头跑 N 帧遭遇战 + 全要素强制验证 + 三方视角截图
.\build\Release\ra2.exe --smoke-campaign 31 5000   # 无头跑第 32 关 5000 帧（任务编号 0~31）
.\build\Release\ra2.exe --campaign-matrix 60   # 32 关启动/触发器/胜负矩阵
.\build\Release\ra2.exe --play-test 5          # 自动完整开局游玩测试（含 UI 点击）
.\build\Release\ra2.exe --net-host 900         # 联机自测主机端（配合 --net-client 双进程）
```

`--smoke`、`--smoke-campaign` 与 `--campaign-matrix` 会汇总每项断言；任一断言失败、场景无法建立或测试抛出异常时，进程返回非零。

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

模块边界、`Game`/`World` 职责与锁步不变量见 [docs/architecture.md](docs/architecture.md)。  
RA2 行为 fidelity 合同见 [docs/ra2-fidelity.md](docs/ra2-fidelity.md) / [docs/ra2-ui-fidelity.md](docs/ra2-ui-fidelity.md)。

贡献约定（摘要）：

- 仿真逻辑进 `world_*.cpp`，UI/渲染/输入进 `game_*.cpp`，不要往 `game.cpp` / `world.cpp` 继续堆大块系统
- 玩家意图一律经 `World::Cmd` → `applyCmd` → `update`；改仿真字段须同步 `checksum` 与存档
- Cursor 规则在 `.cursor/rules/`（架构边界、子系统归属、任务模板）
