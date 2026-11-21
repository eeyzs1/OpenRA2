# 原始第一关的目标条件与胜负动作

all01t 的原始全灭条件和玩家胜负动作已接入模拟，应用按作者的任务结果结算。本关仍未加入选关；此批证明目标条件、实际武器和结算路径，**不是从开局到结算的自然通关证明**。

## 作者链路

| 原始触发器 | 条件与动作 |
|---|---|
| `07506C5C` Enable Victory | 事件 `10,0,14`：BadGuy1 建筑清空；启用 Mission Victory，并停止相应提示 |
| `075F22EC` Mission Victory | 从启用时等 10 秒，动作 `1` 将原始国家 `13` 判胜 |
| `07B92F3C` Objective 1 Complete | 事件 `11,0,15`：BadGuy2 的车辆/步兵和建筑清空；输出原始目标文本及语音请求 |
| `08B9B62C` Enable Failure | 原始谭雅被毁时，启用 `0B1C7E9C` |
| `0B1C7E9C` Enable Failure b | 等 3 秒，启用 Mission Failed |
| `08B9BC5C` Mission Failed | 再等 2 秒，动作 `2` 将玩家判负 |

国家 `13/14/15` 映射当前房屋 `0/1/2`，不能直接作为玩家数组下标。逐字记录见本地 `review_out/native-campaign/native-mission-source-audit.json`；地图 SHA256 仍为 `ad64c9293a8bccb85c38f5871a974ed9c09b8b5da11bb346e990423bf625c79c`。

目标完成的文本及任务专属语音仍由未接线队列保留，不能把触发器执行称为媒体已播放。结算时已选择原始 MissionAccomplished / MissionFailed EVA，普通遭遇战保留 Victory / Defeat。

## 房屋计数

`world_native_mission.cpp` 处理事件 9（车辆/步兵清空）、10（建筑清空）、11（二者清空）：

- AircraftType 不计入，空运的步兵仍属于房屋。存活的 DMISL 不阻挡舰队目标完成。
- 按当前归属和 Significant 标志检查实际实体与嵌套乘员；Insignificant、原始 Terrain/Overlay/桥梁载体及普通墙段不阻挡全灭。
- 可收起建筑按车辆类统计，建造厂保留建筑类。非建造厂的 RA2 形态转换仍需其他关卡验证。
- 已开工的地面单位、生产中的建筑及侧栏待放置建筑计入，未开工队列不计入。完整原生生产身份仍是后续工作。
- 未参战的合法国家号没有房屋实例时条件成立，开局为空的房屋无需先拥有过对象；无效负数或越界操作数拒绝。

