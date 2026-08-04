# 核心机制与数值体系

覆盖 RA2 / YR 遭遇战中与单位、建筑共用的规则。细节以最终 `rules(md).ini` 为准。

## 1. 地图与时间单位

| 概念 | 说明 |
|------|------|
| Cell（格） | 基础地块；约可容纳 1 辆轻型车或最多 3 名步兵叠放 |
| ROF | 武器冷却帧；数值越大射速越慢。社区常用近似：中速下 ROF 15 ≈ 1 秒 |
| Frame | 引擎帧。部分效果用帧计时（如铁幕约 750 帧） |
| 建造半径 | 多数建筑须距己方建筑 ≤2 格；船坞约 12；墙约 8；部分防御约 4 |

## 2. 电力

- 多数建筑耗电；建造厂、墙、部分步兵炮塔（碉堡/哨戒炮）可不耗电。
- **低电**：雷达/部分防御停火、生产减速、超武停充等（具体项以引擎规则为准）。
- 阵营电厂：
  - 盟军：Power Plant（约 +200）
  - 苏军：Tesla Reactor（+150）；后期 Nuclear Reactor（手册约 +1000，可替代特斯拉反应堆作科技前置）
  - 尤里：Bio Reactor（基础 +150；最多进驻 5 名步兵，每人约 +100，满员约 +650）

核电站被毁会产生核爆与辐射，伤害步兵与轻甲。

## 3. 生产与经济

### 3.1 建造与生产

- 建筑由建造厂排队；单位由兵营 / 战争工厂 / 船厂 / 空指部分别生产。
- 生产按时间持续扣款；缺钱则暂停；取消通常返还尚未支出部分。
- 同类生产建筑可加速生产（引擎有上限倍率）。
- 建筑只能放在己方建造半径内；船厂须完全置于水面。
- 出售建筑通常返还造价约 **50%**。

### 3.2 采矿

| 阵营 | 矿车 | 特点 |
|------|------|------|
| 盟军 | Chrono Miner | 满载后可超时空返回精炼厂；载量低于苏军矿车 |
| 苏军 | War Miner | 有炮塔自卫；载量更高；常规行驶返厂 |
| 尤里 | Slave Miner | 可部署为矿场建筑；放出最多 5 名奴隶采矿；可从建造厂与战争工厂同时建造 |

- 精炼厂建成时通常附带一辆免费矿车（尤里为奴隶矿场体系）。
- 宝石矿价值高于普通矿。
- 盟军 **Ore Purifier**（矿石精炼器）：每车矿石收入约 **+25%**；每玩家限一。
- 苏军 **Industrial Plant**（工业工厂，YR）：降低车辆生产成本。

### 3.3 工程师

- 占领敌方/中立可占领建筑（消耗自身）。
- 完全修复己方建筑（消耗自身）。
- 修复断桥（进入桥屋）。
- 拆除 Crazy Ivan 类炸弹（不消耗自身）。
- 占领科技建筑获得对应能力。

## 4. 装甲、弹头与伤害

原版不是简单「步兵/车辆/建筑」三分类，而是：

- 单位有 **Armor** 类型（如 None / Flak / Plate / Light / Medium / Heavy / Wood / Steel / Concrete 等）
- 武器有 **Warhead**，用 `Verses` 对各装甲百分比修正伤害

社区 FAQ 中常见示例（GI 机枪，约 1.004）：

| 目标装甲 | Verses 约 |
|----------|-----------|
| 步兵 None | 100% |
| Flak | 80% |
| Plate | 70% |
| 车辆 Light | 50% |
| Medium / Heavy | 25% |
| 建筑 Wood | 75% |
| Steel | 50% |
| Concrete | 25% |

坦克炮、特斯拉、棱镜、辐射、心灵等各有独立弹头矩阵。校准 OpenRA2 时应以最终 rules 的 Warhead 段为准。

## 5. 军衔（Veteran / Elite）

- 经验按摧毁对象的 **造价点数** 累计。
- 常见阈值：累计超过自身造价约 **3×** → 老兵；再累计至约 **6×** → 精英（须严格大于阈值，恰好等于不升）。
- 老兵常见加成（社区归纳，最终以 `VeteranAbilities` 为准）：
  - 承伤降低（约 /1.5）
  - 速度约 ×1.2
  - ROF 间隔约 ×0.6（射更快）
  - 火力约 ×1.1
- 精英常自带自愈，且部分单位切换 **EliteWeapon**。
- 工厂被间谍渗透后，新产单位可直接以老兵出厂。

## 6. 驻军与运输

- 可驻军民用建筑：内部步兵对外射击，火力提升且受建筑保护。
- **Battle Bunker**（YR 苏军）：可建造的驻军工事，仅征兵等可进驻。
- **Tank Bunker**（尤里）：可进驻有炮塔非炮兵车辆，提升射速并固定位置。
- 运输：两栖运输（步兵+车辆）、夜鹰（仅步兵、雷达隐身）、防空履带车（步兵）、IFV（1 名步兵改武器）、战斗要塞（多人且可碾压车辆）等。
- **IFV** 武器随乘员变化（空载火箭对空；GI 机枪；工程师维修臂；海豹/谭雅重机枪；疯狂伊万自杀弹等）。

