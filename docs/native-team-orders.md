# 原始队伍移动、警戒与控制交还

第一关的原始 Tanya 队伍现在执行 Move to Waypoint(3) 与 Guard(5)，沿原图实际移动后按原始计时链交还控制。解释器另支持队伍脚本 Change House(20)，并修正了创建队伍与执行其后续脚本之间的阻塞边界。

这仍是未完成原任务的一部分。完整第一关没有加入选关；任务影片、语音、完整战斗脚本、目标和胜负尚待接入。下列 tick 是当前项目 30 Hz 的诊断值，不是原版实机时序证明。

## 原始记录与实际开场

`094F68EC`（Player - Tanya1）从场上招募原始 Tanya，使用脚本 `09680DBC`：

1. `3,36`：前往路径点 36，即原图的 (60,99)。
2. `5,30`：警戒 30 秒。

原始出生记录位于 (62,108)，运行时单位使用格心坐标。地图覆盖的 `General.StrayDistance=2` 单独导出到 `NativeEvents.StrayDistance`，旧导出文件默认 2。

完整原场景专项中，Tanya 在 tick=1500 开始移动，tick=1594 进入警戒；原始 Unlock Input → Unlock Input 2 → Unlock Input 3 计时链在 tick=1650 执行 Unlock Input。移动、警戒都通过实际逐 tick 更新和续档验证，未用设置坐标或提前改输入锁替代。

## 十一名 GI 与十四人编制

原图 `Other3 House` 只预置 **11 名 E1**，但队伍 `0A7475CC` 的 TaskForce `08A645E0` 写的是 `14,E1`。这两个原始记录都保留。

交接还有独立的原作者链：`07A707CC` 创建征召兵队伍并启用 `07428AAC`（Enable GI's），后者创建 GI 队伍并启用 `07BBD33C`。`07BBD33C` 的 All Change House 动作把 Other3 房屋的 11 名现存 GI 整体移交给玩家。没有把编制改成 11，也没有补造三名士兵。该链的专项明确启用原始父触发器来隔离验证，并不把它当作整关自然通关。

此前 `nativeTeamBlocker` 要求队伍未来的所有脚本行都已经实现，导致征召兵队伍的后续 Attack(0) 未实现时，连 Create Team 和后面的 Enable GI's 都无法执行。现在可创建的队伍正常建立，先执行已支持的移动行；真正到达未知脚本行时才保留 `blockedScript`，不推进游标。尚不具备的运输编组、类型资料或增援能力仍明确阻塞对应动作，不生成替代单位。

GI 的 14 人队伍可保持未满状态，独立的归属交接继续执行。不能从 `Full=no` 猜测“任意人数立即启动”，也不能为了让测试通过修改原始编制。原始记录、GI 清单和来源哈希在本地 `review_out/native-campaign/team-orders-source-audit.json`。

## 执行约定

`world_native_team_orders.cpp` 负责移动目的地、警戒和归属；`world_native_scripts.cpp` 只分发并推进公共游标。

- 移动行以最近加入的存活成员作当前领队，首次进入该行时选择目的地。原路径点无法进入时，按领队通行域选择最近的可进入格；选择与路线可达性分开。没有路线时保留该目的地并重试，公共移动命令调整到本地可达格不算完成原脚本。
- 所有有效成员到达目的地的 Stray 范围后才推进，区分桥面与地面。每名成员通过公共 `orderMove` 寻路，避免普通大编队散开目标超出 Stray。`Aggressive=yes` 允许正在交战的成员暂不归队；本关相关队伍原值为 no。
- 非零 Guard 从到达该行开始计时，等待完整转换时长，在期限后的队伍更新推进。等待时按成员位置选择归队中心，超出 Stray 的成员实际走回。Guard(0) 保留原有矿车正常采矿行为。
- Change House 脚本更新成员归属并清除旧移动/攻击订单，保留队伍原定义的房屋。运行时 `memberHouse` 区分原队伍房屋与当前成员房屋，后面的移动行仍能执行；无匹配房屋的合法编号不创造新房屋。
- 玩家实际接受的 Move、Attack、Stop、Guard、Scatter 命令释放对应脚本成员；拒绝的移动/攻击保留原脚本。死亡和对象标签保留，因此玩家接管并续档后，Tanya 死亡仍能启用原始失败链。其他单位交互与完整原版脱队规则仍需扩展。

