# 战车 / 空军 / 海军

精确数值见 [stats-tables.md](./stats-tables.md)。下文侧重机制；冲突以 stats-tables / rulesmd 为准。

侧栏「单位」页通常含地面载具、飞行器与舰船。

---

## 地面载具

### 盟军

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Chrono Miner | 1400 | 1000 | 4 | 4 | Refinery | 采矿；满载可超时空回厂；载量低于苏矿；免疫心控 |
| Grizzly Tank | 700 | 300 | 7 | 8 | War Factory | 主战坦克；较快较脆；可碾压步兵 |
| IFV | 600 | 200 | 10 | 8 | War Factory | 1 步兵舱；武器随乘员变化；默认对空火箭 |
| Tank Destroyer*（德国） | 900–1000 | — | — | — | Airforce HQ | 极克坦克；对步兵/建筑极弱 |
| Robot Tank (YR) | 600 | 180 | 10 | 6 | Robot Control | 两栖；免疫心控；指挥中心断电则停摆 |
| Mirage Tank | 1000 | **200** | 7 | 9 | Battle Lab | 静止伪装成树；克步兵/轻甲 |
| Prism Tank | 1200 | 250 | 4 | 8 | Battle Lab | 棱镜折射打多目标；装甲薄 |
| Battle Fortress (YR) | 2000 | **600** | 4 | 6 | Battle Lab | 多步兵；全员可射击；可碾压坦克 |
| MCV | 3000 | 1000 | 4 | 6 | Service Depot | 部署→建造厂 |

#### IFV 乘员武器（摘要）

| 乘员 | IFV 武器 |
|------|----------|
| 空 / 犬 | 对空火箭 |
| GI / Conscript / Spy | 机枪 |
| Flak Trooper | 高射炮 |
| Engineer | 维修臂（修车、清无人机） |
| Sniper | 狙击 |
| SEAL / Tanya / Chrono Commando / Psi Commando | 重机枪 |
| Desolator | 辐射炮 |
| Crazy Ivan / Chrono Ivan / Terrorist | 自杀炸弹 |
| Tesla Trooper | 特斯拉 |
| Yuri / Clone | 精神冲击穹 |
| 部分中立 | 棱镜等 |

### 苏军

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| War Miner | 1400 | 1000 | 4 | 4 | Refinery | 采矿；有炮塔；载量高；免疫心控 |
| Rhino Tank | 900 | 400 | 6 | 8 | War Factory | 主战坦克；更厚更慢 |
| Flak Track | 500 | 180 | 8 | 8 | War Factory | 对空/对轻甲；运 5 步兵 |
| Terror Drone | 500 | 100 | 10 | 4 | War Factory | 钻入载具拆解；对步兵似犬；免疫心控；维修厂/工程师 IFV 可清除 |
| V3 Rocket Launcher | 800 | 150 | 4 | 7 | Radar | 远程火箭；脆弱；火箭可被防空击落 |
| Tesla Tank*（俄罗斯） | 1200 | — | — | — | Radar | 电弧；可打过墙；不能给线圈充能 |
| Demolition Truck*（利比亚） | 1500 | — | — | — | Radar | 自杀小核弹 + 辐射 |
| Apocalypse Tank | 1750 | 800 | 4 | 6 | Battle Lab | 双炮对地；对空导弹；重甲慢速 |
| MCV | 3000 | 1000 | 4 | 6 | Service Depot | 部署→建造厂 |

### 尤里（YR）

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Slave Miner | **1500** | 2000 | 3 | 4 | Bio Reactor | 部署采矿；放出最多 5 奴隶；移动自修 |
| Lasher Tank | 700 | 300 | 7 | 8 | War Factory | 主战坦克；三阵营中最脆 |
| Gattling Tank | 600 | 210 | 6 | 10 | War Factory | 对空/步兵；射击越久越快（三档） |
| Chaos Drone | **800** | 200 | 8 | 6 | War Factory | 释放狂暴毒气；友军互殴且伤害提升；免疫心控 |
| Magnetron | 1000 | 150 | 5 | 10 | Psychic Radar | 吸起车辆/舰船拖拽；对建筑磁光束；几乎不能反步兵 |
| Mastermind | 1750 | 500 | 4 | 9 | Battle Lab | 稳控最多 3；会强制超控致自毁 |
| MCV | 3000 | 1000 | 4 | 8 | Grinder | 部署→建造厂 |

### 主战坦克对比

| | Grizzly | Rhino | Lasher |
|--|---------|-------|--------|
| Cost | 700 | 900 | 700 |
| HP | 300 | 400 | 300 |
| Speed | 7 | 6 | 7 |
| 定位 | 快、便宜、脆 | 厚、贵、慢 | 快但最脆 |

### 矿车对比

| | Chrono Miner | War Miner | Slave Miner |
|--|--------------|-----------|-------------|
| 返厂 | 超时空 | 行驶 | 部署原地加工 |
| 武装 | 无 | 炮塔 | 未部署有枪 |
| 载量 | 较低 | 较高 | 奴隶多次往返 |

---

## 空军

### 盟军