## 7. 心灵控制

可心控单位：Yuri / Yuri Clone、Psi-Corps、Psi Commando、Yuri Prime、Psychic Tower、Mastermind、Psychic Dominator 等。

常见免疫或特殊：

- 矿车、攻击犬、恐怖机器人、飞机（多数）、其他心控单位、部分英雄
- 铁幕单位不可心控
- 运输被控时，舱内单位通常仍属原主人
- 心控中的单位与施术者通常不能再进运输工具
- **Psychic Dominator**：范围内可心控单位被**永久**控制；通常免疫单位与驻军免疫；附带对附近建筑伤害

Mastermind：可稳控最多约 3 个单位，但会强制继续控更多；超限会自毁并释放。

## 8. 超时空、铁幕与力场盾

YR **Force Shield**（全阵营，需 Battle Lab）：建筑无敌约 25s，随后整基断电约 60s；不伤步兵。详见 [mechanics-errata.md](./mechanics-errata.md) 与 [version-diff.md](./version-diff.md)。

### Chronoshift（超时空传送）

- 约 3×3 区域单位瞬间移动到目标点
- 多数步兵/有机单位被传送会**直接死亡**（Chrono Legionnaire / Chrono Ivan 等例外）
- 可用运输载步兵规避
- 可将敌车丢进水、敌舰丢上岸等瞬间摧毁
- 铁幕单位通常不能被 Chronoshift

### Iron Curtain（铁幕）

- 约 3×3 区域载具/建筑短暂无敌（社区常记约 750 帧）
- 覆盖步兵/有机单位会**杀死**它们
- 铁幕单位免疫心控、不可被 Chronoshift；运输内乘员不享受铁幕
- 尤里 **Force Shield**：对建筑无敌，但发动后短时整基断电

## 9. 间谍渗透效果（概要）

盟军 Spy 伪装成敌方步兵进入建筑：

| 目标建筑 | 典型效果 |
|----------|----------|
| 电厂类 | 短暂断电 |
| 雷达 / 空指 | 重置雷达/迷雾相关 |
| 精炼厂 | 窃取资金 |
| 兵营 / 工厂 | 己方对应生产线出老兵 |
| 战斗实验室 | 解锁偷科技单位（Chrono Commando / Chrono Ivan / Psi Commando / Yuri Prime 等，取决于目标阵营） |

攻击犬可识破间谍伪装。

## 10. 裂缝、卫星与传感器

- **Gap Generator**：在半径内维持迷雾，低电失效
- **Spy Satellite**：清除己方迷雾（不穿透敌方裂缝）
- **Psychic Sensor / Psychic Radar**：显示范围内敌方攻击命令目标；可揭露间谍；尤里版兼作雷达并充能 Psychic Reveal

## 11. 碾压与特殊死亡

- 多数坦克可碾压步兵；特斯拉步兵等不可被碾压
- 恐怖机器人进入载具内部拆解；维修厂 / 工程师 IFV / 部分前哨可清除
- 辐射（核打击、爆破卡车、生化兵部署）持续伤害步兵与轻甲
- Chrono Legionnaire「抹除」：目标在被抹除期间无敌，中断则恢复

## 12. 国家特色（摘要）

### 盟军（RA2）

| 国家 | 特色 |
|------|------|
| 美国 | Airborne 伞兵（空指部） |
| 英国 | Sniper |
| 法国 | Grand Cannon |
| 德国 | Tank Destroyer |
| 韩国 | Black Eagle（替代 Harrier） |

### 苏军（RA2）

| 国家 | 特色 |
|------|------|
| 俄罗斯 | Tesla Tank |
| 伊拉克 | Desolator |
| 古巴 | Terrorist |
| 利比亚 | Demolition Truck |

### YR 变更摘要

- 尤里独立阵营；苏军失去心灵传感器与克隆缸（转尤里）
- 苏军新增 Battle Bunker、Industrial Plant、Boris、Siege Chopper、Spy Plane 等
- 盟军新增 Guardian GI、Robot Tank、Battle Fortress、Robot Control Center；海豹进遭遇战等
- 科技建筑：医院改为全局治疗步兵；新增 Machine Shop、Secret Lab、民用电厂等

## 13. 胜负（遭遇战）

- 标准：消灭对手全部单位与基地能力
- Short Game：失去全部建筑且无 MCV/展开能力时可提前判负

---

相关文档：[buildings.md](./buildings.md) · [defenses.md](./defenses.md) · [infantry.md](./infantry.md) · [vehicles.md](./vehicles.md) · [weapons-warheads.md](./weapons-warheads.md) · [superweapons.md](./superweapons.md) · [skirmish-options.md](./skirmish-options.md)
