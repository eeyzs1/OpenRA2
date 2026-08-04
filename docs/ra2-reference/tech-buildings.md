# 中立科技建筑

工程师进入即可占领。占领后视为己方基地延伸，可在其附近建造。旗帜为黄底扳手。数据优先取自 YR `rulesmd.ini` 镜像与 CNCNZ / GameFAQs。

## 一览

| ID | 名称 | HP | Armor | 版本 | 效果 |
|----|------|-----|-------|------|------|
| CAOILD | Tech Oil Derrick | 1000 | steel | RA2+ | 占领即时约 +1000$；之后约每 2 秒 +20$ |
| CAOUTP | Tech Outpost | 2000 | concrete | RA2+ | IFV 式火箭对空/对地；可当维修点清恐怖机器人 |
| CAAIRP | Tech Airport | 800 | concrete | RA2+ | 伞兵支援（冷却约 4:00） |
| CAHOSP | Tech Hospital | 800 | concrete | RA2 / YR 改 | RA2：步兵进入治疗；YR：全局自动治疗己方步兵 |
| CAPOWR | Tech Civilian Power Plant | 800 | concrete | **YR** | +200 电力；不易被飞碟抽电类逻辑针对 |
| CAMACH | Tech Machine Shop | 800 | concrete | **YR** | 全局自动修载具（含舰/部分飞行器）；约每 3 秒修 5% HP |
| CASCAD / Secret Lab | Tech Secret Lab | 1000 | — | **YR** | 解锁一种本不能造的单位/建筑（开局预定） |

## 油田 (Oil Derrick)

- 稳定被动收入；多油田可叠。
- 被摧毁时可能产生金钱箱；爆炸有独立伤害参数（社区 FAQ 有 Oil Explosion 表）。
- 不可建造，仅地图预置。

## 前哨 (Outpost)

- 武器近似空载 IFV 火箭（`HoverMissile`）。
- 兼具维修厂部分功能：修车、清除载具内恐怖机器人。
- 高 HP，适合作为前线锚点。

## 机场 (Airport)

伞兵组成（YR）：

| 阵营 | 伞兵 |
|------|------|
| 盟军 | 6 GI |
| 苏军 | 9 Conscript |
| 尤里 | 6 Initiate |

美国 Airborne（空指部）可与机场伞兵并存，冷却各自独立。运输机被击落则伞兵损失。

## 医院 (Hospital)

| 版本 | 行为 |
|------|------|
| RA2 | 步兵走入治疗 |
| YR | 占领后全图己方步兵持续回血（约每 3 秒 5% HP） |

## 民用电厂 / 机械车间 / 秘密实验室（YR）

- **民用电厂**：+200；补充基地电力。
- **机械车间**：全图载具自修，不必开回维修厂。
- **秘密实验室**：开局即绑定奖励，常见池：

```text
Demolition Truck | Desolator | Grand Cannon | Psi Commando
Sniper | Tank Destroyer | Terrorist | Tesla Tank
```

## 占领与心控

- 需工程师（消耗）。
- YR：**Yuri Prime 不能占领科技建筑**（与工程师路径不同）。
- 科技建筑可被摧毁、再被对方工程师抢（若仍存在）。

## 与 OpenRA2

预置油田/医院等见 `docs/ra2-ui-fidelity.md`（`CAOILD` / `CAHOSP`、ActiveAnim、禁止错误 remap）。