数字指令与基本行为核对参考 [Move to waypoint](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-move/)、[Guard](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-guard/) 和 [Change house](https://opents-developers.github.io/OpenTS/mapping/missions/tmission-change-house/)。计时 Guard 使用秒数而非把参数 30 当作本项目 30 tick。房屋、队伍焦点与计时结构另参考 [YRpp TeamClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/TeamClass.h) 和 [TeamTypeClass](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/TeamTypeClass.h)。这些是实现参考，TS 与 RA2 的完整协调、招募和捕获语义仍须分别校准。

## 持久化与验收

本批以 World v38（`RA2WLD38`）保存当前成员房屋和实际目的地层；游标、目的地、警戒/重试期限全部参与 checksum。队伍存储使用追加时引用稳定的容器，新加入的队伍不在同一次队伍遍历中抢先执行。该批应用封套为 v10，LAN 模拟协议为 v14。类别攻击批增加 World v39 / LAN v15；常规武器批为 World v40 / LAN v16；当前 V3 批沿用 World v40，LAN 升为 v17（见 [原始 V3](native-v3.md)），见 [类别攻击](native-team-attack.md) 和 [常规武器](native-weapons.md)。

继续读取 v37 及已有历史格式。普通地图兼容夹具增加 v10/World v37；本机还在升级前保留了一份由旧程序实际写出的 v37 原始四舰攻击存档，检查其成员房屋默认恢复和新版本续跑。这个可选旧档在 `review_out/native-campaign/legacy-native37-team.sav`，不是把当前格式只改文件头冒充原生旧档。

```powershell
cmake -S . -B build
cmake --build build --config Release
python tools/ra2pack/gen_native_scene.py all01t.map
ctest --test-dir build -C Release -R '^ra2\.(native_team_orders|native_events|native_teams|native_rockets|app_state|state|net_protocol|native_events_assets)$' -V
```

`ra2.native_team_orders` 包含完整原场景的 Tanya 开场移动、警戒和输入交还，以及原始 GI 交接链、玩家接管后标签/失败链、应用与 World 续档、实际旧 v37 存档、不可达路径及恢复、完整警戒期限、非法状态、未知后续指令和两个显示尺寸的机制画面。Change House 连续执行、未知指令和 11 人编队移动反例使用明确标记的源副本；其中编队夹具把测试编制设为实际 11 人，原始 14 人编制另独立核对。

2026-11-14 本批完整 Release **100 项中 99 通过、空战役矩阵 1 项按约定跳过、0 失败**，811.58 秒。日志为 `build/native-team-orders-full-tests.log`，精简断言为 `build/native-team-orders-full-audit.txt`；包含原始队伍命令、四舰完整攻击、导弹、历史存档、普通移动/战斗、资源与双进程联机检查。

最终相关 ASan **24/24 通过**，580.55 秒，日志未报告 AddressSanitizer 错误。覆盖队伍命令、事件、原生场景、桥梁/修桥/闸门、导弹、编队、实体生命周期、普通战斗、AI 侦察/防空、存档和网络协议；四舰长攻击链由上述 Release 覆盖。这不是完整 ASan 或原版整关通关证明。日志为 `build-asan/native-team-orders-final-tests.log`，精简断言为 `build-asan/native-team-orders-final-audit.txt`；构建和关键文件哈希记录于本地 `review_out/native-campaign/native-team-orders-validation.json`。

画面位于 `build/native-team-orders-audit/`；覆盖报告 `build/native-events-audit/coverage.txt` 现另列未实现脚本，不把队伍已建立算作全部指令已完成。机制画面保留原图建筑和树木遮挡，不替代完整视听校准或人工整局验收。

仍需完成：Attack(0) 的完整 RA2 类别/评分校准、队伍跳转和任务切换、运输/卸载、局部变量、完整编队/重整/捕获副作用与脱队规则、离图流程，以及任务媒体、目标和胜负。当前移动使用公共路径与占位模型；原版完整领队/队形、Stray 距离公式和墙钟时间仍未逐帧对照。

后续目标与胜负批已接入原图的全灭条件、玩家终局动作、应用结算和任务结果 EVA；当前写入 World v41，App v10 / SourceScene v9 保持，LAN 为 v18。完整范围、验证及剩余工作见 [原始目标与胜负](native-mission.md)，本文的既有测试成绩保留其历史范围。