依据本机 RA2 编辑器 `FAData.ini` 参数表，并参考 OpenTS 的 [事件 9](https://opents-developers.github.io/OpenTS/mapping/events/tevent-units-destroyed/)、[事件 10](https://opents-developers.github.io/OpenTS/mapping/events/tevent-buildings-destroyed/) 和 [事件 11](https://opents-developers.github.io/OpenTS/mapping/events/tevent-all-destroyed/) 实现说明。这些给出继承自 TS 的计数依据，不能替代 RA2 实机对生产、捕获和同帧事件边界的校准。

## 结算与保存

`NativeEventState` 保存结果、发生 tick 和原始触发器定义索引，全部参与 checksum。当前保留第一项已执行的玩家终局动作；应用随后停止正常战斗更新。同帧冲突动作的原版优先级仍待核对。动作 1/2 当前支持本关的场景玩家目标，外部房屋的胜负传播明确阻塞。

活动原生任务由此结果驱动 `gameOver/victory`，不因普通遭遇战的歼灭判定提前结束。普通自定义任务及 Lua 原有路径保留。结算覆层沿用现有应用布局，完整原版评分与战役收尾尚未完成。

本批写入 **World v41（RA2WLD41）**，应用封套仍为 **v10**，当时 SourceScene 为 **v9**，LAN 为 **v18**；后续 [断桥批](native-demolition.md) 将来源和协议升级为 Scene v10 / NativeEvents v2 / LAN v19，保存布局保持不变。读档校验结果枚举、时间、来源动作、目标房屋及实际触发记录；运行中结果不能带遗留终局来源。继续读取 v40 及更早已支持格式，旧记录默认运行中；增加普通 World40 兼容夹具，并加载升级前的实际活跃事件存档验证续跑。

## 验证

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_mission|native_audio|native_events|native_teams|app_state|state)$' -V
```

`ra2.native_mission` 的正向夹具保留明确边界：

1. 完整原始开局运行到输入解锁，额外放置测试谭雅以隔离命中；实际玩家 Cmd 使用原始 2500 点 Sapper 击沉四艘原舰，触发 Objective 1 Complete，飞行中的 DMISL 不阻挡。没有称其为原作者的单谭雅自然通关。
2. 明确移除 BadGuy1 源建筑，保留军队与其他房屋，检查最后一座建筑、10 秒启用计时及胜利动作。该夹具诊断胜利 tick=302，不是原版通关时刻。
3. 对原始带标签谭雅造成实际致死伤害，检查 3+2 秒失败链及应用结算，夹具失败 tick=150。

另覆盖实际登载乘员、Insignificant、建造厂、生产/侧栏状态、当前归属、未参战房屋、等待与终局续档、两个实际 World40 存档、非法序列化及两种尺寸实绘。`ra2.native_audio` 在真实静音设备上，由作者终局动作触发并验证任务结算 EVA 只播放一次。

2026-11-14 本批完整 Release **106 项中 105 通过、空战役矩阵 1 项按约定跳过、0 失败**，910.78 秒；覆盖普通玩法、全部现有原生机制、历史存档、素材、HUD 和双进程联机。日志为 `build/native-mission-full-tests.log`，精简记录为 `build/native-mission-full-audit.txt`。测试前后的构建与关键场景输入哈希一致。

相关 **ASan Release 30/30 通过**，488.45 秒，未报告 AddressSanitizer 错误；覆盖任务结算、原生音频、事件/队伍、导弹、桥梁、普通战斗、生命周期与续档。配置为 `RA2_ASAN=ON` 的 Release，属于相关选择集。日志为 `build-asan/native-mission-final-tests.log`，精简记录为 `build-asan/native-mission-final-audit.txt`。

最终完整 Release、相关 ASan 配置及二进制/场景哈希位于本地 `review_out/native-campaign/native-mission-validation.json`；画面位于 `build/native-mission-audit/`。

## 仍需完成

后续已接入 [脚本爆破与断桥](native-demolition.md)，连续坍塌及原版伤害边界仍需校准。其他原始条件、队伍指令、运输/增援等动作；完整原生生产身份与地图规则；任务专属文字、语音和影片；原生开局、重开、进度和选关的应用流程；评分与人工整关通关仍未完成。完整战役和原版时序/三维战斗校准也仍未完成，整体复刻目标保持进行中。

2026-11-15 更新：`assets/campaigns/ra2_all01.ini` 将原版第一关接入普通主菜单。`NativeScene` 路径在独立 World 验证后启动，原任务保留自身阵营、资金、科技及 AI，不使用遭遇战初始部队和设置。24 个任务文字标签从原 CSF 提取，行动 21 通过原始 EVA 键播放任务录音；任务清单跟随作者触发器的已保存次数更新。共同 HUD 不再因原生生产栏提前返回而隐藏文字、目标和底栏操作。

正常简报、开始、续档、作者失败、菜单重开和返回由 `ra2.native_campaign` 覆盖；完整正常指令通关改为从同一正式入口开始，并验证四项目标与进度。影片按用户要求不还原。其余原版关卡、完整评分、未支持的演出效果、人工整关和完整规则校准继续待做。
