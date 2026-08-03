# RA2 UI / 交互保真验收标准

权威来源：RA2 1.006 手册、ModEnc、Ares `MouseCursors.txt`、本机 MIX。

## 1. 预置建筑（油田等）

| 项 | 原作 | 验收 |
|----|------|------|
| ID | `CAOILD` / `CAHOSP`（非错剧院雪地替身） | 温带贴图正确 |
| Remapable | `no` | 无阵营亮红乱 remap |
| ActiveAnim | `CAOILD_A` / `CAHOSP_A` 循环 | 局内泵臂/灯火动画 |
| 预置 | 无建造 mk | `free_` 直接成品 |

## 2. 采矿车

| 项 | 原作 | 验收 |
|----|------|------|
| HARV Storage | 40 | 苏军矿车满载 40 |
| CMIN Storage | 20 | 盟军超时空 20 |
| 挖矿动画 | VXL，无独立 dig SHP | 空/满体 + 卸货 HORV/CMON |
| **挖掘距离** | 走到矿格上/邻格再挖 | Chebyshev≤1（**不是** NearScan） |
| **TiberiumNearScan=6** | 采空一格后在同矿脉内找下一格的半径 | 挖空后 `findNearestOre(..., 6)` |
| **TiberiumFarScan=48** | 找新矿脉 | 回厂后再出发 / 无近矿时用 48 |
| **DockUnload** | 精炼厂 4×3 地基上 **(3,1)** 东侧垫；队列 `(4,1)` | 回厂路径/到达用该格，非南缘 |
| Stop / Move | 退出 Harvest，直到再次采矿令 | `autoHarvest=false`；Move **不**恢复 |
| 矿上光标 | Harvest→**AttackMove@404** | 旋转攻击移动图标 |

## 3. 金钱显示

| 项 | 原作 | 验收 |
|----|------|------|
| 字形 | 侧栏青像素数字 + 深描边 | 非 7 段 LED |
| 过滤 | 点采样 | 无糊边 |

## 4. 底栏

权威：MIX `ui.ini` `[AdvancedCommandBar] ButtonList=Team01,Team02,TypeSelect,Deploy,Guard,PlanningMode`。

| 槽位 | 命令 | 键位 |
|------|------|------|
| 0 | Team01（编队 1） | 1 |
| 1 | Team02（编队 2） | 2 |
| 2 | TypeSelect / 规划格 | T |
| 3 | Deploy | D |
| 4 | Guard | G |
| 5 | PlanningMode / Waypoint | Z |

Stop / Scatter 仅保留键盘（S / X），不占默认六键。Unload 走快捷键/运输专用。

## 5. 侧栏生产

| 项 | 原作 | 验收 |
|----|------|------|
| 进度视觉 | `gclock2.shp` 55 帧扫臂 | cameo **不旋转** |
| 扣款 | 按 tick；缺钱可开工；钱不够停 | 同左 |
| Hold | 右键暂停；左键继续；再右键取消退款 | 同左 |
| 队列 | ~30/类 | ≥30 |
| **页签** | 建筑 / 防御(BuildCat=Combat，含超武建筑) / 步兵 / 单位(车+空+海) | 火箭兵∈步兵 |
| **Cameo 序** | rules 类型表声明序 | **不按造价排序** |

## 6. 光标（Ares 默认表）

| 名称 | 起始帧 | 帧数 | Interval |
|------|--------|------|----------|
| Default | 0 | 1 | 0 |
| Move | 31 | 10 | 4 |
| NoMove | 41 | 1 | 0 |
| Attack | 53 | 5 | 4 |
| Enter | 89 | 10 | 4 |
| Deploy | 110 | 9 | 4 |
| Sell | 129 | 10 | 4 |
| Repair | 150 | 20 | 4 |
| AttackMove（矿上 / 按住A） | 404 | 9 | 4 |

`mouse.shp` 须全量导出（≥413 帧）。热点：Default=Left,Top；其余 Center,Middle。按住 **A** 下令移动时显示 AttackMove@404。

## 7. 比例与地面 / 音效

| 项 | 原作 | 验收 |
|----|------|------|
| 瓦片 | 源 60×30 → 引擎 64×32 | `BLD_SCALE = 64/60` |
| VXL | ~1 voxel ≈ 1 原作像素 | 渲染 `px_per_voxel = 64/60`，**禁止** fit×1.22 |
| 地面 | TMP 模板 + 变体 + 岸线；**不做跨格糊边** | POINT；靠 TMP/岸线，非 lerp 糊缝 |
| audio.bag | mono IMA **ChunkSize=512** | `gen_audio.py` 读 flags/ChunkSize |
| 开火 SFX | Report→专用样本；攻击音 ≤0.35s | `WeaponDef.report` + `gen_audio.py` |
| 载具近亲占位 | 本 MIX 缺 VXL 时用近亲车体 | APOC→MTNK；MGTK→RTNK（见 §8） |
| 油田 Remapable | no | 运行时 `cid=-2` 跳过 house remap |

## 8. 载具近亲占位（gen_assets）

本 MIX 无独立 VXL 时 `gen_assets.py` 用 rules Image 近亲渲染；引擎逻辑 ID 不变。

| 引擎单位 | 占位 VXL/SHP | 说明 |
|----------|----------------|------|
| Apocalypse / BattleFortress / MasterMind | MTNK（APOC rules 指向 mtnk 系） | 天启级重坦共用大型车体 |
| MirageTank | RTNK（MGTK rules；本 MIX 有 ltnk/mtnk，幻影用 MGTK→RTNK 表） | 伪装坦克 |
| RobotTank | MTNK | 轻坦占位 |
| Type99 | HTNK | 99 式→犀牛 |

验收：占位仅影响 **贴图**；开火 Report/SFX、装甲、造价仍走引擎 `UnitType` 表。

## 验收记录（2026-08-03）

| 项 | 结果 |
|----|------|
| `docs` + MIX/`gclock2`/Ares 证据 | `docs/ra2-ui-fidelity.md` |
| 光标全量 + AttackMove@404 | 启动日志 `loaded up to frame 450`；`cursor_404.png` |
| cameo + gclock2 / Hold / 队列~30 | 截图可见扫臂；`held` 存档 V13 |
| 金钱青像素字 | `assets/gui/money_digits/num_*.png`；截图清晰 |
| 底栏六键 | Team01/Team02/TypeSelect/Deploy/Guard/PlanningMode |
| HARV40/CMIN20 | `World::harvesterCapacity` |
| 油井/医院 ActiveAnim | 运行时 `bld_*_a_f*` 叠绘 |
| 启动 | 仅用 `启动游戏.bat` → Release |

油井 SHP 南角仅 1–2px 细尖 + 等距抬高水泥垫，肉眼会像略“悬空”；锚点仍钉占地南角（与 `bldScreenPos` 一致），阴影钉可见地基行。
