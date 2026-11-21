# 原版闸门与桥头入口

本批接入原始闸门的开合、通行权限、动画和续档。工程师修桥专项已保留闸门，通过正常开门取得入口，不再以炸掉闸门代替通行。后续 [原版事件批](native-events.md) 已通过实际区域进入和两秒计时完成门与基地的交接；完整事件、目标和胜负仍未完成，第一关尚未加入选关。

## 原始数据与任务边界

盟军第一关只有一座 `GAGATE_A`，位于 `(81,48)`，占地 `3×1`，原归属为 `Civie1 House`。其源对象键及标签 `0965BDDC` 保留。标签对应原作者的 `Change House 2b` 触发器 `0965E07C`，包含 Change House 动作；开局该房屋对玩家并不友好。不能将闸门永久改成玩家所有，或把机制夹具中的停战联盟当作任务交接已经执行。

原始依赖继续追溯为 `Change House 2a`（`096564CC`）的进入事件通过 Enable Trigger 启用 `2b`；`2b` 本身具有 Elapsed Time 事件。相应 CellTag 为 `0965B06C`。原始条目及启用关系保留在 `review_out/native-campaign/gate-events-source.json`，后续事件批已按这条链交接 27 个对象，没有添加无条件开局赠送。

本机核心 `rules.ini` 提供 `Gate=yes`、`Strength=100`、`DeployTime=.044`、`GateCloseDelay=.2`；`art.ini` 提供 `GateStages=9`、`Foundation=3x1`。原始 `gugate_a.shp` 位于 `ra2.mix/urban.mix`，SHA-256 为 `3dd241b115c30f273239f9003b0ab64b97ea86341bc5784fca39c7f3a7d060ae`。原始画布为 `246×210`，共 42 帧，其中 21 个主体帧配对 21 个阴影帧。

资源导出为 Scene v4，增加门标志、阶段数和原始时间帧数。时间按原格式每游戏分钟 900 帧截断，得到开合 39、关闭延迟 180 个原始逻辑帧；运行时换算为本工程 30 Hz 的 78、360 tick。正常门使用主体帧 `0..8`，受损门使用 `10..18`；其余源帧也完整导出，未生成替代图像。使用原版 `uniturb.pal`、原始偏移、配对阴影与独立的阵营变色遮罩。

[YRpp 的 BuildingClass 接口](https://raw.githubusercontent.com/Phobos-developers/YRpp/master/BuildingClass.h) 区分需要等待的 `MakeTraversable` 和实际可通行的 `IsTraversable`，并保留 GateTimer。[OpenTS 的 GateStages 说明](https://opents-developers.github.io/OpenTS/keys/gatestages/) 提供同源引擎的正常/受损帧布局；本机原始 SHP 逐帧核对和原始规则另作为数据依据。原版 RA2 的逐 tick 计时、请求探测边界和关门反转细节尚无实机对照，不据此声称全部原引擎逻辑已经一致。

原版全局 `GateUp`、`GateDown` 都指向 `Dummy`；本批不为本关闸门拼接不相符的开合声音。自定义门音及其他剧场/门型仍待扩展。

## 通行与状态

模拟由 `world_native_gates.cpp` 管理。闭门、开门、全开、关门是明确的模拟阶段，进度及剩余关闭时间进入 checksum。友军可以规划穿门路线；只有抵达门前的实际移动需求才请求开门，单位等到完全打开才通过。正在关闭时有新的合法需求，会从当前进度重新打开。

开门权限由闸门所属房屋对单位所属房屋的有向联盟决定。反向单方面友好不等于门会打开。封闭敌门不为无权单位打开；已经打开的门可以让敌方通过。门内有地面单位时保持打开，避免关门将其封在建筑内。飞行、磁力悬浮、寄生单位及不同物理高度不会请求或占用这条通路。

建筑逻辑占位始终保留：`bldAt`、选取、原始身份、归属和建造阻挡不随动画删除。派生的门格缓存只改变物理通行；普通 A*、双层 A*、编队连通区和交互路线显式携带权限掩码，实际移动另检查物理开门阶段。正常占领和尤里首脑夺取后会立即刷新门格归属。闸门销毁后清除其通行缓存，新实体复用旧编号不能继承门状态。建造、补给箱和矿石生长仍按建筑占位检查。

## 表现与持久化

门进度选择原始 SHP 帧，不消费 World RNG。观察记忆保存实际见过的帧；失去视野后即使门已经关闭，也继续绘制之前见到的姿态。实际画面覆盖 1280×720 和 1920×1080 的闭门、开门中、全开及受损全开。

闸门批以 World v34（`RA2WRLDY`）在既有数据后保存实际门的阶段、进度和剩余延迟；仅含门定义的源场景具有此扩展。计数、实体身份、顺序、阶段与时间范围均校验，截断或多余数据拒绝读取。后续事件批写入 v35 保存原始事件实例，当前队伍脚本批写入 v36 另保存脚本目标；普通非原生地图保留既有尾部布局。应用封套仍为 v10，兼容 v9/v8/v7，另覆盖 v10 封套中的 World v31/v32/v33/v34/v35。当前 LAN 模拟协议为 v12。

## 验证

```powershell
python tools/ra2pack/gen_native_scene.py all01t.map
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_gates|native_bridge_repair|native_scene_assets|app_state|state)$' -V
```

`ra2.native_gates` 使用完整源场景中的真实门，保留全部原始建筑、墙和地形；仅在门旁清走现有移动单位，并设置停战及一组有向敌对关系以隔离机制。检查实际指令规划、开门等待、合法穿门、无人关门、关门反转、门内停留、敌方通行、归属变化、健康/受损原帧、隐藏记忆、各阶段续档、非法状态拒绝及摧毁后的编号复用。它不执行原版任务。

`ra2.native_scene_assets` 独立核对门参数与全部 21 个导出主体帧的原始调色板样本、阵营遮罩、来源 SHA 和尺寸。修桥专项现在保留完好的原闸门，验证取得联盟通行权限后工程师实际穿门、到达小屋并消耗；没有修改导出地图或扩大修桥距离。

2026-11-14 完整 Release 94 项为 **93 通过、空战役矩阵 1 项按约定跳过、0 失败**，776.54 秒。完整运行期间另修正显式地图生成单位位于封闭敌门内的计时边界，避免产生不可保存的关闭计时；收尾代码随后重新构建，相关 Release **20/20 通过**，62.21 秒，涵盖门、双层桥、修桥、工程师、编队/让路、交互、原场景/资源、存档和网络。完整运行和最后修改后的相关运行分开记录。

最终版本相关 ASan **18/18 通过**，285.74 秒，未报告内存错误。它覆盖上述模拟、交互和持久化专项，资源的 Python 核对已在 Release 执行；这不是完整 ASan 或原版关卡通关证据。

本机证据为 `build/native-gates-full-tests.log`、`build/native-gates-full-audit.txt` 及最后修改后的 `build/native-gates-final-tests.log`、`build/native-gates-final-audit.txt`。ASan 证据为 `build-asan/native-gates-final-tests.log`、`build-asan/native-gates-final-audit.txt`。实际帧位于 `build/native-gates-audit/`；源帧检查图位于忽略目录 `review_out/native-campaign/gate-frames.png`。

## 尚未完成

后续已接入部分原始触发器启用/关闭和房屋交接。完整任务条件与队伍脚本、连续桥梁坍塌、完整修桥表现及胜负仍未完成；本关仍没有原版通关证据。武装单位自动破坏挡路敌门、任意闸门布置、SpecialZOverlay 与单位遮挡的精确排序、门声音扩展和原机时序校准也未在本批宣称完成。
