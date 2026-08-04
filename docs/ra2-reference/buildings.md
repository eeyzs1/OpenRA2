# 建筑（生产 / 经济 / 支援）

防御炮塔与超级武器见 [defenses.md](./defenses.md)。数值：造价/电力/前置以 CNCNZ 手册为主；HP 以 GameFAQs YR Units Guide 为主。

## 科技树概览

```text
盟军: CY → Power → (Barracks | Refinery) → War Factory → Airforce HQ → Battle Lab
                 ↘ Naval Yard
苏军: CY → Tesla Reactor → (Barracks | Refinery) → War Factory → Radar → Battle Lab
                        ↘ Naval Yard              ↘ Nuclear Reactor / Cloning(RA2) / Industrial(YR)
尤里: CY → Bio Reactor → (Barracks | Slave Miner) → War Factory → Psychic Radar → Battle Lab
                      ↘ Sub Pen                    ↘ Grinder
```

---

## 盟军建筑

| 建筑 | Cost | Power | HP | 前置 | 作用 |
|------|------|-------|-----|------|------|
| Construction Yard | 3000 (MCV) | 0 | 1000 | — | 建造所有建筑；基地核心 |
| Power Plant | 800* | +200 | 750 | — | 供电 |
| Ore Refinery | 2000 | -50 | 1000 | Power Plant | 收矿换钱；附带 Chrono Miner |
| Barracks | 500 | -10 | 500 | Power Plant | 生产步兵；解锁多数防御 |
| War Factory | 2000 | -25 | 1000 | Refinery + Barracks | 生产地面载具；也可产夜鹰 |
| Naval Shipyard | 1000 | -25 | 1500 | Refinery | 水面放置；产舰并维修 |
| Airforce Command HQ | 1000 | -50 | 600 | Refinery | 雷达；4 个战机停机位（Harrier/Black Eagle） |
| Service Depot | 800 | -25 | 1200 | War Factory | 维修载具/飞机；清除恐怖机器人；可出售载具（RA2 疏漏，YR 1.001 调整） |
| Battle Lab | 2000 | -100 | 500 | War Factory + Airforce HQ | 解锁高科单位/超武 |
| Ore Purifier | 2500 | -200 | 900 | Refinery + Battle Lab | 采矿收入 +25%；每玩家限一 |
| Robot Control Center (YR) | 600 | -100 | 600 | （YR 高科相关） | 控制 Robot Tank；断电则机器人停摆 |

\*`rulesmd`：**$800**（见 [stats-tables.md](./stats-tables.md)）。

### 机制要点

- 空指部既是雷达也是战机母港；战机攻击后须返回空指部装弹，若无空指部会坠毁。
- 多座空指部增加停机位，但不叠美国 Airborne 冷却次数。

---

## 苏军建筑

| 建筑 | Cost | Power | HP | 前置 | 作用 |
|------|------|-------|-----|------|------|
| Construction Yard | 3000 (MCV) | 0 | 1000 | — | 建造核心 |
| Tesla Reactor | 600 | +150 | 750 | — | 供电 |
| Ore Refinery | 2000 | -50 | 1000 | Tesla Reactor | 收矿；附带 War Miner |
| Barracks | 500 | -10 | 500 | Tesla Reactor | 步兵 |
| War Factory | 2000 | -25 | 1000 | Refinery + Barracks | 地面载具；高阶可产飞艇等 |
| Naval Shipyard | 1000 | -20 | 1500 | Refinery | 海军 |
| Radar Tower | 1000 | -50 | 1000 | Refinery | 雷达；YR 解锁 Spy Plane |
| Service Depot | 800 | -20 | 1200 | War Factory | 维修；清恐怖机器人 |
| Battle Lab | 2000 | -100 | 500 | War Factory + Radar | 高科 / 超武 |
| Nuclear Reactor | 1000 | **+2000** | 1000 | Battle Lab | 大电量；被毁核爆+辐射；可替代特斯拉反应堆作前置 |
| Cloning Vats (RA2) | 2500 | -200 | 1000 | Battle Lab | 兵营每产一兵免费再克隆一个；可投步兵回收部分造价。YR 转尤里 |
| Industrial Plant (YR) | 2500 | -200 | 1000 | Battle Lab | 降低车辆造价 |
| Psychic Sensor (RA2) | 1000 | -50 | 750 | Battle Lab | 显示敌方攻击意图；YR 转尤里 Psychic Radar |

核电 Power 以 `rulesmd` **+2000** 为准（手册常写 +1000）。

---

## 尤里建筑（YR）

| 建筑 | Cost | Power | HP | 前置 | 作用 |
|------|------|-------|-----|------|------|
| Construction Yard | 3000 (MCV) | 0 | 1000 | — | 建造核心 |
| Bio Reactor | 600 | +150（+100/人，最多 5） | 700 | — | 供电；可塞步兵（含心控单位） |
| Slave Miner | **1500** | 0 | 2000 | Bio Reactor | 部署采矿；建造厂与战争工厂均可造 |
| Barracks | 500 | -10 | 500 | Bio Reactor | 步兵 |
| War Factory | 2000 | -25 | 1000 | Slave Miner + Barracks | 载具；可产 Floating Disc |
| Submarine Pen | 1000 | -25 | 1500 | Slave Miner | 海军（须水面） |
| Psychic Radar | 1000 | -50 | 750 | Slave Miner | 雷达 + 心灵传感 + Psychic Reveal |
| Grinder | **600** | -50 | 900 | War Factory | 回收步兵/载具返还部分造价；尤里产 MCV 前置 |
| Battle Lab | 2000 | -100 | 500 | War Factory + Psychic Radar | 高科 / 超武 |
| Cloning Vats | 2500 | -200 | — | Battle Lab | 克隆兵营产物（YR 尤里版无投兵回收，回收交给 Grinder） |

### 生化反应堆

- 5 槽；每人约 +100 电。
- 建筑被毁释放内部步兵。
- 心控单位进驻后，若心控来源消失，单位可能归还原主人。

### 奴隶矿场

- 移动时可自修；部署后需工程师进入维修。
- 奴隶被杀会免费补员。
- 矿场被毁时，存活奴隶归「解放者」或中立，变为弱近战单位。

---

## 中立 / 科技建筑（可被工程师占领）

| 建筑 | 效果 |
|------|------|
| Tech Airport | 伞兵支援（阵营不同伞兵组成不同） |
| Tech Hospital | RA2：步兵进入治疗；YR：全局自动治疗己方步兵 |
| Tech Outpost / 其他前哨类 | 扩张/视野/清无人机等（依具体类型） |
| Oil Derrick | 持续产钱 |
| Tech Civilian Power Plant (YR) | +200 电 |
| Tech Machine Shop (YR) | 全局自动修车 |
| Tech Secret Lab (YR) | 随机解锁一种本不能造的单位/建筑（开局预定） |

Secret Lab 可能解锁的示例：Demolition Truck、Desolator、Grand Cannon、Psi Commando、Sniper、Tank Destroyer、Terrorist、Tesla Tank 等。

---

## 建造厂与 MCV

- 遭遇战常以 MCV 开局；在合法空地部署为建造厂（不要求贴近已有基地）。
- 选项 **MCV Repacks** 开启时，建造厂可打包回 MCV。
- YR 对心灵控制下的 MCV/建造厂展开与打包有额外限制。

---

## 间谍进实验室解锁单位（提醒）

详见 [infantry.md](./infantry.md)「渗透产物」：Chrono Commando、Chrono Ivan、Psi Commando、Yuri Prime（版本与目标阵营相关）。
