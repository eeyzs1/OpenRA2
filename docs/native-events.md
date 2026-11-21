# 原版事件图与基地交接

盟军第一关已开始执行原作者的部分事件。本批走通实际 CellTag 进入事件、启用后的两秒计时、27 个对象的归属交接和一辆原始 CMIN 增援；交接后的闸门可以按原联盟关系实际通行。没有把门预先改归属，也没有使用近似波次代替作者记录。

**本关仍未加入选关。** 原图全部记录已经保留，但完整事件、任务队伍脚本、音视频、目标和胜负尚未实现；不能据此声称第一关可通关。

## 原始记录

Scene v5 完整导出第一关的 130 个触发器、128 个标签、180 个格子标签、101 个路径点、49 个队伍类型、29 个 TaskForce 和 38 个脚本。原始事件三元组、每个动作的完整八字段、难度标志、初始禁用态、链接及引用都保留。`NativeEvent.record.*` 保留所有原始队伍/脚本字段，未过滤不支持的指令。

原始国家编号不能当成本工程的玩家下标。本关的地图国家追加在核心 13 个国家后，`13=Player` 映射为 World 房屋 0，`17=Civilian1` 映射为房屋 4。触发器所属国家、动作目标国家和场景房屋显式关联。原图 `FogOfWar=no`、`RevealTriggerRadius=9` 在事件运行时执行；普通遭遇战和未启动事件的场景检查保留原有迷雾行为。

原始 738 个初始对象和其中实际使用的 103 种类型没有改变。事件批增加未预置的 CMIN 增援定义；它使用核心 `rules.ini` 的生命、价格和装甲、已有 ChronoMiner 采矿能力以及原始 CMIN VXL/HVA 图像，未在加载精炼厂时偷偷赠送矿车。后续导弹批增加 DMISL 与 V3ROCKET 运行时定义，当前共 106 个定义，详见 [原生导弹](native-rockets.md)。

## 本批走通的作者链

1. 玩家所属房屋 13 的地面单位实际抵达 `0965B06C` 标记格的格心，才向 `Change House 2a`（`096564CC`）提供进入事件。其他房屋不匹配；仅越过格子边界、原地等待和重复路径点不能代替一次新格抵达。
2. 原始动作列表启用 `Change House 2b`（`0965E07C`），揭示指定路径点周围、请求雷达和任务语音、创建原始矿车队伍，并禁用相应的旧提示触发器。
3. `2b` 的 Elapsed Time=2 从启用时重新计时，在本工程 30 Hz 下到第 60 tick 才执行。
4. Change House 按当前标签实例交接全部 27 个相连对象，包括来自三个原始房屋的建筑、犬和闸门。原始对象键和来源标签继续保留；只改变运行时归属，并刷新电力和门权限。
5. 原始 at-waypoint 增援动作按队伍 `0831459C`、路径点 E 创建一辆 CMIN。另一个 Create Team 动作只创建招募队伍，不额外生成单位。两个队伍引用的原脚本均为 Guard(0)，矿车保留自动采矿能力。

该链还产生任务语音 `Mis_A1_EvaControlFort`、影片编号 68 等**原始表现请求**。请求包含完整参数，应用层保留未接线项；本批没有用通用提示音、占位字幕或其他影片替代，也没有宣称这些内容已播放。瞬时表现请求不随 World 续档重放；原机音视频播放及其应用存档状态仍需实现。

## 执行模型与边界

`native_events_parse.cpp` 校验和解析图；`world_native_events.cpp` 管理标签实例、触发条件、启用计时和进入/死亡通知；`world_native_actions.cpp` 执行动作；`world_native_teams.cpp` 管理当前支持的招募与增援。主 `World` 仅增加薄调用。UI 从 `game_native_events.cpp` 消费表现请求，不能修改事件结果。

