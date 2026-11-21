# 原始第一关的脚本爆破与断桥

all01t 的 `Break Bridges` 现已执行原图动作 63，并与桥面移除、失去支撑、工程师修桥及续档接通。原始事件仍须显式启动，本关尚未作为完成的战役加入选关。当前实现保留原图 `DestroyableBridges=no`，脚本断桥不会放开普通火力毁桥。

## 原始记录与执行

触发器 `080777BC` 的条件为 `1,13,0,0`，在启动后的第一次事件更新满足；动作列表有十项：前五项数字路径点为 80，后五项为 79。实际坐标分别是 `(62,59)` 和 `(82,52)`，覆盖两座高桥。尾部字符串 `CC/CB` 不是动作 63 的寻址依据。记录由 `ra2.native_demolition_assets` 从本机原始 MIX 独立核对，不修改地图触发器。

每次动作按中心及四个对角偏移各执行一次 100 点爆炸。偏移采用整数 lepton 坐标 `±85`；弹头取 `[CombatDamage] C4Warhead=Super`，不是编辑器摘要中提到的 HE。爆点位于原桥面高度，同一位置的后续调用在桥梁消失后仍保持该高度。爆炸不提供开火单位或房屋，不产生战斗伤害统计、经验或还击目标。

来源导出另保留 `BridgeStrength=1500`、`FlightLevel=1500`、十一类 `Verses`、`CellSpread`、`PercentAtMax`、`Wall/Wood/Tiberium` 及 `AnimList`。本关 `Super` 的十一类倍率均为 100%，没有显式 `CellSpread/PercentAtMax`；导出同时标记默认值，避免把推定值称为原文件中存在的字段。当前零扩散模型按同格、同高度处理；有扩散时按三维距离及来源倍率计算。普通爆炸经过既有伤害入口，保留无敌和 Immune 检查。

依据包括本地原始规则与图文件，以及 OpenTS 的 [动作 63 实现说明](https://opents-developers.github.io/OpenTS/mapping/actions/taction-damage/) 和 [C4Warhead 说明](https://opents-developers.github.io/OpenTS/keys/c4warhead/)。OpenTS 描述了继承自 TS 的五次爆炸、重复毁桥尝试和无来源伤害；它不能独自证明 RA2 的精确伤害边界。RA2 的 `CellSpread` 与 TS 的旧 `Spread` 也没有混为一个配置项。

## 桥面与单位后果

现有九段原始桥梁形成两组跨度、二十七格桥面。动作完成五次爆炸后，移除目标跨度中的存活原始桥段，重建表面及占位。相邻另一跨度保留；后续对已经消失的桥梁重复调用安全无副作用。

下层单位继续使用下层表面，不因上层消失受到坍塌伤害。桥上落回合法地表或水面的单位受到来源 `BridgeStrength` 的强制伤害，这一路径越过无敌、军衔和箱子护甲；不能停留在下层水面或被实际占位阻挡的单位按现有失去支撑规则销毁。当前仍采用整个跨度的最终坍塌状态，尚未模拟原版最多四次毁桥尝试产生的中间端头、连续坍塌、残骸及落水动画。

修桥继续按不可变源身份恢复原始桥段，不能复活已经复用的实体编号。专项使用原始 CellTag 入区、两秒基地移交和闸门权限，再发出真实工程师命令修复关联跨度；不通过改变联盟取得权限，也不直接调用恢复方法代替实际到达。额外生成的普通机制工程师不具原生 ENGINEER 身份；此检查不验证原生工程师数值、生产或原关自然获得工程师的流程。

## 模块与兼容

- `world_native_demolition.cpp` 管理爆点、高度、受击者收集和脚本坍塌，模拟层不调用绘图或音频。
- `world_native_actions.cpp` 严格解析动作数字路径点。完整列表预检仍拒绝未知动作或无效操作数，避免部分执行。
- `world_bridges.cpp` 处理支撑消失；`world_combat.cpp` 提供显式强制伤害参数，普通调用默认保持既有行为。
- `gen_native_events.py` / `native_events_parse.cpp` 管理 NativeEvents v2 的不可变爆破配置；解析时拒绝无效强度、非有限扩散和越界倍率。

断桥批采用 **SourceScene v10、NativeEvents v2、LAN v19**，没有新增可变保存字段，当时写入 **World v41 / App v10**；后续 [脚本控制批](native-script-control.md) 因队伍成功状态升为 World v42 / LAN v20，来源和应用封套不变。完整来源字符串已有保存与 checksum 覆盖。旧 NativeEvents v1 存档不补造新配置，动作 63 仍明确阻塞，因而旧场景的续跑能力边界保留。专项加载了升级前保存的实际 World41/Scene9 活跃事件存档，并比较后续逐 tick 校验。

## 验证与边界

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.native_demolition(_assets)?$' -V
```

专项覆盖完整作者开局十项动作、九段原桥、普通火力保护、数字路径点与错误尾部标签、五次独立伤害、无敌、无来源统计、三维高度和坍塌、另一跨度隔离、真实闸门与修桥命令、等待/爆破后/修桥途中的逐 tick 续档、应用存读档、实际旧存档、无效来源配置及两种尺寸实绘。桥下保护与强制坍塌使用明确的反例场景；伤害边界测试不是原版实机对照。

实际画面为 `build/native-demolition-audit/authored-broken-{1280,1920}.png` 与 `engineer-restored-{1280,1920}.png`，绘图前后模拟 checksum 一致。该画面保留本关已有单位、建筑、地表和原始闸门，属于机制专项。

完整 Release 与相关 ASan 的最终结果、二进制/来源哈希和旧存档来源记录在本地 `review_out/native-campaign/native-demolition-validation.json`；日志为 `build/native-demolition-full-tests.log` 和 `build-asan/native-demolition-final-tests.log`。

本批完整 Release 108 项运行了 938.66 秒：106 通过、空战役矩阵 1 项跳过、1 项旧队伍攻击夹具失败。该夹具仍要求断桥后跨河命中，已更正为断桥负例及明确恢复桥面的正例；仅测试夹具改变，生产模拟与来源资产哈希保持不变。最终对应的队伍攻击和断桥两项 **2/2 通过**，10.32 秒。此处保留完整批次的实际失败记录，没有把分次验证改写成一次完整全绿运行。

最终版本相关 **ASan Release 31/31 通过**，523.78 秒，未报告 AddressSanitizer 错误；覆盖普通战斗、生命周期、装卸载、维修、生产反馈、历史存档、原生场景/音频、桥梁/修桥/闸门、事件/队伍、常规武器、两类原生导弹、胜负、断桥及网络协议。配置为 `RA2_ASAN=ON`、MSVC `/fsanitize=address`；这是相关选择集，不是完整 ASan。最终测试后，冻结输入与二进制的哈希保持一致。

精简记录为 `build/native-demolition-full-audit.txt`、`build/native-demolition-fixture-audit.txt` 及 `build-asan/native-demolition-final-audit.txt`。夹具更正后没有重跑未改动的完整集合；相关 Release 和上述 ASan 均使用最终重建的二进制。

仍需完成原版连续毁桥阶段及残骸、准确的零扩散和立体损伤边界、建筑多格受击边界、完整地形速度/步兵子格/高度规则、矿物连锁反应，以及导出的 AnimList、爆炸与落水音画。通用空军使用现有飞行比例和全局 FlightLevel，尚未据原引擎逐帧校准。其他原始任务动作、原生生产身份、任务媒体、重开与进度、人工整关通关及完整战役仍未完成。
