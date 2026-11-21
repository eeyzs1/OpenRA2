# 第一关的原始奖励箱与顺序动作

本文保留第一关作者脚本箱的历史批次与格式；当前工程格式见 [审查计划](review-plan-2026-11-15.md)。来源遭遇战的随机权重、CrateGoodie 和寿命已接入独立路径，范围与当前验证状态见 [随机补给箱](source-random-crates.md)；下文六种作者脚本效果仍保留自己的历史验收范围。

原图动作 108 已接通指定种类奖励箱的生成、实际拾取和效果；动作 101 已接通按路径点重新遮蔽地图。第一关开局的四个原始箱子现在能通过正常操作获取。完整第一关、战役与原版逐帧校准仍未完成。

## 来源与规则

`gen_native_crates.py` 从本机核心 RA2 `rules.ini` 及地图覆盖导出 CrateRadius、SoloCrateMoney 和全部 19 条 Powerups 原始记录。独立资源检查重新读取 MIX，比对原图九次动作 108 的记录；当前实际使用六种效果。保留 108 份类型定义和作者初始 738 个对象，脚本箱在事件执行时创建。

| 原始 ID | 原始种类 | 本图数值 | 当前模拟 |
|---:|---|---|---|
| 0 | Money | SoloCrateMoney=5000 | 给拾取者房屋增加来源金额 |
| 2 | HealBase | 无额外 Data | 恢复该房屋当前实际单位与建筑的来源生命 |
| 9 | Armor | 1.5 | 范围内地面对象获得永久护甲倍率 |
| 10 | Speed | 1.2 | 范围内非飞行移动对象的实际步长乘以 1.2 |
| 11 | Firepower | 2.0 | 范围内地面对象的原生常规武器伤害倍率 |
| 13 | Invulnerability | 1.0 | 当前复用无敌计时；时长与对象边界仍待原机校准 |

原始范围为 3 格。护甲、速度和火力不借用通用随机箱的 45 秒计时；用来源对象键保存倍率，登载、卸载、归属变化与实体编号复用不会混淆身份。范围不限定拾取者的阵营，附近敌方对象也会获得增益；范围外对象不受影响。军衔与箱子倍率合并后再转换为整数伤害，避免中间舍入多损失一点伤害。

