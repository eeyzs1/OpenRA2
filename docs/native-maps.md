# 原版地图导入

当前已从本机原版 MIX 导入盟军第一关 `all01t.map` 和苏军第一关 `sov01t.map` 的地形。盟军第一关的 6,336 格地形、658 个 TMP 帧、249 个单位/建筑、344 个地表对象、101 个路径点和 130 组触发器/事件/动作均保留了原始数据。苏军第一关地形为 10,900 格、665 个 TMP 帧。

原始压缩格式另已验证全部 24 张 RA2 战役地图，共 400,474 格；这不表示已经绘制或实现了全部战役。

这是原版关卡还原的地形基础。后续已实例化盟军第一关的全部 249 个初始单位/建筑，见 [原版场景实体](native-actors.md)，并补上 [地表对象与覆盖物](native-scenery.md)、[高架桥双层通行](native-bridges.md) 和 [部分原始事件](native-events.md)。任务尚未加入选关菜单：完整作者事件链与任务行为仍未完成。不能把机制测试中的移动或交接当作第一关已经可通关。

## 使用

在项目根目录执行：

```powershell
python -m pip install -r tools/ra2pack/requirements.txt
python tools/ra2pack/gen_native_maps.py all01t.map sov01t.map
cmake --build build --config Release
ctest --test-dir build -C Release -R 'ra2.native_map' --output-on-failure
```

完整 `gen_assets.py` 也会在对应地图源存在时导入这两张地图；单独导出单位/建筑不会重新导图。生成结果在忽略目录 `assets/maps/native/<原始地图名>/`，原版素材不写入源码仓库。

| 文件 | 内容 |
|---|---|
| `scenario.map` | 与本机 MIX 内文件逐字节相同的原始关卡 |
| `scenario.json` | 保留所有文本节，包括房屋、任务局部规则、建筑/单位状态、脚本和触发器关联 |
| `terrain.nmap` | 原始格坐标、瓦片号/子图、高度、辅助字段、覆盖物类型/状态，以及当前引擎通行映射 |
| `terrain.png` | 以该地图原始剧场、调色板和实际瓦片拼成的地表，含悬崖立面 |
| `terrain-owners.png` | 每个已绘地形像素所属的原始地图格，供迷雾覆盖悬崖和突出图像 |
| `import-report.json` | 地图、TMP、剧场定义、规则与输出的来源和 SHA-256 |

运行 `ra2 --native-map-regression <输出目录>` 会通过实际引擎加载地形、执行移动、续档并保存两种分辨率的画面。它是开发验收入口；普通战役入口仍只显示实际安装的任务。

## 格式和坐标

