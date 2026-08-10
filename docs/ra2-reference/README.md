# RA2 / Yuri's Revenge 参考资料

本目录汇总官方玩法与表现风格，供 OpenRA2 查阅。**不是**当前实现清单。权威顺序见 `docs/ra2-fidelity.md`。

## 玩法参考

| 文档 | 内容 |
|------|------|
| [mechanics.md](./mechanics.md) | 电力、生产、军衔、装甲、心控、超武概要 |
| [mechanics-errata.md](./mechanics-errata.md) | Force Shield、间谍矩阵、免疫、手册纠错 |
| [weapons-warheads.md](./weapons-warheads.md) | 武器 Damage/ROF/Range + 弹头 Verses |
| [superweapons.md](./superweapons.md) | 超武/支援冷却与持续帧数 |
| [skirmish-options.md](./skirmish-options.md) | 遭遇战选项与 General 常数 |
| [countries.md](./countries.md) | 国家特色单位 |
| [buildings.md](./buildings.md) | 生产/经济建筑 |
| [defenses.md](./defenses.md) | 防御与超武建筑 |
| [infantry.md](./infantry.md) | 步兵与英雄 |
| [vehicles.md](./vehicles.md) | 战车、空军、海军 |
| [transport-ifv.md](./transport-ifv.md) | IFV 矩阵与运输容量 |
| [tech-buildings.md](./tech-buildings.md) | 中立科技建筑 |
| [crates-garrison.md](./crates-garrison.md) | 空投箱、驻军、矿价 |
| [neutrals.md](./neutrals.md) | 动物、平民、战役限定 |
| [stats-tables.md](./stats-tables.md) | Cost/HP/Power 总表 |
| [hotkeys.md](./hotkeys.md) | 默认快捷键 |
| [menu-screens.md](./menu-screens.md) | 主菜单/选项/遭遇战壳层 UV 与素材对照 |
| [version-diff.md](./version-diff.md) | RA2 ↔ YR 差异 |

## 表现风格

| 文档 | 内容 |
|------|------|
| [style/README.md](./style/README.md) | 风格索引 |
| [style/command-shell.md](./style/command-shell.md) | **指挥壳**：菜单/HUD 气质、色票、控件族、反模式 |
| [style/art-visual.md](./style/art-visual.md) | 画风与 UI 概要 |
| [style/music.md](./style/music.md) | 音乐 |
| [style/sfx-voice.md](./style/sfx-voice.md) | 音效与语音 |
| [style/vfx.md](./style/vfx.md) | 视效 |
| [style/assets.md](./style/assets.md) | 素材格式管线 |

## 战役

| 文档 | 内容 |
|------|------|
| [campaign/README.md](./campaign/README.md) | 战役索引 |
| [campaign/mechanics.md](./campaign/mechanics.md) | 战役专用机制与触发器映射 |
| [campaign/ra2-allied.md](./campaign/ra2-allied.md) | RA2 盟军 12 关 |
| [campaign/ra2-soviet.md](./campaign/ra2-soviet.md) | RA2 苏军 12 关 |
| [campaign/yr-campaigns.md](./campaign/yr-campaigns.md) | YR 双线 7+7 关与合作说明 |

## 实现对照

| 文档 | 内容 |
|------|------|
| [fix-plan.md](./fix-plan.md) | 参考文档 vs 当前实现的缺口与分阶段修复计划 |
| [`../ra2-fidelity.md`](../ra2-fidelity.md) | 既有保真状态矩阵（随修复回写） |

## 数值口径

1. 合法安装最终 `rules.ini` / `rulesmd.ini`（YR：`expandmd01.mix`）
2. 本目录 `stats-tables` / `weapons-warheads` / `superweapons`（rulesmd 镜像摘录）
3. CNCNZ / 官方手册（机制描述）
4. 社区 FAQ（仅作补充）

## 有意不收入 / 仍属 rules 原文层

下列不适合再手抄进 Markdown（体量过大或版权/二进制相关），复刻时直接查 MIX：

- 全部 100+ Warhead 与全部 EliteWeapon 逐条
- `artmd.ini` 全动画/炮塔绑定
- `sound(md).ini` / `speech(md).ini` 全语音表
- 全部 Cameo/SHP/VXL 文件清单
- 战役关卡脚本与地图触发器
- 原版 OST/VQA 资源本身（风格见 `style/`，不收录音视频文件）

若下一轮要自动化：写脚本从本机 `rulesmd.ini` 生成 CSV，比继续扩 Markdown 更合适。
