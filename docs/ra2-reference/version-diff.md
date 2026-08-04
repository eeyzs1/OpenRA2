# RA2 vs Yuri's Revenge 差异对照

权威：RA2 **1.006** / YR **1.001**。本表只列对遭遇战影响大的变更。

## 阵营

| | RA2 | YR |
|--|-----|-----|
| 可玩 | 盟军、苏军 | + 尤里 |
| 国家 | 盟 5 + 苏 4 | 同左 + Yuri Country |

## 苏军失去 → 尤里获得

| 内容 | 说明 |
|------|------|
| Psychic Sensor | 变为尤里 Psychic Radar（兼雷达 + Psychic Reveal） |
| Cloning Vats | 尤里版；回收功能改由 Grinder |
| Yuri / Psi-Corps（多人） | 尤里线为 Yuri Clone / Yuri Prime |

## 盟军新增 (YR)

| 类型 | 名称 |
|------|------|
| 步兵 | Guardian GI；SEAL 进遭遇战 |
| 载具 | Robot Tank；Battle Fortress |
| 建筑 | Robot Control Center |
| 支援 | Force Shield（见下） |

## 苏军新增 (YR)

| 类型 | 名称 |
|------|------|
| 步兵 | Boris |
| 载具/空 | Siege Chopper |
| 建筑 | Battle Bunker；Industrial Plant |
| 支援 | Spy Plane；Force Shield |

## 尤里全新

Bio Reactor、Slave Miner、Grinder、Submarine Pen、Citadel Wall、Tank Bunker、Gattling Cannon、Psychic Tower、Genetic Mutator、Psychic Dominator；Initiate / Brute / Virus / Yuri Clone / Yuri Prime；Lasher / Gattling Tank / Chaos Drone / Magnetron / Mastermind；Floating Disc；Boomer 等。

## 全阵营支援 (YR)

**Force Shield**：建造并放置 Battle Lab 后开始充能。

- 范围内友方**建筑**无敌约 **25 秒**（可挡超武）
- 随后整基**断电约 60 秒**
- 不影响单位；不杀死步兵（与铁幕不同）
- 可罩盟友建筑
- 若建筑已有铁幕，Force Shield 会覆盖铁幕保护

## 科技建筑 (YR)

新增：民用电厂、机械车间、秘密实验室。  
医院改为全局步兵治疗。见 [tech-buildings.md](./tech-buildings.md)。

## 其它

| 项 | 变化 |
|----|------|
| 维修厂卖车 | RA2 盟军可卖车、苏军不可（疏漏）；YR 1.001 调整 |
| 伞兵组成 | 按阵营变为 GI / Conscript / Initiate 数量不同 |
| 心控与 MCV | YR 对心控下 MCV/建造厂展开打包有额外限制 |

## 补丁注意

- 落地数值以最终 MIX 内 `rules.ini` / `rulesmd.ini` 为准。
- YR 1.001 规则应从 `expandmd01.mix` 提取，勿误用 `localmd.mix` 的 1.000。