格式依据为 EA 公开的 [FinalAlert2 地图读写](https://github.com/electronicarts/CNC_TS_and_RA2_Mission_Editor/blob/main/MissionEditor/MapData.cpp)、[打包库](https://github.com/electronicarts/CNC_TS_and_RA2_Mission_Editor/blob/main/MissionEditorPackLib/MissionEditorPackLib.cpp)、[TMP 结构](https://github.com/electronicarts/CNC_TS_and_RA2_Mission_Editor/blob/main/3rdParty/xcc/misc/cc_structures.h) 和 [TMP 地形代码](https://github.com/electronicarts/CNC_TS_and_RA2_Mission_Editor/blob/main/MissionEditor/Defines.h)，并对照 [OpenRA 的导入器](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Cnc/UtilityCommands/ImportGen2MapCommand.cs) 与 [TMP 读取器](https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Mods.Cnc/SpriteLoaders/TmpTSLoader.cs)。代码未直接复制这些实现。

`IsoMapPack5` 是 Base64 后的多个长度前缀 LZO1X 块，每格 11 字节；末尾四个零字节保留为结束标记。两个覆盖物节使用相同块头，内容是 LCW/Format80。各覆盖物的索引为 `x + 512*y`。解码检查输入、输出、回指范围、重复坐标、完整菱形和高度；截断数据不能悄悄变成平地。

`TMP` 帧头偏移 40/41/42（从 0 计）分别为附带高度、地形代码和坡型。TMP 地形代码与游戏 `LandType` 枚举不同：`9` 水、`13` 地面、`14` 粗糙、`15` 不可通行，不能直接套用运行时枚举。帧头的额外图像偏移用于悬崖立面；深度图不作为颜色绘制。

引擎保留原格坐标，以边长 `Map.Size.width + height` 的正方形承载原始菱形，菱形外的格子对陆军、海军与两栖单位均关闭。`LocalSize` 原样保存，尚未用于限制镜头和脚本开图范围。原始 60×30 像素按现有引擎坐标统一缩放为 64×32；高度也按同一比例处理。大于 8000 像素的画布按现有纹理上限整体缩小，分块原分辨率渲染尚未实现。不会用噪声、相邻地图或近似岸线代替缺失瓦片。

当前只支持 RA2 的温带、雪地与城市剧场，选择核心 `ra2.mix` 与 `maps01/02.mix`；不把 YR 或融合规则混入 RA2 地图。高架桥双层通行已有独立实现；原版坡面方向、各移动类型的地表速度和建筑许可仍需进一步接入。现有通行映射不能作为原版寻路完全一致的证明。

## 模块和存档

`map_format.py` 负责原始压缩格式，`gen_native_maps.py` 负责本机素材选择和导出。模拟层 `native_map.*` 只读离线包、验证几何并保存原始格标识；表现层 `game_native_terrain.cpp` 加载原始光栅和像素归属。表现缓存不修改 World 或消耗模拟 RNG。每个 8×8 迷雾像素收集全部原始地形归属，采用其中最暗的迷雾状态，避免只采中心点时漏出相邻未知悬崖。

地形批以 World v30（`RA2WRLDU`）增加格子存在/阻挡标志及原始地图元数据，并全部纳入 checksum；旧地图默认完整可用。实体批增加 World v31 的原始定义和身份；修桥批以 World v33 保留 v32 的分层状态并增加修桥信息，闸门批写入 World v34，事件批写入 World v35（`RA2WRLDZ`），队伍脚本批写入 World v36（`RA2WLD36`），导弹批写入 World v37，队伍命令批写入 World v38，类别攻击批写入 World v39，当前常规武器批写入 World v40（`RA2WLD40`）。应用为 `RA2GAM10`（v10），读取应用 v9/v8/v7；更早应用封套仍明确拒绝。LAN 使用 v16，Welcome 数据布局保持原样，见 [高架桥](native-bridges.md)、[修桥与任务开关](native-bridge-repair.md)、[原生导弹](native-rockets.md) 、[队伍命令](native-team-orders.md) 和 [类别攻击](native-team-attack.md)。

## 第一关后续依赖

对 `all01t.map` 的原始事件盘点发现 14 种事件、32 种动作。现有自制战役的定时刷兵不能替代它：原关涉及 8 个房屋及独立盟友关系、组队/增援、单位附着标签和格标签、禁用/启用触发器、建筑移交、选择/发现/受攻击事件、雷达提示、摄像机和输入锁定、原版语音与文本、局部变量和胜负条件。

初始实体现已接入；接下来应实现任务运行状态，再按原地图事件链验收开场、自由女神像及基地移交、增援、失败条件和任务结束。缺失项继续明确记录，不注册一个只有地形和普通歼灭目标的近似任务。

## 本批验证（2026-11-14）

Release 完整 88 项首次运行中，85 项通过、空战役矩阵 1 项按约定跳过，另 2 项因测试仍查找 v29 标识失败。更新为 v30 并增加 v7/v29 实际封套兼容夹具后，地图、应用/World/战役存档、侦察记忆、固定地图、联机协议及双进程一致性相关 11 项复跑全部通过（33.79 秒）。最终严格缺图/禁止虚构开局检查完成后，地图三项再次通过（7.34 秒）。

ASan 对地图、应用/World/战役存档、侦察记忆及协议的 6 项通过（131.94 秒）；最终地图入口额外通过（31.53 秒）。图形检查包括实际 GPU 地形像素、全部混合归属迷雾像素、两种分辨率的三个场景；实际移动指令在 World 续档后 240 tick 保持一致。当前没有原版任务通关证据。

本机日志为 `build/native-map-full-console.log`、`build/native-map-final-tests.log`、`build/native-map-completion-tests.log`、`build/native-map-audit.txt`、`build-asan/native-map-final-tests.log` 和 `build-asan/native-map-completion-tests.log`；原始任务盘点与 24 图目录位于忽略目录 `review_out/native-campaign/`。
