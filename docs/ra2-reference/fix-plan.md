# 参考文档 vs 当前实现 — 缺口与修复计划

对照基准：

- 官方契约：`docs/ra2-reference/**`（单位/建筑/机制/战役/风格）
- 既有差异表：`docs/ra2-fidelity.md`
- 实现：`src/game/**`、`assets/rules/rules.ini`、`assets/campaigns/**`

本文是**修复路线图**，不是已完成清单。融合项（中国阵营、自制战役、共和国之辉共享）保留，但不得冒充“官方一致”。

---

## 0. 总判读

| 层面 | 结论 |
|------|------|
| 遭遇战主循环 | 可玩：MCV、采矿、生产、四阵营、多数超武与标志单位有主路径 |
| 官方数值保真 | **弱**：多数武器仍 Legacy 三系数；Cost/HP/Power/超武冷却未系统对齐 `rulesmd` |
| YR 特色 | **参差**：Dominator/主脑/磁电/飞碟/盖特有主路径；**Force Shield、机器人指挥中心、尤里克隆缸归属**等缺失或错位 |
| 战役 | **大偏离**：32 关自制波次关，非官方 12+12+7+7 地图/触发/FMV |
| 表现 | **占位级**：程序精灵/合成音效为主；风格文档已定义，资产未跟上 |

---

## 1. 问题清单（按严重度）

### P0 — 阻塞“官方战役/YR 支援”叙事

| ID | 问题 | 参考期望 | 当前 | 修复层 |
|----|------|----------|------|--------|
| C1 | 战役不是官方关 | `campaign/ra2-*.md`、`yr-campaigns.md` | `assets/campaigns` 32 关（中8+盟8+苏8+尤8），多为 Wave | campaign 资产 + `game_triggers.cpp` |
| C2 | 缺 FMV/简报流程 | 战役 mechanics | 仅 INI 文本 brief | `game_campaign.cpp` + 资源策略 |
| S1 | **Force Shield 完全缺失** | `superweapons.md`、YR 全阵营 | 无 SW/无逻辑 | `data.h` + `world_superweapons.cpp` + HUD |
| D1 | 文档与 fidelity 未自动对账 | — | 两套叙述易漂移 | 本计划 + 定期回写 `ra2-fidelity.md` |

### P1 — 明显玩法错误 / 版本语义错误

| ID | 问题 | 参考期望 | 当前 | 修复层 |
|----|------|----------|------|--------|
| B1 | 尤里 **Cloning Vats** 未归尤里 | YR 尤里建筑 | `CloningVat` 仍偏苏军掩码 | `data.cpp` / `rules.ini` |
| B2 | **Robot Control Center** 缺失 | YR 盟军；断电机器人停 | `RobotTank` 前置挂 Radar | 新 `BldType` + 断电逻辑 |
| B3 | 苏军 RA2 **Yuri** vs YR 尤里阵营混淆 | version-diff | `UnitType::Yuri` 仍苏军可造 | 模式/版本开关或拆类型 |
| U1 | 无 **Yuri Prime** / **Chrono Ivan** | infantry / 渗透 | 枚举缺失；英雄仅 Tanya/Boris | `data.h` + 生产唯一性 |
| W1 | 武器大多 **Legacy** 伤害 | `weapons-warheads.md` | `rules.ini` 几乎无 `Warhead=` | 从 rulesmd 导入映射 |
| N1 | 核电站功率/毁伤 | stats +2000、核爆 | 功率偏低、无熔毁辐射 | `data.cpp` + `world_combat.cpp` |
| L1 | 超武/精炼器 **每玩家限一** | 手册 | 生产无数量上限 | `world_production.cpp` |

### P2 — 主路径有、边界/数值不对

