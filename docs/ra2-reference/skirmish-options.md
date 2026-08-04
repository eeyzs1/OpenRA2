# 遭遇战选项与全局常数

## 1. 多人默认（`[MultiplayerDialogSettings]`）

| 选项 | 默认 | 说明 |
|------|------|------|
| Money | 10000（范围约 5k–10k） | 起始资金 |
| UnitCount | 0–10（默认 10） | 开局单位数相关 |
| TechLevel | 10 | 科技上限 |
| GameSpeed | 1 | 游戏速度档 |
| BridgeDestruction | yes | 桥可被毁 |
| Shroud | yes | 战争迷雾 |
| Bases | yes | 基地模式 |
| TiberiumGrows | yes | 矿再生/变密 |
| Crates | yes | 空投箱 |
| ShortGame | **yes** | 无建筑且无 MCV 即负（即使有野外部队） |
| FogOfWar | no | 额外战争迷雾变体（默认关） |
| MCVRedeploys | **yes** | 建造厂可打包为 MCV |
| AlliesAllowed | no | 默认是否允许结盟 |
| MultiEngineer | no | 多工程师占领规则变体 |
| CaptureTheFlag | no | 夺旗 |
| HarvesterTruce | no | 矿车停战 |
| ShadowGrow | no | 迷雾回覆生长 |

OpenRA2 应对齐的显式选项至少包括：**Short Game、MCV Repacks、Crates、桥毁、起始资金**。

## 2. 生产与经济常数（`[General]`）

| 参数 | 值 | 含义 |
|------|-----|------|
| BuildSpeed | 0.7 | 造价 1000 物品的基准生产时间（分钟量级） |
| BuildupTime | 0.06 | 建筑展开动画平均时长（分钟） |
| RefundPercent | 50% | 出售返还 |
| RepairPercent | 15% | 修满费用相对造价比例 |
| RepairStep | 8 | 建筑每 tick 回血 |
| RepairRate | 0.016 | 建筑维修间隔（分钟） |
| IRepairStep | 20 | 步兵医院类每 tick |
| SelfHealInfantryAmount/Frames | 20 / 50 | 精英等自愈 |
| SelfHealUnitAmount/Frames | 5 / 75 | 载具自愈 / 机械车间节奏相关 |
| GrowthRate | 5 | 矿生长间隔（分钟） |
| MinLowPowerProductionSpeed | 0.5 | 低电生产下限 |
| MaxLowPowerProductionSpeed | 0.8 | 低电生产上限 |
| VeteranRatio | 3.0 | 军衔阈值 |

多数单位**没有**独立 `BuildTime` 字段，建造时间由 **Cost × BuildSpeed** 与工厂数量加速共同决定。

出售建筑时幸存者数量：盟军除数 500、苏军 250、尤里 750（`AlliedSurvivorDivisor` 等）。

## 3. 矿与矿车

| 项 | 值 |
|----|-----|
| Ore Value | 25 / bail |
| Gem Value | 50 / bail |
| Chrono Miner Storage | 20 |
| War Miner Storage | 40 |
| TiberiumNearScan / FarScan | 6 / 48（寻矿半径，见 UI 保真文档） |

## 4. 箱子再生

见 [crates-garrison.md](./crates-garrison.md)：`CrateRegen=3` 分钟量级。
