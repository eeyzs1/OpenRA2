# 战役与脚本存档

应用存档 `RA2GAM10`（v10） 保存 World、完整 AI 决策状态、本地侦察记忆，以及任务难度、阶段、计时期限、目标完成态、触发器的 `fired/armed/enabled`、增援波次、目标文本和胜负结果。兼容应用 v9/v8/v7；更早应用封套明确拒绝加载。World 内部格式为 v36，保存原版地图格子、原始实体定义/身份、房屋关系、分层、修桥意图、桥梁破坏开关、闸门阶段及原始事件实例；本地建筑记忆保存原始类型和观察时的图像帧，见 [原版地图导入](native-maps.md)、[原版场景实体](native-actors.md)、[高架桥](native-bridges.md) 和 [修桥批](native-bridge-repair.md)。

任务存档记录已安装任务定义的指纹，加载时核对全部定义及触发器、目标和波次数量。修改任务或重排选关目录后，旧存档可能被拒绝；不会把原进度应用到另一关。恢复存档不会重复调用 `OnGameStart`，已结束任务保持原胜负。

## Lua 作者约定

已启用的脚本集合须提供 `OnSave()` 和 `OnLoad(data)`。这是一项显式状态约定：脚本作者负责声明所有会影响后续行为的变量，包括局部计数器、已触发标记和实体 ID；游戏不自动转储 Lua VM、函数闭包或执行中的协程。

```lua
local ticks, ambushFired = 0, false

function OnTick(tick)
    ticks = ticks + 1
    -- Normal gameplay hooks may issue ra2 commands.
end

function OnSave()
    return { ticks = ticks, ambushFired = ambushFired }
end

function OnLoad(data)
    assert(math.type(data.ticks) == "integer" and data.ticks >= 0)
    assert(type(data.ambushFired) == "boolean")
    ticks, ambushFired = data.ticks, data.ambushFired
end
```

`OnSave` 必须只读取脚本状态；`OnLoad` 只恢复脚本变量，不能发兵、加钱、设置目标或调用胜负接口。这些 Game/World API 在持久化钩子中会报错。关卡奖励和其他开局动作放在 `OnGameStart`，不能在脚本文件顶层执行。加载源码时可以读取当前对局，以创建声明和函数。

多文件脚本按文件名排序加载，共用现有全局钩子；存档钩子也须组合，收集每个文件的状态，避免后加载文件覆盖前面的结果。默认关闭的 `assets/scripts/*.lua.example` 给出了调用先前 `OnSave/OnLoad` 的组合方式。所有可变局部变量都必须进入这一组合结果；游戏无法识别作者遗漏的变量。

数据支持普通表、字符串（包括内嵌 NUL）、布尔、64 位整数和有限浮点数。表键限字符串、数字和布尔值；共享表引用和循环引用保留。函数、userdata、协程和带元表的表会拒绝保存。数据图最多 4MiB、4096 张表、65536 对键值、64 层嵌套；源码最多 64 个文件、每文件 4MiB、合计 8MiB。

`math.random` / `math.randomseed` 使用独立、确定性的脚本随机数序列，状态由游戏保存，续档后继续同一序列。它不改变 World RNG；无参 `randomseed` 也从当前对局状态确定。持久化钩子不能取随机数或重新播种。恢复初始化可以使用临时脚本 RNG 建立声明，随后恢复为存档中的 RNG 状态，再调用 `OnLoad`。不要让任务逻辑依赖墙上时间、未声明的外部状态或原生 Lua 随机数序列。

## 保存和加载过程

保存先收集数据，并在独立 World 副本和临时 VM 中实际尝试恢复。缺少钩子、无效数据、脚本初始化失败或 `OnLoad` 拒绝状态时，旧存档保留。全部数据写入临时文件并刷新后，才替换目标存档。

加载先读取和验证元数据、任务状态、脚本状态、AI、侦察和 World。随后在只读临时 VM 中加载当前安装的源码，并与存档记录逐字节比对；一致后才调用 `OnLoad`。存档内文本只作比对，绝不执行，也不加载存档中的字节码。全部成功后才替换当前对局，并将新 VM 绑定到新 World；失败时保留当前 World、任务状态和旧 VM。

## 验证范围

```powershell
ctest --test-dir build -C Release -R '^ra2.campaign_save$' --output-on-failure
```

该专项主动加载 `tests/fixtures/campaign-save`，默认游戏不会安装此内容。它通过真实触发器推进阶段、计时、目标、启用标记和增援，对比中途存档后的 220 个 tick；同时检查脚本局部状态、随机序列、整数/二进制字符串/共享和循环表、开局奖励不重复、倒计时失败、已结束任务及换遭遇战的隔离。还覆盖损坏存档、源码不匹配、钩子错误和非法修改对局等失败路径。

这是独立机制验收。2026-11-15 原版第一关入口另由 `ra2.native_campaign` 验证，正常指令通关由 `ra2.native_playthrough` 验证，不能用本夹具代替这两项。

## 实体句柄与 World49

当前 World49 保存存活及已退休槽位的出生代次，旧 World 文件迁移为代次一。应用加载仍清空选中、编队及未完成的来源选择，不把旧界面引用带入新世界。Lua API 返回包含代次的非负整数实体句柄；`OnSave`/`OnLoad` 应原样保留 Lua 整数，不能拆成低位编号或转换为浮点数。单实体与批量操作均验证完整句柄，已死亡句柄保持失效，当前存档中的存活句柄仍可继续下令。

原版入口的 `NativeScene`、CSF 文件与提取后的文字、目标完成触发器都进入任务指纹；旧的自定义任务未声明这些字段时保持原指纹。原版目标清单从 World 已保存的触发器次数及作者胜利结果更新。读档不会重新执行开局或重播已消费的演出请求；语音文件在对局载入后预载。