基础模型参考 [YRpp TriggerClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/TriggerClass.h)、[TagClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/TagClass.h) 以及 [OpenTS 的触发机制说明](https://opents-developers.github.io/OpenTS/systems/trigger-springing/)，并以本机地图和编辑器参数表核对原始记录。当前包含事件的 AND 条件、有向房屋匹配、启用重置计时、难度许可、一次性/持续标签、链接和同标签重入保护；原版 RA2 的完整时序仍待实机比对。

实际支持的条件为本关使用的常量操作数形式的地面进入、计时、指定建筑存在/不存在和场上附着对象被毁。其他条件保留原数值，覆盖报告明确列为未实现。普通生产产生的同类建筑也参与存在条件；招募原始单位时按原始类型匹配，不能因多个类型共用 NativeVehicle 载体而把轿车招为警车。空中/传送入格、乘员在容器内死亡的完整通知和表达式操作数仍待补齐。

队伍执行包含地面对象的 Attack Waypoint，以及后续补齐的 Move to Waypoint、计时 Guard 和 Change House，见 [队伍脚本](native-teams.md) 和 [队伍命令](native-team-orders.md)。可创建队伍的后续未知脚本不阻断 Create Team，运行到未知行时保存明确的脚本阻塞。历史 NativeEvents v1/v2 在缺少定义、运输编组、动作实现或增援位置时暂缓整个动作列表。

后续 [奖励箱与顺序动作批](native-crates.md) 按原引擎接口说明引入 NativeEvents v3：逐项尝试来源动作，记录首个失败并继续后续条目，正常结算触发次数与一次性标签。增援仍在单条动作内部检查和预约位置。旧来源继续使用历史规则；新的失败增援和未知中间动作专项验证前后动作可实际执行。

演员的原始身份与当前标签实例分开。运行时标签和上一抵达格进入存档；登载、驻军和卸载携带这些字段，槽位复用清除旧关联。一次性标签销毁后清理相应运行时绑定。独立乘员归属超过现有容器模型表达能力时，交接动作拒绝执行，保留待补齐状态。

## 持久化与验证

事件批以 World v35（`RA2WRLDZ`）保存事件是否启用、难度、输入锁、标签/触发器实例、条件记忆、计时起点、执行次数、阻塞动作、队伍成员及演员/乘员绑定。v36 增加脚本目标和阻塞，v37 增加原生导弹；队伍命令批写入 v38，增加成员房屋和目的地层；类别攻击批写入 v39，增加待推进标志；当前常规武器批写入 v40（`RA2WLD40`），继续读取 v39/v38/v37/v36/v35。原始图由保存的 Scene 源文重建；这些状态进入 checksum。临时分发栈和表现请求不入 checksum。应用封套仍为 v10，普通地图兼容夹具覆盖到 v10/World v39；LAN 模拟协议为 v16。

```powershell
python tools/ra2pack/gen_native_scene.py all01t.map
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_events|native_events_assets|native_scene_assets|app_state|state)$' -V
```

`ra2.native_events_assets` 从本机 MIX 独立核对每条图记录、队伍、脚本及国家映射。`ra2.native_events` 在完整原场景上通过实际移动指令触发交接，核对两秒期限、27 个对象、唯一矿车、原联盟下门通行、应用/World 续档、乘员标签、编号复用、非法存档与阻塞列表。机制夹具只额外生成进入区域的测试单位；没有改原始联盟来取得门权限。堵塞和未知动作另用明确的反例夹具检查。

2026-11-14 事件批最终版本完整 Release **96 项中 95 通过、空战役矩阵 1 项按约定跳过、0 失败**，787.08 秒。相关 ASan **19/19 通过**，316.20 秒，日志未报告 AddressSanitizer 错误；覆盖事件、闸门、修桥、分层、乘员装卸载、工程师、编队、让路、生产反馈、侦察记忆、存档和网络协议。这不是完整 ASan 或原版任务通关证明。后续队伍脚本批的当前验证另见上述队伍记录。

完整 Release 证据为 `build/native-events-full-tests.log` 和 `build/native-events-full-console.log`，精简断言为 `build/native-events-final-audit.txt`。ASan 对应 `build-asan/native-events-final-tests.log`、`build-asan/native-events-final-console.log` 和 `build-asan/native-events-final-audit.txt`。画面及未实现项覆盖报告位于 `build/native-events-audit/`；其中 `coverage.txt` 明确列出未实现事件及受阻动作。画面是原场景机制专项，不是已完成关卡截图。

后续仍须补齐其他触发条件、全部任务队伍指令与原始增援类型、任务文本/语音/影片、原始桥梁破坏/修复过程、目标与胜负，以及人工整局验收。事件解释器当前显式启动，用于原场景验证；场景加载不自动开启一个未完成的任务。

后续队伍批已接上 `Mission Start`（`07A9578C`）的四次创建、DRED 招募、`1,0` 攻击指令与续档，修正了显式动作误用 Max 和同类队伍共享标签的问题。导弹批又接入 DMISL 实体、槽位、阶段、本图弹头与实际防空弹；完整飞行/时序仍待校准，见 [原生导弹](native-rockets.md)。后续继续按源记录扩展其他队伍与条件，接任务媒体、目标和胜负，最后进行第一关从开局到结算的人工验收。

当前类别攻击与原始空编组的验证范围见 [原始队伍类别攻击](native-team-attack.md)。覆盖报告把已支持的类别 Attack 与未校准的 RA2 附加类别分别标记，队伍被创建不代表其全部脚本已执行。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA，见 [原始目标与胜负](native-mission.md)。断桥批再接入动作 63 和原图开局爆破，见 [脚本爆破与断桥](native-demolition.md)。脚本控制批增加 Jump(6)、Success(49) 和队伍成功状态续档，见 [脚本控制](native-script-control.md)。玩家生产批接通真实谭雅入区、基地移交、原生兵营建造、工程师训练与修桥，范围见 [玩家生产](native-production.md)。原生 AI 批又补上修桥小屋 EnteredBy 对原始动作 74/3 的启动通知，接通本地队伍与独立工厂作业，见 [原生 AI](native-ai.md)。奖励箱批再接入动作 101/108 和顺序执行，当前为 World v45 / App v10 / SourceScene v13 / NativeEvents v3 / NativeProduction v1 / NativeAI v1 / NativeCrates v1 / LAN v23，见 [奖励箱](native-crates.md)。本文的既有测试成绩保留其历史范围。

后续条件批已推进到 World46 / App10 / SourceScene14 / NativeEvents4 / LAN24，并接入发现、受击、生产、选择和局部变量及卸载脚本，详见 [条件与卸载](native-conditions.md)。上文的格式、覆盖数量与测试成绩保留其历史批次范围；自动运输组装和完整任务流程仍未验收。
