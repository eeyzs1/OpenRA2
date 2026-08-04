# 步兵（兵种）

精确 Cost/HP/Speed/Armor 见 [stats-tables.md](./stats-tables.md)。下文侧重机制与特性；与表冲突时以 stats-tables / rulesmd 为准。

图例：`*` 国家特有；`†` 战役/渗透特有；`YR` 扩展新增或归属变更。

---

## 共享步兵

| 单位 | Cost | HP | Speed | Sight | 特性 |
|------|------|----|-------|-------|------|
| Attack Dog | 200 | 100 | 8 | 9 | 秒杀步兵；识破间谍；对车/建筑无效；免疫心控 |
| Engineer | 500 | 75 | 4 | 4–5 | 占领/修桥/修建筑/拆弹；无武器 |

盟军犬为德国牧羊犬外观，苏军为哈士奇；机制相同。

---

## 盟军步兵

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| GI | 200 | 125 | 4 | 5 | Barracks | 可部署沙袋：射程/火力↑、不可移动；可驻军民用建筑 |
| Guardian GI (YR) | 400 | 100 | 3 | 6 | — | 部署后对装甲/飞机强；移动慢 |
| Rocketeer | 600 | 125 | 9 | 8 | Airforce HQ | 飞行步兵；对空对地 |
| Sniper*（英国） | 600 | — | 慢 | — | Airforce HQ | 远距一击必杀步兵 |
| Spy | 1000 | 100 | 4 | 9 | Battle Lab | 伪装；渗透建筑获不同效果；犬可识破 |
| Navy SEAL | 1000 | 125 | 5 | 8 | Airforce HQ | 两栖；机枪秒步兵；C4 炸建筑/舰/桥屋。RA2 多战役，YR 进遭遇战 |
| Tanya | **1500** | 200 | 6 | 8 | Battle Lab | 英雄；两栖；秒步兵；C4 建筑/舰；唯一限制 |
| Chrono Legionnaire | 1500 | 125 | Chrono | 8 | Battle Lab | 传送（距越远相位越久）；武器「抹除」目标，期间目标无敌 |

### GI 武器量级（约 1.004）

| 状态 | Damage | Range | ROF |
|------|--------|-------|-----|
| 移动 | 15 | 4 | 20 |
| 部署 | 15 | 5 | 15 |
| 精英部署 | 25 | 7 | 5 |

### 间谍渗透摘要

| 进入 | 效果 |
|------|------|
| 电厂 | 断电 |
| 雷达/空指 | 干扰雷达/视野 |
| 精炼厂 | 偷钱 |
| 兵营/工厂 | 己方对应单位出老兵 |
| 敌方 Battle Lab | 解锁偷科技步兵 |

### 渗透产物

| 单位 | Cost | 获取 | 特性 |
|------|------|------|------|
| Chrono Commando† | ~1800–2000 | Spy→盟军实验室 | 海豹火力+C4 + 超时空背包；不能游泳 |
| Psi Commando† | 1000 | Spy→苏军/尤里实验室 | 心控 + C4；不能游泳 |

---

## 苏军步兵

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Conscript | 100 | 125 | 4 | 5 | Barracks | 最便宜基础兵；可驻军；不可部署沙袋 |
| Flak Trooper | 300 | 100 | 4 | 5 | Radar | 对空强；对地溅射；压制步兵 |
| Tesla Trooper | 500 | 130 | 4 | 6 | Barracks | 电弧克甲；**不可被坦克碾压**；可给特斯拉线圈充能 |
| Crazy Ivan | 600 | 125 | 4 | 6 | Radar | 给单位/建筑/动物安炸弹（含友军）；可炸桥屋；不炸运输内部逻辑特殊 |
| Terrorist*（古巴） | **200** | 75 | 6 | 9 | Radar | 自杀爆炸溅射 |
| Desolator*（伊拉克） | 600 | 150 | 4 | 6 | Radar | 辐射炮秒步兵；部署污染地面，阻挡步兵/轻甲 |
| Yuri / Psi-Corps | 1200 | — | — | — | Battle Lab | RA2 苏军心控单位；可精神冲击杀周围步兵。YR 中尤里叛逃后苏军失去 |
| Boris (YR) | 1500 | 200 | 5 | 9 | Battle Lab | 英雄；机枪；可呼叫 MiG 空袭建筑；唯一限制 |
| Chrono Ivan† | ~1000–1750 | 100 | Chrono | 8 | Spy→盟军实验室 | Crazy Ivan + 超时空 |
| Yuri Prime† (RA2) | 2000 | — | 快 | — | Spy→苏军实验室 | 超远心控；英雄级 |

### Conscript 武器量级（约 1.004）

| | Damage | Range | ROF |
|--|--------|-------|-----|
| 普通 | 15 | 4 | 25 |
| 精英 | 20 | 5 | 25 |

### 特斯拉步兵与线圈

靠近 Tesla Coil 自动充能 → 线圈射程/伤害提升；≥3 人时线圈无视低电。

### Desolator

- 移动：辐射炮对步兵极强。
- 部署：地面持续辐射区。
- 防护服免疫己方辐射。

---

## 尤里步兵（YR）

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Initiate | 200 | 100 | 4 | 9 | Barracks | 心灵点火；驻军后对地面极强 |
| Brute | 500 | 200 | 6 | 8 | Barracks | 近战碎甲/碎人；犬不主动咬；接近前脆弱 |
| Virus | 700 | 100 | 4 | 9 | Psychic Radar | 狙击步兵；尸体毒云伤其他步兵；自身免疫毒 |
| Yuri Clone | 800 | 100 | 4 | 12 | Psychic Radar | 心控 1 单位；精神冲击杀周围步兵（敌我） |
| Yuri Prime | 1500 | 150 | 6 | 9 | Battle Lab | 英雄；可心控单位与多数建筑/防御；精神冲击不伤友军；悬浮；唯一限制 |
| Slave | 免费 | 125 | 3 | 5 | Slave Miner | 采矿；不可直接控制；矿场补员 |

### 心控免疫提醒（步兵相关）

犬、部分英雄、矿工体系、飞机等通常不可控；详见 [mechanics.md](./mechanics.md)。

---

## 英雄唯一性

| 英雄 | 阵营 | 典型能力 |
|------|------|----------|
| Tanya | 盟军 | C4、两栖、秒步兵 |
| Boris | 苏军 YR | 空袭建筑、机枪 |
| Yuri Prime | 尤里 / RA2 渗透 | 远距心控、可控建筑 |

同时通常只能拥有一名（克隆缸可绕过部分限制）。英雄常具自愈、抗碾压、抗心控等。

---

## 驻军与 IFV（步兵侧）

- GI / Conscript / Initiate 等可进民用建筑射击。
- Battle Bunker：主要给征兵。
- 进入 IFV 会改变 IFV 武器（见 [vehicles.md](./vehicles.md)）。

---

## 国家 / 版本速查

```text
盟军: GI, Dog, Engineer, Rocketeer, Spy, Chrono Legionnaire, Tanya
      + 英 Sniper | 美伞兵支援 | YR: Guardian GI, SEAL(遭遇战)
苏军: Conscript, Dog, Engineer, Tesla Trooper, Flak Trooper, Crazy Ivan
      + 俄 Tesla Tank(车) | 伊 Desolator | 古 Terrorist | 利 Demolition Truck(车)
      + YR: Boris；失去 Yuri/Sensor/Cloning
尤里: Initiate, Engineer, Brute, Virus, Yuri Clone, Yuri Prime, Slave
```
