# 原始常规武器与武器槽

盟军第一关的原始直接武器现进入实际模拟，包括 GI、征召兵、Tanya、军犬、灰熊、机枪碉堡、哨戒炮和爱国者。主、副、精英主、精英副武器独立保存，射击走公共命令、冷却、伤害、弹丸及连发路径。完整第一关仍未开放选关。

SourceScene v8 首批为 70 个军队/建筑类型保留 68 个非空武器槽。43 个类型使用普通直接武器模式（其中 8 个有主武器，其余按原规则无武器）；25 个驻军建筑只保留占领武器资料，不因存在 Primary 而自动开火；DRED/V3 两个发射器单独标记，不把虚拟发射器 Damage 当作普通弹丸伤害。DRED 继续已有 DMISL 系统；后续 SourceScene v9 已接入 [原始 V3/V3ROCKET](native-v3.md)，追加无普通武器的 V3ROCKET 后现为 71 个类型/68 个槽，完整驻军武器仍待接入。

## 当前实际参数

下表为第一关局部覆盖后的参数，不改全局遭遇战表。项目模拟为 30 Hz；沿用已有原始导弹/脚本时钟，将原始 ROF 帧数乘 2。原版实际游戏速度与墙钟、首发动画和转向时序仍未逐帧对照。

| 武器/状态 | 基础伤害 | 项目冷却 tick | 射程 |
|---|---:|---:|---:|
| 征召兵 M1Carbine | 10 | 50 | 4 |
| GI M60 | 15 | 40 | 4 |
| GI 部署 Para | 15 | 30 | 5 |
| Tanya DoublePistols | 125 | 20 | 6 |
| Tanya Sapper | 2500 | 100 | 1.5 |

冷却和伤害不再由原近似模板覆盖。部署 GI 的专用更新分支也读取当前原始武器，普通遭遇战的分支保留原行为。精英 GI 分别使用 M60E 和 ParaE；灰熊使用独立 105mmE 双发武器。

保留原始 11 类 Verses、射弹名、报告名、AA/AG、范围伤害及弹速。基础伤害与实际扣血分别验证：例如 M1Carbine 基础伤害 10，对 Flak 护甲征召兵的原始倍率为 80%，实际扣血 8；Para 对该护甲为 100%，实际扣血 15。

当前原图 FIREPOWER、ROF、STRONGER 使用各自阶级的能力标志，采用 General 的 1.1 伤害倍率、0.6 冷却倍率和 1.5 护甲除数；伤害按阶段取整数，不重复施加通用模板的军衔加成。灰熊双发采用原始 Burst=2，未指定 BurstDelay0 时按参考引擎的 3～5 原帧转换为 6～10 项目 tick。实现参考 [Phobos 的火力倍率实现](https://raw.githubusercontent.com/Phobos-developers/Phobos/develop/src/Ext/Techno/Body.cpp) 与 [连发延迟文档](https://phobos.readthedocs.io/en/v0.4/New-or-Enhanced-Logics.html#burst-delay-customizations)。这些参考不替代原版 1.006 实机验收。

## C4 与实际原场景

原始 Sapper 使用 Invisible5，弹道标记是 AG=no；C4 是专门的破坏能力，不能用该标记拒绝合法建筑/舰船。执行时保留原始数据，通过能力判定处理地面目标，采用 Sapper 的伤害、射程和 ROF。大建筑按靠近攻击者的脚印边缘接近，不要求 Tanya 走进 4×4 建筑中心。原生场景不再使用原公共模板的延迟 45 tick、6000 伤害炸弹；旧 SourceScene 与普通单位仍保留各自原行为。

在未改原脚本、编制或演员位置的专项中，原始 BadGuy1 两兵队现在从现有单位招募，执行 Attack(2)，实际移动后在项目 tick 128 首次命中建筑，记录到单次 5 点伤害。以同 tick 的队员开火和目标受击事件关联验证，未用全房屋累计伤害冒充该队命中。此前 World v39 的两兵途中阵亡是当时近似武器参数下的历史结果，不再作为当前验收条件。该队伍仍由专项直接创建，不代表原关卡自然触发或通关。

## 武器引用与存档

常规武器批写入 World v40（`RA2WLD40`），应用封套仍为 v10，该批 LAN 模拟协议为 v16；当前 V3 批为 v17，World 仍为 v40。常规弹丸记录发射时的武器槽，伤害、冷却、范围等发射时数值保持不变；保存的原场景源文重建不可变 Verses 和字符串，不保存裸指针。

每类型的六个引用位置区分旧导弹普通/精英资料和四个常规武器槽。即使场景没有导弹发射器定义，常规弹丸引用也能独立保存。旧 World v39 及此前格式继续读取；实际升级前保留的原生 v39 飞行存档另行续跑验证。普通应用夹具增加 v10/World v39。旧 SourceScene 继续使用其保存的数据，没有静默替换成当前武器。

## 验证与边界

```powershell
cmake -S . -B build
cmake --build build --config Release
python tools/ra2pack/gen_native_scene.py all01t.map
ctest --test-dir build -C Release -R '^ra2\.(native_weapons|native_weapons_assets|native_team_attack)$' -V
```

独立源资料审计直接读取 MIX 中的核心规则与原地图覆盖，核对全部 70 个类型/68 个槽。模拟专项检查九组实际逐发扣血和射击间隔、冷却中续档、真实部署命令、军衔/护甲、C4 接近大建筑及水面舰船、精英灰熊连发中续档、无发射器场景的 SAM 飞行引用和非法资料/槽位。

高 HP、相位冻结目标和清空其他演员只用于明确标记的机制场景，不是原关卡改图。上述两兵队另保留完整原场景，不修改兵数、房屋、脚本或防御。

本批完整 Release **104 项中 103 通过、空战役矩阵 1 项按约定跳过、0 失败**，822.50 秒；包括原始四舰长攻击链、旧应用/World 格式、普通对局与双进程联机。完整日志为 `build/native-weapons-full-tests.log`，精简记录为 `build/native-weapons-full-audit.txt`。

最终相关 ASan **26/26 通过**，715.13 秒，日志未报告 AddressSanitizer 错误。覆盖原始常规武器、类别攻击、队伍命令、事件/原场景、桥梁/修桥/闸门、导弹、普通战斗、生命周期、存档和网络协议；四舰长攻击链由上述 Release 覆盖。日志为 `build-asan/native-weapons-final-tests.log`，精简记录为 `build-asan/native-weapons-final-audit.txt`。当前构建与关键输入哈希记录于本地 `review_out/native-campaign/native-weapons-validation.json`。这些成绩不代表完整 ASan、完整战役或原版实机一致性。

仍需完成：完整 V3/其他专属武器与驻军结算、军犬动作、军衔经验/全部能力、移动速度、弹道/高度、原始射击/安放动画与音效、原版游戏速度，以及任务变量、媒体、目标和胜负。没有据此宣布整关或完整复刻完成。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA；当前写入 World v41，App v10 / SourceScene v9 保持，LAN 为 v18。完整范围、验证及剩余工作见 [原始目标与胜负](native-mission.md)，本文的既有测试成绩保留其历史范围。


后续 SourceScene32 已加入独立来源目录与棱镜分裂，当前格式、原始限制及未完成动作见 [来源武器目录](native-weapon-catalog.md)。以上验证数字保留其历史批次范围。