| ID | 问题 | 参考 | 当前缺口 |
|----|------|------|----------|
| E1 | 矿/宝石价值、Purifier、工业厂倍率 | crates-garrison / stats | 近似值；Purifier 无限造 |
| E2 | 油田占领瞬间奖金 + 被动节奏 | tech-buildings | 仅周期给钱，无 +1000 占领奖 |
| SW1 | 铁幕时长、Force Shield 帧数、超武冷却 | superweapons（750/500/1000 帧等） | 项目自定义秒数偏短 |
| SW2 | 基因突变器动物/英雄/驻军矩阵 | fidelity | 步兵无条件变 Brute |
| MC1 | 心灵塔容量 3 | defenses | 无限/未封顶 |
| IFV1 | IFV 全乘员表 | transport-ifv | 部分乘员未映射 |
| SPY1 | 渗透矩阵与实验室偷科技单位集 | mechanics-errata | 有路径；高科额外偷钱等需核对 |
| NAV1 | 反潜/乌贼/航母补机 | vehicles | 简化枚举逻辑 |
| AIR1 | 战机必须回空指部 | buildings | 弹药/坠毁规则不完整 |
| TEC1 | 空指 vs 雷达 vs 心灵雷达 | buildings | 多为通用 `Radar` |
| SUP1 | Spy Plane / Psychic Reveal | version-diff | 缺失 |
| UI1 | shroud 瓦片、原版 SFX/语音 | style/*、ra2-ui-fidelity | 程序雾、合成音 |

### P3 — 融合保留（不修成“删掉”，只隔离）

| ID | 项 | 处理 |
|----|-----|------|
| F1 | 中国第四阵营 | 保留；加 `OfficialOnly`/`Fusion` 配置或文档门禁 |
| F2 | 中国共享幻影/天启/基洛夫/超武 | 标融合；可选官方模式关闭 |
| F3 | 尤里单人 8 关自制战役 | 标融合；官方模式不提供尤里剧情线 |
| F4 | 奴隶矿车部署近似 | 保留可玩；远期换官方建筑语义 |

---

## 2. 修复阶段（建议顺序）

### 阶段 A — 数据与规则对齐（1–2 周量级）

**目标**：让 `assets/rules/rules.ini` 成为“可追溯的 rulesmd 投影”，而不是手填近似。

1. 写/用导入脚本：从合法 `rulesmd.ini` 抽出 Cost/Strength/Power/Storage/主要 Weapon→Warhead→Verses → 生成映射表（CSV）+ 补丁 `rules.ini`。  
2. 为主要武器加 `Warhead=`，减少 Legacy 路径。  
3. 校正：核电 Power、间谍卫星造价、Grinder、SlaveMiner、Battle Fortress HP 等（见 `stats-tables.md` / `mechanics-errata.md`）。  
4. 超武 `ChargeTime` 按 `superweapons.md` 换算到引擎 tick。  
5. 生产限一：OrePurifier、CloningVat、各超武建筑。

**验收**：`logic_tests`/`game_tests` 断言抽样单位 Cost/HP；至少 20 个主武器走非 Legacy；限一建筑第二次开工失败。

**层**：`tools/` + `assets/rules/rules.ini` + `data.cpp` 加载；禁止只改中文攻略记忆。

---

### 阶段 B — YR/标志模拟补洞（紧随 A）

**目标**：补上文档已写、玩家一眼能看出的洞。

| 优先 | 工作项 | 文件 |
|------|--------|------|
| B1 | Force Shield：Battle Lab 解锁；建筑无敌 + 断电 | `world_superweapons.cpp`、`world_update.cpp`、HUD |
| B2 | Robot Control Center + 断电停机器人 | `data.h`、`world_update.cpp` |
| B3 | CloningVat 尤里归属；苏军 RA2/YR 开关若需要 | `data.cpp` |
| B4 | Psychic Tower 心控上限 3 | `world_buildings.cpp` |
| B5 | Yuri Prime（可先渗透解锁）+ 英雄唯一 | `data.h`、`world_production.cpp` |
| B6 | 铁幕时长、基因突变器矩阵收紧 | `world_superweapons.cpp` |
| B7 | Spy Plane / Psychic Reveal | `world_special.cpp` |
| B8 | 油田占领 +1000；核电站毁伤 | `world_buildings.cpp`、`world_combat.cpp` |

**验收**：每项至少 1 条确定性 smoke（仿现有 Dominator/MasterMind 测试风格）。

---

### 阶段 C — 科技树与国家（并行可插）

1. 拆分或标注：Airforce HQ / Radar / Psychic Radar 前置差异。  
2. 逐国 `countryReq` 断言（法巨炮、德坦克杀手、韩黑鹰、美伞兵组成等）。  
3. Tech Airport 伞兵 6/9/6；美国 Airborne 与机场独立冷却。  
4. 心控下 MCV 展开/打包限制（YR 1.001）。

**验收**：错误国家无法开工；正确国家可造；伞兵数量按阵营。

---

### 阶段 D — 战役双轨策略

**不要**把现有 32 关强行改名冒充官方。采用双轨：

| 轨道 | 内容 | 用途 |
|------|------|------|
| Fusion（默认） | 现有 32 关 + 中国/尤里剧情 | 当前产品体验 |
| Official（远期） | 按 `campaign/ra2-allied.md` 等建关卡包 | 保真验收 |

Official 轨最低增量：

1. 盟军 12 + 苏军 12 的**目标文本 + 胜负触发**骨架（可仍用简化地图）。  
2. 必做触发：英雄死亡失败、摧毁信标/放大器/超武建筑胜利、护送/保护。  
3. YR Time Lapse / Time Shift：电厂供电时间机器 + 摧毁 Dominator（可单图原型）。  
4. FMV 可缺；简报与 Objective HUD 不可缺。

**验收**：官方轨关卡列表与文档代号一致；至少 3 关（Lone Guardian、Hail to the Chief、Time Lapse）端到端可通关脚本化。

---

### 阶段 E — 表现层（可与 D 并行，不挡模拟）

按 `docs/ra2-reference/style/**` 与 `ra2-ui-fidelity.md`：

1. 优先：开火 Report 短 SFX、EVA 关键事件音、cameo/gclock 已有项巩固。  
2. 单位语音库（选中/移动/攻击）分批。  
3. shroud 瓦片、油田 ActiveAnim 等 UI 保真项按现有清单扫尾。  
4. 音乐保持自有曲库；风格对齐 Klepacki，不非法塞进原版 OST。

**验收**：对照 `style/` 的“要/不要”做人工抽检表；关键 SFX 自动化可选。

---

## 3. 明确不做 / 延后

- 一次性手抄全部 100+ Warhead 进 Markdown（用 CSV/脚本）。  
- 无合法 MIX 时声称“已 100% rulesmd”。  
- 删除中国阵营以“更像官方”（除非产品要求 Official 模式）。  
- 在未完成遭遇战数值校准前大拆战役地图管线。

---

## 4. 文档同步义务

每完成一阶段：

1. 更新 `docs/ra2-fidelity.md` 对应行状态（一致/部分/融合/缺失）。  
2. 若实现打脸参考文档，改 `ra2-reference` 并注明以何版 rules 为准。  
3. 新自动化测试名写入 fidelity“可执行验收”列。

---

## 5. 建议里程碑

| 里程碑 | 包含 | 退出标准 |
|--------|------|----------|
| M1 规则投影 | 阶段 A | 主武器非 Legacy；限一建筑；抽样数值断言绿 |
| M2 YR 补洞 | 阶段 B 前 5 项 | Force Shield + RCC + 心灵塔帽 + 克隆归属 + 烟雾测试绿 |
| M3 国家/科技树 | 阶段 C | 9 国特色断言绿 |
| M4 官方战役原型 | 阶段 D 三关 | 脚本通关 |
| M5 表现及格线 | 阶段 E 音效+UI 抽检 | 检查表签字 |

---

## 6. 与现有代码的对应入口

| 领域 | 优先文件 |
|------|----------|
| 规则加载 | `src/game/data.cpp`、`assets/rules/rules.ini` |
| 超武 | `src/game/world_superweapons.cpp` |
| 特殊/渗透/伞兵 | `src/game/world_special.cpp` |
| 建筑行为 | `src/game/world_buildings.cpp` |
| 生产限一/克隆 | `src/game/world_production.cpp` |
| 战役 | `src/game/campaign.*`、`game_triggers.cpp`、`assets/campaigns/` |
| 测试 | `src/game/game_tests.cpp`、`logic_tests` 目标 |
| UI/音 | `game_hud.cpp`、`sfx/sound.cpp`、`docs/ra2-ui-fidelity.md` |

---

## 7. 一句话优先级

**先把 rules 投影与 Force Shield / RCC / 弹头矩阵补上（遭遇战保真），再开官方战役骨架；表现层跟风格文档走，不挡模拟。**