原始枚举和倍数字段核对 [YRpp GeneralDefinitions](https://github.com/Phobos-developers/YRpp/blob/master/GeneralDefinitions.h)、[TechnoClass](https://github.com/Phobos-developers/YRpp/blob/master/TechnoClass.h) 和 [FootClass](https://github.com/Phobos-developers/YRpp/blob/master/FootClass.h)。范围、非叠加及全房屋治疗参考同代 [OpenTS crate 实现](https://github.com/OpenTS-Developers/OpenTS/blob/4e5fd62e930339a03c2c18be53a43a167fcf9a79/code/cell.cpp)，RA2 的四字段配置以本机原始资料为准。这些资料不证明 RA2 的每一处边界已完全一致。

## 动作执行顺序

此前模拟在执行前预检整个动作列表，一个无法执行的动作会阻止整组动作。原图 `Create Thugs` 的最后一条动作 101 因而阻止了前面的两次增援和触发器变更。

[YRpp TActionClass](https://github.com/Phobos-developers/YRpp/blob/master/TActionClass.h) 的原引擎接口说明指出，各动作会被逐项调用，不依赖前一项的返回结果。新 NativeEvents v3 因此按来源顺序尝试每条动作，继续后续条目，记录第一处失败用于诊断，并对触发次数和一次性标签正常结算。仍保留单条增援的占位与数量检查、递归与每 tick 操作预算。旧 NativeEvents v1/v2 保留其已有项目行为，加载历史来源时不自动切换。

动作 101 使用原始半径及字母路径点，修改场景玩家对应圆形区域的迷雾；其他房屋不受影响。之后正常视野更新仍可揭开本方单位附近的区域。专项分别检查重遮蔽发生时的模拟状态及后续续跑，不把它当作永久禁止侦察。

## 实际操作与机制分支

真实流程复用谭雅入区、基地交接、建造原生兵营、付费工程师和实际修桥的正向快照。玩家随后下达原生 GI 训练订单，以正常采矿和付款形成护卫队；谭雅先取西侧箱子，再在部队掩护下进入苏军基地。所有进攻、移动、部署与生产均走实际指令。

当前日志记录原始谭雅 `infantry:0` 在 tick5036 拾取火力箱、tick5198 拾取护甲箱、tick7863 拾取治疗箱、tick7872 拾取速度箱。护卫队已实际付费出厂至少十二名 GI，谭雅最终 250 生命、任务没有失败。箱子顺序来自实际路线，治疗先于相邻的速度箱被拾取。主流程未修改资金、生命、联盟或直接消灭守军。

受控分支另验证实际速度步长比 1.2、相同路线的到达时间、附近敌人/远处对象、伤害除以 1.5、火力乘以 2、不叠加、军衔合并舍入、已受伤的单位与建筑恢复、钱箱、无敌计时、实际登载/卸载、活跃乘员续档、实体编号复用，以及非法头部、数量、键、NaN、截断与多余存档数据。受控分支中的额外对象与字段设置不属于上述真实操作链。

`Create Thugs` 的独立机制分支使用原始 EnteredBy、两份来源队伍及全部动作，验证先前被挡住的批次能执行动作 101。事件专项还验证单条增援无处放置、未知动作处于列表中间时，前后已知动作仍按顺序执行。

## 格式、模块与验证

当前为 **World v45 / App v10 / SourceScene v13 / NativeEvents v3 / NativeCrates v1 / NativeAI v1 / NativeProduction v1 / LAN v23**。

- `native_crate_types.h` / `native_crate_parse.cpp` 管理来源配置；`world_native_crates.cpp` 管理生成、拾取和重遮蔽。
- `world_native_crates_save.cpp` 管理有界倍率映射、校验、存档和 checksum。Crate.kind 用独立范围引用原始 Powerup ID，不与通用随机箱编号混用。
- `world_native_actions.cpp` / `world_native_events.cpp` 管理动作准入和顺序执行；武器、受伤和移动在各自模拟模块读取同一来源倍率。
- `game_native_crate_tests.cpp` / `native_crates_assets_test.py` 管理实际流程、来源独立比对、机制和画面验证。

升级前实际保存的 World44/Scene12 活跃 AI 应用存档复制为 `review_out/native-campaign/legacy-native44-crates.sav`，另加普通 v10/World44 二进制兼容夹具。新来源资料从已保存的场景重建；旧来源没有奖励箱资料时保留其历史能力边界。

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release -R '^ra2\.(native_crates|native_crates_assets|native_events|native_production|native_ai|state|app_state)$' -V
```

最终相关 Release **13/13 通过**，73.81 秒；冻结后完整 Release **115 项中 114 通过、空战役矩阵 1 项跳过、0 失败**，877.05 秒。覆盖现有自然开局、完整遭遇战、原生功能、素材/表现矩阵和双进程同步；331 项冻结源码、配置与 Release 二进制在运行后保持一致。精简日志为 `build/native-crates-full-audit.txt`；来源和验证哈希位于本地 `review_out/native-campaign/native-crates-{source-audit,validation}.json`。

最终相关 **ASan Release 36/36 通过**，825.59 秒，未报告内存错误。配置为 `RA2_ASAN=ON` / MSVC `/fsanitize=address`，与 Release 的 GUI 检查顺序运行；覆盖相关选择集，不是全量 ASan。两种构建重现相同的四箱实战与速度对照，活动应用存档逐字节一致；测试后再次核对冻结输入和两个二进制。日志与精简记录为 `build-asan/native-crates-final-{tests.log,console.log,audit.txt}`，选择集为 `native-crates-selection.txt`。

日志与画面位于 `build/native-crates-*` 及 `build-asan/native-crates-*`。画面分支在开局快照上显式揭开迷雾以检查箱子和侧栏；这一观察设置不属于主流程，也不是玩家自然视野或整关验收证据。

## 仍需完成

当前只执行本图脚本实际使用的六种 Powerup；其他原始配置虽保留，不代表其效果已完成。水上箱、附近放置的精确搜索顺序、叠加/替代钱箱边界、驻军与变形矩阵、独立导弹与其他特殊武器倍率、拾取动画和原版升级语音仍需接入或校准。箱子当前沿用已提取的原版 CRATE 地表图像。

Invulnerability 的 Data=1.0 当前按一分钟换算，并复用 CrateRadius 范围。这个换算及铁幕/步兵交互属于待核实推断；专项只证明当前保护与续档机制有效。它没有被计入原机一致性证明。

更新后的静态审查已没有本图未实现的动作类型，仍有 15 条事件条件和 26 条脚本记录尚未接通；明细为本地 `review_out/native-campaign/native-crates-next-mission-audit.json`。条件涉及发现、受击、生产、选中和本地变量，记录存在不等于已经实际可达。第一关仍缺任务媒体、选关/进度，以及从开局到作者结算的完整人工验收。后续应沿实际任务链处理这些缺口，完整复刻目标继续进行。

后续条件批已推进到 World46 / App10 / SourceScene14 / NativeEvents4 / LAN24，并接入发现、受击、生产、选择和局部变量及卸载脚本，详见 [条件与卸载](native-conditions.md)。上文的格式、覆盖数量与测试成绩保留其历史批次范围；自动运输组装和完整任务流程仍未验收。
