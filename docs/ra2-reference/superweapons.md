# 超级武器与支援能力参数

冷却 `RechargeTime` 单位为**分钟**（rules 注释）。帧数按时基换算（社区常按约 15fps：900 帧 ≈ 1 分钟）。

## 1. 充能时间（Special）

| Special | 冷却(分) | 计时可见 | 需电 | 阵营 |
|---------|----------|----------|------|------|
| NukeSpecial | 10 | yes | yes | 苏 |
| IronCurtainSpecial | 5 | yes | yes | 苏 |
| LightningStormSpecial | 10 | yes | yes | 盟 |
| ChronoSphereSpecial | 7 | yes | yes | 盟 |
| PsychicDominatorSpecial | 10 | yes | yes | 尤里 |
| GeneticConverterSpecial | 5 | yes | yes | 尤里 |
| ForceShieldSpecial | 5 | no* | yes | **全阵营 YR** |
| ParaDropSpecial | 4 | no | no | 科技机场 |
| AmericanParaDropSpecial | 4 | no | no | 美国 |
| SpyPlaneSpecial | 4 | no | no | 苏 YR |
| PsychicRevealSpecial | 4 | no | no | 尤里 |

\*侧栏可用 `FlashSidebarTabFrames` 提示。超武建筑建成会通知全员并揭雾。

## 2. 持续时间与范围（General / Special）

| 效果 | 参数 | 值 | 说明 |
|------|------|-----|------|
| 铁幕 | IronCurtainDuration | **750 帧** | 无敌；杀有机单位 |
| 铁幕 Range | IronCurtainSpecial.Range | 1.4 | 选区半径相关 |
| Force Shield | ForceShieldDuration | **500 帧** | 约 25s@15fps |
| Force Shield 断电 | ForceShieldBlackoutDuration | **1000 帧** | 约 60s；大于护盾持续时间 |
| Force Shield 半径 | ForceShieldRadius | 4 格 | — |
| Chronoshift | Range 1.4 | — | 约 3×3 级选区 |
| Nuke | Range 7 | — | 大范围 |
| Lightning | Range 7 | — | 大范围 |
| Genetic Mutator | Range 5 | — | — |
| Dominator | Range 1.4 | — | + 建筑伤害 |

## 3. 行为摘要

| 能力 | 关键规则 |
|------|----------|
| Chronoshift | 有机单位致死；可摔车入水/舰上岸；铁幕单位通常不可传 |
| Iron Curtain | 载具/建筑无敌；盖步兵则杀；免疫心控；运输内乘员不享受 |
| Force Shield | **仅建筑**；可罩盟友；可挡超武；不杀步兵；发动后断电 |
| Nuke | 爆炸 + 辐射残留 |
| Weather Storm | 多段落雷 |
| Genetic Mutator | 步兵→己方 Brute；动物杀死 |
| Psychic Dominator | 永久心控；驻军/免疫除外；伤建筑 |
| Spy Plane / Psychic Reveal | 揭雾 |
| Paradrop | 运输机可被击落 |

## 4. 建造限制

Chronosphere、Weather、Iron Curtain、Nuke Silo、Genetic Mutator、Psychic Dominator、Ore Purifier、Cloning Vats 等通常 **每玩家限一**。
