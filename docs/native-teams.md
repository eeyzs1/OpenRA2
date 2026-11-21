# 原始队伍脚本与 DRED 开局攻击

本批接上盟军第一关的真实 `Mission Start`：原始四次 Create Team 分别招募场上四艘 DRED，执行作者脚本 `BG1 - Attack Statue of Liberty` 的 `1,0`，锁定路径点 A 上的自由女神像。没有增设军舰、替换目标、改联盟或删掉原场景防空塔。

这只是原任务的一部分。完整第一关仍未加入选关，原始队伍的全部指令、任务媒体、目标和胜负仍待实现。后续已接入原始 DMISL 实体、生成槽位及地图弹头，当前实现和未完成的轨迹/时序校准见 [原生导弹](native-rockets.md)。

## 原始记录与修正

`07A9578C` 的计时事件连续四次创建 `0A67F6CC`。该 TeamType 的 `Max=1`，编制是一艘 DRED，原场景共有四艘；此前把 Max 当作所有创建途径的上限，会静默跳过后三支队伍。现在显式场景动作每次都创建队伍；Max 保留给尚未实现的 AI 自动组队约束，允许原始 -1 值。

语义核对参考 [Create Team](https://opents-developers.github.io/OpenTS/mapping/actions/taction-create-team/)、[Max](https://opents-developers.github.io/OpenTS/keys/max/) 和 [YRpp TeamClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/TeamClass.h)。同类型队伍现在获得各自的标签实例；销毁一个一次性标签不能清除兄弟队伍的绑定。开局场上对象和格标签仍保留原始定义对应的初始实例。

## 当前脚本执行

`world_native_teams.cpp` 负责原始类型匹配、招募和增援；`world_native_scripts.cpp` 负责脚本游标及目标。当前包含地面对象的 Attack Waypoint，以及后续加入的 Move to Waypoint、计时 Guard 和 Change House，详见 [队伍命令](native-team-orders.md)。类别 Attack 的选择、重选与待推进状态另见 [类别攻击](native-team-attack.md)。招募不会凭空补单位，未满的普通队伍保持等待；脚本结束释放队伍，单位仍存活。不同队伍不能重复占用同一艘舰。未知后续脚本保留在实际游标，不再提前拒绝队伍创建。

Attack Waypoint 在线开始时解析路径点覆盖的对象，包含原始建筑的完整脚印。第一关 A=(62,71) 位于 CANEWY04 的 3×3 脚印内，不能仅比较建筑左上角。选定对象之后跟踪该对象；离开路径点不改目标。对象销毁立即清除队伍引用，下次执行推进脚本，实体编号复用不会接管旧目标。

初始空路径点保留格目标，后来出现的对象可以被获取；这些对象消失后重新检查格子。当前公共战斗命令还没有地面强制开火，因此空格阶段明确记录 `blockedScript=1`，不伪装成攻击移动或已完成。友军强制攻击、空中/分层目标、完整弹药结束条件、队伍编队与重整、受攻击后的团队协调等仍未完成。实现参考 [Attack Waypoint](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-att-waypt/) 和 [队伍执行模型](https://opents-developers.github.io/OpenTS/systems/ai-team-execution/)，尚未完成 RA2 实机逐帧校准。

## 拦截错误与保真边界

真实开局测试暴露旧拦截路径的问题：每枚导弹每 5 tick 都可从附近防空单位承受固定 9 点伤害，不检查防空武器冷却。同一防空塔能同时持续攻击整批导弹，原场景两座 Patriot 因而永久截住军舰射击，雕像一直不掉血。

`world_interception.cpp` 现以防御武器的伤害、Special2 装甲倍率、射程和共享冷却结算，同时检查供电、施工、失能和弹药。一次开火占用该防御单位的普通攻击冷却，不能对每枚导弹分别获得一份射速。口焰和声音仍只输出表现事件。World 主更新仅调用此模块。

上述瞬时拦截仍用于没有原生生成器配置的旧弹丸。当前 all01t 已改为真实 DMISL 空中实体与原始 NASAM 防空弹，接入两槽再生、暂停/抬头及本图 DMISLWH（包含对 special_1 的 2% 倍率）。队伍批旧诊断 tick=454/1007 已被替代；当前首次受击 tick=1303、摧毁 tick=27709。**这些只是工程诊断数据，不是原版应有时刻**，三维飞行和完整时序仍待校准。

## 持久化与验收

队伍状态由 World v36 引入，保存并校验是否已组齐、当前行是否启动、固定对象/格目标、脚本阻塞和重试期限，全部参与 checksum。v37 增加原生导弹；v38 增加成员房屋与目的地层；v39 增加类别攻击的待推进标志；该批写入 World v40（`RA2WLD40`），再增加原始武器槽引用，继续读取 v39/v38/v37/v36/v35 及更早已支持格式。应用封套仍为 v10，LAN 模拟协议为 v16。普通地图兼容夹具覆盖到 v10/World v39，原事件和队伍专项另验完整原场景状态。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_teams|native_events|app_state|state|net_protocol)$' -V
```

`ra2.native_teams` 检查作者开局的四次创建、独立招募、雕像脚印选取、真实弹丸与扣血、脚本结束、应用/World 续档、目标编号复用、独立队伍标签、移动对象锁定、空格目标以及供电/共享冷却拦截。移动目标、独立标签和空格反例使用明确标记的源场景副本；原始开局攻击使用完整原场景。移动反例通过内部移动订单模拟目标移动，不绕过原任务的玩家输入锁。

2026-11-14 队伍脚本批（World v36，导弹接入前）完整 Release **97 项中 96 通过、空战役矩阵 1 项按约定跳过、0 失败**，760.80 秒。完整回归后仅调整机制截图的构图，重建后队伍专项 **1/1 通过**，5.79 秒，并检查两个显示尺寸的画面。该批最终相关 ASan **23/23 通过**，332.75 秒，日志未报告 AddressSanitizer 错误，包含队伍、事件、桥梁/闸门、乘员、战斗连发、即时战斗、AI 防空、存档和网络协议。这不是完整 ASan 或原版整关通关证明。当前导弹批结果另见 [原生导弹验收](native-rockets.md)。

本机完整日志为 `build/native-teams-full-tests.log`，精简断言为 `build/native-teams-full-audit.txt`；构图调整后复验为 `build/native-teams-final-visual-tests.log` 和 `build/native-teams-final-visual-audit.txt`。ASan 对应 `build-asan/native-teams-final-tests.log` 和 `build-asan/native-teams-final-audit.txt`，机制画面在 `build/native-teams-audit/`。源参数审计在 `review_out/native-campaign/dred-source-rules.json`，记录核心规则、地图覆盖、来源哈希及当前工程定义，供后续校准。

下一步应继续校准原始飞行/拦截并扩展其他原始脚本、局部变量和条件，随后接任务音视频、目标、胜负并进行整关人工验收。不能仅凭军舰开火就开放一个未完成的战役。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA；当前写入 World v41，App v10 / SourceScene v9 保持，LAN 为 v18。完整范围、验证及剩余工作见 [原始目标与胜负](native-mission.md)，本文的既有测试成绩保留其历史范围。
