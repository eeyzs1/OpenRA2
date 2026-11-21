# 原始队伍脚本的跳转与成功标志

原始第一关的脚本指令 6（跳转）和 49（成功标志）已接入模拟。BadGuy1 的原始动员兵脚本不再停在 49；三份原始炮击脚本也能在目标消失后返回第一条攻击指令。本批仅接入控制语义；后续本地 AI 的生产、招募和权重历史见 [原生 AI](native-ai.md)，完整原版 AI 仍未完成。

## 来源与语义

| 指令 | 原图使用 | 当前执行 |
|---|---|---|
| `6,1` | `095F32FC` 单艘无畏舰、`0B7C332C` / `0948E7CC` 两组 V3、`07CA645C` 平民脚本 | 从有效脚本行的第一行重新执行；目标行在下一 tick 执行，不能同帧死循环 |
| `49,0` | `0A64854C` 的 `0,2 → 49,0 → 0,1`；供简单、中等、困难三种动员兵编组使用 | 锁存队伍的成功标志，下一 tick 继续脚本；不停止成员、不重新攻击、不判定玩家胜利 |

原始五份相关脚本、地图 AI 触发器与升级前存档来源已保存在本地 `review_out/native-campaign/native-script-control-source-audit.json`。原图 SHA256 为 `ad64c9293a8bccb85c38f5871a974ed9c09b8b5da11bb346e990423bf625c79c`，图记录没有为适应测试而改写。

跳转使用从 1 开始的行号，按槽位 0..49 中实际存在的行排序计数。解析器原先遇到第一个空槽就停止，现会跳过空槽，忽略原始表范围之外的行。跳转到自身每 tick 只执行一次；0、-1 或超过末行的参数进入脚本末尾，队伍在下一 tick 解散，成员仍保留原有命令。当前输入解析仍有既有整数范围限制，不据此声称任意畸形脚本均与原版一致。

成功标志属于队伍的一生：后续跳转、成员死亡或队伍解散不会清除它；没有执行过 49 的队伍不会自动获得成功。49 的参数不使用，不能把编辑器旧名称“Repeat Until Success”解释成攻击重试循环。原生玩家胜负继续由独立的任务终局动作负责。

依据为 OpenTS 的 [指令 6](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-loop/) 和 [指令 49](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-success/) 实现说明；YRpp 的 [TeamClass](https://github.com/Phobos-developers/YRpp/blob/master/TeamClass.h) 另明确保留执行 49 后的 `AchievedGreatSuccess` 字段，[ScriptTypeClass](https://github.com/Phobos-developers/YRpp/blob/master/ScriptTypeClass.h) 保留 50 项动作表。这些支持控制语义和字段存在，不能替代 RA2 1.006 / YR 1.001 实机的全部同帧、异常参数及 AI 学习时序校准。

## 模拟与保存

`world_native_script_control.cpp` 负责跳转及成功锁存。`setNativeTeamScriptStep` 统一重置脚本目标、固定目标标志、等待与待推进状态，不改变成员实际订单，也不消耗 RNG。正常顺序推进复用这一入口；成功与跳转的末尾处理则保留下一 tick 退队的时序。

`NativeTeamState::succeeded` 参与模拟 checksum，写入 **World v42（RA2WLD42）**。读档还支持 v41 及既有格式，旧队伍默认未成功；已有活动队伍、导弹、来源身份和计时继续恢复。保存校验拒绝未组建队伍的成功标志，以及来源脚本没有 49 却被标记成功的状态。

本批应用封套为 **v10**，来源为 **SourceScene v10 / NativeEvents v2**，通过 **LAN v20** 隔离当时行为。普通 v10/World41 兼容夹具和升级前实际保存的活跃原生 World41/Scene10 存档分别覆盖布局迁移及原生队伍续跑。后续 [玩家生产批](native-production.md) 升为 World v43 / SourceScene v11 / LAN v21，应用和 NativeEvents 格式保留。

## 验证

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_script_control|native_team_attack|state|app_state)$' -V
```

`ra2.native_script_control` 覆盖稀疏槽位、未使用参数、成功锁存、前向/后向/自身/末尾跳转、最后一行成功、成员命令保留、无隐含成功、非法标志拒绝、应用保存以及跳转前后的逐 tick 续档。

实际攻击反例场景使用原始单辆 V3 编制与完全相同的 `1,96 → 6,1` 脚本，仅把路径点 96 移至明确标记的平坦测试场并放置测试目标。第一枚实际导弹命中后移除旧目标，经过原始跳转，再在该路径点生成替代目标；脚本重新捕获并实际命中，随后比较续跑。没有直接修改游标代替正向跳转，也没有直接扣血代替导弹命中。该场景不是原关自然炮击流程或整关通关证明。

`ra2.native_team_attack` 另保留完整原始场景的断桥负例：建筑类别为空后按原脚本执行成功标志并进入任意目标攻击，不虚构跨河建筑命中。恢复桥面的受控正例继续验证两个预置原兵的实际追击与命中；具体边界见 [队伍类别攻击](native-team-attack.md)。

最终完整 Release、相关 ASan、冻结输入哈希与实际旧存档的结果见本地 `review_out/native-campaign/native-script-control-validation.json`。日志位于 `build/native-script-control-*` 与 `build-asan/native-script-control-*`，首轮四项检查 **4/4 通过**，15.51 秒。

本批最终完整 Release **109 项中 108 通过、空战役矩阵 1 项按约定跳过、0 失败**，962.01 秒。包含自然开局、完整对局、耐久与续档、原始机制和资源、实绘矩阵、双进程联机及启动脚本检查。冻结的代码、二进制和场景哈希在测试后保持一致。空战役矩阵的跳过不是战役完成证据。

最终版本相关 **ASan Release 32/32 通过**，575.73 秒，未报告 AddressSanitizer 错误。包含控制指令、原始队伍、实际导弹、桥梁与修桥、胜负、普通战斗与生命周期、存档迁移及网络协议；配置为 `RA2_ASAN=ON`、MSVC `/fsanitize=address`，属于相关选择集。测试前后的 Release / ASan 二进制及冻结来源哈希一致。

精简证据为 `build/native-script-control-full-audit.txt`、`build/native-script-control-focused-audit.txt` 和 `build-asan/native-script-control-final-audit.txt`。验证 JSON 保留来源审计、旧 World41 存档哈希和完整测试结果。

## 仍需完成

原图实际包含六条本地 AI 生产触发器，启用表还引用了全局 AI 条目，但本图设置 `IgnoreGlobalAITriggers=yes`。脚本控制批尚未执行这些 AITriggerTypes；后续 SourceScene12 / World44 已接入本地条件、生产、加权选择和队伍结束时的结果记录，见 [原生 AI](native-ai.md)。本文的历史脚本测试成绩仍不构成自动出兵或完整 AI 的证据。

平民脚本前面的 Panic(19) 仍明确阻塞；炮击路径点为空时的强制地面射击、其他原始指令及完整战斗时序也仍需实现。原生生产身份、任务媒体、重开与战役进度、完整战役和人工整关验收继续保留为未完成项。
