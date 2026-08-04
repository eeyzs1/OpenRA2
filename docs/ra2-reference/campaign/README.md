# RA2 / YR 战役参考索引

本目录定义 **官方战役** 的结构、关卡目标与战役专用机制，供 OpenRA2 对照。不是通关攻略，也不等于本仓库 `assets/campaigns/` 已实现关卡。

## 文档

| 文档 | 内容 |
|------|------|
| [mechanics.md](./mechanics.md) | 战役通用机制（简报、触发、英雄、科技解锁、心灵建筑等） |
| [ra2-allied.md](./ra2-allied.md) | RA2 盟军 12 关 |
| [ra2-soviet.md](./ra2-soviet.md) | RA2 苏军 12 关 |
| [yr-campaigns.md](./yr-campaigns.md) | YR 盟军/苏军各 7 关 + 合作关说明 |

## 规模一览

| 作品 | 单人战役 | 关数 | 可玩反派战役 |
|------|----------|------|--------------|
| Red Alert 2 | 盟军 / 苏军 | 各 12 | 无（双方都可玩） |
| Yuri's Revenge | 盟军 / 苏军 | 各 7 | **尤里无单人战役**（仅遭遇战/合作） |

## 与 OpenRA2

- 引擎已有触发器框架（`TrigCond` / `TrigAct`）与 `assets/campaigns/`，见 `src/game/campaign.h`。
- 当前任务表为**自制/融合**关卡，**不是**原版 `*.map` 1:1 移植。
- 官方战役保真应以：原版地图 + 触发器 + FMV/简报为验收；本目录只提供叙事与机制契约。

## 主要来源

- 官方手册与游戏内简报
- StrategyWiki / CNC Wiki 关卡分类
- GameFAQs / 社区 walkthrough（目标与地点交叉核对）