| 单位 | Cost | HP | Spd | Sight | 特性 |
|------|------|----|-----|-------|------|
| Rocketeer | 600 | 125 | 9 | 8 | 飞行步兵（兵营产） |
| Nighthawk | 1000 | 175 | 12 | 7 | 运 5 步兵；雷达隐身；机枪 |
| Harrier | 1200 | 150 | 14 | 8 | 对地攻击机；须回空指部 |
| Black Eagle*（韩国） | 1200 | — | — | — | 更强 Harrier，完全替代 |
| Osprey | — | 30 | 12 | — | 驱逐舰反潜机，被击落后免费重生 |
| Hornet | — | 75 | 12 | — | 航母舰载机，损失免费补 |

### 苏军

| 单位 | Cost | HP | Spd | Sight | 特性 |
|------|------|----|-----|-------|------|
| Siege Chopper (YR) | **1400** | **300** | 12 | 7 | 空中机枪；可降落部署为攻城炮 |
| Kirov Airship | 2000 | 2000 | 5 | 8 | 极厚飞艇；垂直下方丢弹；极慢 |
| MiG | — | 100 | 16 | — | Boris 呼叫空袭 |
| Spy Plane (YR) | — | 400 | 15 | — | 雷达支援揭雾 |

### 尤里

| 单位 | Cost | HP | Spd | Sight | 特性 |
|------|------|----|-----|-------|------|
| Floating Disc | 1750 | 600 | 15 | 9 | 激光；停在电厂上可断电整基；停在精炼厂/矿场可抽钱；可使耗电防御失效 |

---

## 海军

### 共享

| 单位 | Cost | HP | Spd | Sight | 特性 |
|------|------|----|-----|-------|------|
| Amphibious Transport | 900 | 300 | 6 | 6 | 12 槽；步兵+车辆；两栖；无武装。苏/尤里版装甲优于盟军 |

### 盟军

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Destroyer | 1000 | 600 | 6 | 7 | Shipyard | 炮击水面/岸；Osprey 反潜；可探测潜水单位 |
| Aegis Cruiser | 1200 | 800 | 4 | 8 | Airforce HQ | 纯对空/反导 |
| Dolphin | 500 | 200 | 8 | 4 | Battle Lab | 潜水声波；克舰/乌贼；可揭潜 |
| Aircraft Carrier | 2000 | 800 | 4 | 7 | Battle Lab | 3 架 Hornet 循环出击；损失免费补 |

### 苏军

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Typhoon Attack Sub | 1000 | 600 | 4 | 4 | Shipyard | 潜水鱼雷；不对地 |
| Sea Scorpion | 600 | 400 | 8 | 8 | Radar | 对空/对地/反导；快速 |
| Giant Squid | 1000 | 200 | 8 | 5 | Battle Lab | 潜水缠抱舰船；海豚可驱离 |
| Dreadnought | 2000 | 800 | 4 | 7 | Battle Lab | 双远程导弹打岸；导弹可被防空击落 |

### 尤里

| 单位 | Cost | HP | Spd | Sight | 前置 | 特性 |
|------|------|----|-----|-------|------|------|
| Boomer | 2000 | 1200 | 5 | 8 | Psychic Radar | 潜水鱼雷 + 对地弹道导弹；乌贼难以缠抱 |

### 潜水单位可见性

潜艇、海豚、乌贼等在受伤或攻击时可能暴露；驱逐舰/海豚等可主动探测。

---

## 武器量级摘录（约 RA2 1.004）

便于手感对照，非最终 YR 校准值。

| 单位 | Damage | Range | ROF | 备注 |
|------|--------|-------|-----|------|
| Grizzly | 65 | 5 | 60 | 对步兵 Verses 低 |
| Rhino | 90 | 5.75 | 65 | 更重 |
| Prism Tank | 100 | 10 | 100 | 折射额外段 |
| Mirage | 100 | 7 | 70 | — |
| Apocalypse 炮 | 高 | — | — | 另有对空导弹 |
| V3 | 极高 | 远 | 慢 | 弹可被击落 |
| Harrier | 150 | 6 | 10 | 精英更高 |
| Black Eagle | 200 | 6 | 10 | — |

完整 Verses 表见 Stephanus Compendium 武器段。

---

## 克制关系（简表）

| 单位 | 擅长 | 惧怕 |
|------|------|------|
| 主战坦克 | 车辆、建筑 | 无人机、反坦、空军、心控 |
| IFV / Flak / Gattling | 飞机、步兵 | 重坦 |
| Terror Drone | 车辆 | 犬群、哨戒、维修 |
| Prism / V3 / Dreadnought | 基地、密集单位 | 突击、防空拦截导弹 |
| Kirov | 建筑 | 密集防空 |
| Magnetron | 拖车辆、拆建筑 | 步兵 |
| Floating Disc | 断电、抽钱 | 防空 |
| Mastermind / Psychic Tower | 偷家、偷部队 | 犬、无人机、飞机、超控自毁 |

---

## 版本差异速记

- **RA2**：无 Guardian GI / Robot Tank / Battle Fortress / Siege Chopper / Boris / 尤里阵营等。
- **YR**：苏军工业厂、战斗碉堡、攻城直升机、鲍里斯、侦察机；盟军机器人与战斗要塞；尤里全套。
- 国家特有车辆：德 Tank Destroyer、韩 Black Eagle、俄 Tesla Tank、利 Demolition Truck。
