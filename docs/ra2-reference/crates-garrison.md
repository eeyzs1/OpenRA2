# 空投箱、驻军与地形杂项

## 1. 空投箱 (Crates)

遭遇战选项开启 Crates 时，地图周期性生成木箱/水箱。规则摘自 YR `rulesmd.ini` `[CrateRules]` / `[Powerups]`。

### 生成

| 参数 | 值 | 含义 |
|------|-----|------|
| CrateMinimum | 1 | 最少箱数 |
| CrateMaximum | 255 | 上限 |
| CrateRegen | 3 | 约平均每 3 分钟再生 |
| CrateRadius | 3.0 | 范围类增益半径（格） |
| FreeMCV | yes | 无建筑但仍有钱时，箱子可给免费 MCV（多人） |
| WaterCrateImg | WCRATE | 水上箱子可用 |

### 多人权重（Powerups 前缀数字为相对权重）

| 效果 | 权重 | 音效键 | 数值 | 说明 |
|------|------|--------|------|------|
| Money | 20 | MONEY | 最多约 2000$ | 金钱 |
| Veteran | 20 | VETERAN | +1 军衔级 | 附近单位升级 |
| Unit | 20 | (无图) | 随机载具 | 免费车 |
| Armor | 10 | ARMOR | ×1.5 承伤 | 范围增益 |
| Firepower | 10 | FIREPOWR | ×2.0 火力 | 范围增益 |
| Reveal | 10 | REVEAL | — | 揭开全图迷雾 |
| Speed | 10 | SPEED | ×1.2 | 范围加速 |
| HealBase | 10 | HEALALL | — | 全部建筑回满 |

权重为 0 的条目（Invulnerability、Cloak、Darkness、Explosion、ICBM 等）在默认 YR 遭遇战中**不出现**，属遗留/战役配置。

单人任务银箱/木箱另有 `SilverCrate` / `WoodCrate` / `SoloCrateMoney=5000` 等覆盖。

### 拾取注意

- 范围增益影响箱附近单位（半径约 3 格）。
- 海军可拾水箱。
- 摧毁部分中立物（如油田）也可能掉钱箱。

---

## 2. 民用建筑驻军 (Garrison)

### 基本规则

- GI / Conscript / Initiate 等基础步兵可进入**可驻军**民用建筑。
- 驻军后：步兵受建筑保护，从窗口对外射击，火力显著提升。
- 建筑被毁则内部步兵通常一并死亡或抛出（以引擎为准）。
- 清空驻军：命令撤离或工程师/特殊清楼手段。

### 可建造驻军工事

| 建筑 | 阵营 | 谁可进 | 特点 |
|------|------|--------|------|
| Battle Bunker | 苏军 YR | 主要为 Conscript | 可像普通建筑维修；Cost 约 500 |
| Tank Bunker | 尤里 | 有炮塔、非炮兵车辆 | 固定车体、射速↑；Cost 约 400 |

### 与心灵控制

- **普通心控 / Psychic Dominator**：驻军单位通常**免疫**（Dominator 明确不控驻军）。
- 心控建筑本身（Yuri Prime）与控内部步兵是不同逻辑。

### UI

侧栏/血条 pip 可显示驻军数量（原作 pip 系统）。

---

## 3. 矿石价值（规则层）

YR `rulesmd` 中矿种（引擎仍称 Tiberium）：

| 内部名 | 玩家认知 | Value/bail |
|--------|----------|------------|
| Riparius | 普通矿 (Ore) | 25 |
| Cruentus | 宝石 (Gems) | 50 |
| Vinifera | 另种矿/晶 | 25 |

矿车 Storage：Chrono Miner **20**；War Miner **40**；Slave Miner 体系另算。

盟军 Ore Purifier：精炼收入约 **+25%**。

---

## 4. 桥与地形

- 桥可被摧毁（`DestroyableBridges=yes`）。
- 海豹/谭雅/伊万对**桥屋**下药可毁桥；工程师进桥屋可修。
- 两栖单位（运输、Robot Tank 等）可过水；普通坦克下水销毁（Chronoshift 亦可利用）。

---

## 5. 生产与电力相关常数（General 摘录）

| 参数 | 约值 | 含义 |
|------|------|------|
| BuildSpeed | 0.7 | 造价 1000 物品的基准建造分钟数相关 |
| MinLowPowerProductionSpeed | 0.5 | 低电最低生产速度因子 |
| MaxLowPowerProductionSpeed | 0.8 | 低电最高生产速度因子 |
| VeteranSpeed | 1.2 | 老兵速度倍率 |
| VeteranArmor | 1.5 | 老兵承伤（伤害÷此值） |

精确建造时间仍由各对象 `BuildTime` / 代价与工厂数量共同决定；多工厂加速有引擎上限。
